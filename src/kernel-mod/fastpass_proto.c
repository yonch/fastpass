/*
 * fastpass_proto.c
 *
 *  Created on: Nov 13, 2013
 *      Author: yonch
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <net/protocol.h>
#include <net/ip.h>
#include <net/inet_common.h>
#include <net/inet_hashtables.h>
#include <net/sch_generic.h>
#include <net/pkt_sched.h>

#include "fastpass_proto.h"
#include "outwnd.h"

#undef FASTPASS_PERFORM_RUNTIME_TESTS

struct inet_hashinfo fastpass_hashinfo;
EXPORT_SYMBOL_GPL(fastpass_hashinfo);
#define FASTPASS_EHASH_NBUCKETS 16

#ifdef CONFIG_IP_FASTPASS_DEBUG
bool fastpass_debug;
module_param(fastpass_debug, bool, 0644);
MODULE_PARM_DESC(fastpass_debug, "Enable debug messages");

EXPORT_SYMBOL_GPL(fastpass_debug);
#endif

static struct kmem_cache *fpproto_pktdesc_cachep __read_mostly;


/* locks the qdisc associated with the fastpass socket */
static struct Qdisc *fpproto_lock_qdisc(struct sock *sk)
{
	struct fastpass_sock *fp = fastpass_sk(sk);
	struct Qdisc *sch;
	spinlock_t *root_lock;

	rcu_read_lock_bh();

	sch = rcu_dereference_bh(fp->qdisc);

	if (unlikely(sch == NULL))
		goto unsuccessful;

	root_lock = qdisc_lock(qdisc_root(sch));
	spin_lock_bh(root_lock);

	/* Check that the qdisc destroy func didn't race ahead of us */
	if (unlikely(sch->limit == 0))
		goto raced_with_destroy;

	return sch;

raced_with_destroy:
	spin_unlock_bh(root_lock);
unsuccessful:
	rcu_read_unlock_bh();
	return NULL;
}

/* unlocks the qdisc associated with the fastpass socket */
static void fpproto_unlock_qdisc(struct Qdisc *sch)
{
	if (unlikely(sch == NULL))
		return;

	spin_unlock_bh(qdisc_lock(qdisc_root(sch)));
	rcu_read_unlock_bh();
}

/* configures which qdisc is associated with the fastpass socket */
void fpproto_set_qdisc(struct sock *sk, struct Qdisc *new_qdisc)
{
	struct fastpass_sock *fp = fastpass_sk(sk);
	rcu_assign_pointer(fp->qdisc, new_qdisc);
}

struct fpproto_pktdesc *fpproto_pktdesc_alloc(void)
{
	struct fpproto_pktdesc *pd;
	pd = kmem_cache_zalloc(fpproto_pktdesc_cachep, GFP_ATOMIC | __GFP_NOWARN);
	return pd;
}

void fpproto_pktdesc_free(struct fpproto_pktdesc *pd)
{
	kmem_cache_free(fpproto_pktdesc_cachep, pd);
}


void do_ack_seqno(struct fastpass_sock *fp, u64 seqno)
{
	struct fpproto_pktdesc *pd;

	BUG_ON(time_after_eq64(seqno, fp->next_seqno));
	BUG_ON(time_before64(seqno, fp->next_seqno - FASTPASS_OUTWND_LEN));

	fastpass_pr_debug("ACK seqno 0x%08llX\n", seqno);
	fp->stat_acked_packets++;
	BUG_ON(!outwnd_is_unacked(fp, seqno));
	pd = outwnd_pop(fp, seqno);

	if (fp->ops->handle_ack)
		fp->ops->handle_ack(fp->qdisc, pd);		/* will free pd */
	else
		fpproto_pktdesc_free(pd);
}

void do_neg_ack_seqno(struct fastpass_sock *fp, u64 seq)
{
	struct fpproto_pktdesc *pd = outwnd_pop(fp, seq);
	fastpass_pr_debug("Unacked tx seq 0x%llX\n", seq);
	if (fp->ops->handle_neg_ack)
		fp->ops->handle_neg_ack(fp->qdisc, pd);		/* will free pd */
	else
		fpproto_pktdesc_free(pd);
}

void cancel_and_reset_retrans_timer(struct fastpass_sock *fp)
{
	u64 timeout;
	u64 seqno;

	if (unlikely(hrtimer_try_to_cancel(&fp->retrans_timer) == -1)) {
		fastpass_pr_debug("could not cancel timer. tasklet will reset timer\n");
		return;
	}

	if (outwnd_empty(fp)) {
		fastpass_pr_debug("all packets acked, no need to set timer\n");
		return;
	}

	/* find the earliest unacked, and the timeout */
	seqno = outwnd_earliest_unacked(fp);
	timeout = outwnd_timestamp(fp, seqno) + fp->send_timeout_us;

	/* set timer and earliest_unacked */
	fp->earliest_unacked = seqno;
	hrtimer_start(&fp->retrans_timer, ns_to_ktime(timeout), HRTIMER_MODE_ABS);
	fp->stat_reprogrammed_timer++;
	fastpass_pr_debug("setting timer to %llu for seq#=0x%llX\n", timeout, seqno);
}

static enum hrtimer_restart retrans_timer_func(struct hrtimer *timer)
{
	struct fastpass_sock *fp =
			container_of(timer, struct fastpass_sock, retrans_timer);

	/* schedule tasklet to write request */
	tasklet_schedule(&fp->retrans_tasklet);

	return HRTIMER_NORESTART;
}

static void retrans_tasklet(unsigned long int param)
{
	struct fastpass_sock *fp = (struct fastpass_sock *)param;
	u64 now = fp_get_time_ns();
	struct Qdisc *sch;
	u64 seqno;
	u64 timeout;

	/* Lock the qdisc */
	sch = fpproto_lock_qdisc((struct sock *)fp);
	if (unlikely(sch == NULL))
		goto qdisc_destroyed;

	fp->stat_tasklet_runs++;

	/* notify qdisc of expired timeouts */
	seqno = fp->earliest_unacked;
	while (!outwnd_empty(fp)) {
		/* find seqno and timeout of next unacked packet */
		seqno = outwnd_earliest_unacked_hint(fp, seqno);
		timeout = outwnd_timestamp(fp, seqno) + fp->send_timeout_us;

		/* if timeout hasn't expired, we're done */
		if (unlikely(time_after64(timeout, now)))
			goto set_next_timer;

		fp->stat_timeout_pkts++;
		do_neg_ack_seqno(fp, seqno);
	}
	fastpass_pr_debug("outwnd empty, not setting timer\n");
	goto out; /* outwnd is empty */

set_next_timer:
	/* seqno is the earliest unacked seqno, and timeout is its timeout */
	fp->earliest_unacked = seqno;
	hrtimer_start(&fp->retrans_timer, ns_to_ktime(timeout), HRTIMER_MODE_ABS);
	fastpass_pr_debug("setting timer to %llu for seq#=0x%llX\n", timeout, seqno);

out:
	fpproto_unlock_qdisc(sch);
	return;

qdisc_destroyed:
	fastpass_pr_debug("qdisc seems to have been destroyed\n");
	return;
}

static void do_proto_reset(struct fastpass_sock *fp, u64 reset_time,
		bool in_sync)
{
	u32 time_hash = jhash_1word((u32)reset_time, reset_time >> 32);
	u64 base_seqno = reset_time + time_hash + ((u64)time_hash << 32);

	/* clear unacked packets */
	outwnd_reset(fp);

	/* set new sequence numbers */
	fp->last_reset_time = reset_time;
	fp->next_seqno = base_seqno;
	wnd_reset(&fp->inwnd, base_seqno + 0xDEADBEEF);
	fp->consecutive_bad_pkts = 0;

	/* are we in sync? */
	fp->in_sync = in_sync;
}

static bool tstamp_in_window(u64 tstamp, u64 win_middle, u64 win_size) {
	return (tstamp >= win_middle - (win_size / 2))
			&& (tstamp < win_middle + ((win_size + 1) / 2));
}

static void fpproto_handle_reset(struct fastpass_sock *fp,
		struct Qdisc *sch, u64 partial_tstamp)
{

	u64 now = fp_get_time_ns();

	u64 full_tstamp = now - (1ULL << 55);
	full_tstamp += (partial_tstamp - full_tstamp) & ((1ULL << 56) - 1);

	fastpass_pr_debug("got RESET 0x%llX, last is 0x%llX, full 0x%llX, now 0x%llX\n",
			partial_tstamp, fp->last_reset_time, full_tstamp, now);

	if (full_tstamp == fp->last_reset_time) {
		if (!fp->in_sync) {
			fp->in_sync = 1;
			fastpass_pr_debug("Now in sync\n");
		} else {
			fp->stat_redundant_reset++;
			fastpass_pr_debug("received redundant reset\n");
		}
		return;
	}

	/* reject resets outside the time window */
	if (unlikely(!tstamp_in_window(full_tstamp, now, fp->rst_win_ns))) {
		fastpass_pr_debug("Reset was out of reset window (diff=%lld)\n",
				(s64)full_tstamp - (s64)now);
		fp->stat_reset_out_of_window++;
		return;
	}

	/* if we already processed a newer reset within the window */
	if (unlikely(tstamp_in_window(fp->last_reset_time, now, fp->rst_win_ns)
			&& time_before64(full_tstamp, fp->last_reset_time))) {
		fastpass_pr_debug("Already processed reset within window which is %lluns more recent\n",
						fp->last_reset_time - full_tstamp);
		fp->stat_outdated_reset++;
		return;
	}

	/* okay, accept the reset */
	do_proto_reset(fp, full_tstamp, true);
	if (fp->ops->handle_reset)
		fp->ops->handle_reset(sch);
}

/**
 * Acks a single timeslot.
 * Assumes timeslot is within the window and has not been acked yet.
 */
void fpproto_handle_ack(struct fastpass_sock *fp,
		struct Qdisc *sch, u16 ack_seq, u32 ack_runlen)
{
	u64 cur_seqno;
	s32 next_unacked;
	u64 end_seqno;
	int n_acked = 0;

	fp->stat_ack_payloads++;

	/* find full seqno, strictly before fp->next_seqno */
	cur_seqno = fp->next_seqno - (1 << 16);
	cur_seqno += (ack_seq - cur_seqno) & 0xFFFF;

	/* is the seqno within the window? */
	if (time_before64(cur_seqno, fp->next_seqno - FASTPASS_OUTWND_LEN))
		goto ack_too_early;

	/* if the ack_seq is unacknowledged, process the ack on it */
	if (outwnd_is_unacked(fp, cur_seqno)) {
		do_ack_seqno(fp, cur_seqno);
		n_acked++;
	}
	end_seqno = cur_seqno - 1;

	/* start with the positive nibble */
	ack_runlen <<= 4;

do_next_positive:
	cur_seqno = end_seqno;
	end_seqno -= (ack_runlen >> 28);
	ack_runlen <<= 4;
do_next_unacked:
	/* find next unacked */
	next_unacked = outwnd_at_or_before(fp, cur_seqno);
	if (next_unacked == -1)
		goto done;
	cur_seqno -= next_unacked;

	if (likely(time_after64(cur_seqno, end_seqno))) {
		/* got ourselves an unacked seqno that should be acked */
		do_ack_seqno(fp, cur_seqno);
		n_acked++;
		/* try to find another seqno that should be acked */
		goto do_next_unacked;
	}
	/* finished handling this run. if more runs, handle them as well */
	if (likely(ack_runlen != 0)) {
		end_seqno -= (ack_runlen >> 28);
		ack_runlen <<= 4;
		goto do_next_positive; /* continue handling */
	}
done:
	if (n_acked > 0) {
		cancel_and_reset_retrans_timer(fp);
		fp->stat_informative_ack_payloads++;
	}
	return;

ack_too_early:
	fastpass_pr_debug("too_early_ack: earliest %llu, got %llu\n",
			fp->next_seqno - FASTPASS_OUTWND_LEN, cur_seqno);
	fp->stat_too_early_ack++;
}

/**
 * Receives a packet destined for the protocol. (part of inet socket API)
 */
__sum16 fpproto_checksum(struct sock *sk, struct sk_buff *skb, u64 seqno)
{
	const struct inet_sock *inet = inet_sk(sk);

	u32 seq_hash = jhash_1word((u32)seqno, seqno >> 32);

	skb->csum = skb_checksum(skb, 0, skb->len, seq_hash);
	return csum_tcpudp_magic(inet->inet_saddr, inet->inet_daddr,
			skb->len, IPPROTO_FASTPASS, skb->csum);
}

void fpproto_egress_checksum(struct sock *sk, struct sk_buff *skb,
		u64 seqno)
{
	fastpass_hdr(skb)->checksum = fpproto_checksum(sk, skb, seqno);
}

void got_good_packet(struct fastpass_sock *fp, struct Qdisc *sch)
{
	fp->consecutive_bad_pkts = 0;
}

void got_bad_packet(struct fastpass_sock *fp, struct Qdisc *sch)
{
	u64 now = fp_get_time_ns();

	fp->consecutive_bad_pkts++;
	fastpass_pr_debug("#%u consecutive bad packets\n", fp->consecutive_bad_pkts);

	if (fp->consecutive_bad_pkts < FASTPASS_BAD_PKT_RESET_THRESHOLD)
		goto out;

	/* got too many bad packets */

	/* reset bad packet count to 0 for continued operation */
	fp->consecutive_bad_pkts = 0;

	/* was there a recent reset? */
	if (time_in_range64(
			now - FASTPASS_RESET_WINDOW_NS,
			fp->last_reset_time,
			now + FASTPASS_RESET_WINDOW_NS)) {
		/* will not trigger a new one */
		fp->stat_no_reset_because_recent++;
		fastpass_pr_debug("had a recent reset (last %llu, now %llu). not issuing a new one.\n",
				fp->last_reset_time, now);
	} else {
		/* Will send a RSTREQ */
		fp->stat_reset_from_bad_pkts++;
		do_proto_reset(fp, now, false);
		if (fp->ops->handle_reset)
			fp->ops->handle_reset(sch);
	}

out:
	/* Whatever happens, trigger an outgoing packet to make progress */
	if (fp->ops->trigger_request)
		fp->ops->trigger_request(sch, now);
}

/**
 * Updates the incoming packet window
 * @return 0 if update is successful
 * 		   1 if caller should drop the packet with seqno
 */
int update_inwnd(struct fastpass_sock *fp, u64 seqno)
{
	struct fp_window *inwnd = &fp->inwnd;
	u64 head = wnd_head(inwnd);
	u64 pos;
	u64 gap;

	if (likely(seqno == head + 1)) {
		/* handle a packet sliding off the end of the window */
		if (wnd_is_marked(inwnd, head - FASTPASS_WND_LEN + 1))
			wnd_clear(inwnd, head - FASTPASS_WND_LEN + 1); /* TODO: NACK it? */

		/* advance the window (no marking since seqno has been delivered) */
		wnd_advance(inwnd, 1);

		return 0;
	}

	if (likely(time_after64(seqno, head))) {
		/* no overlap between new window and current window? */
		if (unlikely(time_after_eq64(seqno, head + FASTPASS_WND_LEN)))
			goto trigger_reset;

		/* first clear all bits going out of the window */
		pos = seqno - FASTPASS_WND_LEN;
		while((gap = wnd_at_or_before(inwnd, pos)) >= 0) {
			pos -= gap;
			wnd_clear(inwnd, pos);
			/* TODO: also NACK? */
		}

		/* okay, advance window */
		wnd_advance(inwnd, seqno - head);

		/* mark all packets in range (head, seqno), since we hadn't seen them */
		/* note: we checked seqno > head + 1 */
		wnd_mark_bulk(inwnd, head + 1, seqno - head - 1);

		/* TODO: maybe we need to NACK some of the newly marked seqnos as well? */
		return 0;
	}

	if (unlikely(time_before_eq64(seqno, head - FASTPASS_WND_LEN)))
		/* packet is before the window - we already received/NACKed it */
		return 1; /* drop! */

	/* seqno is in the current window - had we received/NACKed it already */
	if (!wnd_is_marked(inwnd, seqno))
		return 1; /* already received/NACKed - drop! */

	/* accept the packet - we mark it as received for future */
	wnd_clear(inwnd, seqno);
	return 0;

trigger_reset:
	/* TODO */;
	return 1; /* we reset -- drop the packet */
}

int fpproto_rcv(struct sk_buff *skb)
{
	struct sock *sk;
	struct fastpass_sock *fp;
	struct fastpass_hdr *hdr;
	struct Qdisc *sch;
	u64 full_seqno;
	u16 payload_type;
	u64 rst_tstamp;
	__sum16 checksum;
	int i;
	unsigned char *data;
	unsigned char *data_end;
	int alloc_n_dst, alloc_n_tslots;
	u16 alloc_dst[16];
	u32 alloc_base_tslot;
	u16 ack_seq;
	u32 ack_runlen;

	sk = __inet_lookup_skb(&fastpass_hashinfo, skb,
			FASTPASS_DEFAULT_PORT_NETORDER /*sport*/,
			FASTPASS_DEFAULT_PORT_NETORDER /*dport*/);
	fp = fastpass_sk(sk);

	if (sk == NULL) {
		fastpass_pr_debug("got packet on non-connected socket\n");
		kfree_skb(skb);
		return 0;
	}

	sch = fpproto_lock_qdisc(sk);
	if (unlikely(sch == NULL)) {
		fastpass_pr_debug("qdisc seems to have been destroyed\n");
		kfree_skb(skb);
		return 0;
	}

	fp->stat_rx_pkts++;

	if (skb->len < 5)
		goto packet_too_short;

	hdr = (struct fastpass_hdr *)skb->data;
	data = &skb->data[4];
	data_end = &skb->data[skb->len];
	payload_type = *data >> 4;

	/* get full 64-bit sequence number */
	full_seqno = wnd_head(&fp->inwnd) - (1 << 14);
	full_seqno += (ntohs(hdr->seq) - full_seqno) & 0xFFFF;
	fastpass_pr_debug("packet with seqno 0x%04X (full 0x%llX, prev_max 0x%llX)\n",
			ntohs(hdr->seq), full_seqno, wnd_head(&fp->inwnd));

	/* verify checksum */
	checksum = fpproto_checksum(sk, skb, full_seqno);
	if (unlikely(checksum != 0)) {
		fastpass_pr_debug("checksum error. expected 0, got 0x%04X\n", checksum);
		got_bad_packet(fp, sch);
		// goto bad_checksum;
		/* TODO: drop the packet, once RESET handling is fixed */
	} else {
		got_good_packet(fp, sch);
	}

	/* update inwnd */
	if ((payload_type != FASTPASS_PTYPE_RESET) && (update_inwnd(fp, full_seqno) != 0))
		goto cleanup; /* need to drop packet */


handle_payload:
	/* at this point we know there is at least one byte remaining */
	payload_type = *data >> 4;

	switch (payload_type) {
	case FASTPASS_PTYPE_RESET:
		if (data + 8 > data_end)
			goto incomplete_reset_payload;

		rst_tstamp = ((u64)(ntohl(*(u32 *)data) & ((1 << 24) - 1)) << 32) |
				ntohl(*(u32 *)(data + 4));

		fpproto_handle_reset(fp, sch, rst_tstamp);

		data += 8;
		break;

	case FASTPASS_PTYPE_ALLOC:
		if (data + 2 > data_end)
			goto incomplete_alloc_payload_one_byte;

		payload_type = ntohs(*(u16 *)data);
		alloc_n_dst = (payload_type >> 8) & 0xF;
		alloc_n_tslots = 2 * (payload_type & 0x3F);
		data += 2;

		if (data + 2 + 2 * alloc_n_dst + alloc_n_tslots > data_end)
			goto incomplete_alloc_payload;

		/* get base timeslot */
		alloc_base_tslot = ntohs(*(u16 *)data);
		alloc_base_tslot <<= 4;
		data += 2;

		/* convert destinations from network byte-order */
		for (i = 0; i < alloc_n_dst; i++, data += 2)
			alloc_dst[i] = ntohs(*(u16 *)data);

		/* process the payload */
		if (fp->ops->handle_alloc)
			fp->ops->handle_alloc(sch, alloc_base_tslot, alloc_dst, alloc_n_dst,
				data, alloc_n_tslots);

		data += alloc_n_tslots;
		break;
	case FASTPASS_PTYPE_ACK:
		if (data + 6 > data_end)
			goto incomplete_ack_payload;

		ack_runlen = ntohl(*(u32 *)data);
		ack_seq = ntohs(*(u16 *)(data + 4));

		fpproto_handle_ack(fp, sch, ack_seq, ack_runlen);

		data += 6;
		break;
	default:
		goto unknown_payload_type;
	}

	/* more payloads in packet? */
	if (data < data_end)
		goto handle_payload;



cleanup:
	fpproto_unlock_qdisc(sch);
	sock_put(sk);
	__kfree_skb(skb);
	return NET_RX_SUCCESS;

unknown_payload_type:
	fp->stat_rx_unknown_payload++;
	fastpass_pr_debug("got unknown payload type %d\n", payload_type);
	goto cleanup;

incomplete_reset_payload:
	fp->stat_rx_incomplete_reset++;
	fastpass_pr_debug("RESET payload incomplete, expected 8 bytes, got %d\n",
			(int)(data_end - data));
	goto cleanup;

incomplete_alloc_payload_one_byte:
	fp->stat_rx_incomplete_alloc++;
	fastpass_pr_debug("ALLOC payload incomplete, only got one byte\n");
	goto cleanup;

incomplete_alloc_payload:
	fp->stat_rx_incomplete_alloc++;
	fastpass_pr_debug("ALLOC payload incomplete: expected %d bytes, got %d\n",
			2 + 2 * alloc_n_dst + alloc_n_tslots, (int)(data_end - data));
	goto cleanup;

incomplete_ack_payload:
	fp->stat_rx_incomplete_ack++;
	fastpass_pr_debug("ACK payload incomplete: expected 6 bytes, got %d\n",
			(int)(data_end - data));
	goto cleanup;

bad_checksum:
	fp->stat_checksum_error++;
	fastpass_pr_debug("checksum error. expected 0, got 0x%04X\n", checksum);
	goto cleanup;

packet_too_short:
	fp->stat_rx_too_short++;
	fastpass_pr_debug("packet less than minimal size (len=%d)\n", skb->len);
	goto cleanup;
}

/**
 * Connect a socket.
 * Based on ip4_datagram_connect, with appropriate locking and additional
 *    sanity checks
 */
int fpproto_connect(struct sock *sk, struct sockaddr *uaddr, int addr_len)
{
	struct fastpass_sock *fp = fastpass_sk(sk);
	struct inet_sock *inet = inet_sk(sk);
	struct sockaddr_in *usin = (struct sockaddr_in *) uaddr;
	struct flowi4 *fl4;
	struct rtable *rt;
	__be32 saddr;
	int oif;
	int err;

	if (addr_len < sizeof(*usin))
		return -EINVAL;

	if (usin->sin_family != AF_INET)
		return -EAFNOSUPPORT;

	if (fp->ops == NULL)
		return -EINVAL;

	sk_dst_reset(sk);

	bh_lock_sock(sk);

	oif = sk->sk_bound_dev_if;
	saddr = inet->inet_saddr;
	if (ipv4_is_multicast(usin->sin_addr.s_addr)) {
		if (!oif)
			oif = inet->mc_index;
		if (!saddr)
			saddr = inet->mc_addr;
	}
	fl4 = &inet->cork.fl.u.ip4;
	rt = ip_route_connect(fl4, usin->sin_addr.s_addr, saddr,
			      RT_CONN_FLAGS(sk), oif,
			      sk->sk_protocol,
			      inet->inet_sport, usin->sin_port, sk, true);
	if (IS_ERR(rt)) {
		err = PTR_ERR(rt);
		if (err == -ENETUNREACH)
			IP_INC_STATS_BH(sock_net(sk), IPSTATS_MIB_OUTNOROUTES);
		goto out;
	}

	if ((rt->rt_flags & RTCF_BROADCAST) && !sock_flag(sk, SOCK_BROADCAST)) {
		ip_rt_put(rt);
		err = -EACCES;
		goto out;
	}
	if (!inet->inet_saddr)
		inet->inet_saddr = fl4->saddr;	/* Update source address */
	if (!inet->inet_rcv_saddr) {
		inet->inet_rcv_saddr = fl4->saddr;
		if (sk->sk_prot->rehash)
			sk->sk_prot->rehash(sk);
	}
	inet->inet_daddr = fl4->daddr;
	inet->inet_dport = usin->sin_port;
	sk->sk_state = TCP_ESTABLISHED;
	inet->inet_id = jiffies;

	sk_dst_set(sk, &rt->dst);
	err = 0;
out:
	bh_unlock_sock(sk);
	return err;
}

/* close the socket */
static void fpproto_close(struct sock *sk, long timeout)
{
	fastpass_pr_debug("visited\n");

	sk_common_release(sk);
}

/* disconnect (happens if called connect with AF_UNSPEC family) */
static int fpproto_disconnect(struct sock *sk, int flags)
{
	struct inet_sock *inet = inet_sk(sk);

	fastpass_pr_debug("visited\n");

	sk->sk_state = TCP_CLOSE;
	inet->inet_daddr = 0;
	inet->inet_dport = 0;
	sock_rps_reset_rxhash(sk);
	sk->sk_bound_dev_if = 0;
	if (!(sk->sk_userlocks & SOCK_BINDADDR_LOCK))
		inet_reset_saddr(sk);

	if (!(sk->sk_userlocks & SOCK_BINDPORT_LOCK)) {
		sk->sk_prot->unhash(sk);
		inet->inet_sport = 0;
	}
	sk_dst_reset(sk);

	return 0;
}

static void fpproto_destroy_sock(struct sock *sk)
{
	struct fastpass_sock *fp = fastpass_sk(sk);

	fastpass_pr_debug("visited\n");

	/* might not be necessary, doing for safety */
	fpproto_set_qdisc(sk, NULL);

	/* clear unacked packets */
	outwnd_reset(fp);

	/* eliminate the retransmission timer */
	hrtimer_cancel(&fp->retrans_timer);

	/* kill tasklet */
	tasklet_kill(&fp->retrans_tasklet);

}

/**
 * Handles a case of a packet that seems to have not been delivered to
 *    controller successfully, either because of falling off the outwnd end,
 *    or a timeout.
 */
/**
 * Make sure fpproto is ready to accept a new packet.
 *
 * This might NACK a packet.
 */
void fpproto_prepare_to_send(struct sock *sk)
{
	struct fastpass_sock *fp = fastpass_sk(sk);
	u64 window_edge = fp->next_seqno - FASTPASS_OUTWND_LEN;

	/* make sure outwnd is not holding a packet descriptor where @pd will be */
	if (outwnd_is_unacked(fp, window_edge)) {
		/* treat packet going out of outwnd as if it was dropped */
		fp->stat_fall_off_outwnd++;
		do_neg_ack_seqno(fp, window_edge);

		/* reset timer if needed */
		cancel_and_reset_retrans_timer(fp);
	}
}

/**
 * Protocol will commit to guaranteeing the given packet is delivered.
 *
 * A sequence number is allocated to the packet, and timeouts will reflect the
 *    packet.
 *
 * @pd: the packet
 * @now: send timestamp from which timeouts are computed
 */
void fpproto_commit_packet(struct sock *sk, struct fpproto_pktdesc *pd,
		u64 timestamp)
{
	struct fastpass_sock *fp = fastpass_sk(sk);

	pd->sent_timestamp = timestamp;
	pd->seqno = fp->next_seqno;
	pd->send_reset = !fp->in_sync;
	pd->reset_timestamp = fp->last_reset_time;

	/* add packet to outwnd, will advance fp->next_seqno */
	outwnd_add(fp, pd);

	/* if first packet in outwnd, enqueue timer and set fp->earliest_unacked */
	if (fp->tx_num_unacked == 1) {
		u64 timeout = pd->sent_timestamp + fp->send_timeout_us;
		fp->earliest_unacked = pd->seqno;
		hrtimer_start(&fp->retrans_timer, ns_to_ktime(timeout), HRTIMER_MODE_ABS);
		fastpass_pr_debug("first packet in outwnd. setting timer to %llu for seq#=0x%llX\n", timeout, pd->seqno);
	}
}

/**
 * Constructs and sends one packet.
 */
void fpproto_send_packet(struct sock *sk, struct fpproto_pktdesc *pd)
{
	struct fastpass_sock *fp = fastpass_sk(sk);
	struct inet_sock *inet = inet_sk(sk);
	int max_header;
	int payload_len;
	struct sk_buff *skb = NULL;
	int err;
	int i;
	u8 *data;
	struct fastpass_areq *areq;

	/* calculate byte size in packet*/
	payload_len = 4 /* header */
				+ 8 * (pd->send_reset) /* RESET */
				+ 2 + 4 * pd->n_areq /* A-REQ */;
	max_header = sk->sk_prot->max_header;

	/* allocate request skb */
	skb = sock_alloc_send_skb(sk, payload_len + max_header, 1, &err);
	if (!skb)
		goto alloc_err;

	/* reserve space for headers */
	skb_reserve(skb, max_header);
	/* set skb fastpass packet size */
	skb_put(skb, payload_len);

	data = &skb->data[0];

	/* header */
	skb_reset_transport_header(skb);
	*(__be16 *)data = htons((u16)(pd->seqno));
	data += 2;
	*(__be16 *)data = 0; /* checksum */
	data += 2;

	/* RESET */
	if (pd->send_reset) {
		u32 hi_word;
		hi_word = (FASTPASS_PTYPE_RSTREQ << 28) |
					((pd->reset_timestamp >> 32) & 0x00FFFFFF);
		*(__be32 *)data = htonl(hi_word);
		*(__be32 *)(data + 4) = htonl((u32)pd->reset_timestamp);
		data += 8;
	}

	/* A-REQ type short */
	*(__be16 *)data = htons((FASTPASS_PTYPE_AREQ << 12) |
					  (pd->n_areq & 0x3F));
	data += 2;

	/* A-REQ requests */
	for (i = 0; i < pd->n_areq; i++) {
		areq = (struct fastpass_areq *)data;
		areq->dst = htons((__be16)pd->areq[i].src_dst_key);
		areq->count = htons((u16)pd->areq[i].tslots);
		data += 4;
	}

	fastpass_pr_debug("sending packet\n");

	bh_lock_sock(sk);

	/* checksum */
	fpproto_egress_checksum(sk, skb, pd->seqno);

	/* send onwards */
	err = ip_queue_xmit(skb, &inet->cork.fl);
	err = net_xmit_eval(err);
	if (unlikely(err != 0)) {
		fp->stat_xmit_errors++;
		fastpass_pr_debug("got error %d from ip_queue_xmit\n", err);
	}

	bh_unlock_sock(sk);
	return;

alloc_err:
	fp->stat_skb_alloc_error++;
	fastpass_pr_debug("could not alloc skb of size %d\n",
			payload_len + max_header);
	/* no need to unlock */
}

static int fpproto_sk_init(struct sock *sk)
{
	struct fastpass_sock *fp = fastpass_sk(sk);

	fastpass_pr_debug("visited\n");

	/* bind all sockets to port 1, to avoid inet_autobind */
	inet_sk(sk)->inet_num = ntohs(FASTPASS_DEFAULT_PORT_NETORDER);
	inet_sk(sk)->inet_sport = FASTPASS_DEFAULT_PORT_NETORDER;

	fp->mss_cache = 536;

	fpproto_set_qdisc(sk, NULL);

	/* initialize outwnd */
	memset(fp->bin_mask, 0, sizeof(fp->bin_mask));
#ifdef FASTPASS_PERFORM_RUNTIME_TESTS
	outwnd_test(fp);
#endif
	/* initialize retransmission timer */
	hrtimer_init(&fp->retrans_timer, CLOCK_REALTIME, HRTIMER_MODE_ABS);
	fp->retrans_timer.function = retrans_timer_func;

	/* initialize tasklet */
	tasklet_init(&fp->retrans_tasklet, &retrans_tasklet,
			(unsigned long int)fp);


	/* choose reset time */
	do_proto_reset(fp, fp_get_time_ns(), false);

	return 0;
}

static int fpproto_userspace_sendmsg(struct kiocb *iocb, struct sock *sk,
		struct msghdr *msg, size_t len)
{
	return -ENOTSUPP;
}

/* implementation of userspace recv() - unsupported */
static int fpproto_userspace_recvmsg(struct kiocb *iocb, struct sock *sk,
		struct msghdr *msg, size_t len, int noblock, int flags, int *addr_len)
{
	return -ENOTSUPP;
}

/* backlog_rcv - should never be called */
static int fpproto_backlog_rcv(struct sock *sk, struct sk_buff *skb)
{
	BUG();
	return 0;
}

static inline void fpproto_hash(struct sock *sk)
{
	fastpass_pr_debug("visited\n");
	inet_hash(sk);
}
static void fpproto_unhash(struct sock *sk)
{
	fastpass_pr_debug("visited\n");
	inet_unhash(sk);
}
static void fpproto_rehash(struct sock *sk)
{
	fastpass_pr_debug("before %X\n", sk->sk_hash);
	sk->sk_prot->unhash(sk);

	sk->sk_state = TCP_ESTABLISHED;
	inet_sk(sk)->inet_dport = FASTPASS_DEFAULT_PORT_NETORDER;
	inet_sk(sk)->inet_daddr = inet_sk(sk)->cork.fl.u.ip4.daddr;
	sk->sk_prot->hash(sk);

	fastpass_pr_debug("after %X\n", sk->sk_hash);
}
static int fpproto_bind(struct sock *sk, struct sockaddr *uaddr,
		int addr_len)
{
	return -ENOTSUPP;
}

/* The interface for receiving packets from IP */
struct net_protocol fastpass_protocol = {
	.handler = fpproto_rcv,
	.no_policy = 1,
	.netns_ok = 1,
};

/* Interface for AF_INET socket operations */
struct proto	fastpass_prot = {
	.name = "FastPass",
	.owner = THIS_MODULE,
	.close		   = fpproto_close,
	.connect	   = fpproto_connect,
	.disconnect	   = fpproto_disconnect,
	.init		   = fpproto_sk_init,
	.destroy	   = fpproto_destroy_sock,
	.setsockopt	   = ip_setsockopt,
	.getsockopt	   = ip_getsockopt,
	.sendmsg	   = fpproto_userspace_sendmsg,
	.recvmsg	   = fpproto_userspace_recvmsg,
	.bind		   = fpproto_bind,
	.backlog_rcv   = fpproto_backlog_rcv,
	.hash		   = fpproto_hash,
	.unhash		   = fpproto_unhash,
	.rehash		   = fpproto_rehash,
	.max_header	   = MAX_TOTAL_FASTPASS_HEADERS,
	.obj_size	   = sizeof(struct fastpass_sock),
	.slab_flags	   = SLAB_DESTROY_BY_RCU,
	.h.hashinfo		= &fastpass_hashinfo,
#ifdef CONFIG_COMPAT
	.compat_setsockopt = compat_ip_setsockopt,
	.compat_getsockopt = compat_ip_getsockopt,
#endif
	.clear_sk	   = sk_prot_clear_portaddr_nulls,
};
EXPORT_SYMBOL(fastpass_prot);

static struct inet_protosw fastpass4_protosw = {
	.type		=  SOCK_DGRAM,
	.protocol	=  IPPROTO_FASTPASS,
	.prot		=  &fastpass_prot,
	.ops		=  &inet_dgram_ops,
	.no_check	=  0,		/* must checksum (RFC 3828) */
	.flags		=  0,
};

/* Global data for the protocol */
struct fastpass_proto_data {

};

static int init_hashinfo(void)
{
	int i;

	inet_hashinfo_init(&fastpass_hashinfo);
	fastpass_hashinfo.ehash_mask = FASTPASS_EHASH_NBUCKETS - 1;
	fastpass_hashinfo.ehash = kzalloc(
			FASTPASS_EHASH_NBUCKETS * sizeof(struct inet_ehash_bucket),
			GFP_KERNEL);

	if (!fastpass_hashinfo.ehash) {
		FASTPASS_CRIT("Failed to allocate ehash buckets");
		return -ENOMEM;
	}

	fastpass_hashinfo.ehash_locks = kmalloc(sizeof(spinlock_t), GFP_KERNEL);
	fastpass_hashinfo.ehash_locks_mask = 0;
	if (!fastpass_hashinfo.ehash_locks) {
		FASTPASS_CRIT("Failed to allocate ehash locks");
		kfree(fastpass_hashinfo.ehash);
		return -ENOMEM;
	}
	spin_lock_init(&fastpass_hashinfo.ehash_locks[0]);

	for (i = 0; i <= fastpass_hashinfo.ehash_mask; i++) {
			INIT_HLIST_NULLS_HEAD(&fastpass_hashinfo.ehash[i].chain, i);
			INIT_HLIST_NULLS_HEAD(&fastpass_hashinfo.ehash[i].twchain, i);
	}
	return 0;
}

static void destroy_hashinfo(void)
{
	kfree(fastpass_hashinfo.ehash);
	kfree(fastpass_hashinfo.ehash_locks);
}

int __init fpproto_register(void)
{
	int err;

	err = -ENOMEM;
	fpproto_pktdesc_cachep = kmem_cache_create("fpproto_pktdesc_cache",
					   sizeof(struct fpproto_pktdesc),
					   0, 0, NULL);
	if (!fpproto_pktdesc_cachep) {
		FASTPASS_CRIT("%s: Cannot create kmem cache for fpproto_pktdesc\n", __func__);
		goto out;
	}

	err = init_hashinfo();
	if (err) {
		FASTPASS_CRIT("%s: Cannot allocate hashinfo tables\n", __func__);
		goto out_destroy_cache;
	}

	err = proto_register(&fastpass_prot, 1);
	if (err) {
		FASTPASS_CRIT("%s: Cannot add FastPass/IP protocol\n", __func__);
		goto out_destroy_hashinfo;
	}

	err = inet_add_protocol(&fastpass_protocol, IPPROTO_FASTPASS);
	if (err < 0) {
		FASTPASS_CRIT("%s: Cannot add protocol to inet\n", __func__);
		goto out_unregister_proto;
	}

	inet_register_protosw(&fastpass4_protosw);

	return 0;

out_unregister_proto:
	proto_unregister(&fastpass_prot);
out_destroy_hashinfo:
	destroy_hashinfo();
out_destroy_cache:
	kmem_cache_destroy(fpproto_pktdesc_cachep);
out:
	pr_info("%s: failed, ret=%d\n", __func__, err);
	return err;
}

void __exit fpproto_unregister(void)
{
	pr_info("%s: unregistering protocol\n", __func__);
	inet_unregister_protosw(&fastpass4_protosw);
	if (inet_del_protocol(&fastpass_protocol, IPPROTO_FASTPASS) != 0)
		FASTPASS_CRIT("%s: could not inet_del_protocol\n", __func__);
	proto_unregister(&fastpass_prot);
	destroy_hashinfo();
	kmem_cache_destroy(fpproto_pktdesc_cachep);
}

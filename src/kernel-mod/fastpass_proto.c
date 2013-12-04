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

#include "fastpass_proto.h"

struct inet_hashinfo fastpass_hashinfo;
EXPORT_SYMBOL_GPL(fastpass_hashinfo);
#define FASTPASS_EHASH_NBUCKETS 16

#ifdef CONFIG_IP_FASTPASS_DEBUG
bool fastpass_debug;
module_param(fastpass_debug, bool, 0644);
MODULE_PARM_DESC(fastpass_debug, "Enable debug messages");

EXPORT_SYMBOL_GPL(fastpass_debug);
#endif

/* returns the current time, for socket reset */
static u64 fp_sync_get_time(void)
{
	return ktime_to_ns(ktime_get_real());
}

u16 fp_ip_to_id(__be32 ipaddr) {
	return (u16)ntohl(ipaddr);
}

int fastpass_rcv(struct sk_buff *skb)
{
	struct sock *sk;
	struct fastpass_sock *fp;
	struct fastpass_hdr *hdr;
	u16 payload_type;
	u64 rst_tstamp;

	pr_err("%s: visited\n", __func__);

	sk = __inet_lookup_skb(&fastpass_hashinfo, skb,
			FASTPASS_DEFAULT_PORT_NETORDER /*sport*/,
			FASTPASS_DEFAULT_PORT_NETORDER /*dport*/);
	fp = fastpass_sk(sk);

	if (sk == NULL) {
		fastpass_pr_debug("got packet on non-connected socket\n");
		kfree_skb(skb);
		return 0;
	}

	bh_lock_sock(sk);

	if (skb->len < 6)
		goto packet_too_short;

	hdr = (struct fastpass_hdr *)skb->data;
	payload_type = skb->data[4] >> 4;

	switch (payload_type) {
	case FASTPASS_PTYPE_RESET:
		if (skb->len < 12)
			goto incomplete_reset_payload;

		rst_tstamp = ((u64)(ntohl(hdr->rstreq.hi) & ((1 << 24) - 1)) << 32) |
				ntohl(hdr->rstreq.lo);

		fastpass_pr_debug("got RESET 0x%llX, last is 0x%llX\n", rst_tstamp,
				fp->last_reset_time);

		if (rst_tstamp == fp->last_reset_time) {
			if (!fp->in_sync) {
				fp->in_sync = 1;
			} else {
				fastpass_pr_debug("rx redundant reset\n");
				fp->stat_redundant_reset++;
			}
			break;
		}

		/* reject resets outside the time window */
		/* TODO */

		/* need to process reset */
		//if (rst_tstamp > fp->last_reset_time)

		break;

	default:
		goto unknown_payload_type;
	}



cleanup:
	bh_unlock_sock(sk);
	sock_put(sk);
	__kfree_skb(skb);
	return NET_RX_SUCCESS;

unknown_payload_type:
	fastpass_pr_debug("got unknown payload type %d\n", payload_type);
	goto invalid_pkt;

incomplete_reset_payload:
	fastpass_pr_debug("packet has reset payload, but is too short (len=%d)\n",
			skb->len);
	goto invalid_pkt;

packet_too_short:
	fastpass_pr_debug("packet less than minimal size (len=%d)\n", skb->len);
	goto invalid_pkt;

invalid_pkt:
	fp->stat_invalid_rx_pkts++;
	goto cleanup;
}

static void fastpass_proto_close(struct sock *sk, long timeout)
{
	pr_err("%s: visited\n", __func__);

	sk_common_release(sk);
}

static int fastpass_proto_disconnect(struct sock *sk, int flags)
{
	struct inet_sock *inet = inet_sk(sk);

	pr_err("%s: visited\n", __func__);

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
static void fastpass_destroy_sock(struct sock *sk)
{
	struct fastpass_sock *fp = fastpass_sk(sk);

	pr_err("%s: visited\n", __func__);

	bh_lock_sock(sk);
	hrtimer_cancel(&fp->timer);
	fp->qdisc = NULL;
	bh_unlock_sock(sk);
}

static int fastpass_build_header(struct sock *sk, struct sk_buff *skb)
{
	struct fastpass_sock *fp = fastpass_sk(sk);
	struct fastpass_hdr *fh;

	BUG_ON(skb == NULL);

	/* Build header and checksum it. */
	if (likely(fp->in_sync)) {
		skb_push(skb, FASTPASS_REQ_HDR_SIZE);
		skb_reset_transport_header(skb);
		fh = memset(skb_transport_header(skb), 0, FASTPASS_REQ_HDR_SIZE);
	} else {
		u32 hi_word;
		skb_push(skb, FASTPASS_RSTREQ_HDR_SIZE);
		skb_reset_transport_header(skb);
		fh = memset(skb_transport_header(skb), 0, FASTPASS_RSTREQ_HDR_SIZE);
		hi_word = (FASTPASS_PTYPE_RSTREQ << 28) |
					((fp->last_reset_time >> 32) & 0x00FFFFFF);
		fh->rstreq.hi = htonl(hi_word);
		fh->rstreq.lo = htonl((u32)fp->last_reset_time);
	}

	fh->seq = htons((u16)(fp->next_seqno++));

	/* These could be useful for updating state */
//	dccp_update_gss(sk, dcb->dccpd_seq);
//	dccp_hdr_set_seq(fh, fp->dccps_gss);
//	if (set_ack)
//		dccp_hdr_set_ack(dccp_hdr_ack_bits(skb), ackno);
//
//	switch (dcb->dccpd_type) {
//	case DCCP_PKT_REQUEST:
//		dccp_hdr_request(skb)->dccph_req_service =
//						dp->dccps_service;
//		/*
//		 * Limit Ack window to ISS <= P.ackno <= GSS, so that
//		 * only Responses to Requests we sent are considered.
//		 */
//		fp->dccps_awl = fp->dccps_iss;
//		break;
//	case DCCP_PKT_RESET:
//		dccp_hdr_reset(skb)->dccph_reset_code =
//						dcb->dccpd_reset_code;
//		break;
//	}

	return 0;
}

void fastpass_v4_send_check(struct sock *sk, struct sk_buff *skb)
{
	const struct inet_sock *inet = inet_sk(sk);
	struct fastpass_hdr *fh = fastpass_hdr(skb);

	skb->csum = skb_checksum(skb, 0, skb->len, 0);
	fh->checksum = csum_tcpudp_magic(inet->inet_saddr, inet->inet_daddr,
			skb->len, IPPROTO_DCCP, skb->csum);
}

static void fastpass_tasklet_func(unsigned long int param)
{
	struct sock *sk = (struct sock *)param;
	struct fastpass_sock *fp = fastpass_sk(sk);
	struct inet_sock *inet = inet_sk(sk);
	struct sk_buff *skb;
	int rc;

	pr_err("%s: visited\n", __func__);

	bh_lock_sock(sk);

	fp->stat_tasklet_runs++;

	while ((skb = __skb_dequeue_tail(&sk->sk_write_queue)) != NULL) {
		/* write the fastpass header */
		rc = fastpass_build_header(sk, skb);
		if (unlikely(rc != 0)) {
			kfree_skb(skb);
			fp->stat_build_header_errors++;
			fastpass_pr_debug("%s: got error %d while building header\n",
					__func__, rc);
			continue;
		}

		/* checksum */
		fastpass_v4_send_check(sk, skb);

		/* send onwards */
		rc = ip_queue_xmit(skb, &inet->cork.fl);
		rc = net_xmit_eval(rc);
		if (unlikely(rc != 0)) {
			fp->stat_xmit_errors++;
			fastpass_pr_debug("%s: got error %d from ip_queue_xmit\n",
					__func__, rc);
		}
	}

	bh_unlock_sock(sk);
}

static enum hrtimer_restart fastpass_timer_func(struct hrtimer *timer)
{
	struct fastpass_sock *fp = container_of(timer, struct fastpass_sock,
						 timer);
	struct Qdisc *q = fp->qdisc;

	if (q) {
		qdisc_unthrottled(q);
		__netif_schedule(qdisc_root(q));
	}

	return HRTIMER_NORESTART;
}

static void do_proto_reset(struct fastpass_sock *fp, u64 reset_time)
{
	fp->last_reset_time = reset_time & ((1ULL << 56) - 1);
	fp->next_seqno = reset_time +
			((u64)jhash_1word((u32)reset_time, reset_time >> 32) << 32);
	fp->in_sync = 0;
}

static int fastpass_sk_init(struct sock *sk)
{
	struct fastpass_sock *fp = fastpass_sk(sk);

	pr_err("%s: visited\n", __func__);

	/* bind all sockets to port 1, to avoid inet_autobind */
	inet_sk(sk)->inet_num = ntohs(FASTPASS_DEFAULT_PORT_NETORDER);
	inet_sk(sk)->inet_sport = FASTPASS_DEFAULT_PORT_NETORDER;

	fp->mss_cache = 536;

	/* initialize tasklet */
	tasklet_init(&fp->tasklet, &fastpass_tasklet_func,
			(unsigned long int)fp);

	/* initialize hrtimer */
	hrtimer_init(&fp->timer, CLOCK_MONOTONIC, HRTIMER_MODE_ABS);
	fp->timer.function = fastpass_timer_func;
	fp->qdisc = NULL;

	/* choose reset time */
	do_proto_reset(fp, fp_sync_get_time());

	return 0;
}

static int fastpass_sendmsg(struct kiocb *iocb, struct sock *sk,
		struct msghdr *msg, size_t len)
{
	const struct fastpass_sock *fp = fastpass_sk(sk);
	struct inet_sock *inet = inet_sk(sk);
	const int flags = msg->msg_flags;
	const int noblock = flags & MSG_DONTWAIT;
	struct sk_buff *skb;
	int rc, size;

	pr_err("%s: visited\n", __func__);

	if (len > fp->mss_cache)
		return -EMSGSIZE;

	lock_sock(sk);

	/* Send only works on connected sockets. */
	if ((1 << sk->sk_state) & ~FASTPASSF_OPEN) {
		rc = -ENOTCONN;
		goto out_release;
	}

	/* Allocate an skb of suitable size */
	size = sk->sk_prot->max_header + len;
	release_sock(sk);
	skb = sock_alloc_send_skb(sk, size, noblock, &rc);
	lock_sock(sk);
	if (skb == NULL)
		goto out_release;

	/* reserve header & copy contents from userspace */
	skb_reserve(skb, sk->sk_prot->max_header);
	rc = memcpy_fromiovec(skb_put(skb, len), msg->msg_iov, len);
	if (rc != 0)
		goto out_discard;

	/* write the fastpass header */
	rc = fastpass_build_header(sk, skb);
	if (rc != 0)
		goto out_discard;

	/* checksum */
	fastpass_v4_send_check(sk, skb);

	/* send onwards */
	rc = ip_queue_xmit(skb, &inet->cork.fl);
	rc = net_xmit_eval(rc);

out_release:
	release_sock(sk);
	return rc ? : len;
out_discard:
	kfree_skb(skb);
	goto out_release;
}

void fastpass_send_skb_via_tasklet(struct sock *sk, struct sk_buff *skb)
{
	struct fastpass_sock *fp = fastpass_sk(sk);

	pr_err("%s: visited\n", __func__);

	/* enqueue to write queue */
	bh_lock_sock(sk);
	__skb_queue_tail(&sk->sk_write_queue, skb);
	bh_unlock_sock(sk);

	/* schedule tasklet to write from the queue */
	tasklet_schedule(&fp->tasklet);
}


static int fastpass_recvmsg(struct kiocb *iocb, struct sock *sk,
		struct msghdr *msg, size_t len, int noblock, int flags, int *addr_len)
{
	pr_err("%s: visited\n", __func__);
	return 0;
}

static int fastpass_rcv_skb(struct sock *sk, struct sk_buff *skb)
{
	BUG();
	return 0;
}

static inline void fastpass_hash(struct sock *sk)
{
	pr_err("%s: visited\n", __func__);
	inet_hash(sk);
}
static void fastpass_unhash(struct sock *sk)
{
	pr_err("%s: visited\n", __func__);
	inet_unhash(sk);
}
static void fastpass_rehash(struct sock *sk)
{
	pr_err("%s: before %X\n", __func__, sk->sk_hash);
	sk->sk_prot->unhash(sk);

	sk->sk_state = TCP_ESTABLISHED;
	inet_sk(sk)->inet_dport = FASTPASS_DEFAULT_PORT_NETORDER;
	inet_sk(sk)->inet_daddr = inet_sk(sk)->cork.fl.u.ip4.daddr;
	sk->sk_prot->hash(sk);

	pr_err("%s: after %X\n", __func__, sk->sk_hash);
}
static int fastpass_bind(struct sock *sk, struct sockaddr *uaddr,
		int addr_len)
{
	return -ENOTSUPP;
}

/* The interface for receiving packets from IP */
struct net_protocol fastpass_protocol = {
	.handler = fastpass_rcv,
	.no_policy = 1,
	.netns_ok = 1,
};

/* Interface for AF_INET socket operations */
struct proto	fastpass_prot = {
	.name = "FastPass",
	.owner = THIS_MODULE,
	.close		   = fastpass_proto_close,
	.connect	   = ip4_datagram_connect,
	.disconnect	   = fastpass_proto_disconnect,
	.init		   = fastpass_sk_init,
	.destroy	   = fastpass_destroy_sock,
	.setsockopt	   = ip_setsockopt,
	.getsockopt	   = ip_getsockopt,
	.sendmsg	   = fastpass_sendmsg,
	.recvmsg	   = fastpass_recvmsg,
	.bind		   = fastpass_bind,
	.backlog_rcv   = fastpass_rcv_skb,
	.hash		   = fastpass_hash,
	.unhash		   = fastpass_unhash,
	.rehash		   = fastpass_rehash,
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
	.flags		=  INET_PROTOSW_PERMANENT,
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
		return ENOMEM;
	}

	fastpass_hashinfo.ehash_locks = kmalloc(sizeof(spinlock_t), GFP_KERNEL);
	fastpass_hashinfo.ehash_locks_mask = 0;
	if (!fastpass_hashinfo.ehash_locks) {
		FASTPASS_CRIT("Failed to allocate ehash locks");
		kfree(fastpass_hashinfo.ehash);
		return ENOMEM;
	}
	spin_lock_init(&fastpass_hashinfo.ehash_locks[0]);

	for (i = 0; i <= fastpass_hashinfo.ehash_mask; i++) {
			INIT_HLIST_NULLS_HEAD(&fastpass_hashinfo.ehash[i].chain, i);
			INIT_HLIST_NULLS_HEAD(&fastpass_hashinfo.ehash[i].twchain, i);
	}
	return 0;
}


void __init fastpass_proto_register(void)
{
	if (init_hashinfo())
		goto out_mem_err;

	if (proto_register(&fastpass_prot, 1))
		goto out_register_err;

	if (inet_add_protocol(&fastpass_protocol, IPPROTO_FASTPASS) < 0)
		goto out_unregister_proto;

	inet_register_protosw(&fastpass4_protosw);

	return;

out_unregister_proto:
	proto_unregister(&fastpass_prot);
out_register_err:
	pr_crit("%s: Cannot add FastPass/IP protocol\n", __func__);
	return;
out_mem_err:
	pr_crit("%s: Cannot allocate hashinfo tables\n", __func__);
	return;
}


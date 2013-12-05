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



/**
 * Receives a packet destined for the protocol. (part of inet socket API)
 */
int fpproto_rcv(struct sk_buff *skb)
{
	struct sock *sk;
	struct fastpass_sock *fp;
	struct fastpass_hdr *hdr;
	struct Qdisc *sch;
	u16 payload_type;
	u64 rst_tstamp;
	int i;
	unsigned char *data;
	unsigned char *data_end;
	int alloc_n_dst, alloc_n_tslots;
	u16 alloc_dst[16];
	u16 alloc_base_tslot;

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

	sch = fpproto_lock_qdisc(sk);

	if (skb->len < 6)
		goto packet_too_short;

	hdr = (struct fastpass_hdr *)skb->data;
	data = &skb->data[4];
	data_end = &skb->data[skb->len];

handle_payload:
	payload_type = *data >> 4;

	switch (payload_type) {
	case FASTPASS_PTYPE_RESET:
		if (data + 8 > data_end)
			goto incomplete_reset_payload;

		rst_tstamp = ((u64)(ntohl(*(u32 *)data) & ((1 << 24) - 1)) << 32) |
				ntohl(*(u32 *)(data + 4));

		if (fp->ops->handle_reset)
			fp->ops->handle_reset(sch, rst_tstamp);

		data += 8;
		break;

	case FASTPASS_PTYPE_ALLOC:
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
	fastpass_pr_debug("got unknown payload type %d\n", payload_type);
	goto invalid_pkt;

incomplete_reset_payload:
	fastpass_pr_debug("RESET payload incomplete, expected 8 bytes, got %d\n",
			(int)(data_end - data));
	goto invalid_pkt;

incomplete_alloc_payload:
	fastpass_pr_debug("ALLOC payload incomplete: expected %d bytes, got %d\n",
			2 + 2 * alloc_n_dst + alloc_n_tslots, (int)(data_end - data));
	goto invalid_pkt;

packet_too_short:
	fastpass_pr_debug("packet less than minimal size (len=%d)\n", skb->len);
	goto invalid_pkt;

invalid_pkt:
	fp->stat_invalid_rx_pkts++;
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
	pr_err("%s: visited\n", __func__);

	sk_common_release(sk);
}

/* disconnect (happens if called connect with AF_UNSPEC family) */
static int fpproto_disconnect(struct sock *sk, int flags)
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

static void fpproto_destroy_sock(struct sock *sk)
{
	struct fastpass_sock *fp = fastpass_sk(sk);

	pr_err("%s: visited\n", __func__);

	hrtimer_cancel(&fp->timer);

	/* might not be necessary, doing for safety */
	fpproto_set_qdisc(sk, NULL);
}

static int fpproto_build_header(struct sock *sk, struct sk_buff *skb)
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

void fpproto_egress_checksum(struct sock *sk, struct sk_buff *skb)
{
	const struct inet_sock *inet = inet_sk(sk);
	struct fastpass_hdr *fh = fastpass_hdr(skb);

	skb->csum = skb_checksum(skb, 0, skb->len, 0);
	fh->checksum = csum_tcpudp_magic(inet->inet_saddr, inet->inet_daddr,
			skb->len, IPPROTO_DCCP, skb->csum);
}

static void fpproto_tasklet_write_queue(unsigned long int param)
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
		rc = fpproto_build_header(sk, skb);
		if (unlikely(rc != 0)) {
			kfree_skb(skb);
			fp->stat_build_header_errors++;
			fastpass_pr_debug("%s: got error %d while building header\n",
					__func__, rc);
			continue;
		}

		/* checksum */
		fpproto_egress_checksum(sk, skb);

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

static int fpproto_sk_init(struct sock *sk)
{
	struct fastpass_sock *fp = fastpass_sk(sk);

	pr_err("%s: visited\n", __func__);

	/* bind all sockets to port 1, to avoid inet_autobind */
	inet_sk(sk)->inet_num = ntohs(FASTPASS_DEFAULT_PORT_NETORDER);
	inet_sk(sk)->inet_sport = FASTPASS_DEFAULT_PORT_NETORDER;

	fp->mss_cache = 536;

	/* initialize tasklet */
	tasklet_init(&fp->tasklet, &fpproto_tasklet_write_queue,
			(unsigned long int)fp);

	/* initialize hrtimer */
	hrtimer_init(&fp->timer, CLOCK_MONOTONIC, HRTIMER_MODE_ABS);
	fp->timer.function = fastpass_timer_func;
	fp->qdisc = NULL;

	/* choose reset time */
	do_proto_reset(fp, fp_sync_get_time());

	return 0;
}

static int fpproto_userspace_sendmsg(struct kiocb *iocb, struct sock *sk,
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
	rc = fpproto_build_header(sk, skb);
	if (rc != 0)
		goto out_discard;

	/* checksum */
	fpproto_egress_checksum(sk, skb);

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

void fpproto_send_skb_via_tasklet(struct sock *sk, struct sk_buff *skb)
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
	pr_err("%s: visited\n", __func__);
	inet_hash(sk);
}
static void fpproto_unhash(struct sock *sk)
{
	pr_err("%s: visited\n", __func__);
	inet_unhash(sk);
}
static void fpproto_rehash(struct sock *sk)
{
	pr_err("%s: before %X\n", __func__, sk->sk_hash);
	sk->sk_prot->unhash(sk);

	sk->sk_state = TCP_ESTABLISHED;
	inet_sk(sk)->inet_dport = FASTPASS_DEFAULT_PORT_NETORDER;
	inet_sk(sk)->inet_daddr = inet_sk(sk)->cork.fl.u.ip4.daddr;
	sk->sk_prot->hash(sk);

	pr_err("%s: after %X\n", __func__, sk->sk_hash);
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


void __init fpproto_register(void)
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


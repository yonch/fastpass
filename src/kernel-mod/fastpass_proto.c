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
#include <linux/netdevice.h>
#include <net/protocol.h>
#include <net/ip.h>
#include <net/inet_common.h>
#include <net/inet_hashtables.h>
#include <net/sch_generic.h>
#include <net/pkt_sched.h>
#include <net/sock.h>

#include "fastpass_proto.h"
#include "../protocol/platform.h"

struct inet_hashinfo fastpass_hashinfo;
EXPORT_SYMBOL_GPL(fastpass_hashinfo);
#define FASTPASS_EHASH_NBUCKETS		16

#define FASTPASS_SET_DSCP_CLASS				0
#define FASTPASS_CTRL_DSCP_CLASS			46

#define FASTPASS_TX_QUOTA_PER_TASKLET_CALL	50

#ifdef CONFIG_IP_FASTPASS_DEBUG
bool fastpass_debug;
module_param(fastpass_debug, bool, 0644);
MODULE_PARM_DESC(fastpass_debug, "Enable debug messages");

EXPORT_SYMBOL_GPL(fastpass_debug);
#endif

struct kmem_cache *fpproto_pktdesc_cachep __read_mostly;

static inline struct fastpass_sock *fastpass_sk(struct sock *sk)
{
	return (struct fastpass_sock *)sk;
}

/* configures which qdisc is associated with the fastpass socket */
void fpproto_set_priv(struct sock *sk, void *priv)
{
	struct fastpass_sock *fp = fastpass_sk(sk);
	fp->sch_fastpass_priv = priv;
}

int fpproto_rcv(struct sk_buff *skb)
{
	struct sock *sk;

	rcu_read_lock_bh();

	sk = __inet_lookup_skb(&fastpass_hashinfo, skb,
			FASTPASS_DEFAULT_PORT_NETORDER /*sport*/,
			FASTPASS_DEFAULT_PORT_NETORDER /*dport*/);

	if (sk == NULL) {
		fp_debug("got packet on non-connected socket\n");
		goto discard_no_sk;
	}

	if (unlikely((skb_shinfo(skb)->frag_list != NULL)
			|| (skb_shinfo(skb)->nr_frags != 0))) {
		fastpass_sk(sk)->stat.rx_fragmented++;
		FASTPASS_WARN("RX a fragmented control packet, which is not supported\n");
		goto discard_out;
	}

//	bh_lock_sock_nested(sk);
//	if (unlikely(sk_add_backlog(sk, skb, sk->sk_rcvbuf)))
//		goto discard_out_locked;
//	bh_unlock_sock(sk);

	sk_backlog_rcv(sk, skb);

	rcu_read_unlock_bh();

	sock_put(sk);

	return NET_RX_SUCCESS;

//discard_out_locked:
//	bh_unlock_sock(sk);
discard_out:
	sock_put(sk);
discard_no_sk:
	__kfree_skb(skb);
	rcu_read_unlock_bh();
	return NET_RX_SUCCESS;
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

	/* sanity check, has conn been initialized? */
	if (fp->rcv_handler == NULL)
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
	fp_debug("visited\n");

	sk_common_release(sk);
}

/* disconnect (happens if called connect with AF_UNSPEC family) */
static int fpproto_disconnect(struct sock *sk, int flags)
{
	struct inet_sock *inet = inet_sk(sk);

	fp_debug("visited\n");

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
	fp_debug("visited\n");
}

/**
 * Constructs and sends one packet.
 */
static struct sk_buff *fpproto_make_skb(struct sock *sk, struct fpproto_pktdesc *pd)
{
	struct fastpass_sock *fp = fastpass_sk(sk);
	struct inet_sock *inet = inet_sk(sk);
	int payload_len;
	const int max_header = MAX_HEADER;
	struct sk_buff *skb = NULL;
	int err;
	u8 *data;

	/* allocate request skb */
	skb = sock_alloc_send_skb(sk, FASTPASS_MAX_PAYLOAD + max_header, 1, &err);
	if (!skb)
		goto alloc_err;

	/* reserve space for headers */
	skb_reserve(skb, max_header);
	/* set skb fastpass packet size */
	skb_reset_transport_header(skb);

	if (unlikely(pd->n_areq > FASTPASS_PKT_MAX_AREQ)) {
		FASTPASS_CRIT("got n_areq larger than max! n_areq %d max %d send_reset %d seqno %llu\n",
				pd->n_areq, FASTPASS_PKT_MAX_AREQ, pd->send_reset,
				pd->seqno);
		kfree_skb(skb);
		return NULL;
	}

	/* encode the packet from the descriptor */
	data = &skb->data[0];
	payload_len = fpproto_encode_packet(pd, data, FASTPASS_MAX_PAYLOAD,
			inet->inet_saddr, inet->inet_daddr, 26);

	/* adjust the size of the skb based on encoded size */
	if (unlikely((payload_len > FASTPASS_MAX_PAYLOAD) || (payload_len < 0))) {
		FASTPASS_CRIT("invalid packet encoding! len %d max %u n_areq %d send_reset %d seqno %llu\n",
				payload_len, FASTPASS_MAX_PAYLOAD, pd->n_areq, pd->send_reset,
				pd->seqno);
		kfree_skb(skb);
		return NULL;
	}

	skb_put(skb, payload_len);
	return skb;

alloc_err:
	fp->stat.skb_alloc_error++;
	fp_debug("could not alloc skb of size %d\n",
			FASTPASS_MAX_PAYLOAD + max_header);
	return NULL;
}

static void fpproto_send_skb(struct sock *sk, struct sk_buff *skb)
{
	struct fastpass_sock *fp = fastpass_sk(sk);
	struct inet_sock *inet = inet_sk(sk);
	int err;

	fp_debug("sending packet\n");

	/* don't need the lock because the tasklet guarantees mutual exclusion */

	/* send onwards */
	err = ip_queue_xmit(skb, &inet->cork.fl);
	err = net_xmit_eval(err);
	if (unlikely(err != 0)) {
		fp->stat.xmit_errors++;
		fp_debug("got error %d from ip_queue_xmit\n", err);
	} else {
		fp->stat.xmit_success++;
	}

	return;
}

void fpproto_send_pktdesc(struct sock *sk,
		struct fp_kernel_pktdesc *kern_pd)
{
	struct fpproto_pktdesc *pd = &kern_pd->pktdesc;
	struct sk_buff *skb;

	if (atomic_read(&kern_pd->refcount) == 1) {
		/* already timed out, can free */
		free_kernel_pktdesc_no_refcount(kern_pd);
		return;
	}

	/* make the skb */
	skb = fpproto_make_skb(sk, pd);

	/* send the skb */
	if (likely(skb != NULL))
		fpproto_send_skb(sk, skb);

	/* free the pktdesc if refcount allows (be wary of a race here) */
	fpproto_pktdesc_free(pd);
}

static int fpproto_sk_init(struct sock *sk)
{
	struct fastpass_sock *fp = fastpass_sk(sk);

	fp_debug("visited\n");

	/* bind all sockets to port 1, to avoid inet_autobind */
	inet_sk(sk)->inet_num = ntohs(FASTPASS_DEFAULT_PORT_NETORDER);
	inet_sk(sk)->inet_sport = FASTPASS_DEFAULT_PORT_NETORDER;

	fp->mss_cache = 536;

	fpproto_set_priv(sk, NULL);

	/* set socket priority */
	sk->sk_priority = TC_PRIO_CONTROL;
	if (FASTPASS_SET_DSCP_CLASS)
		inet_sk(sk)->tos = FASTPASS_CTRL_DSCP_CLASS << 2;

	/* skb allocation must be atomic (done under the qdisc lock) */
	sk->sk_allocation = GFP_ATOMIC;

	/* caller should initialize fpproto_conn before calling connect() */

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

static int fpproto_backlog_rcv(struct sock *sk, struct sk_buff *skb)
{
	const struct inet_sock *inet = inet_sk(sk);
	struct fastpass_sock *fp = fastpass_sk(sk);

	fp->rcv_handler(fp->sch_fastpass_priv, skb->data, skb->len, inet->inet_saddr,
				inet->inet_daddr);

	__kfree_skb(skb);
	return 0;
}

static inline void fpproto_hash(struct sock *sk)
{
	fp_debug("visited\n");
	inet_hash(sk);
}
static void fpproto_unhash(struct sock *sk)
{
	fp_debug("visited\n");
	inet_unhash(sk);
}
static void fpproto_rehash(struct sock *sk)
{
	fp_debug("before %X\n", sk->sk_hash);
	sk->sk_prot->unhash(sk);

	sk->sk_state = TCP_ESTABLISHED;
	inet_sk(sk)->inet_dport = FASTPASS_DEFAULT_PORT_NETORDER;
	inet_sk(sk)->inet_daddr = inet_sk(sk)->cork.fl.u.ip4.daddr;
	sk->sk_prot->hash(sk);

	fp_debug("after %X\n", sk->sk_hash);
}
static int fpproto_bind(struct sock *sk, struct sockaddr *uaddr,
		int addr_len)
{
	return -ENOTSUPP;
}

void fpproto_print_socket_stats(struct sock *sk, struct seq_file *seq)
{
	struct fastpass_sock *fp = fastpass_sk(sk);
	struct fp_socket_stat *sks = &fp->stat;

	seq_printf(seq, "\n  %llu control packets outputted", sks->xmit_success);
}

void fpproto_print_socket_errors(struct sock *sk, struct seq_file *seq)
{
	struct fastpass_sock *fp = fastpass_sk(sk);
	struct fp_socket_stat *sks = &fp->stat;

	if (sks->skb_alloc_error)
		seq_printf(seq, "\n  %llu control packets failed to allocate skb",
				sks->skb_alloc_error);
	if (sks->xmit_errors)
		seq_printf(seq, "\n  %llu control packets had errors traversing the IP stack",
				sks->xmit_errors);
	if (sks->rx_fragmented)
		seq_printf(seq, "\n  %llu received a fragmented skb (no current support)",
				sks->rx_fragmented);
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
					   sizeof(struct fp_kernel_pktdesc),
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

void fpproto_unregister(void)
{
	pr_info("%s: unregistering protocol\n", __func__);
	inet_unregister_protosw(&fastpass4_protosw);
	if (inet_del_protocol(&fastpass_protocol, IPPROTO_FASTPASS) != 0)
		FASTPASS_CRIT("%s: could not inet_del_protocol\n", __func__);
	proto_unregister(&fastpass_prot);
	destroy_hashinfo();
	kmem_cache_destroy(fpproto_pktdesc_cachep);
}

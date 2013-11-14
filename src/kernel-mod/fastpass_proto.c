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

int fastpass_rcv(struct sk_buff *skb) {
	BUG();
	return -ENOTSUPP;
}

static void fastpass_proto_close(struct sock *sk, long timeout)
{
	sk_common_release(sk);
}

static int fastpass_sk_init(struct sock *sk)
{
	pr_err("%s: visited\n", __func__);

	/* bind all sockets to port 1, to avoid inet_autobind */
	inet_sk(sk)->inet_num = 1;

	return 0;
}
static int fastpass_proto_disconnect(struct sock *sk, int flags)
{
	pr_err("%s: visited\n", __func__);
	return 0;
}
static void fastpass_destroy_sock(struct sock *sk)
{
	pr_err("%s: visited\n", __func__);
}
static int fastpass_sendmsg(struct kiocb *iocb, struct sock *sk,
		struct msghdr *msg, size_t len)
{
	pr_err("%s: visited\n", __func__);
	return 0;
}
static int fastpass_recvmsg(struct kiocb *iocb, struct sock *sk,
		struct msghdr *msg, size_t len, int noblock, int flags, int *addr_len)
{
	pr_err("%s: visited\n", __func__);
	return 0;
}

static int fastpass_queue_rcv_skb(struct sock *sk, struct sk_buff *skb)
{
	BUG();
	return -ENOTSUPP;
}
static inline void fastpass_hash(struct sock *sk)
{
	BUG();
}
static void fastpass_unhash(struct sock *sk)
{
	pr_err("%s: visited\n", __func__);
}

static int fastpass_bind(struct sock *sk, struct sockaddr *uaddr,
		int addr_len)
{
	return -ENOTSUPP;
}

struct fastpass_sock {
	/* inet_sock has to be the first member */
	struct inet_sock inet;
};

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
	.backlog_rcv	   = fastpass_queue_rcv_skb,
	.hash		   = fastpass_hash,
	.unhash		   = fastpass_unhash,
	.obj_size	   = sizeof(struct fastpass_sock),
	.slab_flags	   = SLAB_DESTROY_BY_RCU,
#ifdef CONFIG_COMPAT
	.compat_setsockopt = compat_ip_setsockopt,
	.compat_getsockopt = compat_ip_getsockopt,
#endif
	.clear_sk	   = sk_prot_clear_portaddr_nulls,
};
EXPORT_SYMBOL(fastpass_prot);

#define IPPROTO_FASTPASS 222

static struct inet_protosw fastpass4_protosw = {
	.type		=  SOCK_DGRAM,
	.protocol	=  IPPROTO_FASTPASS,
	.prot		=  &fastpass_prot,
	.ops		=  &inet_dgram_ops,
	.no_check	=  0,		/* must checksum (RFC 3828) */
	.flags		=  INET_PROTOSW_PERMANENT,
};

void __init fastpass_proto_register(void)
{
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
}


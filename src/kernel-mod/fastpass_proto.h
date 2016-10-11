/*
 * fastpass_proto.h
 *
 *  Created on: Nov 18, 2013
 *      Author: yonch
 */

#ifndef FASTPASS_PROTO_H_
#define FASTPASS_PROTO_H_

#include <net/inet_sock.h>
#include <linux/interrupt.h>
#include <linux/hrtimer.h>
#include <linux/types.h>

#include "../protocol/platform/generic.h"
#include "../protocol/fpproto.h"

#define FASTPASS_DEFAULT_PORT_NETORDER 1

#define MAX_TOTAL_FASTPASS_HEADERS MAX_HEADER

extern struct kmem_cache *fpproto_pktdesc_cachep __read_mostly;

struct fp_socket_stat {
	/* rx-related */
	__u64 rx_fragmented;

	/* send-related */
	__u64 xmit_errors;
	__u64 skb_alloc_error;
	__u64 xmit_success;
};

/**
 * @inet: the IPv4 socket information
 * @mss_cache: maximum segment size cache
 * @qdisc: the qdisc that owns the socket
 */
struct fastpass_sock {
	/* inet_sock has to be the first member */
	struct inet_sock 		inet;
	__u32 					mss_cache;
	void					*sch_fastpass_priv;
	void (*rcv_handler)(void *priv, u8 *pkt, u32 len, __be32 saddr, __be32 daddr);

	struct fp_socket_stat stat;
};

struct fp_kernel_pktdesc {
	struct fpproto_pktdesc		pktdesc;
	struct list_head			q_elem;
	atomic_t					refcount;
	struct sock					*sk;
};

extern int __init fpproto_register(void);
void fpproto_unregister(void);

void fpproto_set_priv(struct sock *sk, void *priv);

void fpproto_send_pktdesc(struct sock *sk, struct fp_kernel_pktdesc *kern_pd);

void fpproto_handle_pending_rx(struct sock *sk);

void fpproto_print_socket_stats(struct sock *sk, struct seq_file *seq);
void fpproto_print_socket_errors(struct sock *sk, struct seq_file *seq);

static inline struct fastpass_hdr *fastpass_hdr(
		const struct sk_buff *skb)
{
	return (struct fastpass_hdr *)skb_transport_header(skb);
}


#endif /* FASTPASS_PROTO_H_ */

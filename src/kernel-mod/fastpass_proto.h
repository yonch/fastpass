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
#include "fp_statistics.h"

#define FASTPASS_DEFAULT_PORT_NETORDER 1

#define MAX_TOTAL_FASTPASS_HEADERS MAX_HEADER

extern struct kmem_cache *fpproto_pktdesc_cachep __read_mostly;


/**
 * @inet: the IPv4 socket information
 * @mss_cache: maximum segment size cache
 * @qdisc: the qdisc that owns the socket
 */
struct fastpass_sock {
	/* inet_sock has to be the first member */
	struct inet_sock 		inet;
	__u32 					mss_cache;
	struct Qdisc			*qdisc;
	struct fpproto_conn conn;

	struct list_head		pktdesc_tx_queue;
	struct tasklet_struct	tx_tasklet;
	spinlock_t				pktdesc_lock;

	spinlock_t				conn_lock;

	struct fp_socket_stat stat;
};

struct fp_kernel_pktdesc {
	struct fpproto_pktdesc		pktdesc;
	struct list_head			q_elem;
	atomic_t					refcount;
	struct sock					*sk;
};

extern int __init fpproto_register(void);
void __exit fpproto_unregister(void);

void fpproto_maintenance_lock(struct sock *sk);
void fpproto_maintenance_unlock(struct sock *sk);

void fpproto_set_qdisc(struct sock *sk, struct Qdisc *new_qdisc);

void fpproto_send_pktdesc(struct sock *sk, struct fp_kernel_pktdesc *kern_pd);

void fpproto_handle_pending_rx(struct sock *sk);

static inline struct fastpass_hdr *fastpass_hdr(
		const struct sk_buff *skb)
{
	return (struct fastpass_hdr *)skb_transport_header(skb);
}


#endif /* FASTPASS_PROTO_H_ */

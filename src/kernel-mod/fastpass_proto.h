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

#include "debug.h"
#include "fpproto.h"
#include "window.h"

#define FASTPASS_DEFAULT_PORT_NETORDER 1

#define MAX_TOTAL_FASTPASS_HEADERS MAX_HEADER



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
	struct hrtimer			retrans_timer;
	struct tasklet_struct 	retrans_tasklet;
	struct fpproto_conn conn;

	struct fp_socket_stat stat;
};

extern int __init fpproto_register(void);
void __exit fpproto_unregister(void);

void fpproto_set_qdisc(struct sock *sk, struct Qdisc *new_qdisc);

void fpproto_send_packet(struct sock *sk, struct fpproto_pktdesc *pkt);

/* returns the current real time (the time that is used to determine timeslots) */
static inline u64 fp_get_time_ns(void)
{
	return ktime_to_ns(ktime_get_real());
}



static inline struct fastpass_hdr *fastpass_hdr(
		const struct sk_buff *skb)
{
	return (struct fastpass_hdr *)skb_transport_header(skb);
}


#endif /* FASTPASS_PROTO_H_ */

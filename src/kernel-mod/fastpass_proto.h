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

#include "window.h"

#define IPPROTO_FASTPASS 222

#define FASTPASS_DEFAULT_PORT_NETORDER 1

#define MAX_TOTAL_FASTPASS_HEADERS MAX_HEADER

/**
 * The log of the size of outgoing packet window waiting for ACKs or timeout
 *    expiry. Setting this at < 6 is a bit wasteful since a full word has 64
 *    bits, and the algorithm works with word granularity
 */
#define FASTPASS_OUTWND_LOG			8
#define FASTPASS_OUTWND_LEN			(1 << FASTPASS_OUTWND_LOG)

#define FASTPASS_BAD_PKT_RESET_THRESHOLD	10
#define FASTPASS_RESET_WINDOW_NS	(1000*1000*1000)

#define FASTPASS_PKT_MAX_AREQ		10

#define FASTPASS_PTYPE_RSTREQ		0x0
#define FASTPASS_PTYPE_RESET 		0x1
#define FASTPASS_PTYPE_AREQ			0x2
#define FASTPASS_PTYPE_ALLOC		0x3
#define FASTPASS_PTYPE_ACK			0x4

/*
 * 	Warning and debugging macros, (originally taken from DCCP)
 */
#define FASTPASS_WARN(fmt, a...) LIMIT_NETDEBUG(KERN_WARNING "%s: " fmt,       \
							__func__, ##a)
#define FASTPASS_CRIT(fmt, a...) printk(KERN_CRIT fmt " at %s:%d/%s()\n", ##a, \
					 __FILE__, __LINE__, __func__)
#define FASTPASS_BUG(a...)       do { FASTPASS_CRIT("BUG: " a); dump_stack(); } while(0)
#define FASTPASS_BUG_ON(cond)    do { if (unlikely((cond) != 0))		   \
				     FASTPASS_BUG("\"%s\" holds (exception!)", \
					      __stringify(cond));          \
			     } while (0)

#define FASTPASS_PRINTK(enable, fmt, args...)	do { if (enable)	     \
							printk(fmt, ##args); \
						} while(0)
#define FASTPASS_PR_DEBUG(enable, fmt, a...)	FASTPASS_PRINTK(enable, KERN_DEBUG \
						  "%s: " fmt, __func__, ##a)

#ifdef CONFIG_IP_FASTPASS_DEBUG
extern bool fastpass_debug;
#define fastpass_pr_debug(format, a...)	  FASTPASS_PR_DEBUG(fastpass_debug, format, ##a)
#define fastpass_pr_debug_cat(format, a...)   FASTPASS_PRINTK(fastpass_debug, format, ##a)
#define fastpass_debug(fmt, a...)		  fastpass_pr_debug_cat(KERN_DEBUG fmt, ##a)
#else
#define fastpass_pr_debug(format, a...)
#define fastpass_pr_debug_cat(format, a...)
#define fastpass_debug(format, a...)
#endif

/**
 * An A-REQ for a single destination
 * @src_dst_key: the key for the flow
 * @tslots: the total number of tslots requested
 */
struct fpproto_areq_desc {
	u64		src_dst_key;
	u64		tslots;
};

/**
 * A full packet sent to the controller
 * @n_areq: number of filled in destinations for A-REQ
 * @sent_timestamp: a timestamp when the request was sent
 */
struct fpproto_pktdesc {
	u16							n_areq;
	struct fpproto_areq_desc	areq[FASTPASS_PKT_MAX_AREQ];

	u64							sent_timestamp;
	u64							seqno;
	bool						send_reset;
	u64							reset_timestamp;
};

/**
 * Operations executed by the protocol
 */
struct fpproto_ops {
	void 	(*handle_reset)(struct Qdisc *q);

	void	(*handle_alloc)(struct Qdisc *q, u32 base_tslot,
			u16 *dst, int n_dst, u8 *tslots, int n_tslots);

	/**
	 * Called when an ack is received for a sent packet.
	 * @note: this function becomes responsible for freeing the memory of @pd
	 */
	void	(*handle_ack)(struct Qdisc *q, struct fpproto_pktdesc *pd);

	/**
	 * Called when a sent packet is deemed as probably lost.
	 * @note: this function becomes responsible for freeing the memory of @pd
	 */
	void	(*handle_neg_ack)(struct Qdisc *q, struct fpproto_pktdesc *pd);

	/**
	 * The protocol needs to send information to the controller -- the user
	 *    should send a packet, so that information can piggy back.
	 */
	void	(*trigger_request)(struct Qdisc *q, u64 when);

};

/**
 * @inet: the IPv4 socket information
 * @mss_cache: maximum segment size cache
 * @qdisc: the qdisc that owns the socket
 * @last_reset_time: the time used in the last sent reset
 * @rst_win_ns: time window within which resets are accepted, in nanoseconds
 * @send_timeout_ns: number of ns after which a tx packet is deemed lost
 * @bin_mask: a mask for each bin, 1 if it has not been acked yet.
 * @bins: pointers to the packet descriptors of each bin
 * @earliest_unacked: sequence number of the earliest unacked packet in the
 * 		outwnd. only valid if the outwnd is not empty.
 *
 * Statistics:
 * @stat_tasklet_runs: the number of times the tasklet ran
 * @stat_build_header_errors: #egress failures due to header building
 * @stat_xmit_errors: #egress failures due to IP stack
 * @stat_invalid_rx_pkts: #invalid rx packets
 * @stat_redundant_reset: #times got reset for last_reset_time
 * @stat_reset_out_of_window: the reset was outside rst_wnd_ns
 * @stat_outdated_reset: the socket already processed a more recent reset within
 *   the window
 */
struct fastpass_sock {
	/* inet_sock has to be the first member */
	struct inet_sock 		inet;
	__u32 					mss_cache;
	struct Qdisc			*qdisc;
	u64						last_reset_time;
	u64						next_seqno;
	u64						in_max_seqno;
	u32						in_sync:1;
	struct fpproto_ops		*ops;
	u64 					rst_win_ns;
	u32						send_timeout_us;
	u32						consecutive_bad_pkts;

	/* outwnd */
	unsigned long			bin_mask[BITS_TO_LONGS(2 * FASTPASS_OUTWND_LEN)];
	struct fpproto_pktdesc	*bins[FASTPASS_OUTWND_LEN];
	u32						tx_num_unacked;
	struct hrtimer			retrans_timer;
	struct tasklet_struct 	retrans_tasklet;
	u64						earliest_unacked;

	/* inwnd */
	struct fp_window		inwnd;

	/* statistics */
	u64 stat_tasklet_runs;  /* TODO: change description */
	u64 stat_build_header_errors; /* TODO:deprecate */
	u64 stat_xmit_errors;
	u64 stat_invalid_rx_pkts;
	u64 stat_redundant_reset;
	u64 stat_reset_out_of_window; /* TODO: report */
	u64 stat_outdated_reset; /* TODO: report */
	u64 stat_skb_alloc_error; /* TODO: report */
	u64 stat_rx_unknown_payload; /* TODO: report */
	u64 stat_rx_incomplete_reset; /* TODO: report */
	u64 stat_rx_incomplete_alloc; /* TODO: report */
	u64 stat_rx_too_short; /* TODO: report */
	u64 stat_rx_pkts; /* TODO: report */
	u64 stat_fall_off_outwnd; /*TODO:report*/
	u64 stat_rx_incomplete_ack; /*TODO:report*/
	u64 stat_too_early_ack; /*TODO:report*/
	u64 stat_acked_packets; /* TODO:report*/
	u64 stat_timeout_pkts; /*TODO:report*/
	u64 stat_ack_payloads; /*TODO:report*/
	u64 stat_informative_ack_payloads; /*TODO:report*/
	u64 stat_reprogrammed_timer;
	u64 stat_checksum_error;
	u64 stat_no_reset_because_recent;
	u64 stat_reset_from_bad_pkts;
};

extern int __init fpproto_register(void);
void __exit fpproto_unregister(void);

void fpproto_set_qdisc(struct sock *sk, struct Qdisc *new_qdisc);

struct fpproto_pktdesc *fpproto_pktdesc_alloc(void);
void fpproto_pktdesc_free(struct fpproto_pktdesc *pd);

void fpproto_prepare_to_send(struct sock *sk);
void fpproto_commit_packet(struct sock *sk, struct fpproto_pktdesc *pkt,
		u64 timestamp);
void fpproto_send_packet(struct sock *sk, struct fpproto_pktdesc *pkt);

static inline struct fastpass_sock *fastpass_sk(const struct sock *sk)
{
	return (struct fastpass_sock *)sk;
}

/* returns the current real time (the time that is used to determine timeslots) */
static inline u64 fp_get_time_ns(void)
{
	return ktime_to_ns(ktime_get_real());
}


enum {
	FASTPASSF_OPEN		      = TCPF_ESTABLISHED,
	FASTPASSF_CLOSED	      = TCPF_CLOSE,
};

/**
 * struct fastpass_hdr - FastPass request packet header
 */
struct fastpass_hdr {
	__be16	seq;
	__sum16	checksum;
	union {
		struct {
			__be32		hi;
			__be32		lo;
		} rstreq;
	};
};

struct fastpass_areq {
	__be16	dst;
	__be16	count;
};


static inline struct fastpass_hdr *fastpass_hdr(
		const struct sk_buff *skb)
{
	return (struct fastpass_hdr *)skb_transport_header(skb);
}


#endif /* FASTPASS_PROTO_H_ */

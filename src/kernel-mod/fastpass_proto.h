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

#define IPPROTO_FASTPASS 222

#define FASTPASS_DEFAULT_PORT_NETORDER 1

#define FASTPASS_REQ_HDR_SIZE 4
#define FASTPASS_RSTREQ_HDR_SIZE 12
#define MAX_FASTPASS_ONLY_HEADER 12
#define MAX_TOTAL_FASTPASS_HEADERS (MAX_FASTPASS_ONLY_HEADER + MAX_HEADER)

#define FASTPASS_PTYPE_RSTREQ		0x0
#define FASTPASS_PTYPE_RESET 		0x1
#define FASTPASS_PTYPE_AREQ			0x2
#define FASTPASS_PTYPE_ALLOC		0x3

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
 * Operations executed by the protocol
 */
struct fpproto_ops {
	void 	(*handle_reset)(struct Qdisc *q);

	void	(*handle_alloc)(struct Qdisc *q, u32 base_tslot,
			u16 *dst, int n_dst, u8 *tslots, int n_tslots);
};

/**
 * @inet: the IPv4 socket information
 * @mss_cache: maximum segment size cache
 * @qdisc: the qdisc that owns the socket
 * @last_reset_time: the time used in the last sent reset
 * @rst_win_ns: time window within which resets are accepted, in nanoseconds
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
	u32						in_sync:1;
	struct fpproto_ops		*ops;
	u64 					rst_win_ns;

	/* statistics */
	u64 stat_tasklet_runs;
	u64 stat_build_header_errors;
	u64 stat_xmit_errors;
	u64 stat_invalid_rx_pkts;
	u64 stat_redundant_reset;
	u64 stat_reset_out_of_window; /* TODO: report */
	u64 stat_outdated_reset; /* TODO: report */

};

extern void __init fpproto_register(void);

void fpproto_set_qdisc(struct sock *sk, struct Qdisc *new_qdisc);

void fpproto_send_skb(struct sock *sk, struct sk_buff *skb);

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

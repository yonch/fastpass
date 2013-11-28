/*
 * fastpass_proto.h
 *
 *  Created on: Nov 18, 2013
 *      Author: yonch
 */

#ifndef FASTPASS_PROTO_H_
#define FASTPASS_PROTO_H_

#include <net/inet_sock.h>

#define IPPROTO_FASTPASS 222

#define FASTPASS_DEFAULT_PORT_NETORDER 1

#define MAX_FASTPASS_ONLY_HEADER (sizeof(struct fastpass_req_hdr))
#define MAX_TOTAL_FASTPASS_HEADERS (MAX_FASTPASS_ONLY_HEADER + MAX_HEADER)

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


struct fastpass_sock {
	/* inet_sock has to be the first member */
	struct inet_sock inet;
	__u32 mss_cache;
};

int fastpass_send_skb(struct sock *sk, struct sk_buff *skb);

static inline struct fastpass_sock *fastpass_sk(const struct sock *sk)
{
	return (struct fastpass_sock *)sk;
}

enum {
	FASTPASSF_OPEN		      = TCPF_ESTABLISHED,
	FASTPASSF_CLOSED	      = TCPF_CLOSE,
};

/**
 * struct fastpass_req_hdr - FastPass request packet header
 */
struct fastpass_req_hdr {
	__sum16	checksum;
	__u8	seq;
};

static inline struct fastpass_req_hdr *fastpass_req_hdr(
		const struct sk_buff *skb)
{
	return (struct fastpass_req_hdr *)skb_transport_header(skb);
}

#endif /* FASTPASS_PROTO_H_ */

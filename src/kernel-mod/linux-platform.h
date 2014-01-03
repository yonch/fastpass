/*
 * linux-platform.h
 *
 *  Created on: Dec 30, 2013
 *      Author: yonch
 */

#ifndef FASTPASS_LINUX_PLATFORM_H_
#define FASTPASS_LINUX_PLATFORM_H_

#include "../protocol/fpproto.h"
#include "fastpass_proto.h"

#define FASTPASS_PR_DEBUG(enable, fmt, a...)	do { if (enable)	     \
							printk(KERN_DEBUG "%s: " fmt, __func__, ##a); \
						} while(0)

static inline u64 fp_get_time_ns(void)
{
	return ktime_to_ns(ktime_get_real());
}

static inline
struct fpproto_pktdesc *fpproto_pktdesc_alloc(void)
{
	struct fpproto_pktdesc *pd;
	pd = kmem_cache_zalloc(fpproto_pktdesc_cachep, GFP_ATOMIC | __GFP_NOWARN);
	return pd;
}

static inline
void fpproto_pktdesc_free(struct fpproto_pktdesc *pd)
{
	kmem_cache_free(fpproto_pktdesc_cachep, pd);
}


static inline
int cancel_timer(struct fpproto_conn *proto)
{
	struct fastpass_sock *fp = container_of(proto, struct fastpass_sock, conn);

	if (unlikely(hrtimer_try_to_cancel(&fp->retrans_timer) == -1)) {
		fp_debug("could not cancel timer. tasklet will reset timer\n");
		return -1;
	}

	return 0;
}

static inline
void set_timer(struct fpproto_conn *proto, u64 when)
{
	struct fastpass_sock *fp = container_of(proto, struct fastpass_sock, conn);

	hrtimer_start(&fp->retrans_timer, ns_to_ktime(when), HRTIMER_MODE_ABS);
}

#endif /* FASTPASS_LINUX_PLATFORM_H_ */

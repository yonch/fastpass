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

static inline u64 fp_monotonic_time_ns(void)
{
	return ktime_to_ns(ktime_get());
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

#endif /* FASTPASS_LINUX_PLATFORM_H_ */

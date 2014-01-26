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
struct fp_kernel_pktdesc *fpproto_pktdesc_alloc(void)
{
	struct fp_kernel_pktdesc *kern_pd
		= kmem_cache_zalloc(fpproto_pktdesc_cachep, GFP_ATOMIC | __GFP_NOWARN);

	if (kern_pd == NULL)
		return NULL;

	atomic_set(&kern_pd->refcount, 2);
	return kern_pd;
}

static inline
void free_kernel_pktdesc_no_refcount(struct fp_kernel_pktdesc *kern_pd)
{
	kmem_cache_free(fpproto_pktdesc_cachep, kern_pd);
}

static inline
void fpproto_pktdesc_free(struct fpproto_pktdesc *pd)
{
	struct fp_kernel_pktdesc *kern_pd =
			container_of(pd, struct fp_kernel_pktdesc, pktdesc);

	if (atomic_dec_and_test(&kern_pd->refcount))
		free_kernel_pktdesc_no_refcount(kern_pd);
}


#endif /* FASTPASS_LINUX_PLATFORM_H_ */

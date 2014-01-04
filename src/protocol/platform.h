/*
 * Platform-supplied API
 */

#ifndef PROTOCOL_PLATFORM_H_
#define PROTOCOL_PLATFORM_H_

#include "platform/generic.h"

#ifdef __KERNEL__
#include "../kernel-mod/linux-platform.h"
#else
#include "../controller/dpdk-platform.h"
#endif

/** FUNCTIONS IN PLATFORM.H **/
/**
 * #define FASTPASS_PR_DEBUG(enable, fmt, a...)
 *
 * Outputs debug string
 */

/**
 * static inline u64 fp_get_time_ns(void)
 *
 * returns the current real time (the time that is used to determine timeslots)
 */

/**
 * struct fpproto_pktdesc *fpproto_pktdesc_alloc(void);
 *
 * Allocates a struct fpproto_pktdesc
 */

/**
 * void fpproto_pktdesc_free(struct fpproto_pktdesc *pd);
 *
 * Frees a struct fpproto_pktdesc
 */
#endif /* PROTOCOL_PLATFORM_H_ */

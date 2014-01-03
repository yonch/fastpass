/*
 * Platform-supplied API
 */

#ifndef PLATFORM_H_
#define PLATFORM_H_

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

/*
 * int cancel_timer(struct fpproto_conn *proto);
 *
 * Cancels the timer.
 * Returns 0 if timer was canceled or already completed
 * 			-1 if the timer was running concurrently and could not be canceled
 */

/**
 * void set_timer(struct fpproto_conn *proto, u64 when);
 *
 * Sets timer to the given time, in nanoseconds
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
#endif /* PLATFORM_H_ */

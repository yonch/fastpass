/*
 * admission_log.h
 *
 *  Created on: Jan 2, 2014
 *      Author: yonch
 */

#ifndef ADMISSION_LOG_H_
#define ADMISSION_LOG_H_

#include <rte_log.h>
#include <rte_lcore.h>
#include <rte_byteorder.h>
#include <rte_cycles.h>

#include "control.h"

/**
 * logged information for a core
 */
struct admission_log {
	uint64_t batches_started;
	uint64_t last_started_alloc_tsc;
	uint64_t batches_skipped;
};

extern struct admission_log admission_core_logs[RTE_MAX_LCORE];

#define RTE_LOGTYPE_ADMISSION RTE_LOGTYPE_USER1

#ifdef CONFIG_IP_FASTPASS_DEBUG
#define ADMISSION_DEBUG(a...) RTE_LOG(DEBUG, ADMISSION, ##a)
#else
#define ADMISSION_DEBUG(a...)
#endif

/* current comm log */
#define AL		(&admission_core_logs[rte_lcore_id()])

static inline void admission_log_init(struct admission_log *al)
{
	memset(al, 0, sizeof(*al));
}

static inline
void admission_log_allocation_begin(uint64_t current_timeslot,
		uint64_t start_time_first_timeslot) {
	(void)current_timeslot;(void)start_time_first_timeslot;
	uint64_t now = rte_get_tsc_cycles();
	AL->last_started_alloc_tsc = now;
	AL->batches_started++;
	(void)current_timeslot; (void)start_time_first_timeslot;
//	ADMISSION_DEBUG("core %d started allocation of batch %lu first timeslot time %lu (timeslot %lu cycle timer %lu)\n",
//			rte_lcore_id(), AL->batches_started, start_time_first_timeslot,
//			current_timeslot, now);
}

static inline void admission_log_allocation_end(void) {
//	uint64_t now = rte_get_tsc_cycles();
//	ADMISSION_DEBUG("core %d finished allocation of batch %lu (cycle timer %lu, took %lu)\n",
//			rte_lcore_id(), AL->batches_started, now,
//			now - AL->last_started_alloc_tsc);
}

static inline
void admission_log_skipped_batch() {
	AL->batches_skipped++;
}


#undef CL


#endif /* ADMISSION_LOG_H_ */

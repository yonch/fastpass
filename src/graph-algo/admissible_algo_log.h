/*
 * admissible_algo_log.h
 *
 *  Created on: Jan 24, 2014
 *      Author: yonch
 */

#ifndef ADMISSIBLE_ALGO_LOG_H_
#define ADMISSIBLE_ALGO_LOG_H_

#include "../protocol/platform/generic.h"

#define		MAINTAIN_ADM_LOG_COUNTERS	1

#define		BACKLOG_HISTOGRAM_NUM_BINS	16
#define		BACKLOG_HISTOGRAM_SHIFT		2

/**
 * Per-core statistics, in struct admission_core_state
 */
struct admission_core_statistics {
	uint64_t no_available_timeslots_for_bin_entry;
	uint64_t allocated_backlog_remaining;
	uint64_t backlog_sum;
	uint64_t allocated_no_backlog;
	uint64_t backlog_histogram[BACKLOG_HISTOGRAM_NUM_BINS];
};

/**
 * Global statistics in struct admissible_status
 */
struct admission_statistics {
	uint64_t wait_for_space_in_q_head;
	uint64_t wait_for_space_in_q_urgent;
	uint64_t wait_for_space_in_q_admitted_out;
	uint64_t wait_for_space_in_q_bin_out;
	uint64_t waiting_to_pass_token;
	uint64_t pacing_wait;
	uint64_t wait_for_q_bin_in;
	/* atomic increase statistics */
	uint64_t added_backlog_atomically;
	uint64_t backlog_sum_atomically;
	uint64_t backlog_sum_inc_atomically;
	/* to queue statistics */
	uint64_t added_backlog_to_queue;
	uint64_t backlog_sum_to_queue;
	uint64_t backlog_sum_inc_to_queue;
};

static inline
void adm_algo_log_no_available_timeslots_for_bin_entry(
		struct admission_core_statistics *st,
		uint16_t src, uint16_t dst)
{
	(void)src;(void)dst;
	if (MAINTAIN_ADM_LOG_COUNTERS)
		st->no_available_timeslots_for_bin_entry++;
}

static inline void adm_log_allocated_backlog_remaining(
		struct admission_core_statistics *st,
		uint16_t src, uint16_t dst, int32_t backlog) {
	(void)src;(void)dst;
	if (MAINTAIN_ADM_LOG_COUNTERS) {
		uint32_t hist_bin;
		st->allocated_backlog_remaining++;
		st->backlog_sum += backlog;

		/* mainatain histogram */
		hist_bin = backlog >> BACKLOG_HISTOGRAM_SHIFT;
		if (unlikely(hist_bin >= BACKLOG_HISTOGRAM_NUM_BINS))
			hist_bin = BACKLOG_HISTOGRAM_NUM_BINS - 1;
		st->backlog_histogram[hist_bin]++;
	}
}

static inline void adm_log_allocator_no_backlog(
		struct admission_core_statistics *st, uint16_t src, uint16_t dst) {
	(void)src;(void)dst;
	if (MAINTAIN_ADM_LOG_COUNTERS)
		st->allocated_no_backlog++;
}

static inline void adm_log_increased_backlog_to_queue(
		struct admission_statistics *ast, uint16_t amt, int32_t new_backlog) {
	if (MAINTAIN_ADM_LOG_COUNTERS) {
		ast->added_backlog_to_queue++;
		ast->backlog_sum_inc_to_queue += amt;
		ast->backlog_sum_to_queue += new_backlog;
	}
}

static inline void adm_log_increased_backlog_atomically(
		struct admission_statistics *ast, uint16_t amt, int32_t new_backlog) {
	if (MAINTAIN_ADM_LOG_COUNTERS) {
		ast->added_backlog_atomically++;
		ast->backlog_sum_inc_atomically += amt;
		ast->backlog_sum_atomically += new_backlog;
	}
}

#endif /* ADMISSIBLE_ALGO_LOG_H_ */

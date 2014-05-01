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

#define		BACKLOG_HISTOGRAM_NUM_BINS		16
#define		BACKLOG_HISTOGRAM_SHIFT			2

#define		BIN_SIZE_HISTOGRAM_NUM_BINS		16
#define		BIN_SIZE_HISTOGRAM_SHIFT		4

#define		NEW_REQUEST_HISTOGRAM_NUM_BINS	16
#define		NEW_REQUEST_HISTOGRAM_SHIFT		4


/**
 * Per-core statistics, in struct admission_core_state
 */
struct admission_core_statistics {
	uint64_t no_available_timeslots_for_bin_entry;
	uint64_t allocated_backlog_remaining;
	uint64_t backlog_sum;
	uint64_t allocated_no_backlog;
	uint64_t backlog_histogram[BACKLOG_HISTOGRAM_NUM_BINS];
	uint64_t bin_size_histogram[BIN_SIZE_HISTOGRAM_NUM_BINS];
	uint64_t new_request_histogram[NEW_REQUEST_HISTOGRAM_NUM_BINS];

	uint64_t wait_for_q_bin_in;
	uint64_t wait_for_head;
	uint64_t pacing_wait;
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
	/* atomic increase statistics */
	uint64_t added_backlog_atomically;
	uint64_t backlog_sum_atomically;
	uint64_t backlog_sum_inc_atomically;
	/* to queue statistics */
	uint64_t added_backlog_to_queue;
	uint64_t backlog_sum_to_queue;
	uint64_t backlog_sum_inc_to_queue;
};

static inline __attribute__((always_inline))
void adm_algo_log_no_available_timeslots_for_bin_entry(
		struct admission_core_statistics *st,
		uint16_t src, uint16_t dst)
{
	(void)src;(void)dst;
	if (MAINTAIN_ADM_LOG_COUNTERS)
		st->no_available_timeslots_for_bin_entry++;
}

static inline __attribute__((always_inline))
void adm_log_allocated_backlog_remaining(
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

static inline __attribute__((always_inline))
void adm_log_allocator_no_backlog(
		struct admission_core_statistics *st, uint16_t src, uint16_t dst) {
	(void)src;(void)dst;
	if (MAINTAIN_ADM_LOG_COUNTERS)
		st->allocated_no_backlog++;
}

static inline __attribute__((always_inline))
void adm_log_increased_backlog_to_queue(
		struct admission_statistics *ast, uint32_t amt, int32_t new_backlog) {
	if (MAINTAIN_ADM_LOG_COUNTERS) {
		ast->added_backlog_to_queue++;
		ast->backlog_sum_inc_to_queue += amt;
		ast->backlog_sum_to_queue += new_backlog;
/*		printf("\n added backlog to queue amt %u new_backlog %u\n", amt, new_backlog); */
	}
}

static inline __attribute__((always_inline))
void adm_log_increased_backlog_atomically(
		struct admission_statistics *ast, uint32_t amt, int32_t new_backlog) {
	if (MAINTAIN_ADM_LOG_COUNTERS) {
		ast->added_backlog_atomically++;
		ast->backlog_sum_inc_atomically += amt;
		ast->backlog_sum_atomically += new_backlog;
	}
}

static inline __attribute__((always_inline))
void adm_log_dequeued_bin_in(
		struct admission_core_statistics *st, uint16_t bin_index,
		uint16_t bin_size) {
	if (MAINTAIN_ADM_LOG_COUNTERS) {
		uint32_t hist_bin;

		/* mainatain histogram */
		hist_bin = bin_index >> BIN_SIZE_HISTOGRAM_SHIFT;
		if (unlikely(hist_bin >= BIN_SIZE_HISTOGRAM_NUM_BINS))
			hist_bin = BIN_SIZE_HISTOGRAM_NUM_BINS - 1;
		st->bin_size_histogram[hist_bin]+= bin_size;
	}
}

static inline __attribute__((always_inline))
void adm_log_waiting_for_q_bin_in(
		struct admission_core_statistics *st, uint16_t bin) {
	if (MAINTAIN_ADM_LOG_COUNTERS)
		st->wait_for_q_bin_in++;
}

static inline __attribute__((always_inline))
void adm_log_pacing_wait(
		struct admission_core_statistics *st, uint16_t bin) {
	if (MAINTAIN_ADM_LOG_COUNTERS)
		st->pacing_wait++;
}

static inline __attribute__((always_inline))
void adm_log_waiting_for_head(
		struct admission_core_statistics *st) {
	if (MAINTAIN_ADM_LOG_COUNTERS)
		st->wait_for_head++;
}



#endif /* ADMISSIBLE_ALGO_LOG_H_ */

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

#define		MAINTAIN_CORE_BIN_HISTOGRAM		1
#define		CORE_BIN_HISTOGRAM_NUM_BINS		16
#define		CORE_BIN_HISTOGRAM_SHIFT		2

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
	uint64_t core_bins_histogram[CORE_BIN_HISTOGRAM_NUM_BINS];

	uint64_t admitted_traffic_alloc_failed;
	uint64_t wait_for_space_in_q_bin_out;
	uint64_t wait_for_space_in_q_admitted_out;
	uint64_t out_bin_alloc_failed;
	uint64_t q_out_flush_bin_full;
	uint64_t q_out_flush_batch_finished;
	uint64_t new_request_bins;
	uint64_t new_requests;
	uint64_t waiting_to_pass_token;
	uint64_t passed_bins_during_wrap_up;
	uint64_t carried_over_bins_during_wrap_up;
	uint64_t wrap_up_non_empty_bin;
	uint64_t wrap_up_non_empty_bin_demands;

        /* pim-specific statistics */
        uint64_t phase_finished;
        uint64_t phase_none_ready;
        uint64_t phase_out_of_order;
};

/**
 * Global statistics in struct admissible_status
 */
struct admission_statistics {
	uint64_t wait_for_space_in_q_head;
	uint64_t new_demands_bin_alloc_failed;
	/* atomic increase statistics */
	uint64_t added_backlog_atomically;
	uint64_t backlog_sum_atomically;
	uint64_t backlog_sum_inc_atomically;
	/* to queue statistics */
	uint64_t added_backlog_to_queue;
	uint64_t backlog_sum_to_queue;
	uint64_t backlog_sum_inc_to_queue;
	uint64_t backlog_flush_forced;
	uint64_t backlog_flush_bin_full;
};

/* GLOBAL STATS (in admissible_status) */
static inline __attribute__((always_inline))
void adm_log_wait_for_space_in_q_head(
		struct admission_statistics *st) {
	if (MAINTAIN_ADM_LOG_COUNTERS)
		st->wait_for_space_in_q_head++;
}

static inline __attribute__((always_inline))
void adm_log_new_demands_bin_alloc_failed(
		struct admission_statistics *st) {
	if (MAINTAIN_ADM_LOG_COUNTERS)
		st->new_demands_bin_alloc_failed++;
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
void adm_log_forced_backlog_flush(
		struct admission_statistics *st) {
	if (MAINTAIN_ADM_LOG_COUNTERS)
		st->backlog_flush_forced++;
}

static inline __attribute__((always_inline))
void adm_log_backlog_flush_bin_full(
		struct admission_statistics *st) {
	if (MAINTAIN_ADM_LOG_COUNTERS)
		st->backlog_flush_bin_full++;
}

/* PER CORE STATS */

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
void adm_log_dequeued_bin_in(
		struct admission_core_statistics *st, uint16_t bin_size) {
	if (MAINTAIN_ADM_LOG_COUNTERS) {
		uint32_t hist_bin;

		/* mainatain histogram */
		hist_bin = bin_size >> BIN_SIZE_HISTOGRAM_SHIFT;
		if (unlikely(hist_bin >= BIN_SIZE_HISTOGRAM_NUM_BINS))
			hist_bin = BIN_SIZE_HISTOGRAM_NUM_BINS - 1;
		st->bin_size_histogram[hist_bin]++;
	}
}

static inline __attribute__((always_inline))
void adm_log_processed_core_bin(
		struct admission_core_statistics *st, uint16_t bin_index,
		uint16_t bin_size) {
	if (MAINTAIN_CORE_BIN_HISTOGRAM) {
		uint32_t hist_bin;

		/* mainatain histogram */
		hist_bin = bin_index >> CORE_BIN_HISTOGRAM_SHIFT;
		if (unlikely(hist_bin >= CORE_BIN_HISTOGRAM_NUM_BINS))
			hist_bin = BIN_SIZE_HISTOGRAM_NUM_BINS - 1;
		st->core_bins_histogram[hist_bin]+= bin_size;
	}
}


static inline __attribute__((always_inline))
void adm_log_passed_bins_during_wrap_up(
		struct admission_core_statistics *st, uint32_t n_bins) {
	if (MAINTAIN_ADM_LOG_COUNTERS) {
		st->passed_bins_during_wrap_up += n_bins;
	}
}

static inline __attribute__((always_inline))
void adm_log_carried_over_bins_during_wrap_up(
		struct admission_core_statistics *st, uint32_t n_bins) {
	if (MAINTAIN_ADM_LOG_COUNTERS) {
		st->carried_over_bins_during_wrap_up += n_bins;
	}
}


static inline __attribute__((always_inline))
void adm_log_wrap_up_non_empty_bin(
		struct admission_core_statistics *st, uint32_t bin_size) {
	if (MAINTAIN_ADM_LOG_COUNTERS) {
		st->wrap_up_non_empty_bin++;
		st->wrap_up_non_empty_bin_demands += bin_size;
	}
}

static inline __attribute__((always_inline))
void adm_log_wait_for_space_in_q_bin_out(
		struct admission_core_statistics *st) {
	if (MAINTAIN_ADM_LOG_COUNTERS)
		st->wait_for_space_in_q_bin_out++;
}

static inline __attribute__((always_inline))
void adm_log_wait_for_space_in_q_admitted_traffic(
		struct admission_core_statistics *st) {
	if (MAINTAIN_ADM_LOG_COUNTERS)
		st->wait_for_space_in_q_admitted_out++;
}

static inline __attribute__((always_inline))
void adm_log_admitted_traffic_alloc_failed(
		struct admission_core_statistics *st) {
	if (MAINTAIN_ADM_LOG_COUNTERS)
		st->admitted_traffic_alloc_failed++;
}

static inline __attribute__((always_inline))
void adm_log_out_bin_alloc_failed(
		struct admission_core_statistics *st) {
	if (MAINTAIN_ADM_LOG_COUNTERS)
		st->out_bin_alloc_failed++;
}

static inline __attribute__((always_inline))
void adm_log_q_out_flush_bin_full(
		struct admission_core_statistics *st) {
	if (MAINTAIN_ADM_LOG_COUNTERS)
		st->q_out_flush_bin_full++;
}

static inline __attribute__((always_inline))
void adm_log_q_out_flush_batch_finished(
		struct admission_core_statistics *st) {
	if (MAINTAIN_ADM_LOG_COUNTERS)
		st->q_out_flush_batch_finished++;
}

static inline __attribute__((always_inline))
void adm_log_processed_new_requests(
		struct admission_core_statistics *st, int num_bins,
		uint32_t num_demands) {
	if (MAINTAIN_ADM_LOG_COUNTERS) {
		st->new_request_bins += num_bins;
		st->new_requests += num_demands;
	}
}

static inline __attribute__((always_inline))
void adm_log_wait_for_q_bin_out_enqueue_token(
		struct admission_core_statistics *st) {
	if (MAINTAIN_ADM_LOG_COUNTERS)
		st->waiting_to_pass_token++;
}

static inline __attribute__((always_inline))
void adm_log_phase_finished(
		struct admission_core_statistics *st) {
	if (MAINTAIN_ADM_LOG_COUNTERS)
		st->phase_finished++;
}

static inline __attribute__((always_inline))
void adm_log_phase_none_ready(
		struct admission_core_statistics *st) {
	if (MAINTAIN_ADM_LOG_COUNTERS)
		st->phase_none_ready++;
}

static inline __attribute__((always_inline))
void adm_log_phase_out_of_order(
		struct admission_core_statistics *st) {
	if (MAINTAIN_ADM_LOG_COUNTERS)
		st->phase_out_of_order++;
}

#endif /* ADMISSIBLE_ALGO_LOG_H_ */

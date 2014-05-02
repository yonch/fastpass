/*
 * admissible_traffic.c
 *
 *  Created on: November 19, 2013
 *      Author: aousterh
 */

#include "admissible_traffic.h"

#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

#define MAX(a,b) (((a) > (b)) ? (a) : (b))

#define RING_DEQUEUE_BURST_SIZE		256

#define URGENT_NUM_TIMESLOTS_START		BATCH_SIZE
#define URGENT_NUM_TIMESLOTS_END		1

/**
 * Flushes bin to queue, and allocates a new bin
 */
static inline __attribute__((always_inline))
void core_flush_q_out(struct admission_core_state *core,
		struct fp_ring *queue, struct fp_mempool *bin_mempool)
{
	/* enqueue status->new_backlogs */
	while(fp_ring_enqueue(queue, core->out_bin) == -ENOBUFS)
		adm_log_wait_for_space_in_q_bin_out(&core->stat);

	/* get a fresh bin for status->new_backlogs */
	while(fp_mempool_get(bin_mempool, (void**)&core->out_bin) == -ENOENT)
		adm_log_out_bin_alloc_failed(&core->stat);

	init_bin(core->out_bin);
}

static inline __attribute__((always_inline))
void core_enqueue_to_q_out(struct admission_core_state *core,
		struct fp_ring *queue_out, struct fp_mempool *bin_mempool,
		uint16_t src, uint16_t dst, uint32_t metric)
{
	/* add to status->new_demands */
	enqueue_bin(core->out_bin, src, dst, metric);

	if (unlikely(bin_size(core->out_bin) == SMALL_BIN_SIZE)) {
		adm_log_q_out_flush_bin_full(&core->stat);
		core_flush_q_out(core, queue_out, bin_mempool);
	}
}

static inline __attribute__((always_inline))
void core_enqueue_to_q_out_edge(struct admission_core_state *core,
		struct fp_ring *queue_out, struct fp_mempool *bin_mempool,
		struct backlog_edge *edge)
{
	/* add to status->new_demands */
	enqueue_bin_edge(core->out_bin, edge);

	if (unlikely(bin_size(core->out_bin) == SMALL_BIN_SIZE)) {
		adm_log_q_out_flush_bin_full(&core->stat);
		core_flush_q_out(core, queue_out, bin_mempool);
	}
}

/**
 * Flushes bin to queue, and allocates a new bin
 */
static inline __attribute__((always_inline))
void _flush_backlog_now(struct admissible_status *status)
{
	/* enqueue status->new_demands */
	while(fp_ring_enqueue(status->q_head, status->new_demands) == -ENOBUFS)
		adm_log_wait_for_space_in_q_head(&status->stat);

	/* get a fresh bin for status->new_demands */
	while(fp_mempool_get(status->bin_mempool,
						  (void**)&status->new_demands) == -ENOENT)
		adm_log_new_demands_bin_alloc_failed(&status->stat);

	init_bin(status->new_demands);
}


void flush_backlog(struct admissible_status *status) {
	if (unlikely(is_empty_bin(status->new_demands)))
		return;
	adm_log_forced_backlog_flush(&status->stat);
	_flush_backlog_now(status);
}

void add_backlog(struct admissible_status *status,
		uint16_t src, uint16_t dst, uint32_t amount)
{
	if (backlog_increase(&status->backlog, src, dst, amount,
			&status->stat) == false)
		return; /* no need to enqueue */

	/* add to status->new_demands */
	enqueue_bin(status->new_demands, src, dst,
			status->last_alloc_tslot[get_status_index(src,dst)]);

	if (unlikely(bin_size(status->new_demands) == SMALL_BIN_SIZE)) {
		adm_log_backlog_flush_bin_full(&status->stat);
		_flush_backlog_now(status);
	}

}

static inline __attribute__((always_inline))
void set_bin_non_empty(struct admission_core_state *core, uint16_t bin_index)
{
	asm("bts %1,%0" : "+m" (*(uint64_t *)&core->non_empty_bins[0]) : "r" (bin_index));
}

static inline __attribute__((always_inline))
void incoming_bin_to_core(struct admissible_status *status,
		struct admission_core_state *core, struct bin *bin)
{
	uint32_t n = bin_size(bin);
	uint32_t i;
	for (i = 0; i < n; i++) {
		/* where to put the entry? */
		uint16_t bin_index = bin_index_from_timeslot(bin_get(bin, i)->metric,
													core->current_timeslot);
		/* put it there */
		enqueue_bin_edge(core->new_request_bins[bin_index], bin_get(bin, i));
		/* mark that the bin is non-empty */
		set_bin_non_empty(core, bin_index);
	}
}

static inline __attribute__((always_inline))
void move_bin_to_q_out(struct admissible_status *status,
		struct admission_core_state *core, struct fp_ring *queue_out,
        struct fp_mempool *bin_mp_out, struct bin *bin)
{
	uint32_t n = bin_size(bin);
	uint32_t i;
	for (i = 0; i < n; i++)
		core_enqueue_to_q_out_edge(core, queue_out, bin_mp_out, bin_get(bin, i));
}

static inline __attribute__((always_inline))
void move_core_to_q_out(struct admissible_status *status,
		struct admission_core_state *core, struct fp_ring *queue_out,
        struct fp_mempool *bin_mp_out)
{
	uint32_t bin_mask_ind;

	for (bin_mask_ind = 0; bin_mask_ind < BIN_MASK_SIZE; bin_mask_ind++) {
		uint64_t mask = core->non_empty_bins[bin_mask_ind];
		uint64_t bin_index;
		while (mask) {
			/* get the index of the lsb that is set */
			asm("bsfq %1,%0" : "=r"(bin_index) : "r"(mask));
			/* turn off the set bit in the mask */
			mask &= (mask - 1);
			bin_index += 64 * bin_mask_ind;
			struct bin *bin = core->new_request_bins[bin_index];
			adm_log_wrap_up_non_empty_bin(&core->stat, bin_size(bin));
			move_bin_to_q_out(status, core, queue_out, bin_mp_out, bin);
		}
	}
}

/**
 * Try to allocate the given edge
 * Returns false, if the flow will be handled internally,
 * 		   true, if more backlog remains and should be handled by the caller
 */
static inline __attribute__((always_inline))
bool try_allocation(uint16_t src, uint16_t dst, uint32_t metric,
		struct admission_core_state *core,
		struct admissible_status *status)
{
	int32_t backlog;
    assert(core != NULL);
    assert(status != NULL);

	uint64_t timeslot_bitmap = batch_state_get_avail_bitmap(
			&core->batch_state, src, dst);

    if (timeslot_bitmap == 0ULL) {
    	adm_algo_log_no_available_timeslots_for_bin_entry(&core->stat, src, dst);
    	/* caller should handle allocation of this flow */
    	return true;
    }

	uint64_t batch_timeslot;
	asm("bsfq %1,%0" : "=r"(batch_timeslot) : "r"(timeslot_bitmap));

	// We can allocate this edge now
	batch_state_set_occupied(&core->batch_state, src, dst, batch_timeslot);
	metric = new_metric_after_alloc(src, dst, metric, batch_timeslot, core, status);

	insert_admitted_edge(core->admitted[batch_timeslot], src, dst);

	backlog = backlog_decrease(&status->backlog, src, dst);
	if (backlog != 0) {
    	adm_log_allocated_backlog_remaining(&core->stat, src, dst, backlog);
    	uint16_t bin_index = bin_after_alloc(src, dst, metric, batch_timeslot,
    			core, status);
		enqueue_bin(core->new_request_bins[bin_index], src, dst, metric);
		set_bin_non_empty(core, bin_index);
	} else {
		adm_log_allocator_no_backlog(&core->stat, src, dst);
		status->last_alloc_tslot[get_status_index(src, dst)] = metric;
	}

	return false;
}

static inline __attribute__((always_inline))
void try_allocation_bin(struct admission_core_state *core, uint64_t bin_index,
                    struct fp_ring *queue_out, struct admissible_status *status,
                    struct fp_mempool *bin_mp_out)
{
	bool rc;
    uint32_t i;
    struct bin *bin = core->new_request_bins[bin_index];
    uint32_t n_elem = bin_size(bin);

    for (i = 0; i < n_elem; i++) {
		uint16_t src = bin_get(bin,i)->src;
		uint16_t dst = bin_get(bin,i)->dst;
		uint32_t metric = bin_get(bin, i)->metric;

        rc = try_allocation(src, dst, metric, core, status);
        if (rc == true) {
			// We cannot allocate this edge now - copy to queue_out
			core_enqueue_to_q_out(core, queue_out, bin_mp_out,
					src, dst, metric);
        }
    }
}

static inline __attribute__((always_inline))
void try_allocation_core(struct admission_core_state *core,
                    struct fp_ring *queue_out, struct admissible_status *status,
                    struct fp_mempool *bin_mp_out)
{
	uint32_t bin_mask_ind;

	for (bin_mask_ind = 0; bin_mask_ind < BIN_MASK_SIZE; bin_mask_ind++) {
		uint64_t mask = core->non_empty_bins[bin_mask_ind];
		uint64_t bin_index;
		while (mask) {
			/* get the index of the lsb that is set */
			asm("bsfq %1,%0" : "=r"(bin_index) : "r"(mask));
			/* turn off the set bit in the mask */
			core->non_empty_bins[bin_mask_ind] = (mask & (mask - 1));
			bin_index += 64 * bin_mask_ind;

			try_allocation_bin(core, bin_index,
					queue_out, status, bin_mp_out);
			init_bin(core->new_request_bins[bin_index]);

			/* re-read mask */
			mask = core->non_empty_bins[bin_mask_ind];
		}
	}
}

// Sets the last send time for new requests based on the contents of status
// and sorts them
static inline void process_new_requests(struct admissible_status *status,
                          struct admission_core_state *core,
                          uint16_t current_bin)
{
    assert(status != NULL);
    assert(core != NULL);

    struct bin *bins[RING_DEQUEUE_BURST_SIZE];
    int n, i;
    uint32_t num_entries = 0;
    uint32_t num_bins = 0;

    n = fp_ring_dequeue_burst(status->q_head, (void **)&bins[0],
    		RING_DEQUEUE_BURST_SIZE);

    for (i = 0; i < n; i++) {
    	num_entries += bin_size(bins[i]);
    	num_bins++;
    	incoming_bin_to_core(status, core, bins[i]);
		fp_mempool_put(status->bin_mempool, bins[i]);
    }
    adm_log_processed_new_requests(&core->stat, num_bins, num_entries);
}

// Determine admissible traffic for one timeslot from queue_in
// Puts unallocated traffic in queue_out
// Allocate BATCH_SIZE timeslots at once
void get_admissible_traffic(struct admissible_status *status,
								uint32_t core_index,
								uint64_t first_timeslot, uint32_t tslot_mul,
								uint32_t tslot_shift)
{
    assert(status != NULL);

    struct admission_core_state *core = &status->cores[core_index];

    struct fp_ring *queue_in = status->q_bin[core_index];
    struct fp_ring *queue_out = status->q_bin[(core_index + 1) % ALGO_N_CORES];
    struct fp_mempool *bin_mp_in = status->bin_mempool;
    struct fp_mempool *bin_mp_out = status->bin_mempool;

    // Initialize this core for a new batch of processing
    alloc_core_reset(core, status);

    assert(core->out_bin != NULL);
    assert(is_empty_bin(core->out_bin));

    // Process all bins from previous core, then process all bins from
    // residual backlog from traffic admitted in this batch
    struct bin *bin_in, *bin_out;
    uint16_t bin = 0;
    uint64_t prev_timeslot = ((fp_get_time_ns() * tslot_mul) >> tslot_shift) - 1;
    uint16_t processed_bins = 0;
	uint32_t i;
    bool queue_in_done = false;

    while (fp_mempool_get_bulk(status->admitted_traffic_mempool,
    		(void **)&core->admitted[0], BATCH_SIZE) != 0)
    {
    	adm_log_admitted_traffic_alloc_failed(&core->stat);
    	process_new_requests(status, core, processed_bins - 1);
    }
    for (i = 0; i < BATCH_SIZE; i++)
        init_admitted_traffic(core->admitted[i]);


    while (1) {
#ifdef NO_DPDK
    	/* for benchmark */
    	uint64_t now_timeslot = queue_in_done ? first_timeslot + BATCH_SIZE : first_timeslot - NUM_BINS - 1;
		for (i = 0; i < 10; i++)
			process_new_requests(status, core, processed_bins - 1);
#else
    	uint64_t now_timeslot = (fp_get_time_ns() * tslot_mul) >> tslot_shift;
#endif

    	if (likely(now_timeslot == prev_timeslot))
    		goto handle_inputs;

    	prev_timeslot = now_timeslot;

		/* if time is not close enough to process bins, continue */
		if (likely(time_before64((__u64)now_timeslot, (__u64)(first_timeslot - NUM_BINS))))
			goto handle_inputs;

		uint64_t slot_gap = now_timeslot - first_timeslot + NUM_BINS + 1;
		uint16_t new_processed_bins =
				(slot_gap > BATCH_SIZE + NUM_BINS) ? BATCH_SIZE + NUM_BINS : slot_gap;

		for (bin = processed_bins; bin < new_processed_bins; bin++) {
			if (likely(bin >= NUM_BINS)) {
				/* send out the admitted traffic */
				while(fp_ring_enqueue(status->q_admitted_out,
						core->admitted[bin - NUM_BINS]) == -ENOBUFS)
					adm_log_wait_for_space_in_q_admitted_traffic(&core->stat);

				/* disallow that timeslot */
				batch_state_disallow_lsb_timeslot(&core->batch_state);
			}
		}
		processed_bins = new_processed_bins;

		if (unlikely(processed_bins == BATCH_SIZE + NUM_BINS))
			goto wrap_up;

handle_inputs:
    	/* process new requests if this core is responsible for them */
    	if (unlikely(   (first_timeslot - now_timeslot > URGENT_NUM_TIMESLOTS_END)
				     && (first_timeslot - now_timeslot <= URGENT_NUM_TIMESLOTS_START)))
		{
			process_new_requests(status, core, processed_bins - 1);
		}

    	/* try to dequeue a bin from queue_in */
    	if (likely(   !queue_in_done
    			   && fp_ring_dequeue(queue_in, (void **)&bin_in) == 0))
    	{
    		if (unlikely(bin_in == NULL)) {
    			queue_in_done = true;
    		} else {
    			adm_log_dequeued_bin_in(&core->stat, processed_bins, bin_size(bin_in));
    			incoming_bin_to_core(status, core, bin_in);
    			fp_mempool_put(bin_mp_in, bin_in);
    		}
    	}

		try_allocation_core(core, queue_out, status, bin_mp_out);
    }

wrap_up:
	/* copy all demands to output. no need to process */
	move_core_to_q_out(status, core, queue_out, bin_mp_out);
	/* flush q_out if there is more there */
	if (!is_empty_bin(core->out_bin)) {
		adm_log_q_out_flush_batch_finished(&core->stat);
		core_flush_q_out(core, queue_out, bin_mp_out);
	}
	/* go through all remaining bins in q_in*/
	while (!queue_in_done) {
		if(fp_ring_dequeue(queue_in, (void **)&bin_in) == 0) {
    		if (unlikely(bin_in == NULL)) {
    			queue_in_done = true;
    		} else {
    			adm_log_dequeued_bin_during_wrap_up(&core->stat, bin_size(bin_in));
    			while(fp_ring_enqueue(queue_out, bin_in) == -ENOBUFS)
    				adm_log_wait_for_space_in_q_bin_out(&core->stat);
    		}
		}
	}

    while(fp_ring_enqueue(queue_out, NULL) == -ENOBUFS)
    	adm_log_wait_for_q_bin_out_enqueue_token(&core->stat);

    // Update current timeslot
    core->current_timeslot += ALGO_N_CORES * BATCH_SIZE;
}

// Reset state of all flows for which src is the sender
void reset_sender(struct admissible_status *status, uint16_t src) {
    assert(status != NULL);

    // Do not change last send timeslots

    // Reset pending demands
    uint16_t dst;
    for (dst = 0; dst < MAX_NODES; dst++) {
        backlog_reset_pair(&status->backlog, src, dst);
    }
}

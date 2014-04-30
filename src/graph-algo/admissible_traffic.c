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

#define EDGE_BIN(edge)		((uint16_t)(edge >> 32))
#define EDGE_SRC(edge)		((uint16_t)(edge >> 16))
#define EDGE_DST(edge)		((uint16_t)(edge	  ))

#define RING_DEQUEUE_BURST_SIZE		256

#define URGENT_NUM_TIMESLOTS_START		BATCH_SIZE
#define URGENT_NUM_TIMESLOTS_END		6

/* auxiliary functions */

/**
 * Flushes bin to queue, and allocates a new bin
 */
static inline __attribute__((always_inline))
void _flush_bin_now(struct bin **bin_pp, struct fp_ring *queue,
		struct fp_mempool *bin_mempool)
{
	/* enqueue status->new_backlogs */
	while(fp_ring_enqueue(queue, *bin_pp) == -ENOBUFS)
		/* retry */;

	/* get a fresh bin for status->new_backlogs */
	while(fp_mempool_get(bin_mempool, (void**)bin_pp) == -ENOENT)
		/* retry */;

	init_bin(*bin_pp);
}

static inline __attribute__((always_inline))
void bin_enqueue_buffered(struct bin **bin_pp, struct fp_ring *queue,
		struct fp_mempool *bin_mempool, uint16_t src, uint16_t dst,
		uint32_t metric)
{
	/* add to status->new_demands */
	enqueue_bin(*bin_pp, src, dst, metric);

	if (unlikely(bin_size(*bin_pp) == SMALL_BIN_SIZE))
		_flush_bin_now(bin_pp, queue, bin_mempool);
}

/* end auxiliary functions */

void flush_backlog(struct admissible_status *status) {
	if (unlikely(is_empty_bin(status->new_demands)))
		return;
	_flush_bin_now(&status->new_demands, status->q_head, status->head_bin_mempool);
}

void add_backlog(struct admissible_status *status,
		uint16_t src, uint16_t dst, uint32_t amount)
{
	if (backlog_increase(&status->backlog, src, dst, amount,
			&status->stat) == false)
		return; /* no need to enqueue */

	/* add to status->new_demands */
	bin_enqueue_buffered(&status->new_demands, status->q_head,
			status->head_bin_mempool, src, dst,
			status->last_alloc_tslot[get_status_index(src,dst)]);
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
													status->current_timeslot);
		/* put it there */
		enqueue_bin_edge(core->new_request_bins[bin_index], bin_get(bin, i));
		/* mark that the bin is non-empty */
		set_bin_non_empty(core, bin_index);
	}
}

/**
 * Try to allocate the given edge
 * Returns false, if the flow will be handled internally,
 * 		   true, if more backlog remains and should be handled by the caller
 */
static inline bool try_allocation(uint16_t src, uint16_t dst, uint32_t metric,
		struct admission_core_state *core,
		struct admissible_status *status)
{
	int32_t backlog;
    assert(core != NULL);
    assert(status != NULL);

	uint32_t index = get_status_index(src, dst);
	__builtin_prefetch(&status->last_alloc_tslot[index], 1, 1);

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
		status->last_alloc_tslot[index] = metric;
	}

	return false;
}

static void try_allocation_bin(struct bin *in_bin, struct admission_core_state *core,
                    struct fp_ring *queue_out, struct admissible_status *status,
                    struct fp_mempool *bin_mp_out)
{
	bool rc;
    uint32_t i;
    uint32_t n_elem = bin_size(in_bin);

    for (i = 0; i < n_elem; i++) {
		uint16_t src = bin_get(in_bin,i)->src;
		uint16_t dst = bin_get(in_bin,i)->dst;
		uint32_t metric = bin_get(in_bin, i)->metric;

        rc = try_allocation(src, dst, metric, core, status);
        if (rc == true) {
			// We cannot allocate this edge now - copy to queue_out
			bin_enqueue_buffered(&core->out_bin, queue_out, bin_mp_out,
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

			try_allocation_bin(core->new_request_bins[bin_index], core,
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

    n = fp_ring_dequeue_burst(status->q_head, (void **)&bins[0],
    		RING_DEQUEUE_BURST_SIZE);

    for (i = 0; i < n; i++) {
    	incoming_bin_to_core(status, core, bins[i]);
		fp_mempool_put(status->head_bin_mempool, bins[i]);
    }
}

// Determine admissible traffic for one timeslot from queue_in
// Puts unallocated traffic in queue_out
// Allocate BATCH_SIZE timeslots at once
void get_admissible_traffic(struct admissible_status *status,
								uint32_t core_index,
								struct admitted_traffic **admitted,
								uint64_t first_timeslot, uint32_t tslot_mul,
								uint32_t tslot_shift)
{
    assert(status != NULL);

    struct admission_core_state *core = &status->cores[core_index];

    // TODO: use multiple cores
    struct fp_ring *queue_in = status->q_bin[core_index];
    struct fp_ring *queue_out = status->q_bin[(core_index + 1) % ALGO_N_CORES];
    struct fp_mempool *bin_mp_in = status->core_bin_mempool[core_index];
    struct fp_mempool *bin_mp_out = status->core_bin_mempool[(core_index + 1) % ALGO_N_CORES];

    // Initialize this core for a new batch of processing
    alloc_core_reset(core, status, admitted);

    assert(core->out_bin != NULL);
    assert(is_empty_bin(core->out_bin));

    // Process all bins from previous core, then process all bins from
    // residual backlog from traffic admitted in this batch
    struct bin *bin_in, *bin_out;
    uint16_t bin = 0;
    uint64_t prev_timeslot = ((fp_get_time_ns() * tslot_mul) >> tslot_shift) - 1;
    uint16_t processed_bins = 0;
    bool queue_in_done = false;

    process_new_requests(status, core, 0);

    while (1) {
#ifdef NO_DPDK
    	/* for benchmark */
    	uint64_t now_timeslot = queue_in_done ? first_timeslot + BATCH_SIZE : first_timeslot - NUM_BINS - 1;
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
					status->stat.wait_for_space_in_q_admitted_out++;

				/* disallow that timeslot */
				batch_state_disallow_lsb_timeslot(&core->batch_state);
			}

			try_allocation_bin(core->new_request_bins[bin], core, queue_out,
					status, bin_mp_out);
		}
		processed_bins = new_processed_bins;

		if (unlikely(queue_in_done && (processed_bins == BATCH_SIZE + NUM_BINS)))
			goto wrap_up;

handle_inputs:
    	/* process new requests if this core is responsible for them */
//    	if (unlikely(   (first_timeslot - now_timeslot > URGENT_NUM_TIMESLOTS_END)
//				     && (first_timeslot - now_timeslot <= URGENT_NUM_TIMESLOTS_START)))
//		{
			process_new_requests(status, core, processed_bins - 1);
//		}

    	/* try to dequeue a bin from queue_in */
    	if (likely(   !queue_in_done
    			   && fp_ring_dequeue(queue_in, (void **)&bin_in) == 0))
    	{
    		if (unlikely(bin_in == NULL)) {
    			queue_in_done = true;
    		} else {
    			adm_log_dequeued_bin_in(&core->stat, bin++, bin_size(bin_in));
    			incoming_bin_to_core(status, core, bin_in);
    			fp_mempool_put(bin_mp_in, bin_in);
    		}
    	}

		try_allocation_core(core, queue_out, status, bin_mp_out);
    }

wrap_up:
	if (!is_empty_bin(core->out_bin))
		_flush_bin_now(&core->out_bin, queue_out, bin_mp_out);

    while(fp_ring_enqueue(queue_out, NULL) == -ENOBUFS)
    	status->stat.waiting_to_pass_token++;

    // Update current timeslot
    status->current_timeslot += BATCH_SIZE;
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

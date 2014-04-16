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

#define MAKE_EDGE(bin,src,dst) \
	((void*)(((uint64_t)bin << 32) | ((uint32_t)src << 16) | dst))

#define EDGE_BIN(edge)		((uint16_t)(edge >> 32))
#define EDGE_SRC(edge)		((uint16_t)(edge >> 16))
#define EDGE_DST(edge)		((uint16_t)(edge	  ))

#define RING_DEQUEUE_BURST_SIZE		256

bool add_backlog_no_enqueue(struct admissible_status *status, uint16_t src,
        uint16_t dst, uint32_t backlog_increase, void **out_edge)
{
	int32_t backlog;
    assert(status != NULL);

    // Get full quantity from 16-bit LSB
    uint32_t index = get_status_index(src, dst);

    backlog = atomic32_add_return(&status->flows[index].backlog, backlog_increase);
	if (backlog == backlog_increase) {
		adm_log_increased_backlog_to_queue(&status->stat, backlog_increase, backlog);
		*out_edge = MAKE_EDGE(0,src,dst);
		return true;
	}

	adm_log_increased_backlog_atomically(&status->stat, backlog_increase, backlog);
	return false;
}

// Request num_slots additional timeslots from src to dst
void add_backlog(struct admissible_status *status, uint16_t src,
                       uint16_t dst, uint32_t backlog_increase) {
	void *edge;

	if (add_backlog_no_enqueue(status, src, dst, backlog_increase, &edge) == false) {
		return; /* no need to enqueue */
	}

	/* need to enqueue */
	while (fp_ring_enqueue(status->q_head, edge) == -ENOBUFS)
		status->stat.wait_for_space_in_q_head++;
}

/**
 * Try to allocate the given edge
 * Returns 0 if the flow will be handled internally,
 * 		   1 if more backlog remains and should be handled by the caller
 */
static inline int try_allocation(uint16_t src, uint16_t dst,
		struct admission_core_state *core,
		struct admissible_status *status)
{
	int32_t backlog;
    assert(core != NULL);
    assert(status != NULL);

	uint32_t index = get_status_index(src, dst);
	__builtin_prefetch(&status->flows[index].backlog, 1, 0);

	uint64_t timeslot_bitmap = get_available_timeslot_bitmap(
			&core->batch_state, src, dst);

    if (timeslot_bitmap == 0ULL) {
    	adm_algo_log_no_available_timeslots_for_bin_entry(&core->stat, src, dst);
    	/* caller should handle allocation of this flow */
    	return 1;
    }

	uint64_t batch_timeslot;
	asm("bsfq %1,%0" : "=r"(batch_timeslot) : "r"(timeslot_bitmap));

	// We can allocate this edge now
	set_timeslot_occupied(&core->batch_state, src, dst, batch_timeslot);

	insert_admitted_edge(core->admitted[batch_timeslot], src, dst);
	status->last_alloc_tslot[index] = status->current_timeslot + batch_timeslot;

	backlog = atomic32_sub_return(&status->flows[index].backlog, 1);
	if (backlog != 0) {
    	adm_log_allocated_backlog_remaining(&core->stat, src, dst, backlog);
		enqueue_bin(core->new_request_bins[NUM_BINS + batch_timeslot], src, dst);
	} else {
		adm_log_allocator_no_backlog(&core->stat, src, dst);
	}

	return 0;
}

static void try_allocation_bin(struct bin *in_bin, struct admission_core_state *core,
                    struct bin *bin_out, struct admissible_status *status)
{
	int rc;
    uint16_t i;
    uint16_t tail = in_bin->tail;
    for (i = in_bin->head; i < tail; i++) {
		uint16_t src = in_bin->edges[i].src;
		uint16_t dst = in_bin->edges[i].dst;

        rc = try_allocation(src, dst, core, status);
        if (rc == 1) {
			// We cannot allocate this edge now - copy to queue_out
			enqueue_bin(bin_out, src, dst);
        }
    }
}

static inline void process_one_new_request(uint16_t src, uint16_t dst,
		uint16_t bin_index, struct admission_core_state* core,
		struct admissible_status* status, uint16_t current_bin)
{
	if (bin_index < current_bin) {
		// We have already processed the bin for this src/dst pair
		// Try to allocate immediately
		if (try_allocation(src, dst, core, status) == 1) {
			/* couldn't process, pass on to next core */
			bin_index = (bin_index >= 2 * BATCH_SIZE) ?
							(bin_index - BATCH_SIZE) :
							(bin_index / 2);
			while(fp_ring_enqueue(core->q_urgent_out,
					MAKE_EDGE(bin_index, src, dst)) == -ENOBUFS)
				status->stat.wait_for_space_in_q_urgent++;
		}
	} else {
		// We have not yet processed the bin for this src/dst pair
		// Enqueue it to the working bins for later processing
		enqueue_bin(core->new_request_bins[bin_index], src, dst);
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

    uint64_t edges[RING_DEQUEUE_BURST_SIZE];
    int n;
    int i;

    /* likely because we want fast branch prediction when is_head = true */
    if (likely(core->is_head))
    	goto process_head;

process_q_urgent:
    n = fp_ring_dequeue_burst(core->q_urgent_in, (void **)&edges[0],
    		RING_DEQUEUE_BURST_SIZE);
    for (i = 0; i < n; i++) {
    	uint64_t edge = edges[i];
        if (unlikely(edge == URGENT_Q_HEAD_TOKEN)) {
        	/* got token! */
        	core->is_head = 1; /* TODO: what about the entries i+1..n-1 ?? */
        	goto process_head;
        }

        process_one_new_request(EDGE_SRC(edge), EDGE_DST(edge), EDGE_BIN(edge),
        		core, status, current_bin);
    }
//    adm_log_processed_q_urgent(&core->stat, current_bin, n);
    if (n > 0)
    	goto process_q_urgent;

    return;

process_head:
    // Add new requests to the appropriate working bin
	n = fp_ring_dequeue_burst(status->q_head, (void **)&edges[0],
			RING_DEQUEUE_BURST_SIZE);

	for (i = 0; i < n; i++) {
    	uint64_t edge = edges[i];
        uint16_t src = EDGE_SRC(edge);
    	uint16_t dst = EDGE_DST(edge);
        uint32_t index = get_status_index(src, dst);
  
        uint16_t bin_index = bin_index_from_timeslot(
        		status->last_alloc_tslot[index], status->current_timeslot);
        
        process_one_new_request(src, dst, bin_index, core, status, current_bin);
    }
//	if (n > 0)
//		goto process_head;
}

// Determine admissible traffic for one timeslot from queue_in
// Puts unallocated traffic in queue_out
// Allocate BATCH_SIZE timeslots at once
void get_admissible_traffic(struct admission_core_state *core,
								struct admissible_status *status,
								struct admitted_traffic **admitted,
								uint64_t first_timeslot, uint32_t tslot_mul,
								uint32_t tslot_shift)
{
    assert(status != NULL);

    // TODO: use multiple cores
    struct fp_ring *queue_in = core->q_bin_in;
    struct fp_ring *queue_out = core->q_bin_out;

    // Initialize this core for a new batch of processing
    alloc_core_reset(core, status, admitted);

    // Process all bins from previous core, then process all bins from
    // residual backlog from traffic admitted in this batch
    struct bin *bin_in, *bin_out;
    uint16_t bin;

    bin_out = core->temporary_bins[0];
    assert(is_empty_bin(bin_out) && (bin_out->head == 0));

    process_new_requests(status, core, 0);

    for (bin = 0; bin < NUM_BINS + BATCH_SIZE - 1; bin++) {

    	if (likely(bin < NUM_BINS)) {
			while (fp_ring_dequeue(queue_in, (void **)&bin_in) != 0) {
				adm_log_waiting_for_q_bin_in(&core->stat, bin);
				process_new_requests(status, core, bin);
			}
			adm_log_dequeued_bin_in(&core->stat, bin, bin_in->tail - bin_in->head);
			try_allocation_bin(bin_in, core, bin_out, status);
    	} else {
    	    /* wait for start time */
    	    if (bin % 4 == 0) {
    	    	uint64_t start_timeslot = first_timeslot + (bin - NUM_BINS);
    	    	uint64_t now_timeslot;

process_more_requests:
				/* process requests */
				process_new_requests(status, core, bin);
				/* at least until the next core had finished allocating */
				/*  and we reach the start time */
				now_timeslot = (fp_get_time_ns() * tslot_mul) >> tslot_shift;

        	    if (unlikely(now_timeslot < start_timeslot)) {
        	    	adm_log_pacing_wait(&core->stat, bin);
        	    	goto process_more_requests;
        	    } else if (unlikely(!core->is_head)) {
        	    	adm_log_waiting_for_head(&core->stat);
        	    	goto process_more_requests;
        	    }
    	    }

    	    /* enqueue the allocated traffic for this timeslot */
    	    while(fp_ring_enqueue(status->q_admitted_out,
    	    		core->admitted[bin - NUM_BINS]) == -ENOBUFS)
    	    	status->stat.wait_for_space_in_q_admitted_out++;

    	    /* disallow further allocations to that timeslot */
    	    core->batch_state.allowed_mask <<= 1;
    	    core->batch_state.allowed_mask &= (1ULL << BATCH_SIZE) - 1;
    	    /* will process flows that this batch had previously allocated to timeslot */
    	    bin_in = core->new_request_bins[bin];
    	}

		try_allocation_bin(core->new_request_bins[bin], core, bin_out, status);

		if (likely(bin & ((~0UL << (BATCH_SHIFT+1)) | 1))) {
			while(fp_ring_enqueue(queue_out, bin_out) == -ENOBUFS)
				status->stat.wait_for_space_in_q_bin_out++;
			bin_out = bin_in;
	    	init_bin(bin_out);
		} else {
			/* we keep the same bin_out to fold 2-into-1. */
			core->temporary_bins[bin / 2] = bin_in;
		}
    }

    /* enqueue the last allocated timeslot */
    while(fp_ring_enqueue(status->q_admitted_out,
    		core->admitted[BATCH_SIZE - 1]) == -ENOBUFS)
    	status->stat.wait_for_space_in_q_bin_out++;

    /* hand over token to next core. this should happen after enqueuing _all_
     * allocated_traffic structs, to prevent allocation re-ordering */
    while(fp_ring_enqueue(core->q_urgent_out, (void*)URGENT_Q_HEAD_TOKEN) == -ENOBUFS)
    	status->stat.waiting_to_pass_token++;

    /* enqueue the last bin in batch as-is, next batch will take care of it */
    while(fp_ring_enqueue(queue_out,
    		core->new_request_bins[NUM_BINS + BATCH_SIZE - 1]) == -ENOBUFS)
    	status->stat.wait_for_space_in_q_bin_out++;

    /* re-arrange memory */
    for (bin = 0; bin < BATCH_SIZE; bin++)
    	core->new_request_bins[NUM_BINS + bin] = core->temporary_bins[bin];
    core->temporary_bins[0] = bin_out;

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
        reset_flow(status, src, dst);
    }
}

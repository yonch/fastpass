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

// Request num_slots additional timeslots from src to dst
void add_backlog(struct admissible_status *status, uint16_t src,
                       uint16_t dst, uint16_t backlog_increase) {
    assert(status != NULL);

    // Get full quantity from 16-bit LSB
    uint32_t index = get_status_index(src, dst);

	if (atomic32_add_return(&status->flows[index].backlog, backlog_increase) == backlog_increase)
		fp_ring_enqueue(status->q_head, MAKE_EDGE(0,src,dst));
}

/**
 * Try to allocate the given edge
 * Returns 0 if the flow will be handled internally,
 * 		   1 if more backlog remains and should be handled by the caller
 */
int try_allocation(uint16_t src, uint16_t dst, struct allocation_core *core,
		struct admissible_status *status)
{
    assert(core != NULL);
    assert(status != NULL);

    uint8_t batch_timeslot = get_first_timeslot(&core->batch_state, src, dst);

    if (batch_timeslot == NONE_AVAILABLE)
    	/* caller should handle allocation of this flow */
    	return 1;

	// We can allocate this edge now
	set_timeslot_occupied(&core->batch_state, src, dst, batch_timeslot);

	insert_admitted_edge(core->admitted[batch_timeslot], src, dst);
	uint32_t index = get_status_index(src, dst);
	status->last_alloc_tslot[index] = status->current_timeslot + batch_timeslot;

	if (atomic32_sub_return(&status->flows[index].backlog, 1) != 0)
		enqueue_bin(core->new_request_bins[NUM_BINS + batch_timeslot], src, dst);

	return 0;
}

void try_allocation_bin(struct bin *in_bin, struct allocation_core *core,
                    struct bin *bin_out, struct admissible_status *status)
{
	int rc;
    while (!is_empty_bin(in_bin)) {
        struct backlog_edge *edge = peek_head_bin(in_bin);
        assert(edge != NULL);
        uint16_t src = edge->src;
    	uint16_t dst = edge->dst;

        rc = try_allocation(src, dst, core, status);
        if (rc == 1) {
			// We cannot allocate this edge now - copy to queue_out
			enqueue_bin(bin_out, src, dst);
        }

        dequeue_bin(in_bin);
    }
}

static inline void process_one_new_request(uint16_t src, uint16_t dst,
		uint16_t bin_index, struct allocation_core* core,
		struct admissible_status* status, uint16_t current_bin)
{
	if (bin_index < current_bin) {
		// We have already processed the bin for this src/dst pair
		// Try to allocate immediately
		if (try_allocation(src, dst, core, status) == 1) {
			/* couldn't process, pass on to next core */
			uint64_t edge;
			bin_index = (bin_index >= 2 * BATCH_SIZE) ?
							(bin_index - BATCH_SIZE) :
							(bin_index / 2);
			fp_ring_enqueue(core->q_urgent_out,
					MAKE_EDGE(bin_index, src, dst));
		}
	} else {
		// We have not yet processed the bin for this src/dst pair
		// Enqueue it to the working bins for later processing
		enqueue_bin(core->new_request_bins[bin_index], src, dst);
	}
}


// Sets the last send time for new requests based on the contents of status
// and sorts them
void process_new_requests(struct admissible_status *status,
                          struct allocation_core *core,
                          uint16_t current_bin) {
    assert(status != NULL);
    assert(core != NULL);

    /* likely because we want fast branch prediction when is_head = true */
    if (likely(core->is_head))
    	goto process_head;

    while (!fp_ring_empty(core->q_urgent_in)) {
        uint64_t edge = (uint64_t)fp_ring_dequeue(core->q_urgent_in);

        if (unlikely(edge == URGENT_Q_HEAD_TOKEN)) {
        	/* got token! */
        	core->is_head = 1;
        	goto process_head;
        }

        process_one_new_request(EDGE_SRC(edge), EDGE_DST(edge), EDGE_BIN(edge),
        		core, status, current_bin);
    }

process_head:
    // Add new requests to the appropriate working bin
    while (!fp_ring_empty(status->q_head)) {
        uint64_t edge = (uint64_t)fp_ring_dequeue(status->q_head);
        uint16_t src = EDGE_SRC(edge);
    	uint16_t dst = EDGE_DST(edge);
        uint32_t index = get_status_index(src, dst);
  
        uint16_t bin_index = bin_index_from_timeslot(
        		status->last_alloc_tslot[index], status->current_timeslot);
        
        process_one_new_request(src, dst, bin_index, core, status, current_bin);
    }
}

// Determine admissible traffic for one timeslot from queue_in
// Puts unallocated traffic in queue_out
// Allocate BATCH_SIZE timeslots at once
void get_admissible_traffic(struct allocation_core *core,
								struct admissible_status *status)
{
    assert(status != NULL);

    // TODO: use multiple cores
    struct fp_ring *queue_in = core->q_bin_in;
    struct fp_ring *queue_out = core->q_bin_out;

    // Initialize this core for a new batch of processing
    init_allocation_core(core, status);

    // Process all bins from previous core, then process all bins from
    // residual backlog from traffic admitted in this batch
    struct bin *bin_in, *bin_out;
    uint16_t bin;

    bin_out = core->temporary_bins[0];
    assert(is_empty_bin(bin_out) && (bin_out->head == 0));

    for (bin = 0; bin < NUM_BINS + BATCH_SIZE - 1; bin++) {

//    	process_new_requests(status, core, bin);

    	if (likely(bin < NUM_BINS)) {
			bin_in = (struct bin *)fp_ring_dequeue(queue_in);
			try_allocation_bin(bin_in, core, bin_out, status);
    	} else {
    	    do
    	    	/* process requests */
    	    	process_new_requests(status, core, bin);
    	    	/* at least until the next core had finished allocating */
    	    while (!core->is_head);
    	    bin_in = core->new_request_bins[bin];
    	}

		try_allocation_bin(core->new_request_bins[bin], core, bin_out, status);

		if (likely(bin & ((~0UL << (BATCH_SHIFT+1)) | 1))) {
			fp_ring_enqueue(queue_out, bin_out);
			bin_out = bin_in;
	    	init_bin(bin_out);
		} else {
			/* we keep the same bin_out to fold 2-into-1. */
			core->temporary_bins[bin / 2] = bin_in;
		}
    }

    /* enqueue the last bin in batch as-is, next batch will take care of it */
    fp_ring_enqueue(queue_out, core->new_request_bins[NUM_BINS + BATCH_SIZE - 1]);

    for (bin = 0; bin < BATCH_SIZE; bin++) {
    	fp_ring_enqueue(core->admitted_out, core->admitted[bin]);
    }

    /* hand over token to next core */
    fp_ring_enqueue(core->q_urgent_out, (void*)URGENT_Q_HEAD_TOKEN);

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

/*
 * admissible_traffic_sjf.c
 *
 *  Created on: January 14, 2013
 *      Author: aousterh
 */

#include "admissible_traffic_sjf.h"

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

    // TODO: add port hash or something similar for finer granularity

    // Get full quantity from 16-bit LSB
    uint32_t index = get_status_index(src, dst);

	if (atomic32_add_return(&status->flows[index].backlog, backlog_increase) == backlog_increase)
		fp_ring_enqueue(status->q_head, MAKE_EDGE(0,src,dst));
}

/**
 * Try to allocate the given edge
 * Returns 0 if the flow has been completely allocated,
 *         1 if more backlog remains and should be handled by the caller
 */
static int try_allocation(uint16_t src, uint16_t dst, struct admission_core_state *core,
                          struct admissible_status *status)
{
    assert(core != NULL);
    assert(status != NULL);

    uint32_t index = get_status_index(src, dst);
    do {
        uint8_t batch_timeslot = get_first_timeslot(&core->batch_state, src, dst);

        if (batch_timeslot == NONE_AVAILABLE)
            /* caller should handle allocation of the remainder of this flow */
            return 1;

        // We can allocate this edge now
        set_timeslot_occupied(&core->batch_state, src, dst, batch_timeslot);

        insert_admitted_edge(core->admitted[batch_timeslot], src, dst);
        status->last_alloc_tslot[index] = status->current_timeslot + batch_timeslot;

    } while (atomic32_sub_return(&status->flows[index].backlog, 1) != 0);

    return 0;
}

/**
 * Try to allocate an entire bin. Flows that are not entirely allocated are written
 * out to the corresponding outgoing_bin for this core.
 */
static void try_allocation_bin(struct bin *in_bin, struct admission_core_state *core,
                               struct admissible_status *status)
{
    int rc;
    while (!is_empty_bin(in_bin)) {
        struct backlog_edge *edge = peek_head_bin(in_bin);
        assert(edge != NULL);
        uint16_t src = edge->src;
    	uint16_t dst = edge->dst;

        rc = try_allocation(src, dst, core, status);
        if (rc == 1) {
            // There is remaining backlog in this flow
            uint16_t bin_index = bin_index_from_src_dst(status, src, dst);
	    enqueue_bin(core->outgoing_bins[bin_index], src, dst);
        }

        dequeue_bin(in_bin);
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
            /* couldn't allocate all of it, pass on to next core */
            bin_index = bin_index_from_src_dst(status, src, dst);
	    fp_ring_enqueue(core->q_urgent_out,
			    MAKE_EDGE(bin_index, src, dst));
        }
    } else {
        // We have not yet processed the bin for this src/dst pair
	// Enqueue it to the working bins for later processing
        enqueue_bin(core->new_request_bins[bin_index], src, dst);
    }
}


// Assign new requests to the appropriate bin or processes immediately
// if the bin has passed
static void process_new_requests(struct admissible_status *status,
                                 struct admission_core_state *core,
                                 uint16_t current_bin)
{
    assert(status != NULL);
    assert(core != NULL);

    uint64_t edge;

    /* likely because we want fast branch prediction when is_head == true */
    if (likely(core->is_head))
    	goto process_head;

    while (fp_ring_dequeue(core->q_urgent_in, (void **)&edge) == 0) {
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
	while (fp_ring_dequeue(status->q_head, (void **)&edge) == 0) {
        uint16_t src = EDGE_SRC(edge);
    	uint16_t dst = EDGE_DST(edge);
  
        uint16_t bin_index = bin_index_from_src_dst(status, src, dst);
        
        process_one_new_request(src, dst, bin_index, core, status, current_bin);
    }
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

    struct bin *bin_in;
    uint16_t bin;

    uint16_t i;
    for (i = 0; i < NUM_BINS; i++) {
        struct bin *out_bin = core->outgoing_bins[i];
        assert(is_empty_bin(out_bin) && (out_bin->head == 0));
        struct bin *new_request_bin = core->new_request_bins[i];
        assert(is_empty_bin(new_request_bin) && (new_request_bin->head == 0));
    }

    process_new_requests(status, core, 0);

    for (bin = 0; bin < NUM_BINS; bin++) {
        /* process new requests until bin_in arrives */
	while (fp_ring_dequeue(queue_in, (void **)&bin_in) != 0)
            process_new_requests(status, core, bin);
	try_allocation_bin(bin_in, core, status);

        // process new requests of this size
        try_allocation_bin(core->new_request_bins[bin], core, status);

        // pass outgoing bin along to next core
        if (bin >= BATCH_SIZE) {
            fp_ring_enqueue(queue_out, core->outgoing_bins[bin - BATCH_SIZE]);
            core->outgoing_bins[bin - BATCH_SIZE] = NULL;
        }

        // store bin_in in temporary_bins
        core->temporary_bins[bin] = bin_in;
    }

    /* Output admitted traffic, but continue to process new requests until
       time to output */
    for (bin = 0; bin < BATCH_SIZE; bin++) {
        /* wait for start time */
        if (bin % 4 == 0) {
            uint64_t start_timeslot = first_timeslot + bin;
    	    uint64_t now_timeslot;
            
            do {
                /* process requests */
                process_new_requests(status, core, bin);
                /* at least until the next core finishes allocating */
                /* and we reach the start time */
                now_timeslot = (fp_get_time_ns() * tslot_mul) >> tslot_shift;
            } while (!core->is_head || (now_timeslot < start_timeslot));
        }

        /* enqueue the allocated traffic for this timeslot */
    	fp_ring_enqueue(status->q_admitted_out, core->admitted[bin]);
        /* disallow further allocations to that timeslot */
    	core->batch_state.allowed_mask <<= 1;
    }

    /* hand over token to next core. this should happen after enqueuing _all_
     * allocated_traffic structs, to prevent allocation re-ordering */
    fp_ring_enqueue(core->q_urgent_out, (void*)URGENT_Q_HEAD_TOKEN);

    /* move any requests from the last new_request_bin to the last outgoing bin */
    struct bin *last_new_bin = core->new_request_bins[NUM_BINS - 1];
    struct bin *last_out_bin = core->outgoing_bins[NUM_BINS - 1];
    while (!is_empty_bin(last_new_bin)) {
        struct backlog_edge *edge = peek_head_bin(last_new_bin);
        assert(edge != NULL);
        uint16_t src = edge->src;
        uint16_t dst = edge->dst;
        enqueue_bin(last_out_bin, src, dst);
        dequeue_bin(last_new_bin);
    }

    /* enqueue all of the remaining outgoing bins to the next core */
    for (bin = NUM_BINS - BATCH_SIZE; bin < NUM_BINS; bin++) {
        fp_ring_enqueue(queue_out, core->outgoing_bins[bin]);
        core->outgoing_bins[bin] = NULL;
    }

    /* re-arrange memory */
    for (bin = 0; bin < NUM_BINS; bin++) {
        core->outgoing_bins[bin] = core->temporary_bins[bin];
        core->temporary_bins[bin] = NULL;
    }

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

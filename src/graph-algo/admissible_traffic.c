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

// Methods to be replaced with inter-core communication
static inline
void enqueue_to_Q_head(struct admissible_status *status, uint16_t src,
                       uint16_t dst) {
    assert(status != NULL);

    enqueue_bin(status->q_head, src, dst);
}

static inline
struct backlog_edge *dequeue_from_Q_head(struct admissible_status *status) {
    assert(status != NULL);

    struct backlog_edge *current = peek_head_bin(status->q_head);
    dequeue_bin(status->q_head);

    return current;
}

static inline
void enqueue_to_Q_bins_out(struct bin *bin_out) {
    assert(bin_out != NULL);

    // do nothing for now
}

static inline
void enqueue_to_next_Q_urgent(uint16_t src, uint16_t dst) {
    // TODO
}

static inline
void enqueue_to_my_Q_urgent(uint16_t src, uint16_t dst) {
    // TODO
}

// Request num_slots additional timeslots from src to dst
void add_backlog(struct admissible_status *status, uint16_t src,
                       uint16_t dst, uint16_t backlog_increase) {
    assert(status != NULL);

    // Get full quantity from 16-bit LSB
    uint32_t index = get_status_index(src, dst);

	if (atomic32_add_return(&status->flows[index].backlog, backlog_increase) == backlog_increase)
		enqueue_to_Q_head(status, src, dst);
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

	insert_admitted_edge(&core->admitted[batch_timeslot], src, dst);
	uint32_t index = get_status_index(src, dst);
	status->timeslots[index] = status->current_timeslot + batch_timeslot;

	if (atomic32_sub_return(&status->flows[index].backlog, 1) != 0)
		enqueue_bin(core->batch_bins[batch_timeslot], src, dst);

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

// Sets the last send time for new requests based on the contents of status
// and sorts them
void process_new_requests(struct admissible_status *status,
                          struct allocation_core *core,
                          uint16_t current_bin) {
    assert(status != NULL);
    assert(core != NULL);

    // TODO: choose between q_head and q_urgent when many cores
    struct bin *new_requests = status->q_head;

    // Add new requests to the appropriate working bin
    while (!is_empty_bin(new_requests)) {
        struct backlog_edge *edge = dequeue_from_Q_head(status);
        uint16_t src = edge->src;
    	uint16_t dst = edge->dst;

        uint32_t index = get_status_index(src, dst);
        uint64_t last_send_time = status->timeslots[index];
  
        uint16_t bin_index = last_send_time - (status->current_timeslot - NUM_BINS);
        if (last_send_time < status->current_timeslot - NUM_BINS)
            bin_index = 0;
        
        if (bin_index < current_bin) {
            // We have already processed the bin for this src/dst pair
            // Try to allocate immediately
            if (try_allocation(src, dst, core, status) == 1)
            	enqueue_to_next_Q_urgent(src, dst);
        }
        else {
            // We have not yet processed the bin for this src/dst pair
            // Enqueue it to the working bins for later processing
            enqueue_bin(&core->new_request_bins[bin_index], src, dst);
        }
    }

    // Re-initialize to empty because wrap-around is not currently handled
    init_bin(new_requests);
}

// Determine admissible traffic for one timeslot from queue_in
// Puts unallocated traffic in queue_out
// Allocate BATCH_SIZE timeslots at once
void get_admissible_traffic(struct backlog_queue *queue_in,
                            struct backlog_queue *queue_out,
                            struct admissible_status *status) {
    assert(queue_in != NULL);
    assert(queue_out != NULL);
    assert(status != NULL);

    // TODO: use multiple cores
    uint8_t core_id = 0;  // for now, just one core
    struct allocation_core *core = &status->cores[core_id];

    // Initialize this core for a new batch of processing
    init_allocation_core(core, status);

    // Process all bins from previous core, then process all bins from
    // residual backlog from traffic admitted in this batch
    struct bin *current_bin, *bin_out, *new_bin;
    uint16_t bin;

    bin_out = core->temporary_bins[0];

    /* Fold the first 2*BATCH_SIZE bins into BATCH_SIZE bins */
    for (bin = 0; bin < BATCH_SIZE; bin++) {
    	init_bin(bin_out);

    	process_new_requests(status, core, 2 * bin);

    	current_bin = backlog_queue_dequeue(queue_in);
		try_allocation_bin(current_bin, core, bin_out, status);
		try_allocation_bin(&core->new_request_bins[2 * bin], core, bin_out, status);
		core->temporary_bins[bin] = current_bin;

    	current_bin = backlog_queue_dequeue(queue_in);
		try_allocation_bin(current_bin, core, bin_out, status);
		try_allocation_bin(&core->new_request_bins[2 * bin + 1], core, bin_out, status);

		backlog_queue_enqueue(queue_out, bin_out);
		bin_out = current_bin;
    }

    /* process the next bins one to one */
    for (bin = 2 * BATCH_SIZE; bin < NUM_BINS; bin++) {
    	init_bin(bin_out);

    	process_new_requests(status, core, bin);

    	current_bin = backlog_queue_dequeue(queue_in);
		try_allocation_bin(current_bin, core, bin_out, status);
		try_allocation_bin(&core->new_request_bins[bin], core, bin_out, status);

		backlog_queue_enqueue(queue_out, bin_out);
		bin_out = current_bin;
    }

    /* process the batch bins */
    for (bin = 0; bin < BATCH_SIZE - 1; bin++) {
    	init_bin(bin_out);

    	process_new_requests(status, core, bin);

    	current_bin = core->batch_bins[bin];
		try_allocation_bin(current_bin, core, bin_out, status);

		backlog_queue_enqueue(queue_out, bin_out);
		bin_out = core->temporary_bins[bin];
    }

    /* enqueue the last bin in batch as-is, next batch will take care of it */
    backlog_queue_enqueue(queue_out, core->batch_bins[BATCH_SIZE - 1]);
    core->batch_bins[BATCH_SIZE - 1] = core->temporary_bins[BATCH_SIZE - 1];

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

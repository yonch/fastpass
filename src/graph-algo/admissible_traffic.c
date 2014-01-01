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
struct bin *dequeue_from_Q_bins_in(struct backlog_queue *queue_in, uint16_t bin) {
    assert(queue_in != NULL);

    return &queue_in->bins[bin];
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

	if (atomic32_add_return(&status->flows[index].backlog, backlog_increase) == 0)
		enqueue_to_Q_head(status, src, dst);
}

// Try to allocate the given new edge
// TODO: try_allocation_new and try_allocation are very similar. Is there a clean
// way to combine them and eliminate the redundancy?
void try_allocation_new(struct backlog_edge *edge, 
                        struct allocation_core *core,
                        struct admissible_status *status) {
    assert(edge != NULL);
    assert(core != NULL);
    assert(status != NULL);

    uint16_t src = edge->src;
    uint16_t dst = edge->dst;

    uint8_t batch_timeslot = get_first_timeslot(&core->batch_state, src, dst);

    if (batch_timeslot == NONE_AVAILABLE) {
        // We cannot allocate this edge now - enqueue to next Q_urgent
        enqueue_to_next_Q_urgent(src, dst);
    }
    else {
        // We can allocate this edge now
        set_timeslot_occupied(&core->batch_state, src, dst, batch_timeslot);
       
        insert_admitted_edge(&core->admitted[batch_timeslot], src, dst);
        uint32_t index = get_status_index(src, dst);
        status->timeslots[index] = status->current_timeslot + batch_timeslot;

        // BEGIN ATOMIC
        if (atomic32_sub_return(&status->flows[index].backlog, 1) == 0)
            enqueue_to_my_Q_urgent(src, dst);
        // END ATOMIC
    }
}

/**
 * Try to allocate the given edge
 * Returns 0 if the flow is done, 1 if more backlog remains
 */
int try_allocation(struct backlog_edge *edge, struct allocation_core *core,
                    struct bin *bin_out, struct bin *last_bin_out,
                    struct admissible_status *status) {
    assert(edge != NULL);
    assert(core != NULL);
    assert(bin_out != NULL);
    assert(status != NULL);

    struct bin *admitted_backlog = &core->working_bins[NUM_BINS];

    uint16_t src = edge->src;
    uint16_t dst = edge->dst;

    uint8_t batch_timeslot = get_first_timeslot(&core->batch_state, src, dst);

    if (batch_timeslot == NONE_AVAILABLE) {
        // We cannot allocate this edge now - copy to queue_out
        enqueue_bin(bin_out, src, dst);
    }
    else {
        // We can allocate this edge now
        set_timeslot_occupied(&core->batch_state, src, dst, batch_timeslot);
       
        insert_admitted_edge(&core->admitted[batch_timeslot], src, dst);
        uint32_t index = get_status_index(src, dst);
        status->timeslots[index] = status->current_timeslot + batch_timeslot;

        // BEGIN ATOMIC
        if (atomic32_sub_return(&status->flows[index].backlog, 1) == 0) {
            if (batch_timeslot == BATCH_SIZE - 1)
                enqueue_bin(last_bin_out, src, dst);
            else
                enqueue_bin(&admitted_backlog[batch_timeslot], src, dst);
        }
        // END ATOMIC
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
        struct backlog_edge *current = dequeue_from_Q_head(status);

        uint32_t index = get_status_index(current->src, current->dst);
        uint64_t last_send_time = status->timeslots[index];
  
        uint16_t bin_index = last_send_time - (status->current_timeslot - NUM_BINS);
        if (last_send_time < status->current_timeslot - NUM_BINS)
            bin_index = 0;
        
        if (bin_index < current_bin) {
            // We have already processed the bin for this src/dst pair
            // Try to allocate immediately
            try_allocation_new(current, core, status);
        }
        else {
            // We have not yet processed the bin for this src/dst pair
            // Enqueue it to the working bins for later processing
            enqueue_bin(&core->working_bins[bin_index], current->src, current->dst);
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
    struct bin *last_bin = &queue_out->bins[NUM_BINS - 1];
    struct bin *current_bin, *bin_out, *new_bin;
    struct backlog_edge *edge;
    uint16_t bin;
    for (bin = 0; bin < NUM_BINS + BATCH_SIZE - 1; bin++) {
        process_new_requests(status, core, bin);

        bin_out = &queue_out->bins[MAX(0, bin - BATCH_SIZE)];

        // Process a bin from the previous core, if applicable
        if (bin < NUM_BINS) {
            current_bin = dequeue_from_Q_bins_in(queue_in, bin);

            // Process this bin
            while (!is_empty_bin(current_bin)) {
                edge = peek_head_bin(current_bin);
                try_allocation(edge, core, bin_out, last_bin, status);
                dequeue_bin(current_bin);
            }
        }
     
        // Process the corresponding new bin
        // The new bin has flows from Q_urgent and residual backlog
        // from traffic admitted in this batch
        new_bin = &core->working_bins[bin];
        while (!is_empty_bin(new_bin)) {
            edge = peek_head_bin(new_bin);
            try_allocation(edge, core, bin_out, last_bin, status);
            dequeue_bin(new_bin);
        }

        // If we finished an output bin, enqueue it to the next core
        if (bin >= BATCH_SIZE)
            enqueue_to_Q_bins_out(bin_out);
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

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
void enqueue_to_Q_head(struct bin *new_requests, uint16_t src, uint16_t dst) {
    assert(new_requests != NULL);

    enqueue_bin(new_requests, src, dst);
}

static inline
struct backlog_edge *dequeue_from_Q_head(struct bin *new_requests) {
    assert(new_requests != NULL);

    struct backlog_edge *current = peek_head_bin(new_requests);
    dequeue_bin(new_requests);

    return current;
}


// Request num_slots additional timeslots from src to dst
void request_timeslots(struct bin *new_requests, struct admissible_status *status,
                       uint16_t src, uint16_t dst, uint16_t demand_tslots) {
    assert(new_requests != NULL);
    assert(status != NULL);

    // Get full quantity from 16-bit LSB
    uint32_t index = get_status_index(src, dst);
    uint16_t prev = status->flows[index].demand;
    int16_t prev_wnd = prev - (1 << 15);
    int64_t new_demand = prev_wnd + ((demand_tslots - prev_wnd) & 0xFFFF);

    if (new_demand > prev) {
        // Just add this request at the end of the bin

        // Calculate backlog increase and update demand
        uint16_t backlog_increase = (uint16_t) new_demand -
            status->flows[index].demand;
        status->flows[index].demand = (uint16_t) new_demand;

        // BEGIN ATOMIC
        if (status->flows[index].backlog == 0)
            enqueue_to_Q_head(new_requests, src, dst);

        status->flows[index].backlog += backlog_increase;
        // END ATOMIC
    }
}

// Sets the last send time for new requests based on the contents of status
// and sorts them
void prepare_new_requests(struct bin *new_requests,
                          struct admissible_status *status,
                          struct backlog_queue *queue_in) {
    assert(new_requests != NULL);
    assert(status != NULL);
    assert(queue_in != NULL);

    // Add new requests to the appropriate bins in queue_in
    while (!is_empty_bin(new_requests)) {
        struct backlog_edge *current = dequeue_from_Q_head(new_requests);

        uint32_t index = get_status_index(current->src, current->dst);
        uint64_t last_send_time = status->timeslots[index];
  
        uint16_t bin_index = last_send_time - (status->current_timeslot - NUM_BINS);
        if (last_send_time < status->current_timeslot - NUM_BINS)
            bin_index = 0;
        
        enqueue_bin(&queue_in->bins[bin_index], current->src, current->dst);
    }
}

// Try to allocate
void try_allocation(struct bin *current_bin, struct batch_state *batch_state,
                    struct bin *bin_out, struct bin *last_bin_out,
                    struct admitted_traffic *traffic_out, struct bin *admitted_backlog,
                    struct admissible_status *status) {
    assert(current_bin != NULL);
    assert(batch_state != NULL);
    assert(bin_out != NULL);
    assert(traffic_out != NULL);
    assert(admitted_backlog != NULL);
    assert(status != NULL);
    
    struct backlog_edge *chosen_edge = peek_head_bin(current_bin);

    uint16_t src = chosen_edge->src;
    uint16_t dst = chosen_edge->dst;

    uint8_t batch_timeslot = get_first_timeslot(batch_state, src, dst);

    if (batch_timeslot == NONE_AVAILABLE) {
        // We cannot allocate this edge now - copy to queue_out
        enqueue_bin(bin_out, src, dst);
    }
    else {
        // We can allocate this edge now
        set_timeslot_occupied(batch_state, src, dst, batch_timeslot);
       
        insert_admitted_edge(&traffic_out[batch_timeslot], src, dst);
        uint32_t index = get_status_index(src, dst);
        status->timeslots[index] = status->current_timeslot + batch_timeslot;

        // BEGIN ATOMIC
        status->flows[index].backlog -= 1;
        if (status->flows[index].backlog > 0) {
            if (batch_timeslot == BATCH_SIZE - 1)
                enqueue_bin(last_bin_out, src, dst);
            else
                enqueue_bin(&admitted_backlog[batch_timeslot], src, dst);
        }
        // END ATOMIC
    }

    dequeue_bin(current_bin);
}

// Populate traffic_out with the admissible traffic for one timeslot from queue_in
// Puts unallocated traffic in queue_out
// Allocate BATCH_SIZE timeslots at once
// traffic_out must be an array of BATCH_SIZE struct admitted_traffics
void get_admissible_traffic(struct backlog_queue *queue_in,
                            struct backlog_queue *queue_out,
                            struct bin *new_requests,
                            struct admitted_traffic *traffic_out,
                            struct admissible_status *status) {
    assert(queue_in != NULL);
    assert(queue_out != NULL);
    assert(new_requests != NULL);
    assert(traffic_out != NULL);
    assert(status != NULL);

    // Fetch last_send_time, sort, etc.
    prepare_new_requests(new_requests, status, queue_in);
    assert(!has_duplicates(queue_in));

    struct batch_state batch_state;
    init_batch_state(&batch_state, status->oversubscribed,
                     status->inter_rack_capacity, status->num_nodes);

    // TODO: could use smaller bins here
    struct bin *admitted_backlog = status->admitted_bins;
    uint8_t i;
    for (i = 0; i < BATCH_SIZE - 1; i++)
        init_bin(&admitted_backlog[i]);

    // Process all bins in queue_in
    uint16_t bin;
    for (bin = 0; bin < NUM_BINS; bin++) {
        struct bin *current_bin = &queue_in->bins[bin];
        struct bin *bin_out = &queue_out->bins[MAX(0, bin - BATCH_SIZE)];
        struct bin *last_bin = &queue_out->bins[NUM_BINS - 1];

        while (!is_empty_bin(current_bin))
            try_allocation(current_bin, &batch_state, bin_out, last_bin,
                           traffic_out, admitted_backlog, status);
    }

    // TODO: combine these two loops?
    // Process the rest of this batch
    // (src/dst pairs that were already allocated at least once in this batch)
    uint8_t batch;
    for (batch = 1; batch < BATCH_SIZE; batch++) {
        struct bin *current_bin = &admitted_backlog[batch - 1];
        struct bin *bin_out = &queue_out->bins[NUM_BINS - BATCH_SIZE + batch - 1];
        struct bin *last_bin = &queue_out->bins[NUM_BINS - 1];

        while (!is_empty_bin(current_bin))
            try_allocation(current_bin, &batch_state, bin_out, last_bin,
                           traffic_out, admitted_backlog, status);
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

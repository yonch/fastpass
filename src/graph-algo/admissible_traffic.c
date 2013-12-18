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

// Request num_slots additional timeslots from src to dst
void request_timeslots(struct bin *new_requests, struct admissible_status *status,
                       uint16_t src, uint16_t dst, uint16_t demand_tslots) {
    assert(new_requests != NULL);
    assert(status != NULL);

    // Get full quantity from 16-bit LSB
    uint16_t prev = get_last_demand(status, src, dst);
    int16_t prev_wnd = prev - (1 << 15);
    int64_t new_demand = prev_wnd + ((demand_tslots - prev_wnd) & 0xFFFF);

    if (new_demand > prev) {
        // Just add this request at the end of the backlog queue with an invalid time
        // Obtain the last_sent_timeslot and sort later
        if (get_backlog(status, src, dst) == 0)
            enqueue_bin(new_requests, src, dst);

        set_last_demand(status, src, dst, (uint16_t) new_demand);
        // Note: race condition involving the 3 preceding lines of code
        // Need to check backlog and set demand atomically, or else do this
        // check after we've finished the previous batch allocation
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
        struct backlog_edge *current = peek_head_bin(new_requests);

        uint64_t last_send_time = get_last_timeslot(status, current->src,
                                                    current->dst);
  
        uint16_t bin_index = last_send_time - (status->current_timeslot - NUM_BINS);
        if (last_send_time < status->current_timeslot - NUM_BINS)
            bin_index = 0;
        
        enqueue_bin(&queue_in->bins[bin_index], current->src, current->dst);
        dequeue_bin(new_requests);
        // Note: this does not preserve fair ordering between flows that arrive in
        // the same timeslot and are older than all currently backlogged flows
    }
}

// Try to allocate
void try_allocation(struct bin *current_bin, struct batch_state *batch_state,
                    struct bin *bin_out, struct admitted_traffic *traffic_out,
                    struct bin *admitted_backlog, struct admissible_status *status) {
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
        set_last_timeslot(status, src, dst, status->current_timeslot + batch_timeslot);
        increment_allocation(status, src, dst);
        if (get_backlog(status, src, dst) > 0)
            enqueue_bin(&admitted_backlog[batch_timeslot], src, dst);
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
                     status->inter_rack_capacity);

    // TODO: could use smaller bins here
    struct bin *admitted_backlog = status->admitted_bins;
    uint8_t i;
    for (i = 0; i < BATCH_SIZE; i++)
        init_bin(&admitted_backlog[i]);

    // Process all bins in queue_in
    uint16_t bin;
    for (bin = 0; bin < NUM_BINS; bin++) {
        struct bin *current_bin = &queue_in->bins[bin];

        while (!is_empty_bin(current_bin)) {
            struct bin *bin_out = &queue_out->bins[NUM_BINS - BATCH_SIZE];
            
            try_allocation(current_bin, &batch_state, bin_out, traffic_out,
                           admitted_backlog, status);
        }
    }

    // TODO: combine these two loops?
    // Process the rest of this batch
    // (src/dst pairs that were already allocated at least once in this batch)
    uint8_t batch;
    for (batch = 1; batch < BATCH_SIZE; batch++) {
        struct bin *current_bin = &admitted_backlog[batch - 1];
   
        while (!is_empty_bin(current_bin)) {
            struct bin *bin_out = &queue_out->bins[NUM_BINS - BATCH_SIZE + batch];

            try_allocation(current_bin, &batch_state, bin_out, traffic_out,
                           admitted_backlog, status);
        }
    }

    // Add backlogs for last admitted traffic to end of queue
    // TODO: can you accomplish this by modifying try_allocation above?
    struct bin *last_admitted = &admitted_backlog[BATCH_SIZE - 1];
    while (!is_empty_bin(last_admitted)) {
        struct backlog_edge *edge = peek_head_bin(last_admitted);
        enqueue_bin(&queue_out->bins[NUM_BINS - 1], edge->src, edge->dst);
        dequeue_bin(last_admitted);
    }
    
    // Update current timeslot
    status->current_timeslot += BATCH_SIZE;
}

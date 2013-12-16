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
void request_timeslots(struct backlog_queue *new_requests, struct admissible_status *status,
                       uint16_t src, uint16_t dst, uint16_t demand_tslots) {
    assert(new_requests != NULL);
    assert(status != NULL);

    // Get full quantity from 16-bit LSB
    uint16_t prev = get_last_demand(status, src, dst);
    int16_t prev_wnd = prev - (1 << 15);
    int64_t new_demand = prev_wnd + ((demand_tslots - prev_wnd) & 0xFFFF);

    if (new_demand > prev) {
        set_last_demand(status, src, dst, (uint16_t) new_demand);

        // Just add this request at the end of the backlog queue with an invalid time
        // Obtain the last_sent_timeslot and sort later
        enqueue_backlog(new_requests, src, dst, new_demand - prev, 0);
    }
}

// Sets the last send time for new requests based on the contents of status
// and sorts them
void prepare_new_requests(struct backlog_queue *new_requests,
                          struct admissible_status *status) {
    assert(new_requests != NULL);
    assert(status != NULL);

    uint16_t min_time = (uint16_t) status->oldest_timeslot - 1;

    // Obtain the correct last_send_time for all requests
    struct backlog_edge *current = &new_requests->edges[new_requests->head];
    struct backlog_edge *last = &new_requests->edges[new_requests->tail];
    while (current < last) {
        uint64_t last_send_time = get_last_timeslot(status, current->src,
                                                    current->dst);
        uint16_t last_send_time_abbrv = (uint16_t) last_send_time;
        if (last_send_time < status->oldest_timeslot)
            last_send_time_abbrv = (uint16_t) status->oldest_timeslot - 1;
        current->timeslot = last_send_time_abbrv;
        current++;

        // Note: this does not preserve fair ordering between flows that arrive in
        // the same timeslot and are older than all currently backlogged flows
    }

    // Sort new requests
    sort_backlog(new_requests, min_time);
}

// Populate traffic_out with the admissible traffic for one timeslot from queue_in
// Puts unallocated traffic in queue_out
// Allocate BATCH_SIZE timeslots at once
// traffic_out must be an array of BATCH_SIZE struct admitted_traffics
void get_admissible_traffic(struct backlog_queue *queue_in,
                            struct backlog_queue *queue_out,
                            struct backlog_queue *new_requests,
                            struct admitted_traffic *traffic_out,
                            struct admissible_status *status) {
    assert(queue_in != NULL);
    assert(queue_out != NULL);
    assert(new_requests != NULL);
    assert(traffic_out != NULL);
    assert(status != NULL);

    // Fetch last_send_time, sort, etc.
    prepare_new_requests(new_requests, status);

    uint16_t min_time = (uint16_t) status->oldest_timeslot - 1;

    struct batch_state batch_state;
    init_batch_state(&batch_state, status->oversubscribed,
                     status->inter_rack_capacity);

    struct backlog_queue *admitted_backlog = status->admitted_queues;
    uint8_t i;
    for (i = 0; i < BATCH_SIZE; i++)
        init_backlog_queue(&admitted_backlog[i]);

    while (!is_empty_backlog(queue_in) || !is_empty_backlog(new_requests)) {
        struct backlog_edge *edge = peek_head_backlog(queue_in);
        struct backlog_edge *new_edge = peek_head_backlog(new_requests);

        // Check both backlogs and dequeue from the older one, or both
        // if they refer to the same src/dst pair
        struct backlog_edge *chosen_edge;
        if (is_empty_backlog(queue_in) ||
            (!is_empty_backlog(new_requests) &&
             compare_backlog_edges(new_edge, edge, min_time) < 0)) {
            chosen_edge = new_edge;
            dequeue_backlog(new_requests);
        }
        else if (is_empty_backlog(new_requests) ||
                 compare_backlog_edges(new_edge, edge, min_time) > 0) {
            chosen_edge = edge;
            dequeue_backlog(queue_in);
        }
        else {
            chosen_edge = edge;
            edge->backlog += new_edge->backlog;
            dequeue_backlog(new_requests);
            dequeue_backlog(queue_in);
        }

        // Combine any other new requests for the same src/dst pair
        while (!is_empty_backlog(new_requests) &&
               compare_backlog_edges(chosen_edge, peek_head_backlog(new_requests), min_time) == 0) {
            chosen_edge->backlog += peek_head_backlog(new_requests)->backlog;
            dequeue_backlog(new_requests);
        }

        uint16_t src = chosen_edge->src;
        uint16_t dst = chosen_edge->dst;

        uint8_t batch_timeslot = get_first_timeslot(&batch_state, src, dst);

        if (batch_timeslot == NONE_AVAILABLE) {
            // We cannot allocate this edge now - copy to queue_out
            enqueue_backlog(queue_out, src, dst, chosen_edge->backlog, chosen_edge->timeslot);
        }
        else {
            // We can allocate this edge now
            set_timeslot_occupied(&batch_state, src, dst, batch_timeslot);
       
            insert_admitted_edge(&traffic_out[batch_timeslot], src, dst);
            if (chosen_edge->backlog > 1)
                enqueue_backlog(&admitted_backlog[batch_timeslot], src, dst,
                                chosen_edge->backlog - 1,
                                (uint16_t) status->current_timeslot + batch_timeslot);
            set_last_timeslot(status, src, dst, status->current_timeslot + batch_timeslot);
        }
    }

    // Process the rest of this batch
    uint8_t batch;
    for (batch = 1; batch < BATCH_SIZE; batch++) {
        struct backlog_queue *current_queue_in = &admitted_backlog[batch - 1];
        uint32_t start_index = queue_out->tail;
        struct backlog_edge *queue_out_start = &queue_out->edges[start_index];

        while (!is_empty_backlog(current_queue_in)) {
            struct backlog_edge *edge = peek_head_backlog(current_queue_in);

            uint16_t src = edge->src;
            uint16_t dst = edge->dst;

            uint8_t batch_timeslot = get_first_timeslot(&batch_state, src, dst);

            if (batch_timeslot == NONE_AVAILABLE) {
                // We cannot allocate this edge now - copy to queue_out
                enqueue_backlog(queue_out, src, dst, edge->backlog, edge->timeslot);
            }
            else {
                // We can allocate this edge now
                assert(batch_timeslot >= batch);
                set_timeslot_occupied(&batch_state, src, dst, batch_timeslot);
       
                insert_admitted_edge(&traffic_out[batch_timeslot], src, dst);
                if (edge->backlog > 1)
                    enqueue_backlog(&admitted_backlog[batch_timeslot], src, dst,
                                    edge->backlog - 1,
                                    (uint16_t) status->current_timeslot + batch_timeslot);
                set_last_timeslot(status, src, dst, status->current_timeslot + batch_timeslot);
            }
            dequeue_backlog(current_queue_in);
        }

        // Sort the edges newly added to queue_out
        uint32_t size = queue_out->tail - start_index;
        if (size >= 2)
            quicksort_backlog(queue_out_start, size, 0);
    }

    // Add backlogs for admitted traffic to end of queue
    // Sort first to preserve ordering of queue
    struct backlog_queue *last_admitted = &admitted_backlog[BATCH_SIZE - 1];
    sort_backlog(last_admitted, 0);
    struct backlog_edge *edge = &last_admitted->edges[last_admitted->head];
    struct backlog_edge *end = &last_admitted->edges[last_admitted->tail];
    while (edge < end) {
        enqueue_backlog(queue_out, edge->src, edge->dst,
                        edge->backlog, edge->timeslot);
        edge++;
    }
    
    // Update current timeslot and oldest timeslot
    status->current_timeslot += BATCH_SIZE;
    if (is_empty_backlog(queue_out))
        status->oldest_timeslot = status->current_timeslot;
    else {
        uint16_t oldest_queued_time = peek_head_backlog(queue_out)->timeslot;

        status->oldest_timeslot = oldest_queued_time + (status->current_timeslot & ~(0xFFFFULL));
        if (status->oldest_timeslot > status->current_timeslot)
            status->oldest_timeslot -= 0x10000ULL;
    }

    assert(!out_of_order(queue_out, false));
}

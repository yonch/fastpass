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
void request_timeslots(struct backlog_queue *new_requests, struct flow_status *status,
                       uint16_t src, uint16_t dst, uint16_t num_slots) {
    assert(new_requests != NULL);
    assert(status != NULL);

    // Just add this request at the end of the backlog queue with an invalid time
    // Obtain the last_sent_timeslot and sort later
    enqueue_backlog(new_requests, src, dst, num_slots, 0);
}

// Sets the last send time for new requests based on the contents of status
// and sorts them
void prepare_new_requests(struct backlog_queue *new_requests,
                          struct flow_status *status) {
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
// For now, allocates one timeslot at a time, doesn't parallelize
void get_admissible_traffic(struct backlog_queue *queue_in,
                            struct backlog_queue *queue_out,
                            struct backlog_queue *new_requests,
                            struct admitted_traffic *traffic_out,
                            struct flow_status *status) {
    assert(queue_in != NULL);
    assert(queue_out != NULL);
    assert(new_requests != NULL);
    assert(traffic_out != NULL);
    assert(status != NULL);

    // Fetch last_send_time, sort, etc.
    prepare_new_requests(new_requests, status);

    uint16_t min_time = (uint16_t) status->oldest_timeslot - 1;

    struct admitted_bitmap admitted;
    init_admitted_bitmap(&admitted);

    struct backlog_queue admitted_backlog;
    init_backlog_queue(&admitted_backlog);

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
 
        if (src_is_admitted(&admitted, chosen_edge->src) ||
            dst_is_admitted(&admitted, chosen_edge->dst)) {
            // We cannot allocate this edge now - copy to queue_out
            enqueue_backlog(queue_out, chosen_edge->src, chosen_edge->dst,
                            chosen_edge->backlog, chosen_edge->timeslot);
        }
        else {
            // We can allocate this edge now
            insert_admitted_edge(traffic_out, chosen_edge->src, chosen_edge->dst);
            if (chosen_edge->backlog > 1)
                enqueue_backlog(&admitted_backlog, chosen_edge->src, chosen_edge->dst,
                                chosen_edge->backlog - 1, (uint16_t) status->current_timeslot);
            set_last_timeslot(status, chosen_edge->src, chosen_edge->dst,
                              status->current_timeslot);
            set_src_admitted(&admitted, chosen_edge->src);
            set_dst_admitted(&admitted, chosen_edge->dst);
        }
    }

    // Add backlogs for admitted traffic to end of queue
    // Sort first to preserve ordering of queue
    sort_backlog(&admitted_backlog, (uint16_t) status->current_timeslot);
    struct backlog_edge *edge = &admitted_backlog.edges[admitted_backlog.head];
    struct backlog_edge *end = &admitted_backlog.edges[admitted_backlog.tail];
    while (edge < end) {
        enqueue_backlog(queue_out, edge->src, edge->dst,
                        edge->backlog, edge->timeslot);
        edge++;
    }
    
    // Update current timeslot and oldest timeslot
    status->current_timeslot++;
    if (is_empty_backlog(queue_out))
        status->oldest_timeslot = status->current_timeslot;
    else {
        uint16_t oldest_queued_time = peek_head_backlog(queue_out)->timeslot;
        uint64_t mask = ~(0xffffULL);
        if (oldest_queued_time <= (uint16_t) status->current_timeslot)
            status->oldest_timeslot = status->current_timeslot & mask
                + oldest_queued_time;
        else {
            // current timeslot has wrapped around relative to the oldest queued time
            status->oldest_timeslot = ((status->current_timeslot - 0x10000ULL) & mask)
                + oldest_queued_time;
        }
    }
}


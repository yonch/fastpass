/*
 * admissible_traffic.c
 *
 *  Created on: November 19, 2013
 *      Author: aousterh
 */

#include "admissible_traffic.h"

#include <assert.h>
#include <inttypes.h>
#include <stdlib.h>

static uint16_t current_timeslot = 1;

// Request num_slots additional timeslots from src to dst
void request_timeslots(struct backlog_queue *new_requests, struct flow_status *status,
                       uint16_t src, uint16_t dst, uint16_t num_slots) {
    assert(new_requests != NULL);
    assert(status != NULL);

    uint16_t last_send_time = get_last_timeslot(status, src, dst);
    enqueue_backlog(new_requests, src, dst, num_slots, last_send_time);

    // TODO: requests may be made in any arbitrary order, so we should either insert them
    // into a heap, or sort this list
}

// Populate traffic_out with the admissible traffic for one timeslot from queue_in
// Puts unallocated traffic in queue_out
// For now, allocates one timeslot at a time, doesn't parallelize
void get_admissible_traffic(struct backlog_queue *queue_in,
                            struct backlog_queue *queue_out,
                            struct backlog_queue *new_requests,
                            struct admitted_traffic *traffic_out,
                            struct flow_status *status) {
    assert(traffic_out != NULL);

    struct admitted_bitmap admitted;
    init_admitted_bitmap(&admitted);

    while (!is_empty_backlog(queue_in) || !is_empty_backlog(new_requests)) {
        struct backlog_edge *edge = peek_head_backlog(queue_in);
        struct backlog_edge *new_edge = peek_head_backlog(new_requests);

        // Check both backlogs and dequeue from the older one, or both
        // if they refer to the same src/dst pair
        struct backlog_edge *chosen_edge;
        if (is_empty_backlog(queue_in) ||
            (!is_empty_backlog(new_requests) && compare_backlog_edges(new_edge, edge) < 0)) {
            chosen_edge = new_edge;
            dequeue_backlog(new_requests);
        }
        else if (is_empty_backlog(new_requests) ||
                 compare_backlog_edges(new_edge, edge) > 0) {
            chosen_edge = edge;
            dequeue_backlog(queue_in);
        }
        else {
            chosen_edge = edge;
            edge->backlog += new_edge->backlog;
            dequeue_backlog(new_requests);
            dequeue_backlog(queue_in);
        }
       
        if (src_is_admitted(&admitted, chosen_edge->src) ||
            dst_is_admitted(&admitted, chosen_edge->dst)) {
            // We cannot allocate this edge now - copy to queue_out
            enqueue_backlog(queue_out, chosen_edge->src, chosen_edge->dst,
                            chosen_edge->backlog, chosen_edge->timeslot);
        }
        else {
            // We can allocate this edge now
            insert_admitted_edge(traffic_out, chosen_edge->src, chosen_edge->dst, chosen_edge->backlog - 1);
            set_last_timeslot(status, chosen_edge->src, chosen_edge->dst, current_timeslot);
        }
    }

    // Add backlogs for admitted traffic to end of queue
    struct admitted_edge *edge = &traffic_out->edges[0];
    struct admitted_edge *end = &traffic_out->edges[traffic_out->size];
    while (edge < end) {
        if (edge->remaining_backlog > 0) {
            enqueue_backlog(queue_out, edge->src, edge->dst,
                            edge->remaining_backlog, current_timeslot);
            edge++;
        }
    }
    
    current_timeslot++;
}


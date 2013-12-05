/*
 * test_admissible.c
 *
 *  Created on: December 4, 2013
 *      Author: aousterh
 */

#include <inttypes.h>
#include <stdio.h>

#include "admissible_traffic.h"
#include "admissible_structures.h"

// For now, a simple experiment in which we randomly issue 1 new request per timeslot
int main(void) {
    uint16_t experiments = 10;
    uint32_t duration = 100000;
    uint32_t num_nodes = 256;
    double fraction = 0.95;

    // Data structures
    struct backlog_queue new_requests;
    struct flow_status status;
    struct backlog_queue queue_0;
    struct backlog_queue queue_1;
    struct admitted_traffic admitted;

    init_flow_status(&status);
    init_backlog_queue(&queue_0);
    init_backlog_queue(&queue_1);

    uint16_t i;
    for (i = 0; i < experiments; i++) {
        uint32_t t;
        uint32_t num_admitted = 0;
        for (t = 0; t < duration; t++) {
            // Issue new requests
            init_backlog_queue(&new_requests);
            uint16_t src = rand() / ((double) RAND_MAX) * num_nodes;
            uint16_t dst = rand() / ((double) RAND_MAX) * (num_nodes - 1);
            if (dst >= src)
                dst++;
            request_timeslots(&new_requests, &status, src, dst, fraction * num_nodes);
 
            // Get admissible traffic
            struct backlog_queue *queue_in = &queue_0;
            struct backlog_queue *queue_out = &queue_1;
            if (t % 2 == 1) {
                queue_in = &queue_1;
                queue_out = &queue_0;
            }
            init_admitted_traffic(&admitted);
            init_backlog_queue(queue_out);
            get_admissible_traffic(queue_in, queue_out, &new_requests,
                                   &admitted, &status);
            num_admitted += admitted.size;
            
            assert(!out_of_order(queue_out, false));
        }
        printf("network utilization: %f\n", ((double) num_admitted) / (duration * num_nodes));
    }
}

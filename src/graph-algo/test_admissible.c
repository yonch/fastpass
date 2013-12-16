/*
 * test_admissible.c
 *
 *  Created on: December 4, 2013
 *      Author: aousterh
 */

#include <inttypes.h>
#include <math.h>
#include <stdio.h>

#include "admissible_traffic.h"
#include "admissible_structures.h"
#include "../linux-test/common.h"  // For timing

// Based on a method suggested by wikipedia
// http://en.wikipedia.org/wiki/Exponential_distribution
double generate_exponential_variate(uint32_t mean_t_btwn_requests)
{
  assert(mean_t_btwn_requests > 0);

  double u = rand() / ((double) RAND_MAX);
  return -log(u) * mean_t_btwn_requests;
}
// Generate a sequence of requests with Poisson arrival times, puts them in edges
// Returns the number of requests generated
uint32_t generate_requests_poisson(struct backlog_edge *edges, uint32_t size,
                                   uint32_t num_nodes, uint32_t duration,
                                   double fraction, uint32_t mean)
{
    assert(edges != NULL);

    // Generate a sequence of requests with Poisson arrival times per sender
    // and receivers chosen uniformly at random

    struct backlog_edge *current_edge = edges;
    uint16_t src;
    uint32_t num_generated = 0;
    for (src = 0; src < num_nodes; src++) {
        uint16_t *cumulative_demands = calloc(num_nodes, sizeof(uint16_t));
        double current_time = generate_exponential_variate(mean);
        while (current_time < duration) {
            uint32_t dst = rand() / ((double) RAND_MAX) * (num_nodes - 1);
            if (dst >= src)
                dst++;  // Don't send to self
            current_edge->src = src;
            current_edge->dst = dst;
            cumulative_demands[dst] += (uint16_t) mean * fraction;
            current_edge->backlog = cumulative_demands[dst];
            current_edge->timeslot = (uint16_t) current_time;
            num_generated++;
            current_edge++;
            current_time += generate_exponential_variate(mean);
        }
        free(cumulative_demands);
    }

    assert(num_generated <= size);

    // Sort by timeslot!
    quicksort_backlog(edges, num_generated, 0);

    return num_generated;
}


// Generate a uniformly random sequence of requests, puts them in edges
// Returns the number of requests generated
// TODO: update this to use new API for admissible_traffic (cumulative demands)
uint32_t generate_requests_uniformly(struct backlog_edge *edges, uint32_t size,
                                     uint32_t num_nodes, uint32_t duration,
                                     double fraction)
{
    assert(edges != NULL);

    // Use simple method to randomly generate one new request per timeslot

    uint32_t t;
    struct backlog_edge *current_edge = edges;
    uint32_t num_generated = 0;
    for (t = 0; t < duration; t++) {
        uint16_t src = rand() / ((double) RAND_MAX) * num_nodes;
        uint16_t dst = rand() / ((double) RAND_MAX) * (num_nodes - 1);
        if (dst >= src)
            dst++;  // Don't send to self
        current_edge->src = src;
        current_edge->dst = dst;
        current_edge->backlog = (uint16_t) (num_nodes * fraction);
        current_edge->timeslot = t;

        num_generated++;
        current_edge++;
    }

    assert(num_generated <= size);

    return num_generated;
}

// Runs one experiment. Returns the number of packets admitted.
uint32_t run_experiment(struct backlog_edge *requests, uint32_t duration, uint32_t num_requests,
                        struct backlog_queue *new_requests, struct admissible_status *status,
                        struct backlog_queue *queue_0, struct backlog_queue *queue_1,
                        struct admitted_traffic *admitted) {
    assert(requests != NULL);

    uint32_t b;
    uint32_t num_admitted = 0;
    struct backlog_edge *current_request = requests;
    for (b = 0; b < (duration >> BATCH_SHIFT); b++) {
        // Issue all new requests for this batch
        init_backlog_queue(new_requests);
        while ((current_request->timeslot >> BATCH_SHIFT) == (b % (65536 >> BATCH_SHIFT)) &&
               current_request < requests + num_requests) {
            request_timeslots(new_requests, status, current_request->src,
                              current_request->dst, current_request->backlog);
            current_request++;
        }
 
        // Get admissible traffic
        struct backlog_queue *queue_in = queue_0;
        struct backlog_queue *queue_out = queue_1;
        if (b % 2 == 1) {
            queue_in = queue_1;
            queue_out = queue_0;
        }
        uint8_t i;
        for (i = 0; i < BATCH_SIZE; i++)
            init_admitted_traffic(&admitted[i]);
        init_backlog_queue(queue_out);
        get_admissible_traffic(queue_in, queue_out, new_requests,
                               admitted, status);
        for (i = 0; i < BATCH_SIZE; i++)
            num_admitted += admitted[i].size;
            
        assert(!out_of_order(queue_out, false));
    }
    return num_admitted;
}

// For now, a simple experiment in which we randomly issue 1 new request per timeslot
int main(void) {
    uint16_t experiments = 10;
    // keep duration less than 65536 or else Poisson wont work correctly due to sorting
    uint32_t duration = 60000;
    uint32_t num_nodes = 256;
    double fraction = 0.95;
    uint32_t mean = 10;

    // Data structures
    struct backlog_queue *new_requests = create_backlog_queue();
    struct admissible_status *status = create_admissible_status(false, 0);
    struct backlog_queue *queue_0 = create_backlog_queue();
    struct backlog_queue *queue_1 = create_backlog_queue();
    struct admitted_traffic *admitted = create_admitted_traffic();

    printf("running with %d nodes, requesting %f percent of capacity\n", num_nodes, fraction);

    uint16_t i;
    for (i = 0; i < experiments; i++) {
        // Initialize data structures
        init_admissible_status(status, false, 0);
        init_backlog_queue(queue_0);
        init_backlog_queue(queue_1);

        // Allocate enough space for new requests
        // (this is sufficient for <= 1 request per node per timeslot)
        uint32_t max_requests = duration * num_nodes;
        struct backlog_edge *requests = malloc(max_requests * sizeof(struct backlog_edge));

        // Generate new requests
        uint32_t num_requests = generate_requests_poisson(requests, max_requests, num_nodes,
                                                          duration, fraction, mean);

        // Start timining
        uint64_t start_time = current_time();

        // Run the experiment
        uint32_t num_admitted = run_experiment(requests, duration, num_requests, new_requests,
                                               status, queue_0, queue_1, admitted);
        
        uint64_t end_time = current_time();
        double time_per_experiment = (end_time - start_time) / (2.8 * 1000 * duration);

        // Print stats - percent of requested traffic admitted, percent of network
        // capacity utilized, computation time per admitted timeslot (in microseconds)
        printf("percent of network capacity utilized: %f, ",
               ((double) num_admitted) / (duration * num_nodes));
        printf("avg time (microseconds): %f\n", time_per_experiment);
    }

    free(queue_0);
    free(queue_1);
    free(new_requests);
    free(status->admitted_queues);
    free(status);
    free(admitted);
}

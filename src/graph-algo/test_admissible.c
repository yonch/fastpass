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

// Info about incoming requests
struct request_info {
    uint16_t src;
    uint16_t dst;
    uint16_t backlog;
    uint16_t timeslot;
};

// Compare two request infos based on timeslot only
// Returns a positive value if edge1 > edge2, 0 if they're equal, and a
// negative value if edge1 < edge2
// Min time gives the time that should be considered earliest (to handle overflow)
static inline
int64_t compare_request_info(struct request_info *edge1, struct request_info *edge2,
                              uint16_t min_time) {
    assert(edge1 != NULL);
    assert(edge2 != NULL);

    if (edge1->timeslot == edge2->timeslot)
        return 0;

    if ((edge1->timeslot >= min_time && edge2->timeslot >= min_time) ||
        (edge1->timeslot < min_time && edge2->timeslot < min_time))
        return edge1->timeslot - edge2->timeslot;
    else if (edge1->timeslot > edge2->timeslot)
        return -1;
    else
        return 1;
}

// Swap two request infos. Used in quicksort.
static inline
void swap_request_info(struct request_info *edge_0, struct request_info *edge_1) {
    assert(edge_0 != NULL);
    assert(edge_1 != NULL);

    struct request_info temp;
    temp.src = edge_0->src;
    temp.dst = edge_0->dst;
    temp.backlog = edge_0->backlog;
    temp.timeslot = edge_0->timeslot;

    edge_0->src = edge_1->src;
    edge_0->dst = edge_1->dst;
    edge_0->backlog = edge_1->backlog;
    edge_0->timeslot = edge_1->timeslot;

    edge_1->src = temp.src;
    edge_1->dst = temp.dst;
    edge_1->backlog = temp.backlog;
    edge_1->timeslot = temp.timeslot;
}

// Recursive quicksort on requests, using the compare function above
// Assume size is at least 2
static inline
void quicksort_requests(struct request_info *edges, uint32_t size, uint16_t min_time) {
    assert(edges != NULL);
    assert(size >= 2);

    // Store partition element
    struct request_info *partition = &edges[0];
    
    struct request_info *low = partition + 1;
    struct request_info *high = partition + size - 1;
    while (low < high) {
        // Find an out of place low element and high element
        while (compare_request_info(low, partition, min_time) <= 0 && low < high)
            low++;
        while (compare_request_info(high, partition, min_time) >= 0 && low < high)
            high--;

        // Swap low and high
        swap_request_info(low, high);
    }

    // Swap partition into place
    struct request_info *partition_location = high;
    if (low == high && compare_request_info(low, partition, min_time) > 0)
        partition_location = high - 1;
    swap_request_info(partition_location, partition);

    // Recursively sort portions
    uint32_t size_0 = partition_location - partition;
    if (size_0 >= 2)
        quicksort_requests(edges, size_0, min_time);
    if (size - size_0 - 1 >= 2)
        quicksort_requests(partition_location + 1, size - size_0 - 1, min_time);
}

// Based on a method suggested by wikipedia
// http://en.wikipedia.org/wiki/Exponential_distribution
double generate_exponential_variate(double mean_t_btwn_requests)
{
  assert(mean_t_btwn_requests > 0);

  double u = rand() / ((double) RAND_MAX);
  return -log(u) * mean_t_btwn_requests;
}
// Generate a sequence of requests with Poisson arrival times, puts them in edges
// Returns the number of requests generated
uint32_t generate_requests_poisson(struct request_info *edges, uint32_t size,
                                   uint32_t num_nodes, uint32_t duration,
                                   double fraction, double mean)
{
    assert(edges != NULL);

    // Generate a sequence of requests with Poisson arrival times per sender
    // and receivers chosen uniformly at random

    struct request_info *current_edge = edges;
    uint16_t src;
    uint32_t num_generated = 0;
    for (src = 0; src < num_nodes; src++) {
        uint16_t *cumulative_demands = calloc(num_nodes, sizeof(uint16_t));
        double current_time = generate_exponential_variate(mean / fraction);
        while (current_time < duration) {
            uint32_t dst = rand() / ((double) RAND_MAX) * (num_nodes - 1);
            if (dst >= src)
                dst++;  // Don't send to self
            current_edge->src = src;
            current_edge->dst = dst;
            cumulative_demands[dst] += (uint16_t) mean;
            current_edge->backlog = cumulative_demands[dst];
            current_edge->timeslot = (uint16_t) current_time;
            num_generated++;
            current_edge++;
            current_time += generate_exponential_variate(mean / fraction);
        }
        free(cumulative_demands);
    }

    assert(num_generated <= size);

    // Sort by timeslot!
    quicksort_requests(edges, num_generated, 0);

    return num_generated;
}

// Runs one experiment. Returns the number of packets admitted.
uint32_t run_experiment(struct request_info *requests, uint32_t duration, uint32_t num_requests,
                        struct bin *new_requests, struct admissible_status *status,
                        struct backlog_queue *queue_0, struct backlog_queue *queue_1,
                        struct admitted_traffic *admitted) {
    assert(requests != NULL);

    uint32_t b;
    uint32_t num_admitted = 0;
    struct request_info *current_request = requests;
    for (b = 0; b < (duration >> BATCH_SHIFT); b++) {
        // Issue all new requests for this batch
        init_bin(new_requests);
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
    double mean = 10;

    // Data structures
    struct bin *new_requests = create_bin();
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
        struct request_info *requests = malloc(max_requests * sizeof(struct request_info));

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
    free(status->admitted_bins);
    free(status);
    free(admitted);
}

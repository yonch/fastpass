/*
 * generate_requests.h
 *
 *  Created on: January 19, 2014
 *      Author: aousterh
 */

#ifndef GENERATE_REQUESTS_H_
#define GENERATE_REQUESTS_H_

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
static inline
double generate_exponential_variate(double mean_t_btwn_requests)
{
  assert(mean_t_btwn_requests > 0);

  double u = rand() / ((double) RAND_MAX);
  return -log(u) * mean_t_btwn_requests;
}
// Generate a sequence of requests with Poisson arrival times, puts them in edges
// Returns the number of requests generated
static inline
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
        double current_time = 0;
        double fractional_demand = 0;
        while (current_time < duration) {
            uint32_t dst = rand() / ((double) RAND_MAX) * (num_nodes - 1);
            double new_demand = generate_exponential_variate(mean);
            current_time += generate_exponential_variate(mean / fraction);
            if (new_demand + fractional_demand < 1) {
            	fractional_demand += new_demand;
            	continue;
            }
            if (dst >= src)
                dst++;  // Don't send to self
            current_edge->src = src;
            current_edge->dst = dst;
            new_demand += fractional_demand;
            current_edge->backlog = (uint16_t)new_demand;
            fractional_demand = new_demand - (uint16_t)new_demand;
            if (current_edge->backlog == 0) {
            	printf("oops\n");
            }
            current_edge->timeslot = (uint16_t) current_time;
            num_generated++;
            current_edge++;
        }
    }

    assert(num_generated <= size);

    // Sort by timeslot!
    quicksort_requests(edges, num_generated, 0);

    return num_generated;
}

#endif /* GENERATE_REQUESTS_H_ */

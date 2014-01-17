/*
 * benchmark_sjf.c
 *
 *  Created on: January 15, 2013
 *      Author: aousterh
 */

#include <inttypes.h>
#include <math.h>
#include <stdio.h>

#include "rdtsc.h"  // For timing
#include "admissible_traffic_sjf.h"
#include "admissible_structures_sjf.h"

#define NUM_FRACTIONS_A 11
#define NUM_SIZES_A 7
#define NUM_NODES_P 256
#define PROCESSOR_SPEED 2.8

const double admissible_fractions [NUM_FRACTIONS_A] =
    {0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 0.9, 0.95, 0.99};
const uint32_t admissible_sizes [NUM_SIZES_A] =
    {1024, 512, 256, 128, 64, 32, 16};

enum benchmark_type {
    ADMISSIBLE_SJF
};

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

// Runs one experiment. Returns the number of packets admitted.
uint32_t run_experiment(struct request_info *requests, uint32_t start_time, uint32_t end_time,
                        uint32_t num_requests, struct admissible_status *status,
                        struct request_info **next_request,
                        struct admission_core_state *core,
                        struct admitted_traffic **admitted_batch)
{
    struct admitted_traffic *admitted;
    struct fp_ring *queue_tmp;

    uint32_t b;
    uint8_t i;
    uint32_t num_admitted = 0;
    struct request_info *current_request = requests;

    assert(requests != NULL);
    assert(core->q_bin_in->tail == core->q_bin_in->head + NUM_BINS);

    for (b = (start_time >> BATCH_SHIFT); b < (end_time >> BATCH_SHIFT); b++) {
        // Issue all new requests for this batch
        while ((current_request->timeslot >> BATCH_SHIFT) == (b % (65536 >> BATCH_SHIFT)) &&
               current_request < requests + num_requests) {
            add_backlog(status, current_request->src,
                              current_request->dst, current_request->backlog);
            current_request++;
        }
 
        // Get admissible traffic
        get_admissible_traffic(core, status, admitted_batch, 0, 1, 0);

        for (i = 0; i < BATCH_SIZE; i++) {
        	/* get admitted traffic */
        	fp_ring_dequeue(status->q_admitted_out, (void **)&admitted);
        	/* update statistics */
        	num_admitted += admitted->size;
        	/* return admitted traffic to core */
        	admitted_batch[i] = admitted;
        }
    }

    *next_request = current_request;

    assert(core->q_bin_in->tail == core->q_bin_in->head + NUM_BINS);
	return num_admitted;
}

int main(void)
{
    uint16_t i, j, k;

    // keep duration less than 65536 or else Poisson wont work correctly due to sorting
    // also keep both durations an even number of batches so that bin pointers return to queue_0
    uint32_t warm_up_duration = ((10000 + 127) / 128) * 128;
    uint32_t duration = warm_up_duration + ((50000 + 127) / 128) * 128;
    double mean = 10; // Mean request size and inter-arrival time

    // Each experiment tries out a different combination of target network utilization
    // and number of nodes
    const double *fractions;
    const uint32_t *sizes;
    const uint16_t *capacities;
    uint8_t num_fractions;
    uint8_t num_parameter_2;
    
    // init fractions
    num_fractions = NUM_FRACTIONS_A;
    fractions = admissible_fractions;

    // init parameter 2 - sizes
    num_parameter_2 = NUM_SIZES_A;
    sizes = admissible_sizes;
  
    // Data structures
    struct admissible_status *status;
    struct admission_core_state core;
    struct fp_ring *q_bin;
    struct fp_ring *q_urgent;
    struct fp_ring *q_head;
    struct fp_ring *q_admitted_out;
    struct admitted_traffic **admitted_batch;
    struct admitted_traffic **all_admitted;

    /* init queues */
    q_bin = fp_ring_create(NUM_BINS_SHIFT);
    q_urgent = fp_ring_create(2 * FP_NODES_SHIFT + 1);
    q_head = fp_ring_create(2 * FP_NODES_SHIFT);
    q_admitted_out = fp_ring_create(BATCH_SHIFT);
    if (!q_bin) exit(-1);
    if (!q_urgent) exit(-1);
    if (!q_head) exit(-1);
    if (!q_admitted_out) exit(-1);

    /* init core */
    if (alloc_core_init(&core, q_bin, q_bin, q_urgent, q_urgent) != 0) {
        printf("Error initializing alloc core!\n");
	exit(-1);
    }

    /* init global status */
    status = create_admissible_status(false, 0, 0, 0, q_head, q_admitted_out);

    /* make allocated_traffic containers */
    admitted_batch = malloc(sizeof(struct admitted_traffic *) * BATCH_SIZE);
    if (!admitted_batch) exit(-1);

    for (i = 0; i < BATCH_SIZE; i++) {
        admitted_batch[i] = create_admitted_traffic();
        if (!admitted_batch[i]) exit(-1);
    }
        
    /* fill bin_queue with empty bins */
    for (i = 0; i < NUM_BINS; i++) {
        fp_ring_enqueue(q_bin, create_bin(LARGE_BIN_SIZE));
    }

    printf("target_utilization, nodes, time, observed_utilization, time/utilzn\n");
    
    for (i = 0; i < num_fractions; i++) {

        for (j = 0; j < num_parameter_2; j++) {
            double fraction = fractions[i];
            uint32_t num_nodes;
            uint16_t inter_rack_capacity;

            // Initialize data structures
            num_nodes = sizes[j];
            reset_admissible_status(status, false, 0, 0, num_nodes);
        
            for (k = 0; k < NUM_BINS; k++) {
            	struct bin *b;
            	fp_ring_dequeue(q_bin, (void **)&b);
                init_bin(b);
                fp_ring_enqueue(q_bin, b);
            }
            void *vp;
            while (fp_ring_dequeue(q_urgent, &vp) == 0)
            	/* continue to dequeue */ ;

            fp_ring_enqueue(q_urgent, (void*)URGENT_Q_HEAD_TOKEN);

            // Allocate enough space for new requests
            // (this is sufficient for <= 1 request per node per timeslot)
            uint32_t max_requests = duration * num_nodes;
            struct request_info *requests = malloc(max_requests * sizeof(struct request_info));

            // Generate new requests
            uint32_t num_requests = generate_requests_poisson(requests, max_requests, num_nodes,
                                                              duration, fraction, mean);

            // Issue/process some requests. This is a warm-up period so that there are pending
            // requests once we start timing
            struct request_info *next_request;
            run_experiment(requests, 0, warm_up_duration, num_requests,
                           status, &next_request, &core, admitted_batch);
   
            // Start timining
            uint64_t start_time = current_time();

            // Run the experiment
            uint32_t num_admitted = run_experiment(next_request, warm_up_duration, duration,
                                                   num_requests - (next_request - requests),
                                                   status, &next_request, &core, admitted_batch);
            uint64_t end_time = current_time();
            double time_per_experiment = (end_time - start_time) / (PROCESSOR_SPEED * 1000 *
                                                                    (duration - warm_up_duration));

            double utilzn = ((double) num_admitted) / ((duration - warm_up_duration) * num_nodes);

            // Print stats - percent of network capacity utilized and computation time
            // per admitted timeslot (in microseconds) for different numbers of nodes
            printf("%f, %d, %f, %f, %f\n", fraction, num_nodes, time_per_experiment,
                   utilzn, time_per_experiment / utilzn);
        }
    }

    /* TODO: memory to free up, but won't worry about it now */
    free(status);
}

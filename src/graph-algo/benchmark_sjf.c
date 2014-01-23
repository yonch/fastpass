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
#include "generate_requests.h"

#define NUM_FRACTIONS_A 11
#define NUM_SIZES_A 5
#define NUM_NODES_P 256
#define PROCESSOR_SPEED 2.8

const double admissible_fractions [NUM_FRACTIONS_A] =
    {0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 0.9, 0.95, 0.99};
const uint32_t admissible_sizes [NUM_SIZES_A] =
    {2048, 1024, 512, 256, 128/*, 64, 32, 16*/};

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

    // keep both durations an even number of batches so that bin pointers return to queue_0
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

/*
 * admissible_structures.h
 *
 *  Created on: November 19, 2013
 *      Author: aousterh
 */

#ifndef ADMISSIBLE_STRUCTURES_H_
#define ADMISSIBLE_STRUCTURES_H_

#include <assert.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "fp_ring.h"
#include "platform.h"

#include "algo_config.h"

#include "backlog.h"
#include "batch.h"
#include "bin.h"
#include "admitted.h"

#define SMALL_BIN_SIZE (10*MAX_NODES) // TODO: try smaller values
#define LARGE_BIN_SIZE (MAX_NODES * MAX_NODES) // TODO: try smaller values
#define NUM_BINS_SHIFT 5
#define NUM_BINS 32 // 2^NUM_BINS_SHIFT
#define NUM_SRC_DST_PAIRS (MAX_NODES * (MAX_NODES))  // include dst == out of boundary


// Data structures associated with one allocation core
struct admission_core_state {
	struct bin *new_request_bins[NUM_BINS + BATCH_SIZE]; // pool of backlog bins for incoming requests
	struct bin *temporary_bins[BATCH_SIZE]; // hold spare allocated bins during run
    struct batch_state batch_state;
    struct admitted_traffic **admitted;
    uint8_t is_head;
    struct fp_ring *q_urgent_in;
    struct fp_ring *q_urgent_out;
    struct admission_core_statistics stat;
    struct bin *out_bin;
}  __attribute__((aligned(64))) /* don't want sharing between cores */;

// Tracks status for admissible traffic (last send time and demand for all flows, etc.)
// over the lifetime of a controller
struct admissible_status {
    uint64_t current_timeslot;
    bool oversubscribed;
    uint16_t out_of_boundary_capacity;
    uint16_t inter_rack_capacity;  // Only valid if oversubscribed is true
    uint16_t num_nodes;
    uint64_t last_alloc_tslot[NUM_SRC_DST_PAIRS];
    struct backlog backlog;
    struct bin *new_demands;
    struct fp_ring *q_head;
    struct fp_ring *q_admitted_out;
    struct admission_statistics stat;
    struct fp_mempool *head_bin_mempool;
    struct fp_mempool *core_bin_mempool[ALGO_N_CORES];
    struct fp_mempool *admitted_traffic_mempool;
    struct admission_core_state cores[ALGO_N_CORES];
    struct fp_ring *q_bin[ALGO_N_CORES];
};



#ifdef NO_DPDK
// Prints the contents of a backlog queue, useful for debugging
static inline
void print_backlog(struct fp_ring *queue) {
    assert(queue != NULL);

	struct bin *bin = queue->elem[0];
    printf("printing backlog queue:\n");
    struct backlog_edge *edge;
    for (edge = &bin->edges[0]; edge < &bin->edges[bin->size]; edge++)
        printf("\t%d\t%d\n", edge->src, edge->dst);
}

// Prints the number of src/dst pairs per bin
static inline
void print_backlog_counts(struct fp_ring *queue) {
    assert(queue != NULL);

    printf("printing backlog bin counts\n");
    uint16_t bin_num;
    uint32_t bin_sums = 0;
    for (bin_num = 0; bin_num < NUM_BINS; bin_num++) {
		struct bin *bin = queue->elem[bin_num];
        printf("\tsize of bin %d: %d\n", bin_num, bin_size(bin));
        bin_sums += bin_size(bin);
    }
    printf("total flows: %d\n", bin_sums);
}
#endif

// Initialize all timeslots and demands to zero
static inline
void reset_admissible_status(struct admissible_status *status, bool oversubscribed,
                             uint16_t inter_rack_capacity, uint16_t out_of_boundary_capacity,
                             uint16_t num_nodes)
{
    assert(status != NULL);

    if (status->oversubscribed && !SUPPORTS_OVERSUBSCRIPTION) {
    	printf("ERROR: reset_admissible_status got oversubscribed network, "
    			"but no compile-time support for oversubscription.\n");
    	exit(-1);
    }

    status->current_timeslot = NUM_BINS;  // simplifies logic in request_timeslots
    status->oversubscribed = oversubscribed;
    status->inter_rack_capacity = inter_rack_capacity;
    status->out_of_boundary_capacity = out_of_boundary_capacity;
    status->num_nodes = num_nodes;

    uint32_t i;
    for (i = 0; i < NUM_SRC_DST_PAIRS; i++)
        status->last_alloc_tslot[i] = 0;

    backlog_init(&status->backlog);
}

// Get the index of this flow in the status data structure
static inline
uint32_t get_status_index(uint16_t src, uint16_t dst) {
    return (src << FP_NODES_SHIFT) + dst;
}

// Initializes data structures associated with one allocation core for
// a new batch of processing
static inline
void alloc_core_reset(struct admission_core_state *core,
                          struct admissible_status *status,
                          struct admitted_traffic **admitted) {
    assert(core != NULL);

    uint16_t i;
	for (i = 0; i < NUM_BINS + BATCH_SIZE; i++)
		init_bin(core->new_request_bins[i]);

    batch_state_init(&core->batch_state, status->oversubscribed,
                     status->inter_rack_capacity, status->out_of_boundary_capacity,
                     status->num_nodes);

    for (i = 0; i < BATCH_SIZE; i++)
        init_admitted_traffic(admitted[i]);
    core->admitted = admitted;

    core->is_head = 0;

    /* out_demands should have been flushed out */
    assert(core->out_bin != NULL);
    assert(is_empty_bin(core->out_bin));
}


/**
 * Initializes the alloc core.
 * Returns: 0 if successful, -1 on error.
 * @note: doesn't clean up memory on error - may leak!
 */
static inline int alloc_core_init(struct admissible_status *status,
		uint32_t core_index,
		struct fp_ring *q_urgent_in, struct fp_ring *q_urgent_out)
{
	int j;
	struct admission_core_state* core = &status->cores[core_index];

	for (j = 0; j < NUM_BINS; j++) {
		core->new_request_bins[j] = create_bin(SMALL_BIN_SIZE);
		if (core->new_request_bins[j] == NULL)
			return -1;
	}

	for (j = NUM_BINS; j < NUM_BINS + BATCH_SIZE; j++) {
		core->new_request_bins[j] = create_bin(LARGE_BIN_SIZE);
		if (core->new_request_bins[j] == NULL)
			return -1;
	}

	core->temporary_bins[0] = create_bin(LARGE_BIN_SIZE);
	if (core->temporary_bins[0] == NULL)
		return -1;

	core->q_urgent_in = q_urgent_in;
	core->q_urgent_out = q_urgent_out;

	 if (fp_mempool_get(status->core_bin_mempool[core_index],
			 (void**)&core->out_bin) != 0)
		 return -1;
	init_bin(core->out_bin);

	return 0;
}

/**
 * Initializes an already-allocated struct admissible_status.
 */
static inline
int init_admissible_status(struct admissible_status *status,
                            bool oversubscribed, uint16_t inter_rack_capacity,
                            uint16_t out_of_boundary_capacity, uint16_t num_nodes,
                            struct fp_ring *q_head, struct fp_ring *q_admitted_out,
                            struct fp_mempool *head_bin_mempool,
                            struct fp_mempool **core_bin_mempool,
                            struct fp_mempool *admitted_traffic_mempool,
                            struct fp_ring **q_bin, struct fp_ring **q_urgent)
{
    assert(status != NULL);
    uint32_t i;
    int rc;

    reset_admissible_status(status, oversubscribed, inter_rack_capacity,
                            out_of_boundary_capacity, num_nodes);

    status->q_head = q_head;
    status->q_admitted_out = q_admitted_out;
    status->head_bin_mempool = head_bin_mempool;
    memcpy(&status->core_bin_mempool[0], &core_bin_mempool[0],
    		sizeof(status->core_bin_mempool));
    status->admitted_traffic_mempool = admitted_traffic_mempool;

    memcpy(&status->q_bin, q_bin, sizeof(status->q_bin));

    fp_mempool_get(head_bin_mempool, (void**)&status->new_demands);
    init_bin(status->new_demands);

    for (i = 0; i < ALGO_N_CORES; i++) {
    	rc = alloc_core_init(status, i,
    			q_urgent[i], q_urgent[(i + 1) % ALGO_N_CORES]);
    	if (rc != 0)
    		return -1;
    }
}

/**
 * Returns an initialized struct admissible_status, or NULL on error.
 */
static inline
struct admissible_status *create_admissible_status(bool oversubscribed,
                                                   uint16_t inter_rack_capacity,
                                                   uint16_t out_of_boundary_capacity,
                                                   uint16_t num_nodes,
                                                   struct fp_ring *q_head,
                                                   struct fp_ring *q_admitted_out,
                                                   struct fp_mempool *head_bin_mempool,
                                                   struct fp_mempool **core_bin_mempool,
                                                   struct fp_mempool *admitted_traffic_mempool,
                                                   struct fp_ring **q_bin,
                                                   struct fp_ring **q_urgent)
{
    struct admissible_status *status =
    		fp_malloc("admissible_status", sizeof(struct admissible_status));

    if (status == NULL)
    	return NULL;

    init_admissible_status(status, oversubscribed, inter_rack_capacity,
                           out_of_boundary_capacity, num_nodes, q_head,
                           q_admitted_out, head_bin_mempool, core_bin_mempool,
                           admitted_traffic_mempool, q_bin, q_urgent);

    return status;
}

/**
 * Returns an admission core state, or NULL on error.
 * For testing in Python.
 */
static inline
struct admission_core_state *create_admission_core_state() {
    struct admission_core_state *core = fp_malloc("admission_core_state",
                                                  sizeof(struct admission_core_state));

    if (core == NULL)
        return NULL;

    return core;
}

/**
 * Returns an allocated_traffic container
 * For testing in Python.
 */
static inline
struct admitted_traffic **create_admitted_batch() {
    uint8_t i;

    struct admitted_traffic **admitted_batch = fp_malloc("admitted_batch",
                                                         sizeof(struct admitted_traffic *) * BATCH_SIZE);

    if (admitted_batch == NULL)
        return NULL;
    
    for (i = 0; i < BATCH_SIZE; i++) {
        admitted_batch[i] = create_admitted_traffic();
        if (admitted_batch[i] == NULL)
            return NULL;
    }
    return admitted_batch;
}

#endif /* ADMISSIBLE_STRUCTURES_H_ */

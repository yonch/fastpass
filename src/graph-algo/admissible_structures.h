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

#define BIN_MASK_SIZE		((NUM_BINS + BATCH_SIZE + 63) / 64)

// Data structures associated with one allocation core
struct admission_core_state {
	struct bin *new_request_bins[NUM_BINS + BATCH_SIZE]; // pool of backlog bins for incoming requests
	uint64_t non_empty_bins[BIN_MASK_SIZE];
	uint64_t allowed_bins[BIN_MASK_SIZE];
	struct batch_state batch_state;
    struct admitted_traffic *admitted[BATCH_SIZE];
    struct bin *out_bin;
    struct admission_core_statistics stat;
    uint64_t current_timeslot;
}  __attribute__((aligned(64))) /* don't want sharing between cores */;

// Tracks status for admissible traffic (last send time and demand for all flows, etc.)
// over the lifetime of a controller
struct admissible_status {
    bool oversubscribed;
    uint16_t out_of_boundary_capacity;
    uint16_t inter_rack_capacity;  // Only valid if oversubscribed is true
    uint16_t num_nodes;
    uint64_t last_alloc_tslot[NUM_SRC_DST_PAIRS];
    struct backlog backlog;
    struct bin *new_demands;
    struct fp_ring *q_head;
    struct fp_ring *q_admitted_out;
    struct fp_mempool *bin_mempool;
    struct fp_mempool *core_bin_mempool;
    struct fp_mempool *admitted_traffic_mempool;
    struct admission_core_state cores[ALGO_N_CORES];
    struct fp_ring *q_bin[ALGO_N_CORES];
    struct admission_statistics stat;
};

// Initialize all timeslots and demands to zero
static inline
void reset_admissible_status(struct admissible_status *status, bool oversubscribed,
                             uint16_t inter_rack_capacity, uint16_t out_of_boundary_capacity,
                             uint16_t num_nodes)
{
    assert(status != NULL);

    if (oversubscribed && !SUPPORTS_OVERSUBSCRIPTION) {
    	printf("ERROR: reset_admissible_status got oversubscribed network, "
    			"but no compile-time support for oversubscription.\n");
    	exit(-1);
    }

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
                          struct admissible_status *status) {
    assert(core != NULL);

    uint16_t i;
	for (i = 0; i < NUM_BINS + BATCH_SIZE; i++)
		init_bin(core->new_request_bins[i]);

    batch_state_init(&core->batch_state, status->oversubscribed,
                     status->inter_rack_capacity, status->out_of_boundary_capacity,
                     status->num_nodes);

    memset(core->non_empty_bins, 0, sizeof(core->non_empty_bins));

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
		uint32_t core_index, uint64_t timeslot)
{
	int j;
	struct admission_core_state* core = &status->cores[core_index];

	for (j = 0; j < NUM_BINS + BATCH_SIZE; j++) {
		core->new_request_bins[j] = create_bin(LARGE_BIN_SIZE);
		if (core->new_request_bins[j] == NULL)
			return -1;
	}

	core->allowed_bins[0] = 0x1;
	for (j = 1; j < BIN_MASK_SIZE; j++)
		core->allowed_bins[j] = 0;

	 if (fp_mempool_get(status->bin_mempool,
			 (void**)&core->out_bin) != 0)
		 return -1;
	init_bin(core->out_bin);

	core->current_timeslot = timeslot;

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
                            struct fp_mempool *bin_mempool,
                            struct fp_mempool *admitted_traffic_mempool,
                            struct fp_ring **q_bin)
{
    assert(status != NULL);
    uint32_t i;
    int rc;

    reset_admissible_status(status, oversubscribed, inter_rack_capacity,
                            out_of_boundary_capacity, num_nodes);

    status->q_head = q_head;
    status->q_admitted_out = q_admitted_out;
    status->bin_mempool = bin_mempool;
    status->admitted_traffic_mempool = admitted_traffic_mempool;

    memcpy(&status->q_bin, q_bin, sizeof(status->q_bin));

    fp_mempool_get(bin_mempool, (void**)&status->new_demands);
    init_bin(status->new_demands);

    for (i = 0; i < ALGO_N_CORES; i++) {
    	rc = alloc_core_init(status, i, NUM_BINS + i * BATCH_SIZE);
    	if (rc != 0)
    		return -1;
    }
    return 0;
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
                                                   struct fp_mempool *admitted_traffic_mempool,
                                                   struct fp_ring **q_bin)
{
    struct admissible_status *status =
    		fp_malloc("admissible_status", sizeof(struct admissible_status));

    if (status == NULL)
    	return NULL;

    init_admissible_status(status, oversubscribed, inter_rack_capacity,
                           out_of_boundary_capacity, num_nodes, q_head,
                           q_admitted_out, head_bin_mempool,
                           admitted_traffic_mempool, q_bin);

    return status;
}


#endif /* ADMISSIBLE_STRUCTURES_H_ */

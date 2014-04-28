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

#include "../protocol/topology.h"

#include "backlog.h"
#include "batch.h"
#include "bin.h"

#define SMALL_BIN_SIZE (MAX_NODES * MAX_NODES) // TODO: try smaller values
#define LARGE_BIN_SIZE (MAX_NODES * MAX_NODES) // TODO: try smaller values
#define NUM_BINS_SHIFT 5
#define NUM_BINS 32 // 2^NUM_BINS_SHIFT
#define NUM_SRC_DST_PAIRS (MAX_NODES * (MAX_NODES))  // include dst == out of boundary

struct admitted_edge {
    uint16_t src;
    uint16_t dst;
};

// Admitted traffic
struct admitted_traffic {
    uint16_t size;
    struct admitted_edge edges[MAX_NODES];
};

// Data structures associated with one allocation core
struct admission_core_state {
	struct bin *new_request_bins[NUM_BINS + BATCH_SIZE]; // pool of backlog bins for incoming requests
	struct bin *temporary_bins[BATCH_SIZE]; // hold spare allocated bins during run
    struct batch_state batch_state;
    struct admitted_traffic **admitted;
    uint8_t is_head;
    struct fp_ring *q_bin_in;
    struct fp_ring *q_bin_out;
    struct fp_ring *q_urgent_in;
    struct fp_ring *q_urgent_out;
    struct admission_core_statistics stat;
};

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
    struct fp_ring *q_head;
    struct fp_ring *q_admitted_out;
    struct admission_statistics stat;
};

// Initialize a list of a traffic admitted in a timeslot
static inline
void init_admitted_traffic(struct admitted_traffic *admitted) {
    assert(admitted != NULL);

    admitted->size = 0;
}

// Insert an edge into the admitted traffic
static inline
void insert_admitted_edge(struct admitted_traffic *admitted, uint16_t src,
                          uint16_t dst) {
    assert(admitted != NULL);
    assert(admitted->size < MAX_NODES);
    assert(src < MAX_NODES);
    assert(dst < MAX_NODES);

    struct admitted_edge *edge = &admitted->edges[admitted->size++];
    edge->src = src;
    edge->dst = dst;
}

// Get a pointer to an edge of admitted traffic
static inline
struct admitted_edge *get_admitted_edge(struct admitted_traffic *admitted,
                                        uint16_t index) {
    assert(admitted != NULL);
    assert(index <= admitted->size);

    return &admitted->edges[index];
}


#ifdef NO_DPDK
// Prints the contents of a backlog queue, useful for debugging
static inline
void print_backlog(struct fp_ring *queue) {
    assert(queue != NULL);

	struct bin *bin = queue->elem[0];
    printf("printing backlog queue:\n");
    struct backlog_edge *edge;
    for (edge = &bin->edges[bin->head]; edge < &bin->edges[bin->tail]; edge++)
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
        printf("\tsize of bin %d: %d\n", bin_num, bin->tail - bin->head);
        bin_sums += bin->tail - bin->head;
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
}

// Helper methods for testing in python
static inline
struct admitted_traffic *create_admitted_traffic(void)
{
    struct admitted_traffic *admitted =
    		fp_malloc("admitted_traffic", sizeof(struct admitted_traffic));

    if (admitted == NULL)
    	return NULL;

	init_admitted_traffic(admitted);

    return admitted;
}

static inline
void destroy_admitted_traffic(struct admitted_traffic *admitted) {
    assert(admitted != NULL);

    free(admitted);
}

static inline
struct admitted_traffic *get_admitted_struct(struct admitted_traffic *admitted,
                                             uint8_t index) {
    assert(admitted != NULL);

    return &admitted[index];
}

/**
 * Initializes the alloc core.
 * Returns: 0 if successful, -1 on error.
 * @note: doesn't clean up memory on error - may leak!
 */
static inline int alloc_core_init(struct admission_core_state* core,
		struct fp_ring *q_bin_in, struct fp_ring *q_bin_out,
		struct fp_ring *q_urgent_in, struct fp_ring *q_urgent_out)
{
	int j;

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

	core->q_bin_in = q_bin_in;
	core->q_bin_out = q_bin_out;
	core->q_urgent_in = q_urgent_in;
	core->q_urgent_out = q_urgent_out;

	return 0;
}

/**
 * Initializes an already-allocated struct admissible_status.
 */
static inline
void init_admissible_status(struct admissible_status *status,
                            bool oversubscribed, uint16_t inter_rack_capacity,
                            uint16_t out_of_boundary_capacity, uint16_t num_nodes,
                            struct fp_ring *q_head, struct fp_ring *q_admitted_out)
{
    assert(status != NULL);

    reset_admissible_status(status, oversubscribed, inter_rack_capacity,
                            out_of_boundary_capacity, num_nodes);

    status->q_head = q_head;
    status->q_admitted_out = q_admitted_out;
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
                                                   struct fp_ring *q_admitted_out)
{
    struct admissible_status *status =
    		fp_malloc("admissible_status", sizeof(struct admissible_status));

    if (status == NULL)
    	return NULL;

    init_admissible_status(status, oversubscribed, inter_rack_capacity,
                           out_of_boundary_capacity, num_nodes, q_head, q_admitted_out);

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

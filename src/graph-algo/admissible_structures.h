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

#include "atomic.h"
#include "fp_ring.h"
#include "platform.h"

#include "../protocol/topology.h"

#define BATCH_SIZE 64  // must be consistent with bitmaps in batch_state
#define BATCH_SHIFT 6  // 2^BATCH_SHIFT = BATCH_SIZE
#define NONE_AVAILABLE 251
#define MAX_TIME 66535
#define SMALL_BIN_SIZE (MAX_NODES * MAX_NODES) // TODO: try smaller values
#define LARGE_BIN_SIZE (MAX_NODES * MAX_NODES) // TODO: try smaller values
#define NUM_BINS_SHIFT 8
#define NUM_BINS 256 // 2^NUM_BINS_SHIFT
#define NUM_SRC_DST_PAIRS (MAX_NODES * (MAX_NODES + 1))  // include dst == out of boundary
#define MAX_DSTS MAX_NODES + 1  // include dst == out of boundary
#define MAX_SRCS MAX_NODES

struct admitted_edge {
    uint16_t src;
    uint16_t dst;
};

// Admitted traffic
struct admitted_traffic {
    uint16_t size;
    struct admitted_edge edges[MAX_NODES];
};

// The backlog info for one src-dst pair
struct backlog_edge {
    uint16_t src;
    uint16_t dst;
};

struct bin {
    uint16_t head;
    uint16_t tail;
    struct backlog_edge edges[0];
};

// Tracks which srcs/dsts and src/dst racks are available for this batch
struct batch_state {
    bool oversubscribed;
    uint16_t inter_rack_capacity;  // Only valid if oversubscribed is true
    uint16_t out_of_boundary_capacity;
    uint64_t allowed_mask;
    uint64_t src_endnodes [MAX_SRCS];
    uint64_t dst_endnodes [MAX_DSTS];
    uint64_t src_rack_bitmaps [MAX_NODES];
    uint64_t dst_rack_bitmaps [MAX_NODES];
    uint16_t src_rack_counts [MAX_RACKS * BATCH_SIZE];  // rows are racks
    uint16_t dst_rack_counts [MAX_RACKS * BATCH_SIZE];
    uint16_t out_of_boundary_counts [BATCH_SIZE];
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
};

// Demand/backlog info for a given src/dst pair
struct flow_status {
    atomic32_t backlog;
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
    struct flow_status flows[NUM_SRC_DST_PAIRS];
    struct fp_ring *q_head;
    struct fp_ring *q_admitted_out;
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
    assert(src < MAX_SRCS);
    assert(dst < MAX_DSTS);

    struct admitted_edge *edge = &admitted->edges[admitted->size];
    edge->src = src;
    edge->dst = dst;
    admitted->size++;
}

// Get a pointer to an edge of admitted traffic
static inline
struct admitted_edge *get_admitted_edge(struct admitted_traffic *admitted,
                                        uint16_t index) {
    assert(admitted != NULL);
    assert(index <= admitted->size);

    return &admitted->edges[index];
}

// Initialize a bin
static inline
void init_bin(struct bin *bin) {
    assert(bin != NULL);

    bin->head = 0;
    bin->tail = 0;
}

// Returns true if the bin is empty, false otherwise
static inline
bool is_empty_bin(struct bin *bin) {
    assert(bin != NULL);

    return bin->head == bin->tail;
}

// Insert new edge to the back of this bin
static inline
void enqueue_bin(struct bin *bin, uint16_t src, uint16_t dst) {
    assert(bin != NULL);
    
    bin->edges[bin->tail].src = src;
    bin->edges[bin->tail].dst = dst;
    
    bin->tail++;
}

// Obtain a pointer to the first item in the bin
// Warning: this does not check if the queue has items in it!! It will
// happily return an invalid pointer in that case. This makes loops easier.
static inline
struct backlog_edge *peek_head_bin(struct bin *bin) {
    assert(bin != NULL);

    return &bin->edges[bin->head];
}

// Remove the first item in the bin
static inline
void dequeue_bin(struct bin *bin) {
    assert(bin != NULL);
    assert(!is_empty_bin(bin));

    bin->head++;
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

// Returns the ID of the rack corresponding to id
static inline
uint16_t get_rack_from_id(uint16_t id) {
    return id >> TOR_SHIFT;
}

// Initialize an admitted bitmap
static inline
void init_batch_state(struct batch_state *state, bool oversubscribed,
                      uint16_t inter_rack_capacity, uint16_t out_of_boundary_capacity,
                      uint16_t num_nodes) {
    assert(state != NULL);
    assert(num_nodes <= MAX_NODES);

    state->oversubscribed = oversubscribed;
    state->inter_rack_capacity = inter_rack_capacity;
    state->out_of_boundary_capacity = out_of_boundary_capacity;
    state->allowed_mask = ~0UL;

    uint16_t i;
    for (i = 0; i < num_nodes; i++) {
        state->src_endnodes[i] = 0xFFFFFFFFFFFFFFFFULL;
        state->dst_endnodes[i] = 0xFFFFFFFFFFFFFFFFULL;
    }
    state->dst_endnodes[OUT_OF_BOUNDARY_NODE_ID] = 0xFFFFFFFFFFFFFFFFULL;

    if (oversubscribed) {
        for (i = 0; i < num_nodes; i++) {
            state->src_rack_bitmaps[i] = 0xFFFFFFFFFFFFFFFFULL;
            state->dst_rack_bitmaps[i] = 0xFFFFFFFFFFFFFFFFULL;
        }

        for (i = 0; i < MAX_RACKS * BATCH_SIZE; i++) {
            state->src_rack_counts[i] = 0;
            state->dst_rack_counts[i] = 0;
        }
    }

    // init out of boundary counts
    for (i = 0; i < BATCH_SIZE; i++)
        state->out_of_boundary_counts[i] = 0;
}

// Returns the first available timeslot for src and dst, or NONE_AVAILABLE
static inline
uint8_t get_first_timeslot(struct batch_state *state, uint16_t src, uint16_t dst) {
    assert(state != NULL);
    assert(src < MAX_SRCS);
    assert(dst < MAX_DSTS);

    uint64_t endnode_bitmap = state->allowed_mask & state->src_endnodes[src] & state->dst_endnodes[dst];
      
    uint64_t bitmap = endnode_bitmap;
    if (state->oversubscribed) {
        uint64_t rack_bitmap;
        if (dst == OUT_OF_BOUNDARY_NODE_ID)
            rack_bitmap = state->src_rack_bitmaps[get_rack_from_id(src)];
        else
            rack_bitmap = state->src_rack_bitmaps[get_rack_from_id(src)] &
                state->dst_rack_bitmaps[get_rack_from_id(dst)];

        bitmap = endnode_bitmap & rack_bitmap;
    }
 
    if (bitmap == 0ULL)
        return NONE_AVAILABLE;

    uint64_t timeslot;
    asm("bsfq %1,%0" : "=r"(timeslot) : "r"(bitmap));

    return (uint8_t) timeslot;
}

// Sets a timeslot as occupied for src and dst
static inline
void set_timeslot_occupied(struct batch_state *state, uint16_t src,
                           uint16_t dst, uint8_t timeslot) {
    assert(state != NULL);
    assert(src < MAX_SRCS);
    assert(dst < MAX_DSTS);
    assert(timeslot <= BATCH_SIZE);

    state->src_endnodes[src] = state->src_endnodes[src] & ~(0x1ULL << timeslot);

    if (dst == OUT_OF_BOUNDARY_NODE_ID) {
        // destination is outside of scheduling boundary
        state->out_of_boundary_counts[timeslot]++;

        if (state->out_of_boundary_counts[timeslot] == state->out_of_boundary_capacity)
            state->dst_endnodes[dst] = state->dst_endnodes[dst] & ~(0x1ULL << timeslot);
    }
    else
        state->dst_endnodes[dst] = state->dst_endnodes[dst] & ~(0x1ULL << timeslot);
  
    if (state->oversubscribed) {
        uint16_t src_rack = get_rack_from_id(src);
        state->src_rack_counts[BATCH_SIZE * src_rack + timeslot] += 1;
        if (state->src_rack_counts[BATCH_SIZE * src_rack + timeslot] == state->inter_rack_capacity)
            state->src_rack_bitmaps[src_rack] = state->src_rack_bitmaps[src_rack] & ~(0x1ULL << timeslot);
        
        if (dst != OUT_OF_BOUNDARY_NODE_ID) {
            uint16_t dst_rack = get_rack_from_id(dst);
            state->dst_rack_counts[BATCH_SIZE * dst_rack + timeslot] += 1;
            if (state->dst_rack_counts[BATCH_SIZE * dst_rack + timeslot] == state->inter_rack_capacity)
                state->dst_rack_bitmaps[dst_rack] = state->dst_rack_bitmaps[dst_rack] & ~(0x1ULL << timeslot);
        }
    }

}

// Initialize all timeslots and demands to zero
static inline
void reset_admissible_status(struct admissible_status *status, bool oversubscribed,
                             uint16_t inter_rack_capacity, uint16_t out_of_boundary_capacity,
                             uint16_t num_nodes)
{
    assert(status != NULL);

    status->current_timeslot = NUM_BINS;  // simplifies logic in request_timeslots
    status->oversubscribed = oversubscribed;
    status->inter_rack_capacity = inter_rack_capacity;
    status->out_of_boundary_capacity = out_of_boundary_capacity;
    status->num_nodes = num_nodes;

    uint32_t i;
    for (i = 0; i < NUM_SRC_DST_PAIRS; i++)
        status->last_alloc_tslot[i] = 0;
    for (i = 0; i < NUM_SRC_DST_PAIRS; i++)
        atomic32_init(&status->flows[i].backlog);
}

// Get the index of this flow in the status data structure
static inline
uint32_t get_status_index(uint16_t src, uint16_t dst) {
    return (src << FP_NODES_SHIFT) + dst;
}

// Resets the flow for this src/dst pair
static inline
void reset_flow(struct admissible_status *status, uint16_t src, uint16_t dst) {
    assert(status != NULL);

    struct flow_status *flow = &status->flows[get_status_index(src, dst)];

    int32_t backlog = atomic32_read(&flow->backlog);

    if (backlog != 0) {
        /*
         * There is pending backlog. We want to reduce the backlog, but want to
         *    keep the invariant that a flow is in a bin iff backlog != 0.
         *
         * This invariant can be broken if the allocator races to eliminate the
         *    backlog completely while we are executing this code. So we test for
         *    the race.
         */
    	if (atomic32_sub_return(&flow->backlog, backlog + 1) == -(backlog+1))
    		/* race happened, (backlog was 0 before sub) */
    		atomic32_clear(&flow->backlog);
    	else
    		/* now backlog is <=-1, it's so large that a race is unlikely */
    		atomic32_set(&flow->backlog, 1);
    }

    /* if backlog was 0, nothing to be done */
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

    init_batch_state(&core->batch_state, status->oversubscribed,
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

static inline
struct bin *create_bin(size_t size)
{
	uint32_t n_bytes =
			sizeof(struct bin) + size * sizeof(struct backlog_edge);

	struct bin *bin = fp_malloc("admissible_bin", n_bytes);
    if (bin == NULL)
    	return NULL;

    init_bin(bin);

    return bin;
}

static inline
void destroy_bin(struct bin *bin) {
    assert(bin != NULL);

    free(bin);
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

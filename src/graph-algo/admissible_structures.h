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

#define MAX_NODES 1024  // should be a multiple of 64, due to bitmaps
#define NODES_SHIFT 10  // 2^NODES_SHIFT = MAX_NODES
#define MAX_RACKS 16
#define TOR_SHIFT 5  // number of machines per rack is at most 2^TOR_SHIFT
#define BATCH_SIZE 64  // must be consistent with bitmaps in batch_state
#define BATCH_SHIFT 6  // 2^BATCH_SHIFT = BATCH_SIZE
#define NONE_AVAILABLE 251
#define MAX_TIME 66535
#define BIN_SIZE MAX_NODES * MAX_NODES // TODO: try smaller values
#define NUM_BINS_SHIFT 8
#define NUM_BINS (1 << NUM_BINS_SHIFT)
#define NUM_CORES 1

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
    struct backlog_edge edges[BIN_SIZE];
};

/* A data-structure to communicate pointers between components */
struct pointer_queue {
	uint32_t head;
	uint32_t tail;
	uint32_t mask;
	void *elem[0]; // must be last in struct
};

// Tracks which srcs/dsts and src/dst racks are available for this batch
struct batch_state {
    bool oversubscribed;
    uint16_t inter_rack_capacity;  // Only valid if oversubscribed is true
    uint64_t src_endnodes [MAX_NODES];
    uint64_t dst_endnodes [MAX_NODES];
    uint64_t src_rack_bitmaps [MAX_NODES];
    uint64_t dst_rack_bitmaps [MAX_NODES];
    uint16_t src_rack_counts [MAX_RACKS * BATCH_SIZE];  // rows are racks
    uint16_t dst_rack_counts [MAX_RACKS * BATCH_SIZE];
};

// Data structures associated with one allocation core
struct allocation_core {
	struct bin *new_request_bins[NUM_BINS + BATCH_SIZE]; // pool of backlog bins for incoming requests
	struct bin *temporary_bins[BATCH_SIZE]; // hold spare allocated bins during run
    struct batch_state batch_state;
    struct admitted_traffic *admitted[BATCH_SIZE];  // one batch of traffic admitted by this core
    uint8_t is_head;
    struct pointer_queue *admitted_out;
    struct pointer_queue *q_bin_in;
    struct pointer_queue *q_bin_out;
    struct pointer_queue *q_urgent_in;
    struct pointer_queue *q_urgent_out;
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
    uint16_t inter_rack_capacity;  // Only valid if oversubscribed is true
    uint16_t num_nodes;
    uint64_t timeslots[MAX_NODES * MAX_NODES];
    struct flow_status flows[MAX_NODES * MAX_NODES];
    struct allocation_core cores[NUM_CORES];
    struct pointer_queue *q_head;
};

// Forward declarations
static void print_backlog(struct pointer_queue *queue);

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

// Initialize a backlog queue
static inline
void init_pointer_queue(struct pointer_queue *queue) {
    assert(queue != NULL);
	queue->head = 0;
	queue->tail = 0;
}

// Insert new bin to the back of this backlog queue
static inline
void pointer_queue_enqueue(struct pointer_queue *queue, void *elem) {
	assert(queue != NULL);
	assert(elem != NULL);
	assert(queue->tail != queue->head - queue->mask - 1);

	queue->elem[queue->tail & queue->mask] = elem;
	queue->tail++;
    }

// Insert new bin to the back of this backlog queue
static inline void * pointer_queue_dequeue(struct pointer_queue *queue) {
	assert(queue != NULL);
	assert(queue->head != queue->tail);

	return queue->elem[queue->head++ & queue->mask];
}

// Insert new bin to the back of this backlog queue
static inline int pointer_queue_empty(struct pointer_queue *queue) {
	assert(queue != NULL);
	return (queue->head == queue->tail);
}

// Prints the contents of a backlog queue, useful for debugging
static inline
void print_backlog(struct pointer_queue *queue) {
    assert(queue != NULL);

	struct bin *bin = queue->elem[0];
    printf("printing backlog queue:\n");
    struct backlog_edge *edge;
    for (edge = &bin->edges[bin->head]; edge < &bin->edges[bin->tail]; edge++)
        printf("\t%d\t%d\n", edge->src, edge->dst);
}

// Prints the number of src/dst pairs per bin
static inline
void print_backlog_counts(struct pointer_queue *queue) {
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

// Returns true if this backlog queue contains duplicate entries for a src/dst pair
// Used for debugging
// Note this runs in n^2 time - super slow
static inline
bool has_duplicates(struct pointer_queue *queue) {
    assert(queue != NULL);
    
    uint16_t index;
    uint16_t bin_num;
    bool *flows = calloc(MAX_NODES * MAX_NODES, sizeof(bool));
    assert(flows != NULL);

    for (bin_num = 0; bin_num < NUM_BINS; bin_num++) {
		struct bin *bin = (struct bin *)queue->elem[bin_num];
        for (index = bin->head; index < bin->tail; index++) {
            struct backlog_edge *edge = &bin->edges[index];
            if (flows[edge->src * MAX_NODES + edge->dst] == true)
                return true;
            flows[edge->src * MAX_NODES + edge->dst] = true;
        }
    }
    return false;
}

// Returns the ID of the rack corresponding to id
static inline
uint16_t get_rack_from_id(uint16_t id) {
    return id >> TOR_SHIFT;
}

// Initialize an admitted bitmap
static inline
void init_batch_state(struct batch_state *state, bool oversubscribed,
                      uint16_t inter_rack_capacity, uint16_t num_nodes) {
    assert(state != NULL);
    assert(num_nodes <= MAX_NODES);

    state->oversubscribed = oversubscribed;
    state->inter_rack_capacity = inter_rack_capacity;

    int i;
    if (oversubscribed) {
        for (i = 0; i < num_nodes; i++) {
            state->src_endnodes[i] = 0xFFFFFFFFFFFFFFFFULL;
            state->dst_endnodes[i] = 0xFFFFFFFFFFFFFFFFULL;
            state->src_rack_bitmaps[i] = 0xFFFFFFFFFFFFFFFFULL;
            state->dst_rack_bitmaps[i] = 0xFFFFFFFFFFFFFFFFULL;
        }

        for (i = 0; i < MAX_RACKS * BATCH_SIZE; i++) {
            state->src_rack_counts[i] = 0;
            state->dst_rack_counts[i] = 0;
        }
    }
    else {
        for (i = 0; i < num_nodes; i++) {
            state->src_endnodes[i] = 0xFFFFFFFFFFFFFFFFULL;
            state->dst_endnodes[i] = 0xFFFFFFFFFFFFFFFFULL;
        }
    }
}

// Returns the first available timeslot for src and dst, or NONE_AVAILABLE
static inline
uint8_t get_first_timeslot(struct batch_state *state, uint16_t src, uint16_t dst) {
    assert(state != NULL);
    assert(src < MAX_NODES);
    assert(dst < MAX_NODES);

    uint64_t endnode_bitmap = state->src_endnodes[src] & state->dst_endnodes[dst];
      
    uint64_t bitmap = endnode_bitmap;
    if (state->oversubscribed) {
        uint64_t rack_bitmap = state->src_rack_bitmaps[get_rack_from_id(src)] &
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
    assert(src < MAX_NODES);
    assert(dst < MAX_NODES);
    assert(timeslot <= BATCH_SIZE);

    state->src_endnodes[src] = state->src_endnodes[src] & ~(0x1ULL << timeslot);
    state->dst_endnodes[dst] = state->dst_endnodes[dst] & ~(0x1ULL << timeslot);
  
    if (state->oversubscribed) {
        uint16_t src_rack = get_rack_from_id(src);
        uint16_t dst_rack = get_rack_from_id(dst);

        state->src_rack_counts[BATCH_SIZE * src_rack + timeslot] += 1;
        state->dst_rack_counts[BATCH_SIZE * dst_rack + timeslot] += 1;
        if (state->src_rack_counts[BATCH_SIZE * src_rack + timeslot] == state->inter_rack_capacity)
            state->src_rack_bitmaps[src_rack] = state->src_rack_bitmaps[src_rack] & ~(0x1ULL << timeslot);
        
        if (state->dst_rack_counts[BATCH_SIZE * dst_rack + timeslot] == state->inter_rack_capacity)
            state->dst_rack_bitmaps[dst_rack] = state->dst_rack_bitmaps[dst_rack] & ~(0x1ULL << timeslot);
    }

}

// Initialize all timeslots and demands to zero
static inline
void init_admissible_status(struct admissible_status *status, bool oversubscribed,
                            uint16_t inter_rack_capacity, uint16_t num_nodes) {
    assert(status != NULL);

    status->current_timeslot = NUM_BINS;  // simplifies logic in request_timeslots
    status->oversubscribed = oversubscribed;
    status->inter_rack_capacity = inter_rack_capacity;
    status->num_nodes = num_nodes;

    uint32_t i;
    for (i = 0; i < MAX_NODES * MAX_NODES; i++)
        status->timeslots[i] = 0;
    for (i = 0; i < MAX_NODES * MAX_NODES; i++)
        atomic32_init(&status->flows[i].backlog);
}

// Get the index of this flow in the status data structure
static inline
uint32_t get_status_index(uint16_t src, uint16_t dst) {
    return (src << NODES_SHIFT) + dst;
}

// Resets the flow for this src/dst pair
static inline
void reset_flow(struct admissible_status *status, uint16_t src, uint16_t dst) {
    assert(status != NULL);

    struct flow_status *flow = &status->flows[get_status_index(src, dst)];

    uint32_t backlog = atomic32_read(&flow->backlog);

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
void init_allocation_core(struct allocation_core *core,
                          struct admissible_status *status) {
    assert(core != NULL);

    uint16_t i;
	for (i = 0; i < NUM_BINS + BATCH_SIZE; i++)
		init_bin(core->new_request_bins[i]);

    init_batch_state(&core->batch_state, status->oversubscribed,
                     status->inter_rack_capacity, status->num_nodes);

    for (i = 0; i < BATCH_SIZE; i++)
        init_admitted_traffic(core->admitted[i]);

    core->is_head = 0;
}

// Returns a pointer to the batch of traffic admitted by a particular core
static inline
struct admitted_traffic **get_admitted_by_core(struct admissible_status *status,
                                              uint8_t core) {
    assert(status != NULL);

    return status->cores[core].admitted;
}

// Helper methods for testing in python
static inline
struct admitted_traffic *create_admitted_traffic(void) {
    struct admitted_traffic *admitted = malloc(sizeof(struct admitted_traffic) *
                                               BATCH_SIZE);
    assert(admitted != NULL);

    uint8_t i;
    for (i = 0; i < BATCH_SIZE; i++)
        init_admitted_traffic(&admitted[i]);

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
struct bin *create_bin(void) {
    size_t size = sizeof(struct bin) +
        (MAX_NODES * MAX_NODES - BIN_SIZE) * sizeof(struct backlog_edge);
    struct bin *bin = malloc(size);
    assert(bin != NULL);

    init_bin(bin);

    return bin;
}

static inline
void destroy_bin(struct bin *bin) {
    assert(bin != NULL);

    free(bin);
}

/**
 * Creates a new backlog queue, with 2^{log_size} elements
 */
static inline
struct pointer_queue *create_pointer_queue(uint32_t log_size) {
	uint32_t num_elems = (1 << log_size);
	uint32_t mem_size = sizeof(struct pointer_queue)
							+ num_elems * sizeof(void *);
    struct pointer_queue *queue = malloc(mem_size);
    assert(queue != NULL);

    queue->mask = num_elems - 1;
    init_pointer_queue(queue);

    return queue;
}

static inline
void destroy_pointer_queue(struct pointer_queue *queue) {
    assert(queue != NULL);
	assert((queue->head & queue->mask) == queue->tail);

    free(queue);
}

static inline
struct admissible_status *create_admissible_status(bool oversubscribed,
                                                   uint16_t inter_rack_capacity,
                                                   uint16_t num_nodes) {
    struct admissible_status *status = malloc(sizeof(struct admissible_status));
    assert(status != NULL);

	init_admissible_status(status, oversubscribed, inter_rack_capacity,
			num_nodes);
	uint16_t i, j;
    struct allocation_core *core;
    for (i = 0; i < NUM_CORES; i++) {
        core = &status->cores[i];

        for (j = 0; j < NUM_BINS; j++) {
			core->new_request_bins[j] = malloc(sizeof(struct bin));
			assert(core->new_request_bins[j] != NULL);
		}
		for (j = NUM_BINS; j < NUM_BINS + BATCH_SIZE; j++) {
			core->new_request_bins[j] = create_bin();
			assert(core->new_request_bins[j] != NULL);
		}

		core->temporary_bins[0] = create_bin();

		for (j = 0; j < BATCH_SIZE; j++) {
        	core->admitted[j] = malloc(sizeof(struct admitted_traffic));
            assert(core->admitted[j] != NULL);
		}
		core->admitted_out = create_pointer_queue(BATCH_SHIFT);
		core->q_bin_in = create_pointer_queue(NUM_BINS_SHIFT);
		if (!core->q_bin_in) exit(-1);
		core->q_bin_out = create_pointer_queue(NUM_BINS_SHIFT);
		if (!core->q_bin_out) exit(-1);
		core->q_urgent_in = create_pointer_queue(2 * NODES_SHIFT + 1);
		if (!core->q_urgent_in) exit(-1);
		core->q_urgent_out = create_pointer_queue(2 * NODES_SHIFT + 1);
		if (!core->q_urgent_out) exit(-1);
		core->is_head = 0;
    }

    status->q_head = create_pointer_queue(2 * NODES_SHIFT);
    assert(status->q_head != NULL);

    return status;
}

static inline
void destroy_admissible_status(struct admissible_status *status) {
    assert(status != NULL);

    uint8_t i, j;
    struct allocation_core *core;
    for (i = 0; i < NUM_CORES; i++) {
        core = &status->cores[i];
		for (j = 0; j < NUM_BINS + BATCH_SIZE; j++)
			free(core->new_request_bins[j]);
		for (j = 0; j < BATCH_SIZE; j++)
			free(core->admitted[j]);
    }
    free(status->q_head);

    free(status);
}

#endif /* ADMISSIBLE_STRUCTURES_H_ */

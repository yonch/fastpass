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
#include <stdlib.h>

#define MAX_NODES 64  // should be a multiple of 64, due to bitmaps
#define NODES_SHIFT 6  // 2^NODES_SHIFT = MAX_NODES

struct admitted_edge {
    uint16_t src;
    uint16_t dst;
    uint16_t remaining_backlog;
};

// Admitted traffic
struct admitted_traffic {
    uint8_t size;
    struct admitted_edge edges[MAX_NODES];
};

// The backlog info for one src-dst pair
struct backlog_edge {
    uint16_t src;
    uint16_t dst;
    uint16_t backlog;
    uint16_t timeslot;
};

// Table of all edges (src-dst pairs) with non-zero backlog
struct backlog_queue {
    uint16_t head;
    uint16_t tail;
    struct backlog_edge edges[MAX_NODES * MAX_NODES];
};

// Admitted bitmap - one bit per src, one bit per dst
// Tracks which srcs/dsts already have already been filled for this timeslot
struct admitted_bitmap {
    uint64_t srcs [MAX_NODES >> 6];
    uint64_t dsts [MAX_NODES >> 6];
};

// For all src/dst pairs, gives the timeslot we last sent in, also the current timeslot
struct flow_status {
    uint16_t current_timeslot;
    uint16_t timeslots[MAX_NODES * MAX_NODES];
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
                          uint16_t dst, uint16_t backlog) {
    assert(admitted != NULL);
    assert(admitted->size < MAX_NODES);
    assert(src < MAX_NODES);
    assert(dst < MAX_NODES);

    struct admitted_edge *edge = &admitted->edges[admitted->size];
    edge->src = src;
    edge->dst = dst;
    edge->remaining_backlog = backlog;
    admitted->size++;
}

// Initialize a backlog queue
static inline
void init_backlog_queue(struct backlog_queue *queue) {
    assert(queue != NULL);

    queue->head = 0;
    queue->tail = 0;
}

// Returns true if the queue is empty, false otherwise
static inline
bool is_empty_backlog(struct backlog_queue *queue) {
    assert(queue != NULL);

    return queue->head == queue->tail;
}

// Insert new edge backlog info to the back of the queue
static inline
void enqueue_backlog(struct backlog_queue *queue, uint16_t src, uint16_t dst,
                   uint16_t backlog, uint16_t timeslot) {
    assert(queue != NULL);
    
    queue->edges[queue->tail].src = src;
    queue->edges[queue->tail].dst = dst;
    queue->edges[queue->tail].backlog = backlog;
    queue->edges[queue->tail].timeslot = timeslot;
    
    queue->tail++;
}

// Obtain a pointer to the first item in the backlog
// Warning: this does not check if the queue has items in it!! It will
// happily return an invalid pointer in that case. This makes loops easier.
static inline
struct backlog_edge *peek_head_backlog(struct backlog_queue *queue) {
    assert(queue != NULL);

    return &queue->edges[queue->head];
}

// Compare two backlog edges
// Returns a positive value if edge1 > edge2, 0 if they're equal, and a negative value if edge1 < edge2
// Establishes a total ordering over all backlog edges
static inline
int64_t compare_backlog_edges(struct backlog_edge *edge1, struct backlog_edge *edge2) {
    assert(edge1 != NULL);
    assert(edge2 != NULL);

    // TODO: is there a way to do this without requiring an if statement?
    if (edge1->timeslot != edge2->timeslot)
        return edge1->timeslot - edge2->timeslot;

    uint32_t edge1_num = (edge1->src << NODES_SHIFT) + edge1->dst;
    uint32_t edge2_num = (edge2->src << NODES_SHIFT) + edge2->dst;
    return edge1_num - edge2_num;
}

// Remove the first item in the backlog queue
static inline
void dequeue_backlog(struct backlog_queue *queue) {
    assert(queue != NULL);
    assert(!is_empty_backlog(queue));

    queue->head++;
}

// Swap two backlog edges. Used in quicksort.
static inline
void swap_backlog_edges(struct backlog_edge *edge_0, struct backlog_edge *edge_1) {
    assert(edge_0 != NULL);
    assert(edge_1 != NULL);

    struct backlog_edge temp;
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

// Recursive quicksort on a backlog_queue, using the compare function above
static inline
void quicksort_backlog(struct backlog_edge *edges, uint16_t size) {
    assert(edges != NULL);

    // Store partition element
    struct backlog_edge *partition = &edges[0];
    
    struct backlog_edge *low = partition + 1;
    struct backlog_edge *high = partition + size - 1;
    while (low < high) {
        // Find an out of place low element and high element
        while (compare_backlog_edges(low, partition) <= 0 && low < high)
            low++;
        while (compare_backlog_edges(high, partition) >= 0 && low < high)
            high--;

        // Swap low and high
        swap_backlog_edges(low, high);
    }

    // Swap partition into place
    struct backlog_edge *partition_location = high;
    if (low == high && compare_backlog_edges(low, partition) > 0)
        partition_location = high - 1;
    swap_backlog_edges(partition_location, partition);

    // Recursively sort portions
    uint16_t size_0 = partition_location - partition;
    if (size_0 >= 2)
        quicksort_backlog(edges, size_0);
    if (size - size_0 - 1 >= 2)
        quicksort_backlog(partition_location + 1, size - size_0 - 1);
}

// Sorts a backlog queue using the compare function above
static inline
void sort_backlog(struct backlog_queue *queue) {
    assert(queue != NULL);

    if (queue->tail - queue->head <= 1)
        return;

    // Recursively performs quicksort on the backlog queue
    quicksort_backlog(queue->edges, queue->tail - queue->head);
}

// Initialize an admitted bitmap;
static inline
void init_admitted_bitmap(struct admitted_bitmap *admitted) {
    assert(admitted != NULL);

    int i;
    for (i = 0; i < MAX_NODES >> 6; i++) {
        admitted->srcs[i] = 0x0ULL;
        admitted->dsts[i] = 0x0ULL;
    }
}

// Returns true if src has been admitted already, false otherwise
static inline
bool src_is_admitted(struct admitted_bitmap *admitted, uint16_t src) {
    assert(admitted != NULL);

    uint16_t bit_index =  src % 64;
    return (admitted->srcs[src >> 6] & (0x1ULL << bit_index));
}

// Sets src to admitted
static inline
void set_src_admitted(struct admitted_bitmap *admitted, uint16_t src) {
    assert(admitted != NULL);

    uint16_t bit_index = src % 64;
    admitted->srcs[src >> 6] = admitted->srcs[src >> 6] | (0x1ULL << bit_index);
}

// Returns true if dst has been admitted already, false otherwise
static inline
bool dst_is_admitted(struct admitted_bitmap *admitted, uint16_t dst) {
    assert(admitted != NULL);

    uint16_t bit_index =  dst % 64;
    return (admitted->dsts[dst >> 6] & (0x1ULL << bit_index));
}

// Sets dst to admitted
static inline
void set_dst_admitted(struct admitted_bitmap *admitted, uint16_t dst) {
    assert(admitted != NULL);

    uint16_t bit_index = dst % 64;
    admitted->dsts[dst >> 6] = admitted->dsts[dst >> 6] | (0x1ULL << bit_index);
}

// Initialize all timeslots to zero
static inline
void init_flow_status(struct flow_status *status) {
    assert(status != NULL);

    status->current_timeslot = 1;

    uint16_t i;
    for (i = 0; i < MAX_NODES * MAX_NODES; i++)
        status->timeslots[i] = 0;
}

// Returns the last timeslot we transmitted in for this src/dst pair
static inline
uint16_t get_last_timeslot(struct flow_status *status, uint16_t src, uint16_t dst) {
    assert(status != NULL);

    return status->timeslots[src * MAX_NODES + dst];
}

// Sets the last timeslot we transmitted in for this src/dst pair
static inline
void set_last_timeslot(struct flow_status *status, uint16_t src, uint16_t dst,
                       uint16_t timeslot) {
    assert(status != NULL);

    status->timeslots[src * MAX_NODES + dst] = timeslot;
}

// Helper methods for testing in python
static inline
struct admitted_traffic *create_admitted_traffic() {
    struct admitted_traffic *admitted = malloc(sizeof(struct admitted_traffic));
    init_admitted_traffic(admitted);

    return admitted;
}

static inline
void destroy_admitted_traffic(struct admitted_traffic *admitted) {
    assert(admitted != NULL);

    free(admitted);
}

static inline
struct backlog_queue *create_backlog_queue() {
    struct backlog_queue *queue = malloc(sizeof(struct backlog_queue));
    init_backlog_queue(queue);

    return queue;
}

static inline
void destroy_backlog_queue(struct backlog_queue *queue) {
    assert(queue != NULL);

    free(queue);
}

static inline
struct flow_status *create_flow_status() {
    struct flow_status *status = malloc(sizeof(struct flow_status));
    init_flow_status(status);

    return status;
}

static inline
void destroy_flow_status(struct flow_status *status) {
    assert(status != NULL);

    free(status);
}

#endif /* ADMISSIBLE_STRUCTURES_H_ */

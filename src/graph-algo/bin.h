/*
 * bin.h
 *
 *  Created on: Apr 28, 2014
 *      Author: yonch
 */

#ifndef BIN_H_
#define BIN_H_

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


#endif /* BIN_H_ */

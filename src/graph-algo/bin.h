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
    uint32_t metric;
};

struct bin {
    uint32_t size;
    struct backlog_edge edges[0];
};

// Initialize a bin
static inline __attribute__((always_inline))
void init_bin(struct bin *bin) {
    assert(bin != NULL);
    bin->size = 0;
}

// Returns true if the bin is empty, false otherwise
static inline __attribute__((always_inline))
bool is_empty_bin(struct bin *bin) {
    assert(bin != NULL);
    return bin->size == 0;
}

// Insert new edge to the back of this bin
static inline __attribute__((always_inline))
void enqueue_bin(struct bin *bin, uint16_t src, uint16_t dst, uint32_t metric) {
    assert(bin != NULL);
    uint32_t n = bin->size++;
    bin->edges[n].src = src;
    bin->edges[n].dst = dst;
    bin->edges[n].metric = metric;
}

// Insert new edge to the back of this bin, when given an edge already.
static inline __attribute__((always_inline))
void enqueue_bin_edge(struct bin *bin, struct backlog_edge *edge) {
    assert(bin != NULL);
    uint32_t n = bin->size++;
    memcpy(&bin->edges[n], edge, sizeof(struct backlog_edge));
}

// Insert new edge to the back of this bin
static inline __attribute__((always_inline))
uint32_t bin_size(struct bin *bin) {
    assert(bin != NULL);
    return bin->size;
}

// Obtain a pointer to a member edge
static inline __attribute__((always_inline))
struct backlog_edge *bin_get(struct bin *bin, uint32_t index) {
    assert(bin != NULL);
    return &bin->edges[index];
}

static inline
uint32_t bin_num_bytes(uint32_t n_elem) {
	return sizeof(struct bin) + n_elem * sizeof(struct backlog_edge);
}

static inline
struct bin *create_bin(size_t size)
{
	uint32_t n_bytes = bin_num_bytes(size);

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


#endif /* BIN_H_ */

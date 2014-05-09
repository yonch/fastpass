/*
 * admitted.h
 *
 *  Created on: Apr 28, 2014
 *      Author: yonch
 */

#ifndef ADMITTED_H_
#define ADMITTED_H_

#include "platform.h"
#include "../protocol/topology.h"

#include <assert.h>

struct admitted_edge {
    uint16_t src;
    uint16_t dst;
};

// Admitted traffic
struct admitted_traffic {
    uint16_t size;
    struct admitted_edge edges[MAX_NODES];
};

// Initialize a list of a traffic admitted in a timeslot
static inline
void init_admitted_traffic(struct admitted_traffic *admitted) {
    assert(admitted != NULL);

    admitted->size = 0;
}

// Insert an edge into the admitted traffic
static inline __attribute__((always_inline))
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
static inline __attribute__((always_inline))
struct admitted_edge *get_admitted_edge(struct admitted_traffic *admitted,
                                        uint16_t index) {
    assert(admitted != NULL);
    assert(index <= admitted->size);

    return &admitted->edges[index];
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

#endif /* ADMITTED_H_ */

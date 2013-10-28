/*
 * kapoor_rizzi.h
 *
 *  Created on: October 27, 2013
 *      Author: aousterh
 */

#ifndef KAPOOR_RIZZI_H_
#define KAPOOR_RIZZI_H_

#include "graph.h"

#define MAX_STEPS 128

enum step_type {
    SPLIT_EVEN,
    SPLIT_ODD
};

// Specification of one step
// Note that all indices must be unique
struct kr_step {
    enum step_type type;
    uint8_t src1_index;
    uint8_t src2_index;
    uint8_t dest1_index;
    uint8_t dest2_index;
};

struct kr {
    uint8_t degree;
    uint8_t num_steps;
    struct kr_step steps [MAX_STEPS];
};

// Initialize a KR
void kr_init(struct kr *kr, uint8_t degree);

// Splits graph_in into matchings, using the arbitary_matching
// Uses the approximate method
void solve(struct graph *graph_in, struct graph *arbitrary_matching,
           struct graph *matchings_out);

// Helper methods for creating/destroying KR's from Python code
struct kr *create_kr(uint8_t degree);

void destroy_kr(struct kr *kr);

// Set the next step in this kr
void set_kr_step(struct kr *kr, enum step_type type, uint8_t src1_index,
                 uint8_t src2_index, uint8_t dest1_index, uint8_t dest2_index);

#endif /* KAPOOR_RIZZI_H_ */

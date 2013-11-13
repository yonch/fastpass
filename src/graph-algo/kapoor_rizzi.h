/*
 * kapoor_rizzi.h
 *
 *  Created on: October 27, 2013
 *      Author: aousterh
 */

#ifndef KAPOOR_RIZZI_H_
#define KAPOOR_RIZZI_H_

#include "graph.h"

#define MAX_MATCHINGS 48
#define MAX_STEPS 128

// Specification of one step
// Note that all indices must be unique
struct kr_step {
    uint8_t src_index;
    uint8_t dst1_index;
    uint8_t dst2_index;
};

struct kr {
    uint8_t degree;
    uint8_t num_steps;
    struct kr_step steps [MAX_STEPS];
};

struct matching_set {
    uint8_t num_matchings;
    struct graph_edges matchings [MAX_MATCHINGS];
};

// Initialize a KR
void kr_init(struct kr *kr, uint8_t degree);

// Splits graph_in into matchings, using the arbitary_matching
// Uses the approximate method
void solve(struct kr *kr, struct graph_structure *structure,
           struct graph_edges *edges_in, struct graph_edges *edges_arbitrary,
           struct matching_set *solution);

// Helper methods for creating/destroying KR's from Python code
struct kr *create_kr(uint8_t degree);

void destroy_kr(struct kr *kr);

// Set the next step in this kr
void set_kr_step(struct kr *kr, uint8_t src_index,
                 uint8_t dst1_index, uint8_t dst2_index);

// Initialize a matching_set
void matching_set_init(struct matching_set *solution);

// Helper methods for creating/destroying matching_sets
struct matching_set *create_matching_set();

void destroy_matching_set(struct matching_set *solution);

// Returns the number of matchings
uint8_t get_num_matchings(struct matching_set *solution);

// Return a pointer to a matching in the solution
struct graph_edges *get_matching(struct matching_set *solution, uint8_t index);

#endif /* KAPOOR_RIZZI_H_ */

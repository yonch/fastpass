/*
 * kapoor_rizzi.c
 *
 *  Created on: October 27, 2013
 *      Author: aousterh
 */

#include "graph.h"
#include "kapoor_rizzi.h"

// Initialize a KR solver
void kr_init(struct kr *kr, uint8_t degree) {
    assert(kr != NULL);
    assert(degree >= 1);

    kr->degree = degree;
    kr->num_steps = 0;
}

// Splits graph_in into matchings, using the arbitary_matching
// Uses the approximate method
void solve(struct kr *kr, struct graph *graph_in, struct graph *arbitrary_matching,
           struct matching_set *solution)
{
    // TODO

    // outputs matchings in solution
}

// Create a kr
struct kr *create_kr(uint8_t degree) {
    struct kr *kr_out = malloc(sizeof(struct kr));
    kr_init(kr_out, degree);

    return kr_out;
}

// Destroy a kr
void destroy_kr(struct kr *kr) {
    assert(kr != NULL);

    free(kr);
}

// Set the next step in this kr
void set_kr_step(struct kr *kr, enum step_type type, uint8_t src1_index,
                 uint8_t src2_index, uint8_t dest1_index, uint8_t dest2_index) {
    assert(kr != NULL);
    assert(kr->num_steps < MAX_STEPS);

    struct kr_step *next_step = &kr->steps[kr->num_steps];

    next_step->type = type;
    next_step->src1_index = src1_index;
    next_step->src2_index = src2_index;
    next_step->dest1_index = dest1_index;
    next_step->dest2_index = dest2_index;

    kr->num_steps++;
}

// Initialize a matching_set
void matching_set_init(struct matching_set *solution) {
    solution->num_matchings = 0;
}

// Create a matching set
struct matching_set *create_matching_set() {
    struct matching_set *solution_out = malloc(sizeof(struct matching_set));
    matching_set_init(solution_out);

    return solution_out;
}

// Destroy a matching set
void destroy_matching_set(struct matching_set *solution) {
    assert(solution != NULL);

    free(solution);
}

// Returns the number of matchings
uint8_t get_num_matchings(struct matching_set *solution) {
    assert(solution != NULL);

    return solution->num_matchings;
}

// Return a pointer to a matching in the solution
struct graph *get_matching(struct matching_set *solution, uint8_t index) {
    assert(solution != NULL);
    assert(index < solution->num_matchings);

    return &solution->matchings[index];
}





/*
 * kapoor_rizzi.c
 *
 *  Created on: October 27, 2013
 *      Author: aousterh
 */

#include "euler_split.h"
#include "graph.h"
#include "kapoor_rizzi.h"

// Initialize a KR solver
void kr_init(struct kr *kr, uint8_t degree) {
    assert(kr != NULL);
    assert(degree >= 1);

    kr->degree = degree;
    kr->num_steps = 0;
}

// Splits graph_in into matchings, using the arbitary matching
// Uses the approximate method
void solve(struct kr *kr, struct graph *graph_in, struct graph *arbitrary,
           struct matching_set *solution)
{
    assert(kr != NULL);
    assert(graph_in != NULL);
    assert(arbitrary != NULL);
    assert(solution != NULL);

    uint8_t num_matchings = kr->degree + 1;

    // Initialize the graphs
    int i;
    for (i = 0; i < MAX_MATCHINGS; i++)
        graph_init(&solution->matchings[i], graph_in->n);

    // Copy input graph and arbitrary matching to correct locations in solution
    add_graph(&solution->matchings[num_matchings], graph_in);
    add_graph(&solution->matchings[0], arbitrary);

    for (i = 0; i < kr->num_steps; i++) {
        struct kr_step *step = &kr->steps[i];

        split(&solution->matchings[step->src_index],
              &solution->matchings[step->dst1_index],
              &solution->matchings[step->dst2_index]);
    }

    solution->num_matchings = num_matchings;
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
void set_kr_step(struct kr *kr, uint8_t src_index,
                 uint8_t dst1_index, uint8_t dst2_index) {
    assert(kr != NULL);
    assert(kr->num_steps < MAX_STEPS);

    struct kr_step *next_step = &kr->steps[kr->num_steps];

    next_step->src_index = src_index;
    next_step->dst1_index = dst1_index;
    next_step->dst2_index = dst2_index;

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





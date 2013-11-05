/*
 * kapoor_rizzi.c
 *
 *  Created on: October 27, 2013
 *      Author: aousterh
 */

#include "euler_split.h"
#include "graph.h"
#include "kapoor_rizzi.h"

// Helper methods
void split_even(struct graph *src, struct graph *dst1, struct graph *dst2);
void split_odd(struct graph *src1, struct graph *src2,
               struct graph *dst1, struct graph *dst2);

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
    copy_graph(graph_in, &solution->matchings[num_matchings]);
    copy_graph(arbitrary, &solution->matchings[0]);

    for (i = 0; i < kr->num_steps; i++) {
        struct kr_step *step = &kr->steps[i];

        uint8_t src1 = step->src1_index;
        uint8_t src2 = step->src2_index;
        uint8_t dst1 = step->dst1_index;
        uint8_t dst2 = step->dst2_index;
        if (step->type == SPLIT_EVEN) {
            split_even(&solution->matchings[src1], &solution->matchings[dst1],
                       &solution->matchings[dst2]);
        }
        else {
            split_odd(&solution->matchings[src1], &solution->matchings[src2],
                      &solution->matchings[dst1], &solution->matchings[dst2]);
        }
    }

    solution->num_matchings = num_matchings;
}

void split_even(struct graph *src, struct graph *dst1, struct graph *dst2)
{
    assert(src != NULL);
    assert(dst1 != NULL);
    assert(dst2 != NULL);

    uint8_t n = src->n;
    
    split(src, dst1, dst2);
}

void split_odd(struct graph *src1, struct graph *src2,
               struct graph *dst1, struct graph *dst2)
{
    assert(src1 != NULL);
    assert(src2 != NULL);
    assert(dst1 != NULL);
    assert(dst2 != NULL);
    assert(src1->n == src2->n);

    uint8_t n = src1->n;
 
    add_graph(src1, src2);
    graph_init(src2, n);  // re-initialize graph for future use

    split(src1, dst1, dst2);
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
                 uint8_t src2_index, uint8_t dst1_index, uint8_t dst2_index) {
    assert(kr != NULL);
    assert(kr->num_steps < MAX_STEPS);

    struct kr_step *next_step = &kr->steps[kr->num_steps];

    next_step->type = type;
    next_step->src1_index = src1_index;
    next_step->src2_index = src2_index;
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





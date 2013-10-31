/*
 * graph.h
 *
 *  Created on: October 20, 2013
 *      Author: aousterh
 */

#ifndef GRAPH_H_
#define GRAPH_H_

#include <assert.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdlib.h>

#define MAX(X, Y) ((X) > (Y) ? (X) : (Y))
#define MAX_NODES 40

// Graph representation. Edges are stored in a matrix where each
// entry [i][j] corresponds to the number of edges from i to j.
// n is the number of nodes on each side of the bipartite graph
struct graph {
    uint8_t n;
    uint8_t edges[MAX_NODES][MAX_NODES];
};

// Initializes the bipartite graph
static inline
void graph_init(struct graph *graph, uint8_t n) {
    assert(graph != NULL);
    assert(n <= MAX_NODES);

    graph->n = n;

    int i, j;
    for (i = 0; i < n; i++) {
        for (j = 0; j < n; j++) {
            graph->edges[i][j] = 0;
        }
    }
}

// Returns a neighbor of vertex
// Assumes this vertex has at least one neighbor
static inline
uint8_t get_neighbor(struct graph *graph, uint8_t vertex) {
    assert(graph != NULL);
    assert(vertex < 2 * graph->n);

    uint8_t n = graph->n;
    int i;
    if (vertex < n) {
        // vertex is on left, look for a right vertex
        for (i = 0; i < n; i++) {
            if (graph->edges[vertex][i] > 0)
                return n + i;
        }
    }
    else {
        // vertex is on right, look for a left vertex
        for (i = 0; i < n; i++) {
            if (graph->edges[i][vertex - n] > 0)
                return i;
        }
    }
    
    assert(0);  // No neighbors
    return vertex;   // To avoid compile warning when asserts are disabled
}

// Returns the degree of vertex
static inline
uint8_t get_degree(struct graph *graph, uint8_t vertex) {
    assert(graph != NULL);
    assert(vertex < 2 * graph->n);

    uint8_t n = graph->n;
    int i;
    int degree = 0;
    if (vertex < n) {
        // vertex is on left
        for (i = 0; i < n; i++)
            degree += graph->edges[vertex][i];
    }
    else {
        // vertex is on right
        for (i = 0; i < n; i++)
            degree += graph->edges[i][vertex - n];
    }

    return degree;
}

// Returns the max degree
static inline
uint8_t get_max_degree(struct graph *graph) {
    assert(graph != NULL);

    uint8_t max_degree = 0;
    int i;
    for (i = 0; i < 2 * graph->n; i++)
        max_degree = MAX(max_degree, get_degree(graph, i));

    return max_degree;
}

// Adds an edge from vertex u to vertex v
// Assume u is a left vertex, v is right
static inline
void add_edge(struct graph *graph, uint8_t u, uint8_t v) {
    assert(graph != NULL);
    uint8_t n = graph->n;
    assert(u < n);
    assert(v >= n && v < 2 * n);
 
    graph->edges[u][v - n] += 1;
}

// Removes an edge from vertex u to vertex v
// Assume u is a left vertex, v is right
static inline
void remove_edge(struct graph *graph, uint8_t u, uint8_t v) {
    assert(graph != NULL);
    uint8_t n = graph->n;
    assert(u < n);
    assert(v >= n && v < 2 * n);
    assert(graph->edges[u][v - n] > 0);
    
    graph->edges[u][v - n] -= 1;
}

// Adds the edges from graph_2 to graph_1
static inline
void add_graph(struct graph *graph_1, struct graph *graph_2) {
    assert(graph_1 != NULL);
    assert(graph_2 != NULL);
    assert(graph_1->n == graph_2->n);

    uint8_t n = graph_1->n;
    int i, j;
    for (i = 0; i < n; i++) {
        for (j = 0; j < n; j++) {
            graph_1->edges[i][j] += graph_2->edges[i][j];
        }
    }
}

// Copies the graph from src to dst
static inline
void copy_graph(struct graph *src, struct graph *dst) {
    assert(src != NULL);
    assert(dst != NULL);

    graph_init(dst, src->n);
    add_graph(dst, src);
}

// Returns true if the two graphs are equivalent, false otherwise
static inline
bool are_equal(struct graph *graph_1, struct graph *graph_2) {
    assert(graph_1 != NULL);
    assert(graph_2 != NULL);

    if (graph_1->n != graph_2->n)
        return false;

    uint8_t n = graph_1->n;
    int i, j;
    for (i = 0; i < n; i++) {
        for (j = 0; j < n; j++) {
            if (graph_1->edges[i][j] != graph_2->edges[i][j])
                return false;
        }
    }

    return true;
}

// Returns true if the graph is a perfect matching, false otherwise
static inline
bool is_perfect_matching(struct graph *graph) {
    assert(graph != NULL);

    uint8_t n = graph->n;
    int i, j;
    for (i = 0; i < n; i++) {
        uint8_t edges = 0;
        for (j = 0; j < n; j++)
            edges += graph->edges[i][j];
        if (edges != 1)
            return false;
    }
    for (i = 0; i < n; i++) {
        uint8_t edges = 0;
        for (j = 0; j < n; j++)
            edges += graph->edges[j][i];
        if (edges != 1)
            return false;
    }

    return true;                
}

// Helper methods for testing in python
static inline
struct graph *create_graph_test(uint8_t n) {
    struct graph *graph_out = malloc(sizeof(struct graph));
    graph_init(graph_out, n);

    return graph_out;
}

static inline
void destroy_graph_test(struct graph * graph) {
    assert(graph != NULL);

    free(graph);
}

#endif /* GRAPH_H_ */

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
#include <stdlib.h>

#define MAX_NODES 40

// Graph representation. Edges are stored in a matrix where each
// entry [i][j] corresponds to the number of edges from i to j.
// n is the number of nodes on each side of the bipartite graph
struct graph {
    uint8_t degree;
    uint8_t n;
    uint8_t edges[MAX_NODES][MAX_NODES];
};

// Initializes the bipartite graph
static inline
void graph_init(struct graph *graph, uint8_t degree, uint8_t n) {
    assert(graph != NULL);
    assert(n <= MAX_NODES);

    graph->degree = degree;
    graph->n = n;

    int i, j;
    for (i = 0; i < n; i++) {
        for (j = 0; j < n; j++) {
            graph->edges[i][j] = 0;
        }
    }
}

// Returns a neighbor of vertex
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

// Adds an edge from vertex u to vertex v
static inline
void add_edge(struct graph *graph, uint8_t u, uint8_t v) {
    assert(graph != NULL);
    uint8_t n = graph->n;
    assert(u < 2 * n);
    assert(v < 2 * n);
    // must have one left and one right vertex
    assert((u < n && v >= n) || (v < n && u >= n));

    if (u < n)
        graph->edges[u][v - n] += 1;
    else
        graph->edges[v][u - n] += 1;
}

// Removes an edge from vertex u to vertex v
static inline
void remove_edge(struct graph *graph, uint8_t u, uint8_t v) {
    assert(graph != NULL);
    uint8_t n = graph->n;
    assert(u < 2 * n);
    assert(v < 2 * n);
    // must have one left and one right vertex
    assert((u < n && v >= n) || (v < n && u >= n));

    if (u < n)
        graph->edges[u][v - n] -= 1;
    else
        graph->edges[v][u - n] -= 1;
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

#endif /* GRAPH_H_ */

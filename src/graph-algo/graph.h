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
// entry [i][j] corresponds to the number of edges between i and j.
// Note that edges are stored twice for i != j - in [i][j] and [j][i].
struct graph {
    uint8_t degree;
    uint8_t edges[MAX_NODES][MAX_NODES];
};

// Initializes the graph
static inline
void graph_init(struct graph *graph, uint8_t degree) {
    assert(graph != NULL);

    graph->degree = degree;
}

// Returns a neighbor of vertex
static inline
uint8_t get_neighbor(struct graph *graph, uint8_t vertex) {
    assert(graph != NULL);
    assert(vertex < MAX_NODES);

    int i;

    for (i = 0; i < MAX_NODES; i++) {
        if (graph->edges[vertex][i] > 0)
            return i;
    }

    assert(0);  // No neighbors
}

// Returns the degree of vertex
static inline
uint8_t get_degree(struct graph *graph, uint8_t vertex) {
    assert(graph != NULL);
    assert(vertex < MAX_NODES);

    int i;
    int degree = 0;

    for (i = 0; i < MAX_NODES; i++)
        degree += graph->edges[vertex][i];

    return degree;
}

// Adds an edge from vertex u to vertex v
static inline
void add_edge(struct graph *graph, uint8_t u, uint8_t v) {
    assert(graph != NULL);
    assert(u < MAX_NODES);
    assert(v < MAX_NODES);

    graph->edges[u][v] += 1;
    if (u != v)
        graph->edges[v][u] += 1;
}

// Removes an edge from vertex u to vertex v
static inline
void remove_edge(struct graph *graph, uint8_t u, uint8_t v) {
    assert(graph != NULL);
    assert(u < MAX_NODES);
    assert(v < MAX_NODES);

    graph->edges[u][v] -= 1;
    if (u != v)
        graph->edges[v][u] -= 1;
}

// Adds the edges from graph_2 to graph_1
static inline
void add_graph(struct graph *graph_1, struct graph *graph_2) {
    assert(graph_1 != NULL);
    assert(graph_2 != NULL);

    int i, j;

    for (i = 0; i < MAX_NODES; i++) {
        for (j = 0; j < MAX_NODES; j++) {
            if (i == j)
                continue;
            graph_1->edges[i][j] += graph_2->edges[i][j];
        }
    }

    for (i = 0; i < MAX_NODES; i++)
        graph_1->edges[i][i] += graph_2->edges[i][i];
}

#endif /* GRAPH_H_ */

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
#include <stdio.h>
#include <stdlib.h>

#define MAX(X, Y) ((X) > (Y) ? (X) : (Y))
#define MAX_DEGREE 64
#define MAX_NODES 64

struct edge {
    uint8_t count;
    uint8_t other_vertex;
};

struct vertex_info {
    uint8_t tail;  // first unused index
    uint8_t degree;
    struct edge edges[MAX_DEGREE];  // MAX_DEGREE or MAX_NODES, whichever is smaller
};

// Graph representation. Edges are stored in adjacency lists.
// n is the number of nodes on each side of the bipartite graph
struct graph {
    uint8_t n;
    struct vertex_info vertices[2 * MAX_NODES];
};

// Helper method for debugging
static void print_graph(struct graph *graph);

// Initializes the bipartite graph
static inline
void graph_init(struct graph *graph, uint8_t n) {
    assert(graph != NULL);
    assert(n <= MAX_NODES);

    graph->n = n;

    int i, j;
    for (i = 0; i < 2 * n; i++) {
        graph->vertices[i].tail = 0;
        graph->vertices[i].degree = 0;
        for (j = 0; j < MAX_DEGREE; j++)
            graph->vertices[i].edges[j].count = 0;
    }
}

// Returns the degree of vertex
static inline
uint8_t get_degree(struct graph *graph, uint8_t vertex) {
    assert(graph != NULL);
    assert(vertex < 2 * graph->n);

    return graph->vertices[vertex].degree;
}

// Returns a neighbor of vertex
// Assumes this vertex has at least one neighbor
static inline
uint8_t get_neighbor(struct graph *graph, uint8_t vertex) {
    assert(graph != NULL);
    assert(vertex < 2 * graph->n);
    assert(get_degree(graph, vertex) > 0);

    // Return the last neighbor
    struct vertex_info *vertex_info = &graph->vertices[vertex];
    return vertex_info->edges[vertex_info->tail - 1].other_vertex;
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
static inline
void add_edge(struct graph *graph, uint8_t u, uint8_t v) {
    assert(graph != NULL);
    uint8_t n = graph->n;
    assert(u < 2 * n);
    assert(v < 2 * n);
 
    // For now just add edges to end of list
    struct vertex_info *u_info = &graph->vertices[u];
    u_info->edges[u_info->tail].count = 1;
    u_info->edges[u_info->tail].other_vertex = v;
    u_info->tail++;
    u_info->degree++;

    struct vertex_info *v_info = &graph->vertices[v];
    v_info->edges[v_info->tail].count = 1;
    v_info->edges[v_info->tail].other_vertex = u;
    v_info->tail++;
    v_info->degree++;
}

// Removes an edge from vertex u to vertex v
static inline
void remove_edge(struct graph *graph, uint8_t u, uint8_t v) {
    assert(graph != NULL);
    uint8_t n = graph->n;
    assert(u < 2 * n);
    assert(v < 2 * n);

    // Remove any edge of this type
    struct vertex_info *u_info = &graph->vertices[u];
    int i;
    assert(u_info->tail > 0);
    for (i = 0; i < u_info->tail; i++) {
        if (u_info->edges[i].count > 0 && u_info->edges[i].other_vertex == v) {
            u_info->edges[i].count--;
            break;
        }
    }
    while (u_info->edges[u_info->tail - 1].count == 0 &&
           u_info->tail > 0)
        u_info->tail--;
    u_info->degree--;

    struct vertex_info *v_info = &graph->vertices[v];
    assert(v_info->tail > 0);
    for (i = 0; i < v_info->tail; i++) {
        if (v_info->edges[i].count > 0 && v_info->edges[i].other_vertex == u) {
            v_info->edges[i].count--;
            break;
        }
    }
    while (v_info->edges[v_info->tail - 1].count == 0 &&
           v_info->tail > 0)
        v_info->tail--;
    v_info->degree--;
}

// Adds the edges from graph_2 to graph_1
static inline
void add_graph(struct graph *graph_1, struct graph *graph_2) {
    assert(graph_1 != NULL);
    assert(graph_2 != NULL);
    assert(graph_1->n == graph_2->n);

    uint8_t n = graph_1->n;
    struct vertex_info *vertex_info;
    int i, j;
    for (i = 0; i < n; i++) {
        vertex_info = &graph_2->vertices[i];
        assert(vertex_info->tail >= 0);
        for (j = 0; j < vertex_info->tail; j++) {
            if (vertex_info->edges[j].count > 0)
                add_edge(graph_1, i, vertex_info->edges[j].other_vertex);
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

// Returns the number of edges between u and v in the graph
static inline
uint8_t get_count(struct graph *graph, uint8_t u, uint8_t v) {
    assert(graph != NULL);
    assert(u < 2 * graph->n);
    assert(v < 2 * graph->n);

    struct vertex_info *u_info = &graph->vertices[u];
    uint8_t count = 0;
    int i;
    for (i = 0; i < u_info->tail; i++) {
        if (u_info->edges[i].count > 0 &&
            u_info->edges[i].other_vertex == v)
            count += u_info->edges[i].count;
    }
    return count;
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
    for (i = 0; i < 2 * n; i++) {
        for (j = 0; j < 2 * n; j++) {
            if (get_count(graph_1, i, j) != get_count(graph_2, i, j))
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
    int i;
    for (i = 0; i < 2 * n; i++) {
        if (get_degree(graph, i) != 1)
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

// Helper method for debugging
static inline
void print_graph(struct graph *graph) {
    assert(graph != NULL);
    
    printf("printing graph:\n");
    int i, j;
    for (int i = 0; i < graph->n * 2; i++) {
        struct vertex_info *v_info = &graph->vertices[i];
        printf("vertices adjacent to %d (deg %d): ", i, get_degree(graph, i));
        for (j = 0; j < v_info->tail; j++) {
            if (v_info->edges[j].count == 1)
                printf("%d ", v_info->edges[j].other_vertex);
            else
                assert(v_info->edges[j].count == 0);
        }
        printf("\n");
    }
}

#endif /* GRAPH_H_ */

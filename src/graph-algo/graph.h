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
#define MAX_DEGREE 64  // This cannot exceed 64, the width of the bitmaps
#define MAX_NODES 64

// The active edges in this graph
struct graph_edges {
    uint64_t neighbor_bitmaps[2 * MAX_NODES];
} __attribute__((__packed__));

// This tracks the id and index of a neighbor
struct neighbor {
    uint8_t id;
    uint8_t index;
} __attribute__((__packed__));

// This tracks the neighbors of this vertex
struct vertex_info {
    struct neighbor neighbors[MAX_DEGREE];
} __attribute__((__packed__));

// Graph representation. Possible edges are stored in adjacency lists
// Active edges in a specific graph are stored in a graph_edges struct
// n is the number of nodes on each side of the bipartite graph
struct graph_structure {
    uint8_t n;
    struct vertex_info vertices[2 * MAX_NODES];
} __attribute__((__packed__));

// Helper method for debugging
static void print_graph(struct graph_structure *structure, struct graph_edges *edges);

// Initialize the bipartite graph structure
static inline
void graph_structure_init(struct graph_structure *structure, uint8_t n) {
    assert(structure != NULL);
    assert(n <= MAX_NODES);

    structure->n = n;
}

// Initialize the active edges
static inline
void graph_edges_init(struct graph_edges *edges, uint8_t n) {
    assert(edges != NULL);
    assert(n <= MAX_NODES);
    
    int i;
    for (i = 0; i < 2 * n; i++)
        edges->neighbor_bitmaps[i] = 0;
}

// Returns true if vertex has at least one neighbor, false otherwise
static inline
bool has_neighbor(struct graph_edges *edges, uint8_t vertex) {
    assert(edges != NULL);
    assert(vertex < 2 * MAX_NODES);

    return (edges->neighbor_bitmaps[vertex] != 0);
}

// Returns the degree of vertex
static inline
uint8_t get_degree(struct graph_edges *edges, uint8_t vertex) {
    assert(edges != NULL);
    assert(vertex < 2 * MAX_NODES);

    uint8_t degree = 0;
    int i;
    uint64_t bitmap = edges->neighbor_bitmaps[vertex];
    while (bitmap > 0) {
        degree += bitmap & 0x1ULL;
        bitmap = bitmap >> 1;
    }
    
    return degree;
}

// Returns the max degree
static inline
uint8_t get_max_degree(struct graph_edges *edges, uint8_t n) {
    assert(edges != NULL);
    assert(n <= MAX_NODES);

    uint8_t max_degree = 0;
    int i;
    for (i = 0; i < 2 * n; i++)
        max_degree = MAX(max_degree, get_degree(edges, i));

    return max_degree;
}

// Splits an edge off from vertex in src and adds it to dst
// Returns the other vertex of the split off edge
static inline
uint8_t split_edge(struct graph_structure *structure, struct graph_edges *src_edges,
                   struct graph_edges *dst_edges, uint8_t u) {
    assert(structure != NULL);
    assert(src_edges != NULL);
    assert(dst_edges != NULL);
    assert(u < 2 * structure->n);

    // Find a neighbor
    uint64_t u_bitmap = src_edges->neighbor_bitmaps[u];
    assert(u_bitmap != 0);  // otherwise, results of bsfq are undefined
    uint64_t result;
    asm("bsfq %1,%0" : "=r"(result) : "r"(u_bitmap));
    uint8_t edge_index_u = (uint8_t) result;

    uint8_t v = structure->vertices[u].neighbors[edge_index_u].id;
    uint8_t edge_index_v = structure->vertices[u].neighbors[edge_index_u].index;
    uint64_t v_bitmap = src_edges->neighbor_bitmaps[v];
    
    // Remove the edge in both source bitmaps
    src_edges->neighbor_bitmaps[u] = u_bitmap & ~(0x1ULL << edge_index_u);
    src_edges->neighbor_bitmaps[v] = v_bitmap & ~(0x1ULL << edge_index_v);
 
    // Add the edge in both dest bitmaps
    u_bitmap = dst_edges->neighbor_bitmaps[u];
    v_bitmap = dst_edges->neighbor_bitmaps[v];
    dst_edges->neighbor_bitmaps[u] = u_bitmap | (0x1ULL << edge_index_u);
    dst_edges->neighbor_bitmaps[v] = v_bitmap | (0x1ULL << edge_index_v);

    return v;
}

// Adds an edge from vertex u to vertex v in the graph structure and edges
static inline
void add_edge(struct graph_structure *structure, struct graph_edges *edges,
              uint8_t u, uint8_t v) {
    assert(structure != NULL);
    assert(edges != NULL);
    uint8_t n = structure->n;
    assert(u < 2 * n);
    assert(v < 2 * n);
 
    // Find empty spots for the edge
    uint64_t u_bitmap = edges->neighbor_bitmaps[u];
    assert(~u_bitmap != 0);  // otherwise, results of bsfq are undefined
    uint64_t result;
    asm("bsfq %1,%0" : "=r"(result) : "r"(~u_bitmap));
    uint8_t edge_index_u = (uint8_t) result;

    uint64_t v_bitmap = edges->neighbor_bitmaps[v];
    assert(~v_bitmap != 0);
    asm("bsfq %1,%0" : "=r"(result) : "r"(~v_bitmap));
    uint8_t edge_index_v = (uint8_t) result;
 
    // Add edge to edges
    edges->neighbor_bitmaps[u] = u_bitmap | (0x1ULL << edge_index_u);
    edges->neighbor_bitmaps[v] = v_bitmap | (0x1ULL << edge_index_v);

    // Add edge to structure
    struct vertex_info *u_info = &structure->vertices[u];
    u_info->neighbors[edge_index_u].id = v;
    u_info->neighbors[edge_index_u].index = edge_index_v;
    struct vertex_info *v_info = &structure->vertices[v];
    v_info->neighbors[edge_index_v].id = u;
    v_info->neighbors[edge_index_v].index = edge_index_u;
}

// Adds the edges from graph_2 to graph_1
static inline
void add_edges(struct graph_edges *edges_1, struct graph_edges *edges_2,
               uint8_t n) {
    assert(edges_1 != NULL);
    assert(edges_2 != NULL);

    int i;
    for (i = 0; i < 2 * n; i++) {
        uint64_t bitmap_1 = edges_1->neighbor_bitmaps[i];
        uint64_t bitmap_2 = edges_2->neighbor_bitmaps[i];
        assert((bitmap_1 & bitmap_2) == 0);
        edges_1->neighbor_bitmaps[i] = bitmap_1 | bitmap_2;
    }
}

// Copies an edge set
static inline
void copy_edges(struct graph_edges *src_edges, struct graph_edges *dst_edges,
                uint8_t n) {
    assert(src_edges != NULL);
    assert(dst_edges != NULL);

    int i;
    for (i = 0; i < 2 * n; i++)
        dst_edges->neighbor_bitmaps[i] = src_edges->neighbor_bitmaps[i];
}

// Finds an edge from u to v and marks it as set. Excludes edges set in existing_edges
// Assumes the edge already exists in the structure!
static inline
void set_edge(struct graph_structure *structure, struct graph_edges *existing_edges,
              struct graph_edges *edges, uint8_t u, uint8_t v) {
    assert(structure != NULL);
    assert(existing_edges != NULL);
    assert(edges != NULL);

    struct vertex_info *u_info = &structure->vertices[u];
    uint64_t u_bitmap_existing = existing_edges->neighbor_bitmaps[u];
    uint64_t u_bitmap = edges->neighbor_bitmaps[u];
    uint64_t v_bitmap = edges->neighbor_bitmaps[v];
    int i;
    for (i = 0; i < MAX_DEGREE; i++) {
        if ((u_info->neighbors[i].id == v) && !(u_bitmap & (0x1ULL << i)) &&
            !(u_bitmap_existing & (0x1ULL << i))) {
            edges->neighbor_bitmaps[u] = u_bitmap | (0x1ULL << i);
            edges->neighbor_bitmaps[v] = v_bitmap | (0x1ULL << u_info->neighbors[i].index);
            return;
        }
    }
}

// Returns true if the two graphs are equivalent, false otherwise
static inline
bool are_equal(struct graph_edges *edges_1, struct graph_edges *edges_2,
               uint8_t n) {
    assert(edges_1 != NULL);
    assert(edges_2 != NULL);

    int i;
    for (i = 0; i < 2 * n; i++) {
        uint64_t bitmap_1 = edges_1->neighbor_bitmaps[i];
        uint64_t bitmap_2 = edges_2->neighbor_bitmaps[i];
        if (bitmap_1 != bitmap_2)
            return false;
    }

    return true;
}

// Returns true if the graph is a perfect matching, false otherwise
static inline
bool is_perfect_matching(struct graph_edges *edges, uint8_t n) {
    assert(edges != NULL);

    int i;
    for (i = 0; i < 2 * n; i++) {
        if (get_degree(edges, i) != 1)
            return false;
    }

    return true;                
}

// Helper methods for testing in python
static inline
struct graph_structure *create_graph_structure_test(uint8_t n) {

    struct graph_structure *structure_out = malloc(sizeof(struct graph_structure));
    graph_structure_init(structure_out, n);

    return structure_out;
}

static inline
void destroy_graph_structure_test(struct graph_structure *structure) {
    assert(structure != NULL);

    free(structure);
}

static inline
struct graph_edges *create_graph_edges_test(uint8_t n) {

    struct graph_edges *edges_out = malloc(sizeof(struct graph_edges));
    graph_edges_init(edges_out, n);

    return edges_out;
}

static inline
void destroy_graph_edges_test(struct graph_edges *edges) {
    assert(edges != NULL);

    free(edges);
}

// Helper method for debugging
static inline
void print_graph(struct graph_structure *structure, struct graph_edges *edges) {
    assert(structure != NULL);
    assert(edges != NULL);
    printf("printing graph\n");

    int i, j;
    for (i = 0; i < 2 * structure->n; i++) {
        struct vertex_info *v_info = &structure->vertices[i];
        printf("neighbors of %d: %"PRIx64"\t", i, edges->neighbor_bitmaps[i]);
        for (j = 0; j < MAX_DEGREE; j++) {
            if ((edges->neighbor_bitmaps[i] >> j) & 0x1ULL)
                printf("%d (%d), ", v_info->neighbors[j].id, v_info->neighbors[j].index);
        }
        printf("\n");
    }
}

#endif /* GRAPH_H_ */

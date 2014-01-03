/*
 * path_selection.c
 *
 *  Created on: January 2, 2014
 *      Author: aousterh
 */

#include "admissible_structures.h"
#include "euler_split.h"
#include "graph.h"
#include "path_selection.h"

#define NUM_GRAPHS 3

// Data structure for holding all admitted edge indices
// for a particular pair of src/dst racks
struct racks_to_nodes {
    uint16_t size;
    uint16_t admitted_indices[MAX_NODES_PER_RACK];
};

// Data structure for mapping from rack numbers back to
// src/dst ids
struct racks_to_nodes_mapping {
    struct racks_to_nodes mappings[MAX_RACKS * MAX_RACKS];
};

// Initialize a rack to nodes mapping as empty
void init_racks_to_nodes_mapping(struct racks_to_nodes_mapping *map) {
    assert(map != NULL);

    uint16_t i;
    for (i = 0; i < MAX_RACKS * MAX_RACKS; i++)
        map->mappings[i].size = 0;
}

// Obtain the index in a racks_to_nodes_mapping of a particular
// source and destination rack
uint32_t get_rack_pair_index(uint16_t src_rack, uint16_t dst_rack) {
    assert(src_rack < MAX_RACKS);
    assert(dst_rack < MAX_RACKS);

    return src_rack * MAX_RACKS + dst_rack;
}

// Print the mapping, useful for debugging
void print_racks_to_nodes_mapping_counts(struct racks_to_nodes_mapping *map) {
    assert(map != NULL);

    uint16_t total_count = 0;
    uint16_t src, dst;
    for (src = 0; src < NUM_RACKS; src++) {
        for (dst = 0; dst < NUM_RACKS; dst++) {
            uint32_t index = get_rack_pair_index(src, dst);
            printf("src %d dst %d: %d\n", src, dst, map->mappings[index].size);
            total_count += map->mappings[index].size;
        }
    }
    printf("total count: %d\n", total_count);
}

// Populate the mapping from rack ids to node ids for the given
// admitted traffic
void map_racks_to_nodes(struct admitted_traffic *admitted,
                        struct racks_to_nodes_mapping *map) {
    assert(admitted != NULL);
    assert(map != NULL);

    struct admitted_edge *edge;
    uint16_t i;
    for (i = 0; i < admitted->size; i++) {
        edge = &admitted->edges[i];
        uint16_t src_rack = get_rack_from_id(edge->src);
        uint16_t dst_rack = get_rack_from_id(edge->dst);

        uint32_t rack_pair_index = get_rack_pair_index(src_rack, dst_rack);
        struct racks_to_nodes *rack_pair = &map->mappings[rack_pair_index];
        rack_pair->admitted_indices[rack_pair->size] = i;
        rack_pair->size++;
    }
}

// Construct the graph structure and edges for the admitted traffic
// Ensure that it is a regular graph
void construct_graph(struct admitted_traffic *admitted,
                     struct graph_structure *structure,
                     struct graph_edges *edges) {
    assert(structure != NULL);
    assert(edges != NULL);

    // Set all rack counts to zero initially
    uint8_t src_rack_counts[NUM_RACKS];
    uint8_t dst_rack_counts[NUM_RACKS];
    uint16_t i;
    for (i = 0; i < NUM_RACKS; i++) {
        src_rack_counts[i] = 0;
        dst_rack_counts[i] = 0;
    }

    // Add admitted edges to graph
    uint16_t num_edges = 0;
    struct admitted_edge *edge;
    for (i = 0; i < admitted->size; i++) {
        edge = &admitted->edges[i];
        uint16_t src_rack = get_rack_from_id(edge->src);
        uint16_t dst_rack = get_rack_from_id(edge->dst);

        // Note: graph.h assumes that sources and destinations
        // use different numbers, so we must map carefully
        add_edge(structure, edges, src_rack, dst_rack + NUM_RACKS);
        src_rack_counts[src_rack]++;
        dst_rack_counts[dst_rack]++;
        num_edges++;
    }

    // Find maximum necessary degree
    uint8_t max_degree = 0;
    for (i = 0; i < NUM_RACKS; i++) {
        if (src_rack_counts[i] > max_degree)
            max_degree = src_rack_counts[i];
        if (dst_rack_counts[i] > max_degree)
            max_degree = dst_rack_counts[i];
    }

    // Enforce that max degree is a multiple of NUM_PATHS
    if (max_degree % NUM_PATHS != 0) {
        max_degree = (max_degree / NUM_PATHS + 1) * NUM_PATHS;
    }

    // Add dummy edges so that all racks have max_degree
    // TODO: implement merging approach instead?
    uint8_t src = 0;
    uint8_t dst = 0;
    while (num_edges < max_degree * NUM_RACKS) {
        while (src_rack_counts[src] == max_degree)
            src++;
        while (dst_rack_counts[dst] == max_degree)
            dst++;

        add_edge(structure, edges, src, dst + NUM_RACKS);
        src_rack_counts[src]++;
        dst_rack_counts[dst]++;
        num_edges++;
    }
}

// Assign an edge from src_rack to dst_rack in the admitted traffic to path.
// Use map to find a specific pair of src and dst nodes.
void assign_to_path(struct racks_to_nodes_mapping *map,
                    struct admitted_traffic *admitted,
                    uint8_t src_rack, uint8_t dst_rack, uint8_t path) {
    assert(map != NULL);
    assert(admitted != NULL);
    assert(path < NUM_PATHS);

    uint32_t rack_pair_index = get_rack_pair_index(src_rack, dst_rack);
    struct racks_to_nodes *rack_pair = &map->mappings[rack_pair_index];
    
    if (rack_pair->size > 0) {
        // This is not a dummy edge
        uint16_t admitted_index = rack_pair->admitted_indices[rack_pair->size - 1];
        assert(admitted_index < admitted->size);
        struct admitted_edge *edge = &admitted->edges[admitted_index];
        edge->dst = (edge->dst & PATH_MASK) + (path << PATH_SHIFT);
        rack_pair->size--;
    }
} 

// Split the edges into two sets of edges and set the path information
// for these edges.
// This effectively performs an Euler split, but does not add the split
// edges to new graphs.
void split_and_populate_paths(struct graph_structure *structure,
                              struct graph_edges *edges,
                              struct racks_to_nodes_mapping *map,
                              struct admitted_traffic *admitted,
                              uint8_t path_0, uint8_t path_1) {
    assert(structure != NULL);
    assert(edges != NULL);
    assert(map != NULL);
    assert(admitted != NULL);
    assert(path_0 < NUM_PATHS);
    assert(path_1 < NUM_PATHS);

    uint8_t n = structure->n;

    uint8_t node, cur_node, new_node;
    for (node = 0; node < n; node++) {
        cur_node = node;

        while (has_neighbor(edges, node)) {
            // Peel off two edges and assign them to path_0 and path_1
            new_node = remove_edge_to_neighbor(structure, edges, cur_node);

            // Map back to system where src/dst racks use same indices
            assign_to_path(map, admitted, cur_node, new_node - NUM_RACKS, path_0);
            cur_node = new_node;       
            assert(is_consistent(structure, edges));
 
            new_node = remove_edge_to_neighbor(structure, edges, cur_node);

            // Map back to system where src/dst racks use same indices and be sure
            // to get the src rack and dst rack correct
            assign_to_path(map, admitted, new_node, cur_node - NUM_RACKS, path_1);
            cur_node = new_node;       
            assert(is_consistent(structure, edges));
        }
    }
}

// Selects paths for traffic in admitted. Modifies the highest two
// bits of the destination to specify the path_id.
void select_paths(struct admitted_traffic *admitted) {
    assert(admitted != NULL);

    // Compute the mapping from rack ids to node ids
    struct racks_to_nodes_mapping map;
    init_racks_to_nodes_mapping(&map);
    map_racks_to_nodes(admitted, &map);

    // Initialize the graphs
    struct graph_structure structure;
    graph_structure_init(&structure, NUM_RACKS);
    struct graph_edges edges[NUM_GRAPHS];
    uint8_t i;
    for (i = 0; i < NUM_GRAPHS; i++)
        graph_edges_init(&edges[i], NUM_RACKS);

    // Construct the input graph, make it regular
    construct_graph(admitted, &structure, &edges[0]);

    // Perform an Euler splits to get NUM_COLOR/2 sets of edges
    split(&structure, &edges[0], &edges[1], &edges[2]);

    // Perform the remaining two splits to get NUM_COLOR sets of edges
    // and simultaneously mark the paths in admitted
    split_and_populate_paths(&structure, &edges[1], &map, admitted, 0, 1);
    split_and_populate_paths(&structure, &edges[2], &map, admitted, 2, 3);
}

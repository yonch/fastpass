/*
 * euler_split.c
 *
 *  Created on: October 21, 2013
 *      Author: aousterh
 */

#include "euler_split.h"
#include "graph.h"

// Splits graph_in into graph_1 and graph_2
void split(struct graph_structure *structure, struct graph_edges *edges_in,
           struct graph_edges *edges_1, struct graph_edges *edges_2) {
    assert(structure != NULL);
    assert(edges_in != NULL);
    assert(edges_1 != NULL);
    assert(edges_2 != NULL);

    uint8_t n = structure->n;

    uint8_t node, cur_node, new_node;
    for (node = 0; node < n; node++) {
        cur_node = node;

        while (has_neighbor(edges_in, node)) {
            // Peel off two edges and add them to g1 and g2
            new_node = split_edge(structure, edges_in, edges_1, cur_node);
            cur_node = new_node;
 
            new_node = split_edge(structure, edges_in, edges_2, cur_node);
            cur_node = new_node;
        }
    }
}

/*
 * euler_split.c
 *
 *  Created on: October 21, 2013
 *      Author: aousterh
 */

#include "euler_split.h"
#include "graph.h"

// Splits graph_in into graph_1 and graph_2
void split(struct graph *graph_in, struct graph *graph_1,
           struct graph *graph_2) {
    assert(graph_in != NULL);
    assert(graph_1 != NULL);
    assert(graph_2 != NULL);

    uint8_t n = graph_in->n;

    uint8_t node, cur_node, new_node;
    for (node = 0; node < n; node++) {
        cur_node = node;

        while (has_neighbor(graph_in, node)) {
            // Peel off two edges and add them to g1 and g2
            new_node = remove_edge_to_neighbor(graph_in, cur_node);
            add_edge(graph_1, cur_node, new_node);
            cur_node = new_node;

            new_node = remove_edge_to_neighbor(graph_in, cur_node);
            add_edge(graph_2, new_node, cur_node);
            cur_node = new_node;
        }
    }
}

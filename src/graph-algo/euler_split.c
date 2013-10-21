/*
 * euler_split.c
 *
 *  Created on: October 21, 2013
 *      Author: aousterh
 */

#include "euler_split.h"
#include "graph.h"

#define NOT_IN_PATH -1

// Splits graph_in into graph_1 and graph_2
void split(struct graph *graph_in, struct graph *graph_1,
           struct graph *graph_2) {

    short int path_index [MAX_NODES];
    uint8_t path_node [MAX_NODES];
    uint8_t path_len = 0;

    // Initialize path_index to empty
    int i, j;
    for (i = 0; i < MAX_NODES; i++)
        path_index[i] = NOT_IN_PATH;

    uint8_t node, cur_node;
    for (node = 0; node < MAX_NODES; node++) {
        cur_node = node;

        while (path_len > 0 || get_degree(graph_in, node) > 0) {
            if (path_index[cur_node] != NOT_IN_PATH) {
                // Found Euler cycle, partition edges
                int cycle_len = path_len - path_index[cur_node];
                
                for (j = 0; j < cycle_len; j+=2) {
                    // Have two edges (u, v), (v, cur_node)
                    uint8_t u = path_node[path_len - 2];
                    uint8_t v = path_node[path_len - 1];

                    add_edge(graph_1, u, v);
                    add_edge(graph_2, v, cur_node);

                    path_index[u] = NOT_IN_PATH;
                    path_index[v] = NOT_IN_PATH;
                    
                    path_len -= 2;
                    cur_node = u;
                }
            }
            else {
                path_node[path_len] = cur_node;
                path_index[cur_node] = path_len;
                path_len += 1;
                uint8_t new_node = get_neighbor(graph_in, cur_node);
                remove_edge(graph_in, cur_node, new_node);
                cur_node = new_node;
            }
        }
    }
    
}

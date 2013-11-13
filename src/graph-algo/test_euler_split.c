/*
 * test_euler_split.c
 *
 *  Created on: October 21, 2013
 *      Author: aousterh
 */

#include "euler_split.h"
#include "graph.h"

// Constructs a complete bipartite graph
void create_complete_bipartite_graph(struct graph *graph, uint8_t n) {
  
    int i, j;
    for (i = 0; i < n; i++) {
        for (j = n; j < 2 * n; j++)
            add_edge(graph, i, j);
    }
}

// Returns true if graph_1 and graph_2 represent a valid Euler Split of graph_in
bool check_graphs(struct graph *graph_in, struct graph *graph_1,
                  struct graph *graph_2) {

    // Check that graphs have the correct degree
    uint8_t input_degree = get_max_degree(graph_in);
    if (get_max_degree(graph_1) != input_degree / 2 ||
        get_max_degree(graph_2) != input_degree / 2)
        return false;

    // Check that vertices have the correct degree
    int i;
    for (i = 0; i < graph_in->n * 2; i++) {
        if (get_degree(graph_1, i) != input_degree / 2 ||
            get_degree(graph_2, i) != input_degree / 2)
            return false;
    }

    // Check that the combination of the two graphs equals the original graph
    add_graph(graph_1, graph_2);
    if (!are_equal(graph_in, graph_1))
        return false;

    return true;
}

int main(void) {
    struct graph g, g_copy, g1, g2;

    // Simple test of complete bipartite graphs
    int n;
    int it;
    for (it = 0; it <= 100000; it++) 
    for (n = 20; n <= 20; n += 2) {
        graph_init(&g, n);
        graph_init(&g_copy, n);
        graph_init(&g1, n);
        graph_init(&g2, n);

        create_complete_bipartite_graph(&g, n);
        create_complete_bipartite_graph(&g_copy, n);

        split(&g_copy, &g1, &g2);
    
        if (!check_graphs(&g, &g1, &g2)) {
            printf("FAIL\tn = %d\n", n);
            exit(0);
        }    
    }
    printf("PASS\n");
}

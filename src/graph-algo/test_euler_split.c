/*
 * test_euler_split.c
 *
 *  Created on: October 21, 2013
 *      Author: aousterh
 */

#include "euler_split.h"
#include "graph.h"

int main(void) {
    struct graph g, g1, g2;

    int n = 10;
    int max_degree = 4;
    graph_init(&g, max_degree, n);
    graph_init(&g1, max_degree / 2, n);
    graph_init(&g2, max_degree / 2, n);

    split(&g, &g1, &g2);
}

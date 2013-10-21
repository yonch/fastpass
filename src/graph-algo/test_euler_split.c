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

    int max_degree = 10;
    graph_init(&g, max_degree);
    graph_init(&g1, max_degree / 2);
    graph_init(&g2, max_degree / 2);

    split(&g, &g1, &g2);
}

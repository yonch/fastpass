/*
 * euler_split.h
 *
 *  Created on: October 20, 2013
 *      Author: aousterh
 */

#ifndef EULER_SPLIT_H_
#define EULER_SPLIT_H_

#include "graph.h"

// Splits edges_in into edges_1 and edges_2
void split(struct graph_structure *structure, struct graph_edges *edges_in,
           struct graph_edges *edges_1, struct graph_edges *edges_2);

#endif /* EULER_SPLIT_H_ */

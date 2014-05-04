/*
 * grant-accept.h
 *
 *  Created on: Apr 27, 2014
 *      Author: yonch
 */

#ifndef GRANT_ACCEPT_H_
#define GRANT_ACCEPT_H_

#include <stdint.h>
#include "../protocol/topology.h"
#include "partitioning.h"

#define GA_MAX_DEGREE			256

struct ga_edge {
	uint16_t src;
	uint16_t dst;
} __attribute__((__packed__));

/**
 * An adjacency structure for one partition of the graph.
 *   degree: the number of neighbors of each node in the partition
 *   neigh:  the neighbors of each node
 */
struct ga_adj {
	uint16_t	degree[PARTITION_N_NODES];
	uint16_t	neigh[PARTITION_N_NODES][GA_MAX_DEGREE];
};

/**
 * Erases all edges from the adjacency structure
 */
static inline
void ga_reset_adj(struct ga_adj *adj) {
	memset(&adj->degree[0], 0, sizeof(adj->degree));
}

/**
 * Adds neighbor 'dst' to the src at src_index
 */
static inline
void ga_adj_add_edge_by_src(struct ga_adj *adj, uint16_t src_index,
		uint16_t dst)
{
        uint16_t degree = adj->degree[src_index]++;
	adj->neigh[src_index][degree] = dst;
}

/**
 * Adds neighbor 'src' to the dst at dst_index
 */
static inline
void ga_adj_add_edge_by_dst(struct ga_adj *adj, uint16_t src,
		uint16_t dst_index)
{
	uint16_t degree = adj->degree[dst_index]++;
	adj->neigh[dst_index][degree] = src;
}

#endif /* GRANT_ACCEPT_H_ */

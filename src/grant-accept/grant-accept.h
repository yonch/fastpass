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

#define GA_PARTITION_N_NODES	128
#define GA_MAX_DEGREE			256
#define GA_N_PARTITIONS			(MAX_NODES / GA_PARTITION_N_NODES)

struct ga_edge {
	uint16_t src;
	uint16_t dst;
} __attribute__((__packed__));

/**
 * An adjacency structure for one partition of the graph.
 *   degree: the number of neighbors of each node in the partition
 *   neigh:  which are the neighbors to each node
 */
struct ga_adj {
	uint16_t	degree[GA_PARTITION_N_NODES];
	uint16_t	neigh[GA_PARTITION_N_NODES][GA_MAX_DEGREE];
};

/**
 * Erases all edges from the adjacency structure
 */
void inline ga_reset_adj(struct ga_adj *adj) {
	memset(&adj->degree[0], 0, sizeof(adj->degree));
}

/**
 * Adds neighbor 'src' to the node whose index in the partition is dst_index.
 */
void inline ga_adj_add_edge(struct ga_adj *adj, uint16_t src,
		uint16_t dst_index)
{
	uint16_t degree = adj->degree[dst_index]++;
	adj->neigh[dst_index][degree] = src;

}

/**
 * Removes the 'neigh_index'th edge from the node whose index in the partition
 *   is 'node_index'
 */
void ga_adj_delete_neigh(struct ga_adj *adj, uint16_t node_index,
		uint16_t neigh_index)
{
	uint16_t last_ind = --adj->degree[node_index];

	/* fill the hole caused by the edge removal by moving the last edge into
	 * the hole */
	adj->neigh[node_index][neigh_index] = adj->neigh[node_index][last_ind];
}

/**
 * Adds all edges in the list to the adjacency structure, by destination
 */
void inline ga_edges_dst_to_adj(struct ga_edge *edges, uint32_t n_edges,
		struct ga_adj *adj)
{
	uint32_t i;
	for (i = 0; i < n_edges; i++)
		ga_adj_add_edge(adj, edges[i].src, edges[i].dst % GA_PARTITION_N_NODES);
}

void ga_add_grant(uint16_t src, uint16_t dst);

void ga_do_grant(int partition_index);
void ga_do_accept(int partition_index);



#endif /* GRANT_ACCEPT_H_ */

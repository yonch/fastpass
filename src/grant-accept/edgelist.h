/*
 * partitioned-msgs.h
 *
 *  Created on: Apr 27, 2014
 *      Author: yonch
 */

#ifndef PARTITIONED_EDGELIST_H_
#define PARTITIONED_EDGELIST_H_

#include "grant-accept.h"

#define EDGELIST_PARTITION_MAX_EDGES		PARTITION_N_NODES

/**
 * A container for edges from some paritition x to some partition y.
 *
 * Assumes each partition has at most GA_MAX_PARTITION_EDGES edges to
 *  another partition.
 *
 * @param n: number of edges
 * @param edge: the actual edges
 *
 * To eliminate false sharing on x86, each slot is aligned to 64 bytes
 */
struct ga_edgelist {
	uint32_t n;
	struct ga_edge edge[EDGELIST_PARTITION_MAX_EDGES];
} __attribute__((align(64)));

/**
 * An edgelist partitioned by source
 */
struct ga_src_partd_edgelist {
	struct ga_edgelist src[PARTITION_N_NODES];
};

/**
 * A container for all edgelists in the graph. See ga_edgelist for assumptions.
 */
struct ga_partd_edgelist {
	struct ga_src_partd_edgelist dst[N_PARTITIONS];
};

/**
 * Adds edge to edgelist 'edgelist'
 */
void inline ga_edgelist_add(struct ga_edgelist *edgelist, uint16_t src,
		uint16_t dst)
{
	uint32_t n = edgelist->n++;
	edgelist->edge[n].src = src;
	edgelist->edge[n].dst = dst;
}

/**
 * Adds edge to the paritioned edgelist 'pedgelist'
 */
void inline ga_src_partd_edgelist_add(struct ga_src_partd_edgelist *pel,
		uint16_t src, uint16_t dst)
{
	ga_edgelist_add(&pel->src[PARTITION_OF(src)], src, dst);
}


/**
 * Adds edge to the paritioned edgelist 'pedgelist'
 */
void inline ga_partd_edgelist_add(struct ga_partd_edgelist *pel,
		uint16_t src, uint16_t dst)
{
	ga_src_partd_edgelist_add(&pel->dst[PARTITION_OF(dst)], src, dst);
}

/**
 * Deletes all edges in source partition 'src_partition'
 */
void inline ga_partd_edgelist_reset(struct ga_partd_edgelist *pel,
		uint16_t src_partition)
{
	uint32_t i;
	for(i = 0; i < N_PARTITIONS; i++)
		pel->dst[i].src[src_partition].n = 0;
}

/**
 * Adds all edges destined to a partition to an adjacency structure,
 *   keyed by destination.
 * @param pedgelist: the partitioned edgelist structure
 * @param dst_partition: which destination partition to extract
 * @param dest_adj: the adjacency structure where edges to the destination will
 *    be added
 */
void inline ga_edges_to_adj_by_dst(struct ga_partd_edgelist *pel,
		uint16_t dst_partition, struct ga_adj *dest_adj)
{
	uint32_t i;
	for(i = 0; i < N_PARTITIONS; i++) {
		struct ga_edgelist *edgelist = &pel->dst[dst_partition].src[i];
		ga_edges_to_adj(&edgelist->edge[0], edgelist->n, dest_adj);
	}
}

#endif /* PARTITIONED_EDGELIST_H_ */

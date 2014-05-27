/*
 * edgelist.h
 *
 *  Created on: Apr 27, 2014
 *      Author: yonch
 */

#ifndef EDGELIST_H_
#define EDGELIST_H_

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
} __attribute__((aligned(64))) /* don't want sharing between cores */;

/**
 * An edgelist partitioned by source
 */
struct ga_src_partd_edgelist {
	struct ga_edgelist src[N_PARTITIONS];
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
static inline
void ga_edgelist_add(struct ga_edgelist *edgelist, uint16_t src, uint16_t dst)
{
	uint32_t n = edgelist->n++;
	edgelist->edge[n].src = src;
	edgelist->edge[n].dst = dst;
}

/**
 * Adds edge to the partitioned edgelist 'pedgelist'
 */
static inline
void ga_partd_edgelist_add(struct ga_partd_edgelist *pedgelist,
		uint16_t src, uint16_t dst)
{
        struct ga_edgelist *edgelist;
        edgelist = &pedgelist->dst[PARTITION_OF(dst)].src[PARTITION_OF(src)];
        ga_edgelist_add(edgelist, src, dst);
}

/**
 * Deletes all edges in source partition 'src_partition'
 */
static inline
void ga_partd_edgelist_src_reset(struct ga_partd_edgelist *pedgelist,
		uint16_t src_partition)
{
	uint16_t dst_partition;
	for (dst_partition = 0; dst_partition < N_PARTITIONS; dst_partition++)
		pedgelist->dst[dst_partition].src[src_partition].n = 0;
}

/**
 * Adds all edges in the list to the adjacency structure, by destination
 */
static inline
void ga_edges_to_adj_by_dst(struct ga_edge *edges, uint32_t n_edges,
		struct ga_adj *adj)
{
	uint32_t i;
	for (i = 0; i < n_edges; i++)
		ga_adj_add_edge_by_dst(adj, edges[i].src,
                                       PARTITION_IDX(edges[i].dst));
}

/**
 * Adds all edges destined for a destination partition to an adjacency
 *   structure, keyed by destination node.
 * @param pedgelist: the partitioned edgelist structure
 * @param dst_partition: which destination partition to extract
 * @param dest_adj: the adjacency structure where edges to the destination will
 *    be added
 */
static inline
void ga_edgelist_to_adj_by_dst(struct ga_partd_edgelist *pedgelist,
		uint16_t dst_partition, struct ga_adj *dest_adj)
{
	uint16_t src_partition;
        struct ga_edgelist *edgelist;

	for (src_partition = 0; src_partition < N_PARTITIONS; src_partition++) {
                edgelist = &pedgelist->dst[dst_partition].src[src_partition];
		ga_edges_to_adj_by_dst(&edgelist->edge[0], edgelist->n, dest_adj);
	}
}

/**
 * Prints an edgelist to stdout for debugging
 */
static inline
void ga_print_edgelist(struct ga_edgelist *edgelist)
{
        printf("%d edges", edgelist->n);
        uint32_t i;
        for (i = 0; i < edgelist->n; i++) {
                printf(", (%d, %d)", edgelist->edge[i].src, edgelist->edge[i].dst);
        }
        printf("\n");
}

/**
 * Prints a partitioned edgelist to stdout for debugging
 */
static inline
void ga_print_partd_edgelist(struct ga_partd_edgelist *pedgelist)
{
        uint16_t src_part, dst_part;
        for (src_part = 0; src_part < N_PARTITIONS; src_part++) {
                for (dst_part = 0; dst_part < N_PARTITIONS; dst_part++) {
                        struct ga_edgelist *edgelist;
                        edgelist = &pedgelist->dst[dst_part].src[src_part];

                        if (edgelist->n == 0)
                                continue;

                        printf("edgelist for partition (%d, %d): ", src_part, dst_part);
                        ga_print_edgelist(edgelist);
                }
        }
}

#endif /* EDGELIST_H_ */

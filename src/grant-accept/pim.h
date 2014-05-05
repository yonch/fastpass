/*
 * pim.h
 *
 *  Created on: Apr 27, 2014
 *      Author: yonch
 */

#ifndef PIM_H_
#define PIM_H_

#include "grant-accept.h"
#include "edgelist.h"

#define UNALLOCATED 0
#define ALLOCATED   1

/**
 * A structure for the state of a grant partition
 */
struct pim_state {
        struct ga_adj requests_by_src[N_PARTITIONS];
        struct ga_partd_edgelist grants;
        struct ga_adj grants_by_dst[N_PARTITIONS];
        struct ga_partd_edgelist accepts;
        uint8_t grant_adj_index[MAX_NODES]; /* per src adj index of grant */
        uint8_t src_status[MAX_NODES]; /* TODO: use 1 bit per src instead of 8 */
        uint8_t dst_status[MAX_NODES]; /* TODO: use 1 bit per dst instead of 8 */
};

/**
 * For all source (left-hand) nodes in partition 'partition_index',
 *    selects edges to grant. These are added to 'grants'.
 */
void pim_do_grant(struct pim_state *state, uint16_t partition_index);

/**
 * For every destination (right-hand) node in partition 'partition_index',
 *    select among its granted edges which edge to accept. These edges are
 *    added to 'accepts'
 */
void pim_do_accept(struct pim_state *state, uint16_t partition_index);

void pim_process_accepts(struct pim_state *state, uint16_t partition_index);

void pim_add_backlog(struct pim_state *state, uint16_t src, uint16_t dst);

#endif /* PIM_H_ */

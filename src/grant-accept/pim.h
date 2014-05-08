/*
 * pim.h
 *
 *  Created on: Apr 27, 2014
 *      Author: yonch
 */

#ifndef PIM_H_
#define PIM_H_

#include "edgelist.h"
#include "grant-accept.h"
#include "../graph-algo/backlog.h"
#include "../graph-algo/bin.h"

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
        struct backlog backlog;
        struct bin *new_demands[N_PARTITIONS]; /* new demands, per src partition */
        struct admission_statistics stat;
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

/**
 * Process all of the accepts, after a timeslot is done being allocated
 */
void pim_process_accepts(struct pim_state *state, uint16_t partition_index);

void pim_add_backlog(struct pim_state *state, uint16_t src, uint16_t dst);

/**
 * Initialize all demands to zero
 */
static inline
void pim_reset_state(struct pim_state *state)
{
        uint16_t src_partition;
        for (src_partition = 0; src_partition < N_PARTITIONS; src_partition++) {
                ga_reset_adj(&state->requests_by_src[src_partition]);
        }
        backlog_init(&state->backlog);
}

/**
 * Initialize pim state
 */
static inline
void pim_init_state(struct pim_state *state)
{
        pim_reset_state(state);
        uint16_t partition;
        for (partition = 0; partition < N_PARTITIONS; partition++) {
                state->new_demands[partition] = create_bin(MAX_NODES);
        }
}

#endif /* PIM_H_ */

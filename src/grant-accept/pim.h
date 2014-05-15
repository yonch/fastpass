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
#include "../graph-algo/admitted.h"
#include "../graph-algo/backlog.h"
#include "../graph-algo/bin.h"
#include "../graph-algo/fp_ring.h"
#include "../graph-algo/platform.h"

#define SMALL_BIN_SIZE (10*MAX_NODES / N_PARTITIONS)

/* packing of bitmasks into 8 bit words */
#define PIM_BITMASKS_PER_8_BIT      8
#define PIM_BITMASK_WORD(node)      (node >> 3)
#define PIM_BITMASK_SHIFT(node)     (node & (PIM_BITMASKS_PER_8_BIT - 1))

/**
 * A structure for the state of a grant partition
 */
struct pim_state {
        struct ga_adj requests_by_src[N_PARTITIONS]; /* per src partition */
        struct ga_partd_edgelist grants;
        struct ga_adj grants_by_dst[N_PARTITIONS]; /* per dst partition */
        struct ga_partd_edgelist accepts;
        uint8_t grant_adj_index[MAX_NODES]; /* per src adj index of grant */
        uint8_t src_endnodes[MAX_NODES / PIM_BITMASKS_PER_8_BIT];
        uint8_t dst_endnodes[MAX_NODES / PIM_BITMASKS_PER_8_BIT];
        struct backlog backlog;
        struct bin *new_demands[N_PARTITIONS]; /* per src partition */
        struct fp_ring *q_admitted_out;
        struct fp_mempool *admitted_traffic_mempool;
        struct admission_statistics stat;
};

/**
 * Increase the backlog from src to dst
 */
void pim_add_backlog(struct pim_state *state, uint16_t src, uint16_t dst,
                     uint32_t amount);

/**
 * Flush the backlog to the pim_state
 */
void pim_flush_backlog(struct pim_state *state);

/**
 * Prepare data structures so they are ready to allocate the next timeslot
 */
void pim_prepare(struct pim_state *state, uint16_t partition_index);

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
void pim_init_state(struct pim_state *state, struct fp_ring *q_admitted_out,
                    struct fp_mempool *admitted_traffic_mempool)
{
        pim_reset_state(state);
        uint16_t partition;
        for (partition = 0; partition < N_PARTITIONS; partition++) {
                /* TODO: use bins instead */
                state->new_demands[partition] = create_bin(MAX_NODES);
        }

        state->q_admitted_out = q_admitted_out;
        state->admitted_traffic_mempool = admitted_traffic_mempool;
}

#endif /* PIM_H_ */

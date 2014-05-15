/*
 * pim.c
 *
 *  Created on: May 2, 2014
 *      Author: aousterh
 */

#include "pim.h"

#include <assert.h>

#define MAX_TRIES 10

/**
 * Return true if the src is already allocated, false otherwise.
 */
static inline
bool src_is_allocated(struct pim_state *state, uint16_t src) {
        return ((state->src_endnodes[PIM_BITMASK_WORD(src)] >> PIM_BITMASK_SHIFT(src)) &
                0x1);
}

/**
 * Return true if the dst is already allocated, false otherwise.
 */
static inline
bool dst_is_allocated(struct pim_state *state, uint16_t dst) {
    return ((state->dst_endnodes[PIM_BITMASK_WORD(dst)] >> PIM_BITMASK_SHIFT(dst)) &
                0x1);
}

/**
 * Mark the src as allocated.
 */
static inline
void mark_src_allocated(struct pim_state *state, uint16_t src) {
        state->src_endnodes[PIM_BITMASK_WORD(src)] |= (0x1 << PIM_BITMASK_SHIFT(src));
}

/**
 * Mark the dst as allocated.
 */
static inline
void mark_dst_allocated(struct pim_state *state, uint16_t dst) {
        state->dst_endnodes[PIM_BITMASK_WORD(dst)] |= (0x1 << PIM_BITMASK_SHIFT(dst));
}

/**
 * Move new demands from 'new_demands' to the backlog struct. Also make sure
 * that they are included in requests_by_src
 */
static inline
void process_new_requests(struct pim_state *state, uint16_t partition_index) {
        struct bin *new_demands = state->new_demands[partition_index];
        
        uint32_t i;
        for (i = 0; i < bin_size(new_demands); i++) {
                /* check that this edge belongs to this partition */
                struct backlog_edge *edge = bin_get(new_demands, i);
                assert(partition_index == PARTITION_OF(edge->src));

                if (backlog_increase(&state->backlog, edge->src, edge->dst,
                                     edge->metric, &state->stat) == false)
                        continue; /* no need to add to requests */

                ga_adj_add_edge_by_src(&state->requests_by_src[partition_index],
                                       PARTITION_IDX(edge->src), edge->dst);
        }

        /* mark bin as empty */
        init_bin(new_demands);
}

/**
 * Prepare data structures so they are ready to allocate the next timeslot
 */
void pim_prepare(struct pim_state *state, uint16_t partition_index) {
        /* reset accepts */
        ga_partd_edgelist_src_reset(&state->accepts, partition_index);

        /* reset src and dst endnodes */
        uint32_t start_word = PIM_BITMASK_WORD(first_in_partition(partition_index));
        uint32_t words_per_partition = PIM_BITMASK_WORD(PARTITION_N_NODES);
        memset(((uint8_t *) &state->src_endnodes) + start_word, 0, PARTITION_N_NODES);
        memset(((uint8_t *) &state->dst_endnodes) + start_word, 0, PARTITION_N_NODES);
}

/**
 * For all source (left-hand) nodes in partition 'partition_index',
 *    selects edges to grant. These are added to 'grants'.
 */
void pim_do_grant(struct pim_state *state, uint16_t partition_index) {
        /* add new backlogs to requests */
        process_new_requests(state, partition_index);

        /* reset grant edgelist */
        ga_partd_edgelist_src_reset(&state->grants, partition_index);

        /* for each src in the partition, randomly choose a dst to grant to */
        uint16_t src;
        for (src = first_in_partition(partition_index);
             src <= last_in_partition(partition_index);
             src++) {
                if (src_is_allocated(state, src))
                        continue; /* this src has been allocated in this timeslot */

                uint16_t src_index = PARTITION_IDX(src);
                uint16_t degree = state->requests_by_src[partition_index].degree[src_index];
                if (degree == 0)
                        continue; /* no requests for this src */

                /* find an un-allocated destination to grant to */
                uint8_t tries = MAX_TRIES;
                uint16_t dst_adj_index, dst;
                do {
                        dst_adj_index = rand() % degree;
                        dst = state->requests_by_src[partition_index].neigh[src_index][dst_adj_index];
                } while (dst_is_allocated(state, dst) && (--tries > 0));

                if (dst_is_allocated(state, dst))
                        continue; /* couldn't find a free dst*/

                /* add the granted edge */
                ga_partd_edgelist_add(&state->grants, src, dst);

                /* record the index of the destination we granted to */
                state->grant_adj_index[src] = dst_adj_index;
        }
}

/**
 * For every destination (right-hand) node in partition 'partition_index',
 *    select among its granted edges which edge to accept. These edges are
 *    added to 'accepts'
 */
void pim_do_accept(struct pim_state *state, uint16_t partition_index) {
        /* reset grant adjacency list */
        ga_reset_adj(&state->grants_by_dst[partition_index]);

        /* sort grants from all src partitions by destination node */
        struct ga_adj *dest_adj = &state->grants_by_dst[partition_index];
        ga_edgelist_to_adj_by_dst(&state->grants, partition_index, dest_adj);

        /* for each dst in the partition, randomly choose a src to accept */
        uint16_t dst;
        for (dst = first_in_partition(partition_index);
             dst <= last_in_partition(partition_index);
             dst++) {
                uint16_t dst_index = PARTITION_IDX(dst);
                uint16_t degree = state->grants_by_dst[partition_index].degree[dst_index];
                if (degree == 0)
                        continue; /* no grants for this dst */

                /* choose an edge and accept it */
                uint16_t src_adj_index = rand() % degree;
                uint16_t src = state->grants_by_dst[partition_index].neigh[dst_index][src_adj_index];
                ga_partd_edgelist_add(&state->accepts, src, dst);

                /* mark the src and dst as allocated for this timeslot */
                mark_src_allocated(state, src); /* TODO: this write might cause cache contention */
                mark_dst_allocated(state, dst);
        }
}

/**
 * Process all of the accepts, after a timeslot is done being allocated
 */
void pim_process_accepts(struct pim_state *state, uint16_t partition_index) {
        uint16_t dst_partition;

        /* get memory for admitted traffic, init it */
        struct admitted_traffic *admitted;
        while (fp_mempool_get(state->admitted_traffic_mempool, (void**) &admitted) != 0)
                printf("failure to allocate admitted memory at partition %d\n",
                       partition_index);
        init_admitted_traffic(admitted);

        /* iterate through all accepted edges */
        for (dst_partition = 0; dst_partition < N_PARTITIONS; dst_partition++) {
                struct ga_edgelist *edgelist;
                edgelist = &state->accepts.dst[dst_partition].src[partition_index];

                uint16_t i;
                for (i = 0; i < edgelist->n; i++) {
                        struct ga_edge *edge = &edgelist->edge[i];

                        /* add edge to admitted traffic */
                        insert_admitted_edge(admitted, edge->src, edge->dst);

                        /* decrease the backlog */
                        int32_t backlog = backlog_decrease(&state->backlog, edge->src, edge->dst);
                        if (backlog != 0)
                                continue; /* there is remaining backlog */

                        /* no more backlog, delete the edge from requests */
                        uint16_t grant_adj_index = state->grant_adj_index[edge->src];
                        ga_adj_delete_neigh(&state->requests_by_src[PARTITION_OF(edge->src)],
                                            PARTITION_IDX(edge->src), grant_adj_index);
                }
        }

        /* send out the admitted traffic */
        while (fp_ring_enqueue(state->q_admitted_out, admitted) != 0)
                printf("failure to enqueue admitted traffic at partition %d\n",
                       partition_index);
}

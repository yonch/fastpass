/*
 * pim.c
 *
 *  Created on: May 2, 2014
 *      Author: aousterh
 */

#include "pim.h"

#include <assert.h>

#include "phase.h"

#define MAX_TRIES 10
#define RING_DEQUEUE_BURST_SIZE		256

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
 * Flushes the bin for a specific partition to state and allocates a new bin
 */
void _flush_backlog_now(struct pim_state *state, uint16_t partition_index) {
        /* enqueue state->new_demands[partition_index] */
        while (fp_ring_enqueue(state->q_new_demands[partition_index],
                               state->new_demands[partition_index]) == -ENOBUFS)
                adm_log_wait_for_space_in_q_head(&state->stat);

        /* get a fresh bin for state->new_demands[partition_index] */
        while (fp_mempool_get(state->bin_mempool,
                              (void**) &state->new_demands[partition_index]) == -ENOENT)
                adm_log_new_demands_bin_alloc_failed(&state->stat);

        init_bin(state->new_demands[partition_index]);
}

/**
 * Flush the backlog to state, for all partitions
 */
void pim_flush_backlog(struct pim_state *state) {
        uint16_t partition;
        for (partition = 0; partition < N_PARTITIONS; partition++) {
                if (is_empty_bin(state->new_demands[partition]))
                    continue;
                _flush_backlog_now(state, partition);
        }
        adm_log_forced_backlog_flush(&state->stat);
}

/**
 * Increase the backlog from src to dst
 */
void pim_add_backlog(struct pim_state *state, uint16_t src, uint16_t dst,
                     uint32_t amount) {
        if (backlog_increase(&state->backlog, src, dst, amount,
                             &state->stat) == false)
                return; /* no need to enqueue */

        /* add to state->new_demands for the src partition
         * leave the 'metric' unused */
        uint16_t partition_index = PARTITION_OF(src);
        enqueue_bin(state->new_demands[partition_index], src, dst, 0);

        if (bin_size(state->new_demands[partition_index]) == SMALL_BIN_SIZE) {
                adm_log_backlog_flush_bin_full(&state->stat);
                _flush_backlog_now(state, partition_index);
        }
}

/**
 * Reset state of all flows for which src is the sender
 */
void pim_reset_sender(struct pim_state *state, uint16_t src) {
	/* TODO: implement this */
}

/**
 * Move a bin worth of demands from 'q_new_demands' to requests_by_src
 */
static inline
void process_incoming_bin(struct pim_state *state, uint16_t partition_index,
                          struct bin *bin) {
        uint32_t i;
        for (i = 0; i < bin_size(bin); i++) {
                /* add the edge to requests for this partition */
                struct backlog_edge *edge = bin_get(bin, i);
                ga_adj_add_edge_by_src(&state->requests_by_src[partition_index],
                                       PARTITION_IDX(edge->src), edge->dst);
        }
}

/**
 * Move new demands from 'q_new_demands' to requests_by_src
 */
static inline
void process_new_requests(struct pim_state *state, uint16_t partition_index) {
        struct bin *bins[RING_DEQUEUE_BURST_SIZE];
        int n, i;
        uint32_t num_entries = 0;
        uint32_t num_bins = 0;

        n = fp_ring_dequeue_burst(state->q_new_demands[partition_index],
                                  (void **) &bins[0], RING_DEQUEUE_BURST_SIZE);
        for (i = 0; i < n; i++) {
                num_entries += bin_size(bins[i]);
                num_bins++;

                process_incoming_bin(state, partition_index, bins[i]);
                fp_mempool_put(state->bin_mempool, bins[i]);
        }
        adm_log_processed_new_requests(&state->cores[partition_index].stat,
				       num_bins, num_entries);
}

/**
 * Prepare data structures so they are ready to allocate the next timeslot
 */
void pim_prepare(struct pim_state *state, uint16_t partition_index) {
         /* add new backlogs to requests */
        process_new_requests(state, partition_index);

        /* reset accepts */
        ga_partd_edgelist_src_reset(&state->accepts, partition_index);

        /* reset src and dst endnodes */
        uint32_t start_word = PIM_BITMASK_WORD(first_in_partition(partition_index));
        uint32_t words_per_partition = PIM_BITMASK_WORD(PARTITION_N_NODES);
        memset(((uint8_t *) &state->src_endnodes) + start_word, 0, words_per_partition);
        memset(((uint8_t *) &state->dst_endnodes) + start_word, 0, words_per_partition);
}

/**
 * For all source (left-hand) nodes in partition 'partition_index',
 *    selects edges to grant. These are added to 'grants'.
 */
void pim_do_grant(struct pim_state *state, uint16_t partition_index) {
        struct admission_core_statistics *core_stat = &state->cores[partition_index].stat;

        /* wait until all partitions have finished the previous phase */
        phase_barrier_wait(&state->phase, partition_index, core_stat);

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
        struct admission_core_statistics *core_stat = &state->cores[partition_index].stat;

        /* wait until all partitions have finished the previous phase */
        phase_barrier_wait(&state->phase, partition_index, core_stat);

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
        struct admission_core_statistics *core_stat = &state->cores[partition_index].stat;
        uint16_t dst_partition;

        /* wait until all partitions have finished the previous phase */
        phase_barrier_wait(&state->phase, partition_index, core_stat);

        /* get memory for admitted traffic, init it */
        struct admitted_traffic *admitted;
        while (fp_mempool_get(state->admitted_traffic_mempool, (void**) &admitted) != 0)
		adm_log_admitted_traffic_alloc_failed(core_stat);
        init_admitted_traffic(admitted);
        set_admitted_partition(admitted, partition_index);

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
                        if (backlog != 0) {
                                /* there is remaining backlog */
                                adm_log_allocated_backlog_remaining(core_stat, edge->src,
                                                                    edge->dst, backlog);
				continue;
			}

                        /* no more backlog, delete the edge from requests */
                        adm_log_allocator_no_backlog(core_stat, edge->src, edge->dst);
                        uint16_t grant_adj_index = state->grant_adj_index[edge->src];
                        ga_adj_delete_neigh(&state->requests_by_src[PARTITION_OF(edge->src)],
                                            PARTITION_IDX(edge->src), grant_adj_index);
                }
        }

        /* send out the admitted traffic */
        while (fp_ring_enqueue(state->q_admitted_out, admitted) != 0)
                adm_log_wait_for_space_in_q_admitted_traffic(core_stat);
}

/*
 * pim.c
 *
 *  Created on: May 2, 2014
 *      Author: aousterh
 */

#include "pim.h"

#include <assert.h>

#include "phase.h"
#include "ga_random.h"

#define MAX_TRIES 10
#define RING_DEQUEUE_BURST_SIZE		8

/**
 * Return true if the src is already allocated, false otherwise.
 */
static inline
bool src_is_allocated(struct pim_core_state *core, uint16_t src) {
        uint16_t src_index = PARTITION_IDX(src);
        return ((core->src_endnodes[PIM_BITMASK_WORD(src_index)] >>
                 PIM_BITMASK_SHIFT(src_index)) & 0x1);
}

/**
 * Return true if the dst is already allocated, false otherwise.
 */
static inline
bool dst_is_allocated(struct pim_state *state, uint16_t dst) {
        /* this function may be called by a different core than
         * the core of dst */
        struct pim_core_state *core = &state->cores[PARTITION_OF(dst)];

        uint16_t dst_index = PARTITION_IDX(dst);
        return ((core->dst_endnodes[PIM_BITMASK_WORD(dst_index)] >>
                 PIM_BITMASK_SHIFT(dst_index)) & 0x1);
}

/**
 * Mark the src as allocated.
 */
static inline
void mark_src_allocated(struct pim_core_state *core, uint16_t src) {
        uint16_t src_index = PARTITION_IDX(src);
        core->src_endnodes[PIM_BITMASK_WORD(src_index)] |=
                (0x1 << PIM_BITMASK_SHIFT(src_index));
}

/**
 * Mark the dst as allocated.
 */
static inline
void mark_dst_allocated(struct pim_core_state *core, uint16_t dst) {
        uint16_t dst_index = PARTITION_IDX(dst);
        core->dst_endnodes[PIM_BITMASK_WORD(dst_index)]
                |= (0x1 << PIM_BITMASK_SHIFT(dst_index));
}

/**
 * Flushes the bin for a specific partition to state and allocates a new bin
 */
void _flush_backlog_now(struct pim_state *state, uint16_t partition_index) {
        struct pim_core_state *core = &state->cores[partition_index];

        /* enqueue state->new_demands[partition_index] */
        while (fp_ring_enqueue(core->q_new_demands,
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
 * Move demands from 'bin' to requests_by_src
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
        struct pim_core_state *core = &state->cores[partition_index];
        struct bin *bins[RING_DEQUEUE_BURST_SIZE];
        int n, i;
        uint32_t num_entries = 0;
        uint32_t num_bins = 0;

        n = fp_ring_dequeue_burst(core->q_new_demands,
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
        struct pim_core_state *core = &state->cores[partition_index];
        struct admission_core_statistics *core_stat = &core->stat;

        /* add new backlogs to requests */
        process_new_requests(state, partition_index);

        /* reset src and dst endnodes */
        memset(&core->src_endnodes, 0, PIM_BITMASK_WORD(PARTITION_N_NODES));
        memset(&core->dst_endnodes, 0, PIM_BITMASK_WORD(PARTITION_N_NODES));

        /* get memory for admitted traffic, init it */
        while (fp_mempool_get(state->admitted_traffic_mempool, (void**) &core->admitted) != 0)
		adm_log_admitted_traffic_alloc_failed(core_stat);
        init_admitted_traffic(core->admitted);
        set_admitted_partition(core->admitted, partition_index);
}

/**
 * For all source (left-hand) nodes in partition 'partition_index',
 *    selects edges to grant. These are added to 'grants'. For the
 *    first iteration only.
 */
void pim_do_grant_first_it(struct pim_state *state, uint16_t partition_index) {
        uint16_t count, src_partition, dst_adj_index, dst;
        struct pim_core_state *core = &state->cores[partition_index];
        struct admission_core_statistics *core_stat = &core->stat;

        /* reset grant edgelist */
        ga_partd_edgelist_src_reset(&state->grants, partition_index);

        /* for each src in the partition, randomly choose a dst to grant to */
        uint16_t src;
        for (src = first_in_partition(partition_index);
             src <= last_in_partition(partition_index);
             src++) {
                uint16_t src_index = PARTITION_IDX(src);
                uint16_t degree = state->requests_by_src[partition_index].degree[src_index];
                if (degree == 0)
                        continue; /* no requests for this src */

                /* pick a random destination to grant to */
                dst_adj_index = ga_rand(&core->rand_state, degree);
                dst = state->requests_by_src[partition_index].neigh[src_index][dst_adj_index];

                /* add the granted edge */
                ga_partd_edgelist_add(&state->grants, src, dst);

                /* record the index of the destination we granted to */
                core->grant_adj_index[PARTITION_IDX(src)] = dst_adj_index;
        }
}

/**
 * For all source (left-hand) nodes in partition 'partition_index',
 *    selects edges to grant. These are added to 'grants'.
 */
void pim_do_grant(struct pim_state *state, uint16_t partition_index) {
        uint16_t count, src_partition;
        struct pim_core_state *core = &state->cores[partition_index];
        struct admission_core_statistics *core_stat = &core->stat;

#ifndef SINGLE_CORE
        /* indicate that this partition finished its phase */
        phase_finished(&state->phase, partition_index, core_stat);
#endif

        /* reset grant edgelist */
        ga_partd_edgelist_src_reset(&state->grants, partition_index);

        /* wait until all partitions have finished the previous phase */
        /* process new requests while waiting */
#ifndef SINGLE_CORE
        count = 0;
        while (count < N_PARTITIONS - 1) {
                src_partition = phase_get_finished_partition(&state->phase, partition_index,
                                                             core_stat);
                if (src_partition != NONE_READY)
                        count++;
                else
                        process_new_requests(state, partition_index);
        }
#endif

        /* for each src in the partition, randomly choose a dst to grant to */
        uint16_t src;
        for (src = first_in_partition(partition_index);
             src <= last_in_partition(partition_index);
             src++) {
                if (src_is_allocated(core, src))
                        continue; /* this src has been allocated in this timeslot */

                uint16_t src_index = PARTITION_IDX(src);
                uint16_t degree = state->requests_by_src[partition_index].degree[src_index];
                if (degree == 0)
                        continue; /* no requests for this src */

                /* find an un-allocated destination to grant to */
                uint8_t tries = MAX_TRIES;
                uint16_t dst_adj_index, dst;
                bool dst_is_alloc;
                do {
                        dst_adj_index = ga_rand(&core->rand_state, degree);
                        dst = state->requests_by_src[partition_index].neigh[src_index][dst_adj_index];
                        dst_is_alloc = dst_is_allocated(state, dst);
                } while (dst_is_alloc && (--tries > 0));

                if (dst_is_alloc)
                        continue; /* couldn't find a free dst*/

                /* add the granted edge */
                ga_partd_edgelist_add(&state->grants, src, dst);

                /* record the index of the destination we granted to */
                core->grant_adj_index[PARTITION_IDX(src)] = dst_adj_index;
        }
}

/**
 * For every destination (right-hand) node in partition 'partition_index',
 *    select among its granted edges which edge to accept. These edges are
 *    added to 'accepts'
 */
void pim_do_accept(struct pim_state *state, uint16_t partition_index) {
        uint16_t count, src_partition;
        struct ga_edgelist *edgelist;
	struct pim_core_state *core = &state->cores[partition_index];
        struct admission_core_statistics *core_stat = &core->stat;

#ifndef SINGLE_CORE
        /* indicate that this partition finished its phase */
        phase_finished(&state->phase, partition_index, core_stat);
#endif

        /* reset grant adjacency list */
        ga_reset_adj(&state->grants_by_dst[partition_index]);

        /* sort grants from all src partitions by destination node */
        struct ga_adj *dest_adj = &state->grants_by_dst[partition_index];

#ifndef SINGLE_CORE
        /* sort grants from this partition first */
        edgelist = &state->grants.dst[partition_index].src[partition_index];
        ga_edges_to_adj_by_dst(&edgelist->edge[0], edgelist->n, dest_adj);

        /* sort grants from other partitions, as they are ready */
        count = 0;
        while (count < N_PARTITIONS - 1) {
                src_partition = phase_get_finished_partition(&state->phase, partition_index,
                                                             core_stat);
                if (src_partition != NONE_READY) {
                        count++;
                        edgelist = &state->grants.dst[partition_index].src[src_partition];
                        ga_edges_to_adj_by_dst(&edgelist->edge[0], edgelist->n, dest_adj);
                } else
                        process_new_requests(state, partition_index);
        }
#else
        for (src_partition = 0; src_partition < N_PARTITIONS; src_partition++) {
                edgelist = &state->grants.dst[partition_index].src[src_partition];
                ga_edges_to_adj_by_dst(&edgelist->edge[0], edgelist->n, dest_adj);
        }
#endif

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
                uint16_t src_adj_index = ga_rand(&core->rand_state, degree);
                uint16_t src = state->grants_by_dst[partition_index].neigh[dst_index][src_adj_index];
                ga_partd_edgelist_add(&state->accepts, src, dst);

                /* mark the dst as allocated for this timeslot */
                mark_dst_allocated(core, dst);
        }
}

/* Process accepts involving one source and one destination partition */
static inline
void process_accepts_from_partition(struct pim_state *state, uint16_t src_partition,
                                    uint16_t dst_partition) {
	struct pim_core_state *core = &state->cores[src_partition];
        struct admission_core_statistics *core_stat = &core->stat;
        struct ga_edgelist *edgelist;
        uint16_t i;
        uint32_t backlog;

        struct admitted_traffic *admitted = core->admitted;
        edgelist = &state->accepts.dst[dst_partition].src[src_partition];

        for (i = 0; i < edgelist->n; i++) {
                struct ga_edge *edge = &edgelist->edge[i];

                /* add edge to admitted traffic */
                insert_admitted_edge(admitted, edge->src, edge->dst);

                /* mark the src as allocated for this timeslot */
                mark_src_allocated(core, edge->src);

                /* decrease the backlog */
                assert(backlog_get(&state->backlog, edge->src, edge->dst) != 0);
                backlog = backlog_decrease(&state->backlog, edge->src, edge->dst);
                if (backlog != 0) {
                        /* there is remaining backlog */
                        adm_log_allocated_backlog_remaining(core_stat, edge->src,
                                                            edge->dst, backlog);
                        continue;
                }

                 /* no more backlog, delete the edge from requests */
                 adm_log_allocator_no_backlog(core_stat, edge->src, edge->dst);
                 uint16_t grant_adj_index = core->grant_adj_index[PARTITION_IDX(edge->src)];
                 ga_adj_delete_neigh(&state->requests_by_src[PARTITION_OF(edge->src)],
                                     PARTITION_IDX(edge->src), grant_adj_index);
        }
}

/**
 * Process all of the accepts, after each iteration
 */
void pim_process_accepts(struct pim_state *state, uint16_t partition_index) {
	struct pim_core_state *core = &state->cores[partition_index];
        struct admission_core_statistics *core_stat = &core->stat;
        uint16_t dst_partition, count;

#ifndef SINGLE_CORE
        /* indicate that this partition finished its phase */
        phase_finished(&state->phase, partition_index, core_stat);
#endif

        /* iterate through all accepted edges */
#ifndef SINGLE_CORE
        /* process accepts from this partition first */
        process_accepts_from_partition(state, partition_index, partition_index);
        
        /* process accepts from other partitions, as they are ready */
        count = 0;
        while (count < N_PARTITIONS - 1) {
                dst_partition = phase_get_finished_partition(&state->phase, partition_index,
                                                             core_stat);
                if (dst_partition != NONE_READY) {
                        count++;
                        process_accepts_from_partition(state, partition_index,
                                                       dst_partition);
                } else
                        process_new_requests(state, partition_index);
        }
#else
        for (dst_partition = 0; dst_partition < N_PARTITIONS; dst_partition++) {
                process_accepts_from_partition(state, partition_index, dst_partition);
        }
#endif

        /* reset accepts */
        ga_partd_edgelist_src_reset(&state->accepts, partition_index);
}

/**
 * Clean-up after a timeslot is done being allocated
 */
void pim_complete_timeslot(struct pim_state *state, uint16_t partition_index) {
	struct pim_core_state *core = &state->cores[partition_index];
        struct admission_core_statistics *core_stat = &core->stat;

        /* send out the admitted traffic */
        while (fp_ring_enqueue(state->q_admitted_out, core->admitted) != 0)
                adm_log_wait_for_space_in_q_admitted_traffic(core_stat);
}

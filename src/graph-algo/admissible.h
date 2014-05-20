/*
 * admissible.h
 *
 *  Created on: May 13, 2014
 *      Author: aousterh
 */

#ifndef ADMISSIBLE_H_
#define ADMISSIBLE_H_

#include "algo_config.h"

/* dummy struct definition */
struct admissible_state;

/* parallel algo, e.g. pim */
#ifdef PARALLEL_ALGO
#include "../grant-accept/partitioning.h"
#include "../grant-accept/pim.h"
#include "../grant-accept/pim_admissible_traffic.h"

#define NUM_BINS_SHIFT 0
#define NUM_BINS 1
#define LARGE_BIN_SIZE 0 /* unused */
#define BATCH_SIZE 1
#define BATCH_SHIFT 0
#define ADMITTED_PER_BATCH N_PARTITIONS
#define NUM_BIN_RINGS N_PARTITIONS
#define BIN_RING_SHIFT 16

static inline
void add_backlog(struct admissible_state *state, uint16_t src,
                 uint16_t dst, uint32_t amount) {
        pim_add_backlog((struct pim_state *) state, src, dst, amount);
}

static inline
void flush_backlog(struct admissible_state *state) {
        pim_flush_backlog((struct pim_state *) state);
};

static inline
void get_admissible_traffic(struct admissible_state *state, uint32_t a,
                            uint64_t b, uint32_t c, uint32_t d) {
        pim_get_admissible_traffic((struct pim_state *) state);
}

static inline
struct admissible_state *
create_admissible_state(bool a, uint16_t b, uint16_t c, uint16_t d,
                        struct fp_ring *e, struct fp_ring *q_admitted_out,
                        struct fp_mempool *bin_mempool,
                        struct fp_mempool *admitted_traffic_mempool,
                        struct fp_ring **f, struct fp_ring **q_new_demands)
{
        struct pim_state *state = pim_create_state(q_new_demands, q_admitted_out,
                                                   bin_mempool,
                                                   admitted_traffic_mempool);
        return (struct admissible_state *) state;
}

static inline
void reset_admissible_state(struct admissible_state *state, bool a, uint16_t b,
                            uint16_t c, uint16_t d)
{
        pim_reset_state((struct pim_state *) state);
}

static inline
void reset_sender(struct admissible_state *status, uint16_t src)
{
	pim_reset_sender((struct pim_state *) status, src);
}

static inline
struct fp_ring *get_q_admitted_out(struct admissible_state *state)
{
        struct pim_state *pim_state = (struct pim_state *) state;
        return pim_state->q_admitted_out;
}

static inline
struct fp_mempool *get_admitted_traffic_mempool(struct admissible_state *state)
{
        struct pim_state *pim_state = (struct pim_state *) state;
        return pim_state->admitted_traffic_mempool;
}
#endif

/* pipelined algo */
#ifdef PIPELINED_ALGO
#include "admissible_structures.h"
#include "admissible_traffic.h"
#include "batch.h"

#define ADMITTED_PER_BATCH BATCH_SIZE
#define NUM_BIN_RINGS 0
#define BIN_RING_SHIFT 0

static inline
void add_backlog(struct admissible_state *status, uint16_t src,
                 uint16_t dst, uint32_t amount) {
        seq_add_backlog((struct seq_admissible_status *) status, src, dst, amount);
}

static inline
void flush_backlog(struct admissible_state *status) {
        seq_flush_backlog((struct seq_admissible_status *) status);
}

static inline
void get_admissible_traffic(struct admissible_state *status,
                            uint32_t core_index, uint64_t first_timeslot,
                            uint32_t tslot_mul, uint32_t tslot_shift) {
        seq_get_admissible_traffic((struct seq_admissible_status *) status, core_index,
                                   first_timeslot, tslot_mul, tslot_shift);
}

static inline
struct admissible_state *
create_admissible_state(bool oversubscribed, uint16_t inter_rack_capacity,
                        uint16_t out_of_boundary_capacity, uint16_t num_nodes,
                        struct fp_ring *q_head, struct fp_ring *q_admitted_out,
                        struct fp_mempool *head_bin_mempool,
                        struct fp_mempool *admitted_traffic_mempool,
                        struct fp_ring **q_bin, struct fp_ring **a)
{
        struct seq_admissible_status *status;
        status = seq_create_admissible_status(oversubscribed, inter_rack_capacity,
                                              out_of_boundary_capacity, num_nodes, q_head,
                                              q_admitted_out, head_bin_mempool,
                                              admitted_traffic_mempool, q_bin);
        return (struct admissible_state *) status;
}

static inline
void reset_admissible_state(struct admissible_state *status,
                            bool oversubscribed, uint16_t inter_rack_capacity,
                            uint16_t out_of_boundary_capacity, uint16_t num_nodes)
{
        seq_reset_admissible_status((struct seq_admissible_status *) status, oversubscribed,
                                    inter_rack_capacity, out_of_boundary_capacity, num_nodes);
}

static inline
void reset_sender(struct admissible_state *status, uint16_t src)
{
	seq_reset_sender((struct seq_admissible_status *) status, src);
}

static inline
struct fp_ring *get_q_admitted_out(struct admissible_state *state)
{
        struct seq_admissible_status *status = (struct seq_admissible_status *) state;
        return status->q_admitted_out;
}

static inline
struct fp_mempool *get_admitted_traffic_mempool(struct admissible_state *state)
{
        struct seq_admissible_status *status = (struct seq_admissible_status *) state;
        return status->admitted_traffic_mempool;
}
#endif

#endif /* ADMISSIBLE_H_ */

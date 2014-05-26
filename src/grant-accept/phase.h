/*
 * phase.h
 *
 *  Created on: May 22, 2014
 *      Author: aousterh
 */

#ifndef PHASE_H_
#define PHASE_H_

#include "partitioning.h"
#include "../graph-algo/admissible_algo_log.h"
#include "../graph-algo/fp_ring.h"

#define NONE_READY     N_PARTITIONS

/**
 *  Tracks what phase a partition is in and which
 *  partitions are ready to be processed by it
 */
struct partition_phase_state {
        struct fp_ring *q_ready;
        uint16_t phase;
} __attribute__((aligned(64)));

/**
 *  Tracks phase state for each partition
 */
struct phase_state {
        struct partition_phase_state partitions[N_PARTITIONS];
};

static inline
uint64_t create_queue_entry(uint16_t phase, uint16_t partition_index) {
        return (((uint64_t) phase) << 16) | partition_index;
}

static inline
uint16_t get_phase_from_queue_entry(uint64_t queue_entry) {
        return queue_entry >> 16;
}

static inline
uint16_t get_partition_from_queue_entry(uint64_t queue_entry) {
        return queue_entry & 0xFFFF;
}

/**
 * Initialize a phase_state structure
 */
static inline
void phase_state_init(struct phase_state *phase,
                     struct fp_ring **q_ready) {
        uint16_t i;
        for (i = 0; i < N_PARTITIONS; i++) {
                phase->partitions[i].q_ready = q_ready[i];
                phase->partitions[i].phase = 0;
        }
}

/**
 * Mark this partition as done with the current phase
 */
static inline
void phase_finished(struct phase_state *phase_state,
                    uint16_t partition_index,
                    struct admission_core_statistics *stat) {
        adm_log_phase_finished(stat);

        uint16_t phase = ++phase_state->partitions[partition_index].phase;
        uint64_t queue_entry = create_queue_entry(phase, partition_index);

        /* for each other partition, enqueue an entry */
        uint16_t i;
        for (i = 0; i < N_PARTITIONS; i++) {
                if (i == partition_index)
                        continue;

                fp_ring_enqueue(phase_state->partitions[i].q_ready,
                                (void *) queue_entry);
        }
}

/**
 * Returns another partition that has finished the previous phase
 * or NONE_READY if none are done
 */
static inline
uint16_t phase_get_finished_partition(struct phase_state *phase_state,
                                      uint16_t partition_index,
                                      struct admission_core_statistics *stat) {
        uint64_t queue_entry;
        int ret = fp_ring_dequeue(phase_state->partitions[partition_index].q_ready,
                                  (void *) &queue_entry);

        if (ret == -ENOENT) {
                adm_log_phase_none_ready(stat);
                return NONE_READY;
        }

        uint16_t entry_phase = get_phase_from_queue_entry(queue_entry);
        uint16_t my_phase = phase_state->partitions[partition_index].phase;
        if (entry_phase != my_phase) {
                /* this phase is out of order.
                   re-enqueue the entry and return NONE_READY */
                fp_ring_enqueue(phase_state->partitions[partition_index].q_ready,
                                (void *) queue_entry);
                adm_log_phase_out_of_order(stat);
                return NONE_READY;
        }

        return get_partition_from_queue_entry(queue_entry);
}

/**
 * Temporary simplification that doesn't allow any useful work while
 * waiting for other partitions to finish
 */
static inline
void phase_barrier_wait(struct phase_state *phase_state, uint16_t partition_index,
                        struct admission_core_statistics *stat) {
        /* indicate that this partition finished its phase */
        phase_finished(phase_state, partition_index, stat);

        /* wait for all other partitions to finish their phases too */
        uint16_t count = 0;
        while (count < N_PARTITIONS - 1) {
                if (phase_get_finished_partition(phase_state, partition_index,
                                                 stat) != NONE_READY)
                        count++;
                else
                        adm_log_phase_none_ready_in_barrier(stat);
        }
}

#endif /* PHASE_H_ */

/*
 * admission_core.h
 *
 *  Created on: May 18, 2014
 *      Author: aousterh
 */

#ifndef ADMISSION_CORE_H
#define ADMISSION_CORE_H

#include <rte_ring.h>
#include "seq_admission_core.h"
#include "../graph-algo/admissible.h"

#define		NUM_NODES					256

#define		ALLOWED_TIMESLOT_LAG		4

#define		ADMITTED_TRAFFIC_MEMPOOL_SIZE	(BATCH_SIZE * 16 * N_ADMISSION_CORES)
#define		ADMITTED_TRAFFIC_CACHE_SIZE		(2 * BATCH_SIZE)

#define		BIN_MEMPOOL_CACHE_SIZE			(16 * NUM_BINS - 1)
#define		BIN_MEMPOOL_SIZE				(1024 + 32 * NUM_BINS * N_ADMISSION_CORES)

/* admitted_traffic pool */
extern struct rte_mempool* admitted_traffic_pool[NB_SOCKETS];

/* Specifications for controller thread */
struct admission_core_cmd {
	uint64_t start_time;
	uint64_t end_time;

	uint64_t start_timeslot;

	uint32_t admission_core_index; /* the index among admission cores of this one */
};

static inline
void admission_init_global(struct rte_ring *q_admitted_out) {
	seq_admission_init_global(q_admitted_out);
}

static inline
void admission_init_core(uint16_t lcore_id) {
	seq_admission_init_core(lcore_id);
}

static inline
int exec_admission_core(void *void_cmd_p) {
	exec_seq_admission_core(void_cmd_p);
}

static inline
struct admissible_state *g_admissible_status(void) {
	return (struct admissible_state *) &g_seq_admissible_status;
}

static inline
struct backlog *g_admission_backlog(void) {
	return &g_seq_admissible_status.backlog;
}

static inline
struct admission_core_statistics *g_admission_core_stats(uint16_t i) {
	return &g_seq_admissible_status.cores[i].stat;
}

static inline
struct admission_statistics *g_admission_stats(void) {
	return &g_seq_admissible_status.stat;
}

#endif /* ADMISSION_CORE_H */

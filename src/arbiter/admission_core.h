
#ifndef ADMISSION_CORE_H
#define ADMISSION_CORE_H

#include <stdint.h>
#include <rte_ring.h>
#include "main.h"
#include "control.h"
#include "../graph-algo/admissible_structures.h"

#define		OVERSUBSCRIBED				0
#define		INTER_RACK_CAPACITY			4
#define		OUT_OF_BOUNDARY_CAPACITY	16
#define		NUM_NODES					256

#define		ALLOWED_TIMESLOT_LAG		4

#define		ADMITTED_TRAFFIC_MEMPOOL_SIZE	(BATCH_SIZE * 16 * N_ADMISSION_CORES)
#define		ADMITTED_TRAFFIC_CACHE_SIZE		(2 * BATCH_SIZE)

#define		HEAD_BIN_MEMPOOL_CACHE_SIZE			64
#define		HEAD_BIN_MEMPOOL_SIZE				(1024 + 2 * HEAD_BIN_MEMPOOL_CACHE_SIZE * N_ADMISSION_CORES)
#define		CORE_BIN_MEMPOOL_SIZE				(32 * NUM_BINS - 1)
#define		CORE_BIN_MEMPOOL_CACHE_SIZE			(16 * NUM_BINS - 1)

#define		Q_BIN_RING_SIZE						(32 * NUM_BINS)
#define		Q_HEAD_RING_SIZE		(64 * 1024)
#define		URGENT_RING_SIZE		(64 * 1024)


/* admissible status */
extern struct admissible_status g_admissible_status;

/* admitted_traffic pool */
extern struct rte_mempool* admitted_traffic_pool[NB_SOCKETS];

/* Specifications for controller thread */
struct admission_core_cmd {
	uint64_t start_time;
	uint64_t end_time;

	uint64_t start_timeslot;

	uint32_t admission_core_index; /* the index among admission cores of this one */
};

void admission_init_global(struct rte_ring *q_admitted_out);

/**
 * Initializes a single core to be a comm core
 */
void admission_init_core(uint16_t lcore_id);

/**
 * Runs the admission core
 */
int exec_admission_core(void *void_cmd_p);


#endif /* ADMISSION_CORE_H */

/*
 * admission_core_common.h
 *
 *  Created on: May 19, 2014
 *      Author: aousterh
 */

#ifndef ADMISSION_CORE_COMMON_H
#define ADMISSION_CORE_COMMON_H

#include <rte_ring.h>

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

#endif /* ADMISSION_CORE_COMMON_H */

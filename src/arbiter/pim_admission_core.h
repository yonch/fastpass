/*
 * pim_admission_core.h
 *
 *  Created on: May 18, 2014
 *      Author: aousterh
 */

#ifndef PIM_ADMISSION_CORE_H
#define PIM_ADMISSION_CORE_H

#include <rte_ring.h>

#define		ADMITTED_TRAFFIC_MEMPOOL_SIZE		(16 * N_ADMISSION_CORES)
#define		ADMITTED_TRAFFIC_CACHE_SIZE		8

#define		BIN_MEMPOOL_SIZE			(16 * N_ADMISSION_CORES)
#define		BIN_MEMPOOL_CACHE_SIZE			8

#define		Q_NEW_DEMANDS_RING_SIZE      (64 * 1024)

/* pim state */
extern struct pim_state g_pim_state;

void pim_admission_init_global(struct rte_ring *q_admitted_out);

/**
 * Initializes a single core to be a pim admission core
 */
void pim_admission_init_core(uint16_t lcore_id);

/**
 * Runs the admission core
 */
int exec_pim_admission_core(void *void_cmd_p);


#endif /* PIM_ADMISSION_CORE_H */

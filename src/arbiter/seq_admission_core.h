
#ifndef SEQ_ADMISSION_CORE_H
#define SEQ_ADMISSION_CORE_H

#include <stdint.h>
#include <rte_ring.h>
#include "main.h"
#include "control.h"
#include "../graph-algo/admissible.h"

#define		OVERSUBSCRIBED				0
#define		INTER_RACK_CAPACITY			4
#define		OUT_OF_BOUNDARY_CAPACITY	16

#define		Q_BIN_RING_SIZE						(32 * NUM_BINS)
#define		Q_HEAD_RING_SIZE		(64 * 1024)
#define		URGENT_RING_SIZE		(64 * 1024)

/* admissible status */
struct seq_admissible_status g_seq_admissible_status;

void seq_admission_init_global(struct rte_ring *q_admitted_out);

/**
 * Initializes a single core to be a comm core
 */
void seq_admission_init_core(uint16_t lcore_id);

/**
 * Runs the admission core
 */
int exec_seq_admission_core(void *void_cmd_p);


#endif /* SEQ_ADMISSION_CORE_H */

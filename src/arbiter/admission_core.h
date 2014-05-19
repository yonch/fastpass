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

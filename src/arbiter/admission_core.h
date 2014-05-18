/*
 * admission_core.h
 *
 *  Created on: May 18, 2014
 *      Author: aousterh
 */

#ifndef ADMISSION_CORE_H
#define ADMISSION_CORE_H

#include "seq_admission_core.h"
#include "../graph-algo/admissible.h"

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

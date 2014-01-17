
#ifndef CONTROLLER_H_
#define CONTROLLER_H_

#include <stdint.h>
#include <rte_ip.h>
#include "../protocol/fpproto.h"
#include "../protocol/stat_print.h"
#include "../graph-algo/admissible_structures.h"
#include "fp_timer.h"
#include "main.h"

#define CONTROLLER_SEND_TIMEOUT_SECS 	0.0004

/* The maximum number of admitted time-slots to process in a batch before
 *   sending and receiving packets */
#define MAX_ADMITTED_PER_LOOP		8

/* maximum number of paths possible */
#define MAX_PATHS					4

#define NODE_MAX_PKTS_PER_SEC		100000
/* maximum burst of egress packets to a single node (must be >1, can be fraction) */
#define NODE_MAX_BURST				1.5
/* minimum time between packets */
#define NODE_MIN_TRIGGER_GAP_SEC	1e-6

/**
 * Specifications for controller thread
 * @comm_core_index: the index of the core among the comm cores
 */
struct comm_core_cmd {
	uint64_t start_time;
	uint64_t end_time;

	uint64_t tslot_len; /**< Length of a time slot */
	uint32_t tslot_offset; /**< How many offsets in the future the controller allocates */

	struct rte_ring *q_allocated;
};

/*
 * Per-comm-core state
 * @alloc_enc_space: space used to encode ALLOCs, set to zeros when not inside
 *    the ALLOC code.
 */
struct comm_core_state {
	uint8_t alloc_enc_space[MAX_NODES * MAX_PATHS];
	uint64_t latest_timeslot;

	struct fp_timers timeout_timers;
	struct fp_timers tx_timers;
};
extern struct comm_core_state ccore_state[RTE_MAX_LCORE];

static inline uint32_t controller_ip(void)
{
	return IPv4(10,197,55,111);
}

/**
 * Initializes global data used by comm cores
 */
void comm_init_global_structs(uint64_t first_time_slot);

/**
 * Initializes a single core to be a comm core
 */
void comm_init_core(uint16_t lcore_id, uint64_t first_time_slot);

void exec_comm_core(struct comm_core_cmd * cmd);

void benchmark_cost_of_get_time(void);

void comm_dump_stat(uint16_t node_id, struct conn_log_struct *conn_log);

#endif /* CONTROLLER_H_ */

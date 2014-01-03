
#ifndef CONTROLLER_H_
#define CONTROLLER_H_

#include <stdint.h>

#define CONTROLLER_SEND_TIMEOUT_NS (4*1000*1000)

/* The maximum number of admitted time-slots to process in a batch before
 *   sending and receiving packets */
#define MAX_ADMITTED_PER_LOOP		8

/* maximum number of paths possible */
#define MAX_PATHS					4

/**
 * Specifications for controller thread
 * @comm_core_index: the index of the core among the comm cores
 */
struct comm_core_cmd {
	uint64_t start_time;
	uint64_t end_time;

	uint64_t tslot_len; /**< Length of a time slot */
	uint32_t tslot_offset; /**< How many offsets in the future the controller allocates */

	uint8_t comm_core_index;
	struct rte_ring *q_admitted;
};

/**
 * Initializes global data used by comm cores
 */
void comm_init_global_structs(uint64_t first_time_slot);

/**
 * Initializes a single core to be a comm core
 */
void comm_init_core(uint16_t lcore_id);

void exec_comm_core(struct comm_core_cmd * cmd);

void benchmark_cost_of_get_time(void);

#endif /* CONTROLLER_H_ */


#ifndef CONTROLLER_H_
#define CONTROLLER_H_

#include <stdint.h>

/* Specifications for controller thread */
struct comm_core_cmd {
	uint64_t start_time;
	uint64_t end_time;

	uint64_t tslot_len; /**< Length of a time slot */
	uint32_t tslot_offset; /**< How many offsets in the future the controller allocates */
};

void exec_comm_core(struct comm_core_cmd * cmd);


#endif /* CONTROLLER_H_ */

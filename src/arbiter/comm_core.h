
#ifndef CONTROLLER_H_
#define CONTROLLER_H_

struct expt_cmd;

#include <stdint.h>

/* Specifications for controller thread */
struct expt_controller {
	uint64_t start_time;
	uint64_t end_time;
	int tor_n_machines;
	int agg_n_ports;
	int core_n_ports;
	int alloc_n_paths;
	uint64_t tslot_len; /**< Length of a time slot */
	uint32_t tslot_offset; /**< How many offsets in the future the controller allocates */
};

void exec_controller(struct expt_controller * cmd);


#endif /* CONTROLLER_H_ */

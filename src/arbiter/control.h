/**
 * Experiment control
 */

#ifndef CONTROL_H_
#define CONTROL_H_

#include <stdint.h>

struct expt_cmd;

//#define N_CONTROLLER_PORTS (n_enabled_port / 2)
#define N_CONTROLLER_PORTS 1

#define CPU_BENCHMARK_EXPT 0x1
#define NET_BENCHMARK_EXPT 0x2
#define END_TO_END_EXPT 0x4

#define EXPT_RUN_MASK (CPU_BENCHMARK_EXPT)


/**
 * Allocate queues to lcores
 */
int control_do_queue_allocation(void);

/**
 * Performs experiments
 */
void control_do_experiments(void);


#endif /* CONTROL_H_ */

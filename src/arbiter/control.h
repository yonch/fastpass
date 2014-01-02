/**
 * Experiment control
 */

#ifndef CONTROL_H_
#define CONTROL_H_

#include <stdint.h>

/* bits 1-3 occupied by other experiments */
#define LAUNCH_CONTROLLER_EXPT 0x8

#define EXPT_RUN_MASK (LAUNCH_CONTROLLER_EXPT)

#define N_CONTROLLER_PORTS		1
#define N_ADMISSION_CORES		1
#define N_COMM_CORES			1
#define N_LOG_CORES				0

/**
 * Allocate queues to lcores
 */
int control_do_queue_allocation(void);

/**
 * Performs experiments
 */
void launch_cores(void);


#endif /* CONTROL_H_ */

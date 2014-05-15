/*
 * Defines relating to the algorithm being used
 */

#ifndef ALGO_CONFIG_H_
#define ALGO_CONFIG_H_

#ifndef ALGO_N_CORES
#define ALGO_N_CORES				4
#endif

#define tslot_alloc_add_backlog		lru_alloc_add_backlog

#ifndef PARALLEL_ALGO
/* pipelined algo */
#define PIPELINED_ALGO
#endif

#endif /* ALGO_CONFIG_H_ */

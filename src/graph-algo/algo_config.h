/*
 * Defines relating to the algorithm being used
 */

#ifndef ALGO_CONFIG_H_
#define ALGO_CONFIG_H_

#define tslot_alloc_add_backlog		lru_alloc_add_backlog

#ifdef PARALLEL_ALGO
/* parallel algo */

#ifndef ALGO_N_CORES
#define ALGO_N_CORES				N_PARTITIONS
#endif

#else
/* pipelined algo */
#define PIPELINED_ALGO

#ifndef ALGO_N_CORES
#define ALGO_N_CORES				4
#endif

#endif

#endif /* ALGO_CONFIG_H_ */

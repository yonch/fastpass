/*
 * Defines relating to the algorithm being used
 */

#ifndef ALGO_CONFIG_H_
#define ALGO_CONFIG_H_

#define tslot_alloc_add_backlog		lru_alloc_add_backlog

#if (defined(PARALLEL_ALGO) && defined(PIPELINED_ALGO))
#error "Both PARALLEL_ALGO and PIPELINED_ALGO are defined"
#endif
#if !(defined(PARALLEL_ALGO) || defined(PIPELINED_ALGO))
#error "Neither PARALLEL_ALGO or PIPELINED_ALGO is defined"
#endif

#ifdef PARALLEL_ALGO
/* parallel algo */
#ifndef ALGO_N_CORES
#define ALGO_N_CORES				1 /* for now */
#endif

#endif

#ifdef PIPELINED_ALGO
/* pipelined algo */

#ifndef ALGO_N_CORES
#define ALGO_N_CORES				4
#endif

#endif

#endif /* ALGO_CONFIG_H_ */

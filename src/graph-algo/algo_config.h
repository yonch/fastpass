/*
 * Defines relating to the algorithm being used
 */

#ifndef ALGO_CONFIG_H_
#define ALGO_CONFIG_H_

#ifndef ALGO_N_CORES
#define ALGO_N_CORES				4
#endif

#define tslot_alloc_add_backlog		lru_alloc_add_backlog

#define BATCH_SIZE 16  // must be consistent with bitmaps in batch_state
#define BATCH_SHIFT 4  // 2^BATCH_SHIFT = BATCH_SIZE

#endif /* ALGO_CONFIG_H_ */

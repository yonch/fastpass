/*
 * backlog.h
 *
 *  Created on: Apr 28, 2014
 *      Author: yonch
 */

#ifndef BACKLOG_H_
#define BACKLOG_H_

#include "atomic.h"
#include "admissible_algo_log.h"
#include "../protocol/topology.h"

#include <assert.h>

/**
 * Keeps backlogs between every source and destination
 *    n: the backlog for each pair
 */
struct backlog {
	atomic32_t n[MAX_NODES * MAX_NODES];
};

static void backlog_init(struct backlog *backlog) {
	int i;
    for (i = 0; i < MAX_NODES * MAX_NODES; i++)
        atomic32_init(&backlog->n[i]);
}

// Internal. Get the index of this flow in the status data structure
static inline __attribute__((always_inline))
uint32_t _backlog_index(uint16_t src, uint16_t dst) {
    return (src << FP_NODES_SHIFT) + dst;
}

static inline __attribute__((always_inline))
uint32_t backlog_get(struct backlog *backlog, uint16_t src, uint16_t dst) {
	uint32_t index = _backlog_index(src, dst);
	return atomic32_read(&backlog->n[index]);
}

/**
 * Decreases backlog for the (src,dst) pair by 1
 */
static inline __attribute__((always_inline))
uint32_t backlog_decrease(struct backlog *backlog, uint16_t src, uint16_t dst) {
	uint32_t index = _backlog_index(src, dst);
	return atomic32_sub_return(&backlog->n[index], 1);
}

// Resets the flow for this src/dst pair
static inline
void backlog_reset_pair(struct backlog *backlog, uint16_t src,
		uint16_t dst)
{
    assert(backlog != NULL);

    atomic32_t *pair_backlog_ptr = &backlog->n[_backlog_index(src, dst)];

    int32_t n = atomic32_read(pair_backlog_ptr);

    if (n != 0) {
        /*
         * There is pending backlog. We want to reduce the backlog, but want to
         *    keep the invariant that a flow is in a bin iff backlog != 0.
         *
         * This invariant can be broken if the allocator races to eliminate the
         *    backlog completely while we are executing this code. So we test for
         *    the race.
         */
    	if (atomic32_sub_return(pair_backlog_ptr, n + 1) == -(n+1))
    		/* race happened, (backlog was 0 before sub) */
    		atomic32_clear(pair_backlog_ptr);
    	else
    		/* now backlog is <=-1, it's so large that a race is unlikely */
    		atomic32_set(pair_backlog_ptr, 1);
    }

    /* if backlog was 0, nothing to be done */
}

/**
 * Increases backlog for (src,dst) by 'amount'.
 * @return true if backlog was 0 before the increase, false o/w
 * @param backlog: the backlog struct
 * @param src: source endpoint
 * @param dst: destination endpoint
 * @param amount: the amount by which to increase the backlog
 * @param stat: statistics object, to keep aggregate stats on the increase
 */
static inline
bool backlog_increase(struct backlog *backlog, uint16_t src,
        uint16_t dst, uint32_t amount, struct admission_statistics *stat)
{
	int32_t new_amount;
    assert(backlog != NULL);

    // Get full quantity from 16-bit LSB
    uint32_t index = _backlog_index(src, dst);

    new_amount = atomic32_add_return(&backlog->n[index], amount);
	if (new_amount == amount) {
		adm_log_increased_backlog_to_queue(stat, amount, new_amount);
		return true;
	}

	adm_log_increased_backlog_atomically(stat, amount, new_amount);
	return false;
}

#endif /* BACKLOG_H_ */

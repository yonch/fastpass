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
#include "bitasm.h"

#include <assert.h>

/**
 * Keeps backlogs between every source and destination
 *    n: the backlog for each pair
 */
struct backlog {
	uint32_t n[MAX_NODES * MAX_NODES];
	uint64_t is_active[(MAX_NODES * MAX_NODES + 63) / 64];
};

static void backlog_init(struct backlog *backlog) {
	int i;
	memset(backlog->n, 0 , sizeof(backlog->n));
	memset(backlog->is_active, 0, sizeof(backlog->is_active));
}

// Internal. Get the index of this flow in the status data structure
static inline __attribute__((always_inline))
uint32_t _backlog_index(uint16_t src, uint16_t dst) {
    return (src << FP_NODES_SHIFT) + dst;
}

static inline __attribute__((always_inline))
uint32_t backlog_get(struct backlog *backlog, uint16_t src, uint16_t dst) {
	uint32_t index = _backlog_index(src, dst);
	return backlog->n[index];
}

/**
 * Decreases backlog for the (src,dst) pair by 1
 */
static inline __attribute__((always_inline))
void backlog_non_active(struct backlog *backlog, uint16_t src, uint16_t dst) {
	uint32_t index = _backlog_index(src, dst);
	arr_unset_bit(backlog->is_active, index);
}

// Resets the flow for this src/dst pair
static inline
void backlog_reset_pair(struct backlog *backlog, uint16_t src,
		uint16_t dst)
{
    assert(backlog != NULL);

    backlog->n[_backlog_index(src, dst)] = 0;
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
    assert(backlog != NULL);
    assert(amount != 0);

    // Get full quantity from 16-bit LSB
    uint32_t index = _backlog_index(src, dst);

    set_and_jmp_if_was_set(backlog->is_active, index, already_active);

    assert(backlog->n[index] == 0);
    adm_log_increased_backlog_to_queue(stat, amount, amount);
	return true;

already_active:
	/* the new backlog will get enqueued as the current one is spent */
	backlog->n[index] += amount;
	adm_log_increased_backlog_atomically(stat, amount, backlog->n[index]);
	return false;
}

#endif /* BACKLOG_H_ */

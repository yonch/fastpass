/*
 * admissible_traffic.h
 *
 *  Created on: November 19, 2013
 *      Author: aousterh
 */

#ifndef ADMISSIBLE_TRAFFIC_H_
#define ADMISSIBLE_TRAFFIC_H_

#include "admissible_structures.h"
#include "platform.h"

#include <inttypes.h>

// Increase the backlog from src to dst
void add_backlog(struct admissible_status *status,
                       uint16_t src, uint16_t dst,
                       uint32_t amount);

// Flushes the backlog into admissible_status
void flush_backlog(struct admissible_status *status);

// Determine admissible traffic for one timeslot from queue_in
void get_admissible_traffic(struct admissible_status *status,
								uint32_t core_index,
								uint64_t first_timeslot, uint32_t tslot_mul,
								uint32_t tslot_shift);

// Reset state of all flows for which src is the sender
void reset_sender(struct admissible_status *status, uint16_t src);

/**
 * Returns the bin index a flow last allocated at timeslot @last_allocated
 *   should fit in, when allocating a batch that starts with @current_timeslot
 */
static inline __attribute__((always_inline))
uint16_t bin_index_from_timeslot(uint32_t last_allocated,
	uint64_t current_timeslot)
{
	uint32_t gap = (uint32_t)(current_timeslot + BATCH_SIZE) - (uint32_t)last_allocated;

	if (gap <= NUM_BINS)
		return NUM_BINS + BATCH_SIZE - gap;

	/**
	 * The flow should fit into one of the bins that were folded from multiple
	 *    input bins. There are 32 that originate from exactly 64 bins (2-to-1),
	 *    then 16 bins that originate from 64 bins (4-to-1), then 8 from 64,
	 *    4 from 64, 2 from 64, 1 from 64, and then a bin for _all_ older flows.
	 *
	 * We call these groups: group 0 of the first 64 bins folded into 32,
	 *   group 1 of 64 bins folded into 16 bins etc.
	 *
	 * The offset from the bin (@current_timeslot - NUM_BINS + BATCH_SIZE) of
	 *   all but the last group consists of #group 1's, then a 0, then the
	 *   offset from the groups beginning shifted (1 + #groups) bits right (to
	 *   get the (#groups+1) - to - 1 folding).
	 */
	gap -= NUM_BINS + 1;
	uint16_t group_ind = (gap >> BATCH_SHIFT);
	uint16_t bin_gap = ((~0UL << (BATCH_SHIFT+1)) | (gap & (BATCH_SIZE-1)))
									>> (1 + group_ind);

	/* if the gap is very large, fold into the last group */
	uint16_t is_large_gap = (group_ind >= BATCH_SHIFT);
	bin_gap |= (1 << 15) - is_large_gap;

	return BATCH_SIZE - 1 - (bin_gap & (BATCH_SIZE-1));
}

static inline __attribute__((always_inline))
uint32_t new_metric_after_alloc(uint16_t src, uint16_t dst, uint32_t old_metric,
		uint16_t batch_timeslot,
		struct admission_core_state *core, struct admissible_status *status)
{
	return status->current_timeslot + batch_timeslot;
}

static inline __attribute__((always_inline))
uint32_t bin_after_alloc(uint16_t src, uint16_t dst, uint32_t metric,
		uint16_t batch_timeslot,
		struct admission_core_state *core, struct admissible_status *status)
{
	return NUM_BINS + batch_timeslot;
}

// Helper method for testing in Python. Dequeues and returns an admitted traffic struct.
static inline
struct admitted_traffic *dequeue_admitted_traffic(struct admissible_status *status) {
    assert(status != NULL);

    struct admitted_traffic *traffic;
    fp_ring_dequeue(status->q_admitted_out, (void **)&traffic);

    return traffic;
}

#endif /* ADMISSIBLE_TRAFFIC_H_ */

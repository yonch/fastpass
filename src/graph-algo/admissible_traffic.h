/*
 * admissible_traffic.h
 *
 *  Created on: November 19, 2013
 *      Author: aousterh
 */

#ifndef ADMISSIBLE_TRAFFIC_H_
#define ADMISSIBLE_TRAFFIC_H_

#include "admissible_structures.h"

#include <inttypes.h>

#define URGENT_Q_HEAD_TOKEN		(~0UL)


// Increase the backlog from src to dst
void add_backlog(struct admissible_status *status,
                       uint16_t src, uint16_t dst,
                       uint16_t demand_tslots);

// Determine admissible traffic for one timeslot from queue_in
void get_admissible_traffic(struct admission_core_state *core,
								struct admissible_status *status,
								struct admitted_traffic **admitted,
								uint64_t first_timeslot, uint32_t tslot_mul,
								uint32_t tslot_shift);

// Reset state of all flows for which src is the sender
void reset_sender(struct admissible_status *status, uint16_t src);

/**
 * Returns the bin index a flow last allocated at timeslot @last_allocated
 *   should fit in, when allocating a batch that starts with @current_timeslot
 */
static inline
uint16_t bin_index_from_timeslot(uint64_t last_allocated,
	uint64_t current_timeslot)
{
	int64_t gap = (int64_t)(current_timeslot + BATCH_SIZE) - (int64_t)last_allocated;

	assert(gap > 0);

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
	uint16_t bin_gap = ((~0UL << (BATCH_SHIFT+1)) | (gap & (BATCH_SIZE-1)))
									>> (1 + (gap >> BATCH_SHIFT));

	/* if the gap is very large, fold into the last group */
	uint16_t is_large_gap = ((gap >> BATCH_SHIFT) >= BATCH_SHIFT);
	bin_gap |= (1 << 15) - is_large_gap;

	return BATCH_SIZE - 1 - (bin_gap & (BATCH_SIZE-1));
}

#ifndef likely
#define likely(x)  __builtin_expect((x),1)
#endif /* likely */

#ifndef unlikely
#define unlikely(x)  __builtin_expect((x),0)
#endif /* unlikely */

#endif /* ADMISSIBLE_TRAFFIC_H_ */

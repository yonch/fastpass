/*
 * admissible_traffic_sjf.h
 *
 *  Created on: January 14, 2013
 *      Author: aousterh
 */

#ifndef ADMISSIBLE_TRAFFIC_SJF_H_
#define ADMISSIBLE_TRAFFIC_SJF_H_

#include "admissible_structures_sjf.h"

#include <inttypes.h>

#define URGENT_Q_HEAD_TOKEN		(~0UL)

/**
 * Returns true if @out_edge should be enqueued to q_head
 */
bool add_backlog_no_enqueue(struct admissible_status *status, uint16_t src,
                            uint16_t dst, uint16_t backlog_increase, void **out_edge);

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
 * Returns the bin index a flow should go in.
 * Map flows with x MTUs remaining to bin x - 1. All large flows go into
 * the last bin.
 */
static inline
uint16_t bin_index_from_src_dst(struct admissible_status *status, uint16_t src, uint16_t dst) {
    assert(src < MAX_SRCS);
    assert(dst < MAX_DSTS);

    uint32_t index = get_status_index(src, dst);
    uint32_t backlog = atomic32_read(&status->flows[index].backlog);
    assert(backlog > 0);

    if (backlog <= NUM_BINS)
        return backlog - 1;
    else
        return NUM_BINS - 1;
}

#ifndef likely
#define likely(x)  __builtin_expect((x),1)
#endif /* likely */

#ifndef unlikely
#define unlikely(x)  __builtin_expect((x),0)
#endif /* unlikely */

// Helper method for testing in Python. Enqueues the head token.
static inline
void enqueue_head_token(struct fp_ring *ring) {
    assert(ring != NULL);

    fp_ring_enqueue(ring, (void *) URGENT_Q_HEAD_TOKEN);
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

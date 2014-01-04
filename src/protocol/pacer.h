/**
 * A module to pace to a given rate.
 *
 * When event is triggered, the next allowable time is computed.
 * Features:
 *   - Limit rate
 *   - Burst size will allow some credit to be accumulated if no events are
 *      triggered.
 *   - Minimum delay from trigger, will allow the event after at least that
 *      delay.
 */
#ifndef FP_PACER_H_
#define FP_PACER_H_

#include "platform.h"

#define PACER_NO_NEXT_EVENT		(~0UL)

struct fp_pacer {
	u64		T;
	u64		next_event;
	u32		cost;					/* cost, in tokens, of a request */
	u32		max_credit;				/* the max number of tokens to burst */
	u32		min_gap;				/* min delay between requests */
};

/**
 * Initializes pacer with a full bucket
 * @now: the current time, to make sure bucket is full
 * @cost: the cost (in time) of a request
 * @max_credit: the amount of time that can accumulate to burst
 * @min_gap: the minimum amount of time from a trigger until an event is allowed
 */
static inline
void pacer_init_full(struct fp_pacer *pa, u64 now, u32 cost, u32 max_credit,
		u32 min_gap)
{
	pa->T = now - max_credit;	/* start with full bucket */
	pa->next_event = PACER_NO_NEXT_EVENT;
	pa->cost = cost;
	pa->max_credit = max_credit;
	pa->min_gap = min_gap;
}

/**
 * @return true if the event is pending, false otherwise
 */
static inline bool pacer_is_triggered(struct fp_pacer *pa)
{
	return (pa->next_event != PACER_NO_NEXT_EVENT);
}

/**
 * Trigger an event.
 * @return true if a new event was triggered, false if the event was already
 *   pending
 */
static inline bool pacer_trigger(struct fp_pacer *pa, u64 now)
{
	if (pacer_is_triggered(pa))
		return false;

	pa->next_event = max_t(u64, pa->T + pa->cost, now + pa->min_gap);
	return true;
}

/**
 * @return when the next event should occur, or PACER_NO_NEXT_EVENT if event
 *    is not triggered
 */
static inline u64 pacer_next_event(struct fp_pacer *pa)
{
	return pa->next_event;
}

/**
 * Notifies that the triggered event happened.
 * @note assumes the event was triggered since the last reset/init!
 */
static inline void pacer_reset(struct fp_pacer *pa)
{
	/* update request credits */
	pa->T = max_t(u64, pa->T, pa->next_event - pa->max_credit) + pa->cost;
	pa->next_event = PACER_NO_NEXT_EVENT;
}


#endif /* FP_PACER_H_ */


#ifndef FP_TIMER_H_
#define FP_TIMER_H_

#include <ccan/list/list.h>

#define MAX_TIMER_SLOTS		1024
#define TIMER_GRANULARITY	(16*1024)
#define TIMER_NOT_SET_TIME	(~0UL)

struct fp_timers {
	uint64_t head; /* this is already divided by TIMER_GRANULARITY */
	struct list_head slots[MAX_TIMER_SLOTS];
};

struct fp_timer {
	/* member of timers->slots */
	struct list_node node;

	/* time of this timer, already divided by TIMER_GRANULARITY */
	uint64_t time;
};

/**
 * Initializes the timer repository
 * @now: the time to initialize to
 */
static inline
void fp_init_timers(struct fp_timers *timers, uint64_t now)
{
	int i;

	for (i = 0; i < MAX_TIMER_SLOTS; i++)
		list_head_init(&timers->slots[i]);

	now /= TIMER_GRANULARITY;
	timers->head = now;
}

/**
 * Initializes timer as idle in the timer pool
 */
static inline
void fp_init_timer(struct fp_timer *tim)
{
	tim->time = TIMER_NOT_SET_TIME;
}


/**
 * Enqueues timer at @when
 */
static inline void fp_timer_reset(struct fp_timers *timers,
		struct fp_timer *tim, uint64_t when)
{
	uint32_t slot;

	/* adjust granularity */
	when /= TIMER_GRANULARITY;

	/* choose which slot the timer goes into */
	if (unlikely(when <= timers->head))
		slot = timers->head % MAX_TIMER_SLOTS;
	else
		slot = when % MAX_TIMER_SLOTS;

	/* if timer was already active on another list, remove it */
	if (tim->time != TIMER_NOT_SET_TIME)
		list_del(&tim->node);

	/* set timer */
	tim->time = when;
	list_add_tail(&timers->slots[slot], &tim->node);
}

/**
 * Removes the timer given timer
 */
static inline
void fp_timer_stop(struct fp_timer *tim)
{
	if(tim->time == TIMER_NOT_SET_TIME)
		return;

	tim->time = TIMER_NOT_SET_TIME;
	list_del(&tim->node);
}


/**
 * removes all timers that expire before @now, and adds them to the tail of @l
 */
static inline
void fp_timer_get_expired(struct fp_timers *timers, uint64_t now,
		struct list_head *l)
{
	struct fp_timer *tim, *next;
	uint32_t slot;

	now /= TIMER_GRANULARITY;

	assert(now - timers->head < 100 * MAX_TIMER_SLOTS); /* sanity check */

	while ((int64_t)now - (int64_t)timers->head >= 0) {
		/* process slot relating to timers->head */
		slot = timers->head % MAX_TIMER_SLOTS;
		list_for_each_safe(&timers->slots[slot], tim, next, node) {
			/* for each timer in the slot */
			if (tim->time <= now) {
				/* it expired, move it to l (removing okay because of _safe) */
				list_del_from(&timers->slots[slot], &tim->node);
				tim->time = TIMER_NOT_SET_TIME;
				list_add_tail(l, &tim->node);
			}
		}

		/* advance timers->head */
		timers->head++;
	}

	/* although we processed the slot relating to @now, we want to set
	 * @head = @now, so timer_get_expired several times within the same timeslot
	 * gets newly reset timers within that same timeslot */
	timers->head = now;
}

#endif /* FP_TIMER_H_ */

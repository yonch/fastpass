/*
 * Platform-supplied API
 */

#ifndef PLATFORM_H_
#define PLATFORM_H_

/*
 * Cancels the timer.
 * Returns 0 if timer was canceled or already completed
 * 			-1 if the timer was running concurrently and could not be canceled
 */
int cancel_timer(struct fpproto_conn *proto);

/**
 * Sets timer to the given time, in nanoseconds
 */
void set_timer(struct fpproto_conn *proto, u64 when);

/* returns the current real time (the time that is used to determine timeslots) */
static inline u64 fp_get_time_ns(void)
{
	return ktime_to_ns(ktime_get_real());
}

#endif /* PLATFORM_H_ */

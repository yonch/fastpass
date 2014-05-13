#ifndef SCH_TIMESLOT_H_
#define SCH_TIMESLOT_H_

struct timeslot_ops {
	void		(* new_qdisc)(struct Qdisc *);
	void		(* stop_qdisc)(struct Qdisc *);
	void		(* add_timeslot)(u64 src_dst_key);
};

/**
 * Registers the timeslot queuing discipline
 */
void timeslot_register_qdisc(struct timeslot_ops *ops);

/**
 * Schedules a timeslot for flow
 */
void timeslot_schedule(struct Qdisc *qdisc, u64 src_dst_key, u64 timeslot);

/**
 * Admits a timeslot from a flow right now
 */
void timeslot_admit_now(struct Qdisc *qdisc, u64 src_dst_key);

/**
 * Garbage-collects information for empty queues.
 */
void timeslot_garbage_collect(struct Qdisc *qdisc);


#endif /* SCH_TIMESLOT_H_ */

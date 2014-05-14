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
int timeslot_register_qdisc(struct timeslot_ops *ops);

/**
 * Unregisters the timeslot queuing discipline
 */
void timeslot_unregister_qdisc(void);

/**
 * Schedules a timeslot for flow
 */
void timeslot_schedule(struct Qdisc *sch, u64 src_dst_key, u64 timeslot);

/**
 * Admits a timeslot from a flow right now
 */
void timeslot_admit_now(struct Qdisc *sch, u64 src_dst_key);

/**
 * Garbage-collects information for empty queues.
 */
void timeslot_garbage_collect(struct Qdisc *sch);


#endif /* SCH_TIMESLOT_H_ */

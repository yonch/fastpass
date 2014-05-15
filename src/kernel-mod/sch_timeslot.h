#ifndef SCH_TIMESLOT_H_
#define SCH_TIMESLOT_H_

struct timeslot_ops {
	char			id[IFNAMSIZ];
	int			priv_size;
	int			(* new_qdisc)(void *);
	void		(* stop_qdisc)(void *);
	void		(* add_timeslot)(u64 src_dst_key);
};

struct timeslot_reg {
	struct timeslot_ops *ops;
	struct Qdisc_ops qdisc_ops;
};

/**
 * Initializes global state
 */
int timeslot_init(void);

/**
 * Cleans up global state
 */
void timeslot_exit(void);

/**
 * Registers the timeslot queuing discipline
 */
struct timeslot_reg *timeslot_register_qdisc(struct timeslot_ops *ops);

/**
 * Unregisters the timeslot queuing discipline
 */
void timeslot_unregister_qdisc(struct timeslot_reg *reg);

/**
 * Schedules a timeslot for flow
 */
void timeslot_schedule(void *priv, u64 src_dst_key, u64 timeslot);

/**
 * Admits a timeslot from a flow right now
 */
void timeslot_admit_now(void *priv, u64 src_dst_key);

/**
 * Garbage-collects information for empty queues.
 */
void timeslot_garbage_collect(void *priv);


#endif /* SCH_TIMESLOT_H_ */

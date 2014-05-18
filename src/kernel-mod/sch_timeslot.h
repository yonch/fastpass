#ifndef SCH_TIMESLOT_H_
#define SCH_TIMESLOT_H_

#include <linux/types.h>
#include <net/sch_generic.h>

struct tsq_ops {
	char			id[IFNAMSIZ];
	int			priv_size;
	int			(* new_qdisc)(void *priv, struct net *qdisc_net, u32 tslot_mul,
								u32 tslot_shift);
	void		(* stop_qdisc)(void *priv);
	void		(* add_timeslot)(void *priv, u64 src_dst_key);
};

struct tsq_qdisc_entry {
	struct tsq_ops *ops;
	struct Qdisc_ops qdisc_ops;
};

/**
 * Initializes global state
 */
int tsq_init(void);

/**
 * Cleans up global state
 */
void tsq_exit(void);

/**
 * Registers the timeslot queuing discipline
 */
struct tsq_qdisc_entry *tsq_register_qdisc(struct tsq_ops *ops);

/**
 * Unregisters the timeslot queuing discipline
 */
void tsq_unregister_qdisc(struct tsq_qdisc_entry *reg);

/**
 * Schedules a timeslot for flow
 */
void tsq_schedule(void *priv, u64 src_dst_key, u64 timeslot);

/**
 * Admits a timeslot from a flow right now
 */
void tsq_admit_now(void *priv, u64 src_dst_key);

/**
 * Garbage-collects information for empty queues.
 */
void tsq_garbage_collect(void *priv);


#endif /* SCH_TIMESLOT_H_ */

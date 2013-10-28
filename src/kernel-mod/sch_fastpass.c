/*
 * net/sched/sch_fastpass.c FastPass client
 *
 *  Copyright (C) 2013 Eric Dumazet <edumazet@google.com> (sch_fq.c)
 *  Copyright (C) 2013 Jonathan Perry <yonch@yonch.com>
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/jiffies.h>
#include <linux/string.h>
#include <linux/in.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/skbuff.h>
#include <linux/slab.h>
#include <linux/rbtree.h>
#include <linux/hash.h>
#include <linux/prefetch.h>
#include <linux/time.h>
#include <linux/bitops.h>
#include <net/netlink.h>
#include <net/pkt_sched.h>
#include <net/sock.h>
#include <net/tcp_states.h>
#include <net/sch_generic.h>

/*
 * FastPass client qdisc
 *
 * Invariants:
 *  - If a flow has unreq_tslots > 0, then it is linked to q->unreq_flows,
 *      otherwise flow->next is NULL.
 *
 *  	An exception is if flow->next == &do_not_schedule (which is used for
 *  	q->internal), then it is not linked to q->unreq_flows.
 *
 */

#define FASTPASS_HORIZON		64
#define FASTPASS_TSLOT_NSEC		13000
#define FASTPASS_HZ				(NSEC_PER_SEC / FASTPASS_TSLOT_NSEC)

/* limit number of collected flows per round */
#define FP_GC_MAX 8
#define FP_GC_AGE (3*FASTPASS_HZ)

/*
 * Per flow structure, dynamically allocated
 */
struct fp_flow {
	struct sock	*sk;		/* flow identifier */
	u32		socket_hash;	/* sk_hash, detect reuse of sk after free+alloc */

	struct rb_node	fp_node; 	/* anchor in fp_root[] trees */
	struct fp_flow	*next;		/* singly linked pointer into q->unreq_flows */

	/* queued buffers: */
	struct sk_buff	*head;		/* list of skbs for this flow : first skb */
	struct sk_buff	*sch_tail;	/* last skb that had been scheduled */
	struct sk_buff *tail;		/* last skb in the list */
	int		unreq_tslots;		/* number of unscheduled timeslots */
	int		qlen;				/* number of packets in flow queue */

	s64		credit;				/* time remaining in the last scheduled timeslot */
	int		sch_tslots;			/* number of scheduled tslots that have not ended */
	u64		last_sch_tslot;		/* last slot that had been allocated to flow */
};

struct fp_flow_head {
	struct fp_flow *first;
	struct fp_flow *last;
};

struct fp_timeslot_horizon {
	u64 timeslot;
	u64 mask;
};

struct fp_sched_data {
	struct rb_root	*flow_hash_tbl;		/* table of rb-trees of flows */

	struct fp_flow_head unreq_flows; /* flows with unscheduled packets */

	struct fp_flow *schedule[FASTPASS_HORIZON];	/* flows scheduled in the next time slots: */
												/* slot x at [x % FASTPASS_HORIZON] */
	struct fp_timeslot_horizon	horizon;
	u64		tslot_start_time;

	u64		time_next_sch_req;
	u64		time_next_sch_packet;


	struct fp_flow	internal;	/* for non classified or high prio packets */
	u32		quantum;
	u32		initial_quantum;
	u32		flow_plimit;	/* max packets per flow */
	u8		fp_trees_log;
	struct psched_ratecfg rate;


	u32		flows;
	u32		inactive_flows;
	u32		n_unreq_flows;
	u32		unreq_tslots;

	u64		stat_gc_flows;
	u64		stat_internal_packets;
	u64		stat_tcp_retrans;
	u64		stat_throttled;
	u64		stat_flows_plimit;
	u64		stat_pkts_too_long;
	u64		stat_allocation_errors;
	u64		stat_missed_timeslots;
	struct qdisc_watchdog watchdog;
};

/* special value to mark a flow should not be scheduled */
static struct fq_flow do_not_schedule;

static struct kmem_cache *fp_flow_cachep __read_mostly;

/* advances the horizon 'num_tslots' into the future */
static void horizon_advance(struct fp_timeslot_horizon *h, u32 num_tslots) {
	if (unlikely(num_tslots >= 64))
		h->mask = 0;
	else
		h->mask >>= num_tslots;
	h->timeslot += num_tslots;
}

/* is the time slot 'x' slots into the future set? */
static bool horizon_is_set(struct fp_timeslot_horizon *h, u32 x) {
	return (x < 64) && ((h->mask >> x) & 0x1);
}

/* find the first time slot in the future (x>0) that is set, returns -1 if none */
static u32 horizon_future_ffs(struct fp_timeslot_horizon *h) {
	return __ffs64(h->mask & ~1ULL) - 1;
}

static void fp_flow_add_tail(struct fp_flow_head *head, struct fp_flow *flow)
{
	if (head->first)
		head->last->next = flow;
	else
		head->first = flow;
	head->last = flow;
	flow->next = NULL;
}

/**
 * Flow 'f' is now allowed to use time slot 'tslot'
 */
static void fp_schedule_tslot(struct fp_sched_data *q, u64 tslot,
		struct fp_flow *f);



static bool fp_gc_candidate(const struct fp_sched_data *q,
		const struct fp_flow *f)
{
	return f->qlen == 0 &&
	       time_after64(q->horizon.timeslot, f->last_sch_tslot + FP_GC_AGE);
}

static void fp_gc(struct fp_sched_data *q,
		  struct rb_root *root,
		  struct sock *sk)
{
	struct fp_flow *f, *tofree[FP_GC_MAX];
	struct rb_node **p, *parent;
	int fcnt = 0;

	p = &root->rb_node;
	parent = NULL;
	while (*p) {
		parent = *p;

		f = container_of(parent, struct fp_flow, fp_node);
		if (f->sk == sk)
			break;

		if (fp_gc_candidate(q, f)) {
			tofree[fcnt++] = f;
			if (fcnt == FP_GC_MAX)
				break;
		}

		if (f->sk > sk)
			p = &parent->rb_right;
		else
			p = &parent->rb_left;
	}

	q->flows -= fcnt;
	q->inactive_flows -= fcnt;
	q->stat_gc_flows += fcnt;
	while (fcnt) {
		struct fp_flow *f = tofree[--fcnt];

		rb_erase(&f->fp_node, root);
		kmem_cache_free(fp_flow_cachep, f);
	}
}

static const u8 prio2band[TC_PRIO_MAX + 1] = {
	1, 2, 2, 2, 1, 2, 0, 0 , 1, 1, 1, 1, 1, 1, 1, 1
};

static struct fp_flow *fp_classify(struct sk_buff *skb, struct fp_sched_data *q)
{
	struct rb_node **p, *parent;
	struct sock *sk = skb->sk;
	struct rb_root *root;
	struct fp_flow *f;
	int band;

	/* warning: no starvation prevention... */
	band = prio2band[skb->priority & TC_PRIO_MAX];
	if (unlikely(band == 0))
		return &q->internal;

	if (unlikely(!sk)) {
		/* By forcing low order bit to 1, we make sure to not
		 * collide with a local flow (socket pointers are word aligned)
		 */
		sk = (struct sock *)(skb_get_rxhash(skb) | 1L);
	}

	root = &q->flow_hash_tbl[hash_32((u32)(long)sk, q->fp_trees_log)];

	if (q->flows >= (2U << q->fp_trees_log) &&
	    q->inactive_flows > q->flows/2)
		fp_gc(q, root, sk);

	p = &root->rb_node;
	parent = NULL;
	while (*p) {
		parent = *p;

		f = container_of(parent, struct fp_flow, fp_node);
		if (f->sk == sk) {
			/* socket might have been reallocated, so check
			 * if its sk_hash is the same.
			 * It not, we need to refill credit with
			 * initial quantum
			 */
			if (unlikely(skb->sk &&
				     f->socket_hash != sk->sk_hash)) {
				f->credit = q->initial_quantum;
				f->socket_hash = sk->sk_hash;
			}
			return f;
		}
		if (f->sk > sk)
			p = &parent->rb_right;
		else
			p = &parent->rb_left;
	}

	f = kmem_cache_zalloc(fp_flow_cachep, GFP_ATOMIC | __GFP_NOWARN);
	if (unlikely(!f)) {
		q->stat_allocation_errors++;
		return &q->internal;
	}
	fp_flow_set_detached(f);
	f->sk = sk;
	if (skb->sk)
		f->socket_hash = sk->sk_hash;
	f->credit = q->initial_quantum;

	rb_link_node(&f->fp_node, parent, p);
	rb_insert_color(&f->fp_node, root);

	q->flows++;
	q->inactive_flows++;
	return f;
}


/* remove one skb from head of flow queue */
static struct sk_buff *flow_dequeue_skb(struct Qdisc *sch, struct fp_flow *flow)
{
	struct sk_buff *skb = flow->head;

	if (skb) {
		flow->head = skb->next;
		skb->next = NULL;
		flow->qlen--;
		sch->q.qlen--;
		sch->qstats.backlog -= qdisc_pkt_len(skb);
	}
	return skb;
}

static void flow_inc_unrequested(struct fp_sched_data *q,
		struct fp_flow *flow)
{
	if (flow->unreq_tslots == 0 && flow->next != &do_not_schedule) {
		/* flow not on scheduling queue yet, enqueue */
		BUG_ON(flow->next != NULL);
		fp_flow_add_tail(&q->unreq_flows, flow);
		q->n_unreq_flows++;
	}
	flow->unreq_tslots++;
	q->unreq_tslots++;
}

/* add skb to flow queue */
static void flow_enqueue_skb(struct Qdisc *sch, struct fp_flow *flow,
		struct sk_buff *skb)
{
	struct fp_sched_data *q = qdisc_priv(sch);
	struct sk_buff *head = flow->head;
	s64 cost = (s64) psched_l2t_ns(q->rate, qdisc_pkt_len(skb));

	skb->next = NULL;
	if (!head) {
		flow->head = skb;
		flow->tail = skb;
	} else {
		flow->tail->next = skb;
		flow->tail = skb;
	}

	/* if credit relates to an old slot, discard it */
	if (unlikely(flow->unreq_tslots == 0 && flow->last_sch_tslot <= q->horizon.timeslot))
		flow->credit = 0;

	/* check if need to request a new slot */
	if (cost > flow->credit) {
		flow_inc_unrequested(q, flow);
		flow->credit = FASTPASS_TSLOT_NSEC;
	}

	flow->credit -= cost;
	flow->qlen++;
	sch->q.qlen++;
	sch->qstats.backlog += qdisc_pkt_len(skb);
}

static int fp_enqueue(struct sk_buff *skb, struct Qdisc *sch)
{
	struct fp_sched_data *q = qdisc_priv(sch);
	struct fp_flow *f;

	/* enforce qdisc packet limit */
	if (unlikely(sch->q.qlen >= sch->limit))
		return qdisc_drop(skb, sch);

	f = fp_classify(skb, q);

	/* enforce flow packet limit */
	if (unlikely(f->qlen >= q->flow_plimit && f != &q->internal)) {
		q->stat_flows_plimit++;
		return qdisc_drop(skb, sch);
	}

	/* queue skb to flow, update statistics */
	flow_enqueue_skb(sch, f, skb);

	/* internal queue flows without scheduling */
	if (unlikely(f == &q->internal)) {
		q->stat_internal_packets++;
		qdisc_unthrottled(sch);
	}

	return NET_XMIT_SUCCESS;
}

/**
 * Move a timeslot's worth of skb's from src flow to dst flow, assuming the
 *    packets follow 'rate' rate.
 */
static void fp_move_timeslot(struct Qdisc *sch, struct psched_ratecfg *rate,
		struct fp_flow *src, struct fp_flow *dst)
{
	s64 credit = FASTPASS_TSLOT_NSEC;
	struct sk_buff *skb;

	/* while enough credit, move packets to q->internal */
	while (src->head) {
		credit -= (s64) psched_l2t_ns(rate, qdisc_pkt_len(f->head));
		if (credit < 0)
			return; /* ran out of credit */

		skb = flow_dequeue_skb(sch, src);
		flow_enqueue_skb(sch, dst, skb);
	}
}

static void fp_update_timeslot(struct Qdisc *sch, u64 now)
{
	struct fp_sched_data *q = qdisc_priv(sch);
	struct fp_flow *f;
	int next_slot;
	u64 next_slot_start;

	if(unlikely(time_before64(now, q->tslot_start_time + FASTPASS_TSLOT_NSEC)))
		return; /* still in the old time slot */

begin:
	next_slot = horizon_future_ffs(&q->horizon);
	next_slot_start = q->tslot_start_time + next_slot * FASTPASS_TSLOT_NSEC;

	/* is current slot an empty slot? */
	if (next_slot < 0 || time_before(now, next_slot_start)) {
		/* find the current timeslot's index */
		next_slot = now - q->tslot_start_time;
		do_div(next_slot, FASTPASS_TSLOT_NSEC);

		/* move to the new slot */
		q->tslot_start_time += next_slot * FASTPASS_TSLOT_NSEC;
		horizon_advance(&q->horizon, next_slot);
		return;
	}

	/* advance current time slot to next_slot */
	horizon_advance(&q->horizon, next_slot);
	q->tslot_start_time = next_slot_start;

	/* get the scheduled flow */
	f = q->schedule[q->horizon.timeslot & FASTPASS_HORIZON];
	BUG_ON(f == NULL);

	/* did we encounter a scheduled slot that is in the past */
	if (unlikely(time_after_eq64(now, q->tslot_start_time + FASTPASS_TSLOT_NSEC))) {
		flow_inc_unrequested(q, f); /* flow will need to re-request */
		q->stat_missed_timeslots++;
		goto begin;
	}

	fp_move_timeslot(sch, q->rate, f, q->internal);
}

static struct sk_buff *fp_dequeue(struct Qdisc *sch)
{
	struct fp_sched_data *q = qdisc_priv(sch);
	u64 now = ktime_to_ns(ktime_get());
	struct sk_buff *skb;

	/* any packets already queued? */
	skb = flow_dequeue_skb(sch, &q->internal);
	if (skb)
		goto out;

	/* internal queue is empty; update timeslot (may queue skbs in q->internal) */
	fp_update_timeslot(sch, now);

	/* if packets were queued for this timeslot, send them. */
	skb = flow_dequeue_skb(sch, &q->internal);
	if (skb)
		goto out;

	/* no packets in queue, go to sleep */
	if (q->time_next_delayed_flow != ~0ULL)
		qdisc_watchdog_schedule_ns(&q->watchdog,
								   q->time_next_delayed_flow);

out:
	qdisc_bstats_update(sch, skb);
	qdisc_unthrottled(sch);
	return skb;

}

static void fp_reset(struct Qdisc *sch)
{
	struct fp_sched_data *q = qdisc_priv(sch);
	struct rb_root *root;
	struct sk_buff *skb;
	struct rb_node *p;
	struct fp_flow *f;
	unsigned int idx;

	while ((skb = flow_dequeue_skb(sch, &q->internal)) != NULL)
		kfree_skb(skb);

	if (!q->flow_hash_tbl)
		return;

	for (idx = 0; idx < (1U << q->fp_trees_log); idx++) {
		root = &q->flow_hash_tbl[idx];
		while ((p = rb_first(root)) != NULL) {
			f = container_of(p, struct fp_flow, fp_node);
			rb_erase(p, root);

			while ((skb = flow_dequeue_skb(sch, f)) != NULL)
				kfree_skb(skb);

			kmem_cache_free(fp_flow_cachep, f);
		}
	}
	q->new_flows.first	= NULL;
	q->old_flows.first	= NULL;
	q->delayed		= RB_ROOT;
	q->flows		= 0;
	q->inactive_flows	= 0;
	q->throttled_flows	= 0;
}

static void fp_rehash(struct fp_sched_data *q,
		      struct rb_root *old_array, u32 old_log,
		      struct rb_root *new_array, u32 new_log)
{
	struct rb_node *op, **np, *parent;
	struct rb_root *oroot, *nroot;
	struct fp_flow *of, *nf;
	int fcnt = 0;
	u32 idx;

	for (idx = 0; idx < (1U << old_log); idx++) {
		oroot = &old_array[idx];
		while ((op = rb_first(oroot)) != NULL) {
			rb_erase(op, oroot);
			of = container_of(op, struct fp_flow, fp_node);
			if (fp_gc_candidate(q, of)) {
				fcnt++;
				kmem_cache_free(fp_flow_cachep, of);
				continue;
			}
			nroot = &new_array[hash_32((u32)(long)of->sk, new_log)];

			np = &nroot->rb_node;
			parent = NULL;
			while (*np) {
				parent = *np;

				nf = container_of(parent, struct fp_flow, fp_node);
				BUG_ON(nf->sk == of->sk);

				if (nf->sk > of->sk)
					np = &parent->rb_right;
				else
					np = &parent->rb_left;
			}

			rb_link_node(&of->fp_node, parent, np);
			rb_insert_color(&of->fp_node, nroot);
		}
	}
	q->flows -= fcnt;
	q->inactive_flows -= fcnt;
	q->stat_gc_flows += fcnt;
}

static int fp_resize(struct fp_sched_data *q, u32 log)
{
	struct rb_root *array;
	u32 idx;

	if (q->flow_hash_tbl && log == q->fp_trees_log)
		return 0;

	array = kmalloc(sizeof(struct rb_root) << log, GFP_KERNEL);
	if (!array)
		return -ENOMEM;

	for (idx = 0; idx < (1U << log); idx++)
		array[idx] = RB_ROOT;

	if (q->flow_hash_tbl) {
		fp_rehash(q, q->flow_hash_tbl, q->fp_trees_log, array, log);
		kfree(q->flow_hash_tbl);
	}
	q->flow_hash_tbl = array;
	q->fp_trees_log = log;

	return 0;
}

static const struct nla_policy fp_policy[TCA_FP_MAX + 1] = {
	[TCA_FP_PLIMIT]			= { .type = NLA_U32 },
	[TCA_FP_FLOW_PLIMIT]		= { .type = NLA_U32 },
	[TCA_FP_QUANTUM]		= { .type = NLA_U32 },
	[TCA_FP_INITIAL_QUANTUM]	= { .type = NLA_U32 },
	[TCA_FP_RATE_ENABLE]		= { .type = NLA_U32 },
	[TCA_FP_FLOW_DEFAULT_RATE]	= { .type = NLA_U32 },
	[TCA_FP_FLOW_MAX_RATE]		= { .type = NLA_U32 },
	[TCA_FP_BUCKETS_LOG]		= { .type = NLA_U32 },
};

static int fp_change(struct Qdisc *sch, struct nlattr *opt)
{
	struct fp_sched_data *q = qdisc_priv(sch);
	struct nlattr *tb[TCA_FP_MAX + 1];
	int err, drop_count = 0;
	u32 fp_log;

	if (!opt)
		return -EINVAL;

	err = nla_parse_nested(tb, TCA_FP_MAX, opt, fp_policy);
	if (err < 0)
		return err;

	sch_tree_lock(sch);

	fp_log = q->fp_trees_log;

	if (tb[TCA_FP_BUCKETS_LOG]) {
		u32 nval = nla_get_u32(tb[TCA_FP_BUCKETS_LOG]);

		if (nval >= 1 && nval <= ilog2(256*1024))
			fp_log = nval;
		else
			err = -EINVAL;
	}
	if (tb[TCA_FP_PLIMIT])
		sch->limit = nla_get_u32(tb[TCA_FP_PLIMIT]);

	if (tb[TCA_FP_FLOW_PLIMIT])
		q->flow_plimit = nla_get_u32(tb[TCA_FP_FLOW_PLIMIT]);

	if (tb[TCA_FP_QUANTUM])
		q->quantum = nla_get_u32(tb[TCA_FP_QUANTUM]);

	if (tb[TCA_FP_INITIAL_QUANTUM])
		q->quantum = nla_get_u32(tb[TCA_FP_INITIAL_QUANTUM]);

	if (tb[TCA_FP_FLOW_DEFAULT_RATE])
		q->flow_default_rate = nla_get_u32(tb[TCA_FP_FLOW_DEFAULT_RATE]);

	if (tb[TCA_FP_FLOW_MAX_RATE])
		q->flow_max_rate = nla_get_u32(tb[TCA_FP_FLOW_MAX_RATE]);

	if (tb[TCA_FP_RATE_ENABLE]) {
		u32 enable = nla_get_u32(tb[TCA_FP_RATE_ENABLE]);

		if (enable <= 1)
			q->rate_enable = enable;
		else
			err = -EINVAL;
	}

	if (!err)
		err = fp_resize(q, fp_log);

	while (sch->q.qlen > sch->limit) {
		struct sk_buff *skb = fp_dequeue(sch);

		if (!skb)
			break;
		kfree_skb(skb);
		drop_count++;
	}
	qdisc_tree_decrease_qlen(sch, drop_count);

	sch_tree_unlock(sch);
	return err;
}

static void fp_destroy(struct Qdisc *sch)
{
	struct fp_sched_data *q = qdisc_priv(sch);

	fp_reset(sch);
	kfree(q->flow_hash_tbl);
	qdisc_watchdog_cancel(&q->watchdog);
}

static int fp_init(struct Qdisc *sch, struct nlattr *opt)
{
	struct fp_sched_data *q = qdisc_priv(sch);
	int err;

	sch->limit		= 10000;
	q->flow_plimit		= 100;
	q->quantum		= 2 * psched_mtu(qdisc_dev(sch));
	q->initial_quantum	= 10 * psched_mtu(qdisc_dev(sch));
	q->flow_default_rate	= 0;
	q->flow_max_rate	= ~0U;
	q->rate_enable		= 1;
	q->new_flows.first	= NULL;
	q->old_flows.first	= NULL;
	q->delayed		= RB_ROOT;
	q->flow_hash_tbl		= NULL;
	q->fp_trees_log		= ilog2(1024);
	qdisc_watchdog_init(&q->watchdog, sch);

	if (opt)
		err = fp_change(sch, opt);
	else
		err = fp_resize(q, q->fp_trees_log);

	return err;
}

static int fp_dump(struct Qdisc *sch, struct sk_buff *skb)
{
	struct fp_sched_data *q = qdisc_priv(sch);
	struct nlattr *opts;

	opts = nla_nest_start(skb, TCA_OPTIONS);
	if (opts == NULL)
		goto nla_put_failure;

	if (nla_put_u32(skb, TCA_FP_PLIMIT, sch->limit) ||
	    nla_put_u32(skb, TCA_FP_FLOW_PLIMIT, q->flow_plimit) ||
	    nla_put_u32(skb, TCA_FP_QUANTUM, q->quantum) ||
	    nla_put_u32(skb, TCA_FP_INITIAL_QUANTUM, q->initial_quantum) ||
	    nla_put_u32(skb, TCA_FP_RATE_ENABLE, q->rate_enable) ||
	    nla_put_u32(skb, TCA_FP_FLOW_DEFAULT_RATE, q->flow_default_rate) ||
	    nla_put_u32(skb, TCA_FP_FLOW_MAX_RATE, q->flow_max_rate) ||
	    nla_put_u32(skb, TCA_FP_BUCKETS_LOG, q->fp_trees_log))
		goto nla_put_failure;

	nla_nest_end(skb, opts);
	return skb->len;

nla_put_failure:
	return -1;
}

static int fp_dump_stats(struct Qdisc *sch, struct gnet_dump *d)
{
	struct fp_sched_data *q = qdisc_priv(sch);
	u64 now = ktime_to_ns(ktime_get());
	struct tc_fastpass_qd_stats st = {
		.gc_flows		= q->stat_gc_flows,
		.highprio_packets	= q->stat_internal_packets,
		.tcp_retrans		= q->stat_tcp_retrans,
		.throttled		= q->stat_throttled,
		.flows_plimit		= q->stat_flows_plimit,
		.pkts_too_long		= q->stat_pkts_too_long,
		.allocation_errors	= q->stat_allocation_errors,
		.flows			= q->flows,
		.inactive_flows		= q->inactive_flows,
		.throttled_flows	= q->throttled_flows,
		.time_next_delayed_flow	= q->time_next_delayed_flow - now,
	};

	return gnet_stats_copy_app(d, &st, sizeof(st));
}

static struct Qdisc_ops fp_qdisc_ops __read_mostly = {
	.id		=	"fastpass",
	.priv_size	=	sizeof(struct fp_sched_data),

	.enqueue	=	fp_enqueue,
	.dequeue	=	fp_dequeue,
	.peek		=	qdisc_peek_dequeued,
	.init		=	fp_init,
	.reset		=	fp_reset,
	.destroy	=	fp_destroy,
	.change		=	fp_change,
	.dump		=	fp_dump,
	.dump_stats	=	fp_dump_stats,
	.owner		=	THIS_MODULE,
};

static int __init fp_module_init(void)
{
	int ret;

	fp_flow_cachep = kmem_cache_create("fp_flow_cache",
					   sizeof(struct fp_flow),
					   0, 0, NULL);
	if (!fp_flow_cachep)
		return -ENOMEM;

	ret = register_qdisc(&fp_qdisc_ops);
	if (ret)
		kmem_cache_destroy(fp_flow_cachep);
	return ret;
}

static void __exit fp_module_exit(void)
{
	unregister_qdisc(&fp_qdisc_ops);
	kmem_cache_destroy(fp_flow_cachep);
}

module_init(fp_module_init)
module_exit(fp_module_exit)
MODULE_AUTHOR("Eric Dumazet");
MODULE_LICENSE("GPL");

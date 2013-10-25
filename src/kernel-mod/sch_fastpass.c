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
#include <net/netlink.h>
#include <net/pkt_sched.h>
#include <net/sock.h>
#include <net/tcp_states.h>

#define FASTPASS_HORIZON 256

/*
 * Per flow structure, dynamically allocated
 */
struct fp_flow {
	struct sock	*sk;		/* flow identifier */
	u32		socket_hash;	/* sk_hash, detect reuse of sk after free+alloc */

	struct rb_node	fp_node; 	/* anchor in fp_root[] trees */

	/* queued buffers: */
	struct sk_buff	*head;		/* list of skbs for this flow : first skb */
	struct sk_buff	*sch_tail;	/* last skb that had been scheduled */
	struct sk_buff *tail;		/* last skb in the list */
	int		sch_credit;			/* credit remaining in the last scheduled tslot */
	int		qlen;				/* number of packets in flow queue */

	int		credit;
	int		sch_tslots;			/* number of scheduled tslots that have not ended */
	u64		last_sch_tslot;		/* last slot that had been allocated to flow */
};

struct fp_flow_head {
	struct fp_flow *first;
	struct fp_flow *last;
};

struct fp_sched_data {
	struct fp_flow_head unsch_flows; /* flows with unscheduled packets */

	struct rb_root	delayed;	/* for rate limited flows */
	u64		time_next_delayed_flow;

	struct fp_flow	internal;	/* for non classified or high prio packets */
	u32		quantum;
	u32		initial_quantum;
	u32		flow_plimit;	/* max packets per flow */
	struct rb_root	*flow_hash_tbl;
	u8		fp_trees_log;

	struct fp_flow *schedule[FASTPASS_HORIZON];
	u64		cur_tslot;
	u64		next_tslot_time;

	u32		flows;
	u32		inactive_flows;
	u32		throttled_flows;

	u64		stat_gc_flows;
	u64		stat_internal_packets;
	u64		stat_tcp_retrans;
	u64		stat_throttled;
	u64		stat_flows_plimit;
	u64		stat_pkts_too_long;
	u64		stat_allocation_errors;
	struct qdisc_watchdog watchdog;
};

/* special value to mark a detached flow (not on old/new list) */
static struct fp_flow detached, throttled;

/**
 * Flow 'f' is now allowed to use time slot 'tslot'
 */
static void fp_schedule_tslot(struct fp_sched_data *q, u64 tslot,
		struct fp_flow *f);




static void fp_flow_set_detached(struct fp_flow *f)
{
	f->next = &detached;
}

static bool fp_flow_is_detached(const struct fp_flow *f)
{
	return f->next == &detached;
}

static void fp_flow_set_throttled(struct fp_sched_data *q, struct fp_flow *f)
{
	struct rb_node **p = &q->delayed.rb_node, *parent = NULL;

	while (*p) {
		struct fp_flow *aux;

		parent = *p;
		aux = container_of(parent, struct fp_flow, rate_node);
		if (f->time_next_packet >= aux->time_next_packet)
			p = &parent->rb_right;
		else
			p = &parent->rb_left;
	}
	rb_link_node(&f->rate_node, parent, p);
	rb_insert_color(&f->rate_node, &q->delayed);
	q->throttled_flows++;
	q->stat_throttled++;

	f->next = &throttled;
	if (q->time_next_delayed_flow > f->time_next_packet)
		q->time_next_delayed_flow = f->time_next_packet;
}


static struct kmem_cache *fp_flow_cachep __read_mostly;

static void fp_flow_add_tail(struct fp_flow_head *head, struct fp_flow *flow)
{
	if (head->first)
		head->last->next = flow;
	else
		head->first = flow;
	head->last = flow;
	flow->next = NULL;
}

/* limit number of collected flows per round */
#define FP_GC_MAX 8
#define FP_GC_AGE (3*HZ)

static bool fp_gc_candidate(const struct fp_flow *f)
{
	return fp_flow_is_detached(f) &&
	       time_after(jiffies, f->age + FP_GC_AGE);
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

		if (fp_gc_candidate(f)) {
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
static struct sk_buff *fp_dequeue_head(struct Qdisc *sch, struct fp_flow *flow)
{
	struct sk_buff *skb = flow->head;

	if (skb) {
		flow->head = skb->next;
		skb->next = NULL;
		flow->qlen--;
		sch->qstats.backlog -= qdisc_pkt_len(skb);
		sch->q.qlen--;
	}
	return skb;
}

/* We might add in the future detection of retransmits
 * For the time being, just return false
 */
static bool skb_is_retransmit(struct sk_buff *skb)
{
	return false;
}

/* add skb to flow queue
 * flow queue is a linked list, kind of FIFO, except for TCP retransmits
 * We special case tcp retransmits to be transmitted before other packets.
 * We rely on fact that TCP retransmits are unlikely, so we do not waste
 * a separate queue or a pointer.
 * head->  [retrans pkt 1]
 *         [retrans pkt 2]
 *         [ normal pkt 1]
 *         [ normal pkt 2]
 *         [ normal pkt 3]
 * tail->  [ normal pkt 4]
 */
static void flow_queue_add(struct fp_flow *flow, struct sk_buff *skb)
{
	struct sk_buff *prev, *head = flow->head;

	skb->next = NULL;
	if (!head) {
		flow->head = skb;
		flow->tail = skb;
		return;
	}
	if (likely(!skb_is_retransmit(skb))) {
		flow->tail->next = skb;
		flow->tail = skb;
		return;
	}

	/* This skb is a tcp retransmit,
	 * find the last retrans packet in the queue
	 */
	prev = NULL;
	while (skb_is_retransmit(head)) {
		prev = head;
		head = head->next;
		if (!head)
			break;
	}
	if (!prev) { /* no rtx packet in queue, become the new head */
		skb->next = flow->head;
		flow->head = skb;
	} else {
		if (prev == flow->tail)
			flow->tail = skb;
		else
			skb->next = prev->next;
		prev->next = skb;
	}
}

static int fp_enqueue(struct sk_buff *skb, struct Qdisc *sch)
{
	struct fp_sched_data *q = qdisc_priv(sch);
	struct fp_flow *f;

	if (unlikely(sch->q.qlen >= sch->limit))
		return qdisc_drop(skb, sch);

	f = fp_classify(skb, q);
	if (unlikely(f->qlen >= q->flow_plimit && f != &q->internal)) {
		q->stat_flows_plimit++;
		return qdisc_drop(skb, sch);
	}

	f->qlen++;
	flow_queue_add(f, skb);
	if (skb_is_retransmit(skb))
		q->stat_tcp_retrans++;
	sch->qstats.backlog += qdisc_pkt_len(skb);
	if (fp_flow_is_detached(f)) {
		fp_flow_add_tail(&q->new_flows, f);
		if (q->quantum > f->credit)
			f->credit = q->quantum;
		q->inactive_flows--;
		qdisc_unthrottled(sch);
	}
	if (unlikely(f == &q->internal)) {
		q->stat_internal_packets++;
		qdisc_unthrottled(sch);
	}
	sch->q.qlen++;

	return NET_XMIT_SUCCESS;
}

static void fp_check_throttled(struct fp_sched_data *q, u64 now)
{
	struct rb_node *p;

	if (q->time_next_delayed_flow > now)
		return;

	q->time_next_delayed_flow = ~0ULL;
	while ((p = rb_first(&q->delayed)) != NULL) {
		struct fp_flow *f = container_of(p, struct fp_flow, rate_node);

		if (f->time_next_packet > now) {
			q->time_next_delayed_flow = f->time_next_packet;
			break;
		}
		rb_erase(p, &q->delayed);
		q->throttled_flows--;
		fp_flow_add_tail(&q->old_flows, f);
	}
}

static struct sk_buff *fp_dequeue(struct Qdisc *sch)
{
	struct fp_sched_data *q = qdisc_priv(sch);
	u64 now = ktime_to_ns(ktime_get());
	struct fp_flow_head *head;
	struct sk_buff *skb;
	struct fp_flow *f;
	u32 rate;

	skb = fp_dequeue_head(sch, &q->internal);
	if (skb)
		goto out;
	fp_check_throttled(q, now);
begin:
	head = &q->new_flows;
	if (!head->first) {
		head = &q->old_flows;
		if (!head->first) {
			if (q->time_next_delayed_flow != ~0ULL)
				qdisc_watchdog_schedule_ns(&q->watchdog,
							   q->time_next_delayed_flow);
			return NULL;
		}
	}
	f = head->first;

	if (f->credit <= 0) {
		f->credit += q->quantum;
		head->first = f->next;
		fp_flow_add_tail(&q->old_flows, f);
		goto begin;
	}

	if (unlikely(f->head && now < f->time_next_packet)) {
		head->first = f->next;
		fp_flow_set_throttled(q, f);
		goto begin;
	}

	skb = fp_dequeue_head(sch, f);
	if (!skb) {
		head->first = f->next;
		/* force a pass through old_flows to prevent starvation */
		if ((head == &q->new_flows) && q->old_flows.first) {
			fp_flow_add_tail(&q->old_flows, f);
		} else {
			fp_flow_set_detached(f);
			f->age = jiffies;
			q->inactive_flows++;
		}
		goto begin;
	}
	prefetch(&skb->end);
	f->time_next_packet = now;
	f->credit -= qdisc_pkt_len(skb);

	if (f->credit > 0 || !q->rate_enable)
		goto out;

	if (skb->sk && skb->sk->sk_state != TCP_TIME_WAIT) {
		rate = skb->sk->sk_pacing_rate ?: q->flow_default_rate;

		rate = min(rate, q->flow_max_rate);
	} else {
		rate = q->flow_max_rate;
		if (rate == ~0U)
			goto out;
	}
	if (rate) {
		u32 plen = max(qdisc_pkt_len(skb), q->quantum);
		u64 len = (u64)plen * NSEC_PER_SEC;

		do_div(len, rate);
		/* Since socket rate can change later,
		 * clamp the delay to 125 ms.
		 * TODO: maybe segment the too big skb, as in commit
		 * e43ac79a4bc ("sch_tbf: segment too big GSO packets")
		 */
		if (unlikely(len > 125 * NSEC_PER_MSEC)) {
			len = 125 * NSEC_PER_MSEC;
			q->stat_pkts_too_long++;
		}

		f->time_next_packet = now + len;
	}
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

	while ((skb = fp_dequeue_head(sch, &q->internal)) != NULL)
		kfree_skb(skb);

	if (!q->flow_hash_tbl)
		return;

	for (idx = 0; idx < (1U << q->fp_trees_log); idx++) {
		root = &q->flow_hash_tbl[idx];
		while ((p = rb_first(root)) != NULL) {
			f = container_of(p, struct fp_flow, fp_node);
			rb_erase(p, root);

			while ((skb = fp_dequeue_head(sch, f)) != NULL)
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
			if (fp_gc_candidate(of)) {
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

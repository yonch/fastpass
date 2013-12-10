/*
 * net/sched/sch_fastpass.c FastPass client
 *
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

#include "fastpass_proto.h"

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
#define FASTPASS_REQUEST_WINDOW_SIZE (1 << 13)

/**
 * Don't bother sending a fresh request when there are already many timeslots
 *   requested. Rather, wait until the number of allocated timeslots gets close
 *   to the number of request packets.
 */
#define FASTPASS_REQUEST_LOW_WATERMARK (1 << 9)

/* limit number of collected flows per round */
#define FP_GC_MAX 8
#define FP_GC_NUM_SECS 3

/*
 * Per flow structure, dynamically allocated
 */
struct fp_flow {
	u64		src_dst_key;		/* flow identifier */

	struct rb_node	fp_node; 	/* anchor in fp_root[] trees */
	struct fp_flow	*next;		/* singly linked pointer into q->unreq_flows */

	/* queued buffers: */
	struct sk_buff	*head;		/* list of skbs for this flow : first skb */
	struct sk_buff *tail;		/* last skb in the list */
	u64		demand_tslots;		/* total needed timeslots */
	u64		requested_tslots;	/* highest requested timeslots */
	u64		alloc_tslots;		/* total received allocations */
	int		qlen;				/* number of packets in flow queue */

	s64		credit;				/* time remaining in the last scheduled timeslot */
	int		sch_tslots;			/* number of scheduled tslots that have not ended */
	u64		last_sch_tslot;		/* last slot that had been allocated to flow */
};

/* special pointer signifies flow is not in a queue */
struct fp_flow		not_in_queue;

struct fp_flow_head {
	struct fp_flow *first;
	struct fp_flow *last;
};

struct fp_timeslot_horizon {
	u64 timeslot;
	u64 mask;
};

struct fp_req_ack_info {

};

/**
 *
 * @stat_redundant_allocations: got an allocation that isn't required
 */
struct fp_sched_data {
	/* configuration paramters */
	u32		flow_plimit;				/* max packets per flow */
	u8		hash_tbl_log;				/* log number of hash buckets */
	struct psched_ratecfg data_rate;	/* rate of payload packets */
	u32		tslot_len;					/* duration of a timeslot, in nanosecs */
	u32		req_cost;					/* cost, in tokens, of a request */
	u32		req_bucketlen;				/* the max number of tokens to burst */
	u32		req_min_gap;				/* min delay between requests (ns) */
	u32		gc_age;						/* number of tslots to keep empty flows */
	__be32	ctrl_addr_netorder;			/* IP of the controller, network byte order */
	u32		reset_window_us;			/* time window of acceptable resets */

	/* state */
	struct rb_root	*flow_hash_tbl;		/* table of rb-trees of flows */

	struct fp_flow	internal;		/* for non classified or high prio packets */

	struct fp_flow_head unreq_flows; 	/* flows with unscheduled packets */

	u64		tslot_start_time;			/* current time slot start time */
	struct fp_timeslot_horizon	horizon;/* which slots have been allocated */
	u64		schedule[FASTPASS_HORIZON];	/* flows scheduled in the next time slots: */
										/* slot x at [x % FASTPASS_HORIZON] */

	u64		req_t;						/* time when request credits = zero */
	u64		time_next_req;				/* time to send next request */
	struct socket	*ctrl_sock;			/* socket to the controller */

	struct qdisc_watchdog 	watchdog;
	struct hrtimer 			request_timer;
	struct tasklet_struct 	request_tasklet;


	/* counters */
	u32		flows;
	u32		inactive_flows;
	u32		n_unreq_flows;
	u64		demand_tslots;		/* total needed timeslots */
	u64		requested_tslots;	/* highest requested timeslots */
	u64		alloc_tslots;		/* total received allocations */

	/* statistics */
	u64		stat_gc_flows;
	u64		stat_internal_packets;
	u64		stat_flows_plimit;
	u64		stat_allocation_errors;
	u64		stat_missed_timeslots;
	u64		stat_used_timeslots;
	u64		stat_requests;
	u64		stat_classify_errors;
	u64		stat_req_alloc_errors;
	u64		stat_non_ctrl_highprio_pkts;
	u64		stat_redundant_allocations;   /* TODO: report to tc */
	u64		stat_oob_alloc_no_flow;   /* TODO: report to tc */
	u64		stat_oob_alloc_past;   /* TODO: report to tc */
	u64		stat_oob_alloc_future;   /* TODO: report to tc */
	u64 	stat_ntp_packets; /* TODO: report to tc */
	u64		stat_late_allocation_tslots; /* TODO: report to tc */
	u64		stat_too_futuristic_alloc_tslots; /* TODO: report to tc */
};

static struct kmem_cache *fp_flow_cachep __read_mostly;

/* translates IP address to short FastPass ID */
u16 ip_to_id(__be32 ipaddr) {
	return (u16)ntohl(ipaddr);
}

/* hashes a flow key into a u32, for lookup in the hash tables */
static inline u32 src_dst_key_hash(u64 src_dst_key) {
	return jhash_2words((__be32)(src_dst_key >> 32),
						 (__be32)src_dst_key, 0);
}

/* returns the src_dst_key allocated to the current timeslot */
static inline u64 horizon_current_key(struct fp_sched_data* q)
{
	return q->schedule[q->horizon.timeslot % FASTPASS_HORIZON];
}

/* advances the horizon 'num_tslots' into the future */
static void horizon_advance(struct fp_timeslot_horizon *h, u32 num_tslots) {
	if (unlikely(num_tslots >= 64))
		h->mask = 0;
	else
		h->mask >>= num_tslots;
	h->timeslot += num_tslots;
}

/* find the first time slot in the horizon that is set, returns -1 if none */
static u32 horizon_next_nonempty(struct fp_timeslot_horizon *h) {
	return h->mask ? __ffs64(h->mask) : -1;
}

static bool horizon_cur_is_marked(struct fp_timeslot_horizon *h)
{
	return !!(h->mask & 1ULL);
}

static void horizon_unmark_current(struct fp_timeslot_horizon *h)
{
	h->mask &= ~1ULL;
}

static void horizon_set(struct fp_sched_data* q, u64 timeslot, u64 src_dst_key) 
{
	struct fp_timeslot_horizon *h = &q->horizon;

	h->mask |= 1ULL << (timeslot - h->timeslot);
	q->schedule[timeslot % FASTPASS_HORIZON] = src_dst_key;
}

/* computes the time when next request should go out */
void set_request_timer(struct fp_sched_data* q, u64 now)
{
	q->time_next_req = max_t(u64, q->req_t + q->req_cost,
								  now + q->req_min_gap);

	hrtimer_start(&q->request_timer,
			      ns_to_ktime(q->time_next_req),
			      HRTIMER_MODE_ABS);
}


static bool flow_in_flowqueue(struct fp_flow *f)
{
	return (f->next != &not_in_queue);
}

/* adds flow to a list of flows, at tail */
static void flowqueue_enqueue(struct fp_sched_data *q, struct fp_flow *f)
{
	struct fp_flow_head *head = &q->unreq_flows;

	BUG_ON(flow_in_flowqueue(f));

	/* enqueue */
	if (head->first)
		head->last->next = f;
	else
		head->first = f;
	head->last = f;
	f->next = NULL;

	q->n_unreq_flows++;

	/* if enqueued first flow in q->unreq_flows, set request timer */
	if (q->n_unreq_flows == 1) {
		set_request_timer(q, fp_get_time_ns());
		fastpass_pr_debug("set request timer to %llu\n", q->time_next_req);
	}
}

static struct fp_flow *flowqueue_dequeue(struct fp_sched_data* q)
{
	struct fp_flow *f = q->unreq_flows.first;

	BUG_ON(f == NULL);

	q->unreq_flows.first = f->next;
	f->next = &not_in_queue;
	q->n_unreq_flows--;

	return f;
}

static bool flowqueue_is_empty(struct fp_sched_data* q)
{
	return (q->unreq_flows.first == NULL);
}

/* should the flow be garbage-collected? */
static bool fp_gc_candidate(const struct fp_sched_data *q,
		const struct fp_flow *f)
{
	return f->qlen == 0 &&
	       time_after64(q->horizon.timeslot, f->last_sch_tslot + q->gc_age);
}

/* garbage collect the given tree on the path from @root to @src_dst_key */
static void fp_gc(struct fp_sched_data *q,
		  struct rb_root *root,
		  u64 src_dst_key)
{
	struct fp_flow *f, *tofree[FP_GC_MAX];
	struct rb_node **p, *parent;
	int fcnt = 0;

	p = &root->rb_node;
	parent = NULL;
	while (*p) {
		parent = *p;

		f = container_of(parent, struct fp_flow, fp_node);
		if (f->src_dst_key == src_dst_key)
			break;

		if (fp_gc_candidate(q, f)) {
			tofree[fcnt++] = f;
			if (fcnt == FP_GC_MAX)
				break;
		}

		if (f->src_dst_key > src_dst_key)
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

/**
 * Looks up the specific key in the flow hash table.
 *   When the flow is not present:
 *     If create_if_missing is true, creates a new flow and returns it.
 *     Otherwise, returns NULL.
 */
static struct fp_flow *fpq_lookup(struct fp_sched_data *q, u64 src_dst_key,
		bool create_if_missing)
{
	struct rb_node **p, *parent;
	struct rb_root *root;
	struct fp_flow *f;
	u32 skb_hash;

	/* get the key's hash */
	skb_hash = src_dst_key_hash(src_dst_key);

	root = &q->flow_hash_tbl[skb_hash >> (32 - q->hash_tbl_log)];

	if (q->flows >= (2U << q->hash_tbl_log) &&
	    q->inactive_flows > q->flows/2)
		fp_gc(q, root, src_dst_key);

	p = &root->rb_node;
	parent = NULL;
	while (*p) {
		parent = *p;

		f = container_of(parent, struct fp_flow, fp_node);
		if (f->src_dst_key == src_dst_key)
			return f;

		if (f->src_dst_key > src_dst_key)
			p = &parent->rb_right;
		else
			p = &parent->rb_left;
	}

	/* did not find existing entry */
	if (!create_if_missing)
		return NULL;

	/* allocate a new one */
	f = kmem_cache_zalloc(fp_flow_cachep, GFP_ATOMIC | __GFP_NOWARN);
	if (unlikely(!f)) {
		q->stat_allocation_errors++;
		return &q->internal;
	}
	f->src_dst_key = src_dst_key;

	rb_link_node(&f->fp_node, parent, p);
	rb_insert_color(&f->fp_node, root);
	f->next = &not_in_queue;

	q->flows++;
	q->inactive_flows++;
	return f;
}

/* returns the flow for the given packet, allocates a new flow if needed */
static struct fp_flow *fpq_classify(struct sk_buff *skb, struct fp_sched_data *q)
{
	struct sock *sk = skb->sk;
	int band;
	struct flow_keys keys;
	u64 src_dst_key;

	/* warning: no starvation prevention... */
	band = prio2band[skb->priority & TC_PRIO_MAX];
	if (unlikely(band == 0)) {
		if (unlikely(skb->sk != q->ctrl_sock->sk))
			q->stat_non_ctrl_highprio_pkts++;
		return &q->internal;
	}

	/* get source and destination IPs */
	if (likely(   sk
			   && (sk->sk_family == AF_INET)
			   && (sk->sk_protocol == IPPROTO_TCP))) {
		keys.src = inet_sk(sk)->inet_saddr;
		keys.dst = inet_sk(sk)->inet_daddr;
	} else {
		if (!skb_flow_dissect(skb, &keys))
			goto cannot_classify;
	}

	/* special case for NTP packets, let them through with high priority */
	if (unlikely(keys.ip_proto == IPPROTO_UDP && keys.port16[1] == htons(123))) {
		q->stat_ntp_packets++;
		return &q->internal;
	}

	/* get the skb's key (src_dst_key) */
	src_dst_key = ip_to_id(keys.dst);

	return fpq_lookup(q, src_dst_key, true);

cannot_classify:
	// ARP packets should not count as classify errors
	if (unlikely(skb->protocol != htons(ETH_P_ARP))) {
		q->stat_classify_errors++;
		if (fastpass_debug) {
			fastpass_pr_debug("cannot classify packet with protocol %u:\n", skb->protocol);
			print_hex_dump(KERN_DEBUG, "cannot classify: ", DUMP_PREFIX_OFFSET,
					16, 1, skb->data, min_t(size_t, skb->len, 64), false);
		}
	}
	return &q->internal;
}

/* remove one skb from head of flow queue */
static struct sk_buff *flow_dequeue_skb(struct Qdisc *sch, struct fp_flow *flow)
{
	struct fp_sched_data *q = qdisc_priv(sch);
	struct sk_buff *skb = flow->head;

	if (skb) {
		flow->head = skb->next;
		skb->next = NULL;
		flow->qlen--;
		sch->q.qlen--;
		sch->qstats.backlog -= qdisc_pkt_len(skb);
		if (flow->qlen == 0)
			q->inactive_flows++;
	}
	return skb;
}

static bool flow_is_below_watermark(struct fp_flow* f)
{
	u64 watermark = f->alloc_tslots + FASTPASS_REQUEST_LOW_WATERMARK;
	return time_before_eq64(f->requested_tslots, watermark);
}

void flow_inc_alloc(struct fp_sched_data* q, struct fp_flow* f)
{
	f->alloc_tslots++;
	q->alloc_tslots++;

	if (unlikely((!flow_in_flowqueue(f))
			&& (f->requested_tslots != f->demand_tslots)
			&& flow_is_below_watermark(f)))
		flowqueue_enqueue(q, f);
}

/**
 * Increase the number of unrequested packets for the flow.
 *   Maintains the necessary invariants, e.g. adds the flow to the unreq_flows
 *   list if necessary
 */
static void flow_inc_demand(struct fp_sched_data *q, struct fp_flow *f)
{
	f->demand_tslots++;
	q->demand_tslots++;
	if ((f->demand_tslots == f->requested_tslots + 1)
			&& flow_is_below_watermark(f)) {

		/* flow not on scheduling queue yet, enqueue */
		flowqueue_enqueue(q, f);
	}
}

/* add skb to flow queue */
static void flow_enqueue_skb(struct Qdisc *sch, struct fp_flow *flow,
		struct sk_buff *skb)
{
	struct fp_sched_data *q = qdisc_priv(sch);
	struct sk_buff *head = flow->head;
	s64 cost = (s64) psched_l2t_ns(&q->data_rate, qdisc_pkt_len(skb));

	skb->next = NULL;
	if (!head) {
		flow->head = skb;
		flow->tail = skb;
	} else {
		flow->tail->next = skb;
		flow->tail = skb;
	}

	if (flow != &q->internal) {
		/* if credit relates to an old slot, discard it */
		if (unlikely(flow->demand_tslots == flow->alloc_tslots))
			flow->credit = 0;

		/* check if need to request a new slot */
		if (cost > flow->credit) {
			flow_inc_demand(q, flow);
			flow->credit = q->tslot_len;
		}
		flow->credit -= cost;
	}

	/* if queue was empty before, decrease inactive flow count */
	if (flow->qlen == 0)
		q->inactive_flows--;

	flow->qlen++;
	sch->q.qlen++;
	sch->qstats.backlog += qdisc_pkt_len(skb);
}

static struct sk_buff *flow_queue_peek(struct fp_flow *f)
{
	return f->head;
}

static bool flow_has_skbs(struct fp_flow *f)
{
	return (f->head == NULL);
}

/* enqueue packet to the qdisc (part of the qdisc api) */
static int fpq_enqueue(struct sk_buff *skb, struct Qdisc *sch)
{
	struct fp_sched_data *q = qdisc_priv(sch);
	struct fp_flow *f;

	/* enforce qdisc packet limit */
	if (unlikely(sch->q.qlen >= sch->limit))
		return qdisc_drop(skb, sch);

	f = fpq_classify(skb, q);

	/* enforce flow packet limit */
	if (unlikely(f->qlen >= q->flow_plimit && f != &q->internal)) {
		q->stat_flows_plimit++;
		return qdisc_drop(skb, sch);
	}

	/* queue skb to flow, update statistics */
	flow_enqueue_skb(sch, f, skb);
	fastpass_pr_debug("enqueued packet of len %d to flow 0x%llX, qlen=%d\n",
			qdisc_pkt_len(skb), f->src_dst_key, f->qlen);

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
static void move_timeslot_from_flow(struct Qdisc *sch, struct psched_ratecfg *rate,
		struct fp_flow *src, struct fp_flow *dst)
{
	struct fp_sched_data *q = qdisc_priv(sch);
	s64 credit = q->tslot_len;
	struct sk_buff *skb;
	u32 count = 0;

	/* while enough credit, move packets to q->internal */
	while (!flow_has_skbs(src)) {
		u32 skb_len = qdisc_pkt_len(flow_queue_peek(src));
		u64 skb_ns = psched_l2t_ns(rate, skb_len);

		fastpass_pr_debug("credit=%llu, pkt_len=%u, cost=%llu\n",
				credit, skb_len, skb_ns);
		credit -= (s64) skb_ns;
		if (credit < 0)
			break; /* ran out of credit */

		skb = flow_dequeue_skb(sch, src);
		flow_enqueue_skb(sch, dst, skb);
		count++;
	}

	fastpass_pr_debug("@%llu moved %u out of %u packets from %llu (0x%04llx)\n",
			q->horizon.timeslot, count, src->qlen + count, src->src_dst_key,
			src->src_dst_key);
}

/**
 * Handles cases where a flow was allocated but we cannot fulfill it, either
 *    because the time has passed or is too far in the future.
 */
void handle_out_of_bounds_allocation(struct fp_sched_data* q,
		u64 src_dst_key)
{
	struct fp_flow *f = fpq_lookup(q, src_dst_key, false);

	if (f == NULL) {
		/*
		 * Couldn't find the flow. The allocation was either for an invalid
		 *    destination, or was not needed and the flow was garbage-collected.
		 */
		q->stat_oob_alloc_no_flow++;
		return;
	}

	/* flow will need to re-request a slot*/
	flow_inc_demand(q, f);
	/* we mark that we were allocated this timeslot */
	flow_inc_alloc(q, f);
}

/**
 * Change the qdisc state from its old time slot to the time slot at time @now
 */
static void update_current_timeslot(struct Qdisc *sch, u64 now)
{
	struct fp_sched_data *q = qdisc_priv(sch);
	int next_nonempty;
	u64 next_nonempty_start;
	struct fp_flow *f;

	if (unlikely(time_before64(now, q->tslot_start_time + q->tslot_len))) {
		/* still in the old time slot. should we move packets from a queue? */
		if (horizon_cur_is_marked(&q->horizon) && flow_has_skbs(&q->internal))
			goto move_current;
		else
			return; /* shouldn't move anything. */
	}

begin:
	next_nonempty = horizon_next_nonempty(&q->horizon);
	next_nonempty_start = q->tslot_start_time + next_nonempty * q->tslot_len;

	/* is current slot an empty slot? */
	if (next_nonempty < 0 || time_before64(now, next_nonempty_start)) {
		/* find the current timeslot's index */
		u64 tslot_advance = now - q->tslot_start_time;
		do_div(tslot_advance, q->tslot_len);

		/* move to the new slot */
		q->tslot_start_time += tslot_advance * q->tslot_len;
		horizon_advance(&q->horizon, tslot_advance);
		fastpass_pr_debug("moved by %llu timeslots to empty timeslot %llu\n",
				tslot_advance, q->horizon.timeslot);

		return;
	}

	/* advance current time slot to next non-empty slot */
	horizon_advance(&q->horizon, next_nonempty);
	q->tslot_start_time = next_nonempty_start;

	/* did we encounter a scheduled slot that is in the past */
	if (unlikely(time_after_eq64(now, q->tslot_start_time + q->tslot_len))) {
		q->stat_missed_timeslots++;
		fastpass_pr_debug("missed timeslot %llu by %llu ns, rescheduling\n",
				q->horizon.timeslot, now - (q->tslot_start_time + q->tslot_len));
		handle_out_of_bounds_allocation(q, horizon_current_key(q));
		horizon_unmark_current(&q->horizon);
		goto begin;
	}

move_current:
	f = fpq_lookup(q, horizon_current_key(q), false);
	if (unlikely(f == NULL)) {
		q->stat_oob_alloc_no_flow++;
		return;
	}

	move_timeslot_from_flow(sch, &q->data_rate, f, &q->internal);
	horizon_unmark_current(&q->horizon);
	q->stat_used_timeslots++;
	flow_inc_alloc(q,f);
}

void set_watchdog(struct Qdisc* sch) {
	struct fp_sched_data *q = qdisc_priv(sch);
	int next_slot;

	BUG_ON(q->internal.qlen != 0);

	next_slot = horizon_next_nonempty(&q->horizon);
	if (unlikely(next_slot < 0)) {
		qdisc_throttled(sch);
		fastpass_pr_debug("horizon empty. throttling qdisc\n");
	} else {
		qdisc_watchdog_schedule_ns(&q->watchdog,
				q->tslot_start_time + next_slot * q->tslot_len);
		fastpass_pr_debug("scheduled %d tslots in future, at %llu\n",
				next_slot, q->tslot_start_time + next_slot * q->tslot_len);
	}
}

/**
 * Performs a reset and garbage collection of flows
 */
static void handle_reset(struct Qdisc *sch)
{
	struct fp_sched_data *q = qdisc_priv(sch);

	(void)q;/* TODO */
}

/**
 * Handles an ALLOC payload
 */
static void handle_alloc(struct Qdisc *sch, u32 base_tslot, u16 *dst,
		int n_dst, u8 *tslots, int n_tslots) 
{
	struct fp_sched_data *q = qdisc_priv(sch);
	int i;
	u8 spec;
	int dst_ind;
	u64 full_tslot;
	u64 now = fp_get_time_ns();

	update_current_timeslot(sch, now);
	full_tslot = q->horizon.timeslot - (1ULL << 18); /* 1/4 back, 3/4 front */
	full_tslot += ((u32)base_tslot - (u32)full_tslot) & 0xFFFFF; /* 20 bits */

	fastpass_pr_debug("got ALLOC for timeslot %d (full %llu, current %llu), %d destinations, %d timeslots, mask 0x%016llX",
			base_tslot, full_tslot, q->horizon.timeslot, n_dst, n_tslots, q->horizon.mask);

	for (i = 0; i < n_tslots; i++) {
		spec = tslots[i];
		dst_ind = spec >> 4;

		if (dst_ind == 0) {
			/* Skip instruction */
			base_tslot += 16 * (1 + (spec & 0xF));
			full_tslot += 16 * (1 + (spec & 0xF));
			fastpass_pr_debug("ALLOC skip to timeslot %d full %llu (no allocation)\n",
					base_tslot, full_tslot);
			continue;
		}

		if (dst_ind > n_dst) {
			/* destination index out of bounds */
			FASTPASS_CRIT("ALLOC tslot spec 0x%02X has illegal dst index %d (max %d)\n",
					spec, dst_ind, n_dst);
			return;
		}

		base_tslot += 1 + (spec & 0xF);
		full_tslot += 1 + (spec & 0xF);
		fastpass_pr_debug("Timeslot %d (full %llu) to destination 0x%04x (%d)\n",
				base_tslot, full_tslot, dst[dst_ind - 1], dst[dst_ind - 1]);

		if (unlikely(full_tslot < q->horizon.timeslot)) {
			/* this allocation is too late */
			q->stat_late_allocation_tslots++;
			handle_out_of_bounds_allocation(q, dst[dst_ind - 1]);
			fastpass_pr_debug("-X- already gone, will reschedule\n");
		} else if (unlikely(full_tslot >= q->horizon.timeslot + FASTPASS_HORIZON)) {
			q->stat_too_futuristic_alloc_tslots++;
			handle_out_of_bounds_allocation(q, dst[dst_ind - 1]);
			fastpass_pr_debug("-X- too futuristic, will reschedule\n");
		} else {
			/* okay, allocate */
			horizon_set(q, full_tslot, dst[dst_ind - 1]);
		}
	}

	fastpass_pr_debug("mask after: 0x%016llX, is marked=%d\n",
			q->horizon.mask, horizon_cur_is_marked(&q->horizon));

	/* schedule transmission or set watchdog */
	if (unlikely(q->internal.qlen != 0)) {
		fastpass_pr_debug("queue is non-empty, will let dequeue reset watchdog\n");
		return; /* don't want to sabotage current queue */
	}

	if (q->horizon.mask & 1ULL) {
		fastpass_pr_debug("current timeslot is allocated, unthrottling qdisc\n");
		qdisc_unthrottled(sch);
		__netif_schedule(qdisc_root(sch));
	} else {
		set_watchdog(sch);
	}
}

/**
 * Send a request packet to the controller
 */
static void send_request(struct Qdisc *sch, u64 now)
{
	struct fp_sched_data *q = qdisc_priv(sch);
	spinlock_t *root_lock = qdisc_lock(qdisc_root(sch));

	struct fp_flow *f;
	struct sk_buff *skb = NULL;
	const int max_payload_len = 40;
	struct fastpass_areq *areq;
	int max_header;
	int payload_len = 0;
	int err;
	u64 new_requested;

	spin_lock_bh(root_lock);

	/* Check that the qdisc destroy func didn't race ahead of us */
	if (unlikely(sch->limit == 0)) {
		spin_unlock_bh(root_lock);
		return;
	}

	fastpass_pr_debug(
			"start: unreq_flows=%u, unreq_tslots=%llu, now=%llu, scheduled=%llu, diff=%lld\n",
			q->n_unreq_flows, q->demand_tslots - q->requested_tslots, now,
			q->time_next_req, (s64 )now - (s64 )q->time_next_req);

	BUG_ON(q->req_t + q->req_cost > now);
	BUG_ON(flowqueue_is_empty(q));
	BUG_ON(!q->ctrl_sock);

	/* allocate request skb */
	max_header = q->ctrl_sock->sk->sk_prot->max_header;
	skb = sock_alloc_send_skb(q->ctrl_sock->sk, max_payload_len + max_header, 1,
			&err);
	if (!skb)
		goto alloc_err;
	skb_reserve(skb, max_header);
	skb_reserve(skb, 2);

	while ((payload_len + 4 < max_payload_len) && !flowqueue_is_empty(q)) {
		f = flowqueue_dequeue(q);

		new_requested = min_t(u64, f->demand_tslots,
				f->alloc_tslots + FASTPASS_REQUEST_WINDOW_SIZE - 1);
		BUG_ON(new_requested <= f->requested_tslots);

		skb_put(skb, 4);
		areq = (struct fastpass_areq *)&skb->data[payload_len];
		areq->dst = htons((__be16)f->src_dst_key);
		areq->count = htons((u16)new_requested);
		payload_len += 4;

		q->requested_tslots += (new_requested - f->requested_tslots);
		f->requested_tslots = new_requested;

		/* TODO: add to retransmission queue */
	}
	skb_push(skb, 2);
	*(__be16 *)&skb->data[0] = htons((FASTPASS_PTYPE_AREQ << 12) |
			((payload_len >> 2) & 0x3F));

	fastpass_pr_debug("end: unreq_flows=%u, unreq_tslots=%llu\n",
			q->n_unreq_flows, q->demand_tslots - q->requested_tslots);

	q->stat_requests++;

update_credits:
	/* update request credits */
	q->req_t = max_t(u64, q->req_t, now - q->req_bucketlen) + q->req_cost;
	/* set timer for next request, if a request would be required */
	if (q->n_unreq_flows)
		set_request_timer(q, now);
	else
		q->time_next_req = ~0ULL;

	spin_unlock_bh(root_lock);

	if (likely(skb != NULL))
		fpproto_send_skb(q->ctrl_sock->sk, skb);

	return;

alloc_err:
	q->stat_req_alloc_errors++;
	fastpass_pr_debug("request allocation failed, err %d\n", err);
	goto update_credits;
}

static enum hrtimer_restart send_request_timer_func(struct hrtimer *timer)
{
	struct fp_sched_data *q =
			container_of(timer, struct fp_sched_data, request_timer);

	/* schedule tasklet to write request */
	tasklet_schedule(&q->request_tasklet);

	return HRTIMER_NORESTART;
}

static void send_request_tasklet(unsigned long int param)
{
	struct Qdisc *sch = (struct Qdisc *)param;
	u64 now = fp_get_time_ns();

	send_request(sch, now);
}

/* Extract packet from the queue (part of the qdisc API) */
static struct sk_buff *fpq_dequeue(struct Qdisc *sch)
{
	struct fp_sched_data *q = qdisc_priv(sch);
	u64 now = fp_get_time_ns();
	struct sk_buff *skb;

	/* any packets already queued? */
	skb = flow_dequeue_skb(sch, &q->internal);
	if (skb)
		goto out_got_skb;

	/* internal queue is empty; update timeslot (may queue skbs in q->internal) */
	update_current_timeslot(sch, now);

	/* if packets were queued for this timeslot, send them. */
	skb = flow_dequeue_skb(sch, &q->internal);
	if (skb)
		goto out_got_skb;

	/* no packets in queue, go to sleep */
	BUG_ON(horizon_cur_is_marked(&q->horizon));
	set_watchdog(sch);
	return NULL;

out_got_skb:
	qdisc_bstats_update(sch, skb);
	qdisc_unthrottled(sch);
	return skb;
}

struct fpproto_ops fastpass_sch_proto_ops = {
	.handle_reset = &handle_reset,
	.handle_alloc = &handle_alloc,
};

/* reconnects the control socket to the controller */
static int reconnect_ctrl_socket(struct Qdisc *sch)
{
	struct fp_sched_data *q = qdisc_priv(sch);
	int rc;
	struct sockaddr_in sock_addr = {
			.sin_family = AF_INET,
			.sin_port = FASTPASS_DEFAULT_PORT_NETORDER
	};

	/* if socket exists, close it */
	if (q->ctrl_sock) {
		sock_release(q->ctrl_sock);
		q->ctrl_sock = NULL;
	}

	/* create socket */
	rc = __sock_create(dev_net(qdisc_dev(sch)), AF_INET, SOCK_DGRAM,
			   IPPROTO_FASTPASS, &q->ctrl_sock, 1);
	if (rc != 0) {
		FASTPASS_WARN("Error %d creating socket\n", rc);
		q->ctrl_sock = NULL;
		return rc;
	}

	/* set socket priority */
	q->ctrl_sock->sk->sk_priority = TC_PRIO_CONTROL;

	/* skb allocation must be atomic (done under the qdisc lock) */
	q->ctrl_sock->sk->sk_allocation = GFP_ATOMIC;

	/* give socket a reference to this qdisc for watchdog */
	fpproto_set_qdisc(q->ctrl_sock->sk, sch);

	/* set protocol ops */
	fastpass_sk(q->ctrl_sock->sk)->ops = &fastpass_sch_proto_ops;

	/* set reset window */
	fastpass_sk(q->ctrl_sock->sk)->rst_win_ns =
				(u64)q->reset_window_us * NSEC_PER_USEC;


	/* connect */
	sock_addr.sin_addr.s_addr = q->ctrl_addr_netorder;
	rc = kernel_connect(q->ctrl_sock, (struct sockaddr *)&sock_addr,
			sizeof(sock_addr), 0);
	if (rc != 0)
		goto err_release;

	return 0;

err_release:
	FASTPASS_WARN("Error %d trying to connect to addr 0x%X (in netorder)\n",
			rc, q->ctrl_addr_netorder);
	sock_release(q->ctrl_sock);
	q->ctrl_sock = NULL;
	return rc;
}

/* resets the state of the qdisc (part of qdisc API) */
static void fp_tc_reset(struct Qdisc *sch)
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

	for (idx = 0; idx < (1U << q->hash_tbl_log); idx++) {
		root = &q->flow_hash_tbl[idx];
		while ((p = rb_first(root)) != NULL) {
			f = container_of(p, struct fp_flow, fp_node);
			rb_erase(p, root);

			while ((skb = flow_dequeue_skb(sch, f)) != NULL)
				kfree_skb(skb);

			kmem_cache_free(fp_flow_cachep, f);
		}
	}
	q->unreq_flows.first	= NULL;
	q->horizon.mask = 0ULL;
	q->flows		= 0;
	q->inactive_flows	= 0;
	q->n_unreq_flows	= 0;
	q->demand_tslots	= 0;
	q->requested_tslots	= 0;
	q->alloc_tslots		= 0;
}

/*
 * Re-hashes flow to a hash table with a potentially different size.
 *   Performs garbage collection in the process.
 */
static void fp_tc_rehash(struct fp_sched_data *q,
		      struct rb_root *old_array, u32 old_log,
		      struct rb_root *new_array, u32 new_log)
{
	struct rb_node *op, **np, *parent;
	struct rb_root *oroot, *nroot;
	struct fp_flow *of, *nf;
	int fcnt = 0;
	u32 idx;
	u32 skb_hash;

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
			skb_hash = src_dst_key_hash(of->src_dst_key);
			nroot = &new_array[skb_hash >> (32 - new_log)];

			np = &nroot->rb_node;
			parent = NULL;
			while (*np) {
				parent = *np;

				nf = container_of(parent, struct fp_flow, fp_node);
				BUG_ON(nf->src_dst_key == of->src_dst_key);

				if (nf->src_dst_key > of->src_dst_key)
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

/* Resizes the hash table to a new size, rehashing if necessary */
static int fp_tc_resize(struct fp_sched_data *q, u32 log)
{
	struct rb_root *array;
	u32 idx;

	if (q->flow_hash_tbl && log == q->hash_tbl_log)
		return 0;

	array = kmalloc(sizeof(struct rb_root) << log, GFP_ATOMIC);
	if (!array)
		return -ENOMEM;

	for (idx = 0; idx < (1U << log); idx++)
		array[idx] = RB_ROOT;

	if (q->flow_hash_tbl) {
		fp_tc_rehash(q, q->flow_hash_tbl, q->hash_tbl_log, array, log);
		kfree(q->flow_hash_tbl);
	}
	q->flow_hash_tbl = array;
	q->hash_tbl_log = log;

	return 0;
}

/* netlink protocol data */
static const struct nla_policy fp_policy[TCA_FASTPASS_MAX + 1] = {
	[TCA_FASTPASS_PLIMIT]			= { .type = NLA_U32 },
	[TCA_FASTPASS_FLOW_PLIMIT]		= { .type = NLA_U32 },
	[TCA_FASTPASS_BUCKETS_LOG]		= { .type = NLA_U32 },
	[TCA_FASTPASS_DATA_RATE]		= { .type = NLA_U32 },
	[TCA_FASTPASS_TIMESLOT_NSEC]	= { .type = NLA_U32 },
	[TCA_FASTPASS_REQUEST_COST]		= { .type = NLA_U32 },
	[TCA_FASTPASS_REQUEST_BUCKET]	= { .type = NLA_U32 },
	[TCA_FASTPASS_REQUEST_GAP]		= { .type = NLA_U32 },
	[TCA_FASTPASS_CONTROLLER_IP]	= { .type = NLA_U32 },
	[TCA_FASTPASS_RST_WIN_USEC]		= { .type = NLA_U32 },
};

/* change configuration (part of qdisc API) */
static int fp_tc_change(struct Qdisc *sch, struct nlattr *opt) {
	struct fp_sched_data *q = qdisc_priv(sch);
	struct nlattr *tb[TCA_FASTPASS_MAX + 1];
	int err, drop_count = 0;
	u32 fp_log;
	bool should_reconnect = false;
	struct tc_ratespec data_rate_spec ={
			.linklayer = TC_LINKLAYER_ETHERNET,
			.overhead = 24};


	if (!opt)
		return -EINVAL;

	err = nla_parse_nested(tb, TCA_FASTPASS_MAX, opt, fp_policy);
	if (err < 0)
		return err;

	sch_tree_lock(sch);

	fp_log = q->hash_tbl_log;

	if (tb[TCA_FASTPASS_PLIMIT]) {
		u32 nval = nla_get_u32(tb[TCA_FASTPASS_PLIMIT]);

		if (nval > 0)
			sch->limit = nval;
		else
			err = -EINVAL;
	}

	if (tb[TCA_FASTPASS_FLOW_PLIMIT])
		q->flow_plimit = nla_get_u32(tb[TCA_FASTPASS_FLOW_PLIMIT]);

	if (tb[TCA_FASTPASS_BUCKETS_LOG]) {
		u32 nval = nla_get_u32(tb[TCA_FASTPASS_BUCKETS_LOG]);

		if (nval >= 1 && nval <= ilog2(256*1024))
			fp_log = nval;
		else
			err = -EINVAL;
	}
	if (tb[TCA_FASTPASS_DATA_RATE]) {
		data_rate_spec.rate = nla_get_u32(tb[TCA_FASTPASS_DATA_RATE]);
		if (data_rate_spec.rate == 0)
			err = -EINVAL;
		else
			psched_ratecfg_precompute(&q->data_rate, &data_rate_spec);
	}
	if (tb[TCA_FASTPASS_TIMESLOT_NSEC]) {
		u64 now = fp_get_time_ns();
		q->tslot_len = nla_get_u32(tb[TCA_FASTPASS_TIMESLOT_NSEC]);
		q->horizon.timeslot = now;
		q->tslot_start_time = now - do_div(q->horizon.timeslot, q->tslot_len);
	}

	if (tb[TCA_FASTPASS_REQUEST_COST])
		q->req_cost = nla_get_u32(tb[TCA_FASTPASS_REQUEST_COST]);

	if (tb[TCA_FASTPASS_REQUEST_BUCKET]) {
		u64 now = fp_get_time_ns();
		q->req_bucketlen = nla_get_u32(tb[TCA_FASTPASS_REQUEST_BUCKET]);
		q->req_t = now - q->req_bucketlen;	/* start with full bucket */
	}

	if (tb[TCA_FASTPASS_REQUEST_GAP]) {
		q->req_min_gap = nla_get_u32(tb[TCA_FASTPASS_REQUEST_GAP]);
		q->gc_age = DIV_ROUND_UP(FP_GC_NUM_SECS * NSEC_PER_SEC, q->tslot_len);
	}

	if (tb[TCA_FASTPASS_CONTROLLER_IP]) {
		q->ctrl_addr_netorder = nla_get_u32(tb[TCA_FASTPASS_CONTROLLER_IP]);
		should_reconnect = true;
	}

	if (tb[TCA_FASTPASS_RST_WIN_USEC]) {
		q->reset_window_us = nla_get_u32(tb[TCA_FASTPASS_RST_WIN_USEC]);
		if (q->ctrl_sock)
			fastpass_sk(q->ctrl_sock->sk)->rst_win_ns =
					(u64)q->reset_window_us * NSEC_PER_USEC;
	}

	if (!err && (should_reconnect || !q->ctrl_sock))
		err = reconnect_ctrl_socket(sch);

	if (!err)
		err = fp_tc_resize(q, fp_log);

	while (sch->q.qlen > sch->limit) {
		struct sk_buff *skb = fpq_dequeue(sch);

		if (!skb)
			break;
		kfree_skb(skb);
		drop_count++;
	}
	qdisc_tree_decrease_qlen(sch, drop_count);

	sch_tree_unlock(sch);
	return err;
}

/* destroy the qdisc (part of qdisc API) */
static void fp_tc_destroy(struct Qdisc *sch)
{
	struct fp_sched_data *q = qdisc_priv(sch);
	spinlock_t *root_lock = qdisc_root_lock(sch);

	/* Apparently no lock protection here. We lock to prevent races */

	/* Notify lockers that qdisc is being destroyed */
	spin_lock_bh(root_lock);
	sch->limit = 0;
	spin_unlock_bh(root_lock);

	/* close socket. no new packets should arrive afterwards */
	sock_release(q->ctrl_sock);

	/* eliminate the request timer */
	hrtimer_cancel(&q->request_timer);

	/**
	 * make sure there isn't a tasklet running which might try to lock
	 *   after the the lock is destroyed
	 */
	tasklet_kill(&q->request_tasklet);

	spin_lock_bh(root_lock);
	fp_tc_reset(sch);
	kfree(q->flow_hash_tbl);
	qdisc_watchdog_cancel(&q->watchdog);
	spin_unlock_bh(root_lock);
}

/* initialize a new qdisc (part of qdisc API) */
static int fp_tc_init(struct Qdisc *sch, struct nlattr *opt)
{
	struct fp_sched_data *q = qdisc_priv(sch);
	u64 now = fp_get_time_ns();
	struct tc_ratespec data_rate_spec ={
			.linklayer = TC_LINKLAYER_ETHERNET,
			.rate = 1e9/8,
			.overhead = 24};
	int err;
	struct qdisc_watchdog tmp_watchdog;

	sch->limit			= 10000;
	q->flow_plimit		= 100;
	q->hash_tbl_log		= ilog2(1024);
	psched_ratecfg_precompute(&q->data_rate, &data_rate_spec);
	q->tslot_len		= 13000;
	q->req_cost			= 2 * q->tslot_len;
	q->req_bucketlen	= 4 * q->req_cost;
	q->req_min_gap		= 1000;
	q->gc_age = DIV_ROUND_UP(FP_GC_NUM_SECS * NSEC_PER_SEC, q->tslot_len);
	q->ctrl_addr_netorder = htonl(0x7F000001); /* need sensible default? */
	q->reset_window_us	= 2e6; /* 2 seconds */
	q->flow_hash_tbl	= NULL;
	q->unreq_flows.first= NULL;
	q->internal.src_dst_key = 0xD066F00DDEADBEEF;
	q->time_next_req = ~0ULL;

	/* calculate timeslot from beginning of Epoch */
	q->horizon.timeslot = now;
	q->tslot_start_time = now - do_div(q->horizon.timeslot, q->tslot_len);

	q->req_t = now - q->req_bucketlen;	/* start with full bucket */
	q->ctrl_sock		= NULL;

	/* initialize watchdog */
	qdisc_watchdog_init(&tmp_watchdog, sch);
	qdisc_watchdog_init(&q->watchdog, sch);
	/* hack to get watchdog on realtime clock */
	hrtimer_init(&q->watchdog.timer, CLOCK_REALTIME, HRTIMER_MODE_ABS);
	q->watchdog.timer.function = tmp_watchdog.timer.function;

	/* initialize request timer */
	hrtimer_init(&q->request_timer, CLOCK_REALTIME, HRTIMER_MODE_ABS);
	q->request_timer.function = send_request_timer_func;

	/* initialize tasklet */
	tasklet_init(&q->request_tasklet, &send_request_tasklet,
			(unsigned long int)sch);

	if (opt) {
		err = fp_tc_change(sch, opt);
	} else {
		err = reconnect_ctrl_socket(sch);
		if (!err)
			err = fp_tc_resize(q, q->hash_tbl_log);
	}

	return err;
}

/* dumps configuration of the qdisc to netlink skb (part of qdisc API) */
static int fp_tc_dump(struct Qdisc *sch, struct sk_buff *skb)
{
	struct fp_sched_data *q = qdisc_priv(sch);
	struct nlattr *opts;

	opts = nla_nest_start(skb, TCA_OPTIONS);
	if (opts == NULL)
		goto nla_put_failure;

	if (nla_put_u32(skb, TCA_FASTPASS_PLIMIT, sch->limit) ||
	    nla_put_u32(skb, TCA_FASTPASS_FLOW_PLIMIT, q->flow_plimit) ||
	    nla_put_u32(skb, TCA_FASTPASS_BUCKETS_LOG, q->hash_tbl_log) ||
	    nla_put_u32(skb, TCA_FASTPASS_DATA_RATE, q->data_rate.rate_bytes_ps) ||
	    nla_put_u32(skb, TCA_FASTPASS_TIMESLOT_NSEC, q->tslot_len) ||
	    nla_put_u32(skb, TCA_FASTPASS_REQUEST_COST, q->req_cost) ||
	    nla_put_u32(skb, TCA_FASTPASS_REQUEST_BUCKET, q->req_bucketlen) ||
	    nla_put_u32(skb, TCA_FASTPASS_REQUEST_GAP, q->req_min_gap) ||
	    nla_put_u32(skb, TCA_FASTPASS_CONTROLLER_IP, q->ctrl_addr_netorder) ||
	    nla_put_u32(skb, TCA_FASTPASS_RST_WIN_USEC, q->reset_window_us))
		goto nla_put_failure;

	nla_nest_end(skb, opts);
	return skb->len;

nla_put_failure:
	return -1;
}

/* dumps statistics to netlink skb (part of qdisc API) */
static int fp_tc_dump_stats(struct Qdisc *sch, struct gnet_dump *d)
{
	struct fp_sched_data *q = qdisc_priv(sch);
	u64 now = fp_get_time_ns();
	struct tc_fastpass_qd_stats st = {
		.gc_flows			= q->stat_gc_flows,
		.highprio_packets	= q->stat_internal_packets,
		.flows_plimit		= q->stat_flows_plimit,
		.allocation_errors	= q->stat_allocation_errors,
		.missed_timeslots	= q->stat_missed_timeslots,
		.used_timeslots		= q->stat_used_timeslots,
		.current_timeslot	= q->horizon.timeslot,
		.horizon_mask		= q->horizon.mask,
		.time_next_request	= q->time_next_req - ( ~q->time_next_req ? now : 0),
		.requests			= q->stat_requests,
		.classify_errors	= q->stat_classify_errors,
		.flows				= q->flows,
		.inactive_flows		= q->inactive_flows,
		.unrequested_flows	= q->n_unreq_flows,
		.unrequested_tslots	= q->demand_tslots - q->requested_tslots,
		.req_alloc_errors	= q->stat_req_alloc_errors,
		.non_ctrl_highprio_pkts	= q->stat_non_ctrl_highprio_pkts,
		.socket_tasklet_runs = ~0ULL,
		.socket_build_header_errors = ~0ULL,
		.socket_xmit_errors = ~0ULL,
	};

	/* gather socket statistics */
	if (q->ctrl_sock) {
		struct sock *sk = q->ctrl_sock->sk;
		struct fastpass_sock *fp = fastpass_sk(sk);

		bh_lock_sock(sk);
		st.socket_tasklet_runs = fp->stat_tasklet_runs;
		st.socket_build_header_errors = fp->stat_build_header_errors;
		st.socket_xmit_errors = fp->stat_xmit_errors;
		bh_unlock_sock(sk);
	}

	return gnet_stats_copy_app(d, &st, sizeof(st));
}

static struct Qdisc_ops fastpass_qdisc_ops __read_mostly = {
	.id		=	"fastpass",
	.priv_size	=	sizeof(struct fp_sched_data),

	.enqueue	=	fpq_enqueue,
	.dequeue	=	fpq_dequeue,
	.peek		=	qdisc_peek_dequeued,
	.init		=	fp_tc_init,
	.reset		=	fp_tc_reset,
	.destroy	=	fp_tc_destroy,
	.change		=	fp_tc_change,
	.dump		=	fp_tc_dump,
	.dump_stats	=	fp_tc_dump_stats,
	.owner		=	THIS_MODULE,
};

static int __init fastpass_module_init(void)
{
	int ret = -ENOMEM;

	fp_flow_cachep = kmem_cache_create("fp_flow_cache",
					   sizeof(struct fp_flow),
					   0, 0, NULL);
	if (!fp_flow_cachep)
		goto out;

	ret = register_qdisc(&fastpass_qdisc_ops);
	if (ret)
		goto out_destroy_cache;

	ret = fpproto_register();
	if (ret)
		goto out_unregister_qdisc;

	return 0;

out_unregister_qdisc:
	unregister_qdisc(&fastpass_qdisc_ops);
out_destroy_cache:
	kmem_cache_destroy(fp_flow_cachep);
out:
	return ret;
}

static void __exit fastpass_module_exit(void)
{
	fpproto_unregister(); /* TODO: verify this is safe */
	unregister_qdisc(&fastpass_qdisc_ops);
	kmem_cache_destroy(fp_flow_cachep);
}

module_init(fastpass_module_init)
module_exit(fastpass_module_exit)
MODULE_AUTHOR("Jonathan Perry");
MODULE_LICENSE("GPL");

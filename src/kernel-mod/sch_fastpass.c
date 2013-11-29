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

struct fp_req_ack_info {

};

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

	/* state */
	struct rb_root	*flow_hash_tbl;		/* table of rb-trees of flows */

	struct fp_flow	internal;		/* for non classified or high prio packets */

	struct fp_flow_head unreq_flows; 	/* flows with unscheduled packets */

	u64		tslot_start_time;			/* current time slot start time */
	struct fp_timeslot_horizon	horizon;/* which slots have been allocated */
	struct fp_flow *schedule[FASTPASS_HORIZON];	/* flows scheduled in the next time slots: */
												/* slot x at [x % FASTPASS_HORIZON] */

	u64		req_t;						/* time when request credits = zero */
	u64		time_next_req;				/* time to send next request */
	struct socket	*ctrl_sock;			/* socket to the controller */

	struct qdisc_watchdog watchdog;

	/* counters */
	u32		flows;
	u32		inactive_flows;
	u32		n_unreq_flows;
	u32		unreq_tslots;

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
};

/* special value to mark a flow should not be scheduled */
static struct fp_flow do_not_schedule;

static struct kmem_cache *fp_flow_cachep __read_mostly;

/* advances the horizon 'num_tslots' into the future */
static void horizon_advance(struct fp_timeslot_horizon *h, u32 num_tslots) {
	if (unlikely(num_tslots >= 64))
		h->mask = 0;
	else
		h->mask >>= num_tslots;
	h->timeslot += num_tslots;
}

/* find the first time slot in the future (x>0) that is set, returns -1 if none */
static u32 horizon_future_ffs(struct fp_timeslot_horizon *h) {
	return (h->mask & ~1ULL) ? __ffs64(h->mask & ~1ULL) : -1;
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

void compute_time_next_req(struct fp_sched_data* q, u64 now)
{
	if (q->n_unreq_flows)
		q->time_next_req = max_t(u64, q->req_t + q->req_cost,
									  now + q->req_min_gap);
	else
		q->time_next_req = ~0ULL;
}

/* Set watchdog (assuming there are no packets in q->internal) */
void set_watchdog(struct fp_sched_data* q)
{
	int next_slot;
	u64 next_time;

	BUG_ON(q->internal.qlen); /* we should be throttled */

	next_time = q->time_next_req;
	next_slot = horizon_future_ffs(&q->horizon);
	if (next_slot > 0)
		next_time = min_t(u64, next_time,
				q->tslot_start_time + next_slot * q->tslot_len);

	qdisc_watchdog_schedule_ns(&q->watchdog, next_time);
}

static bool fp_gc_candidate(const struct fp_sched_data *q,
		const struct fp_flow *f)
{
	return f->qlen == 0 &&
	       time_after64(q->horizon.timeslot, f->last_sch_tslot + q->gc_age);
}

static void fp_gc(struct fp_sched_data *q,
		  struct rb_root *root,
		  u64 src_dst_key,
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

static struct fp_flow *fp_classify(struct sk_buff *skb, struct fp_sched_data *q)
{
	struct rb_node **p, *parent;
	struct sock *sk = skb->sk;
	struct rb_root *root;
	struct fp_flow *f;
	int band;
	struct flow_keys keys;
	u64 src_dst_key;
	u32 skb_hash;

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

	/* get the skb's key (src_dst_key) and the key's hash (skb_hash) */
	src_dst_key = ((u64)keys.src) << 32 | keys.dst;
	skb_hash = jhash_2words(keys.src, keys.dst, 0);

	root = &q->flow_hash_tbl[skb_hash >> (32 - q->hash_tbl_log)];

	if (q->flows >= (2U << q->hash_tbl_log) &&
	    q->inactive_flows > q->flows/2)
		fp_gc(q, root, src_dst_key, sk);

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

	/* did not find existing entry, allocate a new one */
	f = kmem_cache_zalloc(fp_flow_cachep, GFP_ATOMIC | __GFP_NOWARN);
	if (unlikely(!f)) {
		q->stat_allocation_errors++;
		return &q->internal;
	}
	f->src_dst_key = src_dst_key;

	rb_link_node(&f->fp_node, parent, p);
	rb_insert_color(&f->fp_node, root);

	q->flows++;
	q->inactive_flows++;
	return f;

cannot_classify:
	// ARP packets should not count as classify errors
	if (unlikely(skb->protocol != ETH_P_ARP))
		q->stat_classify_errors++;
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

static void flow_inc_unrequested(struct fp_sched_data *q,
		struct fp_flow *flow)
{
	if (flow->unreq_tslots == 0) {
		/* flow not on scheduling queue yet, enqueue */
		BUG_ON(flow->next != NULL);
		fp_flow_add_tail(&q->unreq_flows, flow);
		q->n_unreq_flows++;
		/* if enqueued only flow in q->unreq_flows, compute next request time */
		if (q->n_unreq_flows == 1) {
			compute_time_next_req(q, ktime_to_ns(ktime_get()));
			/* if we are throttled, watchdog time might need to change. set it */
			if (q->internal.qlen == 0)
				set_watchdog(q);
		}
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
	s64 cost = (s64) psched_l2t_ns(&q->data_rate, qdisc_pkt_len(skb));

	skb->next = NULL;
	if (!head) {
		flow->head = skb;
		flow->tail = skb;
	} else {
		flow->tail->next = skb;
		flow->tail = skb;
	}

	if (flow->next != &do_not_schedule) {
		/* if credit relates to an old slot, discard it */
		if (unlikely(flow->unreq_tslots == 0 &&
				time_before_eq64(flow->last_sch_tslot, q->horizon.timeslot)))
			flow->credit = 0;

		/* check if need to request a new slot */
		if (cost > flow->credit) {
			flow_inc_unrequested(q, flow);
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

static int fastpass_enqueue(struct sk_buff *skb, struct Qdisc *sch)
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
	struct fp_sched_data *q = qdisc_priv(sch);
	s64 credit = q->tslot_len;
	struct sk_buff *skb;

	/* while enough credit, move packets to q->internal */
	while (src->head) {
		credit -= (s64) psched_l2t_ns(rate, qdisc_pkt_len(src->head));
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

	if(unlikely(time_before64(now, q->tslot_start_time + q->tslot_len)))
		return; /* still in the old time slot */

begin:
	next_slot = horizon_future_ffs(&q->horizon);
	next_slot_start = q->tslot_start_time + next_slot * q->tslot_len;

	/* is current slot an empty slot? */
	if (next_slot <= 0 || time_before64(now, next_slot_start)) {
		/* find the current timeslot's index */
		u64 tslot_advance = now - q->tslot_start_time;
		do_div(tslot_advance, q->tslot_len);

		/* move to the new slot */
		q->tslot_start_time += tslot_advance * q->tslot_len;
		horizon_advance(&q->horizon, tslot_advance);
		return;
	}

	/* advance current time slot to next_slot */
	horizon_advance(&q->horizon, next_slot);
	q->tslot_start_time = next_slot_start;

	/* get the scheduled flow */
	f = q->schedule[q->horizon.timeslot % FASTPASS_HORIZON];
	BUG_ON(f == NULL);

	/* did we encounter a scheduled slot that is in the past */
	if (unlikely(time_after_eq64(now, q->tslot_start_time + q->tslot_len))) {
		flow_inc_unrequested(q, f); /* flow will need to re-request a slot*/
		q->schedule[q->horizon.timeslot % FASTPASS_HORIZON] = NULL;
		q->stat_missed_timeslots++;
		goto begin;
	}

	fp_move_timeslot(sch, &q->data_rate, f, &q->internal);
	q->schedule[q->horizon.timeslot % FASTPASS_HORIZON] = NULL;
	q->stat_used_timeslots++;
}

static void fp_do_request(struct fp_sched_data *q, u64 now)
{
	struct fp_flow *f;
	int empty_slot;

	struct sk_buff *skb;
	const int max_payload_len = 40;
	int max_header;
	int payload_len = 0;
	int err;

	BUG_ON(q->req_t + q->req_cost > now);
	BUG_ON(!q->unreq_flows.first);

	fastpass_pr_debug("fp_do_request start unreq_flows=%u, unreq_tslots=%u\n",
			q->n_unreq_flows, q->unreq_tslots);

	BUG_ON(!q->ctrl_sock);

	/* allocate request skb */
	max_header = q->ctrl_sock->sk->sk_prot->max_header;
	skb = sock_alloc_send_skb(q->ctrl_sock->sk, max_payload_len + max_header, 1, &err);
	if (!skb)
		goto alloc_err;
	skb_reserve(skb, max_header);

	f = q->unreq_flows.first;
	while (payload_len + 4 < max_payload_len && f) {
		u32 dst_addr = ntohl((u32)f->src_dst_key);
		skb_put(skb, 4);
		skb->data[payload_len++] = (dst_addr     ) & 0xFF;
		skb->data[payload_len++] = (dst_addr >> 8) & 0xFF;
		skb->data[payload_len++] = (f->unreq_tslots     ) & 0xFF;
		skb->data[payload_len++] = (f->unreq_tslots >> 8) & 0xFF;
		f = f->next;
	}

	fastpass_send_skb_via_tasklet(q->ctrl_sock->sk, skb);

	while (q->unreq_flows.first && (~q->horizon.mask & ~0xffULL)) {
		f = q->unreq_flows.first;
		empty_slot = __ffs64(~q->horizon.mask & ~0xffULL);
		printk("fp_do_request empty_slot=%d horizon=%llx ~h=%llx, ~1=%llx, &=%llx\n",
				empty_slot, q->horizon.mask, ~q->horizon.mask, ~1ULL, ~q->horizon.mask & ~1ULL);

		/* allocate slot to flow */
		BUG_ON(q->schedule[(q->horizon.timeslot + empty_slot) % FASTPASS_HORIZON]);
		q->schedule[(q->horizon.timeslot + empty_slot) % FASTPASS_HORIZON] = f;
		q->horizon.mask |= (1ULL << empty_slot);
		f->unreq_tslots--;
		q->unreq_tslots--;
		f->last_sch_tslot = max_t(u64, f->last_sch_tslot,
									   q->horizon.timeslot + empty_slot);
		/* if we were receiving an allocation packet here, we would also
		 * make sure that at the end, the queue is unthrottled or the watchdog
		 * is at the correct time */

		if (f->unreq_tslots == 0) {
			/* remove flow from the q->unreq_flows queue */
			q->unreq_flows.first = f->next;
			f->next = NULL;
			q->n_unreq_flows--;
		}
	}

	printk("fp_do_request end unreq_flows=%u, unreq_tslots=%u\n",
			q->n_unreq_flows, q->unreq_tslots);

	q->stat_requests++;

update_credits:
	/* update request credits */
	q->req_t = max_t(u64, q->req_t, now - q->req_bucketlen) + q->req_cost;
	compute_time_next_req(q, now);
	return;

alloc_err:
	q->stat_req_alloc_errors++;
	fastpass_pr_debug("%s: request allocation failed, err %d\n", __func__, err);
	goto update_credits;
}

static struct sk_buff *fastpass_dequeue(struct Qdisc *sch)
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

	/* initiate a request if appropriate */
	if (now >= q->time_next_req)
		fp_do_request(q, now);

	/* if packets were queued for this timeslot, send them. */
	skb = flow_dequeue_skb(sch, &q->internal);
	if (skb)
		goto out;

	/* no packets in queue, go to sleep */
	set_watchdog(q);
	return NULL;

out:
	qdisc_bstats_update(sch, skb);
	qdisc_unthrottled(sch);
	return skb;

}

/* reconnects the control socket to the controller */
static int fastpass_reconnect(struct Qdisc *sch)
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

static void fastpass_reset(struct Qdisc *sch)
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
	for (idx = 0; idx < FASTPASS_HORIZON; idx++)
		q->schedule[idx] = NULL;
	q->horizon.mask = 0ULL;
	q->flows		= 0;
	q->inactive_flows	= 0;
	q->n_unreq_flows = 0;
	q->unreq_tslots	= 0;
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
			skb_hash = jhash_2words((__be32)(of->src_dst_key >> 32),
					(__be32)of->src_dst_key, 0);
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

static int fp_resize(struct fp_sched_data *q, u32 log)
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
		fp_rehash(q, q->flow_hash_tbl, q->hash_tbl_log, array, log);
		kfree(q->flow_hash_tbl);
	}
	q->flow_hash_tbl = array;
	q->hash_tbl_log = log;

	return 0;
}

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
};

static int fastpass_change(struct Qdisc *sch, struct nlattr *opt)
{
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

	if (tb[TCA_FASTPASS_BUCKETS_LOG])
		sch->limit = nla_get_u32(tb[TCA_FASTPASS_BUCKETS_LOG]);

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
	if (tb[TCA_FASTPASS_TIMESLOT_NSEC])
		q->tslot_len = nla_get_u32(tb[TCA_FASTPASS_TIMESLOT_NSEC]);

	if (tb[TCA_FASTPASS_REQUEST_COST])
		q->req_cost = nla_get_u32(tb[TCA_FASTPASS_REQUEST_COST]);

	if (tb[TCA_FASTPASS_REQUEST_BUCKET])
		q->req_bucketlen = nla_get_u32(tb[TCA_FASTPASS_REQUEST_BUCKET]);

	if (tb[TCA_FASTPASS_REQUEST_GAP]) {
		q->req_min_gap = nla_get_u32(tb[TCA_FASTPASS_REQUEST_GAP]);
		q->gc_age = DIV_ROUND_UP(FP_GC_NUM_SECS * NSEC_PER_SEC, q->tslot_len);
	}

	if (tb[TCA_FASTPASS_CONTROLLER_IP]) {
		q->ctrl_addr_netorder = nla_get_u32(tb[TCA_FASTPASS_CONTROLLER_IP]);
		should_reconnect = true;
	}

	if (!err && (should_reconnect || !q->ctrl_sock))
		err = fastpass_reconnect(sch);

	if (!err)
		err = fp_resize(q, fp_log);

	while (sch->q.qlen > sch->limit) {
		struct sk_buff *skb = fastpass_dequeue(sch);

		if (!skb)
			break;
		kfree_skb(skb);
		drop_count++;
	}
	qdisc_tree_decrease_qlen(sch, drop_count);

	sch_tree_unlock(sch);
	return err;
}

static void fastpass_destroy(struct Qdisc *sch)
{
	struct fp_sched_data *q = qdisc_priv(sch);

	fastpass_reset(sch);
	if (q->ctrl_sock)
		sock_release(q->ctrl_sock);
	kfree(q->flow_hash_tbl);
	qdisc_watchdog_cancel(&q->watchdog);
}

static int fastpass_init(struct Qdisc *sch, struct nlattr *opt)
{
	struct fp_sched_data *q = qdisc_priv(sch);
	u64 now = ktime_to_ns(ktime_get());
	struct tc_ratespec data_rate_spec ={
			.linklayer = TC_LINKLAYER_ETHERNET,
			.rate = 1e9/8,
			.overhead = 24};
	int err;

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
	q->flow_hash_tbl	= NULL;
	q->unreq_flows.first= NULL;
	q->internal.next = &do_not_schedule;
	q->time_next_req = ~0ULL;
	q->tslot_start_time = now;			/* timeslot 0 starts now */
	q->req_t = now - q->req_bucketlen;	/* start with full bucket */
	q->ctrl_sock		= NULL;
	qdisc_watchdog_init(&q->watchdog, sch);

	if (opt) {
		err = fastpass_change(sch, opt);
	} else {
		err = fastpass_reconnect(sch);
		if (!err)
			err = fp_resize(q, q->hash_tbl_log);
	}

	return err;
}

static int fastpass_dump(struct Qdisc *sch, struct sk_buff *skb)
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
	    nla_put_u32(skb, TCA_FASTPASS_CONTROLLER_IP, q->ctrl_addr_netorder))
		goto nla_put_failure;

	nla_nest_end(skb, opts);
	return skb->len;

nla_put_failure:
	return -1;
}

static int fastpass_dump_stats(struct Qdisc *sch, struct gnet_dump *d)
{
	struct fp_sched_data *q = qdisc_priv(sch);
	u64 now = ktime_to_ns(ktime_get());
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
		.unrequested_tslots	= q->unreq_tslots,
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

	.enqueue	=	fastpass_enqueue,
	.dequeue	=	fastpass_dequeue,
	.peek		=	qdisc_peek_dequeued,
	.init		=	fastpass_init,
	.reset		=	fastpass_reset,
	.destroy	=	fastpass_destroy,
	.change		=	fastpass_change,
	.dump		=	fastpass_dump,
	.dump_stats	=	fastpass_dump_stats,
	.owner		=	THIS_MODULE,
};

extern void __init fastpass_proto_register(void);

static int __init fastpass_module_init(void)
{
	int ret;

	fp_flow_cachep = kmem_cache_create("fp_flow_cache",
					   sizeof(struct fp_flow),
					   0, 0, NULL);
	if (!fp_flow_cachep)
		return -ENOMEM;

	ret = register_qdisc(&fastpass_qdisc_ops);
	if (ret)
		kmem_cache_destroy(fp_flow_cachep);

	fastpass_proto_register();

	return ret;
}

static void __exit fastpass_module_exit(void)
{
	unregister_qdisc(&fastpass_qdisc_ops);
	kmem_cache_destroy(fp_flow_cachep);
}

module_init(fastpass_module_init)
module_exit(fastpass_module_exit)
MODULE_AUTHOR("Jonathan Perry");
MODULE_LICENSE("GPL");

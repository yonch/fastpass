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
#include <linux/version.h>
#include <linux/ip.h>
#include <net/netlink.h>
#include <net/pkt_sched.h>
#include <net/sock.h>
#include <net/tcp_states.h>
#include <net/sch_generic.h>

#if LINUX_VERSION_CODE == KERNEL_VERSION(3,2,45)
#include "compat-3_2.h"
#endif

#include "fastpass_proto.h"
#include "fp_statistics.h"
#include "../protocol/platform.h"
#include "../protocol/pacer.h"
#include "../protocol/window.h"
#include "../protocol/topology.h"

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

enum {
	FLOW_UNQUEUED,
	FLOW_REQUEST_QUEUE,
	FLOW_RETRANSMIT_QUEUE,
};

/*
 * Per flow structure, dynamically allocated
 */
struct fp_flow {
	u64		src_dst_key;		/* flow identifier */

	struct rb_node	fp_node; 	/* anchor in fp_root[] trees */
	struct list_head	queue_entry; /* entry into one of the request queues */
	uint8_t state;

	/* queued buffers: */
	struct sk_buff	*head;		/* list of skbs for this flow : first skb */
	struct sk_buff *tail;		/* last skb in the list */
	u64		demand_tslots;		/* total needed timeslots */
	u64		requested_tslots;	/* highest requested timeslots */
	u64		acked_tslots;		/* highest requested timeslots that was acked*/
	u64		alloc_tslots;		/* total received allocations */
	int		qlen;				/* number of packets in flow queue */

	s64		credit;				/* time remaining in the last scheduled timeslot */
};

struct fp_timeslot_horizon {
	u64 timeslot;
	u64 mask;
};

/**
 *
 */
struct fp_sched_data {
	/* configuration paramters */
	u32		flow_plimit;				/* max packets per flow */
	u8		hash_tbl_log;				/* log number of hash buckets */
	struct psched_ratecfg data_rate;	/* rate of payload packets */
	u32		tslot_len_approx;					/* duration of a timeslot, in nanosecs */
	u32		req_cost;					/* cost, in tokens, of a request */
	u32		req_bucketlen;				/* the max number of tokens to burst */
	u32		req_min_gap;				/* min delay between requests (ns) */
	__be32	ctrl_addr_netorder;			/* IP of the controller, network byte order */
	u32		reset_window_us;			/* time window of acceptable resets */
	u32		send_timeout_us;			/* when to resend packets */
	u32		tslot_mul;					/* mul to calculate timeslot from nsec */
	u32		tslot_shift;				/* shift to calculate timeslot from nsec */
	u32		miss_threshold;				/* #timeslots that alloc is considered missed */
	u32		max_dev_backlog_ns;			/* max ns that allow a tslot to be enqueued to internal */
	u32		max_preload;				/* how early (#timeslots) can enqueueing be */
	u32		update_timeslot_timer_ns;	/* every how many ns to update queues */

	/* state */
	struct rb_root	*flow_hash_tbl;		/* table of rb-trees of flows */

	struct fp_flow	internal;		/* for non classified or high prio packets */
	u64				internal_free_time; /* approx time when internal will be free */

	struct list_head unreq_flows; 		/* flows with unscheduled packets */
	struct list_head retrans_flows; 	/* flows with a retransmission pending */

	struct fp_window alloc_wnd;
	u64		current_timeslot;
	u64		schedule[(1 << FASTPASS_WND_LOG)];	/* flows scheduled in the next time slots: */

										/* slot x at [x % FASTPASS_HORIZON] */

	struct fp_pacer request_pacer;
	struct socket	*ctrl_sock;			/* socket to the controller */

	struct hrtimer 			timeslot_update_timer;
	struct tasklet_struct 	timeslot_update_tasklet;

	struct hrtimer 			request_timer;
	struct tasklet_struct 	request_tasklet;

	struct hrtimer			retrans_timer;
	struct tasklet_struct 	retrans_tasklet;

	/* counters */
	u32		flows;
	u32		inactive_flows;
	u32		n_unreq_flows;
	u64		demand_tslots;		/* total needed timeslots */
	u64		requested_tslots;	/* highest requested timeslots */
	u64		alloc_tslots;		/* total received allocations */
	u64		acked_tslots;		/* total acknowledged requests */

	/* statistics */
	struct fp_sched_stat stat;
};

static struct kmem_cache *fp_flow_cachep __read_mostly;

static void handle_reset(void *param);


static inline struct fpproto_conn *fpproto_conn(struct fp_sched_data *q)
{
	struct fastpass_sock *fp = (struct fastpass_sock *)q->ctrl_sock->sk;
	return &fp->conn;
}

/* hashes a flow key into a u32, for lookup in the hash tables */
static inline u32 src_dst_key_hash(u64 src_dst_key) {
	return jhash_2words((__be32)(src_dst_key >> 32),
						 (__be32)src_dst_key, 0);
}

/**
 *  computes the time when next request should go out
 *  @returns true if the timer was set, false if it was already set
 */
static inline bool trigger_tx(struct fp_sched_data* q)
{
	/* rate limit sending of requests */
	if (pacer_trigger(&q->request_pacer, fp_monotonic_time_ns())) {
		hrtimer_start(&q->request_timer,
					  ns_to_ktime(pacer_next_event(&q->request_pacer)),
					  HRTIMER_MODE_ABS);
		return true;
	}
	return false;
}

void trigger_tx_voidp(void *param)
{
	struct Qdisc *sch = (struct Qdisc *)param;
	struct fp_sched_data *q = qdisc_priv(sch);
	trigger_tx(q);
}

static int cancel_retrans_timer(void *param)
{
	struct Qdisc *sch = (struct Qdisc *)param;
	struct fp_sched_data *q = qdisc_priv(sch);

	if (unlikely(hrtimer_try_to_cancel(&q->retrans_timer) == -1)) {
		fp_debug("could not cancel timer. tasklet will reset timer\n");
		return -1;
	}

	return 0;
}

static void set_retrans_timer(void *param, u64 when)
{
	struct Qdisc *sch = (struct Qdisc *)param;
	struct fp_sched_data *q = qdisc_priv(sch);

	hrtimer_start(&q->retrans_timer, ns_to_ktime(when), HRTIMER_MODE_ABS);
}

static enum hrtimer_restart retrans_timer_func(struct hrtimer *timer)
{
	struct fp_sched_data *q =
			container_of(timer, struct fp_sched_data, retrans_timer);

	/* schedule tasklet to write request */
	tasklet_schedule(&q->retrans_tasklet);

	return HRTIMER_NORESTART;
}

static void retrans_tasklet(unsigned long int param)
{
	struct Qdisc *sch = (struct Qdisc *)param;
	struct fp_sched_data *q = qdisc_priv(sch);
	u64 now_monotonic = fp_monotonic_time_ns();

	/* Lock the qdisc */
	spinlock_t *root_lock = qdisc_lock(qdisc_root(sch));

	spin_lock_bh(root_lock);

	/* Check that the qdisc destroy func didn't race ahead of us */
	if (unlikely(sch->limit == 0))
		goto qdisc_destroyed;

	fpproto_handle_timeout(fpproto_conn(q), now_monotonic);

cleanup:
	spin_unlock_bh(root_lock);
	return;

qdisc_destroyed:
	FASTPASS_WARN("qdisc seems to have been destroyed\n");
	goto cleanup;
}

/**
 * Called whenever a flow is enqueued for a request or retransmit
 * Caller should hold the qdisc lock.
 */
void req_timer_flowqueue_enqueue(struct fp_sched_data* q)
{
	/* if enqueued first flow in q->unreq_flows, set request timer */
	if (trigger_tx(q))
		fp_debug("set request timer to %llu\n", pacer_next_event(&q->request_pacer));
}

/**
 * Called when just about to send a request.
 * Caller should hold the qdisc lock.
 */
void req_timer_sending_request(struct fp_sched_data *q, u64 now)
{
	/* update request credits */
	pacer_reset(&q->request_pacer);

	/* set timer for next request, if a request would be required */
	if (q->demand_tslots != q->alloc_tslots)
		/* have more requests to send */
		trigger_tx(q);
}

static inline bool flow_in_flowqueue(struct fp_flow *f)
{
	return (f->state != FLOW_UNQUEUED);
}

/**
 * Enqueues flow to the request queue, if it's not already in the retransmit
 *    queue
 */
static void flowqueue_enqueue_request(struct fp_sched_data *q, struct fp_flow *f)
{
	FASTPASS_BUG_ON(f->state == FLOW_REQUEST_QUEUE);

	if (f->state == FLOW_RETRANSMIT_QUEUE)
		return; /* already in queue with higher priority */

	/* enqueue */
	list_add_tail(&f->queue_entry, &q->unreq_flows);
	f->state = FLOW_REQUEST_QUEUE;

	q->n_unreq_flows++;

	/* update request timer if necessary */
	req_timer_flowqueue_enqueue(q);
}

/**
 * enqueues flow into the retransmit queue, dequeueing from the request
 *   if required.
 */
static void flowqueue_enqueue_retransmit(struct fp_sched_data *q, struct fp_flow *f)
{
	if (f->state == FLOW_RETRANSMIT_QUEUE)
		return; /* already in the queue (must be closer to the head) */

	if (f->state == FLOW_REQUEST_QUEUE) {
		/* move flow to the retransmit queue */
		list_move_tail(&f->queue_entry, &q->retrans_flows);
	} else {
		/* wasn't in a queue, enqueue */
		list_add_tail(&f->queue_entry, &q->retrans_flows);
		q->n_unreq_flows++;
	}
	f->state = FLOW_RETRANSMIT_QUEUE;

	/* update request timer if necessary */
	req_timer_flowqueue_enqueue(q);
}

static bool flowqueue_is_empty(struct fp_sched_data* q)
{
	return list_empty(&q->unreq_flows) && list_empty(&q->retrans_flows);
}

static struct fp_flow *flowqueue_dequeue(struct fp_sched_data* q)
{
	struct fp_flow *f;

	/* get entry */
	if (unlikely(!list_empty(&q->retrans_flows))) {
		/* got retransmit flow */
		f = list_first_entry(&q->retrans_flows, struct fp_flow, queue_entry);
	} else if (likely(!list_empty(&q->unreq_flows))) {
		/* got regular request */
		f = list_first_entry(&q->unreq_flows, struct fp_flow, queue_entry);
	} else {
		BUG();
	}
	/* remove it from queue */
	list_del(&f->queue_entry);
	f->state = FLOW_UNQUEUED;

	/* update counter */
	q->n_unreq_flows--;

	return f;
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
		q->stat.allocation_errors++;
		return &q->internal;
	}
	f->src_dst_key = src_dst_key;

	rb_link_node(&f->fp_node, parent, p);
	rb_insert_color(&f->fp_node, root);
	f->state = FLOW_UNQUEUED;

	q->flows++;
	q->inactive_flows++;
	return f;
}

/* returns the flow for the given packet, allocates a new flow if needed */
static struct fp_flow *fpq_classify(struct sk_buff *skb, struct fp_sched_data *q)
{
	__be16 proto = skb->protocol;
	int nhoff = skb_network_offset(skb);
	int band;
	struct flow_keys keys;
	u64 src_dst_key;
	struct ethhdr *ethh;
	u64 masked_mac;

	/* warning: no starvation prevention... */
	band = prio2band[skb->priority & TC_PRIO_MAX];
	if (unlikely(band == 0)) {
		if (unlikely(skb->sk != q->ctrl_sock->sk))
			q->stat.non_ctrl_highprio_pkts++;
		else
			q->stat.ctrl_pkts++;
		return &q->internal;
	}

	/* special cases for PTP and NTP over IP */
	if (proto == __constant_htons(ETH_P_IP)) {
		const struct iphdr *iph;
		struct iphdr _iph;

		iph = skb_header_pointer(skb, nhoff, sizeof(_iph), &_iph);
		if (!iph || iph->ihl < 5)
			goto cannot_classify;

		if (!skb_flow_dissect(skb, &keys))
			goto cannot_classify;
		/* special case for important packets, let through with high priority */
		if (unlikely(keys.ip_proto == IPPROTO_UDP)) {
			/* NTP packets */
			if (unlikely(keys.port16[1] == htons(123))) {
				q->stat.ntp_pkts++;
				return &q->internal;
			}
			/* PTP packets are port 319,320 */
			if (unlikely(((ntohs(keys.port16[1]) - 1) & ~1) == 318)) {
				q->stat.ptp_pkts++;
				return &q->internal;
			}
		}
	}

	/* get MAC address */
	ethh = (struct ethhdr *)skb_mac_header(skb);
	src_dst_key = ((u64)ntohs(*(__be16 *)&ethh->h_dest[0]) << 32)
				 | ntohl(*(__be32 *)&ethh->h_dest[2]);
	FASTPASS_WARN("got ethernet %02X:%02X:%02X:%02X:%02X:%02X parsed 0x%012llX\n",
			ethh->h_dest[0],ethh->h_dest[1],ethh->h_dest[2],ethh->h_dest[3],
			ethh->h_dest[4],ethh->h_dest[5], src_dst_key);

	/* Special case the PTP broadcasts: MAC 01:1b:19:00:00:00 */
	if (unlikely(src_dst_key == 0x011b19000000)) {
		q->stat.ptp_pkts++;
		return &q->internal;
	}

	/* get the skb's key (src_dst_key) */
	masked_mac = src_dst_key & MANUFACTURER_MAC_MASK;
	if (unlikely((masked_mac == VRRP_SWITCH_MAC_PREFIX)
			  || (masked_mac == CISCO_SWITCH_MAC_PREFIX)))
		src_dst_key = OUT_OF_BOUNDARY_NODE_ID;
	else
		src_dst_key = fp_map_mac_to_id(src_dst_key);

	q->stat.data_pkts++;
	return fpq_lookup(q, src_dst_key, true);

cannot_classify:
	// ARP packets should not count as classify errors
	if (unlikely(skb->protocol != htons(ETH_P_ARP))) {
		q->stat.classify_errors++;
		fp_debug("cannot classify packet with protocol %u:\n", skb->protocol);
		print_hex_dump(KERN_DEBUG, "cannot classify: ", DUMP_PREFIX_OFFSET,
				16, 1, skb->data, min_t(size_t, skb->len, 64), false);
	} else {
		q->stat.arp_pkts++;
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

void flow_inc_alloc(struct fp_sched_data* q, struct fp_flow* f)
{
	if (unlikely(f->alloc_tslots == f->demand_tslots)) {
		fp_debug("got an allocation over demand, flow 0x%04llX, demand %llu\n",
				f->src_dst_key, f->demand_tslots);
		q->stat.unwanted_alloc++;
		return;
	}

	f->alloc_tslots++;
	q->alloc_tslots++;
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

	if (!flow_in_flowqueue(f))
		/* flow not on scheduling queue yet, enqueue */
		flowqueue_enqueue_request(q, f);
}

/* add skb to flow queue. returns false if skb is too large*/
static bool flow_enqueue_skb(struct Qdisc *sch, struct fp_flow *flow,
		struct sk_buff *skb)
{
	struct fp_sched_data *q = qdisc_priv(sch);
	struct sk_buff *head = flow->head;
	s64 cost = (s64) psched_l2t_ns(&q->data_rate, qdisc_pkt_len(skb));

	if (cost > q->tslot_len_approx)
		return false;

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
			flow->credit = q->tslot_len_approx;
		}
		flow->credit -= cost;
	} else {
		q->internal_free_time += cost;
	}

	/* if queue was empty before, decrease inactive flow count */
	if (flow->qlen == 0)
		q->inactive_flows--;

	flow->qlen++;
	sch->q.qlen++;
	sch->qstats.backlog += qdisc_pkt_len(skb);
	return true;
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

	/* internal queue flows without scheduling */
	if (unlikely(f == &q->internal)) {
		/* fix internal_free_time if there hasn't been enqueues in a while */
		u64 now_monotonic = fp_monotonic_time_ns();
		q->internal_free_time = max_t(u64, q->internal_free_time, now_monotonic);

		/* unthrottle qdisc */
		qdisc_unthrottled(sch);
	} else if (unlikely(f->qlen >= q->flow_plimit)) {
		/* enforce flow packet limit */
		q->stat.flows_plimit++;
		return qdisc_drop(skb, sch);
	}

	/* queue skb to flow, update statistics */
	if (!flow_enqueue_skb(sch, f, skb)) {
		FASTPASS_WARN("got packet that is larger than a timeslot len=%d, flow 0x%llX\n",
			qdisc_pkt_len(skb), f->src_dst_key);
		q->stat.pkt_too_big++;
		return qdisc_drop(skb, sch);
	}
	fp_debug("enqueued packet of len %d to flow 0x%llX, qlen=%d\n",
			qdisc_pkt_len(skb), f->src_dst_key, f->qlen);


	return NET_XMIT_SUCCESS;
}

/**
 * Move a timeslot's worth of skb's from src flow to dst flow, assuming the
 *    packets follow 'rate' rate.
 */
static void move_timeslot_from_flow(struct Qdisc *sch, struct psched_ratecfg *rate,
		struct fp_flow *src, struct fp_flow *dst, u16 path)
{
	struct fp_sched_data *q = qdisc_priv(sch);
	s64 credit = q->tslot_len_approx;
	struct sk_buff *skb;
	u32 count = 0;

	/* while enough credit, move packets to q->internal */
	while (!flow_has_skbs(src)) {
		u32 skb_len = qdisc_pkt_len(flow_queue_peek(src));
		u64 skb_ns = psched_l2t_ns(rate, skb_len);

		fp_debug("credit=%llu, pkt_len=%u, cost=%llu\n",
				credit, skb_len, skb_ns);
		credit -= (s64) skb_ns;
		if (credit < 0)
			break; /* ran out of credit */

		skb = flow_dequeue_skb(sch, src);
		flow_enqueue_skb(sch, dst, skb);
		count++;
	}

	fp_debug("@%llu moved %u out of %u packets from %llu (0x%04llx) path %d\n",
			q->current_timeslot, count, src->qlen + count, src->src_dst_key,
			src->src_dst_key, path);
}

/**
 * Handles cases where a flow was allocated but we cannot fulfill it, either
 *    because the time has passed or is too far in the future.
 */
void handle_out_of_bounds_allocation(struct Qdisc *sch, u64 src_dst_key)
{
	struct fp_sched_data *q = qdisc_priv(sch);
	struct fp_flow *f = fpq_lookup(q, fp_alloc_node(src_dst_key), false);

	if (f == NULL) {
		/*
		 * Couldn't find the flow. The allocation was either for an invalid
		 *    destination, or was not needed and the flow was garbage-collected.
		 */
		q->stat.flow_not_found_oob++;
		fp_debug("could not find flow with key 0x%llX node 0x%X will force reset\n",
				src_dst_key, fp_alloc_node(src_dst_key));
		/* This corrupts the status; will force a reset */
		fpproto_force_reset(fpproto_conn(q));
		handle_reset((void *)sch); /* manually call callback since fpproto won't call it */
		return;
	}

	if (f->alloc_tslots == f->demand_tslots) {
		q->stat.unwanted_out_of_bounds++;
		fp_debug("got an out-of-bound packet for flow 0x%llx node 0x%X but demand=alloc=%lu, so will discard\n",
				src_dst_key, fp_alloc_node(src_dst_key), f->demand_tslots);
		return;
	}

	/* flow will need to re-request a slot*/
	flow_inc_demand(q, f);
	/* we mark that we were allocated this timeslot */
	flow_inc_alloc(q, f);
}

/**
 * Change the qdisc state from its old time slot to the time slot at time @now.
 *
 * At the end of the function, the window tail (edge) will be at
 *    cur_tslot - FASTPASS_MAX_PAST_SLOTS, and all timelots before cur_tslot
 *    will be unmarked.
 */
static void update_current_timeslot(struct Qdisc *sch, u64 now_real)
{
	struct fp_sched_data *q = qdisc_priv(sch);
	u64 next_nonempty;
	u64 next_key;
	struct fp_flow *f;
	u64 tslot_advance;
	u32 moved_timeslots = 0;
	u64 now_monotonic = fp_monotonic_time_ns();

	q->current_timeslot = (now_real * q->tslot_mul) >> q->tslot_shift;
	q->internal_free_time = max_t(u64, q->internal_free_time, now_monotonic);


begin:
	if (unlikely(wnd_empty(&q->alloc_wnd)))
		goto done;

	next_nonempty = wnd_earliest_marked(&q->alloc_wnd);
	next_key = q->schedule[wnd_pos(next_nonempty)];

	/* is this alloc too far in the future? */
	if (unlikely(time_after64(next_nonempty, q->current_timeslot + q->max_preload)))
		goto done; /* won't move it */

	/* is alloc too far in the past? */
	if (unlikely(time_before64(next_nonempty, q->current_timeslot - q->miss_threshold))) {
		q->stat.missed_timeslots++;
		fp_debug("missed timeslot %llu by %llu timeslots, rescheduling\n",
				next_nonempty, q->current_timeslot - next_nonempty);
		handle_out_of_bounds_allocation(sch, next_key);
		wnd_clear(&q->alloc_wnd, next_nonempty);
		goto begin;
	}

	if (time_after64(q->internal_free_time, now_monotonic + q->max_dev_backlog_ns)) {
		/* queue full, cannot move flow */
		if (next_nonempty > q->current_timeslot)
			goto done; /* maybe later */

		/* timeslots are late and backlog is full. will reschedule */
		q->stat.backlog_too_high++;
		fp_debug("backlog too high processing timeslot %llu at %llu, rescheduling key 0x%llX\n",
				next_nonempty, q->current_timeslot, next_key);
		handle_out_of_bounds_allocation(sch, next_key);
		wnd_clear(&q->alloc_wnd, next_nonempty);
		goto begin;
	}

	/* Okay can move timeslot! */
	f = fpq_lookup(q, fp_alloc_node(next_key), false);
	if (unlikely(f == NULL)) {
		fp_debug("could not find flow for allocation at timeslot %llu key 0x%llX node 0x%X will force reset\n",
				next_nonempty, next_key, fp_alloc_node(next_key));
		q->stat.flow_not_found_update++;
		/* This corrupts the status; will force a reset */
		fpproto_force_reset(fpproto_conn(q));
		handle_reset((void *)sch); /* manually call callback since fpproto won't call it */
		return;
	}

	move_timeslot_from_flow(sch, &q->data_rate, f, &q->internal,
			fp_alloc_path(next_key));
	wnd_clear(&q->alloc_wnd, next_nonempty);
	flow_inc_alloc(q,f);
	moved_timeslots++;

	/* statistics */
	q->stat.used_timeslots++;
	if (next_nonempty < q->current_timeslot)
		q->stat.late_enqueue++;
	if (next_nonempty > q->current_timeslot)
		q->stat.early_enqueue++;

	goto begin; /* try another timeslot */

done:
	/* update window around current timeslot */
	tslot_advance = q->current_timeslot + FASTPASS_WND_LEN - 1 - q->miss_threshold - wnd_head(&q->alloc_wnd);
	if (unlikely((s64)tslot_advance < 0)) {
		if ((s64)tslot_advance < -4)
			FASTPASS_WARN("current timeslot moved back a lot: %lld timeslots. new current %llu\n",
					(s64)tslot_advance, q->current_timeslot);
		else
			fp_debug("current timeslot moved back a little: %lld timeslots. new current %llu\n",
					(s64)tslot_advance, q->current_timeslot);
	} else {
		/* tslot advance is non-negative, can call advance */
		wnd_advance(&q->alloc_wnd, tslot_advance);
		fp_debug("moved by %llu timeslots to empty timeslot %llu, now tslot %llu\n",
				tslot_advance, wnd_head(&q->alloc_wnd), q->current_timeslot);
	}

	/* schedule transmission */
	if (moved_timeslots > 0) {
		fp_debug("moved timeslots, unthrottling qdisc\n");
		qdisc_unthrottled(sch);
		__netif_schedule(qdisc_root(sch));
	}

	/* set timer to update timeslots */
	if (!wnd_empty(&q->alloc_wnd)) {
		hrtimer_start(&q->timeslot_update_timer,
				ns_to_ktime(q->update_timeslot_timer_ns), HRTIMER_MODE_REL);
	}
}

/**
 * Performs a reset and garbage collection of flows
 */
static void handle_reset(void *param)
{
	struct Qdisc *sch = (struct Qdisc *)param;
	struct fp_sched_data *q = qdisc_priv(sch);

	struct rb_node *cur, *next;
	struct rb_root *root;
	struct fp_flow *f;
	u32 idx;
	u32 base_idx = src_dst_key_hash(fp_monotonic_time_ns()) >> (32 - q->hash_tbl_log);
	u32 mask = (1U << q->hash_tbl_log) - 1;

	q->flows = 0;
	q->inactive_flows = 0;		/* will remain 0 when we're done */
	q->demand_tslots = 0;
	q->requested_tslots = 0;	/* will remain 0 when we're done */
	q->alloc_tslots = 0;		/* will remain 0 when we're done */
	q->acked_tslots = 0; 		/* will remain 0 when we're done */

	/* for each cell in hash table: */
	for (idx = 0; idx < (1U << q->hash_tbl_log); idx++) {
		root = &q->flow_hash_tbl[(idx + base_idx) & mask];
		next = rb_first(root); /* we traverse tree in-order */

		/* while haven't finished traversing rbtree: */
		while (next != NULL) {
			cur = next;
			next = rb_next(cur);

			f = container_of(cur, struct fp_flow, fp_node);

			/* can we garbage-collect this flow? */
			if (f->demand_tslots == f->alloc_tslots) {
				/* yes, let's gc */
				FASTPASS_BUG_ON(f->qlen != 0);
				FASTPASS_BUG_ON(f->state != FLOW_UNQUEUED);
				/* erase from old tree */
				rb_erase(cur, root);
				fp_debug("gc flow 0x%04llX, used %llu timeslots\n",
						f->src_dst_key, f->demand_tslots);
				q->stat.gc_flows++;
				continue;
			}

			/* has timeslots pending, rebase counters to 0 */
			f->demand_tslots -= f->alloc_tslots;
			f->alloc_tslots = 0;
			f->acked_tslots = 0;
			f->requested_tslots = 0;

			q->flows++;
			q->demand_tslots += f->demand_tslots;

			fp_debug("rebased flow 0x%04llX, new demand %llu timeslots\n",
					f->src_dst_key, f->demand_tslots);

			/* add flow to request queue if it's not already there */
			if (f->state == FLOW_UNQUEUED)
				flowqueue_enqueue_request(q, f);
		}
	}
}

/**
 * Handles an ALLOC payload
 */
static void handle_alloc(void *param, u32 base_tslot, u16 *dst,
		int n_dst, u8 *tslots, int n_tslots)
{
	struct Qdisc *sch = (struct Qdisc *)param;
	struct fp_sched_data *q = qdisc_priv(sch);
	int i;
	u8 spec;
	int dst_ind;
	u64 full_tslot;
	u64 now_real = fp_get_time_ns();

	/* every alloc should be ACKed */
	trigger_tx(q);

	update_current_timeslot(sch, now_real);
	full_tslot = q->current_timeslot - (1ULL << 18); /* 1/4 back, 3/4 front */
	full_tslot += ((u32)base_tslot - (u32)full_tslot) & 0xFFFFF; /* 20 bits */

	fp_debug("got ALLOC for timeslot %d (full %llu, current %llu), %d destinations, %d timeslots, mask 0x%016llX\n",
			base_tslot, full_tslot, q->current_timeslot, n_dst, n_tslots,
			wnd_get_mask(&q->alloc_wnd, q->current_timeslot+63));

	for (i = 0; i < n_tslots; i++) {
		spec = tslots[i];
		dst_ind = spec >> 4;

		if (dst_ind == 0) {
			/* Skip instruction */
			base_tslot += 16 * (1 + (spec & 0xF));
			full_tslot += 16 * (1 + (spec & 0xF));
			fp_debug("ALLOC skip to timeslot %d full %llu (no allocation)\n",
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
		fp_debug("Timeslot %d (full %llu) to destination 0x%04x (%d)\n",
				base_tslot, full_tslot, dst[dst_ind - 1], dst[dst_ind - 1]);

		if (unlikely(wnd_seq_before(&q->alloc_wnd, full_tslot))) {
			q->stat.alloc_too_late++;
			handle_out_of_bounds_allocation(sch, dst[dst_ind - 1]);
			fp_debug("-X- already gone, will reschedule\n");
			continue;
		}

		if (unlikely(wnd_seq_after(&q->alloc_wnd, full_tslot))) {
			q->stat.alloc_premature++;
			handle_out_of_bounds_allocation(sch, dst[dst_ind - 1]);
			fp_debug("-X- too futuristic, will reschedule\n");
			continue;
		}

		/* sanity check */
		if (wnd_is_marked(&q->alloc_wnd, full_tslot)) {
			FASTPASS_WARN("got ALLOC tslot %llu dst 0x%X but but it was already marked for 0x%llX current_tslot %llu base %u now_real %llu\n",
					full_tslot, dst[dst_ind - 1], q->schedule[wnd_pos(full_tslot)],
					q->current_timeslot, base_tslot, now_real);
			continue;
		}

		/* okay, allocate */
		wnd_mark(&q->alloc_wnd, full_tslot);
		q->schedule[wnd_pos(full_tslot)] = dst[dst_ind - 1];
	}

	fp_debug("mask after: 0x%016llX\n",
			wnd_get_mask(&q->alloc_wnd, q->current_timeslot+63));

	/* enqueue latest timeslots if possible */
	update_current_timeslot(sch, now_real);
}

static void handle_ack(void *param, struct fpproto_pktdesc *pd)
{
	struct Qdisc *sch = (struct Qdisc *)param;
	struct fp_sched_data *q = qdisc_priv(sch);
	int i;
	struct fp_flow *f;
	u64 new_acked;
	u64 delta;

	for (i = 0; i < pd->n_areq; i++) {
		f = fpq_lookup(q, pd->areq[i].src_dst_key, false);
		if (unlikely(f == NULL)) {
			FASTPASS_CRIT("could not find flow - known destruction race\n");
			break;
		}
		new_acked = pd->areq[i].tslots;
		if (f->acked_tslots < new_acked) {
			FASTPASS_BUG_ON(new_acked > f->demand_tslots);
			delta = new_acked - f->acked_tslots;
			q->acked_tslots += delta;
			f->acked_tslots = new_acked;
			fp_debug("acked request of %llu additional slots, flow 0x%04llX, total %llu slots\n",
					delta, f->src_dst_key, new_acked);

			/* the demand-limiting window might be in effect, re-enqueue flow */
			if (unlikely((!flow_in_flowqueue(f))
					&& (f->requested_tslots != f->demand_tslots)))
				flowqueue_enqueue_request(q, f);
		}
	}
	fpproto_pktdesc_free(pd);
}

static void handle_neg_ack(void *param, struct fpproto_pktdesc *pd)
{
	struct Qdisc *sch = (struct Qdisc *)param;
	struct fp_sched_data *q = qdisc_priv(sch);
	int i;
	struct fp_flow *f;
	u64 req_tslots;

	for (i = 0; i < pd->n_areq; i++) {
		f = fpq_lookup(q, pd->areq[i].src_dst_key, false);
		if (unlikely(f == NULL)) {
			FASTPASS_CRIT("could not find flow - known destruction race\n");
			break;
		}

		req_tslots = pd->areq[i].tslots;
		/* don't need to resend if got ack >= req_tslots */
		if (req_tslots <= f->acked_tslots) {
			fp_debug("nack for request of %llu for flow 0x%04llX, but already acked %llu\n",
							req_tslots, f->src_dst_key, f->acked_tslots);
			continue;
		}

		/* don't need to re-add if already in queue */
		if (unlikely(f->state == FLOW_RETRANSMIT_QUEUE)) {
			fp_debug("nack for flow 0x%04llX, already queued for retransmission\n",
					f->src_dst_key);
			continue;
		}

		/* add to retransmit queue */
		flowqueue_enqueue_retransmit(q, f);
		fp_debug("nack for request of %llu for flow 0x%04llX (%llu acked), added to retransmit queue\n",
						req_tslots, f->src_dst_key, f->acked_tslots);
	}
	fpproto_pktdesc_free(pd);
}

/**
 * Send a request packet to the controller
 */
static void send_request(struct Qdisc *sch)
{
	struct fp_sched_data *q = qdisc_priv(sch);
	spinlock_t *root_lock = qdisc_lock(qdisc_root(sch));
	u64 now_monotonic = fp_monotonic_time_ns();

	struct fp_flow *f;
	struct fpproto_pktdesc *pkt;
	u64 new_requested;

	spin_lock_bh(root_lock);

	/* Check that the qdisc destroy func didn't race ahead of us */
	if (unlikely(sch->limit == 0)) {
		FASTPASS_WARN("qdisc seems to have been destroyed\n");
		spin_unlock_bh(root_lock);
		return;
	}

	fp_debug(
			"start: unreq_flows=%u, unreq_tslots=%llu, now_mono=%llu, scheduled=%llu, diff=%lld, next_seq=%08llX\n",
			q->n_unreq_flows, q->demand_tslots - q->requested_tslots, now_monotonic,
			pacer_next_event(&q->request_pacer),
			(s64 )now_monotonic - (s64 )pacer_next_event(&q->request_pacer),
			fpproto_conn(q)->next_seqno);

	WARN(q->request_pacer.T + q->request_pacer.cost > now_monotonic,
			"send_request called too early T=%llu, req_cost=%u, now_mono=%llu (diff=%lld)\n",
			q->request_pacer.T, q->request_pacer.cost, now_monotonic,
			(s64)now_monotonic - (s64)(q->request_pacer.T + q->request_pacer.cost));
	if(flowqueue_is_empty(q)) {
		q->stat.request_with_empty_flowqueue++;
		fp_debug("was called with no flows pending (could be due to bad packets?)\n");
	}
	FASTPASS_BUG_ON(!q->ctrl_sock);

	/* allocate packet descriptor */
	pkt = fpproto_pktdesc_alloc();
	if (!pkt)
		goto alloc_err;

	/**
	 * this might NACK a packet, but the request timer will not be set because
	 *   already flow queue is not empty
	 */
	fpproto_prepare_to_send(fpproto_conn(q));

	pkt->n_areq = 0;
	while ((pkt->n_areq < FASTPASS_PKT_MAX_AREQ) && !flowqueue_is_empty(q)) {
		f = flowqueue_dequeue(q);

		new_requested = min_t(u64, f->demand_tslots,
				f->acked_tslots + FASTPASS_REQUEST_WINDOW_SIZE - 1);
		if(new_requested <= f->acked_tslots) {
			q->stat.queued_flow_already_acked++;
			fp_debug("flow 0x%04llX was in queue, but already fully acked\n",
					f->src_dst_key);
			continue;
		}

		pkt->areq[pkt->n_areq].src_dst_key = f->src_dst_key;
		pkt->areq[pkt->n_areq].tslots = new_requested;

		q->requested_tslots += (new_requested - f->requested_tslots);
		f->requested_tslots = new_requested;

		pkt->n_areq++;
	}

	fp_debug("end: unreq_flows=%u, unreq_tslots=%llu\n",
			q->n_unreq_flows, q->demand_tslots - q->requested_tslots);

	fpproto_commit_packet(fpproto_conn(q), pkt, now_monotonic);

out:
	req_timer_sending_request(q, now_monotonic);

	if (likely(pkt != NULL)) {
		struct sock *sk = q->ctrl_sock->sk;
		struct sk_buff *skb;

		/* make the skb -- keep the lock here so the pkt_desc doesn't timeout
		 * underneath us */
		skb = fpproto_make_skb(sk, pkt);

		/* unlock the qdisc so packets could traverse the stack */
		spin_unlock_bh(root_lock);

		/* send the packets */
		if (likely(skb != NULL))
			fpproto_send_skb(sk, skb);
	} else {
		/* remember to unlock even when not sending packets */
		spin_unlock_bh(root_lock);
	}

	/* caution: no qdisc locked held at this point */

	return;

alloc_err:
	q->stat.req_alloc_errors++;
	fp_debug("request allocation failed\n");
	goto out;
}

static enum hrtimer_restart timeslot_update_timer_func(struct hrtimer *timer)
{
	struct fp_sched_data *q =
			container_of(timer, struct fp_sched_data, timeslot_update_timer);

	/* schedule tasklet to write request */
	tasklet_schedule(&q->timeslot_update_tasklet);

	return HRTIMER_NORESTART;
}

static void timeslot_update_tasklet_func(unsigned long int param)
{
	struct Qdisc *sch = (struct Qdisc *)param;
	u64 now_real = fp_get_time_ns();

	/* Lock the qdisc */
	spinlock_t *root_lock = qdisc_lock(qdisc_root(sch));

	spin_lock_bh(root_lock);

	/* Check that the qdisc destroy func didn't race ahead of us */
	if (unlikely(sch->limit == 0))
		goto qdisc_destroyed;

	update_current_timeslot(sch, now_real);

cleanup:
	spin_unlock_bh(root_lock);
	return;

qdisc_destroyed:
	FASTPASS_WARN("qdisc seems to have been destroyed\n");
	goto cleanup;
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

	send_request(sch);
}

/* Extract packet from the queue (part of the qdisc API) */
static struct sk_buff *fpq_dequeue(struct Qdisc *sch)
{
	struct fp_sched_data *q = qdisc_priv(sch);
	struct sk_buff *skb;

	/* any packets already queued? */
	skb = flow_dequeue_skb(sch, &q->internal);
	if (skb)
		goto out_got_skb;

	/* no packets in queue, go to sleep */
	qdisc_throttled(sch);
	return NULL;

out_got_skb:
	qdisc_bstats_update(sch, skb);
	qdisc_unthrottled(sch);
	return skb;
}

struct fpproto_ops fastpass_sch_proto_ops = {
	.handle_reset	= &handle_reset,
	.handle_alloc	= &handle_alloc,
	.handle_ack		= &handle_ack,
	.handle_neg_ack	= &handle_neg_ack,
	.trigger_request= &trigger_tx_voidp,
	.set_timer		= &set_retrans_timer,
	.cancel_timer	= &cancel_retrans_timer,
};

/* reconnects the control socket to the controller */
static int reconnect_ctrl_socket(struct Qdisc *sch)
{
	struct fp_sched_data *q = qdisc_priv(sch);
	struct sock *sk;
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

	sk = q->ctrl_sock->sk;

	FASTPASS_BUG_ON(sk->sk_priority != TC_PRIO_CONTROL);
	FASTPASS_BUG_ON(sk->sk_allocation != GFP_ATOMIC);

	/* give socket a reference to this qdisc for watchdog */
	fpproto_set_qdisc(sk, sch);

	/* initialize the fastpass protocol */
	fpproto_init_conn(fpproto_conn(q), &fastpass_sch_proto_ops, (void *)sch,
			(u64)q->reset_window_us * NSEC_PER_USEC,
			q->send_timeout_us);

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
	INIT_LIST_HEAD(&q->unreq_flows);
	INIT_LIST_HEAD(&q->retrans_flows);
	wnd_reset(&q->alloc_wnd, q->current_timeslot);
	q->flows		= 0;
	q->inactive_flows	= 0;
	q->n_unreq_flows	= 0;
	q->demand_tslots	= 0;
	q->requested_tslots	= 0;
	q->alloc_tslots		= 0;
	q->internal_free_time = fp_monotonic_time_ns();
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
	u32 idx;
	u32 skb_hash;

	/* for each cell in hash table: */
	for (idx = 0; idx < (1U << old_log); idx++) {
		oroot = &old_array[idx];
		/* while rbtree not empty: */
		while ((op = rb_first(oroot)) != NULL) {
			/* erase from old tree */
			rb_erase(op, oroot);
			/* find new cell in hash table */
			of = container_of(op, struct fp_flow, fp_node);
			skb_hash = src_dst_key_hash(of->src_dst_key);
			nroot = &new_array[skb_hash >> (32 - new_log)];

			/* insert in tree */
			np = &nroot->rb_node;
			parent = NULL;
			while (*np) {
				parent = *np;

				nf = container_of(parent, struct fp_flow, fp_node);
				FASTPASS_BUG_ON(nf->src_dst_key == of->src_dst_key);

				if (nf->src_dst_key > of->src_dst_key)
					np = &parent->rb_right;
				else
					np = &parent->rb_left;
			}

			rb_link_node(&of->fp_node, parent, np);
			rb_insert_color(&of->fp_node, nroot);
		}
	}
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
	[TCA_FASTPASS_TIMESLOT_MUL]		= { .type = NLA_U32 },
	[TCA_FASTPASS_TIMESLOT_SHIFT]	= { .type = NLA_U32 },
	[TCA_FASTPASS_MISS_THRESHOLD]	= { .type = NLA_U32 },
	[TCA_FASTPASS_DEV_BACKLOG_NS]	= { .type = NLA_U32 },
	[TCA_FASTPASS_MAX_PRELOAD]		= { .type = NLA_U32 },
	[TCA_FASTPASS_UPDATE_TIMESLOT_TIMER_NS]		= { .type = NLA_U32 },
};

/* change configuration (part of qdisc API) */
static int fp_tc_change(struct Qdisc *sch, struct nlattr *opt) {
	struct fp_sched_data *q = qdisc_priv(sch);
	struct nlattr *tb[TCA_FASTPASS_MAX + 1];
	int err, drop_count = 0;
	u32 fp_log;
	bool should_reconnect = false;
	bool changed_pacer = false;
	u32 tslot_mul = q->tslot_mul;
	u32 tslot_shift = q->tslot_shift;
	bool changed_tslot_len = false;
	struct tc_ratespec data_rate_spec ={
#if LINUX_VERSION_CODE != KERNEL_VERSION(3,2,45)
			.linklayer = TC_LINKLAYER_ETHERNET,
#endif
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
		FASTPASS_WARN("got deprecated timeslot length paramter\n");
		err = -EINVAL;
	}
	if (tb[TCA_FASTPASS_TIMESLOT_MUL]) {
		tslot_mul = nla_get_u32(tb[TCA_FASTPASS_TIMESLOT_MUL]);
		changed_tslot_len = true;
		if (tslot_mul == 0)
			err = -EINVAL;
	}
	if (tb[TCA_FASTPASS_TIMESLOT_SHIFT]) {
		tslot_shift = nla_get_u32(tb[TCA_FASTPASS_TIMESLOT_SHIFT]);
		changed_tslot_len = true;
	}

	if (tb[TCA_FASTPASS_REQUEST_COST]) {
		q->req_cost = nla_get_u32(tb[TCA_FASTPASS_REQUEST_COST]);
		changed_pacer = true;
	}

	if (tb[TCA_FASTPASS_REQUEST_BUCKET]) {
		q->req_bucketlen = nla_get_u32(tb[TCA_FASTPASS_REQUEST_BUCKET]);
		changed_pacer = true;
	}

	if (tb[TCA_FASTPASS_REQUEST_GAP]) {
		q->req_min_gap = nla_get_u32(tb[TCA_FASTPASS_REQUEST_GAP]);
		changed_pacer = true;
	}

	if (tb[TCA_FASTPASS_CONTROLLER_IP]) {
		q->ctrl_addr_netorder = nla_get_u32(tb[TCA_FASTPASS_CONTROLLER_IP]);
		should_reconnect = true;
	}

	if (tb[TCA_FASTPASS_RST_WIN_USEC]) {
		q->reset_window_us = nla_get_u32(tb[TCA_FASTPASS_RST_WIN_USEC]);
		should_reconnect = true;
	}

	if (tb[TCA_FASTPASS_MISS_THRESHOLD]) {
		q->miss_threshold = nla_get_u32(tb[TCA_FASTPASS_MISS_THRESHOLD]);
	}

	if (tb[TCA_FASTPASS_DEV_BACKLOG_NS]) {
		q->max_dev_backlog_ns = nla_get_u32(tb[TCA_FASTPASS_DEV_BACKLOG_NS]);
	}

	if (tb[TCA_FASTPASS_MAX_PRELOAD]) {
		q->max_preload = nla_get_u32(tb[TCA_FASTPASS_MAX_PRELOAD]);
	}

	if (tb[TCA_FASTPASS_UPDATE_TIMESLOT_TIMER_NS]) {
		q->update_timeslot_timer_ns = nla_get_u32(tb[TCA_FASTPASS_UPDATE_TIMESLOT_TIMER_NS]);
	}

	/* TODO: when changing send_timeout, also change inside ctrl socket */

	if (!err && (should_reconnect || !q->ctrl_sock))
		err = reconnect_ctrl_socket(sch);

	if (!err)
		err = fp_tc_resize(q, fp_log);

	if (!err && changed_pacer) {
		bool was_trigerred = pacer_is_triggered(&q->request_pacer);
		u64 now_monotonic = fp_monotonic_time_ns();
		pacer_init_full(&q->request_pacer, now_monotonic, q->req_cost,
				q->req_bucketlen, q->req_min_gap);

		if (was_trigerred)
			pacer_trigger(&q->request_pacer, now_monotonic);
	}

	if (!err && changed_tslot_len) {
		u64 now_real = fp_get_time_ns();
		q->tslot_mul		= tslot_mul;
		q->tslot_shift		= tslot_shift;
		q->tslot_len_approx		= (1 << q->tslot_shift);
		do_div(q->tslot_len_approx, q->tslot_mul);
		q->current_timeslot = (now_real * q->tslot_mul) >> q->tslot_shift;
		wnd_reset(&q->alloc_wnd, q->current_timeslot);
	}

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
	fp_debug("marking fastpass qdisc as destroyed\n");
	spin_lock_bh(root_lock);
	sch->limit = 0;
	spin_unlock_bh(root_lock);
	fp_debug("marked fastpass qdisc as destroyed\n");

	/* close socket. no new packets should arrive afterwards */
	fp_debug("closing control socket\n");
	sock_release(q->ctrl_sock);

	/* eliminate the timeslot update timer */
	fp_debug("canceling timeslot update timer\n");
	hrtimer_cancel(&q->timeslot_update_timer);

	/* kill tasklet */
	fp_debug("killing timeslot update tasklet\n");
	tasklet_kill(&q->timeslot_update_tasklet);

	/* eliminate the request timer */
	fp_debug("canceling TX timer\n");
	hrtimer_cancel(&q->request_timer);

	/**
	 * make sure there isn't a tasklet running which might try to lock
	 *   after the the lock is destroyed
	 */
	fp_debug("killing TX tasklet\n");
	tasklet_kill(&q->request_tasklet);

	/* eliminate the retransmission timer */
	fp_debug("canceling retransmission timer\n");
	hrtimer_cancel(&q->retrans_timer);

	/* kill tasklet */
	fp_debug("killing retransmission tasklet\n");
	tasklet_kill(&q->retrans_tasklet);

	fp_debug("locking qdisc for reset\n");
	spin_lock_bh(root_lock);
	fp_tc_reset(sch);
	fp_debug("done resetting qdisc. freeing flow_hash_tbl\n");
	kfree(q->flow_hash_tbl);
	fp_debug("unlocking qdisc\n");
	spin_unlock_bh(root_lock);
	fp_debug("done\n");
}

/* initialize a new qdisc (part of qdisc API) */
static int fp_tc_init(struct Qdisc *sch, struct nlattr *opt)
{
	struct fp_sched_data *q = qdisc_priv(sch);
	u64 now_real = fp_get_time_ns();
	u64 now_monotonic = fp_monotonic_time_ns();
	struct tc_ratespec data_rate_spec ={
#if LINUX_VERSION_CODE != KERNEL_VERSION(3,2,45)
			.linklayer = TC_LINKLAYER_ETHERNET,
#endif
			.rate = 1e9/8,
			.overhead = 24};
	int err;

	sch->limit			= 10000;
	q->flow_plimit		= 100;
	q->hash_tbl_log		= ilog2(1024);
	psched_ratecfg_precompute(&q->data_rate, &data_rate_spec);
	q->req_cost			= 2 << 20;
	q->req_bucketlen	= 4 * q->req_cost;
	q->req_min_gap		= 1000;
	q->ctrl_addr_netorder = htonl(0x7F000001); /* need sensible default? */
	q->reset_window_us	= 2e6; /* 2 seconds */
	q->send_timeout_us	= 100000; /* 5ms timeout */
	q->flow_hash_tbl	= NULL;
	INIT_LIST_HEAD(&q->unreq_flows);
	INIT_LIST_HEAD(&q->retrans_flows);
	q->internal.src_dst_key = 0xD066F00DDEADBEEF;
	q->internal_free_time = now_monotonic;
	q->tslot_mul		= 1;
	q->tslot_shift		= 20;
	q->miss_threshold	= 5;
	q->max_dev_backlog_ns = (2 << 20);
	q->max_preload		= 5;
	q->update_timeslot_timer_ns = 2048;


	/* calculate timeslot from beginning of Epoch */
	q->tslot_len_approx		= (1 << q->tslot_shift);
	do_div(q->tslot_len_approx, q->tslot_mul);
	q->current_timeslot = (now_real * q->tslot_mul) >> q->tslot_shift;
	wnd_reset(&q->alloc_wnd, q->current_timeslot);

	q->ctrl_sock		= NULL;

	pacer_init_full(&q->request_pacer, now_monotonic, q->req_cost,
			q->req_bucketlen, q->req_min_gap);

	/* initialize timeslot update timer */
	hrtimer_init(&q->timeslot_update_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	q->timeslot_update_timer.function = timeslot_update_timer_func;

	/* initialize request timer */
	hrtimer_init(&q->request_timer, CLOCK_MONOTONIC, HRTIMER_MODE_ABS);
	q->request_timer.function = send_request_timer_func;

	/* initialize retransmission timer */
	hrtimer_init(&q->retrans_timer, CLOCK_MONOTONIC, HRTIMER_MODE_ABS);
	q->retrans_timer.function = retrans_timer_func;

	/* initialize timeslot update tasklet */
	tasklet_init(&q->timeslot_update_tasklet, &timeslot_update_tasklet_func,
			(unsigned long int)sch);

	/* initialize retransmission tasklet */
	tasklet_init(&q->retrans_tasklet, &retrans_tasklet,
			(unsigned long int)sch);

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
	    nla_put_u32(skb, TCA_FASTPASS_DATA_RATE, (u32)(q->data_rate.rate_bps >> 3)) ||
	    nla_put_u32(skb, TCA_FASTPASS_TIMESLOT_NSEC, q->tslot_len_approx) ||
	    nla_put_u32(skb, TCA_FASTPASS_REQUEST_COST, q->req_cost) ||
	    nla_put_u32(skb, TCA_FASTPASS_REQUEST_BUCKET, q->req_bucketlen) ||
	    nla_put_u32(skb, TCA_FASTPASS_REQUEST_GAP, q->req_min_gap) ||
	    nla_put_u32(skb, TCA_FASTPASS_CONTROLLER_IP, q->ctrl_addr_netorder) ||
	    nla_put_u32(skb, TCA_FASTPASS_RST_WIN_USEC, q->reset_window_us) ||
	    nla_put_u32(skb, TCA_FASTPASS_TIMESLOT_MUL, q->tslot_mul) ||
	    nla_put_u32(skb, TCA_FASTPASS_TIMESLOT_SHIFT, q->tslot_shift) ||
		nla_put_u32(skb, TCA_FASTPASS_MISS_THRESHOLD, q->miss_threshold) ||
		nla_put_u32(skb, TCA_FASTPASS_DEV_BACKLOG_NS, q->max_dev_backlog_ns) ||
		nla_put_u32(skb, TCA_FASTPASS_MAX_PRELOAD, q->max_preload) ||
		nla_put_u32(skb, TCA_FASTPASS_UPDATE_TIMESLOT_TIMER_NS, q->update_timeslot_timer_ns))
		goto nla_put_failure;

	nla_nest_end(skb, opts);
	return skb->len;

nla_put_failure:
	return -1;
}

/* this will statically check if the space in the TC statistics buffer is sufficient */
struct __fp_check_sizes {
	char sched_statistics__that_should_fit_in_pkt_sched_dot_h__[
	      TC_FASTPASS_SCHED_STAT_MAX_BYTES - sizeof(struct fp_sched_stat)];
	char socket_statistics__that_should_fit_in_pkt_sched_dot_h__[
	      TC_FASTPASS_SOCKET_STAT_MAX_BYTES - sizeof(struct fp_socket_stat)];
	char proto_statistics__that_should_fit_in_pkt_sched_dot_h__[
	      TC_FASTPASS_PROTO_STAT_MAX_BYTES - sizeof(struct fp_proto_stat)];
};

/* dumps statistics to netlink skb (part of qdisc API) */
static int fp_tc_dump_stats(struct Qdisc *sch, struct gnet_dump *d)
{
	struct fp_sched_data *q = qdisc_priv(sch);
	u64 now_real = fp_get_time_ns();
	u64 now_monotonic = fp_monotonic_time_ns();
	u64 time_next_req = pacer_next_event(&q->request_pacer);
	struct tc_fastpass_qd_stats st = {
		.version			= FASTPASS_STAT_VERSION,
		.flows				= q->flows,
		.inactive_flows		= q->inactive_flows,
		.n_unreq_flows		= q->n_unreq_flows,
		.stat_timestamp		= now_real,
		.current_timeslot	= q->current_timeslot,
		.horizon_mask		= wnd_get_mask(&q->alloc_wnd, q->current_timeslot+63),
		.time_next_request	= time_next_req - ( ~time_next_req ? now_monotonic : 0),
		.demand_tslots		= q->demand_tslots,
		.requested_tslots	= q->requested_tslots,
		.alloc_tslots		= q->alloc_tslots,
		.acked_tslots		= q->acked_tslots,
	};

	memset(&st.sched_stats[0], 0, TC_FASTPASS_SCHED_STAT_MAX_BYTES);
	memset(&st.socket_stats[0], 0, TC_FASTPASS_SOCKET_STAT_MAX_BYTES);
	memset(&st.proto_stats[0], 0, TC_FASTPASS_PROTO_STAT_MAX_BYTES);

	memcpy(&st.sched_stats[0], &q->stat, sizeof(q->stat));

	/* gather socket statistics */
	if (q->ctrl_sock) {
		struct fastpass_sock *fp = (struct fastpass_sock *)q->ctrl_sock->sk;
		struct fpproto_conn *conn = fpproto_conn(q);
		memcpy(&st.socket_stats[0], &fp->stat, sizeof(fp->stat));
		fpproto_dump_stats(conn, (struct fp_proto_stat *)&st.proto_stats);
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

	pr_info("%s: initializing\n", __func__);

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

	pr_info("%s: success\n", __func__);
	return 0;

out_unregister_qdisc:
	unregister_qdisc(&fastpass_qdisc_ops);
out_destroy_cache:
	kmem_cache_destroy(fp_flow_cachep);
out:
	pr_info("%s: failed, ret=%d\n", __func__, ret);
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

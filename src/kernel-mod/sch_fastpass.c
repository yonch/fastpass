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
#include <linux/slab.h>
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

#include "sch_timeslot.h"
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

#define FASTPASS_HORIZON					64
#define FASTPASS_REQUEST_WINDOW_SIZE 		(1 << 13)

#define CLOCK_MOVE_RESET_THRESHOLD_TSLOTS	64

#define FASTPASS_CTRL_SOCK_WMEM				(64*1024*1024)

#define SHOULD_DUMP_FLOW_INFO				0
enum {
	FLOW_UNQUEUED,
	FLOW_REQUEST_QUEUE,
};

/* module parameters */
static u32 req_cost = (2 << 20);
module_param(req_cost, uint, 0444);
MODULE_PARM_DESC(req_cost, "Cost of sending a request in ns, for request pacing");
EXPORT_SYMBOL_GPL(req_cost);

static u32 req_bucketlen = (4 * req_cost);
module_param(req_bucketlen, uint, 0444);
MODULE_PARM_DESC(req_bucketlen, "Max bucket size in ns, for request pacing");
EXPORT_SYMBOL_GPL(req_bucketlen);

static u32 req_min_gap = 1000;
module_param(req_min_gap, uint, 0444);
MODULE_PARM_DESC(req_min_gap, "ns to wait from when data arrives to sending request");
EXPORT_SYMBOL_GPL(req_min_gap);

static char *ctrl_addr = NULL;
module_param(ctrl_addr, charp, 0444);
MODULE_PARM_DESC(ctrl_addr, "IPv4 address of the controller");
EXPORT_SYMBOL_GPL(ctrl_addr);
static __be32 ctrl_addr_netorder;

static u32 reset_window_us = 2e6; /* 2 seconds */
module_param(reset_window_us, uint, 0444);
MODULE_PARM_DESC(reset_window_us, "the maximum time discrepancy (in us) to consider a reset valid");
EXPORT_SYMBOL_GPL(reset_window_us);

static u32 retrans_timeout_ns = 200000;
module_param(retrans_timeout_ns, uint, 0444);
MODULE_PARM_DESC(retrans_timeout_ns, "how long to wait for an ACK before retransmitting request");
EXPORT_SYMBOL_GPL(retrans_timeout_ns);

static u32 update_timer_ns = 2048;
module_param(update_timer_ns, uint, 0444);
MODULE_PARM_DESC(update_timer_ns, "how often to perform periodic tasks");
EXPORT_SYMBOL_GPL(update_timer_ns);

/*
 * Per flow structure, dynamically allocated
 */
struct fp_dst {
	u64		demand_tslots;		/* total needed timeslots */
	u64		requested_tslots;	/* highest requested timeslots */
	u64		acked_tslots;		/* highest requested timeslots that was acked*/
	u64		alloc_tslots;		/* total received allocations */
	u64		used_tslots;		/* timeslots in which packets moved */
	uint8_t state;
};

/**
 *
 */
struct fp_sched_data {
	/* configuration paramters */
	u32		tslot_mul;					/* mul to calculate timeslot from nsec */
	u32		tslot_shift;				/* shift to calculate timeslot from nsec */

	/* state */
	u16 unreq_flows[MAX_NODES]; 		/* flows with unscheduled packets */
	u32 unreq_dsts_head;
	u32 unreq_dsts_tail;

	struct fp_window alloc_wnd;
	u64		current_timeslot;
	u64		schedule[(1 << FASTPASS_WND_LOG)];	/* flows scheduled in the next time slots */

	struct tasklet_struct	maintenance_tasklet;
	struct hrtimer			maintenance_timer;
	struct tasklet_struct	enqueue_tasklet;

	struct fp_pacer request_pacer;
	struct socket	*ctrl_sock;			/* socket to the controller */
	struct qdisc_watchdog watchdog;

	u64		retrans_time;

	/* counters */
	u32		flows;
	u32		inactive_flows;  /* protected by fpproto_maintenance_lock */
	u32		n_unreq_flows;
	u64		demand_tslots;		/* total needed timeslots */
	u64		requested_tslots;	/* highest requested timeslots */
	u64		alloc_tslots;		/* total received allocations */
	u64		acked_tslots;		/* total acknowledged requests */
	u64		used_tslots;

	/* statistics */
	struct fp_sched_stat stat;
};

static struct tsq_qdisc_entry *fastpass_tsq_entry;
static struct proc_dir_entry *fastpass_proc_entry;

static void handle_reset(void *param);

static inline struct fpproto_conn *fpproto_conn(struct fp_sched_data *q)
{
	struct fastpass_sock *fp = (struct fastpass_sock *)q->ctrl_sock->sk;
	return &fp->conn;
}

/**
 *  computes the time when next request should go out
 *  @returns true if the timer was set, false if it was already set
 */
static inline bool trigger_tx(struct fp_sched_data* q)
{
	/* rate limit sending of requests */
	return pacer_trigger(&q->request_pacer, fp_monotonic_time_ns());
}

void trigger_tx_voidp(void *param)
{
	struct fp_sched_data *q = (struct fp_sched_data *)param;
	trigger_tx(q);
}

static int cancel_retrans_timer(void *param)
{
	struct fp_sched_data *q = (struct fp_sched_data *)param;
	q->retrans_time = ~0ULL;
	return 0;
}

static void set_retrans_timer(void *param, u64 when)
{
	struct fp_sched_data *q = (struct fp_sched_data *)param;
	q->retrans_time = when;
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

static inline bool flow_in_flowqueue(struct fp_dst *f)
{
	return (f->state != FLOW_UNQUEUED);
}

/**
 * Enqueues flow to the request queue, if it's not already in the retransmit
 *    queue
 */
static void flowqueue_enqueue_request(struct fp_sched_data *q, struct fp_dst *f)
{
	FASTPASS_BUG_ON(f->state == FLOW_REQUEST_QUEUE);

	/* enqueue */
	list_add_tail(&f->queue_entry, &q->unreq_flows);
	f->state = FLOW_REQUEST_QUEUE;

	q->n_unreq_flows++;

	/* update request timer if necessary */
	req_timer_flowqueue_enqueue(q);
}

/**
 * We no longer have a distinction between enqueuing retransmits and requests
 */
static void flowqueue_enqueue_retransmit(struct fp_sched_data *q, struct fp_dst *f)
{
	if (flow_in_flowqueue(f))
		return;

	flowqueue_enqueue_request(q, f);
}

static bool flowqueue_is_empty(struct fp_sched_data* q)
{
	return list_empty(&q->unreq_flows);
}

static struct fp_dst *flowqueue_dequeue(struct fp_sched_data* q)
{
	struct fp_dst *f;

	FASTPASS_BUG_ON(list_empty(&q->unreq_flows));

	/* get entry */
	f = list_first_entry(&q->unreq_flows, struct fp_dst, queue_entry);

	/* remove it from queue */
	list_del(&f->queue_entry);
	f->state = FLOW_UNQUEUED;

	/* update counter */
	q->n_unreq_flows--;

	return f;
}


void flow_inc_used(struct fp_sched_data *q, struct fp_dst* f, u64 amount) {
	f->used_tslots += amount;
	q->used_tslots += amount;
}

/**
 * Increase the number of unrequested packets for the flow.
 *   Maintains the necessary invariants, e.g. adds the flow to the unreq_flows
 *   list if necessary
 */
static void flow_inc_demand(struct fp_sched_data *q, struct fp_dst *f, u64 amount)
{
	f->demand_tslots += amount;
	q->demand_tslots += amount;

	if (!flow_in_flowqueue(f))
		/* flow not on scheduling queue yet, enqueue */
		flowqueue_enqueue_request(q, f);
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
	struct fp_dst *f;
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

	/* look up the flow of this allocation */
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

	/* is this an allocation we don't need? (redundant) */
	if (unlikely(f->used_tslots == f->demand_tslots)) {
		if (next_nonempty > q->current_timeslot)
			goto done; /* maybe we'll get more packets by that time */

		/* an unwanted alloc. it would have been added to statistics at the
		 * time it arrived in handle_alloc. just ignore */
		wnd_clear(&q->alloc_wnd, next_nonempty);
		goto begin;
	}

	/* now we know that we need the allocation */

	/* is alloc too far in the past? */
	if (unlikely(time_before64(next_nonempty, q->current_timeslot - q->miss_threshold))) {
		q->stat.missed_timeslots++;
		fp_debug("missed timeslot %llu by %llu timeslots, rescheduling\n",
				next_nonempty, q->current_timeslot - next_nonempty);
		goto reschedule_timeslot_and_continue;
	}

	if (time_after64(q->internal_free_time, now_monotonic + q->max_dev_backlog_ns)) {
		/* queue full, cannot move flow */
		if (next_nonempty > q->current_timeslot)
			goto done; /* maybe later queue will drain */

		/* timeslots are late and backlog is full. will reschedule */
		q->stat.backlog_too_high++;
		fp_debug("backlog too high processing timeslot %llu at %llu, will try again at next update\n",
				next_nonempty, q->current_timeslot);
		goto done;
	}

	/* Okay can move timeslot! */
	/* clear the allocation */
	wnd_clear(&q->alloc_wnd, next_nonempty);

	spin_lock(&q->prequeue_lock);
	move_timeslot_from_flow(sch, &q->data_rate, f, &q->prequeue,
							fp_alloc_path(next_key));
	spin_unlock(&q->prequeue_lock);

	/* mark that we used a timeslot */
	flow_inc_used(q, f, 1);
	f->last_moved_timeslot = next_nonempty;

	/* statistics */
	moved_timeslots++;
	q->stat.sucessful_timeslots++;
	if (next_nonempty > q->current_timeslot) {
		q->stat.early_enqueue++;
	} else {
		u64 tslot = q->current_timeslot;
		u64 thresh = q->miss_threshold;
		if (unlikely(next_nonempty < tslot - (thresh >> 1))) {
			if (unlikely(next_nonempty < tslot - 3*(thresh >> 2)))
				q->stat.late_enqueue4++;
			else
				q->stat.late_enqueue3++;
		} else {
			if (unlikely(next_nonempty < tslot - (thresh >> 2)))
				q->stat.late_enqueue2++;
			else
				q->stat.late_enqueue1++;

		}
	}

	goto begin; /* try another timeslot */

reschedule_timeslot_and_continue:
	flow_inc_used(q, f, 1);
	flow_inc_demand(q, f, 1);
	wnd_clear(&q->alloc_wnd, next_nonempty);
	goto begin; /* try the next timeslot */

done:
	/* update window around current timeslot */
	tslot_advance = q->current_timeslot + FASTPASS_WND_LEN - 1 - q->miss_threshold - wnd_head(&q->alloc_wnd);
	if (unlikely((s64)tslot_advance < 0)) {
		if (-1*(s64)tslot_advance > CLOCK_MOVE_RESET_THRESHOLD_TSLOTS) {
			FASTPASS_WARN("current timeslot moved back a lot: %lld timeslots. new current %llu. will reset\n",
					(s64)tslot_advance, q->current_timeslot);
			q->stat.clock_move_causes_reset++;
			/* This corrupts the status; will force a reset */
			fpproto_force_reset(fpproto_conn(q));
			handle_reset((void *)sch); /* manually call callback since fpproto won't call it */
		} else {
			fp_debug("current timeslot moved back a little: %lld timeslots. new current %llu\n",
					(s64)tslot_advance, q->current_timeslot);
		}
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
}

/**
 * Performs a reset and garbage collection of flows
 */
static void handle_reset(void *param)
{
	struct fp_sched_data *q = (struct fp_sched_data *)param;

	struct rb_node *cur, *next;
	struct rb_root *root;
	struct fp_dst *f;
	u32 idx;
	u32 base_idx = src_dst_key_hash(fp_monotonic_time_ns()) >> (32 - q->hash_tbl_log);
	u32 mask = (1U << q->hash_tbl_log) - 1;

	/* reset future allocations */
	wnd_reset(&q->alloc_wnd, q->current_timeslot);

	q->flows = 0;
	q->inactive_flows = 0;		/* will remain 0 when we're done */
	q->demand_tslots = 0;
	q->requested_tslots = 0;	/* will remain 0 when we're done */
	q->alloc_tslots = 0;		/* will remain 0 when we're done */
	q->acked_tslots = 0; 		/* will remain 0 when we're done */
	q->used_tslots = 0; 		/* will remain 0 when we're done */

	/* for each cell in hash table: */
	for (idx = 0; idx < (1U << q->hash_tbl_log); idx++) {
		root = &q->flow_hash_tbl[(idx + base_idx) & mask];
		next = rb_first(root); /* we traverse tree in-order */

		/* while haven't finished traversing rbtree: */
		while (next != NULL) {
			cur = next;
			next = rb_next(cur);

			f = container_of(cur, struct fp_dst, fp_node);

			/* can we garbage-collect this flow? */
			if (f->demand_tslots == f->used_tslots) {
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
			f->demand_tslots -= f->used_tslots;
			f->alloc_tslots = 0;
			f->acked_tslots = 0;
			f->requested_tslots = 0;
			f->used_tslots = 0;

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
	struct fp_sched_data *q = (struct fp_sched_data *)param;
	int i;
	u8 spec;
	int dst_ind;
	u64 full_tslot;
	u64 now_real = fp_get_time_ns();
	u16 node_id;
	u64 current_timeslot;

	/* every alloc should be ACKed */
	trigger_tx(q);

	/* find full timeslot value of the ALLOC */
	current_timeslot = (now_real * q->tslot_mul) >> q->tslot_shift;

	full_tslot = current_timeslot - (1ULL << 18); /* 1/4 back, 3/4 front */
	full_tslot += ((u32)base_tslot - (u32)full_tslot) & 0xFFFFF; /* 20 bits */

	fp_debug("got ALLOC for timeslot %d (full %llu, current %llu), %d destinations, %d timeslots, mask 0x%016llX\n",
			base_tslot, full_tslot, q->current_timeslot, n_dst, n_tslots,
			wnd_get_mask(&q->alloc_wnd, q->current_timeslot+63));

	for (i = 0; i < n_tslots; i++) {
		struct fp_dst *f;

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

		/* is alloc too far in the past? */
		if (unlikely(time_before64(full_tslot, current_timeslot - q->miss_threshold))) {
		//if (unlikely(wnd_seq_before(&q->alloc_wnd, full_tslot))) {
			q->stat.alloc_too_late++;
			fp_debug("-X- already gone, dropping\n");
			continue;
		}

		if (unlikely(time_after64(full_tslot, current_timeslot + q->max_preload))) {
//		if (unlikely(wnd_seq_after(&q->alloc_wnd, full_tslot))) {
			q->stat.alloc_premature++;
			fp_debug("-X- too futuristic, dropping\n");
			continue;
		}

		/* sanity check */
//		if (wnd_is_marked(&q->alloc_wnd, full_tslot)) {
//			FASTPASS_WARN("got ALLOC tslot %llu dst 0x%X but but it was already marked for 0x%llX current_tslot %llu base %u now_real %llu\n",
//					full_tslot, dst[dst_ind - 1], q->schedule[wnd_pos(full_tslot)],
//					q->current_timeslot, base_tslot, now_real);
//			continue;
//		}

		node_id = fp_alloc_node(dst[dst_ind - 1]);
		f = fpq_lookup(q, node_id, false);
		if (unlikely(f == NULL)) {
			FASTPASS_WARN("couldn't find flow 0x%X from alloc. will reset\n",
					node_id);
			q->stat.alloc_flow_not_found++;
			/* This corrupts the status; will force a reset */
			fpproto_force_reset(fpproto_conn(q));
			handle_reset((void *)sch); /* manually call callback since fpproto won't call it */
			return;
		}

		/* okay, allocate */
//		wnd_mark(&q->alloc_wnd, full_tslot);
//		q->schedule[wnd_pos(full_tslot)] = dst[dst_ind - 1];
		if (f->used_tslots != f->demand_tslots) {

			spin_lock(&q->prequeue_lock);
			move_timeslot_from_flow(sch, &q->data_rate, f, &q->prequeue,
									fp_alloc_path(dst[dst_ind - 1]));
			spin_unlock(&q->prequeue_lock);

			flow_inc_used(q, f, 1);

			f->alloc_tslots++;
			q->alloc_tslots++;
			q->stat.sucessful_timeslots++;
			if (full_tslot > current_timeslot) {
				q->stat.early_enqueue++;
			} else {
				u64 tslot = current_timeslot;
				u64 thresh = q->miss_threshold;
				if (unlikely(full_tslot < tslot - (thresh >> 1))) {
					if (unlikely(full_tslot < tslot - 3*(thresh >> 2)))
						q->stat.late_enqueue4++;
					else
						q->stat.late_enqueue3++;
				} else {
					if (unlikely(full_tslot < tslot - (thresh >> 2)))
						q->stat.late_enqueue2++;
					else
						q->stat.late_enqueue1++;

				}
			}

		} else {
			/* alloc arrived that was considered dead, we keep it in
			 *  q->schedule so it might at least reduce latency if demand
			 *  increases later */
			q->stat.unwanted_alloc++;
			fp_debug("got an allocation over demand, flow 0x%04llX, demand %llu\n",
					f->src_dst_key, f->demand_tslots);
		}
	}

	fp_debug("mask after: 0x%016llX\n",
			wnd_get_mask(&q->alloc_wnd, q->current_timeslot+63));
}

static void handle_areq(void *param, u16 *dst_and_count, int n)
{
	struct fp_sched_data *q = (struct fp_sched_data *)param;
	struct fp_dst *f;
	int i;
	u16 dst;
	u16 count_low;
	u64 count;

	trigger_tx(q);

	for (i = 0; i < n; i++) {
		dst = ntohs(dst_and_count[2*i]);
		count_low = ntohs(dst_and_count[2*i + 1]);

		f = fpq_lookup(q, dst, false);
		if (unlikely(f == NULL)) {
			FASTPASS_WARN("couldn't find flow 0x%X from alloc report. will reset\n",
					dst);
			q->stat.alloc_report_flow_not_found++;
			/* This corrupts the status; will force a reset */
			fpproto_force_reset(fpproto_conn(q));
			handle_reset((void *)sch); /* manually call callback since fpproto won't call it */
			return;
		}

		/* get full count */
		/* TODO: This is not perfectly safe. For example, if there is a big
		 * outage and the controller thinks it had produced many timeslots, this
		 * can go out of sync */
		count = f->alloc_tslots - (1 << 15);
		count += (u16)(count_low - count);

		/* update counts */
		if ((s64)(count - f->alloc_tslots) > 0) {
			u64 n_lost = count - f->alloc_tslots;

			if (unlikely((s64)(count - f->requested_tslots) > 0)) {
				FASTPASS_WARN("got an alloc report for dst %d larger than requested (%llu > %llu), will reset\n",
						dst, count, f->requested_tslots);
				q->stat.alloc_report_larger_than_requested++;
				/* This corrupts the status; will force a reset */
				fpproto_force_reset(fpproto_conn(q));
				handle_reset((void *)sch); /* manually call callback since fpproto won't call it */
				return;
			}

			fp_debug("controller allocated %llu our allocated %llu, will increase demand by %llu\n",
					count, f->alloc_tslots, n_lost);

			q->alloc_tslots += n_lost;
			f->alloc_tslots += n_lost;
			q->stat.timeslots_assumed_lost += n_lost;

			flow_inc_used(q, f, n_lost);
			flow_inc_demand(q, f, n_lost);
		}
	}
}

static void handle_ack(void *param, struct fpproto_pktdesc *pd)
{
	struct fp_sched_data *q = (struct fp_sched_data *)param;
	int i;
	struct fp_dst *f;
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
}

static void handle_neg_ack(void *param, struct fpproto_pktdesc *pd)
{
	struct fp_sched_data *q = (struct fp_sched_data *)param;
	int i;
	struct fp_dst *f;
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

		/* add to retransmit queue */
		flowqueue_enqueue_retransmit(q, f);
		fp_debug("nack for request of %llu for flow 0x%04llX (%llu acked), added to retransmit queue\n",
						req_tslots, f->src_dst_key, f->acked_tslots);
	}
}

/**
 * Send a request packet to the controller
 */
static void send_request(struct Qdisc *sch)
{
	struct fp_sched_data *q = qdisc_priv(sch);
	u64 now_monotonic = fp_monotonic_time_ns();

	struct fp_dst *f;
	struct fp_kernel_pktdesc *kern_pd;
	struct fpproto_pktdesc *pd;
	u64 new_requested;

	fp_debug("start: unreq_flows=%u, unreq_tslots=%llu, now_mono=%llu, scheduled=%llu, diff=%lld, next_seq=%08llX\n",
			q->n_unreq_flows, q->demand_tslots - q->requested_tslots, now_monotonic,
			pacer_next_event(&q->request_pacer),
			(s64 )now_monotonic - (s64 )pacer_next_event(&q->request_pacer),
			fpproto_conn(q)->next_seqno);
	if(flowqueue_is_empty(q)) {
		q->stat.request_with_empty_flowqueue++;
		fp_debug("was called with no flows pending (could be due to bad packets?)\n");
	}
	FASTPASS_BUG_ON(!q->ctrl_sock);

	/* update request credits */
	pacer_reset(&q->request_pacer);

	/* allocate packet descriptor */
	kern_pd = fpproto_pktdesc_alloc();
	if (!kern_pd)
		goto alloc_err;
	pd = &kern_pd->pktdesc;

	/* nack the tail of the outwnd if it has not been nacked or acked */
	fpproto_prepare_to_send(fpproto_conn(q));

	pd->n_areq = 0;

	while ((pd->n_areq < FASTPASS_PKT_MAX_AREQ) && !flowqueue_is_empty(q)) {
		/* get entry */
		f = flowqueue_dequeue(q);

		new_requested = min_t(u64, f->demand_tslots,
				f->acked_tslots + FASTPASS_REQUEST_WINDOW_SIZE - 1);
		if(new_requested <= f->acked_tslots) {
			q->stat.queued_flow_already_acked++;
			fp_debug("flow 0x%04llX was in queue, but already fully acked\n",
					f->src_dst_key);
			continue;
		}

		pd->areq[pd->n_areq].src_dst_key = f->src_dst_key;
		pd->areq[pd->n_areq].tslots = new_requested;

		q->requested_tslots += (new_requested - f->requested_tslots);
		f->requested_tslots = new_requested;

		pd->n_areq++;
	}

	fp_debug("end: unreq_flows=%u, unreq_tslots=%llu\n",
			q->n_unreq_flows, q->demand_tslots - q->requested_tslots);

	fpproto_commit_packet(fpproto_conn(q), pd, now_monotonic);

	/* let fpproto send the pktdesc */
	fpproto_send_pktdesc(q->ctrl_sock->sk, kern_pd);

	/* set timer for next request, if a request would be required */
	if (q->demand_tslots != q->alloc_tslots)
		/* have more requests to send */
		trigger_tx(q);

	return;

alloc_err:
	q->stat.req_alloc_errors++;
	fp_debug("request allocation failed\n");
	trigger_tx(q); /* try again */
}


static enum hrtimer_restart maintenance_timer_func(struct hrtimer *timer)
{
	struct fp_sched_data *q =
			container_of(timer, struct fp_sched_data, maintenance_timer);

	/* schedule tasklet to write request */
	tasklet_schedule(&q->maintenance_tasklet);

	hrtimer_forward_now(timer, ns_to_ktime(update_timer_ns));
	return HRTIMER_RESTART;
}

static void maintenance_tasklet_func(unsigned long int param)
{
	struct Qdisc *sch = (struct Qdisc *)param;
	struct fp_sched_data *q = qdisc_priv(sch);
	u64 now_monotonic;
//	u64 now_real;


	/* move skbs from flow queues to the prequeue */
//	now_real = fp_get_time_ns();
//	fpproto_maintenance_lock(q->ctrl_sock->sk);
//	update_current_timeslot(sch, now_real);
//	fpproto_maintenance_unlock(q->ctrl_sock->sk);

	/* check for retransmission timeouts and process */
	now_monotonic = fp_monotonic_time_ns();
	if (q->retrans_time <= now_monotonic) {
		fpproto_maintenance_lock(q->ctrl_sock->sk);
		fpproto_handle_timeout(fpproto_conn(q), now_monotonic);
		fpproto_maintenance_unlock(q->ctrl_sock->sk);
	}

	/* now is also a good opportunity to send a request, if allowed */
	if (pacer_is_triggered(&q->request_pacer) &&
		time_after_eq64(now_monotonic, pacer_next_event(&q->request_pacer))) {
		fpproto_maintenance_lock(q->ctrl_sock->sk);
		send_request(sch);
		fpproto_maintenance_unlock(q->ctrl_sock->sk);
	}

}

struct fpproto_ops fastpass_sch_proto_ops = {
	.handle_reset	= &handle_reset,
	.handle_alloc	= &handle_alloc,
	.handle_ack		= &handle_ack,
	.handle_neg_ack	= &handle_neg_ack,
	.handle_areq	= &handle_areq,
	.trigger_request= &trigger_tx_voidp,
	.set_timer		= &set_retrans_timer,
	.cancel_timer	= &cancel_retrans_timer,
};

/* reconnects the control socket to the controller */
static int reconnect_ctrl_socket(struct fp_sched_data *q)
{
	struct sock *sk;
	int opt;
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

	/* we need a larger-than-default wmem, so we don't run out. ask for a lot,
	 * the call will not fail if it's too much */
	opt = FASTPASS_CTRL_SOCK_WMEM;
	rc = kernel_setsockopt(q->ctrl_sock, SOL_SOCKET, SO_SNDBUF, (char *)&opt,
			sizeof(opt));
	if (rc != 0)
		FASTPASS_WARN("Could not set socket wmem size\n");

	sk = q->ctrl_sock->sk;

	FASTPASS_BUG_ON(sk->sk_priority != TC_PRIO_CONTROL);
	FASTPASS_BUG_ON(sk->sk_allocation != GFP_ATOMIC);

	/* give socket a reference to this qdisc for watchdog */
	fpproto_set_qdisc(sk, q);

	/* initialize the fastpass protocol */
	fpproto_init_conn(fpproto_conn(q), &fastpass_sch_proto_ops, (void *)sch,
			(u64)reset_window_us * NSEC_PER_USEC,
			send_timeout_us);

	/* connect */
	sock_addr.sin_addr.s_addr = ctrl_addr_netorder;
	rc = kernel_connect(q->ctrl_sock, (struct sockaddr *)&sock_addr,
			sizeof(sock_addr), 0);
	if (rc != 0)
		goto err_release;

	return 0;

err_release:
	FASTPASS_WARN("Error %d trying to connect to addr 0x%X (in netorder)\n",
			rc, ctrl_addr_netorder);
	sock_release(q->ctrl_sock);
	q->ctrl_sock = NULL;
	return rc;
}


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

	if (tb[TCA_FASTPASS_MISS_THRESHOLD]) {
		q->miss_threshold = nla_get_u32(tb[TCA_FASTPASS_MISS_THRESHOLD]);
	}

	if (tb[TCA_FASTPASS_DEV_BACKLOG_NS]) {
		q->max_dev_backlog_ns = nla_get_u32(tb[TCA_FASTPASS_DEV_BACKLOG_NS]);
	}

	if (tb[TCA_FASTPASS_MAX_PRELOAD]) {
		q->max_preload = nla_get_u32(tb[TCA_FASTPASS_MAX_PRELOAD]);
	}

	/* TODO: when changing send_timeout, also change inside ctrl socket */

	if (!err && (should_reconnect || !q->ctrl_sock))
		err = reconnect_ctrl_socket(q);

	if (!err)
		err = fp_tc_resize(q, fp_log);

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

static int fpq_new_qdisc(void *priv, u32 tslot_mul, u32 tslot_shift)
{
	struct fp_sched_data *q = (struct fp_sched_data *)param;

	u64 now_real = fp_get_time_ns();
	u64 now_monotonic = fp_monotonic_time_ns();
	int err;

	q->unreq_dsts_head = q->unreq_dsts_tail = 0;
	q->tslot_mul		= tslot_mul;
	q->tslot_shift		= tslot_shift;


	/* calculate timeslot from beginning of Epoch */
	q->current_timeslot = (now_real * q->tslot_mul) >> q->tslot_shift;
	wnd_reset(&q->alloc_wnd, q->current_timeslot);

	q->ctrl_sock		= NULL;

	pacer_init_full(&q->request_pacer, now_monotonic, req_cost,
			req_bucketlen, req_min_gap);

	/* initialize retransmission timer */
	q->retrans_time = ~0ULL;

	tasklet_init(&q->maintenance_tasklet, &maintenance_tasklet_func, (unsigned long int)sch);

	err = reconnect_ctrl_socket(q);
	if (err != 0)
		goto out;

	hrtimer_init(&q->maintenance_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	q->maintenance_timer.function = maintenance_timer_func;
	hrtimer_start(&q->maintenance_timer, ns_to_ktime(update_timer_ns),
			HRTIMER_MODE_REL);
	return err;

out:
	pr_info("%s: error creating new qdisc err=%d\n", err);
	return err;
}


/*
 * Prints flow status
 */
static void dump_flow_info(struct fp_sched_data *q, bool only_active)
{
	struct rb_node *cur, *next;
	struct rb_root *root;
	struct fp_dst *f;
	u32 idx;
	u32 num_printed = 0;

	printk(KERN_DEBUG "fastpass flows (only_active=%d):\n", only_active);

	/* for each cell in hash table: */
	for (idx = 0; idx < (1U << q->hash_tbl_log); idx++) {
		root = &q->flow_hash_tbl[idx];
		next = rb_first(root); /* we traverse tree in-order */

		/* while haven't finished traversing rbtree: */
		while (next != NULL) {
			cur = next;
			next = rb_next(cur);

			f = container_of(cur, struct fp_dst, fp_node);

			if (f->qlen == 0 && only_active)
				continue;

			num_printed++;
			printk(KERN_DEBUG "flow 0x%04llX demand %llu requested %llu acked %llu alloc %llu used %llu qlen %d credit %lld last_moved 0x%llX state %d\n",
					f->src_dst_key, f->demand_tslots, f->requested_tslots,
					f->acked_tslots, f->alloc_tslots, f->used_tslots, f->qlen,
					f->credit, f->last_moved_timeslot, f->state);
		}
	}

	printk(KERN_DEBUG "fastpass printed %u flows\n", num_printed);
}

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
		.used_tslots		= q->used_tslots,
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

	if (SHOULD_DUMP_FLOW_INFO)
		dump_flow_info(q, false);

	return gnet_stats_copy_app(d, &st, sizeof(st));
}

static int fastpass_proc_show(struct seq_file *seq, void *v)
{
	struct fpq_sched_data *q = (struct fpq_sched_data *)seq->private;
	u64 now_real = fp_get_time_ns();
	struct fp_sched_stat *scs = &q->stat;

	/* time */
	seq_printf(seq, "  fp_sched_data *p = %p ", q);
	seq_printf(seq, ", timestamp 0x%llX ", now_real);
	seq_printf(seq, ", timeslot 0x%llX", q->current_timeslot);

	/* flow statistics */
	seq_printf(seq, "\n  %u flows (%u inactive)",
		q->flows, q->inactive_flows);
	seq_printf(seq, ", %llu gc", scs->gc_flows);

	/* timeslot statistics */
	seq_printf(seq, "\n  horizon mask 0x%016llx",
			wnd_get_mask(&q->alloc_wnd, q->current_timeslot+63));
	seq_printf(seq, ", %llu added timeslots", q->added_tslots);
	seq_printf(seq, ", %llu used", scs->sucessful_timeslots);
	seq_printf(seq, " (%llu %llu %llu %llu behind, %llu fast)", scs->late_enqueue4,
			scs->late_enqueue3, scs->late_enqueue2, scs->late_enqueue1,
			scs->early_enqueue);
	seq_printf(seq, ", %llu missed", scs->missed_timeslots);
	seq_printf(seq, ", %llu high_backlog", scs->backlog_too_high);
	seq_printf(seq, ", %llu assumed_lost", scs->timeslots_assumed_lost);
	seq_printf(seq, "  (%llu late", scs->alloc_too_late);
	seq_printf(seq, ", %llu premature)", scs->alloc_premature);

	/* egress packet statistics */
	seq_printf(seq, "\n  enqueued %llu ctrl", scs->ctrl_pkts);
	seq_printf(seq, ", %llu ntp", scs->ntp_pkts);
	seq_printf(seq, ", %llu ptp", scs->ptp_pkts);
	seq_printf(seq, ", %llu arp", scs->arp_pkts);
	seq_printf(seq, ", %llu igmp", scs->igmp_pkts);
	seq_printf(seq, ", %llu ssh", scs->ssh_pkts);
	seq_printf(seq, ", %llu data", scs->data_pkts);
	seq_printf(seq, ", %llu flow_plimit", scs->flows_plimit);
	seq_printf(seq, ", %llu too big", scs->pkt_too_big);

	/* error statistics */
	seq_printf(seq, "\n errors:");
	if (scs->allocation_errors)
		seq_printf(seq, "\n  %llu allocation errors in dst_lookup", scs->allocation_errors);
	if (scs->classify_errors)
		seq_printf(seq, "\n  %llu packets could not be classified", scs->classify_errors);
	if (scs->flow_not_found_update)
		seq_printf(seq, "\n  %llu flow could not be found in update_current_tslot!",
				scs->flow_not_found_update);
	if (scs->req_alloc_errors)
		seq_printf(seq, "\n  %llu could not allocate pkt_desc for request", scs->req_alloc_errors);

	/* warnings */
	seq_printf(seq, "\n warnings:");
	if (scs->unwanted_alloc)
		seq_printf(seq, "\n  %llu timeslots allocated beyond the demand of the flow (could happen due to reset / controller timeouts)",
				scs->unwanted_alloc);
	if (scs->alloc_premature)
		seq_printf(seq, "\n  %llu premature allocations (something wrong with time-sync?)\n",
				scs->alloc_premature);
	if (scs->clock_move_causes_reset)
		seq_printf(seq, "\n  %llu large clock moves caused resets",
				scs->clock_move_causes_reset);

	return 0;
}

static int fastpass_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, fastpass_proc_show, PDE_DATA(inode));
}

static const struct file_operations fastpass_proc_fops = {
	.owner	 = THIS_MODULE,
	.open	 = fastpass_proc_open,
	.read	 = seq_read,
	.llseek	 = seq_lseek,
	.release = single_release,
};

static int fastpass_proc_init(struct fpq_sched_data *fpq, struct tsq_ops *ops)
{
	char fname[PROC_FILENAME_MAX_SIZE];
	snprintf(fname, PROC_FILENAME_MAX_SIZE, "fastpass/%s-%p", ops->id, fpq);
	fpq->proc_entry = proc_create_data(fname, S_IRUGO, NULL, &fastpass_proc_fops, fpq);
	if (fpq->proc_entry == NULL)
		return -1; /* error */
	return 0; /* success */
}

static void fastpass_proc_cleanup(struct fpq_sched_data *q)
{
	proc_remove(q->proc_entry);
}

static void fpq_stop_qdisc(void *priv) {
	return;
}
static void fpq_add_timeslot(void *priv, u64 src_dst_key) {
	tsq_admit_now(priv, src_dst_key);
}

static struct tsq_ops fastpass_tsq_ops __read_mostly = {
	.id		=	"fastpass",
	.priv_size	=	sizeof(struct fp_sched_data),

	.new_qdisc = fpq_new_qdisc,
	.stop_qdisc = fpq_stop_qdisc,
	.add_timeslot = fpq_add_timeslot,
};

static int __init fastpass_module_init(void)
{
	int ret = -ENOSYS;

	pr_info("%s: initializing\n", __func__);

	fastpass_proc_entry = proc_mkdir("fastpass", NULL);
	if (fastpass_proc_entry == NULL)
		goto out;

	ret = fpproto_register();
	if (ret)
		goto out_remove_proc;

	ret = tsq_init();
	if (ret != 0)
		goto out_unregister_fpproto;

	fastpass_tsq_entry = tsq_register_qdisc(&fastpass_tsq_ops);
	if (fastpass_tsq_entry == NULL)
		goto out_exit;



	pr_info("%s: success\n", __func__);
	return 0;
out_exit:
	tsq_exit();
out_unregister_fpproto:
	fpproto_unregister();
out_remove_proc:
	proc_remove(fastpass_proc_entry);
out:
	pr_info("%s: failed, ret=%d\n", __func__, ret);
	return ret;
}

static void __exit fastpass_module_exit(void)
{
	proc_remove(fastpass_proc_entry);
	tsq_unregister_qdisc(fastpass_tsq_entry);
	tsq_exit();
	fpproto_unregister(); /* TODO: verify this is safe */
}

module_init(fastpass_module_init)
module_exit(fastpass_module_exit)
MODULE_AUTHOR("Jonathan Perry");
MODULE_LICENSE("GPL");

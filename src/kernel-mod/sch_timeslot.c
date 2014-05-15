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

#define CLOCK_MOVE_RESET_THRESHOLD_TSLOTS	64

#define SHOULD_DUMP_FLOW_INFO				0

struct timeslot_skb_q {
	struct list_head *list;
	struct sk_buff	*head;		/* list of skbs for this flow : first skb */
	struct sk_buff *tail;		/* last skb in the list */
};

/*
 * Per flow structure, dynamically allocated
 */
struct timeslot_dst {
	u64		src_dst_key;		/* flow identifier */
	struct rb_node	fp_node; 	/* anchor in fp_root[] trees */
	struct list_head skb_qs;	/* a queue for each timeslot */
	s64		credit;				/* time remaining in the last scheduled timeslot */
};

struct rcu_hash_tbl_cleanup {
	struct rb_root *hash_tbl;
	struct rcu_head rcu_head;
};

/**
 *
 */
struct fp_sched_data {
	/* configuration paramters */
	u8		hash_tbl_log;				/* log number of hash buckets */
	u32		tslot_mul;					/* mul to calculate timeslot from nsec */
	u32		tslot_shift;				/* shift to calculate timeslot from nsec */
	u32		miss_threshold;				/* #timeslots that alloc is considered missed */
	u32		max_dev_backlog_ns;			/* max ns that allow a tslot to be enqueued to internal */
	u32		max_preload;				/* how early (#timeslots) can enqueueing be */
	u32		update_timeslot_timer_ns;	/* every how many ns to update queues */

	struct psched_ratecfg data_rate;	/* rate of payload packets */
	u32		tslot_len_approx;					/* duration of a timeslot, in nanosecs */

	struct timeslot_ops *timeslot_ops;

	/* state */
	spinlock_t		hash_tbl_lock;
	struct rb_root	*dst_hash_tbl;		/* table of rb-trees of flows */
	struct rcu_hash_tbl_cleanup *hash_tbl_cleanup;

	/* enqueued packets go into enqueue_queue; tasklet distributes into flows */
	spinlock_t				enqueue_lock;		/* protect enqueue_queue */
	struct timeslot_skb_q	enqueue_skb_q;		/* for data packets to be classified to flows */
	struct tasklet_struct	enqueue_tasklet;


	struct timeslot_skb_q	reg_prio;			/* for regular queue */
	struct timeslot_skb_q	hi_prio;			/* for high prio traffic */
 	u64					next_zero_queue_time; /* approx time when internal will be free */

	struct timeslot_skb_q	prequeue;		/* a flow for packets that need to go into internal */
	spinlock_t		prequeue_lock;

	struct fp_window alloc_wnd;
	u64		current_timeslot;
	u64		schedule[(1 << FASTPASS_WND_LOG)];	/* flows scheduled in the next time slots */

	struct qdisc_watchdog watchdog;

	/* counters */
	u32		flows;
	u32		inactive_flows;  /* protected by fpproto_maintenance_lock */
	u64		added_tslots;		/* total needed timeslots */
	u64		used_tslots;

	/* statistics */
	struct fp_sched_stat stat;
};

static struct kmem_cache *timeslot_dst_cachep __read_mostly;
static struct kmem_cache *timeslot_skb_q_cachep __read_mostly;

static inline struct fp_sched_data *priv_to_sched_data(void *priv) {
	return (struct fp_sched_data *)((char *)priv - QDISC_ALIGN(sizeof(struct fp_sched_data)));
}
static inline void *sched_data_to_priv(struct fp_sched_data *q) {
	return (char *)q + QDISC_ALIGN(sizeof(struct fp_sched_data));
}

/* hashes a flow key into a u32, for lookup in the hash tables */
static inline u32 src_dst_key_hash(u64 src_dst_key) {
	return jhash_2words((__be32)(src_dst_key >> 32),
						 (__be32)src_dst_key, 0);
}

static const u8 prio2band[TC_PRIO_MAX + 1] = {
	1, 2, 2, 2, 1, 2, 0, 0 , 1, 1, 1, 1, 1, 1, 1, 1
};

static void skb_q_init(struct timeslot_skb_q *queue)
{
	queue->head = NULL;
}

static inline bool skb_q_empty(struct timeslot_skb_q *queue)
{
	return (queue->head == NULL);
}

/* remove one skb from head of flow queue */
static void skb_q_enqueue(struct timeslot_skb_q *queue,
		struct sk_buff *skb)
{
	skb->next = NULL;
	if (!queue->head) {
		queue->head = skb;
	} else {
		queue->tail->next = skb;
	}
	queue->tail = skb;
}

static struct sk_buff *skb_q_dequeue(struct timeslot_skb_q *queue)
{
	struct sk_buff *skb = queue->head;

	if (skb) {
		queue->head = skb->next;
		skb->next = NULL;
	}
	return skb;
}

/* moves content of 'what' into 'queue'. 'queue' must be empty! */
static void skb_q_move(struct timeslot_skb_q *queue,
		struct timeslot_skb_q *what)
{
	FASTPASS_BUG_ON(queue->head != NULL);
	queue->head = what->head;
	queue->tail = what->tail;
	what->head = NULL;
}

/* Appends 'what' to the end of 'queue' */
static void skb_q_append(struct timeslot_skb_q *queue,
		struct timeslot_skb_q *what)
{
	/* have skbs to move */
	if (queue->head == NULL)
		queue->head = what->head;
	else
		queue->tail->next = what->head;
	queue->tail = what->tail;

	what->head = NULL;
}

/**
 * Looks up the specific key in the flow hash table.
 *   When the flow is not present:
 *     If create_if_missing is true, creates a new flow and returns it.
 *     Otherwise, returns NULL.
 */
static struct timeslot_dst *dst_lookup(struct fp_sched_data *q, u64 src_dst_key,
		bool create_if_missing)
{
	struct rb_node **p, *parent;
	struct rb_root *root;
	struct timeslot_dst *dst;
	u32 skb_hash;

	/* get the key's hash */
	skb_hash = src_dst_key_hash(src_dst_key);

	root = &q->dst_hash_tbl[skb_hash >> (32 - q->hash_tbl_log)];

	p = &root->rb_node;
	parent = NULL;
	while (*p) {
		parent = *p;

		dst = container_of(parent, struct timeslot_dst, fp_node);
		if (dst->src_dst_key == src_dst_key)
			return dst;

		if (dst->src_dst_key > src_dst_key)
			p = &parent->rb_right;
		else
			p = &parent->rb_left;
	}

	/* did not find existing entry */
	if (!create_if_missing)
		return NULL;

	/* allocate a new one */
	dst = kmem_cache_zalloc(timeslot_dst_cachep, GFP_ATOMIC | __GFP_NOWARN);
	if (unlikely(!dst)) {
		q->stat.allocation_errors++;
		return &q->reg_prio;
	}
	dst->src_dst_key = src_dst_key;

	rb_link_node(&dst->fp_node, parent, p);
	rb_insert_color(&dst->fp_node, root);
	list_init(&dst->skb_qs);
	dst->credit = 0;

	q->flows++;
	q->inactive_flows++;
	return dst;
}

static inline u64 get_mac(struct sk_buff *skb)
{
	struct ethhdr *ethh;
	u64 res;

	ethh = (struct ethhdr *)skb_mac_header(skb);
	res = ((u64)ntohs(*(__be16 *)&ethh->h_dest[0]) << 32)
				 | ntohl(*(__be32 *)&ethh->h_dest[2]);
//	FASTPASS_WARN("got ethernet %02X:%02X:%02X:%02X:%02X:%02X parsed 0x%012llX node_id %u\n",
//			ethh->h_dest[0],ethh->h_dest[1],ethh->h_dest[2],ethh->h_dest[3],
//			ethh->h_dest[4],ethh->h_dest[5], res, fp_map_mac_to_id(res));
	return res;
}

/* returns the flow for the given packet if it is a hi_prio packet, o/w returns NULL */
static struct timeslot_dst *classify_hi_prio(struct sk_buff *skb, struct fp_sched_data *q)
{
	__be16 proto = skb->protocol;
	int band;
	struct flow_keys keys;

	/* warning: no starvation prevention... */
	band = prio2band[skb->priority & TC_PRIO_MAX];
	if (unlikely(band == 0)) {
		if (unlikely(skb->sk != q->ctrl_sock->sk))
			q->stat.non_ctrl_highprio_pkts++;
		else
			q->stat.ctrl_pkts++;
		return &q->hi_prio;
	}

	// ARP packets should not count as classify errors
	switch (proto) {
	case __constant_htons(ETH_P_ARP):
		q->stat.arp_pkts++;
		return &q->reg_prio;

	case __constant_htons(ETH_P_1588):
	case __constant_htons(ETH_P_ALL):
		/* Special case the PTP broadcasts: MAC 01:1b:19:00:00:00 */
		if (likely(get_mac(skb) == 0x011b19000000)) {
			q->stat.ptp_pkts++;
			return &q->hi_prio;
		}
		goto cannot_classify;

	case __constant_htons(ETH_P_IPV6):
	case __constant_htons(ETH_P_IP):
		goto ipv4_ipv6;

	default:
		goto cannot_classify;
	}

ipv4_ipv6:
	/* special cases for PTP and NTP over IP */
	if (!skb_flow_dissect(skb, &keys))
		goto cannot_classify;

	/* special case for important packets, let through with high priority */
	switch(keys.ip_proto) {
	case IPPROTO_IGMP:
		/* IGMP is used for PTP multicast membership, allow all of them */
		q->stat.igmp_pkts++;
		return &q->reg_prio;
	case IPPROTO_UDP:
		/* NTP packets */
		if (unlikely(keys.port16[1] == __constant_htons(123))) {
			q->stat.ntp_pkts++;
			return &q->hi_prio;
		}
		/* PTP packets are port 319,320 */
		if (((ntohs(keys.port16[1]) - 1) & ~1) == 318) {
			q->stat.ptp_pkts++;
			return &q->hi_prio;
		}
		break;
	case IPPROTO_TCP:
		/* SSH packets, so we can access server */
		if (unlikely(keys.port16[0] == __constant_htons(22)
				|| keys.port16[1] == __constant_htons(22))) {
			q->stat.ssh_pkts++;
			return &q->reg_prio;
		}
		break;
	default:
		break;
	}

	return NULL;

cannot_classify:
	q->stat.classify_errors++;
	fp_debug("cannot classify packet with protocol %u:\n", skb->protocol);
	print_hex_dump(KERN_DEBUG, "cannot classify: ", DUMP_PREFIX_OFFSET,
			16, 1, skb->data, min_t(size_t, skb->len, 64), false);

	return &q->reg_prio;
}

/* returns the flow for the given packet, allocates a new flow if needed */
static struct timeslot_dst *classify_data(struct sk_buff *skb, struct fp_sched_data *q)
{
	u64 src_dst_key;
	u64 masked_mac;

	/* get MAC address */
	src_dst_key = get_mac(skb);

	/* get the skb's key (src_dst_key) */
	masked_mac = src_dst_key & MANUFACTURER_MAC_MASK;
	if (unlikely((masked_mac == VRRP_SWITCH_MAC_PREFIX)
			  || (masked_mac == CISCO_SWITCH_MAC_PREFIX)))
		src_dst_key = OUT_OF_BOUNDARY_NODE_ID;
	else
		src_dst_key = fp_map_mac_to_id(src_dst_key);

	q->stat.data_pkts++;
	return dst_lookup(q, src_dst_key, true);
}

/* enqueue packet to the qdisc (part of the qdisc api) */
static int fpq_enqueue(struct sk_buff *skb, struct Qdisc *sch)
{
	struct fp_sched_data *q = qdisc_priv(sch);
	struct timeslot_dst *dst;

	dst = classify_hi_prio(skb, q);

	/* high prio flows enqueued directly */
	if (unlikely(dst != NULL)) {
		skb_q_enqueue(dst, skb);
		sch->q.qlen++;
		fp_debug("enqueued hi-prio packet of len %d to flow 0x%llX, qlen=%d\n",
				qdisc_pkt_len(skb), dst->src_dst_key, dst->qlen);

		/* unthrottle qdisc */
		qdisc_unthrottled(sch);
		__netif_schedule(qdisc_root(sch));

		return NET_XMIT_SUCCESS;
	}

	/* this is a data packet */

	/* enforce qdisc packet limit on data packets */
	if (unlikely(sch->q.qlen >= sch->limit))
		return qdisc_drop(skb, sch);

	spin_lock(&q->enqueue_lock);
	skb_q_enqueue(sch, &q->enqueue_skb_q, skb, true);
	spin_unlock(&q->enqueue_lock);

	sch->q.qlen++;
	tasklet_schedule(&q->enqueue_tasklet);

	return NET_XMIT_SUCCESS;
}

static void enqueue_single_skb(struct fp_sched_data *q, struct sk_buff *skb)
{
	struct timeslot_dst *dst;
	struct timeslot_skb_q *timeslot_q;
	s64 cost;

	cost = (s64) psched_l2t_ns(&q->data_rate, qdisc_pkt_len(skb));

	spin_lock(&q->hash_tbl_lock);
	dst = classify_data(skb, q);

	/* check if need to request a new slot */
	if (cost > dst->credit) {
		timeslot_q = kmem_cache_alloc(timeslot_skb_q_cachep, GFP_ATOMIC | __GFP_NOWARN);
		if (unlikely(timeslot_q == NULL))
			goto cannot_alloc_skb_q;

		if (unlikely(list_empty(&dst->skb_qs)))
			q->inactive_flows--;

		skb_q_init(timeslot_q);
		list_add_tail(&timeslot_q->list, &dst->skb_qs);
		dst->credit = q->tslot_len_approx;
		q->added_tslots++;

		/* packets bigger than a timeslot cause warning and still get timeslot */
		if (unlikely(cost > q->tslot_len_approx)) {
			FASTPASS_WARN("got packet that is larger than a timeslot len=%d\n",
				qdisc_pkt_len(skb));
			q->stat.pkt_too_big++;
		}
	} else {
		timeslot_q = list_entry(dst->skb_qs.prev, struct timeslot_skb_q, list);
	}

	dst->credit -= cost;
	skb_q_enqueue(timeslot_q, skb);
	spin_unlock(&q->hash_tbl_lock);

	fp_debug("enqueued data packet of len %d to flow 0x%llX, qlen=%d\n",
			qdisc_pkt_len(skb), dst->src_dst_key, dst->qlen);

	return;

cannot_alloc_skb_q:
	FASTPASS_WARN("allocation of timeslot_skb_q failed, enqueueing to prequeue");
	spin_lock(&q->prequeue_lock);
	skb_q_enqueue(&q->prequeue, skb);
	spin_unlock(&q->prequeue_lock);

	/* unthrottle qdisc */
	qdisc_unthrottled(sch);
	__netif_schedule(qdisc_root(sch));
}

static void enqueue_tasklet_func(unsigned long int param)
{
	struct Qdisc *sch = (struct Qdisc *)param;
	struct fp_sched_data *q = qdisc_priv(sch);
	struct sk_buff *skb, *next;


	spin_lock(&q->enqueue_lock);
	next = q->enqueue_skb_q.head;
	q->enqueue_skb_q.head = NULL;
	spin_unlock(&q->enqueue_lock);

	while ((skb = next) != NULL) {
		next = skb->next;
		skb->next = NULL;
		enqueue_single_skb(q, skb);
	}
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
	struct timeslot_dst *f;
	u64 tslot_advance;
	u32 moved_timeslots = 0;
	u64 now_monotonic = fp_monotonic_time_ns();

	q->current_timeslot = (now_real * q->tslot_mul) >> q->tslot_shift;
	q->next_zero_queue_time = max_t(u64, q->next_zero_queue_time, now_monotonic);


begin:
	if (unlikely(wnd_empty(&q->alloc_wnd)))
		goto done;

	next_nonempty = wnd_earliest_marked(&q->alloc_wnd);
	next_key = q->schedule[wnd_pos(next_nonempty)];

	/* is this alloc too far in the future? */
	if (unlikely(time_after64(next_nonempty, q->current_timeslot + q->max_preload)))
		goto done; /* won't move it */

	/* look up the flow of this allocation */
	f = dst_lookup(q, fp_alloc_node(next_key), false);
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

	if (time_after64(q->next_zero_queue_time, now_monotonic + q->max_dev_backlog_ns)) {
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

void timeslot_admit_now(void *priv, u64 src_dst_key)
{
	struct fp_sched_data *q = priv_to_sched_data(priv);
	int i;
	u8 spec;
	int dst_ind;
	u64 full_tslot;
	u64 now_real = fp_get_time_ns();
	u16 node_id;
	u64 current_timeslot;
	struct timeslot_dst *dst;
	struct timeslot_skb_q *timeslot_q;

	/* find the mentioned destination */
	spin_lock(&q->hash_tbl_lock);
	dst = dst_lookup(q, src_dst_key, false);
	if (unlikely(dst == NULL)) {
		FASTPASS_WARN("couldn't find flow 0x%X from alloc.\n", src_dst_key);
		q->stat.alloc_flow_not_found++;
		return;
	}

	/* are there timeslots waiting? */
	if (unlikely(list_empty(&dst->skb_qs))) {
		/* got an alloc without a timeslot */
		q->stat.unwanted_alloc++;
		fp_debug("got an allocation over demand, flow 0x%04llX\n",
				dst->src_dst_key);
		return;
	}

	/* get a timeslot's worth skb_q */
	timeslot_q = list_first_entry(&dst->skb_qs, struct timeslot_skb_q, list);
	list_del(dst->skb_qs.next);
	/* if we dequeued the last skb, make sure it has no remaining credit */
	if (unlikely(list_empty(&dst->skb_qs))) {
		dst->credit = 0;
		q->inactive_flows++;
	}
	q->used_tslots++;
	spin_unlock(&q->hash_tbl_lock);

	/* put in prequeue */
	spin_lock(&q->prequeue_lock);
	skb_q_append(&q->prequeue, timeslot_q);
	spin_unlock(&q->prequeue_lock);

	/* unthrottle qdisc */
	qdisc_unthrottled(sch);
	__netif_schedule(qdisc_root(sch));

	/* free the timeslot_q */
	kmem_cache_free(timeslot_skb_q_cachep, timeslot_q);
}

/* Extract packet from the queue (part of the qdisc API) */
static struct sk_buff *fpq_dequeue(struct Qdisc *sch)
{
	struct fp_sched_data *q = qdisc_priv(sch);
	struct sk_buff *skb;

	/* try hi_prio queue first */
	skb = skb_q_dequeue(sch, &q->hi_prio, true);
	if (skb)
		goto out_got_skb;

	/* any packets already queued? */
	skb = skb_q_dequeue(sch, &q->reg_prio, true);
	if (skb)
		goto out_got_skb;

	/* try to get ready skbs from the prequeue */
	spin_lock(&q->prequeue_lock);
	if (!skb_q_empty(&q->prequeue))
		skb_q_move(&q->reg_prio, &q->prequeue);
	spin_unlock(&q->prequeue_lock);

	/* try the internal queue again, might be non-empty after timeslot update*/
	skb = skb_q_dequeue(sch, &q->reg_prio, true);
	if (skb)
		goto out_got_skb;

	/* no packets in queue, go to sleep */
	qdisc_throttled(sch);
	/* will re-read time, to make sure we sleep >0 time */
	qdisc_watchdog_schedule_ns(&q->watchdog,
				   fp_monotonic_time_ns() + q->update_timeslot_timer_ns);
	return NULL;

out_got_skb:
	qdisc_bstats_update(sch, skb);
	qdisc_unthrottled(sch);
	return skb;
}

/* resets the state of the qdisc (part of qdisc API) */
static void fp_tc_reset(struct Qdisc *sch)
{
	struct fp_sched_data *q = qdisc_priv(sch);
	struct rb_root *root;
	struct sk_buff *skb;
	struct rb_node *p;
	struct timeslot_dst *dst;
	struct timeslot_skb_q *timeslot_q, *next_q;
	unsigned int idx;

	while ((skb = skb_q_dequeue(sch, &q->reg_prio, false)) != NULL)
		kfree_skb(skb);
	while ((skb = skb_q_dequeue(sch, &q->hi_prio, false)) != NULL)
		kfree_skb(skb);

	spin_lock(&q->enqueue_lock);
	while ((skb = skb_q_dequeue(sch, &q->enqueue_skb_q, false)) != NULL)
		kfree_skb(skb);
	spin_unlock(&q->enqueue_lock);

	spin_lock(&q->prequeue_lock);
	while ((skb = skb_q_dequeue(sch, &q->prequeue, false)) != NULL)
		kfree_skb(skb);
	spin_unlock(&q->prequeue_lock);

	spin_lock(&q->hash_tbl_lock);
	for (idx = 0; idx < (1U << q->hash_tbl_log); idx++) {
		root = &q->dst_hash_tbl[idx];
		while ((p = rb_first(root)) != NULL) {
			dst = container_of(p, struct timeslot_dst, fp_node);
			rb_erase(p, root);

			list_for_each_entry_safe(timeslot_q, next_q, &dst->skb_qs, list) {
				while ((skb = skb_q_dequeue(timeslot_q)) != NULL)
					kfree_skb(skb);
				kmem_cache_free(timeslot_skb_q_cachep, timeslot_q);
			}

			kmem_cache_free(timeslot_dst_cachep, dst);
		}
	}
	spin_unlock(&q->hash_tbl_lock);

	wnd_reset(&q->alloc_wnd, q->current_timeslot);
	q->flows			= 0;
	q->inactive_flows	= 0;
	q->added_tslots		= 0;
	q->used_tslots		= 0;
	q->next_zero_queue_time = fp_monotonic_time_ns();
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
	struct timeslot_dst *of, *nf;
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
			of = container_of(op, struct timeslot_dst, fp_node);
			skb_hash = src_dst_key_hash(of->src_dst_key);
			nroot = &new_array[skb_hash >> (32 - new_log)];

			/* insert in tree */
			np = &nroot->rb_node;
			parent = NULL;
			while (*np) {
				parent = *np;

				nf = container_of(parent, struct timeslot_dst, fp_node);
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

	if (q->dst_hash_tbl && log == q->hash_tbl_log)
		return 0;

	array = kmalloc(sizeof(struct rb_root) << log, GFP_ATOMIC);
	if (!array)
		return -ENOMEM;

	for (idx = 0; idx < (1U << log); idx++)
		array[idx] = RB_ROOT;

	if (q->dst_hash_tbl) {
		fp_tc_rehash(q, q->dst_hash_tbl, q->hash_tbl_log, array, log);
		kfree(q->dst_hash_tbl);
	}
	q->dst_hash_tbl = array;
	q->hash_tbl_log = log;

	return 0;
}

/**
 * Performs a reset and garbage collection of flows
 */
void timeslot_garbage_collect(struct Qdisc *sch)
{
	struct fp_sched_data *q = qdisc_priv(sch);

	struct rb_node *cur, *next;
	struct rb_root *root;
	struct timeslot_dst *dst;
	u32 idx;
	u32 base_idx = src_dst_key_hash(fp_monotonic_time_ns()) >> (32 - q->hash_tbl_log);
	u32 mask = (1U << q->hash_tbl_log) - 1;

	/* for each cell in hash table: */
	for (idx = 0; idx < (1U << q->hash_tbl_log); idx++) {
		root = &q->dst_hash_tbl[(idx + base_idx) & mask];
		next = rb_first(root); /* we traverse tree in-order */

		/* while haven't finished traversing rbtree: */
		while (next != NULL) {
			cur = next;
			next = rb_next(cur);

			dst = container_of(cur, struct timeslot_dst, fp_node);

			/* can we garbage-collect this flow? */
			if (list_empty(&dst->skb_qs)) {
				/* yes, let's gc */
				/* erase from old tree */
				rb_erase(cur, root);
				fp_debug("gc flow 0x%04llX, used %llu timeslots\n",
						dst->src_dst_key, dst->demand_tslots);
				kmem_cache_free(timeslot_dst_cachep, dst);
				q->stat.gc_flows++;
				continue;
			}
		}
	}
}

/* netlink protocol data */
static const struct nla_policy fp_policy[TCA_FASTPASS_MAX + 1] = {
	[TCA_FASTPASS_PLIMIT]			= { .type = NLA_U32 },
	[TCA_FASTPASS_BUCKETS_LOG]		= { .type = NLA_U32 },
	[TCA_FASTPASS_DATA_RATE]		= { .type = NLA_U32 },
	[TCA_FASTPASS_TIMESLOT_NSEC]	= { .type = NLA_U32 },
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

static void timeslot_rcu_free(struct rcu_head *head)
{
	struct rcu_hash_tbl_cleanup *cleanup =
			container_of(head, struct rcu_hash_tbl_cleanup, rcu_head);

	fp_debug("freeing hash table\n");
	kfree(cleanup->hash_tbl);
	kfree(cleanup);
	fp_debug("done\n");
}

/* destroy the qdisc (part of qdisc API) */
static void fp_tc_destroy(struct Qdisc *sch)
{
	struct fp_sched_data *q = qdisc_priv(sch);

	q->timeslot_ops->stop_qdisc(sched_data_to_priv(q));

	qdisc_watchdog_cancel(&q->watchdog);

	fp_debug("resetting qdisc\n");
	fp_tc_reset(sch);
	fp_debug("done resetting qdisc. setting up rcu\n");
	q->hash_tbl_cleanup->hash_tbl = q->dst_hash_tbl;
	call_rcu(&q->hash_tbl_cleanup->rcu_head, timeslot_rcu_free);
	fp_debug("done\n");
}

/* initialize a new qdisc (part of qdisc API) */
static int fp_tc_init(struct Qdisc *sch, struct nlattr *opt)
{
	struct fp_sched_data *q = qdisc_priv(sch);
	struct timeslot_reg *reg = container_of(sch->ops, struct timeslot_reg, qdisc_ops);
	u64 now_real = fp_get_time_ns();
	u64 now_monotonic = fp_monotonic_time_ns();
	struct tc_ratespec data_rate_spec ={
#if LINUX_VERSION_CODE != KERNEL_VERSION(3,2,45)
			.linklayer = TC_LINKLAYER_ETHERNET,
#endif
			.rate = 1e9/8,
			.overhead = 24};
	int err;

	/* defaults */
	sch->limit			= 10000;
	q->hash_tbl_log		= ilog2(1024);
	q->tslot_mul		= 1;
	q->tslot_shift		= 20;
	q->miss_threshold	= 5;
	q->max_dev_backlog_ns = (2 << 20);
	q->max_preload		= 5;
	q->update_timeslot_timer_ns = 2048;


	psched_ratecfg_precompute(&q->data_rate, &data_rate_spec);
	q->dst_hash_tbl	= NULL;
	skb_q_init(&q->enqueue_skb_q);
	skb_q_init(&q->reg_prio);
	skb_q_init(&q->hi_prio);
	skb_q_init(&q->prequeue);
	q->next_zero_queue_time = now_monotonic;

	/* calculate timeslot from beginning of Epoch */
	q->tslot_len_approx		= (1 << q->tslot_shift);
	do_div(q->tslot_len_approx, q->tslot_mul);
	q->current_timeslot = (now_real * q->tslot_mul) >> q->tslot_shift;
	wnd_reset(&q->alloc_wnd, q->current_timeslot);

	q->flows			= 0;
	q->inactive_flows	= 0;
	q->added_tslots		= 0;
	q->used_tslots		= 0;

	qdisc_watchdog_init(&q->watchdog, sch);

	tasklet_init(&q->enqueue_tasklet, &enqueue_tasklet_func, (unsigned long int)sch);

	q->hash_tbl_cleanup = kmalloc(sizeof(struct rcu_hash_tbl_cleanup), GFP_ATOMIC);
	if (q->hash_tbl_cleanup == NULL) {
		err = -ENOMEM;
		goto out;
	}

	if (opt) {
		err = fp_tc_change(sch, opt);
	} else {
		err = fp_tc_resize(q, q->hash_tbl_log);
	}
	if (err)
		goto out_free_cleanup;

	q->timeslot_ops = reg->ops;
	err = reg->ops->new_qdisc(sched_data_to_priv(q));
	if (err)
		goto out_free_hash_tbl;

	return err;

out_free_hash_tbl:
	kfree(q->hash_tbl_log);
out_free_cleanup:
	kfree(q->hash_tbl_cleanup);
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
	    nla_put_u32(skb, TCA_FASTPASS_BUCKETS_LOG, q->hash_tbl_log) ||
	    nla_put_u32(skb, TCA_FASTPASS_DATA_RATE, (u32)(q->data_rate.rate_bps >> 3)) ||
	    nla_put_u32(skb, TCA_FASTPASS_TIMESLOT_NSEC, q->tslot_len_approx) ||
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

struct timeslot_reg *timeslot_register_qdisc(struct timeslot_ops *ops)
{
	int err = -ENOMEM;
	struct timeslot_reg *reg;
	struct Qdisc_ops qops;

	pr_info("%s: initializing\n", __func__);

	reg = kmalloc(sizeof(struct timeslot_reg), GFP_ATOMIC);
	if (!reg)
		goto out;

	reg->ops = ops;
	qops = &reg->qdisc_ops;
	memset(qops, 0, sizeof(*qops));
	memcpy(qops->id, ops->id, sizeof(qops->id));
	qops->priv_size = QDISC_ALIGN(sizeof(struct fp_sched_data)) + ops->priv_size;
	qops->enqueue	=	fpq_enqueue,
	qops->dequeue	=	fpq_dequeue,
	qops->peek		=	qdisc_peek_dequeued,
	qops->init		=	fp_tc_init,
	qops->reset		=	fp_tc_reset,
	qops->destroy	=	fp_tc_destroy,
	qops->change		=	fp_tc_change,
	qops->dump		=	fp_tc_dump,
	qops->dump_stats	=	fp_tc_dump_stats,
	qops->owner		=	THIS_MODULE,

	err = register_qdisc(qops);
	if (err)
		goto out_destroy_caches;

	pr_info("%s: success\n", __func__);
	return 0;

out_destroy_caches:
	kmem_cache_destroy(timeslot_skb_q_cachep);
out_destroy_timeslot_reg:
	kfree(reg);
out:
	pr_info("%s: failed, err=%d\n", __func__, err);
	return NULL;
}

void timeslot_unregister_qdisc(struct timeslot_reg *reg)
{
	pr_info("%s: begin\n", __func__);
	unregister_qdisc(&reg->qdisc_ops);
	pr_info("%s: end\n", __func__);
}

int timeslot_init(void)
{
	timeslot_dst_cachep = kmem_cache_create("timeslot_flow_cache",
					   sizeof(struct timeslot_dst),
					   0, 0, NULL);
	if (!timeslot_dst_cachep)
		goto out;

	timeslot_skb_q_cachep = kmem_cache_create("timeslot_skb_q_cache",
					   sizeof(struct timeslot_skb_q),
					   0, 0, NULL);
	if (!timeslot_skb_q_cachep)
		goto out_destroy_dst_cache;

out_destroy_dst_cache:
	kmem_cache_destroy(timeslot_dst_cachep);
out:
	pr_info("%s: failed, couldn't kmem_cache_create\n", __func__);
}

void timeslot_exit(void)
{
	pr_info("%s: begin\n", __func__);
	kmem_cache_destroy(timeslot_skb_q_cachep);
	kmem_cache_destroy(timeslot_dst_cachep);
	pr_info("%s: end\n", __func__);
}

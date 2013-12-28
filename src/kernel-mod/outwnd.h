/*
 * outwnd.h
 *
 *  Created on: Dec 23, 2013
 *      Author: yonch
 */

#ifndef OUTWND_H_
#define OUTWND_H_

#include "window.h"

struct fpproto_outwnd {
	struct fpproto_pktdesc	*bins[(1 << FASTPASS_WND_LOG)];
	struct fp_window		wnd;
};

/**
 * Assumes seqno is in the correct range, returns whether the bin is unacked.
 */
static inline bool outwnd_is_unacked(struct fpproto_outwnd *ow, u64 seqno)
{
	return wnd_is_marked(&ow->wnd, seqno);
}

static inline u64 outwnd_head(struct fpproto_outwnd *ow)
{
	return wnd_head(&ow->wnd);
}

static inline u64 outwnd_edge(struct fpproto_outwnd *ow)
{
	return wnd_edge(&ow->wnd);
}

/* returns true if seqno is strictly before the window */
static inline bool outwnd_before_wnd(struct fpproto_outwnd *ow, u64 seqno)
{
	return time_before64(seqno, wnd_edge(&ow->wnd));
}

/* returns true if seqno is strictly after the window */
static inline bool outwnd_after_wnd(struct fpproto_outwnd *ow, u64 seqno)
{
	return time_after64(seqno, wnd_head(&ow->wnd));
}

static inline u32 outwnd_num_unacked(struct fpproto_outwnd *ow)
{
	return wnd_num_marked(&ow->wnd);
}

/**
 * Adds the packet descriptor as the next_seq
 */
static inline void outwnd_add(struct fpproto_outwnd *ow, struct fpproto_pktdesc *pd)
{
	BUG_ON(!pd);

	wnd_advance(&ow->wnd, 1);
	wnd_mark(&ow->wnd, ow->wnd.head);
	ow->bins[wnd_pos(ow->wnd.head)] = pd;
}

/**
 * Removes the packet description with the given seqno, marking it as acked.
 *    Returns the removed packet.
 *
 * Assumes the seqno is in the correct range.
 */
static inline struct fpproto_pktdesc * outwnd_pop(struct fpproto_outwnd *ow, u64 seqno)
{
	u32 seqno_index = wnd_pos(seqno);
	struct fpproto_pktdesc *res = ow->bins[seqno_index];

	wnd_clear(&ow->wnd, seqno);
	ow->bins[seqno_index] = NULL;
	return res;
}

/**
 * Returns (@seqno - first_seqno), where first_seqno is the sequence no. of the
 *    first unacked packet *at* or *before* @seqno if such exists within the
 *    window, or -1 if it doesn't.
 */
static inline s32 outwnd_at_or_before(struct fpproto_outwnd *ow, u64 seqno)
{
	return wnd_at_or_before(&ow->wnd, seqno);
}

/**
 * Returns the sequence no of the earliest unacked packet.
 * Assumes such a packet exists!
 */
static inline u64 outwnd_earliest_unacked(struct fpproto_outwnd *ow)
{
	return wnd_earliest_marked(&ow->wnd);
}

static inline void outwnd_reset(struct fpproto_outwnd* ow, u64 head_seqno)
{
	wnd_reset(&ow->wnd, head_seqno);
}

static inline bool outwnd_empty(struct fpproto_outwnd* ow)
{
	return wnd_empty(&ow->wnd);
}

/**
 * Returns the pktdesc of the descriptor with @seqno
 * Assumes @seqno is within the window and unacked
 */
static inline
struct fpproto_pktdesc *outwnd_peek(struct fpproto_outwnd* ow, u64 seqno)
{
	return ow->bins[wnd_pos(seqno)];
}

static inline void outwnd_test(struct fpproto_outwnd *ow) {
	u64 tslot;
	s32 gap;
	int i;
	const int BASE = 10007;

	fastpass_pr_debug("testing outwnd\n");
	outwnd_reset(ow, BASE - 1);
	for(tslot = BASE - FASTPASS_WND_LEN; tslot < BASE; tslot++) {
		BUG_ON(outwnd_at_or_before(ow, tslot) != -1);
		BUG_ON(outwnd_is_unacked(ow, tslot));
	}

	for(i = 0; i < FASTPASS_WND_LEN; i++)
		outwnd_add(ow, (struct fpproto_pktdesc *)(0xFF00L + i));

	for(tslot = BASE; tslot < BASE + FASTPASS_WND_LEN; tslot++) {
		BUG_ON(!outwnd_is_unacked(ow, tslot));
		BUG_ON(outwnd_at_or_before(ow, tslot) != 0);
	}

	BUG_ON(outwnd_earliest_unacked(ow) != BASE);
	BUG_ON(outwnd_pop(ow, BASE) != (void *)0xFF00L);
	BUG_ON(outwnd_earliest_unacked(ow) != BASE+1);
	BUG_ON(outwnd_at_or_before(ow, BASE) != -1);
	BUG_ON(outwnd_at_or_before(ow, BASE+1) != 0);
	BUG_ON(outwnd_pop(ow, BASE+2) != (void *)0xFF02L);
	BUG_ON(outwnd_earliest_unacked(ow) != BASE+1);
	BUG_ON(outwnd_at_or_before(ow, BASE+2) != 1);

	for(tslot = BASE+3; tslot < BASE + 152; tslot++) {
		BUG_ON(outwnd_pop(ow, tslot) != (void *)0xFF00L + tslot - BASE);
		BUG_ON(outwnd_is_unacked(ow, tslot));
		BUG_ON(outwnd_at_or_before(ow, tslot) != tslot - BASE - 1);
		BUG_ON(outwnd_at_or_before(ow, tslot+1) != 0);
		BUG_ON(outwnd_earliest_unacked(ow) != BASE+1);
	}
	for(tslot = BASE+152; tslot < BASE + FASTPASS_WND_LEN; tslot++) {
		BUG_ON(!outwnd_is_unacked(ow, tslot));
		BUG_ON(outwnd_at_or_before(ow, tslot) != 0);
	}

	BUG_ON(outwnd_pop(ow, BASE+1) != (void *)0xFF01L);
	BUG_ON(outwnd_earliest_unacked(ow) != BASE+152);

	fastpass_pr_debug("done testing outwnd, cleaning up\n");

	/* clean up */
	tslot = outwnd_head(ow);
clear_next_unacked:
	gap = outwnd_at_or_before(ow, tslot);
	if (gap >= 0) {
		tslot -= gap;
		BUG_ON(outwnd_pop(ow, tslot) != (void *)0xFF00L + tslot - BASE);
		goto clear_next_unacked;
	}

	/* make sure pointer array is clean */
	for (i = 0; i < FASTPASS_WND_LEN; i++)
		BUG_ON(ow->bins[i] != NULL);
}

#endif /* OUTWND_H_ */

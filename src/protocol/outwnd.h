/*
 * outwnd.h
 *
 *  Created on: Dec 23, 2013
 *      Author: yonch
 */

#ifndef FP_OUTWND_H_
#define FP_OUTWND_H_

#include "window.h"

/**
 * Adds the packet descriptor as the next_seq
 */
static inline
void outwnd_add(struct fpproto_conn *conn, struct fpproto_pktdesc *pd)
{
	BUG_ON(!pd);

	wnd_advance(&conn->outwnd, 1);
	wnd_mark(&conn->outwnd, conn->outwnd.head);
	conn->unacked_pkts[wnd_pos(conn->outwnd.head)] = pd;
}

/**
 * Removes the packet description with the given seqno, marking it as acked.
 *    Returns the removed packet.
 *
 * Assumes the seqno is in the correct range.
 */
static inline
struct fpproto_pktdesc *outwnd_pop(struct fpproto_conn *conn, u64 seqno)
{
	u32 seqno_index = wnd_pos(seqno);
	struct fpproto_pktdesc *res = conn->unacked_pkts[seqno_index];

	wnd_clear(&conn->outwnd, seqno);
	conn->unacked_pkts[seqno_index] = NULL;
	return res;
}

/**
 * Returns the pktdesc of the descriptor with @seqno
 * Assumes @seqno is within the window and unacked
 */
static inline
struct fpproto_pktdesc *outwnd_peek(struct fpproto_conn* conn, u64 seqno)
{
	return conn->unacked_pkts[wnd_pos(seqno)];
}

static inline void outwnd_test(struct fpproto_conn* conn) {
	u64 tslot;
	s32 gap;
	u32 i;
	const u32 BASE = 10007;

	struct fp_window *ow = &conn->outwnd;

	fp_debug("testing outwnd\n");
	wnd_reset(ow, BASE - 1);
	for(tslot = BASE - FASTPASS_WND_LEN; tslot < BASE; tslot++) {
		BUG_ON(wnd_at_or_before(ow, tslot) != -1);
		BUG_ON(wnd_is_marked(ow, tslot));
	}

	for(i = 0; i < FASTPASS_WND_LEN; i++)
		outwnd_add(conn, (struct fpproto_pktdesc *)(0xFF00L + i));

	for(tslot = BASE; tslot < BASE + FASTPASS_WND_LEN; tslot++) {
		BUG_ON(!wnd_is_marked(ow, tslot));
		BUG_ON(wnd_at_or_before(ow, tslot) != 0);
	}

	BUG_ON(wnd_earliest_marked(ow) != BASE);
	BUG_ON(outwnd_pop(conn, BASE) != (void *)0xFF00L);
	BUG_ON(wnd_earliest_marked(ow) != BASE+1);
	BUG_ON(wnd_at_or_before(ow, BASE) != -1);
	BUG_ON(wnd_at_or_before(ow, BASE+1) != 0);
	BUG_ON(outwnd_pop(conn, BASE+2) != (void *)0xFF02L);
	BUG_ON(wnd_earliest_marked(ow) != BASE+1);
	BUG_ON(wnd_at_or_before(ow, BASE+2) != 1);

	for(tslot = BASE+3; tslot < BASE + 152; tslot++) {
		BUG_ON(outwnd_pop(conn, tslot) != (void *)(0xFF00L + tslot - BASE));
		BUG_ON(wnd_is_marked(ow, tslot));
		BUG_ON(wnd_at_or_before(ow, tslot) != (int)(tslot - BASE - 1));
		BUG_ON(wnd_at_or_before(ow, tslot+1) != 0);
		BUG_ON(wnd_earliest_marked(ow) != BASE+1);
	}
	for(tslot = BASE+152; tslot < BASE + FASTPASS_WND_LEN; tslot++) {
		BUG_ON(!wnd_is_marked(ow, tslot));
		BUG_ON(wnd_at_or_before(ow, tslot) != 0);
	}

	BUG_ON(outwnd_pop(conn, BASE+1) != (void *)0xFF01L);
	BUG_ON(wnd_earliest_marked(ow) != BASE+152);

	fp_debug("done testing outwnd, cleaning up\n");

	/* clean up */
	tslot = wnd_head(ow);
clear_next_unacked:
	gap = wnd_at_or_before(ow, tslot);
	if (gap >= 0) {
		tslot -= gap;
		BUG_ON(outwnd_pop(conn, tslot) != (void *)(0xFF00L + tslot - BASE));
		goto clear_next_unacked;
	}

	/* make sure pointer array is clean */
	for (i = 0; i < FASTPASS_WND_LEN; i++)
		BUG_ON(conn->unacked_pkts[i] != NULL);
}

#endif /* FP_OUTWND_H_ */

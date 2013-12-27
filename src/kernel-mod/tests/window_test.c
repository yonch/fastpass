/*
 * window_test.c
 *
 *  Created on: Dec 24, 2013
 *      Author: yonch
 */

typedef _Bool			bool;

#include "../linux-compat.h"
#include "../window.h"

void bulk_test(u64 BASE, u64 seqno, u32 amount, u64 m0, u64 m1, u64 m2, u64 m3, u64 e_summary)
{
	int i;
	struct fp_window wnd;
	struct fp_window *wndp = &wnd;
	wnd_reset(wndp, BASE-1);
	wnd_advance(wndp, FASTPASS_WND_LEN);
	wnd_mark_bulk(wndp, seqno, amount);
	BUG_ON(wndp->summary != e_summary);
	BUG_ON(wndp->marked[0] != m0);
	BUG_ON(wndp->marked[1] != m1);
	BUG_ON(wndp->marked[2] != m2);
	BUG_ON(wndp->marked[3] != m3);
}


/* test */
int main(void) {
	u64 tslot;
	s32 gap;
	int i;
	const int BASE = 10071;
	struct fp_window wnd;
	struct fp_window *wndp = &wnd;

	wnd_reset(wndp, BASE - 1);
	for(tslot = BASE - FASTPASS_WND_LEN; tslot < BASE; tslot++) {
		BUG_ON(wnd_at_or_before(wndp, tslot) != -1);
		BUG_ON(wnd_is_marked(wndp, tslot));
	}

	for(i = 0; i < FASTPASS_WND_LEN; i++) {
		wnd_advance(wndp, 1);
		wnd_mark(wndp, wnd.head);
	}

	for(tslot = BASE; tslot < BASE + FASTPASS_WND_LEN; tslot++) {
		BUG_ON(!wnd_is_marked(wndp, tslot));
		BUG_ON(wnd_at_or_before(wndp, tslot) != 0);
	}

	BUG_ON(wnd_earliest_marked(wndp) != BASE);
	wnd_clear(wndp, BASE);
	BUG_ON(wnd_earliest_marked(wndp) != BASE+1);
	BUG_ON(wnd_at_or_before(wndp, BASE) != -1);
	BUG_ON(wnd_at_or_before(wndp, BASE+1) != 0);
	wnd_clear(wndp, BASE+2);
	BUG_ON(wnd_earliest_marked(wndp) != BASE+1);
	BUG_ON(wnd_at_or_before(wndp, BASE+2) != 1);

	for(tslot = BASE+3; tslot < BASE + 152; tslot++) {
		wnd_clear(wndp, tslot);
		BUG_ON(wnd_is_marked(wndp, tslot));
		BUG_ON(wnd_at_or_before(wndp, tslot) != tslot - BASE - 1);
		BUG_ON(wnd_at_or_before(wndp, tslot+1) != 0);
		BUG_ON(wnd_earliest_marked(wndp) != BASE+1);
	}
	for(tslot = BASE+152; tslot < BASE + FASTPASS_WND_LEN; tslot++) {
		BUG_ON(!wnd_is_marked(wndp, tslot));
		BUG_ON(wnd_at_or_before(wndp, tslot) != 0);
	}

	wnd_clear(wndp, BASE+1);
	BUG_ON(wnd_earliest_marked(wndp) != BASE+152);

	/* prepared for BASE = 10071 */
	/* all marks within a single word, first word */
	bulk_test(BASE, BASE+18, 16, 0,0x1FFFE0000000000UL,0,0, 0x8);
	/* all marks within a single word, second word */
	bulk_test(BASE, BASE+18+64, 16, 0,0,0x1FFFE0000000000UL,0, 0x4);
	/* all marks within a single word, last word */
	bulk_test(BASE, BASE+3*64-19, 16, 0xFFFF0UL,0,0,0, 0x1);
	/* span multiple words, at word boundary */
	bulk_test(BASE, BASE+41, 128, 0,0,~0UL,~0UL, 0x6);
	/* span multiple words, with intermediate*/
	bulk_test(BASE, BASE+37, 4+128+9, 0x1FF,0xFUL << 60,~0UL,~0UL, 0xf);


	/* test getting bitmap */
	wnd_reset(wndp, BASE-1);
	wnd_advance(wndp, FASTPASS_WND_LEN);
	wnd_mark(wndp, BASE);
	wnd_mark(wndp, BASE + 1);
	wnd_mark(wndp, BASE + FASTPASS_WND_LEN - 1);
	wnd_mark(wndp, BASE + FASTPASS_WND_LEN - 3);
	wnd_mark(wndp, BASE + FASTPASS_WND_LEN - 33);
	wnd_mark(wndp, BASE + 42);
	/* last word */
	BUG_ON(wnd_get_mask(wndp, BASE + 2) != (3UL << 61));
	/* last bit */
	BUG_ON(wnd_get_mask(wndp, BASE) != (1UL << 63));
	/* just before last bit */
	BUG_ON(wnd_get_mask(wndp, BASE - 1) != 0);
	/* two bits before last bit - shouldn't catch bit BASE+FASTPASS_WND_LEN-1 */
	BUG_ON(wnd_get_mask(wndp, BASE - 2) != 0);
	/* full word (last word) */
	BUG_ON(wnd_get_mask(wndp, BASE + 40) != (3UL << 23));
	/* two words */
	BUG_ON(wnd_get_mask(wndp, BASE + 64) != 1 + (1UL << 41));
	/* bits up to head */
	BUG_ON(wnd_get_mask(wndp, wndp->head) != (5UL << 61) + (1UL << 31));
	/* bit before head */
	BUG_ON(wnd_get_mask(wndp, wndp->head - 1) != (1UL << 62) + (1UL << 32));
	/* bit after head */
	BUG_ON(wnd_get_mask(wndp, wndp->head + 1) != (5UL << 60)  + (1UL << 30));
	/* just before losing the bit in prev word */
	BUG_ON(wnd_get_mask(wndp, wndp->head + 31) != (5UL << 30)  + 1UL);
	/* just after losing the bit in prev word */
	BUG_ON(wnd_get_mask(wndp, wndp->head + 32) != (5UL << 29));
	/* when only the head is in the bitmask */
	BUG_ON(wnd_get_mask(wndp, wndp->head + 63) != 1UL);
	/* when the head just went out of the bitmask */
	BUG_ON(wnd_get_mask(wndp, wndp->head + 64) != 0);
	/* way after the head */
	BUG_ON(wnd_get_mask(wndp, wndp->head + 65) != 0);
	BUG_ON(wnd_get_mask(wndp, wndp->head + 1005) != 0);

	printf("done testing wnd, cleaning up\n");

	/* clean up */
	tslot = wndp->head;
clear_next_marked:
	gap = wnd_at_or_before(wndp, tslot);
	if (gap >= 0) {
		tslot -= gap;
		wnd_clear(wndp, tslot);
		goto clear_next_marked;
	}

	return 0;
}


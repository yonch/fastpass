/*
 * window.h
 *
 *  Created on: Dec 23, 2013
 *      Author: yonch
 */

#ifndef WINDOW_H_
#define WINDOW_H_

/**
 * The log of the size of outgoing packet window waiting for ACKs or timeout
 *    expiry. Setting this at < 6 is a bit wasteful since a full word has 64
 *    bits, and the algorithm works with word granularity
 */
#define FASTPASS_WND_LOG			8
#define FASTPASS_WND_LEN			((1 << FASTPASS_WND_LOG) - BITS_PER_LONG)
#define FASTPASS_WND_WORDS			(BITS_TO_LONGS(1 << FASTPASS_WND_LOG))

struct fp_window {
	unsigned long	marked[FASTPASS_WND_WORDS];
	unsigned long summary;
	u64				head;
	u32				head_word;
	u32				num_marked;

};

static inline u32 wnd_pos(u64 tslot)
{
	return tslot & ((1 << FASTPASS_WND_LOG) - 1);
}

static inline u32 summary_pos(struct fp_window *wnd, u32 pos) {
	return (wnd->head_word - BIT_WORD(pos)) % FASTPASS_WND_WORDS;
}

static bool wnd_empty(struct fp_window *wnd)
{
	return (wnd->num_marked == 0);
}

/**
 * Assumes seqno is in the correct range, returns whether the bin is unacked.
 */
static bool wnd_is_marked(struct fp_window *wnd, u64 seqno)
{
	return !!test_bit(wnd_pos(seqno), wnd->marked);
}

static void wnd_mark(struct fp_window *wnd, u64 seqno)
{
	BUG_ON(wnd_is_marked(wnd, seqno));

	u32 seqno_index = wnd_pos(seqno);
	__set_bit(seqno_index, wnd->marked);
	__set_bit(summary_pos(wnd, seqno_index), &wnd->summary);
	wnd->num_marked++;
}

static void wnd_clear(struct fp_window *wnd, u64 seqno)
{
	BUG_ON(!wnd_is_marked(wnd, seqno));

	u32 seqno_index = wnd_pos(seqno);
	__clear_bit(seqno_index, wnd->marked);
	if (unlikely(wnd->marked[BIT_WORD(seqno_index)] == 0))
		__clear_bit(summary_pos(wnd, seqno_index), &wnd->summary);
	wnd->num_marked--;
}

/**
 * Returns (@seqno - first_seqno), where first_seqno is the sequence no. of the
 *    first unacked packet *at* or *before* @seqno if such exists within the
 *    window, or -1 if it doesn't.
 */
static s32 wnd_at_or_before(struct fp_window *wnd, u64 seqno)
{
	u32 seqno_index;
	u32 seqno_word;
	u32 seqno_offset;
	u32 result_word_offset;
	unsigned long tmp;

	/* sanity check: seqno shouldn't be after window */
	BUG_ON(time_after64(seqno, wnd->head));

	/* if before window, return -1 */
	if (unlikely(time_before_eq64(seqno, wnd->head - FASTPASS_WND_LEN)))
		return -1;

	/* check seqno's word in marked */
	seqno_index = wnd_pos(seqno);
	seqno_word = BIT_WORD(seqno_index);
	seqno_offset = seqno_index % BITS_PER_LONG;
	tmp = wnd->marked[seqno_word] << (BITS_PER_LONG - 1 - seqno_offset);
	if (tmp != 0)
		return BITS_PER_LONG - 1 - __fls(tmp);

	/* didn't find in first word, look at summary of all words strictly after */
	tmp = wnd->summary >> summary_pos(wnd, seqno_index);
	tmp &= ~1UL;
	if (tmp == 0)
		return -1; /* summary indicates no marks there */

	result_word_offset = __ffs(tmp);
	tmp = wnd->marked[(seqno_word - result_word_offset) % FASTPASS_WND_WORDS];
	return BITS_PER_LONG * result_word_offset + seqno_offset - __fls(tmp);
}

/**
 * Returns the sequence no of the earliest unacked packet.
 * Assumes such a packet exists!
 */
static u64 wnd_earliest_marked(struct fp_window *wnd)
{
	u32 word_offset;
	u64 result;
	unsigned long tmp;
	word_offset = __fls(wnd->summary);
	tmp = wnd->marked[(wnd->head_word - word_offset) % FASTPASS_WND_WORDS];

	result = (wnd->head & ~(BITS_PER_LONG-1)) - (word_offset * BITS_PER_LONG)
			+ __ffs(tmp);

	BUG_ON(!wnd_is_marked(wnd, result));
	BUG_ON(wnd_at_or_before(wnd, result - 1) != -1);

	return result;
}

static void wnd_reset(struct fp_window *wnd, u64 head)
{
	memset(wnd->marked, 0, sizeof(wnd->marked));
	wnd->head = head;
	wnd->head_word = BIT_WORD(wnd_pos(head));
	wnd->summary = 0UL;
	wnd->num_marked = 0;
}

/**
 * Caller must make sure there are at most FASTPASS_WND_LEN - BITS_PER_LONG
 *    marked slots in the new window (or unmark them first)
 */
static void wnd_advance(struct fp_window *wnd, u64 amount)
{
	u64 word_shift = BIT_WORD(wnd->head + amount) - BIT_WORD(wnd->head);
	if (word_shift >= FASTPASS_WND_WORDS) {
		BUG_ON(wnd->num_marked != 0);
		memset(wnd->marked, 0, sizeof(wnd->marked));
		wnd->summary = 0UL;
	} else {
		BUG_ON(!wnd_empty(wnd) &&
				time_before_eq64(wnd_earliest_marked(wnd),
						wnd->head + amount - FASTPASS_WND_LEN));
		wnd->summary <<= word_shift;
	}
	wnd->head += amount;
	wnd->head_word = (wnd->head_word + word_shift) % FASTPASS_WND_WORDS;
}

#endif /* WINDOW_H_ */


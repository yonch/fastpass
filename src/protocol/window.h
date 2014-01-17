/*
 * window.h
 *
 *  Created on: Dec 23, 2013
 *      Author: yonch
 */

#ifndef FP_WINDOW_H_
#define FP_WINDOW_H_

#include "platform/generic.h"

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

static inline bool wnd_empty(struct fp_window *wnd)
{
	return (wnd->num_marked == 0);
}

static inline u32 wnd_num_marked(struct fp_window *wnd)
{
	return wnd->num_marked;
}

/* return the latest edge of the window (the head) */
static inline u64 wnd_head(struct fp_window *wnd)
{
	return wnd->head;
}

/* returns the early edge of the window */
static inline u64 wnd_edge(struct fp_window *wnd)
{
	return wnd->head - FASTPASS_WND_LEN + 1;
}

/* returns true if seqno is strictly before the window */
static inline bool wnd_seq_before(struct fp_window *wnd, u64 seqno)
{
	return time_before64(seqno, wnd_edge(wnd));
}

/* returns true if seqno is strictly after the window */
static inline bool wnd_seq_after(struct fp_window *wnd, u64 seqno)
{
	return time_after64(seqno, wnd_head(wnd));
}

/**
 * Assumes seqno is in the correct range, returns whether the bin is unacked.
 */
static inline bool wnd_is_marked(struct fp_window *wnd, u64 seqno)
{
	return !!test_bit(wnd_pos(seqno), wnd->marked);
}

static inline void wnd_mark(struct fp_window *wnd, u64 seqno)
{
	u32 seqno_index = wnd_pos(seqno);
	FASTPASS_BUG_ON(wnd_is_marked(wnd, seqno));

	__set_bit(seqno_index, wnd->marked);
	__set_bit(summary_pos(wnd, seqno_index), &wnd->summary);
	wnd->num_marked++;
}

/**
 * marks a consecutive stretch of sequence numbers [seqno, seqno+amount)
 */
static inline void wnd_mark_bulk(struct fp_window *wnd, u64 seqno, u32 amount)
{
	u32 start_index;
	u32 cur_word;
	u32 start_offset;
	u32 end_index;
	u32 end_word;
	u32 end_offset;
	unsigned long mask;

	FASTPASS_BUG_ON(time_before_eq64(seqno, wnd->head - FASTPASS_WND_LEN));
	FASTPASS_BUG_ON(time_after64(seqno + amount - 1, wnd->head));

	start_index = wnd_pos(seqno);
	start_offset = start_index % BITS_PER_LONG;
	end_index = wnd_pos(seqno + amount - 1);
	end_word = BIT_WORD(end_index);
	end_offset = end_index % BITS_PER_LONG;

	cur_word = BIT_WORD(start_index);
	mask = (~0UL << start_offset);

	if (cur_word == end_word)
		goto end_word;

	/* separate start word and end word */
	/* start word: */
	FASTPASS_BUG_ON((wnd->marked[cur_word] & mask) != 0);
	wnd->marked[cur_word] |= mask;
	mask = ~0UL;

	/* intermediate words */
next_intermediate:
	cur_word = (cur_word + 1) % FASTPASS_WND_WORDS;
	if (likely(cur_word != end_word)) {
		FASTPASS_BUG_ON(wnd->marked[cur_word] != 0);
		wnd->marked[cur_word] = ~0UL;
		goto next_intermediate;
	}

	/* end word */
	/* changes contained in one word */
end_word:
	mask &= (~0UL >> (BITS_PER_LONG - 1 - end_offset));
	FASTPASS_BUG_ON((wnd->marked[cur_word] & mask) != 0);
	wnd->marked[cur_word] |= mask;

	/* update summary */
	mask = ~0UL >> (BITS_PER_LONG - 1 - summary_pos(wnd, start_index));
	mask &= ~0UL << summary_pos(wnd, end_index);
	wnd->summary |= mask;

	/* update num_marked */
	wnd->num_marked += amount;

	return;
}

static inline void wnd_clear(struct fp_window *wnd, u64 seqno)
{
	u32 seqno_index = wnd_pos(seqno);

	FASTPASS_BUG_ON(!wnd_is_marked(wnd, seqno));

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
static inline s32 wnd_at_or_before(struct fp_window *wnd, u64 seqno)
{
	u32 seqno_index;
	u32 seqno_word;
	u32 seqno_offset;
	u32 result_word_offset;
	unsigned long tmp;

	/* sanity check: seqno shouldn't be after window */
	FASTPASS_BUG_ON(time_after64(seqno, wnd->head));

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
static inline u64 wnd_earliest_marked(struct fp_window *wnd)
{
	u32 word_offset;
	u64 result;
	unsigned long tmp;
	word_offset = __fls(wnd->summary);
	tmp = wnd->marked[(wnd->head_word - word_offset) % FASTPASS_WND_WORDS];

	result = (wnd->head & ~(BITS_PER_LONG-1)) - (word_offset * BITS_PER_LONG)
			+ __ffs(tmp);

	// FASTPASS_BUG_ON(!wnd_is_marked(wnd, result));
	// FASTPASS_BUG_ON(wnd_at_or_before(wnd, result - 1) != -1);

	return result;
}

static inline void wnd_reset(struct fp_window *wnd, u64 head)
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
static inline void wnd_advance(struct fp_window *wnd, u64 amount)
{
	u64 word_shift = BIT_WORD(wnd->head + amount) - BIT_WORD(wnd->head);
	if (word_shift >= FASTPASS_WND_WORDS) {
		FASTPASS_BUG_ON(wnd->num_marked != 0);
		memset(wnd->marked, 0, sizeof(wnd->marked));
		wnd->summary = 0UL;
	} else {
		FASTPASS_BUG_ON(!wnd_empty(wnd) &&
				time_before_eq64(wnd_earliest_marked(wnd),
						wnd->head + amount - FASTPASS_WND_LEN));
		wnd->summary <<= word_shift;
	}
	wnd->head += amount;
	wnd->head_word = (wnd->head_word + word_shift) % FASTPASS_WND_WORDS;
}

/**
 * See wnd_get_mask
 * This version assumes @pos is in the range
 * 		[head-FASTPASS_WND_LEN, head+BITS_PER_LONG]
 */
static inline u64 wnd_get_mask_unsafe(struct fp_window *wnd, u64 pos)
{
	u64 res = 0;
	u32 pos_index;
	u32 pos_word;
	u32 pos_offset;

	pos_index = wnd_pos(pos);
	pos_word = BIT_WORD(pos_index);
	pos_offset = pos_index % BITS_PER_LONG;

	if (unlikely(pos_offset == BITS_PER_LONG - 1))
		/* the mask is a full word in wnd->marked, return it */
		return wnd->marked[pos_word];

	/**
	 * we use the fact that between the head and tail of the array there
	 * are BITS_PER_LONG zeros, which are safe to take
	 */
	res = wnd->marked[pos_word] << (BITS_PER_LONG - 1 - pos_offset);
	pos_word = (pos_word - 1) % FASTPASS_WND_WORDS;
	res |= wnd->marked[pos_word] >> (pos_offset + 1);

	return res;
}

/**
 * Gets a 64-bit bit-mask of marked packets, of bits [@pos-63,@pos] where @pos
 *   is MSB (i.e., bit i is @pos-63+i).
 *
 * Locations in [@pos-63,@pos] that do not overlap the window will be 0.
 */
static inline u64 wnd_get_mask(struct fp_window *wnd, u64 pos)
{
#ifndef CONFIG_64BIT
#error "wnd_get_mask assumes unsigned long is 64-bit long"
#endif

	/* is pos before the window? */
	if (unlikely(time_before_eq64(pos, wnd->head - FASTPASS_WND_LEN)))
		return 0;

	/* is pos so large the bitmap wouldn't overlap the window? */
	if (unlikely(time_after_eq64(pos, wnd->head + BITS_PER_LONG)))
		return 0;

	return wnd_get_mask_unsafe(wnd, pos);
}

#endif /* FP_WINDOW_H_ */


/*
 * outwnd.h
 *
 *  Created on: Dec 23, 2013
 *      Author: yonch
 */

#ifndef OUTWND_H_
#define OUTWND_H_

#include "fastpass_proto.h"

static inline u32 outwnd_pos(u64 tslot)
{
	return ((u32)(-tslot)) & (FASTPASS_OUTWND_LEN-1);
}

/**
 * Assumes seqno is in the correct range, returns whether the bin is unacked.
 */
static bool outwnd_is_unacked(struct fastpass_sock *fp, u64 seqno)
{
	return !!test_bit(outwnd_pos(seqno), fp->bin_mask);
}

/**
 * Adds the packet descriptor as the next_seq
 */
static void outwnd_add(struct fastpass_sock *fp, struct fpproto_pktdesc *pd)
{
	u32 circular_index = outwnd_pos(fp->next_seqno);

	BUG_ON(outwnd_is_unacked(fp, fp->next_seqno - FASTPASS_OUTWND_LEN));
	BUG_ON(!pd);

	__set_bit(circular_index, fp->bin_mask);
	__set_bit(circular_index + FASTPASS_OUTWND_LEN, fp->bin_mask);
	fp->bins[circular_index] = pd;
	fp->tx_num_unacked++;

	fp->next_seqno++;
}

/**
 * Removes the packet description with the given seqno, marking it as acked.
 *    Returns the removed packet.
 *
 * Assumes the seqno is in the correct range.
 */
static struct fpproto_pktdesc * outwnd_pop(struct fastpass_sock *fp, u64 seqno)
{
	u32 circular_index = outwnd_pos(seqno);
	struct fpproto_pktdesc *res = fp->bins[circular_index];

	BUG_ON(!outwnd_is_unacked(fp, seqno));

	__clear_bit(circular_index, fp->bin_mask);
	__clear_bit(circular_index + FASTPASS_OUTWND_LEN, fp->bin_mask);
	fp->bins[circular_index] = NULL;
	fp->tx_num_unacked--;
	return res;
}

/**
 * Returns (@seqno - first_seqno), where first_seqno is the sequence no. of the
 *    first unacked packet *at* or *before* @seqno if such exists within the
 *    window, or -1 if it doesn't.
 */
static s32 outwnd_at_or_before(struct fastpass_sock *fp, u64 seqno)
{
	u32 head_index;
	u32 seqno_index;
	u32 found_offset;

	BUG_ON(time_after_eq64(seqno, fp->next_seqno));

	if (unlikely(time_before64(seqno, fp->next_seqno - FASTPASS_OUTWND_LEN)))
		return -1;

	head_index = outwnd_pos(fp->next_seqno - 1);

	/*
	 * there are two indices that could correspond to seqno, get the first
	 *   one not smaller than head_index.
	 */
	seqno_index = head_index + outwnd_pos(seqno - (fp->next_seqno - 1));

	found_offset = find_next_bit(fp->bin_mask,
			head_index + FASTPASS_OUTWND_LEN,
			seqno_index);

	/* TODO: remove later, for performance */
	BUG_ON((found_offset != head_index + FASTPASS_OUTWND_LEN)
			&& !outwnd_is_unacked(fp, seqno - (found_offset - seqno_index)));

	return (found_offset == head_index + FASTPASS_OUTWND_LEN) ?
			-1 : (found_offset - seqno_index);
}

/**
 * Returns the sequence no of the earliest unacked packet, given that that
 *    earliest seqno is not before @hint.
 * Assumes such a packet exists, and that hint is within the outwnd.
 */
static u64 outwnd_earliest_unacked_hint(struct fastpass_sock *fp, u64 hint)
{
	u32 hint_pos = outwnd_pos(hint);
	u32 found_offset;
	u64 earliest;

	found_offset = find_last_bit(fp->bin_mask, hint_pos + FASTPASS_OUTWND_LEN + 1);

	/**
	 * found_offset runs between hint_pos+1 to hint_pos+FASTPASS_OUTWND_LEN
	 * (hint_pos + FASTPASS_OUTWND_LEN - found_offset) is #timeslots after
	 * 		@hint of sought pkt
	 */
	earliest = hint + (hint_pos + FASTPASS_OUTWND_LEN - found_offset);

	/* TODO: remove the check when debugged, for performance */
	BUG_ON((earliest != fp->next_seqno - FASTPASS_OUTWND_LEN)
			&& (outwnd_at_or_before(fp, earliest - 1) != -1));

	return earliest;
}

/**
 * Returns the sequence no of the earliest unacked packet.
 * Assumes such a packet exists!
 */
static u64 outwnd_earliest_unacked(struct fastpass_sock *fp)
{
	return outwnd_earliest_unacked_hint(fp,
			fp->next_seqno - FASTPASS_OUTWND_LEN);
}

#ifdef FASTPASS_PERFORM_RUNTIME_TESTS
static void outwnd_test(struct fastpass_sock *fp) {
	u64 tslot;
	s32 gap;
	int i;
	const int BASE = 10007;

	fastpass_pr_debug("testing outwnd\n");
	fp->next_seqno = BASE;
	for(tslot = BASE - FASTPASS_OUTWND_LEN; tslot < BASE; tslot++) {
		BUG_ON(outwnd_at_or_before(fp, tslot) != -1);
		BUG_ON(outwnd_is_unacked(fp, tslot));
	}

	for(i = 0; i < FASTPASS_OUTWND_LEN; i++)
		outwnd_add(fp, (struct fpproto_pktdesc *)(0xFF00L + i));

	for(tslot = BASE; tslot < BASE + FASTPASS_OUTWND_LEN; tslot++) {
		BUG_ON(!outwnd_is_unacked(fp, tslot));
		BUG_ON(outwnd_at_or_before(fp, tslot) != 0);
	}

	BUG_ON(outwnd_earliest_unacked(fp) != BASE);
	BUG_ON(outwnd_pop(fp, BASE) != (void *)0xFF00L);
	BUG_ON(outwnd_earliest_unacked(fp) != BASE+1);
	BUG_ON(outwnd_at_or_before(fp, BASE) != -1);
	BUG_ON(outwnd_at_or_before(fp, BASE+1) != 0);
	BUG_ON(outwnd_pop(fp, BASE+2) != (void *)0xFF02L);
	BUG_ON(outwnd_earliest_unacked(fp) != BASE+1);
	BUG_ON(outwnd_at_or_before(fp, BASE+2) != 1);

	for(tslot = BASE+3; tslot < BASE + 152; tslot++) {
		BUG_ON(outwnd_pop(fp, tslot) != (void *)0xFF00L + tslot - BASE);
		BUG_ON(outwnd_is_unacked(fp, tslot));
		BUG_ON(outwnd_at_or_before(fp, tslot) != tslot - BASE - 1);
		BUG_ON(outwnd_at_or_before(fp, tslot+1) != 0);
		BUG_ON(outwnd_earliest_unacked(fp) != BASE+1);
	}
	for(tslot = BASE+152; tslot < BASE + FASTPASS_OUTWND_LEN; tslot++) {
		BUG_ON(!outwnd_is_unacked(fp, tslot));
		BUG_ON(outwnd_at_or_before(fp, tslot) != 0);
	}

	BUG_ON(outwnd_pop(fp, BASE+1) != (void *)0xFF01L);
	BUG_ON(outwnd_earliest_unacked(fp) != BASE+152);

	fastpass_pr_debug("done testing outwnd, cleaning up\n");

	/* clean up */
	tslot = fp->next_seqno - 1;
clear_next_unacked:
	gap = outwnd_at_or_before(fp, tslot);
	if (gap >= 0) {
		tslot -= gap;
		BUG_ON(outwnd_pop(fp, tslot) != (void *)0xFF00L + tslot - BASE);
		goto clear_next_unacked;
	}

	/* make sure pointer array is clean */
	for (i = 0; i < FASTPASS_OUTWND_LEN; i++)
		BUG_ON(fp->bins[i] != NULL);
}
#endif

static void outwnd_reset(struct fastpass_sock* fp)
{
	u64 tslot;
	s32 gap;

	tslot = fp->next_seqno - 1; /* start at the last transmitted message */
	clear_next_unacked: gap = outwnd_at_or_before(fp, tslot);
	if (gap >= 0) {
		tslot -= gap;
		fpproto_pktdesc_free(outwnd_pop(fp, tslot));
		goto clear_next_unacked;
	}
}

static bool outwnd_empty(struct fastpass_sock* fp)
{
	return (fp->tx_num_unacked == 0);
}

/**
 * Returns the timestamp of the descriptor with @seqno
 * Assumes @seqno is within the window and unacked
 */
static u64 outwnd_timestamp(struct fastpass_sock* fp, u64 seqno)
{
	return fp->bins[outwnd_pos(seqno)]->sent_timestamp;
}

#endif /* OUTWND_H_ */

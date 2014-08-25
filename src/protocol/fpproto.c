/*
 * Platform independent FastPass protocol implementation
 */

#include "fpproto.h"

#include "platform.h"
#include "outwnd.h"

#undef FASTPASS_PERFORM_RUNTIME_TESTS

/**
 * FastPass packet header
 */
struct fastpass_hdr {
	__be16	seq;
	__be16	ack_seq;
	__be16	ack_vec;
	__sum16	checksum;
};

struct fastpass_areq {
	__be16	dst;
	__be16	count;
};

/**
 * Computes the base sequence number from a reset timestamp
 */
static u64 base_seqno_from_timestamp(u64 reset_time)
{
	u32 time_hash = jhash_1word((u32) reset_time, reset_time >> 32);
	return reset_time + time_hash + ((u64) time_hash << 32);
}

static bool tstamp_in_window(u64 tstamp, u64 win_middle, u64 win_size) {
	return (tstamp >= win_middle - (win_size / 2))
			&& (tstamp < win_middle + ((win_size + 1) / 2));
}

/**
 * Receives a packet destined for the protocol. (part of inet socket API)
 */
static __sum16 fastpass_checksum(u8 *pkt, u32 len, __be32 saddr, __be32 daddr,
		u64 seqno, u64 ack_seq)
{
	u32 seq_hash = jhash_3words((u32)seqno, seqno >> 32, (u32)ack_seq,
			ack_seq >> 32);
	__wsum csum = csum_partial(pkt, len, seq_hash);
#if 0
	fp_debug("ptr 0x%p seq_hash 0x%X csum 0x%X len %u csum_partial 0 0x%X 1 0x%X 2 0x%X 3 0x%X 4 0x%X 5 0x%X 6 0x%X\n",
			pkt, seq_hash, csum, len,
			csum_partial(pkt, 0, seq_hash),
			csum_partial(pkt, 1, seq_hash),
			csum_partial(pkt, 2, seq_hash),
			csum_partial(pkt, 3, seq_hash),
			csum_partial(pkt, 4, seq_hash),
			csum_partial(pkt, 5, seq_hash),
			csum_partial(pkt, 6, seq_hash));
#endif
	return csum_tcpudp_magic(0, 0, len, IPPROTO_FASTPASS, csum);
	/* TODO: reinstate checksumming saddr and daddr */
	return csum_tcpudp_magic(saddr, daddr, len, IPPROTO_FASTPASS, csum);
}

static void recompute_and_reset_retrans_timer(struct fpproto_conn *conn)
{
	u64 timeout;
	u64 seqno;
	bool has_unacked;

	/* find the earliest untimed-out and unacked seqno, and its the timeout */
	has_unacked = wnd_at_or_after(&conn->outwnd, conn->next_timeout_seqno, &seqno);
	if (!has_unacked) {
		fp_debug("all packets acked, no need to set timer\n");
		conn->next_timeout_seqno = wnd_head(&conn->outwnd) + 1;
		conn->ops->cancel_timer(conn->ops_param);
		return;
	}

	conn->next_timeout_seqno = seqno;
	timeout = outwnd_peek(conn, seqno)->sent_timestamp
			+ conn->send_timeout;

	conn->ops->cancel_timer(conn->ops_param);
	conn->ops->set_timer(conn->ops_param, timeout);
	conn->stat.reprogrammed_timer++;
	fp_debug("setting timer to %llu for seq#=0x%llX\n", timeout, seqno);
}

static void do_ack_seqno(struct fpproto_conn *conn, u64 seqno)
{
	struct fpproto_pktdesc *pd;

	FASTPASS_BUG_ON(wnd_seq_after(&conn->outwnd, seqno));
	FASTPASS_BUG_ON(wnd_seq_before(&conn->outwnd,seqno));

	fp_debug("ACK seqno 0x%08llX\n", seqno);
	conn->stat.acked_packets++;
	FASTPASS_BUG_ON(!wnd_is_marked(&conn->outwnd, seqno));
	pd = outwnd_pop(conn, seqno);

	if (conn->ops->handle_ack)
		conn->ops->handle_ack(conn->ops_param, pd);		/* will free pd */

	fpproto_pktdesc_free(pd);
}

static void do_neg_ack_seqno(struct fpproto_conn *conn, u64 seq)
{
	struct fpproto_pktdesc *pd = outwnd_peek(conn, seq);
	fp_debug("Unacked tx seq 0x%llX\n", seq);
	if (conn->ops->handle_neg_ack)
		conn->ops->handle_neg_ack(conn->ops_param, pd);		/* will NOT free pd */
}

static void free_unacked(struct fpproto_conn *conn)
{
	u64 tslot;
	s32 gap;

	struct fp_window *ow = &conn->outwnd;

	tslot = wnd_head(ow); /* start at the head of the outwnd */
clear_next_unacked:
	gap = wnd_at_or_before(ow, tslot);
	if (gap >= 0) {
		tslot -= gap;
		fpproto_pktdesc_free(outwnd_pop(conn, tslot));
		goto clear_next_unacked;
	}
}

static void do_proto_reset(struct fpproto_conn *conn, u64 reset_time,
		bool in_sync)
{
	u64 base_seqno = base_seqno_from_timestamp(reset_time);

	free_unacked(conn);

	/* set new sequence numbers */
	conn->last_reset_time = reset_time;
	wnd_reset(&conn->outwnd, base_seqno + FASTPASS_EGRESS_SEQNO_OFFSET - 1);
	conn->in_max_seqno = base_seqno + FASTPASS_INGRESS_SEQNO_OFFSET - 1;
	conn->inwnd = ~0UL;
	conn->consecutive_bad_pkts = 0;
	conn->next_timeout_seqno = wnd_head(&conn->outwnd) + 1;

	/* are we in sync? */
	conn->in_sync = in_sync;

	/* statistics */
	conn->stat.proto_resets++;
}

void fpproto_force_reset(struct fpproto_conn *conn)
{
	u64 now = fp_get_time_ns();
	fp_debug("executing forced reset at %llu\n", now);
	conn->stat.forced_reset++;
	do_proto_reset(conn, now, false);
}


void fpproto_handle_timeout(struct fpproto_conn *conn, u64 now)
{
	u64 seqno;
	u64 timeout;

	conn->stat.timeout_handler_runs++;

	/* notify qdisc of expired timeouts */
	seqno = conn->next_timeout_seqno;
	while (wnd_at_or_after(&conn->outwnd, seqno, &seqno)) {
		timeout = outwnd_peek(conn, seqno)->sent_timestamp
				+ conn->send_timeout;

		/* if timeout hasn't expired, we're done */
		if (unlikely(time_after64(timeout, now)))
			goto set_next_timer;

		conn->stat.timeout_pkts++;
		do_neg_ack_seqno(conn, seqno);

		seqno++;
	}
	conn->next_timeout_seqno = wnd_head(&conn->outwnd) + 1;
	fp_debug("outwnd empty, not setting timer\n");
	return;

set_next_timer:
	/* seqno is the earliest unacked seqno, and timeout is its timeout */
	conn->next_timeout_seqno = seqno;
	conn->ops->set_timer(conn->ops_param, timeout);
	fp_debug("setting timer to %llu for seq#=0x%llX\n", timeout, seqno);
}

/*
 * returns 0 if okay to continue processing, 1 to drop
 *
 *                   					LAST RESET
 *          					is recent    =   not recent
 *          			   ====================================
 *          	is recent  =      accept     =     accept     =
 *  PAYLOAD                =     maximum     =     payload    =
 *  TIMESTAMP              =-----------------=----------------=
 *  			not recent =       do        =   produce new  =
 *  			           =     nothing     =    timestamp   =
 *  			           ====================================
 */
static int reset_payload_handler(struct fpproto_conn *conn, u64 full_tstamp)
{
	u64 now = fp_get_time_ns();
	bool last_is_recent;
	bool payload_is_recent;

	conn->stat.reset_payloads++;
	fp_debug("got RESET, last is 0x%llX, full 0x%llX, now 0x%llX\n",
			conn->last_reset_time, full_tstamp, now);

	if (full_tstamp == conn->last_reset_time) {
		if (IS_ENDPOINT && !conn->in_sync) {
			fp_debug("Now in sync\n");
		} else {
			conn->stat.redundant_reset++;
			fp_debug("received redundant reset\n");
		}
		conn->in_sync = IS_ENDPOINT;
		return 0;
	}

	/* did we accept a reset recently? */
	last_is_recent = tstamp_in_window(conn->last_reset_time, now, conn->rst_win_ns);
	/* is the timestamp requested recent? */
	payload_is_recent = tstamp_in_window(full_tstamp, now, conn->rst_win_ns);

	if (last_is_recent) {
		if (payload_is_recent) {
			/* accept maximum */
			fp_debug("both last_reset and payload timestamps are recent. will accept max.\n");
			if (unlikely(time_before64(full_tstamp, conn->last_reset_time))) {
				fp_debug("last_reset larger by %lluns. will not accept.\n",
								conn->last_reset_time - full_tstamp);
				conn->stat.reset_both_recent_last_reset_wins++;
				return 1;
			} else {
				/* the payload is more recent. we will accept it */
				fp_debug("payload larger by %lluns. will accept.\n",
								full_tstamp - conn->last_reset_time);
				conn->stat.reset_both_recent_payload_wins++;
				goto accept;
			}
		} else {
			fp_debug("last_reset recent, payload is old. will stick with last_reset.\n");
			conn->stat.reset_last_recent_payload_old++;
			return 1;
		}
	} else {
		if (payload_is_recent) {
			fp_debug("payload is recent, last_reset is old. will accept.\n");
			conn->stat.reset_last_old_payload_recent++;
			goto accept;
		} else {
			fp_debug("neither payload nor last_reset are recent. will choose a new timestamp.\n");
			conn->stat.reset_both_old++;
			do_proto_reset(conn, now, IS_ENDPOINT);
			if (conn->ops->handle_reset)
				conn->ops->handle_reset(conn->ops_param);
			return 1;
		}
	}

accept:
	do_proto_reset(conn, full_tstamp, IS_ENDPOINT);
	if (conn->ops->handle_reset)
		conn->ops->handle_reset(conn->ops_param);
	return 0;
}

static void ack_payload_handler(struct fpproto_conn *conn, u64 ack_seq, u64 ack_vec)
{
	u64 cur_seqno;
	u32 offset;
	int n_acked = 0;
	u64 unacked_mask;
	u64 todo_mask;

	conn->stat.ack_payloads++;

	/* is the seqno within the window? */
	if (wnd_seq_before(&conn->outwnd, ack_seq))
		goto ack_too_early;

	unacked_mask = wnd_get_mask(&conn->outwnd, ack_seq);

	fp_debug("handling ack_seq 0x%llX ack_vec 0x%016llX unacked 0x%016llX\n",
				ack_seq, ack_vec, unacked_mask);

	todo_mask = ack_vec & unacked_mask;

	while(todo_mask) {
		offset = __ffs(todo_mask);
		cur_seqno = ack_seq - 63 + offset;

		FASTPASS_BUG_ON(wnd_seq_before(&conn->outwnd, cur_seqno));
		FASTPASS_BUG_ON(!wnd_is_marked(&conn->outwnd, cur_seqno));

		do_ack_seqno(conn, cur_seqno);
		n_acked++;

		todo_mask &= ~(1UL << offset);
	}

	if (n_acked > 0) {
		recompute_and_reset_retrans_timer(conn);
		conn->stat.informative_ack_payloads++;
	}
	return;

ack_too_early:
	fp_debug("too_early_ack: earliest %llu, got %llu\n",
			wnd_edge(&conn->outwnd), ack_seq);
	conn->stat.too_early_ack++;
}

static void got_good_packet(struct fpproto_conn *conn)
{
	conn->consecutive_bad_pkts = 0;
}

static void got_bad_packet(struct fpproto_conn *conn)
{
	u64 now = fp_get_time_ns();

	/* we better assume we're not in sync and send RESET payloads */
	conn->in_sync = 0;

	conn->consecutive_bad_pkts++;
	fp_debug("#%u consecutive bad packets\n", conn->consecutive_bad_pkts);

	if (conn->consecutive_bad_pkts < FASTPASS_BAD_PKT_RESET_THRESHOLD)
		goto out;

	/* got too many bad packets */

	/* reset bad packet count to 0 for continued operation */
	conn->consecutive_bad_pkts = 0;

	/* was there a recent reset? */
	if (time_in_range64(
			conn->last_reset_time,
			now - FASTPASS_RESET_WINDOW_NS,
			now + FASTPASS_RESET_WINDOW_NS)) {
		/* will not trigger a new one */
		conn->stat.no_reset_because_recent++;
		fp_debug("had a recent reset (last %llu, now %llu). not issuing a new one.\n",
				conn->last_reset_time, now);
	} else {
		/* Will send a RSTREQ */
		conn->stat.reset_from_bad_pkts++;
		do_proto_reset(conn, now, false);
		if (conn->ops->handle_reset)
			conn->ops->handle_reset(conn->ops_param);
	}

out:
	/* Whatever happens, trigger an outgoing packet to make progress */
	if (conn->ops->trigger_request)
		conn->ops->trigger_request(conn->ops_param);
}

/**
 * Updates the incoming packet window
 * @return 0 if update is successful
 * 		   1 if caller should drop the packet with seqno
 */
static int update_inwnd(struct fpproto_conn *conn, u64 seqno)
{
	u64 head = conn->in_max_seqno;

	/* seqno >= head + 64 ? */
	if (unlikely(time_after_eq64(seqno, head + 64))) {
		conn->stat.inwnd_jumped++;
		conn->in_max_seqno = seqno;
		conn->inwnd = 1UL << 63;
		return 0; /* accept */
	}

	/* seqno in [head+1, head+63] ? */
	if (likely(time_after64(seqno, head))) {
		/* advance no more than 63 */
		conn->inwnd >>= (seqno - head);
		conn->inwnd |= 1UL << 63;
		conn->in_max_seqno = seqno;
		return 0; /* accept */
	}

	/* seqno before the bits kept in inwnd ? */
	if (unlikely(time_before_eq64(seqno, head - 64))) {
		/* we don't know whether we had already previously processed packet */
		conn->stat.seqno_before_inwnd++;
		return 1; /* drop */
	}

	/* seqno in [head-63, head] */
	if (conn->inwnd & (1UL << (63 - (head - seqno)))) {
		/* already marked as received */
		conn->stat.rx_dup_pkt++;
		return 1; /* drop */
	}

	conn->inwnd |= (1UL << (63 - (head - seqno)));
	conn->stat.rx_out_of_order++;
	return 0; /* accept */
}

/**
 * At-most-once: should we accept the packet?
 * @return 0 if can accept
 * 		   1 if caller should drop the packet with seqno
 */
static int at_most_once_may_accept(struct fpproto_conn *conn, u64 seqno)
{
	u64 head = conn->in_max_seqno;

	/* seqno after head */
	if (likely(time_after64(seqno, head)))
		return 0; /* accept */

	/* seqno before the bits kept in inwnd ? */
	if (unlikely(time_before_eq64(seqno, head - 64)))
		return 1; /* drop */

	/* seqno in [head-63, head] */
	if (conn->inwnd & (1UL << (63 - (head - seqno))))
		/* already marked as received */
		return 1; /* drop */

	return 0; /* accept */
}

/**
 * Processes ALLOC payload.
 * On success, returns the payload length in bytes. On failure returns -1.
 */
static int process_alloc(struct fpproto_conn *conn, u8 *data, u8 *data_end)
{
	u16 payload_type;
	int alloc_n_dst, alloc_n_tslots;
	u16 alloc_dst[16];
	u32 alloc_base_tslot;
	u8 *curp = data;
	int i;

	if (curp + 2 > data_end)
		goto incomplete_alloc_payload_one_byte;

	payload_type = ntohs(*(u16 *)curp);
	alloc_n_dst = (payload_type >> 8) & 0xF;
	alloc_n_tslots = 2 * (payload_type & 0x3F);
	curp += 2;

	if (curp + 2 + 2 * alloc_n_dst + alloc_n_tslots > data_end)
		goto incomplete_alloc_payload;

	/* get base timeslot */
	alloc_base_tslot = ntohs(*(u16 *)curp);
	alloc_base_tslot <<= 4;
	curp += 2;

	/* convert destinations from network byte-order */
	for (i = 0; i < alloc_n_dst; i++, curp += 2)
		alloc_dst[i] = ntohs(*(u16 *)curp);

	/* process the payload */
	if (conn->ops->handle_alloc)
		conn->ops->handle_alloc(conn->ops_param, alloc_base_tslot, alloc_dst, alloc_n_dst,
			curp, alloc_n_tslots);

	return 4 + 2 * alloc_n_dst + alloc_n_tslots;

incomplete_alloc_payload_one_byte:
	conn->stat.rx_incomplete_alloc++;
	fp_debug("ALLOC payload incomplete, only got one byte\n");
	return -1;

incomplete_alloc_payload:
	conn->stat.rx_incomplete_alloc++;
	fp_debug("ALLOC payload incomplete: expected %d bytes, got %d\n",
			2 + 2 * alloc_n_dst + alloc_n_tslots, (int)(data_end - curp));
	return -1;
}

/**
 * Processes A-REQ payload.
 * On success, returns the payload length in bytes. On failure returns -1.
 */
static int process_areq(struct fpproto_conn *conn, u8 *data, u8 *data_end)
{
	u8 *curp = data;
	u32 n_dst;
	u16 payload_type;

	if (curp + 2 > data_end)
		goto incomplete;

	payload_type = ntohs(*(u16 *)curp);
	n_dst = payload_type & 0x3F;
	curp += 2;
	if (curp + 4 * n_dst > data_end)
		goto incomplete;

	if (conn->ops->handle_areq)
		conn->ops->handle_areq(conn->ops_param, (u16 *)curp, n_dst);

	curp += 4 * n_dst;
	return curp - data;

incomplete:
	fp_debug("incomplete A-REQ\n");
	conn->stat.rx_incomplete_areq++;
	return -1;
}

bool fpproto_handle_rx_packet(struct fpproto_conn *conn, u8 *pkt, u32 len,
		__be32 saddr, __be32 daddr, u64 *returned_in_seq)
{
	struct fastpass_hdr *hdr;
	u64 in_seq, ack_seq;
	u16 payload_type;
	u64 rst_tstamp = 0;
	__sum16 checksum;
	u8 *curp;
	u8 *data_end;
	u64 ack_vec;
	u16 ack_vec16;
	__sum16 expected_checksum;

	conn->stat.rx_pkts++;

	if (unlikely(len < 8))
		goto packet_too_short;


	hdr = (struct fastpass_hdr *)pkt;
	curp = &pkt[8];
	data_end = &pkt[len];
	if (unlikely(len == 8))
		payload_type = 0;
	else
		payload_type = *curp >> 4;

	/* get full 64-bit sequence number for the pseudo-header */
	if (unlikely(payload_type == FASTPASS_PTYPE_RESET)) {
		/* DERIVE SEQNO FROM RESET TIMESTAMP */
		u64 now = fp_get_time_ns();
		u64 partial_tstamp;
		u64 base_seqno;

		if (unlikely(curp + 8 > data_end))
			goto incomplete_reset_payload;

		/* get lower 56 bits of timestamp */
		partial_tstamp = ((u64)(ntohl(*(u32 *)curp) & ((1 << 24) - 1)) << 32) |
				ntohl(*(u32 *)(curp + 4));

		/* reconstruct all 64 bits of timestamp */
		rst_tstamp = now - (1ULL << 55);
		rst_tstamp += (partial_tstamp - rst_tstamp) & ((1ULL << 56) - 1);

		base_seqno = base_seqno_from_timestamp(rst_tstamp);

		in_seq = base_seqno + FASTPASS_INGRESS_SEQNO_OFFSET;
		ack_seq = base_seqno + FASTPASS_EGRESS_SEQNO_OFFSET - 1;
	} else {
		/* get seqno from stored state */
		in_seq = conn->in_max_seqno - (1 << 14);
		ack_seq = wnd_head(&conn->outwnd) - (1 << 16) + 1;
	}
	in_seq += (ntohs(hdr->seq) - in_seq) & 0xFFFF;
	ack_seq += (ntohs(hdr->ack_seq) - ack_seq) & 0xFFFF;
	fp_debug("packet with in_seq 0x%04X (full 0x%llX, prev_max 0x%llX)"
			" ack_seq 0x%04X (full 0x%llX, max_sent 0x%llX) checksum 0x%04X\n",
			ntohs(hdr->seq), in_seq, conn->in_max_seqno,
			ntohs(hdr->ack_seq), ack_seq, wnd_head(&conn->outwnd),
			hdr->checksum);

	/* verify checksum */
	expected_checksum = hdr->checksum;
	hdr->checksum = 0;
	checksum = fastpass_checksum(pkt, len, saddr, daddr, in_seq, ack_seq);
	if (unlikely(checksum != expected_checksum)) {
		got_bad_packet(conn);
		goto bad_checksum; /* will drop packet */
	} else {
		got_good_packet(conn);
	}

	if (unlikely(payload_type == FASTPASS_PTYPE_RESET)) {
		/* a reset in any direction will cause RESETs to be sent until the
		 * end-node decides it is in sync and stops sending RESETs */
		conn->in_sync = 0;

		/* a good-checksum RESET packet always triggers a controller response */
		if (!IS_ENDPOINT && conn->ops->trigger_request)
			conn->ops->trigger_request(conn->ops_param);

		if (reset_payload_handler(conn, rst_tstamp) != 0)
			/* reset was not applied, drop packet */
			return false;
		curp += 8;
	} else {
		conn->in_sync = 1;
	}

	/* check if may accept the packet */
	if (at_most_once_may_accept(conn, in_seq) != 0)
		return false; /* drop packet to keep at-most-once semantics */

	/* handle acks */
	ack_vec16 = ntohs(hdr->ack_vec);
	ack_vec = ((1UL << 48) - (ack_vec16 >> 15)) & ~(1UL << 48);
	ack_vec |= ((u64)(ack_vec16 & 0x7FFF) << 48) | (1UL << 63); /* ack the ack_seqno */
	ack_payload_handler(conn, ack_seq, ack_vec);

	if (unlikely(curp == data_end)) {
		/* no more payloads in this packet, we're done with it */
		fp_debug("no more payloads\n");
		fpproto_successful_rx(conn, in_seq);
		return false;
	}

	*returned_in_seq = in_seq;

	payload_type = *curp >> 4;
	if (payload_type != FASTPASS_PTYPE_ACK)
		return true;

	/* handle extended ACK */
	if (curp + 6 > data_end)
		goto incomplete_ack_payload;

	ack_vec = ntohl(*(u32 *)curp) & ((1UL << 28) - 1);
	ack_vec <<= 20;
	ack_vec |= (u64)ntohs(*(u16 *)(curp + 4)) << 4;
	ack_payload_handler(conn, ack_seq, ack_vec);

	return true;

incomplete_reset_payload:
	conn->stat.rx_incomplete_reset++;
	fp_debug("RESET payload incomplete, expected 8 bytes, got %d\n",
			(int)(data_end - curp));
	return false;

incomplete_ack_payload:
	conn->stat.rx_incomplete_ack++;
	fp_debug("ACK payload incomplete: expected 6 bytes, got %d\n",
			(int)(data_end - curp));
	return false;

bad_checksum:
	conn->stat.rx_checksum_error++;
	fp_debug("checksum error. expected 0x%04X, got 0x%04X\n",
			expected_checksum, checksum);
	return false;

packet_too_short:
	conn->stat.rx_too_short++;
	fp_debug("packet less than minimal size (len=%d)\n", len);
	return false;
}


bool fpproto_perform_rx_callbacks(struct fpproto_conn *conn, u8 *pkt, u32 len)
{
	u16 payload_type;
	int payload_length;
	u8 *curp;
	u8 *data_end;


	curp = &pkt[8];
	data_end = &pkt[len];
	FASTPASS_BUG_ON(curp == data_end);

handle_payload:
	/* at this point we know there is at least one byte remaining */
	payload_type = *curp >> 4;

	switch (payload_type) {
	case FASTPASS_PTYPE_ACK:
		curp += 6;
		break;

	case FASTPASS_PTYPE_RESET:
		curp += 8;
		break;

	case FASTPASS_PTYPE_ALLOC:
		payload_length = process_alloc(conn, curp, data_end);

		fp_debug("process_alloc returned %d\n", payload_length);
		if (unlikely(payload_length == -1))
			return false;

		curp += payload_length;
		break;

	case FASTPASS_PTYPE_AREQ:
		payload_length = process_areq(conn, curp, data_end);

		fp_debug("process_areq returned %d\n", payload_length);
		if (unlikely(payload_length == -1))
			return false;

		curp += payload_length;
		break;

	case FASTPASS_PTYPE_PADDING:
		/* okay, we're done, it's padding from now on */
		fp_debug("got padding. done with this packet.\n");
		curp = data_end;
		break;

	default:
		goto unknown_payload_type;
	}

	/* more payloads in packet? */
	if (curp < data_end)
		goto handle_payload;

	return true;

unknown_payload_type:
	conn->stat.rx_unknown_payload++;
	fp_debug("got unknown payload type %d at offset %lld\n",
			payload_type, (s64)(curp - pkt));
	return false;

}

void fpproto_successful_rx(struct fpproto_conn *conn, u64 in_seq)
{
	/* successful parsing, can ack */
	update_inwnd(conn, in_seq);
}

void fpproto_handle_rx_complete(struct fpproto_conn *conn, u8 *pkt, u32 len,
		__be32 saddr, __be32 daddr)
{
	bool ret;
	u64 in_seq;

	ret = fpproto_handle_rx_packet(conn, pkt, len, saddr, daddr, &in_seq);
	if (!ret)
		return;

	ret = fpproto_perform_rx_callbacks(conn, pkt, len);
	if (!ret)
		return;

	fpproto_successful_rx(conn, in_seq);
}

/**
 * Handles a case of a packet that seems to have not been delivered to
 *    controller successfully, either because of falling off the outwnd end,
 *    or a timeout.
 */
/**
 * Make sure fpproto is ready to accept a new packet.
 *
 * This might NACK a packet.
 */
void fpproto_prepare_to_send(struct fpproto_conn *conn)
{
	u64 window_edge = wnd_edge(&conn->outwnd);

	/* make sure outwnd is not holding a packet descriptor where @pd will be */
	if (wnd_is_marked(&conn->outwnd, window_edge)) {
		/* this is a packet that hasn't been acked before falling off */
		conn->stat.never_acked_pkts++;

		/* if the packet has never been NACKed either, nack it now (and log) */
		if (time_after_eq64(window_edge, conn->next_timeout_seqno)) {
			conn->stat.fall_off_outwnd++;
			do_neg_ack_seqno(conn, window_edge);
			conn->next_timeout_seqno = window_edge + 1;
			recompute_and_reset_retrans_timer(conn);
		}
		
		/* remove from the window and free */
		fpproto_pktdesc_free(outwnd_pop(conn, window_edge));
	}
}

/**
 * Protocol will commit to guaranteeing the given packet is delivered.
 *
 * A sequence number is allocated to the packet, and timeouts will reflect the
 *    packet.
 *
 * @pd: the packet
 * @now: send timestamp from which timeouts are computed
 */
void fpproto_commit_packet(struct fpproto_conn *conn, struct fpproto_pktdesc *pd,
		u64 timestamp)
{
	pd->sent_timestamp = timestamp;
	pd->seqno = wnd_head(&conn->outwnd) + 1;
	pd->send_reset = !conn->in_sync;
	pd->reset_timestamp = conn->last_reset_time;
	pd->ack_seq = conn->in_max_seqno;
	pd->ack_vec = ((conn->inwnd >> 48) & 0x7FFF);
	pd->ack_vec |= ((conn->inwnd & (~0UL >> 16)) == (~0UL >> 16)) << 15;

	/* add packet to outwnd, will advance fp->next_seqno */
	outwnd_add(conn, pd);
	conn->stat.committed_pkts++;

	recompute_and_reset_retrans_timer(conn);
}

int fpproto_encode_packet(struct fpproto_pktdesc *pd, u8 *pkt, u32 max_len,
		__be32 saddr, __be32 daddr, u32 min_size)
{
	int i;
	struct fastpass_areq *areq;

	u8 *curp = pkt;
	u32 remaining_len = max_len;

	/* header */
	if (unlikely(remaining_len < 8))
		return -1;
	*(__be16 *)curp = htons((u16)(pd->seqno));
	curp += 2;
	*(__be16 *)curp = htons((u16)(pd->ack_seq));
	curp += 2;
	*(__be16 *)curp = htons((u16)(pd->ack_vec));
	curp += 2;
	*(__be16 *)curp = 0; /* checksum */
	curp += 2;
	remaining_len -= 8;

	/* RESET */
	if (pd->send_reset) {
		u32 hi_word;

		if (unlikely(remaining_len < 8))
			return -2;

		hi_word = (FASTPASS_PTYPE_RESET << 28) |
					((pd->reset_timestamp >> 32) & 0x00FFFFFF);
		*(__be32 *)curp = htonl(hi_word);
		*(__be32 *)(curp + 4) = htonl((u32)pd->reset_timestamp);
		curp += 8;
		remaining_len -= 8;
	}

#ifdef FASTPASS_CONTROLLER
	if (pd->alloc_tslot > 0) {
		/* ALLOC type short */
		*(__be16 *)curp = htons((FASTPASS_PTYPE_ALLOC << 12)
								| (pd->n_dsts << 8)
								|  ((pd->alloc_tslot + 1) / 2));
		curp += 2;
		*(__be16 *)curp = htons(pd->base_tslot);
		curp += 2;
		for (i = 0; i < pd->n_dsts; i++) {
			*(__be16 *)curp = htons(pd->dsts[i]);
			curp += 2;
		}
		memcpy(curp, pd->tslot_desc, pd->alloc_tslot);
		curp += pd->alloc_tslot;
	}
	(void) i; (void) areq; (void)max_len; /* TODO, fix this better */
#endif

	/* Must encode the A-REQ *after* allocations for correct endnode handling */
	if (pd->n_areq > 0) {
		if (unlikely(remaining_len < 2 + 4 * pd->n_areq))
			return -3;

		/* A-REQ type short */
		*(__be16 *)curp = htons((FASTPASS_PTYPE_AREQ << 12) |
						  (pd->n_areq & 0x3F));
		curp += 2;
		remaining_len -= 2;

		/* A-REQ requests */
		for (i = 0; i < pd->n_areq; i++) {
			areq = (struct fastpass_areq *)curp;
			areq->dst = htons((__be16)pd->areq[i].src_dst_key);
			areq->count = htons((u16)pd->areq[i].tslots);
			curp += 4;
			remaining_len -= 4;
		}
	}

	if (curp - pkt < min_size) {
		if (unlikely(remaining_len < min_size - (curp - pkt)))
			return -4;
		/* add padding */
		memset(curp, 0, min_size - (curp - pkt));
		curp = pkt + min_size;
	}

	/* checksum */
	*(__be16 *)(pkt + 6) = fastpass_checksum(pkt, curp - pkt, saddr, daddr,
			pd->seqno, pd->ack_seq);

	fp_debug("encoded pkt with seq 0x%llX ack_seq 0x%llX checksum 0x%04X len %ld\n",
			pd->seqno, pd->ack_seq, *(__be16 *)(pkt + 6), curp - pkt);

	if (unlikely((int)(curp - pkt) > max_len))
		return -5;

	return (int)(curp - pkt);
}

void fpproto_dump_stats(struct fpproto_conn *conn, struct fp_proto_stat *stat)
{
	memcpy(stat, &conn->stat, sizeof(conn->stat));
}

void fpproto_update_internal_stats(struct fpproto_conn *conn)
{
	conn->stat.version				= FASTPASS_PROTOCOL_STATS_VERSION;
	conn->stat.last_reset_time		= conn->last_reset_time;
	conn->stat.out_max_seqno		= wnd_head(&conn->outwnd);
	conn->stat.in_max_seqno			= conn->in_max_seqno;
	conn->stat.in_sync				= conn->in_sync;
	conn->stat.consecutive_bad_pkts	= (__u16)conn->consecutive_bad_pkts;
	conn->stat.tx_num_unacked		= (__u16)wnd_num_marked(&conn->outwnd);
	conn->stat.earliest_unacked		=
			wnd_empty(&conn->outwnd) ? 0 : wnd_earliest_marked(&conn->outwnd);
	conn->stat.inwnd					= conn->inwnd;
	conn->stat.next_timeout_seqno	= conn->next_timeout_seqno;
}

void fpproto_init_conn(struct fpproto_conn *conn, struct fpproto_ops *ops,
		void *ops_param, u64 rst_win_ns, u64 send_timeout)
{
	/* choose reset time */
	do_proto_reset(conn, fp_get_time_ns(), false);

#ifdef FASTPASS_PERFORM_RUNTIME_TESTS
	outwnd_test(conn);
#endif

	/* ops */
	conn->ops = ops;
	conn->ops_param = ops_param;

	/* timeouts */
	conn->rst_win_ns = rst_win_ns;
	conn->send_timeout = send_timeout;
}

void fpproto_destroy_conn(struct fpproto_conn *conn)
{
	/* clear unacked packets */
	free_unacked(conn);
}

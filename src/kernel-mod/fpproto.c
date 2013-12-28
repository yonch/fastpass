/*
 * Platform independent FastPass protocol implementation
 */

#include "outwnd.h"
#include "platform.h"

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
u64 base_seqno_from_timestamp(u64 reset_time)
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
__sum16 fpproto_checksum(u8 *pkt, u32 len, __be32 saddr, __be32 daddr,
		u64 seqno)
{
	u32 seq_hash = jhash_1word((u32)seqno, seqno >> 32);
	__wsum csum = csum_partial(pkt, len, seq_hash);
	return csum_tcpudp_magic(saddr, daddr, len, IPPROTO_FASTPASS, csum);
}

void cancel_and_reset_retrans_timer(struct fpproto_conn *conn)
{
	u64 timeout;
	u64 seqno;

	cancel_timer(conn);

	if (outwnd_empty(conn)) {
		fastpass_pr_debug("all packets acked, no need to set timer\n");
		return;
	}

	/* find the earliest unacked, and the timeout */
	seqno = outwnd_earliest_unacked(conn);
	timeout = outwnd_timestamp(conn, seqno) + conn->send_timeout_us;

	/* set timer and earliest_unacked */
	conn->earliest_unacked = seqno;
	set_timer(conn, timeout);
	conn->stat.reprogrammed_timer++;
	fastpass_pr_debug("setting timer to %llu for seq#=0x%llX\n", timeout, seqno);
}

void do_ack_seqno(struct fpproto_conn *conn, u64 seqno)
{
	struct fpproto_pktdesc *pd;

	BUG_ON(time_after_eq64(seqno, conn->next_seqno));
	BUG_ON(time_before64(seqno, conn->next_seqno - FASTPASS_OUTWND_LEN));

	fastpass_pr_debug("ACK seqno 0x%08llX\n", seqno);
	conn->stat.acked_packets++;
	BUG_ON(!outwnd_is_unacked(conn, seqno));
	pd = outwnd_pop(conn, seqno);

	if (conn->ops->handle_ack)
		conn->ops->handle_ack(conn->ops_param, pd);		/* will free pd */
	else
		fpproto_pktdesc_free(pd);
}

void do_neg_ack_seqno(struct fpproto_conn *conn, u64 seq)
{
	struct fpproto_pktdesc *pd = outwnd_pop(conn, seq);
	fastpass_pr_debug("Unacked tx seq 0x%llX\n", seq);
	if (conn->ops->handle_neg_ack)
		conn->ops->handle_neg_ack(conn->ops_param, pd);		/* will free pd */
	else
		fpproto_pktdesc_free(pd);
}

static void do_proto_reset(struct fpproto_conn *conn, u64 reset_time,
		bool in_sync)
{
	u64 base_seqno = base_seqno_from_timestamp(reset_time);
	/* clear unacked packets */
	outwnd_reset(conn);

	/* set new sequence numbers */
	conn->last_reset_time = reset_time;
	conn->next_seqno = base_seqno + FASTPASS_TO_CONTROLLER_SEQNO_OFFSET;
	conn->in_max_seqno = base_seqno + FASTPASS_TO_ENDPOINT_SEQNO_OFFSET - 1;
	conn->inwnd = ~0UL;
	conn->consecutive_bad_pkts = 0;

	/* are we in sync? */
	conn->in_sync = in_sync;

	/* statistics */
	conn->stat.proto_resets++;
}


void fpproto_handle_timeout(struct fpproto_conn *conn, u64 now)
{
	u64 seqno;
	u64 timeout;

	conn->stat.tasklet_runs++;

	/* notify qdisc of expired timeouts */
	seqno = conn->earliest_unacked;
	while (!outwnd_empty(conn)) {
		/* find seqno and timeout of next unacked packet */
		seqno = outwnd_earliest_unacked_hint(conn, seqno);
		timeout = outwnd_timestamp(conn, seqno) + conn->send_timeout_us;

		/* if timeout hasn't expired, we're done */
		if (unlikely(time_after64(timeout, now)))
			goto set_next_timer;

		conn->stat.timeout_pkts++;
		do_neg_ack_seqno(conn, seqno);
	}
	fastpass_pr_debug("outwnd empty, not setting timer\n");
	return;

set_next_timer:
	/* seqno is the earliest unacked seqno, and timeout is its timeout */
	conn->earliest_unacked = seqno;
	set_timer(conn, timeout);
	fastpass_pr_debug("setting timer to %llu for seq#=0x%llX\n", timeout, seqno);
}

/*
 * returns 0 if okay to continue processing, 1 to drop
 */
static int fpproto_handle_reset(struct fpproto_conn *conn, u64 full_tstamp)
{
	u64 now = fp_get_time_ns();

	conn->stat.reset_payloads++;
	fastpass_pr_debug("got RESET, last is 0x%llX, full 0x%llX, now 0x%llX\n",
			conn->last_reset_time, full_tstamp, now);

	if (full_tstamp == conn->last_reset_time) {
		if (!conn->in_sync) {
			conn->in_sync = 1;
			fastpass_pr_debug("Now in sync\n");
		} else {
			conn->stat.redundant_reset++;
			fastpass_pr_debug("received redundant reset\n");
		}
		return 0;
	}

	/* reject resets outside the time window */
	if (unlikely(!tstamp_in_window(full_tstamp, now, conn->rst_win_ns))) {
		fastpass_pr_debug("Reset was out of reset window (diff=%lld)\n",
				(s64)full_tstamp - (s64)now);
		conn->stat.reset_out_of_window++;
		return 1;
	}

	/* if we already processed a newer reset within the window */
	if (unlikely(tstamp_in_window(conn->last_reset_time, now, conn->rst_win_ns)
			&& time_before64(full_tstamp, conn->last_reset_time))) {
		fastpass_pr_debug("Already processed reset within window which is %lluns more recent\n",
						conn->last_reset_time - full_tstamp);
		conn->stat.outdated_reset++;
		return 1;
	}

	/* okay, accept the reset */
	do_proto_reset(conn, full_tstamp, true);
	if (conn->ops->handle_reset)
		conn->ops->handle_reset(conn->ops_param);
	return 0;
}

void fpproto_handle_ack(struct fpproto_conn *fp, u16 ack_seq, u32 ack_runlen)
{
	u64 cur_seqno;
	s32 next_unacked;
	u64 end_seqno;
	int n_acked = 0;

	fp->stat.ack_payloads++;

	/* find full seqno, strictly before fp->next_seqno */
	cur_seqno = fp->next_seqno - (1 << 16);
	cur_seqno += (ack_seq - cur_seqno) & 0xFFFF;

	/* is the seqno within the window? */
	if (time_before64(cur_seqno, fp->next_seqno - FASTPASS_OUTWND_LEN))
		goto ack_too_early;

	/* if the ack_seq is unacknowledged, process the ack on it */
	if (outwnd_is_unacked(fp, cur_seqno)) {
		do_ack_seqno(fp, cur_seqno);
		n_acked++;
	}
	end_seqno = cur_seqno - 1;

	/* start with the positive nibble */
	ack_runlen <<= 4;

do_next_positive:
	cur_seqno = end_seqno;
	end_seqno -= (ack_runlen >> 28);
	ack_runlen <<= 4;
do_next_unacked:
	/* find next unacked */
	next_unacked = outwnd_at_or_before(fp, cur_seqno);
	if (next_unacked == -1)
		goto done;
	cur_seqno -= next_unacked;

	if (likely(time_after64(cur_seqno, end_seqno))) {
		/* got ourselves an unacked seqno that should be acked */
		do_ack_seqno(fp, cur_seqno);
		n_acked++;
		/* try to find another seqno that should be acked */
		goto do_next_unacked;
	}
	/* finished handling this run. if more runs, handle them as well */
	if (likely(ack_runlen != 0)) {
		end_seqno -= (ack_runlen >> 28);
		ack_runlen <<= 4;
		goto do_next_positive; /* continue handling */
	}
done:
	if (n_acked > 0) {
		cancel_and_reset_retrans_timer(fp);
		fp->stat.informative_ack_payloads++;
	}
	return;

ack_too_early:
	fastpass_pr_debug("too_early_ack: earliest %llu, got %llu\n",
			fp->next_seqno - FASTPASS_OUTWND_LEN, cur_seqno);
	fp->stat.too_early_ack++;
}

void got_good_packet(struct fpproto_conn *conn)
{
	conn->consecutive_bad_pkts = 0;
}

void got_bad_packet(struct fpproto_conn *conn)
{
	u64 now = fp_get_time_ns();

	conn->consecutive_bad_pkts++;
	fastpass_pr_debug("#%u consecutive bad packets\n", conn->consecutive_bad_pkts);

	if (conn->consecutive_bad_pkts < FASTPASS_BAD_PKT_RESET_THRESHOLD)
		goto out;

	/* got too many bad packets */

	/* reset bad packet count to 0 for continued operation */
	conn->consecutive_bad_pkts = 0;

	/* was there a recent reset? */
	if (time_in_range64(
			now - FASTPASS_RESET_WINDOW_NS,
			conn->last_reset_time,
			now + FASTPASS_RESET_WINDOW_NS)) {
		/* will not trigger a new one */
		conn->stat.no_reset_because_recent++;
		fastpass_pr_debug("had a recent reset (last %llu, now %llu). not issuing a new one.\n",
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
		conn->ops->trigger_request(conn->ops_param, now);
}

/**
 * Updates the incoming packet window
 * @return 0 if update is successful
 * 		   1 if caller should drop the packet with seqno
 */
int update_inwnd(struct fpproto_conn *conn, u64 seqno)
{
	u64 head = conn->in_max_seqno;

	/* seqno >= head + 64 ? */
	if (unlikely(time_after_eq64(seqno, head + 64))) {
		conn->stat.inwnd_jumped++;
		conn->in_max_seqno = seqno;
		conn->inwnd = 1UL;
		return 0; /* accept */
	}

	/* seqno in [head+1, head+63] ? */
	if (likely(time_after64(seqno, head))) {
		/* advance no more than 63 */
		conn->inwnd <<= (seqno - head);
		conn->inwnd |= 1UL;
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
	if (conn->inwnd & (1UL << (head - seqno))) {
		/* already marked as received */
		conn->stat.rx_dup_pkt++;
		return 1; /* drop */
	}

	conn->inwnd |= (1UL << (head - seqno));
	conn->stat.rx_out_of_order++;
	return 0; /* accept */
}

void fpproto_handle_rx_packet(struct fpproto_conn *conn, u8 *pkt, u32 len,
		__be32 saddr, __be32 daddr)
{
	struct fastpass_hdr *hdr;
	u64 full_seqno;
	u16 payload_type;
	u64 rst_tstamp = 0;
	__sum16 checksum;
	int i;
	u8 *curp;
	u8 *data_end;
	int alloc_n_dst, alloc_n_tslots;
	u16 alloc_dst[16];
	u32 alloc_base_tslot;
	u16 ack_seq;
	u32 ack_runlen;

	conn->stat.rx_pkts++;

	if (len < 9)
		goto packet_too_short;

	hdr = (struct fastpass_hdr *)pkt;
	curp = &pkt[8];
	data_end = &pkt[len];
	payload_type = *curp >> 4;

	/* get full 64-bit sequence number for the pseudo-header */
	if (unlikely(payload_type == FASTPASS_PTYPE_RESET)) {
		/* DERIVE SEQNO FROM RESET TIMESTAMP */
		u64 now = fp_get_time_ns();
		u64 partial_tstamp;

		if (unlikely(curp + 8 > data_end))
			goto incomplete_reset_payload;

		/* get lower 56 bits of timestamp */
		partial_tstamp = ((u64)(ntohl(*(u32 *)curp) & ((1 << 24) - 1)) << 32) |
				ntohl(*(u32 *)(curp + 4));

		/* reconstruct all 64 bits of timestamp */
		rst_tstamp = now - (1ULL << 55);
		rst_tstamp += (partial_tstamp - rst_tstamp) & ((1ULL << 56) - 1);

		full_seqno = base_seqno_from_timestamp(rst_tstamp) +
					FASTPASS_TO_ENDPOINT_SEQNO_OFFSET;
	} else {
		/* GET SEQNO FROM PREVIOUS SEQNO */
		full_seqno = conn->in_max_seqno - (1 << 14);
	}
	full_seqno += (ntohs(hdr->seq) - full_seqno) & 0xFFFF;
	fastpass_pr_debug("packet with seqno 0x%04X (full 0x%llX, prev_max 0x%llX)\n",
			ntohs(hdr->seq), full_seqno, conn->in_max_seqno);

	/* verify checksum */
	checksum = fpproto_checksum(pkt, len, saddr, daddr, full_seqno);
	if (unlikely(checksum != 0)) {
		got_bad_packet(conn);
		goto bad_checksum; /* will drop packet */
	} else {
		got_good_packet(conn);
	}

	if (unlikely(payload_type == FASTPASS_PTYPE_RESET)) {
		if (fpproto_handle_reset(conn, rst_tstamp) != 0)
			/* reset was not applied, drop packet */
			return;
		if (curp == data_end)
			/* only RESET in this packet, we're done with it */
			return;
	}

	/* update inwnd */
	if (update_inwnd(conn, full_seqno) != 0)
		return; /* drop packet to keep at-most-once semantics */


handle_payload:
	/* at this point we know there is at least one byte remaining */
	payload_type = *curp >> 4;

	switch (payload_type) {
	case FASTPASS_PTYPE_ALLOC:
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

		curp += alloc_n_tslots;
		break;
	case FASTPASS_PTYPE_ACK:
		if (curp + 6 > data_end)
			goto incomplete_ack_payload;

		ack_runlen = ntohl(*(u32 *)curp);
		ack_seq = ntohs(*(u16 *)(curp + 4));

		fpproto_handle_ack(conn, ack_seq, ack_runlen);

		curp += 6;
		break;
	default:
		goto unknown_payload_type;
	}

	/* more payloads in packet? */
	if (curp < data_end)
		goto handle_payload;

unknown_payload_type:
	conn->stat.rx_unknown_payload++;
	fastpass_pr_debug("got unknown payload type %d\n", payload_type);
	return;

incomplete_reset_payload:
	conn->stat.rx_incomplete_reset++;
	fastpass_pr_debug("RESET payload incomplete, expected 8 bytes, got %d\n",
			(int)(data_end - curp));
	return;

incomplete_alloc_payload_one_byte:
	conn->stat.rx_incomplete_alloc++;
	fastpass_pr_debug("ALLOC payload incomplete, only got one byte\n");
	return;

incomplete_alloc_payload:
	conn->stat.rx_incomplete_alloc++;
	fastpass_pr_debug("ALLOC payload incomplete: expected %d bytes, got %d\n",
			2 + 2 * alloc_n_dst + alloc_n_tslots, (int)(data_end - curp));
	return;

incomplete_ack_payload:
	conn->stat.rx_incomplete_ack++;
	fastpass_pr_debug("ACK payload incomplete: expected 6 bytes, got %d\n",
			(int)(data_end - curp));
	return;

bad_checksum:
	conn->stat.rx_checksum_error++;
	fastpass_pr_debug("checksum error. expected 0, got 0x%04X\n", checksum);
	return;

packet_too_short:
	conn->stat.rx_too_short++;
	fastpass_pr_debug("packet less than minimal size (len=%d)\n", len);
	return;
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
	u64 window_edge = conn->next_seqno - FASTPASS_OUTWND_LEN;

	/* make sure outwnd is not holding a packet descriptor where @pd will be */
	if (outwnd_is_unacked(conn, window_edge)) {
		/* treat packet going out of outwnd as if it was dropped */
		conn->stat.fall_off_outwnd++;
		do_neg_ack_seqno(conn, window_edge);

		/* reset timer if needed */
		cancel_and_reset_retrans_timer(conn);
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
	pd->seqno = conn->next_seqno;
	pd->send_reset = !conn->in_sync;
	pd->reset_timestamp = conn->last_reset_time;
	pd->ack_seq = (u16)conn->in_max_seqno;
	pd->ack_vec = ((conn->inwnd >> 1) & 0x7FFF);
	pd->ack_vec |= ((conn->inwnd & (~0UL << 16)) == (~0UL << 16)) << 15;

	/* add packet to outwnd, will advance fp->next_seqno */
	outwnd_add(conn, pd);

	/* if first packet in outwnd, enqueue timer and set fp->earliest_unacked */
	if (conn->tx_num_unacked == 1) {
		u64 timeout = pd->sent_timestamp + conn->send_timeout_us;
		conn->earliest_unacked = pd->seqno;
		set_timer(conn, timeout);
		fastpass_pr_debug("first packet in outwnd. setting timer to %llu for seq#=0x%llX\n", timeout, pd->seqno);
	}
}

int fpproto_encode_packet(struct fpproto_conn *conn,
		struct fpproto_pktdesc *pd, u8 *pkt, u32 max_len, __be32 saddr,
		__be32 daddr)
{
	int i;
	struct fastpass_areq *areq;

	u8 *curp = pkt;

	/* header */
	*(__be16 *)curp = htons((u16)(pd->seqno));
	curp += 2;
	*(__be16 *)curp = htons((u16)(pd->ack_seq));
	curp += 2;
	*(__be16 *)curp = htons((u16)(pd->ack_vec));
	curp += 2;
	*(__be16 *)curp = 0; /* checksum */
	curp += 2;

	/* RESET */
	if (pd->send_reset) {
		u32 hi_word;
		hi_word = (FASTPASS_PTYPE_RSTREQ << 28) |
					((pd->reset_timestamp >> 32) & 0x00FFFFFF);
		*(__be32 *)curp = htonl(hi_word);
		*(__be32 *)(curp + 4) = htonl((u32)pd->reset_timestamp);
		curp += 8;
	}

	/* A-REQ type short */
	*(__be16 *)curp = htons((FASTPASS_PTYPE_AREQ << 12) |
					  (pd->n_areq & 0x3F));
	curp += 2;

	/* A-REQ requests */
	for (i = 0; i < pd->n_areq; i++) {
		areq = (struct fastpass_areq *)curp;
		areq->dst = htons((__be16)pd->areq[i].src_dst_key);
		areq->count = htons((u16)pd->areq[i].tslots);
		curp += 4;
	}

	/* checksum */
	*(__be16 *)(pkt + 6) = fpproto_checksum(pkt, curp - pkt, saddr, daddr,
			pd->seqno);

	return curp - pkt;
}

void fpproto_init_conn(struct fpproto_conn *conn, struct fpproto_ops *ops,
		void *ops_param, u64 rst_win_ns, u64 send_timeout_us)
{
	/* initialize outwnd */
	memset(conn->bin_mask, 0, sizeof(conn->bin_mask));
#ifdef FASTPASS_PERFORM_RUNTIME_TESTS
	outwnd_test(conn);
#endif

	/* choose reset time */
	do_proto_reset(conn, fp_get_time_ns(), false);

	/* ops */
	conn->ops = ops;
	conn->ops_param = ops_param;

	/* timeouts */
	conn->rst_win_ns = rst_win_ns;
	conn->send_timeout_us = send_timeout_us;
}

void fpproto_destroy_conn(struct fpproto_conn *conn)
{
	/* clear unacked packets */
	outwnd_reset(conn);
}

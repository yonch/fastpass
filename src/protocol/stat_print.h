
#ifndef STAT_PRINT_H_
#define STAT_PRINT_H_

#include "fpproto.h"
#include <stdio.h>

static void fpproto_print_stats(FILE* f, struct fp_proto_stat* sps);
static void fpproto_print_errors(FILE* f, struct fp_proto_stat* sps);
static void fpproto_print_warnings(FILE* f, struct fp_proto_stat* sps);
static void fpproto_print_complete(FILE* f, struct fp_proto_stat* sps);


static int __fpproto_print_assert_version(FILE *f, struct fp_proto_stat* sps)
{
	if (sps->version != FASTPASS_PROTOCOL_STATS_VERSION) {
		fprintf(f, "  unknown protocol statistics version number %d, expected %d\n",
			sps->version, FASTPASS_PROTOCOL_STATS_VERSION);
		return -1;
	}
	return 0;
}

static void fpproto_print_stats(FILE* f, struct fp_proto_stat* sps)
{
	if (!__fpproto_print_assert_version(f, sps))
		return;

	/* protocol state */
	fprintf(f, "\n  protocol in_sync=%d", sps->in_sync);
	fprintf(f, "\n  last reset 0x%llX", sps->last_reset_time);
	fprintf(f, "\n  ingress_seq 0x%llX", sps->in_max_seqno);
	fprintf(f, ", inwnd 0x%016llX", sps->inwnd);
	fprintf(f, ", consecutive bad %d", sps->consecutive_bad_pkts);
	fprintf(f, "\n  egress_seq 0x%llX", sps->out_max_seqno);
	fprintf(f, ", earliest_unacked 0x%llX", sps->earliest_unacked);
	/* TX */
	fprintf(f, "\n  TX %llu ctrl pkts", sps->committed_pkts);
	fprintf(f, " (%llu acked, %llu timeout, %llu fell off)", sps->acked_packets,
			sps->timeout_pkts, sps->fall_off_outwnd);
	fprintf(f, ", %llu timeouts", sps->tasklet_runs);
	fprintf(f, ", %llu timer_sets", sps->reprogrammed_timer);
	fprintf(f, "\n  %llu ack payloads", sps->ack_payloads);
	fprintf(f, " (%llu w/new info)", sps->informative_ack_payloads);
	fprintf(f, ", %d currently unacked", sps->tx_num_unacked);
	/* RX */
	fprintf(f, "\n  RX %llu ctrl pkts", sps->rx_pkts);
	fprintf(f, " (%llu out-of-order)", sps->rx_out_of_order);
	fprintf(f, "\n  %llu RX reset payloads", sps->reset_payloads);
	fprintf(f,
			" (%llu redundant, %llu/%llu both-recent lost/won, %llu old w/recent last, %llu recent w/old last, %llu both-old)",
			sps->redundant_reset, sps->reset_both_recent_last_reset_wins,
			sps->reset_both_recent_payload_wins,
			sps->reset_last_recent_payload_old,
			sps->reset_last_old_payload_recent, sps->reset_both_old);
	/* executed resets */
	fprintf(f, "\n  executed %llu resets", sps->proto_resets);
	fprintf(f, " (%llu due to bad pkts, %llu forced)", sps->reset_from_bad_pkts,
			sps->forced_reset);
	fprintf(f, ", %llu no reset from bad pkts", sps->no_reset_because_recent);
}

void fpproto_print_errors(FILE* f, struct fp_proto_stat* sps)
{
	if (!__fpproto_print_assert_version(f, sps))
		return;

	if (sps->rx_too_short)
		fprintf(f, "\n  %llu rx control packets too short", sps->rx_too_short);

	if (sps->rx_unknown_payload)
		fprintf(f, "\n  %llu rx control packets with unknown payload",
				sps->rx_unknown_payload);

	if (sps->rx_incomplete_reset)
		fprintf(f, "\n  %llu rx incomplete RESET payload",
				sps->rx_incomplete_reset);

	if (sps->rx_incomplete_alloc)
		fprintf(f, "\n  %llu rx incomplete ALLOC payload",
				sps->rx_incomplete_alloc);

	if (sps->rx_incomplete_ack)
		fprintf(f, "\n  %llu rx incomplete ACK payload",
				sps->rx_incomplete_ack);

	if (sps->rx_incomplete_areq)
		fprintf(f, "\n  %llu rx incomplete A-REQ payload",
				sps->rx_incomplete_areq);
}

static void fpproto_print_warnings(FILE* f, struct fp_proto_stat* sps)
{
	if (!__fpproto_print_assert_version(f, sps))
		return;

	if (sps->too_early_ack)
		fprintf(f, "\n  %llu acks were so late the seq was before the window",
				sps->too_early_ack);

	if (sps->fall_off_outwnd)
		fprintf(f,
				"\n  %llu packets dropped off egress window before their timeout (window too short? unreliable timeout?)",
				sps->fall_off_outwnd);

	if (sps->rx_dup_pkt)
		fprintf(f, "\n  %llu rx duplicate packets detected", sps->rx_dup_pkt);

	if (sps->rx_checksum_error)
		fprintf(f, "\n  %llu rx checksum failures", sps->rx_checksum_error);

	if (sps->inwnd_jumped)
		fprintf(f, "\n  %llu inwnd jumped by >=64", sps->inwnd_jumped);

	if (sps->seqno_before_inwnd)
		fprintf(f, "\n  %llu major reordering events", sps->seqno_before_inwnd);
}

static inline void fpproto_print_complete(FILE* f, struct fp_proto_stat* sps)
{
	fpproto_print_stats(f, sps);
	/* errors */
	fprintf(f, "\n errors:");
	fpproto_print_errors(f, sps);
	/* warnings */
	fprintf(f, "\n warnings:");
	fpproto_print_warnings(f, sps);
}


#endif /* STAT_PRINT_H_ */

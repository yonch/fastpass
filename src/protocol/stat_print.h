
#ifndef STAT_PRINT_H_
#define STAT_PRINT_H_

#include "fpproto.h"
#include <stdio.h>

#define CONN_LOG_STRUCT_VERSION		3

struct conn_log_struct {
	uint16_t version;
	uint16_t node_id;
	uint32_t backlog;
	uint64_t timestamp;
	int64_t next_retrans_gap;
	int64_t next_tx_gap;
	int64_t pacer_gap;
	uint64_t demands;
	struct fp_proto_stat stat;
};

static inline void fpproto_print_stats(struct fp_proto_stat* sps, void *file);
static inline void fpproto_print_errors(struct fp_proto_stat* sps, void *file);
static inline void fpproto_print_warnings(struct fp_proto_stat* sps, void *file);
static inline void fpproto_print_log_struct(struct conn_log_struct *conn_log, void *file);

static inline int __fpproto_print_assert_version(struct fp_proto_stat* sps)
{
	if (sps->version != FASTPASS_PROTOCOL_STATS_VERSION) {
		printf("  unknown protocol statistics version number %d, expected %d\n",
			sps->version, FASTPASS_PROTOCOL_STATS_VERSION);
		return -1;
	}
	return 0;
}

static inline void fpproto_print_stats(struct fp_proto_stat* sps, void *file)
{
	if (__fpproto_print_assert_version(sps) != 0)
		return;

	/* protocol state */
	fp_fprintf(file, "\n  protocol in_sync=%d", sps->in_sync);
	fp_fprintf(file, "\n  last reset 0x%llX", sps->last_reset_time);
	fp_fprintf(file, "\n  ingress_seq 0x%llX", sps->in_max_seqno);
	fp_fprintf(file, ", inwnd 0x%016llX", sps->inwnd);
	fp_fprintf(file, ", consecutive bad %d", sps->consecutive_bad_pkts);
	fp_fprintf(file, "\n  egress_seq 0x%llX", sps->out_max_seqno);
	fp_fprintf(file, ", earliest_unacked 0x%llX", sps->earliest_unacked);
	fp_fprintf(file, ", next_timeout seq 0x%llX", sps->next_timeout_seqno);
	/* TX */
	fp_fprintf(file, "\n  TX %llu ctrl pkts", sps->committed_pkts);
	fp_fprintf(file, " (%llu acked, %llu not acked, %llu timeout, %llu fell off)",
			sps->acked_packets, sps->never_acked_pkts, sps->timeout_pkts,
			sps->fall_off_outwnd);
	fp_fprintf(file, ", %llu timeouts", sps->timeout_handler_runs);
	fp_fprintf(file, ", %llu timer_sets", sps->reprogrammed_timer);
	fp_fprintf(file, "\n  %llu ack payloads", sps->ack_payloads);
	fp_fprintf(file, " (%llu w/new info)", sps->informative_ack_payloads);
	fp_fprintf(file, ", %d currently unacked", sps->tx_num_unacked);
	/* RX */
	fp_fprintf(file, "\n  RX %llu ctrl pkts", sps->rx_pkts);
	fp_fprintf(file, " (%llu out-of-order)", sps->rx_out_of_order);
	fp_fprintf(file, "\n  %llu RX reset payloads", sps->reset_payloads);
	fp_fprintf(file, " (%llu redundant, %llu/%llu both-recent lost/won, %llu old w/recent last, %llu recent w/old last, %llu both-old)",
			sps->redundant_reset, sps->reset_both_recent_last_reset_wins,
			sps->reset_both_recent_payload_wins,
			sps->reset_last_recent_payload_old,
			sps->reset_last_old_payload_recent, sps->reset_both_old);
	/* executed resets */
	fp_fprintf(file, "\n  executed %llu resets", sps->proto_resets);
	fp_fprintf(file, " (%llu due to bad pkts, %llu forced)", sps->reset_from_bad_pkts,
			sps->forced_reset);
	fp_fprintf(file, ", %llu no reset from bad pkts", sps->no_reset_because_recent);
}

void fpproto_print_errors(struct fp_proto_stat* sps, void *file)
{
	if (__fpproto_print_assert_version(sps) != 0)
		return;

	if (sps->rx_too_short)
		fp_fprintf(file, "\n  %llu rx control packets too short", sps->rx_too_short);

	if (sps->rx_unknown_payload)
		fp_fprintf(file, "\n  %llu rx control packets with unknown payload",
				sps->rx_unknown_payload);

	if (sps->rx_incomplete_reset)
		fp_fprintf(file, "\n  %llu rx incomplete RESET payload",
				sps->rx_incomplete_reset);

	if (sps->rx_incomplete_alloc)
		fp_fprintf(file, "\n  %llu rx incomplete ALLOC payload",
				sps->rx_incomplete_alloc);

	if (sps->rx_incomplete_ack)
		fp_fprintf(file, "\n  %llu rx incomplete ACK payload",
				sps->rx_incomplete_ack);

	if (sps->rx_incomplete_areq)
		fp_fprintf(file, "\n  %llu rx incomplete A-REQ payload",
				sps->rx_incomplete_areq);
}

static inline void fpproto_print_warnings(struct fp_proto_stat* sps, void *file)
{
	if (__fpproto_print_assert_version(sps) != 0)
		return;

	if (sps->too_early_ack)
		fp_fprintf(file, "\n  %llu acks were so late the seq was before the window",
				sps->too_early_ack);

	if (sps->fall_off_outwnd)
		fp_fprintf(file, "\n  %llu packets dropped off egress window before their timeout (window too short? unreliable timeout?)",
				sps->fall_off_outwnd);

	if (sps->rx_dup_pkt)
		fp_fprintf(file, "\n  %llu rx duplicate packets detected", sps->rx_dup_pkt);

	if (sps->rx_checksum_error)
		fp_fprintf(file, "\n  %llu rx checksum failures", sps->rx_checksum_error);

	if (sps->inwnd_jumped)
		fp_fprintf(file, "\n  %llu inwnd jumped by >=64", sps->inwnd_jumped);

	if (sps->seqno_before_inwnd)
		fp_fprintf(file, "\n  %llu major reordering events", sps->seqno_before_inwnd);
}

static inline void fpproto_print_log_struct(struct conn_log_struct *conn_log, void *file)
{
	if (conn_log->version != CONN_LOG_STRUCT_VERSION) {
		fp_fprintf(file, "-- unknown conn_log version number %d, expected %d\n",
				conn_log->version, CONN_LOG_STRUCT_VERSION);
		return;
	}

	fp_fprintf(file, "\n--\n");
	fp_fprintf(file, "node %d timestamp %lu next_retrans %ld(cycles) next_tx %ld(cycles) pacer %ld(cycles)\n",
			conn_log->node_id, conn_log->timestamp, conn_log->next_retrans_gap,
			conn_log->next_tx_gap, conn_log->pacer_gap);
	fp_fprintf(file, "demand %lu backlog %u\n", conn_log->demands, conn_log->backlog);
	fpproto_print_stats(&conn_log->stat, file);
	/* errors */
	fp_fprintf(file, "\n errors:");
	fpproto_print_errors(&conn_log->stat, file);
	/* warnings */
	fp_fprintf(file, "\n warnings:");
	fpproto_print_warnings(&conn_log->stat, file);
	fp_fprintf(file, "\n");
}


#endif /* STAT_PRINT_H_ */

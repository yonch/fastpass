
#ifndef STAT_PRINT_H_
#define STAT_PRINT_H_

#include "fpproto.h"
#include <stdio.h>

#define CONN_LOG_STRUCT_VERSION		2

struct conn_log_struct {
	uint16_t version;
	uint16_t node_id;
	uint64_t timestamp;
	int64_t next_retrans_gap;
	int64_t next_tx_gap;
	int64_t pacer_gap;
	struct fp_proto_stat stat;
};

static inline void fpproto_print_stats(struct fp_proto_stat* sps);
static inline void fpproto_print_errors(struct fp_proto_stat* sps);
static inline void fpproto_print_warnings(struct fp_proto_stat* sps);
static inline void fpproto_print_log_struct(struct conn_log_struct *conn_log);

static inline int __fpproto_print_assert_version(struct fp_proto_stat* sps)
{
	if (sps->version != FASTPASS_PROTOCOL_STATS_VERSION) {
		printf("  unknown protocol statistics version number %d, expected %d\n",
			sps->version, FASTPASS_PROTOCOL_STATS_VERSION);
		return -1;
	}
	return 0;
}

static inline void fpproto_print_stats(struct fp_proto_stat* sps)
{
	if (__fpproto_print_assert_version(sps) != 0)
		return;

	/* protocol state */
	printf("\n  protocol in_sync=%d", sps->in_sync);
	printf("\n  last reset 0x%llX", sps->last_reset_time);
	printf("\n  ingress_seq 0x%llX", sps->in_max_seqno);
	printf(", inwnd 0x%016llX", sps->inwnd);
	printf(", consecutive bad %d", sps->consecutive_bad_pkts);
	printf("\n  egress_seq 0x%llX", sps->out_max_seqno);
	printf(", earliest_unacked 0x%llX", sps->earliest_unacked);
	/* TX */
	printf("\n  TX %llu ctrl pkts", sps->committed_pkts);
	printf(" (%llu acked, %llu timeout, %llu fell off)", sps->acked_packets,
			sps->timeout_pkts, sps->fall_off_outwnd);
	printf(", %llu timeouts", sps->tasklet_runs);
	printf(", %llu timer_sets", sps->reprogrammed_timer);
	printf("\n  %llu ack payloads", sps->ack_payloads);
	printf(" (%llu w/new info)", sps->informative_ack_payloads);
	printf(", %d currently unacked", sps->tx_num_unacked);
	/* RX */
	printf("\n  RX %llu ctrl pkts", sps->rx_pkts);
	printf(" (%llu out-of-order)", sps->rx_out_of_order);
	printf("\n  %llu RX reset payloads", sps->reset_payloads);
	printf(" (%llu redundant, %llu/%llu both-recent lost/won, %llu old w/recent last, %llu recent w/old last, %llu both-old)",
			sps->redundant_reset, sps->reset_both_recent_last_reset_wins,
			sps->reset_both_recent_payload_wins,
			sps->reset_last_recent_payload_old,
			sps->reset_last_old_payload_recent, sps->reset_both_old);
	/* executed resets */
	printf("\n  executed %llu resets", sps->proto_resets);
	printf(" (%llu due to bad pkts, %llu forced)", sps->reset_from_bad_pkts,
			sps->forced_reset);
	printf(", %llu no reset from bad pkts", sps->no_reset_because_recent);
}

void fpproto_print_errors(struct fp_proto_stat* sps)
{
	if (__fpproto_print_assert_version(sps) != 0)
		return;

	if (sps->rx_too_short)
		printf("\n  %llu rx control packets too short", sps->rx_too_short);

	if (sps->rx_unknown_payload)
		printf("\n  %llu rx control packets with unknown payload",
				sps->rx_unknown_payload);

	if (sps->rx_incomplete_reset)
		printf("\n  %llu rx incomplete RESET payload",
				sps->rx_incomplete_reset);

	if (sps->rx_incomplete_alloc)
		printf("\n  %llu rx incomplete ALLOC payload",
				sps->rx_incomplete_alloc);

	if (sps->rx_incomplete_ack)
		printf("\n  %llu rx incomplete ACK payload",
				sps->rx_incomplete_ack);

	if (sps->rx_incomplete_areq)
		printf("\n  %llu rx incomplete A-REQ payload",
				sps->rx_incomplete_areq);
}

static inline void fpproto_print_warnings(struct fp_proto_stat* sps)
{
	if (__fpproto_print_assert_version(sps) != 0)
		return;

	if (sps->too_early_ack)
		printf("\n  %llu acks were so late the seq was before the window",
				sps->too_early_ack);

	if (sps->fall_off_outwnd)
		printf("\n  %llu packets dropped off egress window before their timeout (window too short? unreliable timeout?)",
				sps->fall_off_outwnd);

	if (sps->rx_dup_pkt)
		printf("\n  %llu rx duplicate packets detected", sps->rx_dup_pkt);

	if (sps->rx_checksum_error)
		printf("\n  %llu rx checksum failures", sps->rx_checksum_error);

	if (sps->inwnd_jumped)
		printf("\n  %llu inwnd jumped by >=64", sps->inwnd_jumped);

	if (sps->seqno_before_inwnd)
		printf("\n  %llu major reordering events", sps->seqno_before_inwnd);
}

static inline void fpproto_print_log_struct(struct conn_log_struct *conn_log)
{
	if (conn_log->version != CONN_LOG_STRUCT_VERSION) {
		printf("-- unknown conn_log version number %d, expected %d\n",
				conn_log->version, CONN_LOG_STRUCT_VERSION);
		return;
	}

	printf("\n--\n");
	printf("node %d timestamp %lu next_retrans %ld(cycles) next_tx %ld(cycles) pacer %ld(cycles)\n",
			conn_log->node_id, conn_log->timestamp, conn_log->next_retrans_gap,
			conn_log->next_tx_gap, conn_log->pacer_gap);
	fpproto_print_stats(&conn_log->stat);
	/* errors */
	printf("\n errors:");
	fpproto_print_errors(&conn_log->stat);
	/* warnings */
	printf("\n warnings:");
	fpproto_print_warnings(&conn_log->stat);
	printf("\n");
}


#endif /* STAT_PRINT_H_ */

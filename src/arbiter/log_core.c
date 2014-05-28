/*
 * log_core.c
 *
 *  Created on: Jan 14, 2014
 *      Author: yonch
 */

#include <stdio.h>

#include <rte_log.h>

#include "log_core.h"
#include "control.h"
#include "comm_core.h"
#include "admission_core.h"
#include "admission_log.h"
#include "../grant-accept/partitioning.h"
#include "../graph-algo/algo_config.h"
#include "../protocol/fpproto.h"
#include "../protocol/platform.h"
#include "../protocol/stat_print.h"

#define MAX_FILENAME_LEN 256

#define RTE_LOGTYPE_LOGGING RTE_LOGTYPE_USER1
#define LOGGING_ERR(a...) RTE_LOG(CRIT, LOGGING, ##a)

static struct comm_log saved_comm_log;

void print_comm_log(uint16_t lcore_id)
{
	struct comm_log *cl = &comm_core_logs[lcore_id];
	struct comm_log *sv = &saved_comm_log;
	struct comm_core_state *ccs = &ccore_state[enabled_lcore[0]];
	u64 now_real = fp_get_time_ns();
	u64 now_timeslot = (now_real * TIMESLOT_MUL) >> TIMESLOT_SHIFT;

	printf("\ncomm_log lcore %d timeslot 0x%lX (now_timeslot 0x%llX, now - served %lld)",
			lcore_id, ccs->latest_timeslot[0], now_timeslot,
			(s64)(now_timeslot - ccs->latest_timeslot[0]));

#define D(X) (cl->X - sv->X)
	printf("\n  RX %lu pkts, %lu bytes in %lu batches (%lu non-empty batches), %lu dropped",
			D(rx_pkts), D(rx_bytes), D(rx_batches), D(rx_non_empty_batches),
			D(dropped_rx_due_to_deadline));
	printf("\n  %lu total demand from %lu demand increases %lu demand remained, %lu neg-ack with alloc, %lu demands",
			D(total_demand), D(demand_increased), D(demand_remained), D(neg_acks_with_alloc),
			D(neg_ack_timeslots));
	printf("\n  %lu informative acks for %lu allocations, %lu non-informative",
			D(acks_with_alloc), D(total_acked_timeslots), D(acks_without_alloc));
	printf("\n  processed %lu tslots (%lu non-empty ptn) with %lu node-tslots",
			D(processed_tslots), D(non_empty_tslots), D(occupied_node_tslots));
	printf("\n  TX %lu pkts, %lu bytes, %lu triggers, %lu report-triggers",
			D(tx_pkt), D(tx_bytes), D(triggered_send), D(reports_triggered));
#undef D
	printf("\n");

	printf("\n  RX %lu pkts, %lu bytes in %lu batches (%lu non-empty batches), %lu dropped",
			cl->rx_pkts, cl->rx_bytes, cl->rx_batches, cl->rx_non_empty_batches,
			cl->dropped_rx_due_to_deadline);
	printf("\n  %lu watchdog, %lu non-IPv4, %lu IPv4 non-fastpass",
			cl->rx_watchdog_pkts, cl->rx_non_ipv4_pkts, cl->rx_ipv4_non_fastpss_pkts);
	printf("\n  %lu total demand from %lu demand increases, %lu demand remained",
			cl->total_demand, cl->demand_increased, cl->demand_remained);
	printf("\n  %lu informative acks for %lu allocations, %lu non-informative",
			cl->acks_with_alloc, cl->total_acked_timeslots, cl->acks_without_alloc);
	printf("\n  handled %lu resets", cl->handle_reset);

	printf("\n  processed %lu tslots (%lu non-empty ptn) with %lu node-tslots",
			cl->processed_tslots, cl->non_empty_tslots, cl->occupied_node_tslots);
	printf("\n  TX %lu pkts (%lu watchdogs), %lu bytes, %lu triggers, %lu report-triggers (%lu due to neg-acks(",
			cl->tx_pkt, cl->tx_watchdog_pkts, cl->tx_bytes, cl->triggered_send, cl->reports_triggered,
			cl->neg_ack_triggered_reports);
	printf("\n  set %lu timers, canceled %lu, expired %lu",
			cl->timer_set, cl->timer_cancel, cl->retrans_timer_expired);
	printf("\n  neg acks: %lu without alloc, %lu with alloc with %lu timeslots to %lu dsts",
			cl->neg_acks_without_alloc, cl->neg_acks_with_alloc,
			cl->neg_ack_timeslots, cl->neg_ack_destinations);

	printf("\n errors:");
	if (cl->tx_cannot_alloc_mbuf)
		printf("\n  %lu failures to allocate mbuf", cl->tx_cannot_alloc_mbuf);
	if (cl->rx_truncated_pkt)
		printf("\n  %lu rx packets were truncated", cl->rx_truncated_pkt);
	if (cl->areq_invalid_dst)
		printf("\n  %lu A-REQ payloads with invalid dst", cl->areq_invalid_dst);
	if (cl->dequeue_admitted_failed)
		printf("\n  %lu times couldn't dequeue a struct admitted_traffic!",
				cl->dequeue_admitted_failed);
	if (cl->error_encoding_packet)
		printf("\n  %lu times couldn't encode packet (due to bug?)",
				cl->error_encoding_packet);
	if (cl->failed_to_allocate_watchdog)
		printf("\n  %lu failed to allocate watchdog packet",
				cl->failed_to_allocate_watchdog);
	if (cl->failed_to_burst_watchdog)
		printf("\n  %lu failed to burst watchdog packet",
				cl->failed_to_burst_watchdog);

	printf("\n warnings:");
	if (cl->alloc_fell_off_window)
		printf("\n  %lu alloc fell off window", cl->alloc_fell_off_window);
	if (cl->flush_buffer_in_add_backlog)
		printf("\n  %lu buffer flushes in add backlog (buffer might be too small)",
				cl->flush_buffer_in_add_backlog);
	printf("\n");

	memcpy(&saved_comm_log, &comm_core_logs[lcore_id], sizeof(saved_comm_log));

}

struct admission_statistics saved_admission_statistics;

void print_global_admission_log() {
	struct admission_statistics *st = g_admission_stats();
	struct admission_statistics *sv = &saved_admission_statistics;
	int i;

#define D(X) (st->X - sv->X)
	#ifdef PARALLEL_ALGO
	printf("\nadmission core (pim with %d ptns, %d nodes per ptn)", N_PARTITIONS, PARTITION_N_NODES);
	#endif
	#ifdef PIPELINED_ALGO
	printf("\nadmission core (seq)");
	#endif
	printf("\n  enqueue waits: %lu q_head, %lu alloc_new_demands",
			st->wait_for_space_in_q_head, st->new_demands_bin_alloc_failed);
	printf("\n  add_backlog; %lu atomic add %0.2f to avg %0.2f; %lu queue add %0.2f to avg %0.2f",
			st->added_backlog_atomically,
			(float)st->backlog_sum_inc_atomically / (float)(st->added_backlog_atomically+1),
			(float)st->backlog_sum_atomically / (float)(st->added_backlog_atomically+1),
			st->added_backlog_to_queue,
			(float)st->backlog_sum_inc_to_queue / (float)(st->added_backlog_to_queue+1),
			(float)st->backlog_sum_to_queue / (float)(st->added_backlog_to_queue+1));
	printf("\n    %lu bin enqueues (%lu automatic, %lu forced)",
			st->backlog_flush_bin_full + st->backlog_flush_forced,
			st->backlog_flush_bin_full,
			st->backlog_flush_forced);
	printf("\n");
#undef D

	memcpy(sv, st, sizeof(*sv));
}

struct admission_core_statistics saved_admission_core_statistics[N_ADMISSION_CORES];

void print_admission_core_log(uint16_t lcore, uint16_t adm_core_index) {
	int i;
	struct admission_log *al = &admission_core_logs[lcore];
	struct admission_core_statistics *st = g_admission_core_stats(adm_core_index);
	struct admission_core_statistics *sv = &saved_admission_core_statistics[adm_core_index];

#define D(X) (st->X - sv->X)
	printf("admission lcore %d: %lu no_timeslot, %lu need more (avg %0.2f), %lu done",
			lcore, st->no_available_timeslots_for_bin_entry,
			st->allocated_backlog_remaining,
			(float)st->backlog_sum / (float)(st->allocated_backlog_remaining+1),
			st->allocated_no_backlog);
	printf("\n  %lu skipped %lu fail_alloc_admitted, %lu q_admitted_full. %lu bin_alloc_fail, %lu q_out_full, %lu wait_token",
			al->batches_skipped,
			st->admitted_traffic_alloc_failed, st->wait_for_space_in_q_admitted_out,
			st->out_bin_alloc_failed, st->wait_for_space_in_q_bin_out,
			st->waiting_to_pass_token);
	printf("\n  %lu flushed q_out (%lu automatic, %lu forced); processed from q_head %lu bins, %lu demands; wrap up q_out %lu bins, %lu demands, internal %lu bins %lu demands",
			st->q_out_flush_bin_full + st->q_out_flush_batch_finished,
			st->q_out_flush_bin_full, st->q_out_flush_batch_finished,
			st->new_request_bins, st->new_requests,
			st->dequeue_bin_during_wrap_up,
			st->dequeued_demands_during_wrap_up,
			st->wrap_up_non_empty_bin, st->wrap_up_non_empty_bin_demands);
	#ifdef PARALLEL_ALGO
	printf("\n    %lu phases completed, %lu not ready, %lu out of order",
               st->phase_finished, st->phase_none_ready, st->phase_out_of_order);
	#endif
	printf("\n");
#undef D

	printf("  backlog_hist: ");
	for (i = 0; i < BACKLOG_HISTOGRAM_NUM_BINS; i++)
		printf("%lu ", st->backlog_histogram[i]);
	printf ("\n");

	#ifdef PIPELINED_ALGO
	printf("  bin_index >> %d: ", BIN_SIZE_HISTOGRAM_SHIFT);
	for (i = 0; i < BIN_SIZE_HISTOGRAM_NUM_BINS; i++)
		printf("%lu ", st->bin_size_histogram[i]);
	printf ("\n");

	if (MAINTAIN_CORE_BIN_HISTOGRAM) {
		printf("  core_bin >> %d: ", CORE_BIN_HISTOGRAM_SHIFT);
		for (i = 0; i < CORE_BIN_HISTOGRAM_NUM_BINS; i++)
			printf("%lu ", st->core_bins_histogram[i]);
		printf ("\n");
	}
	#endif
}

int exec_log_core(void *void_cmd_p)
{
	struct log_core_cmd *cmd = (struct log_core_cmd *) void_cmd_p;
	uint64_t next_ticks = rte_get_timer_cycles();
	int i, j;
	struct conn_log_struct conn_log;
	FILE *fp;
	char filename[MAX_FILENAME_LEN];

	snprintf(filename, MAX_FILENAME_LEN, "log/conn-%016llX.csv",
			fp_get_time_ns());

	/* open file for conn log */
	fp = fopen(filename, "w");
	if (fp == NULL) {
		LOGGING_ERR("lcore %d could not open file for logging: %s\n",
				rte_lcore_id(), filename);
		return -1;
	}

	/* copy baseline statistics */
	memcpy(&saved_comm_log, &comm_core_logs[enabled_lcore[FIRST_COMM_CORE]],
			sizeof(saved_comm_log));
	memcpy(&saved_admission_statistics, g_admission_stats(),
			sizeof(saved_admission_statistics));
	for (i = 0; i < N_ADMISSION_CORES; i++)
		memcpy(&saved_admission_core_statistics[i],
		       g_admission_core_stats(i),
				sizeof(saved_admission_core_statistics[i]));

	while (1) {
		/* wait until proper time */
		while (next_ticks > rte_get_timer_cycles())
			rte_pause();

		print_comm_log(enabled_lcore[FIRST_COMM_CORE]);
		print_global_admission_log();
		for (i = 0; i < N_ADMISSION_CORES; i++)
			print_admission_core_log(enabled_lcore[FIRST_ADMISSION_CORE+i], i);
		fflush(stdout);

		/* write log */
//		for (i = 0; i < MAX_NODES; i++) {
		for (i = 49; i < 55; i++) {
			conn_log.version = CONN_LOG_STRUCT_VERSION;
			conn_log.node_id = i;
			conn_log.timestamp = fp_get_time_ns();
			comm_dump_stat(i, &conn_log);

			/* get backlog */
			conn_log.backlog = 0;
			for (j = 0; j < MAX_NODES; j++)
				conn_log.backlog +=
					backlog_get(g_admission_backlog(), i, j);

			if (fwrite(&conn_log, sizeof(conn_log), 1, fp) != 1)
				LOGGING_ERR("couldn't write conn info of node %d to file\n", i);
		}

		fflush(fp);

		next_ticks += cmd->log_gap_ticks;
	}

	return 0;
}

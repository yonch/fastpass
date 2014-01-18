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
#include "../protocol/fpproto.h"
#include "../protocol/platform.h"
#include "../graph-algo/admissible_structures.h"
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
			lcore_id, ccs->latest_timeslot, now_timeslot,
			(s64)(now_timeslot - ccs->latest_timeslot));

#define D(X) (cl->X - sv->X)
	printf("\n  RX %lu pkts, %lu bytes in %lu batches (%lu non-empty batches)",
			D(rx_pkts), D(rx_bytes), D(rx_batches), D(rx_non_empty_batches));
	printf("\n  %lu total demand from %lu demand increases %lu demand remained, %lu neg-ack with alloc, %lu demands",
			D(total_demand), D(demand_increased), D(demand_remained), D(neg_acks_with_alloc),
			D(neg_ack_timeslots));
	printf("\n  %lu informative acks for %lu allocations, %lu non-informative",
			D(acks_with_alloc), D(total_acked_timeslots), D(acks_without_alloc));
	printf("\n  processed %lu tslots (%lu non-empty) with %lu node-tslots",
			D(processed_tslots), D(non_empty_tslots), D(occupied_node_tslots));
	printf("\n  TX %lu pkts, %lu triggers, %lu report-triggers",
			D(tx_pkt), D(triggered_send), D(reports_triggered));
#undef D
	printf("\n");

	printf("\n  RX %lu pkts, %lu bytes in %lu batches (%lu non-empty batches)",
			cl->rx_pkts, cl->rx_bytes, cl->rx_batches, cl->rx_non_empty_batches);
	printf("\n  %lu non-IPv4, %lu IPv4 non-fastpass",
			cl->rx_non_ipv4_pkts, cl->rx_ipv4_non_fastpss_pkts);
	printf("\n  %lu total demand from %lu demand increases, %lu demand remained",
			cl->total_demand, cl->demand_increased, cl->demand_remained);
	printf("\n  %lu informative acks for %lu allocations, %lu non-informative",
			cl->acks_with_alloc, cl->total_acked_timeslots, cl->acks_without_alloc);
	printf("\n  handled %lu resets", cl->handle_reset);

	printf("\n  processed %lu tslots (%lu non-empty) with %lu node-tslots",
			cl->processed_tslots, cl->non_empty_tslots, cl->occupied_node_tslots);
	printf("\n  TX %lu pkts, %lu triggers, %lu report-triggers (%lu due to neg-acks(",
			cl->tx_pkt, cl->triggered_send, cl->reports_triggered, 
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

	printf("\n warnings:");
	if (cl->alloc_fell_off_window)
		printf("\n  %lu alloc fell off window", cl->alloc_fell_off_window);
	if (cl->flush_buffer_in_add_backlog)
		printf("\n  %lu buffer flushes in add backlog (buffer might be too small)",
				cl->flush_buffer_in_add_backlog);
	printf("\n");

	memcpy(&saved_comm_log, &comm_core_logs[lcore_id], sizeof(saved_comm_log));

}

void print_global_admission_log() {
	struct admission_statistics *st = &g_admissible_status.stat;
	printf("\nadmission core");
	printf("\n  enqueue waits: %lu q_head, %lu q_urgent, %lu q_admitted, %lu q_bin",
			st->wait_for_space_in_q_head, st->wait_for_space_in_q_urgent,
			st->wait_for_space_in_q_admitted_out, st->wait_for_space_in_q_bin_out);
	printf("\n  %lu delay in passing token", st->waiting_to_pass_token);
	printf("\n  %lu pacing wait", st->pacing_wait);
	printf("\n  %lu wait for q_bin_in", st->wait_for_q_bin_in);
	printf("\n");
}

void print_admission_core_log(uint16_t lcore) {
	struct admission_log *al = &admission_core_logs[lcore];
	printf("admission lcore %d: %lu failed alloc\n",
			lcore, al->failed_admitted_traffic_alloc);
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

	while (1) {
		/* wait until proper time */
		while (next_ticks > rte_get_timer_cycles())
			rte_pause();

		print_comm_log(enabled_lcore[FIRST_COMM_CORE]);
		print_global_admission_log();
		for (i = 0; i < N_ADMISSION_CORES; i++)
			print_admission_core_log(enabled_lcore[FIRST_ADMISSION_CORE+i]);
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
			for (j = 0; j < MAX_NODES; j++) {
				uint32_t index = get_status_index(i, j);
				conn_log.backlog +=
						atomic32_read(&g_admissible_status.flows[index].backlog);
			}

			if (fwrite(&conn_log, sizeof(conn_log), 1, fp) != 1)
				LOGGING_ERR("couldn't write conn info of node %d to file\n", i);
		}

		fflush(fp);

		next_ticks += cmd->log_gap_ticks;
	}

	return 0;
}

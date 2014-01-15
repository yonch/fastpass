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
#include "../protocol/fpproto.h"
#include "../protocol/platform.h"
#include "../graph-algo/admissible_structures.h"
#include "../protocol/stat_print.h"

#define MAX_FILENAME_LEN 256

#define RTE_LOGTYPE_LOGGING RTE_LOGTYPE_USER1
#define LOGGING_ERR(a...) RTE_LOG(CRIT, LOGGING, ##a)

void print_comm_log(uint16_t lcore_id)
{
	struct comm_log *cl = &comm_core_logs[lcore_id];
	printf("\ncomm_log lcore %d", lcore_id);
	printf("\n  RX %lu pkts in %lu batches (%lu non-empty batches)",
			cl->rx_pkts, cl->rx_batches, cl->rx_non_empty_batches);
	printf("\n  %lu non-IPv4, %lu IPv4 non-fastpass",
			cl->rx_non_ipv4_pkts, cl->rx_ipv4_non_fastpss_pkts);
	printf("\n  %lu demand increases, %lu demand remained",
			cl->demand_increased, cl->demand_remained);
	printf("\n  handled %lu resets", cl->handle_reset);

	printf("\n  processed %lu tslots (%lu non-empty) with %lu node-tslots",
			cl->processed_tslots, cl->non_empty_tslots, cl->occupied_node_tslots);
	printf("\n  TX %lu pkts, %lu triggers", cl->tx_pkt, cl->triggered_send);
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

	printf("\n warnings:");
	if (cl->alloc_fell_off_window)
		printf("\n  %lu alloc fell off window", cl->alloc_fell_off_window);
	printf("\n");
}

int exec_log_core(void *void_cmd_p)
{
	struct log_core_cmd *cmd = (struct log_core_cmd *) void_cmd_p;
	uint64_t next_ticks = rte_get_timer_cycles();
	int i;
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

	while (1) {
		/* wait until proper time */
		while (next_ticks > rte_get_timer_cycles())
			rte_pause();

		print_comm_log(enabled_lcore[0]);

		/* write log */
//		for (i = 0; i < MAX_NODES; i++) {
		for (i = 49; i < 50; i++) {
			conn_log.version = CONN_LOG_STRUCT_VERSION;
			conn_log.node_id = i;
			conn_log.timestamp = fp_get_time_ns();
			comm_dump_stat(i, &conn_log);
			if (fwrite(&conn_log, sizeof(conn_log), 1, fp) != 1)
				LOGGING_ERR("couldn't write conn info of node %d to file\n", i);
		}

		fflush(fp);

		next_ticks += cmd->log_gap_ticks;
	}

	return 0;
}

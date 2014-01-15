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

#define CONN_LOG_STRUCT_VERSION		1

struct conn_log_struct {
	uint16_t version;
	uint16_t node_id;
	struct fp_proto_stat stat;
};

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

		/* write log */
//		for (i = 0; i < MAX_NODES; i++) {
		for (i = 49; i < 50; i++) {
			conn_log.version = CONN_LOG_STRUCT_VERSION;
			conn_log.node_id = i;
			comm_dump_stat(i, &conn_log.stat);
			if (fwrite(&conn_log, sizeof(conn_log), 1, fp) != 1)
				LOGGING_ERR("couldn't write conn info of node %d to file\n", i);
#ifdef PRINT_CONN_LOG_TO_STDOUT
			printf(" node %d now_real %llu\n", i, fp_get_time_ns());
			fpproto_print_complete(stdout, &conn_log.stat);
#endif
		}

		next_ticks += cmd->log_gap_ticks;
	}

	return 0;
}

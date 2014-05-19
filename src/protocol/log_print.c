
#include <stdio.h>
#include <unistd.h>

#include "stat_print.h"

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

int main(int argc, char *argv[])
{
	FILE *f;

	if (argc != 2) {
		printf(" usage: %s <log_filename>\n", argv[0]);
		return -1;
	}

	f = fopen(argv[1],"r");
	if (f == NULL) {
		printf("could not open file %s\n", argv[1]);
		return -1;
	}

	while (1) {
		struct conn_log_struct conn_log;
		int n_read = fread(&conn_log, sizeof(conn_log), 1, f);
		if (n_read == 0)
			usleep(10000);
		else
			fpproto_print_log_struct(&conn_log, stdout);
	}

	return 0;
}


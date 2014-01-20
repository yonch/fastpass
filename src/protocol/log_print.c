
#include <stdio.h>
#include <unistd.h>

#include "stat_print.h"

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
			fpproto_print_log_struct(&conn_log);
	}

	return 0;
}


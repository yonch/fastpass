
#include "admission_core.h"

#include <rte_mempool.h>
#include <rte_errno.h>
#include <rte_string_fns.h>
#include <string.h>

#include "main.h"
#include "admission_log.h"
#include "../graph-algo/admissible_traffic.h"

struct admissible_status g_admissible_status;

struct rte_mempool* admitted_traffic_pool[NB_SOCKETS];

struct admission_log admission_core_logs[RTE_MAX_LCORE];

struct rte_ring *q_head;
struct rte_ring *q_bin[N_ADMISSION_CORES];
struct rte_ring *q_urgent[N_ADMISSION_CORES];

void admission_init_global(struct rte_ring *q_admitted_out)
{
	int i;
	char s[64];
	struct rte_ring *q_head;

	/* init q_head */
	q_head = rte_ring_create("q_head", Q_HEAD_RING_SIZE, 0, 0);
	if (q_head == NULL)
		rte_exit(EXIT_FAILURE,
				"Cannot init q_head: %s\n", rte_strerror(rte_errno));

	init_admissible_status(&g_admissible_status, OVERSUBSCRIBED,
			INTER_RACK_CAPACITY, NUM_NODES, q_head, q_admitted_out);

	/* init log */
	for (i = 0; i < RTE_MAX_LCORE; i++)
		admission_log_init(&admission_core_logs[i]);

	/* init q_bin */
	for (i = 0; i < N_ADMISSION_CORES; i++) {
		rte_snprintf(s, sizeof(s), "q_bin_%d", i);
		q_bin[i] = rte_ring_create(s, 2 * BATCH_SIZE, 0, 0);
		if (q_bin[i] == NULL)
			rte_exit(EXIT_FAILURE,
					"Cannot init q_bin[%d]: %s\n", i, rte_strerror(rte_errno));
	}

	/* init q_urgent */
	for (i = 0; i < N_ADMISSION_CORES; i++) {
		rte_snprintf(s, sizeof(s), "q_urgent_%d", i);
		q_urgent[i] = rte_ring_create(s, URGENT_RING_SIZE, 0, 0);
		if (q_urgent[i] == NULL)
			rte_exit(EXIT_FAILURE,
					"Cannot init q_urgent[%d]: %s\n", i, rte_strerror(rte_errno));
	}

	/* push bins into first q_bin */
	for (i = 0; i < NUM_BINS; i++) {
		struct bin *bin = create_bin(LARGE_BIN_SIZE);
		if (bin == NULL)
			rte_exit(EXIT_FAILURE, "Cannot create bin %d to initialize q_bin[0]", i);

		rte_ring_enqueue(q_bin[0], (void *)bin);
	}

}

void admission_init_core(uint16_t lcore_id)
{
	int pool_index;
	int socketid;
	char s[64];

	socketid = rte_lcore_to_socket_id(lcore_id);

	/* we currently set pool_index to 0 because other cores need to free the memory */
	pool_index = 0;

	if (admitted_traffic_pool[pool_index] == NULL) {
		rte_snprintf(s, sizeof(s), "admitted_traffic_pool_%d", pool_index);
		admitted_traffic_pool[pool_index] =
			rte_mempool_create(s,
				ADMITTED_TRAFFIC_MEMPOOL_SIZE, /* num elements */
				sizeof(struct admitted_traffic), /* element size */
				ADMITTED_TRAFFIC_CACHE_SIZE, /* cache size */
				0, NULL, NULL, NULL, NULL, /* custom initialization, disabled */
				socketid, 0);
		if (admitted_traffic_pool[pool_index] == NULL)
			rte_exit(EXIT_FAILURE,
					"Cannot init admitted traffic pool on socket %d: %s\n", socketid,
					rte_strerror(rte_errno));
		else
			RTE_LOG(INFO, ADMISSION, "Allocated admitted traffic pool on socket %d - %lu bufs\n",
					socketid, (uint64_t)ADMITTED_TRAFFIC_MEMPOOL_SIZE);
	}
}

int exec_admission_core(void *void_cmd_p)
{
	struct admission_core_cmd *cmd = (struct admission_core_cmd *)void_cmd_p;
	struct admission_core_state core;
	/* int traffic_pool_socketid = rte_lcore_to_socket_id(rte_lcore_id()); */
	int traffic_pool_socketid = 0;
	struct admitted_traffic *admitted[BATCH_SIZE];
	int rc;
	uint32_t core_ind = cmd->admission_core_index;
	uint64_t current_timeslot = cmd->start_timeslot;
	uint64_t start_time_first_timeslot;

	/* initialize core */
	rc = alloc_core_init(&core,
			q_bin[core_ind],
			q_bin[(core_ind + 1) % N_ADMISSION_CORES],
			q_urgent[core_ind],
			q_urgent[(core_ind + 1) % N_ADMISSION_CORES]);
	if (rc != 0)
		rte_exit(EXIT_FAILURE, "could not successfully init admission_core_state\n");

	/* if we're first core, we should have the token */
	if (core_ind == 0)
		rte_ring_enqueue(q_urgent[core_ind], (void*)URGENT_Q_HEAD_TOKEN);

	ADMISSION_DEBUG("starting allocations\n");

	/* do allocation loop */
	while (1) {
		/* get admitted_traffic structures */
		rc = rte_mempool_get_bulk(admitted_traffic_pool[traffic_pool_socketid],
				(void **)&admitted[0], BATCH_SIZE);
		if (unlikely(rc != 0)) {
			admission_log_failed_to_allocate_admitted_traffic();
			continue; /* we try again */
		}

		/* decide when the first timeslot should start processing */
		start_time_first_timeslot =
				current_timeslot * cmd->timeslot_len - cmd->prealloc_gap_ns;

		/* perform allocation */
		admission_log_allocation_begin(current_timeslot,
				start_time_first_timeslot);
		get_admissible_traffic(&core, &g_admissible_status, &admitted[0],
				start_time_first_timeslot, cmd->timeslot_len);
		admission_log_allocation_end();

		current_timeslot += BATCH_SIZE * N_ADMISSION_CORES;
	}

	return 0;
}

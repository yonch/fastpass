
#include "seq_admission_core.h"

#include <rte_mempool.h>
#include <rte_errno.h>
#include <rte_string_fns.h>
#include <rte_timer.h>
#include <string.h>

#include "main.h"
#include "admission_core_common.h"
#include "admission_log.h"
#include "../protocol/platform.h"
#include "../graph-algo/admissible.h"
#include "../graph-algo/algo_config.h"

struct rte_mempool* admitted_traffic_pool[NB_SOCKETS];

struct admission_log admission_core_logs[RTE_MAX_LCORE];

struct rte_ring *q_head;
struct rte_ring *q_bin[2 * N_ADMISSION_CORES];

void seq_admission_init_global(struct rte_ring *q_admitted_out)
{
	int i;
	char s[64];
	struct rte_ring *q_head;
	struct rte_mempool *bin_mempool;

	/* init q_head */
	q_head = rte_ring_create("q_head", Q_HEAD_RING_SIZE, 0, 0);
	if (q_head == NULL)
		rte_exit(EXIT_FAILURE,
				"Cannot init q_head: %s\n", rte_strerror(rte_errno));

	/* allocate head_bin_mempool */
	uint32_t pool_index = 0;
	uint32_t socketid = 0;
	rte_snprintf(s, sizeof(s), "bin_pool_%d", pool_index);
	bin_mempool =
		rte_mempool_create(s,
			BIN_MEMPOOL_SIZE, /* num elements */
			bin_num_bytes(SMALL_BIN_SIZE), /* element size */
			BIN_MEMPOOL_CACHE_SIZE, /* cache size */
			0, NULL, NULL, NULL, NULL, /* custom initialization, disabled */
			socketid, 0);
	if (bin_mempool == NULL)
		rte_exit(EXIT_FAILURE,
				"Cannot init bin mempool on socket %d: %s\n", socketid,
				rte_strerror(rte_errno));
	else
		RTE_LOG(INFO, ADMISSION, "Allocated bin mempool on socket %d - %lu bufs\n",
				socketid, (uint64_t)BIN_MEMPOOL_SIZE);

	/* allocate admitted_traffic_pool */
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

	/* init log */
	for (i = 0; i < RTE_MAX_LCORE; i++)
		admission_log_init(&admission_core_logs[i]);

	/* init q_bin */
	for (i = 0; i < 2 * N_ADMISSION_CORES; i++) {
		rte_snprintf(s, sizeof(s), "q_bin_%d", i);
		q_bin[i] = rte_ring_create(s, Q_BIN_RING_SIZE, 0,
									RING_F_SP_ENQ | RING_F_SC_DEQ);
		if (q_bin[i] == NULL)
			rte_exit(EXIT_FAILURE,
					"Cannot init q_bin[%d]: %s\n", i, rte_strerror(rte_errno));
	}

	/* init admissible_status */
	seq_init_admissible_status(&g_seq_admissible_status, OVERSUBSCRIBED,
				   INTER_RACK_CAPACITY, OUT_OF_BOUNDARY_CAPACITY,
				   NUM_NODES, q_head, q_admitted_out, bin_mempool,
				   admitted_traffic_pool[0], &q_bin[0]);

}

void seq_admission_init_core(uint16_t lcore_id)
{
	int pool_index;
	int socketid;
	char s[64];

	socketid = rte_lcore_to_socket_id(lcore_id);

}

int exec_seq_admission_core(void *void_cmd_p)
{
	struct admission_core_cmd *cmd = (struct admission_core_cmd *)void_cmd_p;
	uint32_t core_ind = cmd->admission_core_index;
	uint64_t logical_timeslot = cmd->start_timeslot;
	struct seq_admission_core_state *core = &g_seq_admissible_status.cores[core_ind];
	/* int traffic_pool_socketid = rte_lcore_to_socket_id(rte_lcore_id()); */
	int traffic_pool_socketid = 0;
	int rc;
	uint64_t start_time_first_timeslot;

	/* if we're first core, we should have the token */
	if (core_ind == 0)
		rte_ring_enqueue(q_bin[core_ind], NULL);

	ADMISSION_DEBUG("core %d admission %d starting allocations\n",
			rte_lcore_id(), core_ind);

	/* do allocation loop */
	while (1) {
		/* decide whether to skip timeslots */
		uint64_t actual_timeslot = (fp_get_time_ns() * TIMESLOT_MUL) >> TIMESLOT_SHIFT;
		uint64_t earliest_logical_timeslot = actual_timeslot - ALLOWED_TIMESLOT_LAG;
		while (time_before64((__u64)logical_timeslot, (__u64)earliest_logical_timeslot)) {
			logical_timeslot += BATCH_SIZE * N_ADMISSION_CORES;
			admission_log_skipped_batch();
		}

		/* perform allocation */
		admission_log_allocation_begin(logical_timeslot,
				start_time_first_timeslot);
		seq_get_admissible_traffic(&g_seq_admissible_status, core_ind,
					   logical_timeslot - PREALLOC_DURATION_TIMESLOTS,
					   TIMESLOT_MUL, TIMESLOT_SHIFT);
		admission_log_allocation_end();

		logical_timeslot += BATCH_SIZE * N_ADMISSION_CORES;

		/* manage timers: timer documentation asks for this to run on all cores
		 * there shouldn't be any timers on this core */
		rte_timer_manage();
	}

	return 0;
}

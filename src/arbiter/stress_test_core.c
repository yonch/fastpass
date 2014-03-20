#include "stress_test_core.h"

#include <rte_log.h>
#include <rte_common.h>
#include <rte_cycles.h>
#include <rte_memcpy.h>
#include <rte_string_fns.h>
#include <rte_errno.h>
#include <rte_mempool.h>
#include <ccan/list/list.h>
#include "control.h"
#include "main.h"
#include "../graph-algo/admissible_structures.h"
#include "../graph-algo/admissible_traffic.h"
#include "../graph-algo/generate_requests.h"
#include "admission_core.h"
#include "../protocol/stat_print.h"
#include "../protocol/topology.h"

/* logs */
struct stress_test_log {
	uint64_t processed_tslots;
	uint64_t occupied_node_tslots;
};
struct stress_test_log stress_test_core_logs[RTE_MAX_LCORE];
/* current log */
#define CL		(&stress_test_core_logs[rte_lcore_id()])

static inline void stress_test_log_init(struct stress_test_log *cl)
{
	memset(cl, 0, sizeof(*cl));
}

static inline void stress_test_log_got_admitted_tslot(uint16_t size, uint64_t timeslot) {
	(void)size;(void)timeslot;
	CL->processed_tslots++;
	CL->occupied_node_tslots += size;
}

static void flush_q_head_buffer(struct stress_test_core_state *core)
{
	uint32_t remaining = core->q_head_buf_len;
	void **edge_p = &core->q_head_write_buffer[0];
	int rc;

	while(remaining > 0) {
		rc = rte_ring_enqueue_burst(g_admissible_status.q_head,
				edge_p, remaining);
		if (unlikely(rc < 0))
			rte_exit(EXIT_FAILURE, "got negative value (%d) from rte_ring_enqueue_burst, should never happen\n", rc);
		remaining -= rc;
		edge_p += rc;
	}

	core->q_head_buf_len = 0;
}

static void add_backlog_buffered(struct stress_test_core_state *core,
		struct admissible_status *status, uint16_t src, uint16_t dst,
        uint16_t demand_tslots)
{
	void *edge;

	if (add_backlog_no_enqueue(status, src, dst, demand_tslots, &edge) == true) {
		/* need to add to q_head, will do so through buffer */
		core->q_head_write_buffer[core->q_head_buf_len++] = edge;

		if (unlikely(core->q_head_buf_len == Q_HEAD_WRITE_BUFFER_SIZE)) {
			flush_q_head_buffer(core);
		}
	}
}

static inline void process_allocated_traffic(struct stress_test_core_state *core,
		struct rte_ring *q_admitted)
{
	int rc;
	int i;
	struct admitted_traffic* admitted[MAX_ADMITTED_PER_LOOP];
	uint64_t current_timeslot;

	/* Process newly allocated timeslots */
	rc = rte_ring_dequeue_burst(q_admitted, (void **) &admitted[0],
								MAX_ADMITTED_PER_LOOP);
	if (unlikely(rc < 0)) {
		/* error in dequeuing.. should never happen?? */
		comm_log_dequeue_admitted_failed(rc);
		return;
	}

	for (i = 0; i < rc; i++) {
		current_timeslot = ++core->latest_timeslot;
		stress_test_log_got_admitted_tslot(admitted[i]->size, current_timeslot);
	}
	/* free memory */
	rte_mempool_put_bulk(admitted_traffic_pool[0], (void **) admitted, rc);
}

void exec_stress_test_core(struct stress_test_core_cmd * cmd,
		uint64_t first_time_slot)
{
	int i;
	const unsigned lcore_id = rte_lcore_id();
	uint64_t now;
	struct request_generator gen;
	struct request next_request;
	struct stress_test_core_state core;

	core.latest_timeslot = first_time_slot - 1;
	core.q_head_buf_len = 0;

	stress_test_log_init(&stress_test_core_logs[lcore_id]);

	/* Initialize gen */
	init_request_generator(&gen, cmd->mean_t_btwn_requests, cmd->start_time,
			cmd->num_nodes);

	while (rte_get_timer_cycles() < cmd->start_time);

	/* Generate first request */
	get_next_request(&gen, &next_request);
	now = rte_get_timer_cycles();

	/* MAIN LOOP */
	while (now < cmd->end_time) {
		/* if time to enqueue request, do so now */
		for (i = 0; i < MAX_ENQUEUES_PER_LOOP; i++) {
			if (next_request.time > now)
				break;

			/* enqueue the request */
			add_backlog_buffered(&core, &g_admissible_status,
					next_request.src, next_request.dst, cmd->demand_tslots);

			/* generate the next request */
			get_next_request(&gen, &next_request);
		}

		/* TODO: do something to detect if we're falling behind */

		/* Process newly allocated timeslots */
		process_allocated_traffic(&core, cmd->q_allocated);

		/* flush q_head's buffer into q_head */
		flush_q_head_buffer(&core);

		now = rte_get_timer_cycles();
	}

	/* Dump some stats */
	printf("Stress test finished; %lu processed timeslots, %lu node-tslots\n",
			CL->processed_tslots, CL->occupied_node_tslots);
}

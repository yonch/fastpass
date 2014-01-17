
#include "control.h"

#include <rte_cycles.h>
#include <rte_errno.h>
#include <rte_timer.h>
#include "port_alloc.h"
#include "main.h"
#include "comm_core.h"
#include "admission_core.h"
#include "path_sel_core.h"
#include "log_core.h"

int control_do_queue_allocation(void)
{
	int ret, i, j;

	/* If we don't need network, return */
	if (!(EXPT_RUN_MASK)) {
		return 0;
	}

	if(n_enabled_lcore < N_ADMISSION_CORES + N_COMM_CORES + N_LOG_CORES + N_PATH_SEL_CORES) {
		rte_exit(EXIT_FAILURE, "Need #alloc + #comm + #log + #path_sel cores (need %d, got %d)\n",
				N_ADMISSION_CORES + N_COMM_CORES + N_LOG_CORES + N_PATH_SEL_CORES,
				n_enabled_lcore);
	}

	if(n_enabled_port < N_CONTROLLER_PORTS) {
		rte_exit(EXIT_FAILURE, "Need %d enabled ports, got %d\n",
				N_CONTROLLER_PORTS, n_enabled_port);
	}

	/** TX queues */
	for (i = 0; i < n_enabled_lcore; i++) {
		for (j = 0; j < n_enabled_port; j++) {
			ret = conf_alloc_tx_queue(enabled_lcore[i], enabled_port[j]);
			if (ret != 0)
				return ret;
		}
	}

	/** RX queues */
	for (i = 0; i < N_CONTROLLER_PORTS; i++) {
		/* First half of RX ports go to the controller, enabled lcore 0 */
		ret = conf_alloc_rx_queue(enabled_lcore[0], enabled_port[i]);
		if (ret != 0) {
			return ret;
		}
	}
	return 0;
}

/**
 * Enqueues commands for allocation network experiments
 *
 * @return number of remaining experiments after this one
 */
void launch_cores(void)
{
	/* variables */
	//struct comm_core_cmd comm_cores[RTE_MAX_LCORE];
	uint64_t start_time;
	static uint64_t end_time;
	int i; (void)i;
	struct comm_core_cmd comm_cmd;
	struct admission_core_cmd admission_cmd[N_ADMISSION_CORES];
	struct path_sel_core_cmd path_sel_cmd;
	struct log_core_cmd log_cmd;
	uint64_t first_time_slot;
	uint64_t now;
	struct rte_ring *q_admitted;
	struct rte_ring *q_path_selected;

	rte_timer_subsystem_init();

	benchmark_cost_of_get_time();

	/* decide what the first time slot to be output is */
	now = fp_get_time_ns();
	first_time_slot = ((now + INIT_MAX_TIME_NS) * TIMESLOT_MUL) >> TIMESLOT_SHIFT;
	CONTROL_INFO("now %lu first time slot will be %lu\n", now, first_time_slot);

	/*** LOGGING OUTPUT ***/
#ifdef LOG_TO_STDOUT
	rte_openlog_stream(stdout);
#endif

	/*** GLOBAL INIT ***/
	/* initialize comm core global data */
	comm_init_global_structs(first_time_slot);

	/* create q_admitted_out */
	q_admitted = rte_ring_create("q_admitted",
			2 * ADMITTED_TRAFFIC_MEMPOOL_SIZE, 0, 0);
	if (q_admitted == NULL)
		rte_exit(EXIT_FAILURE,
				"Cannot init q_admitted: %s\n", rte_strerror(rte_errno));

	/* create q_path_selected_out */
	q_path_selected = rte_ring_create("q_path_selected",
			2 * ADMITTED_TRAFFIC_MEMPOOL_SIZE, 0, 0);
	if (q_path_selected == NULL)
		rte_exit(EXIT_FAILURE,
				"Cannot init q_path_selected: %s\n", rte_strerror(rte_errno));

	/* initialize admission core global data */
	admission_init_global(q_admitted);

	// Calculate start and end times
	start_time = rte_get_timer_cycles() + sec_to_hpet(0.2); /* start after last end */
	end_time = start_time + sec_to_hpet(100*1000*1000);

	/*** PATH_SELECTION CORES ***/
	/* set commands */
	path_sel_cmd.q_admitted = q_admitted;
	path_sel_cmd.q_path_selected = q_path_selected;

	/* launch admission core */
	if (N_PATH_SEL_CORES > 0)
		rte_eal_remote_launch(exec_path_sel_core, &path_sel_cmd,
				enabled_lcore[FIRST_PATH_SEL_CORE]);

	/*** ADMISSION CORES ***/
	/* initialize core structures */
	for (i = 0; i < N_ADMISSION_CORES; i++) {
		uint16_t lcore_id = enabled_lcore[FIRST_ADMISSION_CORE + i];
		admission_init_core(lcore_id);
	}

	for (i = 0; i < N_ADMISSION_CORES; i++) {
		uint16_t lcore_id = enabled_lcore[FIRST_ADMISSION_CORE + i];

		/* set commands */
		admission_cmd[i].start_time = start_time;
		admission_cmd[i].end_time = end_time;
		admission_cmd[i].admission_core_index = i;
		admission_cmd[i].start_timeslot = first_time_slot + i * BATCH_SIZE;

		/* launch admission core */
		rte_eal_remote_launch(exec_admission_core, &admission_cmd[i], lcore_id);
	}

	/*** LOG CORE ***/
	log_cmd.log_gap_ticks = (uint64_t)(LOG_GAP_SECS * rte_get_timer_hz());

	/* launch log core */
	rte_eal_remote_launch(exec_log_core, &log_cmd,
			enabled_lcore[FIRST_LOG_CORE]);

	/*** COMM CORES ***/
	// Set commands
	comm_cmd.start_time = start_time;
	comm_cmd.end_time = end_time;
	comm_cmd.q_allocated = ((N_PATH_SEL_CORES > 0) ? q_path_selected : q_admitted);

	/* initialize comm core on this core */
	comm_init_core(rte_lcore_id(), first_time_slot);

	/** Run the controller on this core */
	exec_comm_core(&comm_cmd);

	/** Wait for all cores */
	rte_eal_mp_wait_lcore();

	rte_exit(EXIT_SUCCESS, "Done");
}

/* may be useful later for logging cores */
//	for (i = 0; i < N_CONTROLLER_PORTS; i++) {
//		uint32_t log_core = 1 + i;
//		uint32_t gen_core = 1 + N_CONTROLLER_PORTS + i;
//		uint32_t ctrl_port = i;
//		uint32_t gen_port = N_CONTROLLER_PORTS + i;
//
//		rx_cmds[log_core].start_time = start_time;
//		rx_cmds[log_core].end_time = end_time;
//		/* filename res_grant_TOR_AGG_CORE_PATHS_MEAN-T.csv */
//		snprintf(rx_cmds[log_core].filename, RX_GRANT_MAX_FILENAME_LEN,
//				"results/res_grant_%"PRIu32"_%"PRIu32"_%"PRIu32"_%"PRIu32
//				"_%e_port_%d.csv", tor_n_machines,agg_n_ports,core_n_ports,alloc_n_paths,
//				(double)mean_reqs_per_sec[next_run_index], ctrl_port);
//		rte_eal_remote_launch(exec_rx_grant, &rx_cmds[log_core], log_core);
//
//		rx_cmds[log_core].start_time = start_time;
//		rx_cmds[log_core].end_time = end_time;
//		ether_addr_copy(&port_info[enabled_port[ctrl_port]].eth_addr, &gen_cmds[gen_core].dst_addr);
//		gen_cmds[gen_core].egress_port = enabled_port[gen_port];
//		gen_cmds[gen_core].mean_t_btwn_requests = N_CONTROLLER_PORTS * sec_to_hpet(1.0 / mean_reqs_per_sec[next_run_index]);
//		gen_cmds[gen_core].num_machines = num_machines;
//		rte_eal_remote_launch(exec_traffic_gen, &gen_cmds[gen_core], gen_core);
//
//	}

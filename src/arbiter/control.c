
#include "control.h"

#include <rte_cycles.h>
#include "port_alloc.h"
#include "main.h"
#include "comm_core.h"

int control_do_queue_allocation(void)
{
	int ret, i, j;

	/* If we don't need network, return */
	if (!(EXPT_RUN_MASK)) {
		return 0;
	}

	if(n_enabled_lcore < N_ALLOC_CORES + N_COMM_CORES + N_LOG_CORES) {
		rte_exit(EXIT_FAILURE, "Need #alloc + #comm + #log cores (need %d, got %d)\n",
				N_ALLOC_CORES + N_COMM_CORES + N_LOG_CORES, n_enabled_lcore);
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
void launch_controller_cores(void)
{
	/* variables */
	//struct comm_core_cmd comm_cores[RTE_MAX_LCORE];
	uint64_t start_time;
	static uint64_t end_time;
	int i; (void)i;
	struct comm_core_cmd ctrl_cmd;

	/* initialize comm core global data */
	comm_init_global_structs();

	// Calculate start and end times
	start_time = rte_get_timer_cycles() + sec_to_hpet(0.2); /* start after last end */
	end_time = start_time + sec_to_hpet(1000);

	// Set commands
	ctrl_cmd.start_time = start_time;
	ctrl_cmd.end_time = end_time;

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

	/* initialize comm core on this core */
	comm_init_core(rte_lcore_id());

	/** Run the controller on this core */
	exec_comm_core(&ctrl_cmd);

	/** Wait for all cores */
	rte_eal_mp_wait_lcore();

	rte_exit(EXIT_SUCCESS, "Done");
}


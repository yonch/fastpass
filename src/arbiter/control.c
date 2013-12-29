
#include "control.h"

#include <rte_cycles.h>
#include "port_alloc.h"
#include "main.h"
#include "traffic_gen.h"
#include "cpu_benchmark.h"
#include "controller.h"
#include "rx_grant.h"
#include "node.h"

int control_do_queue_allocation(void)
{
	int ret, i, j;

	/* If we don't need network, return */
	if (!(EXPT_RUN_MASK & (END_TO_END_EXPT | NET_BENCHMARK_EXPT))) {
		return 0;
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
	if(n_enabled_lcore < n_enabled_port - N_CONTROLLER_PORTS + 1) {
		rte_exit(EXIT_FAILURE, "Need #ports - #controller_ports + 1 cores (need %d, got %d)\n",
				n_enabled_port - N_CONTROLLER_PORTS + 1, n_enabled_lcore);
	}

	for (i = 0; i < N_CONTROLLER_PORTS; i++) {
		/* First half of RX ports go to the controller, enabled lcore 0 */
		ret = conf_alloc_rx_queue(enabled_lcore[0], enabled_port[i]);
		if (ret != 0) {
			return ret;
		}
	}

	for (i = 0; i < n_enabled_port - N_CONTROLLER_PORTS; i++) {
		/* Other ports go to logging cores, cores 1..n_controller_ports */
		ret = conf_alloc_rx_queue(enabled_lcore[i+1], enabled_port[i+N_CONTROLLER_PORTS]);
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
static void allocator_network_experiment(void) {
	/* run parameters */
	const double duration_sec = 1.000;
	static int next_run_index = 0;
	const uint64_t mean_reqs_per_sec[] = {2.4e6, 1e6, 1.5e6, 2e6, 2.5e6, 3e6, 3.5e6, 4e6, 5e6, 7e6, 10e6};
	const int num_runs = 1;
	const uint32_t tor_n_machines = 8;
	const uint32_t agg_n_ports = 2*tor_n_machines;
	const uint32_t core_n_ports = 4;
	const uint32_t num_machines = tor_n_machines * agg_n_ports / 2 * core_n_ports;
	const uint32_t alloc_n_paths = 4;
	const uint32_t MTU_SIZE_BYTES = 1542;
	const uint64_t MTU_N_CYCLES = sec_to_hpet(MTU_SIZE_BYTES * 8 / 1.0e9);

	/* variables */
	struct expt_traffic_gen gen_cmds[RTE_MAX_LCORE];
	struct expt_rx_grant_cmd rx_cmds[RTE_MAX_LCORE];
	uint64_t start_time;
	static uint64_t end_time;
	int i;
	struct expt_controller ctrl_cmd;

	for (next_run_index = 0; next_run_index < num_runs; next_run_index++) {

		// Calculate start and end times
		start_time = rte_get_timer_cycles() + sec_to_hpet(0.2); /* start after last end */
		end_time = start_time + sec_to_hpet(duration_sec);

		// Set commands
		ctrl_cmd.start_time = start_time;
		ctrl_cmd.end_time = end_time;
		ctrl_cmd.tor_n_machines = tor_n_machines;
		ctrl_cmd.agg_n_ports = agg_n_ports;
		ctrl_cmd.core_n_ports = core_n_ports;
		ctrl_cmd.alloc_n_paths = alloc_n_paths;
		ctrl_cmd.tslot_len = MTU_N_CYCLES;
		ctrl_cmd.tslot_offset = 0;

		for (i = 0; i < N_CONTROLLER_PORTS; i++) {
			uint32_t log_core = 1 + i;
			uint32_t gen_core = 1 + N_CONTROLLER_PORTS + i;
			uint32_t ctrl_port = i;
			uint32_t gen_port = N_CONTROLLER_PORTS + i;

			rx_cmds[log_core].start_time = start_time;
			rx_cmds[log_core].end_time = end_time;
			/* filename res_grant_TOR_AGG_CORE_PATHS_MEAN-T.csv */
			snprintf(rx_cmds[log_core].filename, RX_GRANT_MAX_FILENAME_LEN,
					"results/res_grant_%"PRIu32"_%"PRIu32"_%"PRIu32"_%"PRIu32
					"_%e_port_%d.csv", tor_n_machines,agg_n_ports,core_n_ports,alloc_n_paths,
					(double)mean_reqs_per_sec[next_run_index], ctrl_port);
			rte_eal_remote_launch(exec_rx_grant, &rx_cmds[log_core], log_core);

			rx_cmds[log_core].start_time = start_time;
			rx_cmds[log_core].end_time = end_time;
			ether_addr_copy(&port_info[enabled_port[ctrl_port]].eth_addr, &gen_cmds[gen_core].dst_addr);
			gen_cmds[gen_core].egress_port = enabled_port[gen_port];
			gen_cmds[gen_core].mean_t_btwn_requests = N_CONTROLLER_PORTS * sec_to_hpet(1.0 / mean_reqs_per_sec[next_run_index]);
			gen_cmds[gen_core].num_machines = num_machines;
			rte_eal_remote_launch(exec_traffic_gen, &gen_cmds[gen_core], gen_core);

		}

		/** Run the controller on this core */
		exec_controller(&ctrl_cmd);

		/** Wait for all cores */
		rte_eal_mp_wait_lcore();
	}
}

/**
 * Enqueues commands for allocation network experiments
 *
 * @return number of remaining experiments after this one
 */
static void end_to_end_experiment(void) {
	/* run parameters */
	const double duration_sec = 10.000;
	static int next_run_index = 0;
	const uint64_t mean_reqs_per_sec[] = {600,800,1000,1200,1400,1600,1800,2000,2200,2400,2600,2800,3000,3200,3400};
	const int num_runs = 15;
	struct ether_addr switch_mac = { //78:19:f7:97:db:81
			.addr_bytes = {0x78, 0x19, 0xf7, 0x97, 0xdb, 0x81}};
	const uint32_t tor_n_machines = 2;
	const uint32_t agg_n_ports = 2*tor_n_machines;
	const uint32_t core_n_ports = 2;
	const uint32_t alloc_n_paths = -1;
	const uint32_t MTU_SIZE_BYTES = 1542;
	const uint64_t MTU_N_CYCLES = sec_to_hpet(MTU_SIZE_BYTES * 8 / 1.0e9);
	const uint64_t tslot_duration = 1.06 * MTU_N_CYCLES;

	/* variables */
	struct expt_node node_cmds[RTE_MAX_LCORE];
	uint64_t start_time;
	static uint64_t end_time;
	int i;

	struct expt_controller ctrl_cmd;

	for (next_run_index = 0; next_run_index < num_runs; next_run_index++) {

		// Calculate start and end times
		start_time = rte_get_timer_cycles() + sec_to_hpet(0.2);
		end_time = start_time + sec_to_hpet(duration_sec);

		// Set commands
		ctrl_cmd.start_time = start_time;
		ctrl_cmd.end_time = end_time;
		ctrl_cmd.tor_n_machines = tor_n_machines;
		ctrl_cmd.agg_n_ports = agg_n_ports;
		ctrl_cmd.core_n_ports = core_n_ports;
		ctrl_cmd.alloc_n_paths = alloc_n_paths;
		ctrl_cmd.tslot_len = tslot_duration;
		ctrl_cmd.tslot_offset = 10;

		for (i = 0; i < n_enabled_port - N_CONTROLLER_PORTS; i++) {
			uint32_t node_core = 1 + i;
			uint32_t node_port = N_CONTROLLER_PORTS + i;
			struct expt_node *node = &node_cmds[i];

			node->start_time = start_time;
			node->end_time = end_time;
			node->tslot_len = tslot_duration;
			node->mean_t_btwn_requests = sec_to_hpet(1.0 / mean_reqs_per_sec[next_run_index]);
			node->egress_port = enabled_port[node_port];
			node->src = i;
			ether_addr_copy(&port_info[enabled_port[node_port]].eth_addr, &node->egress_addr);
			ether_addr_copy(&switch_mac, &node->gw_addr);
			node->timeout = tslot_duration * 1000;
			node->retry_interval = tslot_duration * 40;
			node->hist_interval = sec_to_hpet(5e-6);
			/* filename: node_<node_id>_<mean_req_per_sec>.csv */
			snprintf(node->filename, NODE_MAX_FILENAME_LEN,
					"results/node_%u_%lu.csv", i, mean_reqs_per_sec[next_run_index]);

			rte_eal_remote_launch(exec_node, node, enabled_lcore[node_core]);
		}

		/** Run the controller on this core */
		exec_controller(&ctrl_cmd);

		/** Wait for all cores */
		rte_eal_mp_wait_lcore();
	}
}



void control_do_experiments(void)
{
	if (EXPT_RUN_MASK & CPU_BENCHMARK_EXPT)
		heuristic_benchmark_experiment();

	if (EXPT_RUN_MASK & NET_BENCHMARK_EXPT)
		allocator_network_experiment();

	if (EXPT_RUN_MASK & END_TO_END_EXPT)
		end_to_end_experiment();

	if (EXPT_RUN_MASK & (END_TO_END_EXPT | NET_BENCHMARK_EXPT))
		// Print XON/XOFF statistics for enabled ports
		print_xon_xoff_statistics();

	// Wait a bit for all lcores to finish
	rte_exit(EXIT_SUCCESS, "Done");
}


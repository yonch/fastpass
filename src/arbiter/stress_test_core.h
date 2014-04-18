
#ifndef STRESS_TEST_CORE_H_
#define STRESS_TEST_CORE_H_

#include <stdint.h>
#include <rte_ip.h>
#include "../graph-algo/admissible_structures.h"

#define MAX_ENQUEUES_PER_LOOP		1024
#define MAX_ADMITTED_PER_LOOP		(4*BATCH_SIZE)

/* The buffer size when writing to q_head */
#define Q_HEAD_WRITE_BUFFER_SIZE		(32*1024)

/**
 * Specifications for controller thread
 */
struct stress_test_core_cmd {
	uint64_t start_time;
	uint64_t end_time;

	double mean_t_btwn_requests;
	uint32_t num_nodes;
	uint32_t demand_tslots;

	uint32_t num_initial_srcs;
	uint32_t num_initial_dsts_per_src;
	uint32_t initial_flow_size;

	struct rte_ring *q_allocated;
};

//struct stress_test_core_state {
//	uint64_t latest_timeslot;
//
//	void *q_head_write_buffer[Q_HEAD_WRITE_BUFFER_SIZE];
//	uint32_t q_head_buf_len;
//};

void exec_stress_test_core(struct stress_test_core_cmd * cmd,
		uint64_t first_time_slot);


#endif /* STRESS_TEST_CORE_H_ */

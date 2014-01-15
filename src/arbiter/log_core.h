/*
 * log_core.h
 *
 *  Created on: Jan 6, 2014
 *      Author: yonch
 */

#ifndef LOG_CORE_H_
#define LOG_CORE_H_

/* Specifications for controller thread */
struct log_core_cmd {
	uint64_t start_time;
	uint64_t end_time;

	uint64_t log_gap_ticks;
};

/**
 * Runs the log core
 */
int exec_log_core(void *void_cmd_p);

#endif /* LOG_CORE_H_ */

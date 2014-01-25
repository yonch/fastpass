/*
 * admissible_algo_log.h
 *
 *  Created on: Jan 24, 2014
 *      Author: yonch
 */

#ifndef ADMISSIBLE_ALGO_LOG_H_
#define ADMISSIBLE_ALGO_LOG_H_

struct admission_core_statistics {
	uint64_t no_available_timeslots_for_bin_entry;
};

static inline
void adm_algo_log_no_available_timeslots_for_bin_entry(
		struct admission_core_state *core,
		uint16_t src, uint16_t dst)
{
	(void)src;(void)dst;
	core->stat.no_available_timeslots_for_bin_entry++;
}


#endif /* ADMISSIBLE_ALGO_LOG_H_ */

/*
 * path_sel_core.h
 *
 *  Created on: Jan 5, 2014
 *      Author: yonch
 */

#ifndef PATH_SEL_CORE_H_
#define PATH_SEL_CORE_H_

/* Specifications for path selection core thread */
struct path_sel_core_cmd {
	struct rte_ring *q_admitted;
	struct rte_ring *q_path_selected;
};

int exec_path_sel_core(void *void_cmd_p);

#endif /* PATH_SEL_CORE_H_ */

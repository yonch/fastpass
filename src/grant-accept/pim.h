/*
 * pim.h
 *
 *  Created on: Apr 27, 2014
 *      Author: yonch
 */

#ifndef PIM_H_
#define PIM_H_

#include "grant-accept.h"
#include "edgelist.h"

/**
 * A structure for the state of a grant partition
 */
struct pim_state {
        struct ga_adj requests_by_src[N_PARTITIONS];
	struct ga_partd_edgelist grants;
	struct ga_adj grants_by_dst[N_PARTITIONS];
	struct ga_partd_edgelist accepts;
};

/**
 * For all source (left-hand) nodes in partition 'partition_index',
 *    selects edges to grant. These are added to 'grants'.
 */
void pim_do_grant(struct pim_state *state, uint16_t partition_index);

/**
 * For every destination (right-hand) node in partition 'partition_index',
 *    select among its granted edges which edge to accept. These edges are
 *    added to 'accepts'
 */
void pim_do_accept(struct pim_state *state, uint16_t partition_index);

void pim_process_accepts(struct pim_state *state, uint16_t partition_index);

void pim_add_backlog(struct pim_state *state, uint16_t src, uint16_t dst);

#endif /* PIM_H_ */

/*
 * pim.h
 *
 *  Created on: Apr 27, 2014
 *      Author: yonch
 */

#ifndef PIM_H_
#define PIM_H_

#include "grant-accept.h"
#include "partitioned-edgelist.h"

/**
 * A structure for the state of a grant partition
 */
struct pim_grant_partn_state {

};

/**
 * For all destination (right-hand) nodes in partition 'partition_index',
 *    selects edges to grant. These are added to 'grants'.
 */
void pim_do_grant(struct ga_adj *requests, uint16_t partition_index,
		struct ga_partd_edgelist *grants);

/**
 * For every source (left-hand) node in partition 'partition_index', select
 *    among its granted edges which edge to accept. These edges are added to
 *    'accepts'
 */
void pim_do_accept(struct ga_partd_edgelist *grants, uint16_t partition_index,
		struct ga_adj *tmp_adj, struct ga_partd_edgelist *accepts);

void pim_process_accepts(struct ga_partd_edgelist *accepts,
		struct ga_adj *requests);


#endif /* PIM_H_ */

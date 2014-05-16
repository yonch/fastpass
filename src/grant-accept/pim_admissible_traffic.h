/*
 * pim_admissible_traffic.h
 *
 *  Created on: May 9, 2014
 *      Author: aousterh
 */

#include "pim.h"
#include "../graph-algo/fp_ring.h"
#include "../graph-algo/platform.h"

/**
 * Determine admissible traffic for one timeslot
 */
void pim_get_admissible_traffic(struct pim_state *state);

/**
 * Check that the admitted edges are admissible, returns true if admissible,
 * or false otherwise
 */
bool pim_is_valid_admitted_traffic(struct pim_state *state);

/**
 * Returns an initialized struct pim state, or NULL on error
 */
static inline
struct pim_state *pim_create_state(struct fp_ring **q_new_demands,
                                   struct fp_ring *q_admitted_out,
                                   struct fp_mempool *bin_mempool,
                                   struct fp_mempool *admitted_traffic_mempool)
{
        struct pim_state *state = fp_malloc("pim_state", sizeof(struct pim_state));
        if (state == NULL)
                return NULL;

        pim_init_state(state, q_new_demands, q_admitted_out, bin_mempool,
                       admitted_traffic_mempool);

        return state;
}

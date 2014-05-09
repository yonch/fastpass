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
 * Increase the backlog from src to dst
 */
void add_backlog(struct pim_state *state, uint16_t src, uint16_t dst,
                 uint32_t amount);

/**
 * Determine admissible traffic for one timeslot
 */
void get_admissible_traffic(struct pim_state *state);

/**
 * Check that the admitted edges are admissible, returns true if admissible,
 * or false otherwise
 */
bool valid_admitted_traffic(struct pim_state *state);

/**
 * Returns an initialized struct pim state, or NULL on error
 */
static inline
struct pim_state *pim_create_state(bool a, uint16_t b, uint16_t c, uint16_t d,
                                   struct fp_ring *e, struct fp_ring *q_admitted_out,
                                   struct fp_mempool *f,
                                   struct fp_mempool *admitted_traffic_mempool,
                                   struct fp_ring **g)
{
        struct pim_state *state = fp_malloc("pim_state", sizeof(struct pim_state));
        if (state == NULL)
                return NULL;

        pim_init_state(state, q_admitted_out, admitted_traffic_mempool);

        return state;
}

/*
 * pim_admissible_traffic.h
 *
 *  Created on: May 9, 2014
 *      Author: aousterh
 */

#include "pim.h"

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

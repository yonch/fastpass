/*
 * pim_test.c
 *
 *  Created on: May 13, 2014
 *      Author: aousterh
 */

#include "pim.h"
#include "pim_admissible_traffic.h"

#include <time.h> /* for seeding srand */

#define ADMITTED_TRAFFIC_MEMPOOL_SIZE           (51*1000)
#define ADMITTED_OUT_RING_LOG_SIZE		16

/**
 * Simple test of pim for a few timeslots
 */
int main() {
        /* initialize rand */
        srand(time(NULL));

        /* initialize state */
        struct fp_ring *q_admitted_out;
        struct fp_mempool *admitted_traffic_mempool;
        q_admitted_out = fp_ring_create(ADMITTED_OUT_RING_LOG_SIZE);
        admitted_traffic_mempool = fp_mempool_create(ADMITTED_TRAFFIC_MEMPOOL_SIZE,
                                                     sizeof(struct admitted_traffic));
        struct pim_state *state = pim_create_state(q_admitted_out, admitted_traffic_mempool);

        /* add some test edges */
        struct ga_edge test_edges[] = {{1, 3}, {4, 5}, {1, 5}};
        uint8_t i;
        for (i = 0; i < sizeof(test_edges) / sizeof(struct ga_edge); i++) {
                uint16_t src = test_edges[i].src;
                uint16_t dst = test_edges[i].dst;
                pim_add_backlog(state, src, dst, 0x2UL);
        }

        uint8_t NUM_TIMESLOTS = 3;
        for (i = 0; i < NUM_TIMESLOTS; i++) {
                pim_get_admissible_traffic(state);

                if (!pim_is_valid_admitted_traffic(state))
                        printf("invalid admitted traffic\n");
        }
}

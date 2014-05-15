/*
 * pim_admissible_traffic.c
 *
 *  Created on: May 5, 2014
 *      Author: aousterh
 */

#include "pim.h"
#include "pim_admissible_traffic.h"

#define NUM_ITERATIONS 3
#define UNALLOCATED 0
#define ALLOCATED   1

/**
 * Determine admissible traffic for one timeslot
 */
void pim_get_admissible_traffic(struct pim_state *state)
{
        /* reset per-timeslot state */
        uint16_t partition;
        for (partition = 0; partition < N_PARTITIONS; partition++)
                pim_prepare(state, partition);

        /* run multiple iterations of pim and print out accepted edges */
        /* TODO: add multiple cores and synchronization between them */
        uint8_t i;
        for (i = 0; i < NUM_ITERATIONS; i++) {
                for (partition = 0; partition < N_PARTITIONS; partition++)
                        pim_do_grant(state, partition);
                for (partition = 0; partition < N_PARTITIONS; partition++)
                        pim_do_accept(state, partition);
        }
        for (partition = 0; partition < N_PARTITIONS; partition++)
                pim_process_accepts(state, partition);
}

/**
 * Check that the admitted edges are admissible, returns true if admissible,
 * or false otherwise
 */
bool pim_is_valid_admitted_traffic(struct pim_state *state)
{
        uint8_t src_status[MAX_NODES];
        uint8_t dst_status[MAX_NODES];
        memset(&src_status, UNALLOCATED, MAX_NODES);
        memset(&dst_status, UNALLOCATED, MAX_NODES);

        printf("PIM finished. Accepted edges:\n");
        uint16_t partition;
        struct admitted_traffic *admitted;
        for (partition = 0; partition < N_PARTITIONS; partition++) {
                fp_ring_dequeue(state->q_admitted_out, (void **) &admitted);

                uint16_t j;
                for (j = 0; j < admitted->size; j++) {
                        struct admitted_edge *edge = get_admitted_edge(admitted, j);
                        printf("accepted edge: %d %d\n", edge->src, edge->dst);
                        if (src_status[edge->src] == ALLOCATED)
                                return false;
                        if (dst_status[edge->dst] == ALLOCATED)
                                return false;

                        src_status[edge->src] = ALLOCATED;
                        dst_status[edge->dst] = ALLOCATED;
                }

                fp_ring_enqueue(state->q_admitted_out, (void *) admitted);
        }
        return true;
}

/*
 * pim_admissible_traffic.c
 *
 *  Created on: May 5, 2014
 *      Author: aousterh
 */

#include "pim.h"

#include <time.h> /* for seeding srand */

#define NUM_ITERATIONS 3

/* Increase the backlog from src to dst */
void add_backlog(struct pim_state *state, uint16_t src, uint16_t dst,
                 uint32_t amount)
{
        /* add to state->new_demands for the src partition 
           repurpose the 'metric' field for the amount */
        uint16_t partition_index = PARTITION_OF(src);
        enqueue_bin(state->new_demands[partition_index], src, dst, amount);
}

/* Determine admissible traffic for one timeslot */
void get_admissible_traffic(struct pim_state *state)
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

/* check that the admitted edges are admissible, returns true if admissible,
   or false otherwise */
bool valid_admitted_traffic(struct pim_state *state)
{
        uint8_t src_status[MAX_NODES];
        uint8_t dst_status[MAX_NODES];
        memset(&src_status, UNALLOCATED, MAX_NODES);
        memset(&dst_status, UNALLOCATED, MAX_NODES);

        printf("PIM finished. Accepted edges:\n");
        uint16_t partition;
        for (partition = 0; partition < N_PARTITIONS; partition++) {
                struct admitted_traffic *admitted = state->admitted[partition];
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
        }
        return true;
}

/* simple test of pim for a few timeslots */
int main() {
        /* initialize rand */
        srand(time(NULL));

        /* initialize state to all zeroes */
        struct pim_state state;
        pim_init_state(&state);

        /* add some test edges */
        struct ga_edge test_edges[] = {{1, 3}, {4, 5}, {1, 5}};
        uint8_t i;
        for (i = 0; i < sizeof(test_edges) / sizeof(struct ga_edge); i++) {
                uint16_t src = test_edges[i].src;
                uint16_t dst = test_edges[i].dst;
                add_backlog(&state, src, dst, 0x2UL);
        }

        uint8_t NUM_TIMESLOTS = 3;
        for (i = 0; i < NUM_TIMESLOTS; i++) {
                get_admissible_traffic(&state);

                if (!valid_admitted_traffic(&state))
                        printf("invalid admitted traffic\n");
        }
}

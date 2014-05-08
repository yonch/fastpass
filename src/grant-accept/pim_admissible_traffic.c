/*
 * pim_admissible_traffic.c
 *
 *  Created on: May 5, 2014
 *      Author: aousterh
 */

#include "pim.h"

#include <time.h> /* for seeding srand */

/* Determine admissible traffic for one timeslot */
void get_admissible_traffic(struct pim_state *state)
{
        /* reset per-timeslot state */
        uint16_t src_partition;
        for (src_partition = 0; src_partition < N_PARTITIONS; src_partition++)
                ga_partd_edgelist_src_reset(&state->accepts, src_partition);
        memset(&state->src_status, 0, sizeof(state->src_status));
        memset(&state->dst_status, 0, sizeof(state->dst_status));

        /* run multiple iterations of pim and print out accepted edges */
        /* TODO: add multiple cores and synchronization between them */
        uint8_t NUM_ITERATIONS = 3;
        uint16_t partition;
        uint8_t i;
        for (i = 0; i < NUM_ITERATIONS; i++) {
                for (partition = 0; partition < N_PARTITIONS; partition++)
                        pim_do_grant(state, partition);
                for (partition = 0; partition < N_PARTITIONS; partition++)
                        pim_do_accept(state, partition);
        }
        printf("PIM finished. Accepted edges:\n");
        for (partition = 0; partition < N_PARTITIONS; partition++)
                pim_process_accepts(state, partition);
}

/* simple test of pim for a single timeslot */
int main() {
        /* initialize rand */
        srand(time(NULL));

        /* initialize state to all zeroes */
        struct pim_state state;
        uint16_t src_partition;
        for (src_partition = 0; src_partition < N_PARTITIONS; src_partition++) {
                ga_reset_adj(&state.requests_by_src[src_partition]);
        }

        /* add some test edges */
        struct ga_edge test_edges[] = {{1, 3}, {4, 5}, {1, 5}};
        uint8_t i;
        for (i = 0; i < sizeof(test_edges) / sizeof(struct ga_edge); i++) {
                uint16_t src = test_edges[i].src;
                uint16_t dst = test_edges[i].dst;
                ga_adj_add_edge_by_src(&state.requests_by_src[PARTITION_OF(src)],
                                       PARTITION_IDX(src), dst);
        }

        get_admissible_traffic(&state);
}

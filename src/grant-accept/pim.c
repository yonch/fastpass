/*
 * pim.c
 *
 *  Created on: May 2, 2014
 *      Author: aousterh
 */

#include "pim.h"

/**
 * For all source (left-hand) nodes in partition 'partition_index',
 *    selects edges to grant. These are added to 'grants'.
 */
void pim_do_grant(struct pim_state *state, uint16_t partition_index) {
        uint16_t src;
        
        /* for each src in the partition, randomly choose a dst to grant to */
        for (src = FIRST_IN_PART(partition_index);
             src <= LAST_IN_PART(partition_index);
             src++) {
                uint16_t degree = state->requests_by_src[partition_index].degree[src];
                if (degree == 0)
                        continue; /* no requests for this src */

                uint16_t dst_index = rand() % degree;
                uint16_t dst = state->requests_by_src[partition_index].neigh[src][dst_index];
                ga_partd_edgelist_add(&state->grants, src, dst);
        }
}

/**
 * For every destination (right-hand) node in partition 'partition_index',
 *    select among its granted edges which edge to accept. These edges are
 *    added to 'accepts'
 */
void pim_do_accept(struct pim_state *state, uint16_t partition_index) {
        /* sort grants from all src partitions by destination node */
        struct ga_adj *dest_adj = &state->grants_by_dst[partition_index];
        ga_edgelist_to_adj_by_dst(&state->grants, partition_index, dest_adj);

        /* for each dst in the partition, randomly choose a src to accept */
        uint16_t dst;
        for (dst = FIRST_IN_PART(partition_index);
             dst <= LAST_IN_PART(partition_index);
             dst++) {
                uint16_t degree = state->grants_by_dst[partition_index].degree[dst];
                if (degree == 0)
                        continue; /* no grants for this dst */

                uint16_t src_index = rand() % degree;
                uint16_t src = state->grants_by_dst[partition_index].neigh[dst][src_index];
                ga_partd_edgelist_add(&state->accepts, src, dst);
        }
}

void pim_process_accepts(struct pim_state *state, uint16_t partition_index) {
        uint16_t dst_partition;

        /* print out all accepted edges */
        for (dst_partition = 0; dst_partition < N_PARTITIONS; dst_partition++) {
                struct ga_edgelist *edgelist;
                edgelist = &state->accepts.dst[dst_partition].src[partition_index];

                uint16_t i;
                for (i = 0; i < edgelist->n; i++) {
                        struct ga_edge *edge = &edgelist->edge[i];
                        printf("accepted edge: %d %d\n", edge->src, edge->dst);
                }
        }
}

/* simple test of pim */
int main() {
        uint16_t src_partition;

        /* initialize state to all zeroes */
        struct pim_state state;
        for (src_partition = 0; src_partition < N_PARTITIONS; src_partition++) {
                ga_reset_adj(&state.requests_by_src[src_partition]);
                ga_partd_edgelist_src_reset(&state.grants, src_partition);
                ga_reset_adj(&state.grants_by_dst[src_partition]);
                ga_partd_edgelist_src_reset(&state.accepts, src_partition);
        }

        /* add a test edge */
        uint16_t test_src, test_dst;
        test_src = 1;
        test_dst = 3;
        ga_adj_add_edge_by_src(&state.requests_by_src[0], test_src, test_dst);

        /* run pim and print out accepted edges */
        pim_do_grant(&state, 0);
        pim_do_accept(&state, 0);
        pim_process_accepts(&state, 0);
}

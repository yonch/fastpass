/*
 * pim.c
 *
 *  Created on: May 2, 2014
 *      Author: aousterh
 */

#include "pim.h"

#include <time.h> /* for seeding srand */

/**
 * For all source (left-hand) nodes in partition 'partition_index',
 *    selects edges to grant. These are added to 'grants'.
 */
void pim_do_grant(struct pim_state *state, uint16_t partition_index) {
        /* reset grant edgelist */
        ga_partd_edgelist_src_reset(&state->grants, partition_index);

        /* for each src in the partition, randomly choose a dst to grant to */
        uint16_t src;
        for (src = first_in_partition(partition_index);
             src <= last_in_partition(partition_index);
             src++) {
                uint16_t src_index = PARTITION_IDX(src);
                uint16_t degree = state->requests_by_src[partition_index].degree[src_index];
                if (degree == 0)
                        continue; /* no requests for this src */

                uint16_t dst_index = rand() % degree;
                uint16_t dst = state->requests_by_src[partition_index].neigh[src_index][dst_index];
                ga_partd_edgelist_add(&state->grants, src, dst);
        }
}

/**
 * For every destination (right-hand) node in partition 'partition_index',
 *    select among its granted edges which edge to accept. These edges are
 *    added to 'accepts'
 */
void pim_do_accept(struct pim_state *state, uint16_t partition_index) {
        /* reset grant adjacency list */
        ga_reset_adj(&state->grants_by_dst[partition_index]);

        /* sort grants from all src partitions by destination node */
        struct ga_adj *dest_adj = &state->grants_by_dst[partition_index];
        ga_edgelist_to_adj_by_dst(&state->grants, partition_index, dest_adj);

        /* reset accept edgelist */
        ga_partd_edgelist_src_reset(&state->accepts, partition_index);

        /* for each dst in the partition, randomly choose a src to accept */
        uint16_t dst;
        for (dst = first_in_partition(partition_index);
             dst <= last_in_partition(partition_index);
             dst++) {
                uint16_t dst_index = PARTITION_IDX(dst);
                uint16_t degree = state->grants_by_dst[partition_index].degree[dst_index];
                if (degree == 0)
                        continue; /* no grants for this dst */

                uint16_t src_index = rand() % degree;
                uint16_t src = state->grants_by_dst[partition_index].neigh[dst_index][src_index];
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

        /* run one iteration of pim and print out accepted edges */
        uint16_t partition;
        for (partition = 0; partition < N_PARTITIONS; partition++)
                pim_do_grant(&state, partition);
        for (partition = 0; partition < N_PARTITIONS; partition++)
                pim_do_accept(&state, partition);
        for (partition = 0; partition < N_PARTITIONS; partition++)
                pim_process_accepts(&state, partition);
}

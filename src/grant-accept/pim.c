/*
 * pim.c
 *
 *  Created on: May 2, 2014
 *      Author: aousterh
 */

#include "pim.h"

#include <time.h> /* for seeding srand */

#define MAX_TRIES 10

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
                if (state->src_status[src] == ALLOCATED)
                        continue; /* this src has been allocated in this timeslot */

                uint16_t src_index = PARTITION_IDX(src);
                uint16_t degree = state->requests_by_src[partition_index].degree[src_index];
                if (degree == 0)
                        continue; /* no requests for this src */

                /* find an un-allocated destination to grant to */
                uint8_t tries = MAX_TRIES;
                uint16_t dst_adj_index, dst;
                do {
                        dst_adj_index = rand() % degree;
                        dst = state->requests_by_src[partition_index].neigh[src_index][dst_adj_index];
                } while ((state->dst_status[dst] == ALLOCATED) && (--tries > 0));

                if (state->dst_status[dst] == ALLOCATED)
                        continue; /* couldn't find a free dst*/

                /* add the granted edge */
                ga_partd_edgelist_add(&state->grants, src, dst);

                /* record the index of the destination we granted to */
                state->grant_adj_index[src] = dst_adj_index;
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

        /* for each dst in the partition, randomly choose a src to accept */
        uint16_t dst;
        for (dst = first_in_partition(partition_index);
             dst <= last_in_partition(partition_index);
             dst++) {
                uint16_t dst_index = PARTITION_IDX(dst);
                uint16_t degree = state->grants_by_dst[partition_index].degree[dst_index];
                if (degree == 0)
                        continue; /* no grants for this dst */

                /* choose an edge and accept it */
                uint16_t src_adj_index = rand() % degree;
                uint16_t src = state->grants_by_dst[partition_index].neigh[dst_index][src_adj_index];
                ga_partd_edgelist_add(&state->accepts, src, dst);

                /* mark the src and dst as allocated for this timeslot */
                state->src_status[src] = ALLOCATED; /* TODO: this write might cause cache contention */
                state->dst_status[dst] = ALLOCATED;
        }
}

void pim_process_accepts(struct pim_state *state, uint16_t partition_index) {
        uint16_t dst_partition;

        /* iterate through all accepted edges */
        for (dst_partition = 0; dst_partition < N_PARTITIONS; dst_partition++) {
                struct ga_edgelist *edgelist;
                edgelist = &state->accepts.dst[dst_partition].src[partition_index];

                uint16_t i;
                for (i = 0; i < edgelist->n; i++) {
                        struct ga_edge *edge = &edgelist->edge[i];

                        /* print out the edge */
                        printf("accepted edge: %d %d\n", edge->src, edge->dst);
                        
                        /* delete the edge from requests, to prepare for the next timeslot */
                        /* TODO: check if there is remaining pending demand */
                        uint16_t grant_adj_index = state->grant_adj_index[edge->src];
                        ga_adj_delete_neigh(&state->requests_by_src[PARTITION_OF(edge->src)],
                                            PARTITION_IDX(edge->src), grant_adj_index);
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
                ga_partd_edgelist_src_reset(&state.accepts, src_partition);
                memset(&state.src_status, 0, sizeof(state.src_status));
                memset(&state.dst_status, 0, sizeof(state.dst_status));
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

        /* run multiple iterations of pim and print out accepted edges */
        uint8_t NUM_ITERATIONS = 3;
        uint16_t partition;
        for (i = 0; i < NUM_ITERATIONS; i++) {
                for (partition = 0; partition < N_PARTITIONS; partition++)
                        pim_do_grant(&state, partition);
                for (partition = 0; partition < N_PARTITIONS; partition++)
                        pim_do_accept(&state, partition);
        }
        printf("PIM finished. Accepted edges:\n");
        for (partition = 0; partition < N_PARTITIONS; partition++)
                pim_process_accepts(&state, partition);
}

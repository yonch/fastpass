/*
 * python_helpers.h
 *
 *  Created on: May 1, 2014
 *      Author: yonch
 */

#ifndef PYTHON_HELPERS_H_
#define PYTHON_HELPERS_H_

// Helper method for testing in Python. Dequeues and returns an admitted traffic struct.
static inline
struct admitted_traffic *dequeue_admitted_traffic(struct admissible_status *status) {
    assert(status != NULL);

    struct admitted_traffic *traffic;
    fp_ring_dequeue(status->q_admitted_out, (void **)&traffic);

    return traffic;
}

/**
 * Returns an admission core state, or NULL on error.
 * For testing in Python.
 */
static inline
struct admission_core_state *create_admission_core_state() {
    struct admission_core_state *core = fp_malloc("admission_core_state",
                                                  sizeof(struct admission_core_state));

    if (core == NULL)
        return NULL;

    return core;
}

/**
 * Returns an allocated_traffic container
 * For testing in Python.
 */
static inline
struct admitted_traffic **create_admitted_batch() {
    uint8_t i;

    struct admitted_traffic **admitted_batch = fp_malloc("admitted_batch",
                                                         sizeof(struct admitted_traffic *) * BATCH_SIZE);

    if (admitted_batch == NULL)
        return NULL;

    for (i = 0; i < BATCH_SIZE; i++) {
        admitted_batch[i] = create_admitted_traffic();
        if (admitted_batch[i] == NULL)
            return NULL;
    }
    return admitted_batch;
}

#ifdef NO_DPDK
// Prints the contents of a backlog queue, useful for debugging
static inline
void print_backlog(struct fp_ring *queue) {
    assert(queue != NULL);

	struct bin *bin = queue->elem[0];
    printf("printing backlog queue:\n");
    struct backlog_edge *edge;
    for (edge = &bin->edges[0]; edge < &bin->edges[bin->size]; edge++)
        printf("\t%d\t%d\n", edge->src, edge->dst);
}

// Prints the number of src/dst pairs per bin
static inline
void print_backlog_counts(struct fp_ring *queue) {
    assert(queue != NULL);

    printf("printing backlog bin counts\n");
    uint16_t bin_num;
    uint32_t bin_sums = 0;
    for (bin_num = 0; bin_num < NUM_BINS; bin_num++) {
		struct bin *bin = queue->elem[bin_num];
        printf("\tsize of bin %d: %d\n", bin_num, bin_size(bin));
        bin_sums += bin_size(bin);
    }
    printf("total flows: %d\n", bin_sums);
}
#endif

#endif /* PYTHON_HELPERS_H_ */

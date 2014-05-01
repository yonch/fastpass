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


#endif /* PYTHON_HELPERS_H_ */

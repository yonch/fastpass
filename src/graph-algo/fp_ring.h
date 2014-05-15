#ifndef FP_RING_H_
#define FP_RING_H_

#include <errno.h>

#define FP_RING_BUFFER_SIZE		128

#ifndef NO_DPDK

#include <rte_ring.h>

#define		fp_ring					rte_ring
#define		fp_ring_enqueue			rte_ring_enqueue
#define		fp_ring_dequeue			rte_ring_dequeue
#define		fp_ring_dequeue_burst	rte_ring_dequeue_burst

#else

#include <assert.h>
#include <stdlib.h>

/* A data-structure to communicate pointers between components */
struct fp_ring {
	uint32_t head;
	uint32_t tail;
	uint32_t mask;
	void *elem[0]; // must be last in struct
};

/**
 * Creates a new backlog queue, with 2^{log_size} elements
 */
static inline
struct fp_ring *fp_ring_create(uint32_t log_size) {
	uint32_t num_elems = (1 << log_size);
	uint32_t mem_size = sizeof(struct fp_ring)
							+ num_elems * sizeof(void *);
    struct fp_ring *ring = malloc(mem_size);
    assert(ring != NULL);

    ring->mask = num_elems - 1;
	ring->head = 0;
	ring->tail = 0;
    return ring;
}

// Insert new bin to the back of this backlog queue
static inline
int fp_ring_enqueue(struct fp_ring *ring, void *elem) {
	assert(ring != NULL);
	assert(ring->tail != ring->head - ring->mask - 1);

	ring->elem[ring->tail & ring->mask] = elem;
	ring->tail++;
        
        return 0;
}

/**
 * dequeue
 * @returns 0 on success, -ENOENT if no empty
 */
static inline int fp_ring_dequeue(struct fp_ring *ring, void **obj_p) {
	assert(ring != NULL);
	assert(obj_p != NULL);

	if (ring->head == ring->tail)
		return -ENOENT;

	*obj_p = ring->elem[ring->head++ & ring->mask];
	return 0;
}

static inline
int fp_ring_dequeue_burst(struct fp_ring *r, void **obj_table, unsigned n) {
	int rc = 0;
	int i;
	for (i = 0; i < n; i++) {
		if (fp_ring_dequeue(r, &obj_table[i]) == 0)
			rc++;
		else
			break;
	}
	return rc;
}

// Insert new bin to the back of this backlog queue
static inline int fp_ring_empty(struct fp_ring *ring) {
	assert(ring != NULL);
	return (ring->head == ring->tail);
}

static inline
void destroy_pointer_queue(struct fp_ring *queue) {
    assert(queue != NULL);
	assert(queue->head == queue->tail);

    free(queue);
}

#endif

#endif /* FP_RING_H_ */

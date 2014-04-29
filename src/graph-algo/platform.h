/**
 * Code to support different platforms, e.g. DPDK v.s. vanilla gcc
 */

#ifndef GRAPH_ALGO_PLATFORM_H_
#define GRAPH_ALGO_PLATFORM_H_

#ifndef NO_DPDK

/** DPDK **/
#include <rte_malloc.h>
#include <rte_mempool.h>
#include <rte_branch_prediction.h>
#include "../protocol/platform/generic.h"
#include "../controller/dpdk-time.h"
#define fp_free(ptr)                            rte_free(ptr)
#define fp_calloc(typestr, num, size)           rte_calloc(typestr, num, size, 0)
#define fp_malloc(typestr, size)		rte_malloc(typestr, size, 0)
#define fp_pause()								rte_pause()

#define fp_mempool	 			rte_mempool
#define fp_mempool_get	 		rte_mempool_get
#define fp_mempool_put	 		rte_mempool_put

#else

/** VANILLA **/
#define fp_free(ptr)                            free(ptr)
#define fp_calloc(typestr, num, size)           calloc(num, size)
#define fp_malloc(typestr, size)		malloc(size)
#define fp_get_time_ns()				(1UL << 62)
#define fp_pause()						while (0) {}

#ifndef likely
#define likely(x)  __builtin_expect((x),1)
#endif /* likely */

#ifndef unlikely
#define unlikely(x)  __builtin_expect((x),0)
#endif /* unlikely */

/* mempool */
struct fp_mempool {
	uint32_t total_elements;
	uint32_t cur_elements;
	void **elements;
};
static struct fp_mempool * fp_mempool_create(unsigned n, unsigned elt_size) {
	struct fp_mempool *mp;
	unsigned i;
	/* allocate the struct */
	mp = malloc(sizeof(struct fp_mempool));
	if (mp == NULL)
		return NULL;
	/* populate the mempool struct */
	mp->total_elements = n;
	mp->cur_elements = 0;
	/* allocate the pointer table */
	mp->elements = malloc(n * sizeof(void *));
	if (mp->elements == NULL)
		goto cannot_alloc_ptrs;

	/* allocate structs */
	for (i = 0; i < n; i++) {
		mp->elements[i] = malloc(elt_size);
		if (mp->elements[i] == NULL)
			goto cannot_alloc_elements;
		mp->cur_elements++; /* cur_elements counts successfully alloc. elem. */
	}
	return mp;

cannot_alloc_elements:
	/* free all elements we successfully allocated */
	for (i = 0; i < mp->cur_elements; i++)
		free(mp->elements[i]);
	/* free the array itself */
	free(mp->elements);
cannot_alloc_ptrs:
	/* free the struct */
	free(mp);
	return NULL;
}
static inline int __attribute__((always_inline))
fp_mempool_get(struct fp_mempool *mp, void **obj_p) {
	if (unlikely(mp->cur_elements == 0))
		return -ENOENT;
	*obj_p = mp->elements[--(mp->cur_elements)];
	return 0;
}
static inline void __attribute__((always_inline))
fp_mempool_put(struct fp_mempool *mp, void *obj) {
	mp->elements[mp->cur_elements++] = obj;
}

#endif

#endif /* GRAPH_ALGO_PLATFORM_H_ */

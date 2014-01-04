/**
 * Code to support different platforms, e.g. DPDK v.s. vanilla gcc
 */

#ifndef GRAPH_ALGO_PLATFORM_H_
#define GRAPH_ALGO_PLATFORM_H_

#ifndef NO_DPDK

/** DPDK **/
#include <rte_malloc.h>
#include "../protocol/platform/generic.h"
#include "../controller/dpdk-time.h"
#define fp_malloc(typestr, size)		rte_malloc(typestr, size, 0)

#else

/** VANILLA **/
#define fp_malloc(typestr, size)		malloc(size)
#define fp_get_time_ns()				(1UL << 62)

#endif

#endif /* GRAPH_ALGO_PLATFORM_H_ */

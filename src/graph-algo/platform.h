/**
 * Code to support different platforms, e.g. DPDK v.s. vanilla gcc
 */

#ifndef PLATFORM_H_
#define PLATFORM_H_

#ifndef NO_DPDK

/** DPDK **/
#include <rte_malloc.h>
#define fp_malloc(typestr, size)		rte_malloc(typestr, size, 0)

#else

/** VANILLA **/
#define fp_malloc(typestr, size)		malloc(size)

#endif

#endif /* PLATFORM_H_ */

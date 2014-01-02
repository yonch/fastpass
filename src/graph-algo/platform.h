/**
 * Code to support different platforms, e.g. DPDK v.s. vanilla gcc
 */

#ifndef PLATFORM_H_
#define PLATFORM_H_

#ifndef NO_DPDK

/** DPDK **/
#include <rte_malloc.h>
#define fp_malloc		rte_malloc

#else

/** VANILLA **/
#define fp_malloc		malloc

#endif

#endif /* PLATFORM_H_ */

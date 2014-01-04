/*
 * atomic.h
 *
 *  Created on: Dec 31, 2013
 *      Author: yonch
 */

#ifndef GRAPH_ALGO_ATOMIC_H_
#define GRAPH_ALGO_ATOMIC_H_

#ifdef NO_DPDK
typedef uint32_t atomic32_t;
#define atomic32_init(xptr)				(*(xptr) = 0)
#define atomic32_clear(xptr)			(*(xptr) = 0)
#define atomic32_read(xptr)				(*(xptr))
#define atomic32_add_return(xptr,inc)	(*(xptr) += (inc))
#define atomic32_sub_return(xptr,sub)	(*(xptr) -= (sub))
#define atomic32_set(xptr,val)			(*(xptr) = (val))
#else
#include <rte_atomic.h>

typedef rte_atomic32_t atomic32_t;
#define atomic32_init(xptr)				rte_atomic32_init(xptr)
#define atomic32_clear(xptr)			rte_atomic32_clear(xptr)
#define atomic32_read(xptr)				rte_atomic32_read(xptr)
#define atomic32_add_return(xptr,inc)	rte_atomic32_add_return(xptr,inc)
#define atomic32_sub_return(xptr,sub)	rte_atomic32_sub_return(xptr,sub)
#define atomic32_set(xptr,val)			rte_atomic32_set(xptr,val)
#endif


#endif /* GRAPH_ALGO_ATOMIC_H_ */

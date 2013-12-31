
#ifndef DPDK_PLATFORM_H_
#define DPDK_PLATFORM_H_

#include <time.h>
#include <rte_mempool.h>
#include "../kernel-mod/linux-compat.h"
#include "comm_log.h"
#include "main.h"

extern struct rte_mempool* pktdesc_pool[NB_SOCKETS];


#define FASTPASS_PR_DEBUG(enable, fmt, a...)	do { if (enable)	     \
							COMM_DEBUG("%s: " fmt, __func__, ##a); \
						} while(0)

static inline u64 fp_get_time_ns(void)
{
	struct timespec tp;

	if (unlikely(clock_gettime(CLOCK_REALTIME, &tp) != 0))
		return -1;

	return (1000*1000*1000) * (u64)tp.tv_sec + tp.tv_nsec;
}

static inline
struct fpproto_pktdesc *fpproto_pktdesc_alloc(void)
{
	struct fpproto_pktdesc *pd;
	int socketid = rte_lcore_to_socket_id(rte_lcore_id());

	if (unlikely(rte_mempool_get(pktdesc_pool[socketid], (void**)&pd) != 0))
			return NULL;

	return pd;
}

static inline
void fpproto_pktdesc_free(struct fpproto_pktdesc *pd)
{
	int socketid = rte_lcore_to_socket_id(rte_lcore_id());
	rte_mempool_put(pktdesc_pool[socketid], pd);
}

static inline
int cancel_timer(struct fpproto_conn *proto)
{
//	struct fastpass_sock *fp = container_of(proto, struct fastpass_sock, conn);
	(void)proto;
	COMM_DEBUG("cancel_timer\n");
	return 0;
}

static inline
void set_timer(struct fpproto_conn *proto, u64 when)
{
	(void)proto;
	COMM_DEBUG("set_timer requested for %llu\n", when);
}


#endif /* DPDK_PLATFORM_H_ */

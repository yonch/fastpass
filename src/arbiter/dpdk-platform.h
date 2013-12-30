
#ifndef DPDK_PLATFORM_H_
#define DPDK_PLATFORM_H_

#include <time.h>
#include "../kernel-mod/linux-compat.h"
#include "comm_log.h"

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
	pd = NULL; /* TODO */
	return pd;
}

static inline
void fpproto_pktdesc_free(struct fpproto_pktdesc *pd)
{
	(void) pd; /* TODO */
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

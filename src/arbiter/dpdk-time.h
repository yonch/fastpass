
#ifndef CONTROLLER_DPDK_TIME_H_
#define CONTROLLER_DPDK_TIME_H_

static inline __attribute__((always_inline))
u64 fp_get_time_ns(void)
{
	struct timespec tp;

	if (unlikely(clock_gettime(CLOCK_REALTIME, &tp) != 0))
		return -1;

	return (1000*1000*1000) * (u64)tp.tv_sec + tp.tv_nsec;
}


#endif /* CONTROLLER_DPDK_TIME_H_ */

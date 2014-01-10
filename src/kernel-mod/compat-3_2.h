

#ifndef COMPAT_3_2_H_
#define COMPAT_3_2_H_

#include <linux/version.h>

#if LINUX_VERSION_CODE == KERNEL_VERSION(3,2,45)


#include <linux/kernel.h>
#include <net/sch_generic.h>
#include <net/pkt_sched.h>


/*** from include/uapi/linux/pkt_sched.h **/
/* Need to corrospond to iproute2 tc/tc_core.h "enum link_layer" */
enum tc_link_layer {
	TC_LINKLAYER_UNAWARE, /* Indicate unaware old iproute2 util */
	TC_LINKLAYER_ETHERNET,
	TC_LINKLAYER_ATM,
};
#define TC_LINKLAYER_MASK 0x0F /* limit use to lower 4 bits */


/*** from include/net/sch_generic.h ***/
struct psched_ratecfg {
	u64	rate_bps;
	u32	mult;
	u16	overhead;
	u8	linklayer;
	u8	shift;
};

static inline u64 psched_l2t_ns(const struct psched_ratecfg *r,
				unsigned int len)
{
	len += r->overhead;

	if (unlikely(r->linklayer == TC_LINKLAYER_ATM))
		return ((u64)(DIV_ROUND_UP(len,48)*53) * r->mult) >> r->shift;

	return ((u64)len * r->mult) >> r->shift;
}

extern void psched_ratecfg_precompute(struct psched_ratecfg *r, const struct tc_ratespec *conf);

void qdisc_watchdog_schedule_ns(struct qdisc_watchdog *wd, u64 expires);

#endif

#endif /* COMPAT_3_2_H_ */

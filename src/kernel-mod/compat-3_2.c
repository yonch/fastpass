
#include "compat-3_2.h"

#include <linux/version.h>

#if LINUX_VERSION_CODE == KERNEL_VERSION(3,2,45)

/*** from net/sched/sch_generic.c ***/

void psched_ratecfg_precompute(struct psched_ratecfg *r,
			       const struct tc_ratespec *conf)
{
	u64 factor;
	u64 mult;
	int shift;

	memset(r, 0, sizeof(*r));
	r->overhead = conf->overhead;
	r->rate_bps = (u64)conf->rate << 3;
	r->linklayer = (conf->__reserved /* CHANGED! */ & TC_LINKLAYER_MASK);
	r->mult = 1;
	/*
	 * Calibrate mult, shift so that token counting is accurate
	 * for smallest packet size (64 bytes).  Token (time in ns) is
	 * computed as (bytes * 8) * NSEC_PER_SEC / rate_bps.  It will
	 * work as long as the smallest packet transfer time can be
	 * accurately represented in nanosec.
	 */
	if (r->rate_bps > 0) {
		/*
		 * Higher shift gives better accuracy.  Find the largest
		 * shift such that mult fits in 32 bits.
		 */
		for (shift = 0; shift < 16; shift++) {
			r->shift = shift;
			factor = 8LLU * NSEC_PER_SEC * (1 << r->shift);
			mult = div64_u64(factor, r->rate_bps);
			if (mult > UINT_MAX)
				break;
		}

		r->shift = shift - 1;
		factor = 8LLU * NSEC_PER_SEC * (1 << r->shift);
		r->mult = div64_u64(factor, r->rate_bps);
	}
}


/*** from net/sched/sch_api.c ***/
void qdisc_watchdog_schedule_ns(struct qdisc_watchdog *wd, u64 expires)
{
	if (test_bit(__QDISC_STATE_DEACTIVATED,
		     &qdisc_root_sleeping(wd->qdisc)->state))
		return;

	qdisc_throttled(wd->qdisc);

	hrtimer_start(&wd->timer,
		      ns_to_ktime(expires),
		      HRTIMER_MODE_ABS);
}

#endif

/*
 * net/sched/sch_fastpass.c FastPass client
 *
 *  Copyright (C) 2013 Jonathan Perry <yonch@yonch.com>
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/prefetch.h>

#include "sch_timeslot.h"

static struct tsq_qdisc_entry *simple_tsq_entry;

static int simple_new_qdisc(void *priv) {
	return 0;
}
static void simple_stop_qdisc(void *priv) {
	return;
}
static void simple_add_timeslot(void *priv, u64 src_dst_key) {
	tsq_admit_now(priv, src_dst_key);
}

static struct tsq_ops simple_tsq_ops __read_mostly = {
	.id		=	"fastpass",
	.priv_size	=	0,

	.new_qdisc = simple_new_qdisc,
	.stop_qdisc = simple_stop_qdisc,
	.add_timeslot = simple_add_timeslot,
};

static int __init simple_module_init(void)
{
	int ret = -ENOMEM;

	pr_info("%s: initializing\n", __func__);

	ret = tsq_init();
	if (ret != 0)
		goto out;

	simple_tsq_entry = tsq_register_qdisc(&simple_tsq_ops);
	if (simple_tsq_entry == NULL)
		goto out_exit;


	pr_info("%s: success\n", __func__);
	return 0;

out_exit:
	tsq_exit();
out:
	pr_info("%s: failed, ret=%d\n", __func__, ret);
	return ret;
}

static void __exit simple_module_exit(void)
{
	tsq_unregister_qdisc(simple_tsq_entry);
	tsq_exit();
}

module_init(simple_module_init)
module_exit(simple_module_exit)
MODULE_AUTHOR("Jonathan Perry");
MODULE_LICENSE("GPL");

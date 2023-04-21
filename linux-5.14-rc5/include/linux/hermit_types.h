/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_HERMIT_TYPES_H
#define _LINUX_HERMIT_TYPES_H
/*
 * Declarations for Hermit internal data structures
 */

#include <linux/workqueue.h>
#include <linux/spinlock_types.h>
#include <linux/atomic.h>

/* [Hermit] for now support up to 32 sthreads */
#define HMT_MAX_NR_STHDS 32

struct hmt_work_struct {
	struct work_struct work;
	int id;
};

/*
 * swap stats to control async swap out
 */
struct hmt_swap_ctrl {
	spinlock_t lock;
	atomic_t sthd_cnt;
	atomic_t active_sthd_cnt;
	uint64_t swin_ts[2];
	uint64_t nr_pg_charged[2];
	uint64_t swin_thrghpt;
	uint64_t swout_thrghpt;

	struct {
		uint64_t nr_pages;
		uint64_t total;
		uint64_t avg;
		unsigned cnt;
	} swout_dur;
	bool stop;
	bool master_up;
	short log_cnt;

	struct {
		atomic64_t total;
		atomic_t cnt;
		uint32_t prev_val;
	} rft_dist;
	uint64_t low_watermark;

	struct mm_struct *mm;
};

#endif // _LINUX_HERMIT_TYPES_H

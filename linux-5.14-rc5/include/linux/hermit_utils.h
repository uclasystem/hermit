/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_HERMIT_UTILS_H
#define _LINUX_HERMIT_UTILS_H
/*
 * Declarations for Hermit functions in mm/hermit_utils.c
 */
#include <linux/adc_macros.h>
#include <linux/adc_timer.h>
#include <linux/swap.h>
#include <linux/hermit_types.h>

/***
 * util functions
 */
void hermit_init_memcg(struct mem_cgroup *memcg);
void hermit_cleanup_memcg(struct mem_cgroup *memcg);

void hmt_async_reclaim(struct mm_struct *mm, struct mem_cgroup *memcg);
struct mm_struct *hmt_find_dominate_proc(struct mem_cgroup *memcg);
unsigned hmt_get_sthd_cnt(struct mem_cgroup *memcg, struct hmt_swap_ctrl *sc);
void hmt_update_swap_ctrl(struct mem_cgroup *memcg, struct hmt_swap_ctrl *sc);
#endif // _LINUX_HERMIT_UTILS_H

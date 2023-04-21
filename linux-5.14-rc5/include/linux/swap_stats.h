/**
 * swap_stats.h - collect swap stats for profiling
 */

#ifndef _LINUX_SWAP_STATS_H
#define _LINUX_SWAP_STATS_H

#include <linux/atomic.h>

#include "linux/adc_macros.h"
#include "linux/adc_timer.h"

// profile swap-in cnts
enum adc_counter_type {
	ADC_ONDEMAND_SWAPIN,
	ADC_PREFETCH_SWAPIN,
	ADC_HIT_ON_PREFETCH,
	ADC_SWAPOUT,
	ADC_RECLAIM,
	ADC_BATCH_RECLAIM,
	ADC_HERMIT_SWAPOUT,
	ADC_HERMIT_VPAGES,
	ADC_HERMIT_VADDRS,
	ADC_HERMIT_RECLAIM,
	ADC_OPTIM_FAILED,
	NUM_ADC_COUNTER_TYPE
};

extern atomic_t adc_counters[NUM_ADC_COUNTER_TYPE];

static inline void reset_adc_counter(enum adc_counter_type type)
{
	atomic_set(&adc_counters[type], 0);
}

static inline void adc_profile_counter_inc(enum adc_counter_type type)
{
	atomic_inc(&adc_counters[type]);
}

static inline void adc_counter_add(int cnt, enum adc_counter_type type)
{
	atomic_add(cnt, &adc_counters[type]);
}

static inline int get_adc_counter(enum adc_counter_type type)
{
	return atomic_read(&adc_counters[type]);
}

void report_adc_counters(void);

// profile page fault latency
enum adc_pf_bits {
	ADC_PF_SWAP_BIT = 0,
	ADC_PF_MAJOR_BIT = 1,
	ADC_PF_SWAPOUT_BIT = 2,
	ADC_PF_HERMIT_BIT = 3
};

static inline void set_adc_pf_bits(int *word, enum adc_pf_bits bit_pos)
{
	if (!word)
		return;
	*word |= (1 << bit_pos);
}

static inline void clr_adc_pf_bits(int *word, enum adc_pf_bits bit_pos)
{
	if (!word)
		return;
	*word &= ~(1 << bit_pos);
}

static inline bool get_adc_pf_bits(int word, enum adc_pf_bits bit_pos)
{
	return !!(word & (1 << bit_pos));
}

// profile accumulated time stats
struct adc_time_stat {
	atomic64_t accum_val;
	atomic_t cnt;
};

enum adc_time_stat_type {
	ADC_SWAP_MAJOR_DUR,
	ADC_SWAP_MINOR_DUR,
	ADC_SWAP_OUT_DUR,
	ADC_NON_SWAP_DUR,
	ADC_RDMA_READ_LAT,
	ADC_RDMA_WRITE_LAT,
	ADC_RMAP1_LAT,
	ADC_RMAP2_LAT,
	ADC_HERMIT_RMAP1_LAT,
	ADC_HERMIT_RMAP2_LAT,
	ADC_TLB_FLUSH_DIR,
	ADC_TLB_FLUSH_LAT,
	ADC_IB_CALLBACK,
	ADC_POLL_WAIT,
	ADC_POLL_ALL,
	NUM_ADC_LAT_TYPE,
	NUM_ADC_TIME_STAT_TYPE
};

extern struct adc_time_stat adc_time_stats[NUM_ADC_TIME_STAT_TYPE];
void reset_adc_time_stat(enum adc_time_stat_type type);

static inline void accum_adc_time_stat(enum adc_time_stat_type type, uint64_t val)
{
	struct adc_time_stat *ts = &adc_time_stats[type];
	atomic64_add(val, &ts->accum_val);
	atomic_inc(&ts->cnt);
}

static inline void accum_multi_adc_time_stat(enum adc_time_stat_type type,
					     uint64_t val, int cnt)
{
	struct adc_time_stat *ts = &adc_time_stats[type];
	atomic64_add(val, &ts->accum_val);
	atomic_add(cnt, &ts->cnt);
}

void record_adc_pf_time(int adc_pf_bits, uint64_t dur);
void report_adc_time_stat(void);
void reset_adc_swap_stats(void);

// [RMGrid] page fault breakdown profiling
enum adc_pf_breakdown_type {
	ADC_TRAP_TO_KERNEL,
	ADC_LOCK_GET_PTE,
	ADC_LOOKUP_SWAPCACHE,
	ADC_DEDUP_SWAPIN,
	ADC_CGROUP_ACCOUNT,
	ADC_PAGE_RECLAIM,
	ADC_PREFETCH,
	ADC_UPD_METADATA,
	ADC_PAGE_IO,
	ADC_SETPTE,
	ADC_SET_PAGEMAP_UNLOCK,
	ADC_RET_TO_USER,
	ADC_TOTAL_PF,
	ADC_PG_CHECK_REF,
	ADC_TRY_TO_UNMAP,
	ADC_TLB_FLUSH_DIRTY,
	ADC_BATCHING_OUT,
	ADC_POLL_STORE,
	ADC_POST_BATCHING,
	ADC_RLS_PG_RM_MAP,
	ADC_UNMAP_TLB_FLUSH,
	ADC_ALLOC_SWAP_SLOT,
	ADC_ADD_TO_SWAPCACHE,
	ADC_SHRNK_ACTV_LST,
	ADC_SHRNK_SLAB,
	ADC_READ_PAGE,
	ADC_WRITE_PAGE,
	ADC_RD_CACHE_ASYNC,
	ADC_ALLOC_PAGE,
	ADC_POLL_LOAD,
	NUM_ADC_PF_BREAKDOWN_TYPE
};

enum adc_pf_type {
	ADC_MAJOR_SPF,
	ADC_MINOR_SPF,
	ADC_SWAPOUT_SPF,
	// ADC_ALL_SPF,
	ADC_HMT_OUT_SPF,
	NUM_ADC_PF_TYPE
};
struct adc_pf_time_stat_list {
	atomic64_t accum_vals[NUM_ADC_PF_BREAKDOWN_TYPE];
	atomic_t cnt;
};
extern struct adc_pf_time_stat_list adc_pf_breakdowns[NUM_ADC_PF_TYPE];

#ifdef ADC_PROFILE_PF_BREAKDOWN
static inline void adc_pf_breakdown_stt(uint64_t *pf_breakdown,
					enum adc_pf_breakdown_type type,
					uint64_t ts)
{
	if (!pf_breakdown)
		return;
	pf_breakdown[type] -= ts;
}

static inline void adc_pf_breakdown_end(uint64_t *pf_breakdown,
					enum adc_pf_breakdown_type type,
					uint64_t ts)
{
	if (!pf_breakdown)
		return;
	pf_breakdown[type] += ts;
}

static inline void reset_adc_pf_breakdown(void)
{
	int i, j;
	for (i = 0; i < NUM_ADC_PF_TYPE; i++) {
		for (j = 0; j < NUM_ADC_PF_BREAKDOWN_TYPE; j++) {
			atomic64_set(&adc_pf_breakdowns[i].accum_vals[j], 0);
		}
		atomic_set(&adc_pf_breakdowns[i].cnt, 0);
	}
}

static inline void accum_adc_pf_breakdown(uint64_t pf_breakdown[],
					  enum adc_pf_type pf_type)
{
	const int MAX_CNT = (1 << 30);
	if (!pf_breakdown)
		return;
	if (atomic_read(&adc_pf_breakdowns[pf_type].cnt) < MAX_CNT) {
		int i;
		atomic_inc(&adc_pf_breakdowns[pf_type].cnt);
		for (i = 0; i < NUM_ADC_PF_BREAKDOWN_TYPE; i++) {
			atomic64_add(pf_breakdown[i],
				     &adc_pf_breakdowns[pf_type].accum_vals[i]);
		}
	}
}
void parse_adc_pf_breakdown(int adc_pf_bits, uint64_t pf_breakdown[]);
void report_adc_pf_breakdown(uint64_t *buf);
#else
static inline void adc_pf_breakdown_stt(uint64_t *pf_breakdown,
					enum adc_pf_breakdown_type type,
					uint64_t ts)
{
}

static inline void adc_pf_breakdown_end(uint64_t *pf_breakdown,
					enum adc_pf_breakdown_type type,
					uint64_t ts)
{
}

static inline void reset_adc_pf_breakdown(void)
{
}

static inline void accum_adc_pf_breakdown(int major, uint64_t pf_breakdown[])
{
}

static inline void parse_adc_pf_breakdown(int adc_pf_bits,
					  uint64_t pf_breakdown[])
{
}

static inline void report_adc_pf_breakdown(uint64_t *buf)
{
}
#endif // ADC_PROFILE_PF_BREAKDOWN

// profile swap-in faults
#ifdef HERMIT_DBG_PF_TRACE
#define SPF_BUF_LEN (100L * 1000 * 1000)
#define NUM_SPF_LAT_TYPE 13
extern atomic_t spf_cnt;
extern unsigned int *spf_lat_buf[NUM_SPF_LAT_TYPE];

void record_spf_lat(uint64_t pf_breakdown[]);
#else // !HERMIT_DBG_PF_TRACE
static inline void record_spf_lat(uint64_t pf_breakdown[])
{
}
#endif // HERMIT_DBG_PF_TRACE

#endif /* _LINUX_SWAP_STATS_H */

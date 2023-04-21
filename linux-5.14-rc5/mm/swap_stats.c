#include <linux/swap_stats.h>
#include <linux/kconfig.h>
#include <linux/export.h>
#include <linux/printk.h>

atomic_t adc_counters[NUM_ADC_COUNTER_TYPE];
EXPORT_SYMBOL(adc_counters);

struct adc_time_stat adc_time_stats[NUM_ADC_TIME_STAT_TYPE];
EXPORT_SYMBOL(adc_time_stats);

static const char *adc_counter_names[NUM_ADC_COUNTER_TYPE] = {
	"Demand         ", "Prefetch       ", "HitOnCache     ",
	"TotalSwapOut   ", "TotalReclaim   ", "BatchReclaim   ",
	"HermitSwapOut  ", "HermitIsoVpages", "HermitIsoVaddrs",
	"HermitReclaim  ", "Optimisic Faild"
};

void report_adc_counters(void)
{
	int type;
	for (type = 0; type < NUM_ADC_COUNTER_TYPE; type++) {
		printk("%s: %d\n", adc_counter_names[type],
		       get_adc_counter(type));
	}
}

void reset_adc_time_stat(enum adc_time_stat_type type)
{
	struct adc_time_stat *ts = &adc_time_stats[type];
	atomic64_set(&(ts->accum_val), 0);
	atomic_set(&(ts->cnt), 0);
}

static const char *adc_time_stat_names[NUM_ADC_TIME_STAT_TYPE] = {
	"major swap duration", "minor swap duration", "swap-out   duration",
	"non-swap   duration", "RDMA read  latency ", "RDMA write latency ",
	"check references   ", "reverse mapping    ", "hermit check refs  ",
	"hermit rmapping    ", "TLB flush dirty    ", "TLB flush          ",
	"IB MLX4 callback   ", "Poll wait          ", "Poll All           ",
};

void report_adc_time_stat(void)
{
	int type;
	for (type = 0; type < NUM_ADC_TIME_STAT_TYPE; type++) {
		struct adc_time_stat *ts = &adc_time_stats[type];
		int64_t dur =
			adc_safe_div((int64_t)atomic64_read(&ts->accum_val),
				     (int64_t)atomic_read(&ts->cnt));
		if (type < NUM_ADC_LAT_TYPE) { // latencies
			dur = dur * 1000 / RMGRID_CPU_FREQ;
			printk("%s: %lldns, #: %lld\n",
			       adc_time_stat_names[type], dur,
			       (int64_t)atomic_read(&ts->cnt));
		}
	}
}

void reset_adc_swap_stats(void)
{
	int i;
	for (i = 0; i < NUM_ADC_COUNTER_TYPE; i++) {
		reset_adc_counter(i);
	}

	for (i = 0; i < NUM_ADC_TIME_STAT_TYPE; i++) {
		reset_adc_time_stat(i);
	}
}

inline void record_adc_pf_time(int adc_pf_bits, uint64_t dur)
{
	enum adc_time_stat_type type = NUM_ADC_TIME_STAT_TYPE;
	if (get_adc_pf_bits(adc_pf_bits, ADC_PF_SWAP_BIT)) { // swap fault
		if (get_adc_pf_bits(adc_pf_bits, ADC_PF_MAJOR_BIT)) {
			// Major swap fault
			type = ADC_SWAP_MAJOR_DUR;
		} else { // Minor swap fault
			type = ADC_SWAP_MINOR_DUR;
		}
	} else { // non-swap fault
		type = ADC_NON_SWAP_DUR;
	}
	accum_adc_time_stat(type, dur);
}

// [RMGrid] page fault breakdown profiling
struct adc_time_stat adc_time_stats[NUM_ADC_TIME_STAT_TYPE];
struct adc_pf_time_stat_list adc_pf_breakdowns[NUM_ADC_PF_TYPE];

#ifdef ADC_PROFILE_PF_BREAKDOWN
static const char *adc_pf_breakdown_names[NUM_ADC_PF_BREAKDOWN_TYPE] = {
	"TRAP_TO_KERNEL    ",
	"LOCK_GET_PTE      ",
	"LOOKUP_SWAPCACHE  ",
	"DEDUP_SWAPIN      ",
	"CGROUP_ACCOUNT    ",
	"PAGE_RECLAIM      ",
	"PREFETCH          ",
	"UPD_METADATA      ",
	"PAGE_IO           ",
	"SETPTE            ",
	"SET_PAGEMAP_UNLOCK",
	"RET_TO_USER       ",
	"TOTAL_PF          ",
	"PG_CHECK_REF      ",
	"TRY_TO_UNMAP      ",
	"TLB_FLUSH_DIRTY   ",
	"BATCHING_OUT      ",
	"POLL_STORE        ",
	"POST BATCHING     ",
	"RLS_PG_RM_MAP     ",
	"TLB_FLUSH         ",
	"ALLOC_SWAP_SLOT   ",
	"ADD_TO_SWAPCACHE  ",
	"ADC_SHRNK_ACTV_LST",
	"ADC_SHRNK_SLAB    ",
	"READ_PAGE         ",
	"WRITE_PAGE        ",
	"READ_CACHE_ASYNC  ",
	"ALLOC_PAGE        ",
	"POLL_LOAD         ",
};

static const char *adc_pf_type_names[NUM_ADC_PF_TYPE] = {
	"Major   SPF", "Minor   SPF", "Swapout SPF", // "Avg All SPF",
	"Hmt out SPF"
};

void report_adc_pf_breakdown(uint64_t *buf)
{
	int type;
	for (type = 0; type < NUM_ADC_PF_TYPE; type++) {
		int i;
		int cnt = atomic_read(&adc_pf_breakdowns[type].cnt);
		for (i = 0; i < NUM_ADC_PF_BREAKDOWN_TYPE; i++) {
			uint64_t duration = atomic64_read(
				&adc_pf_breakdowns[type].accum_vals[i]);
			if (cnt == 0)
				duration = 0;
			else {
				duration /= cnt;
				duration = duration * 1000 / RMGRID_CPU_FREQ;
			}
			printk("%s: %s \t%lluns", adc_pf_type_names[type],
			       adc_pf_breakdown_names[i], duration);
		}
		printk("Total #(%s): %d\n", adc_pf_type_names[type], cnt);
	}
}

void parse_adc_pf_breakdown(int adc_pf_bits, uint64_t pf_breakdown[])
{
	if (get_adc_pf_bits(adc_pf_bits, ADC_PF_SWAP_BIT)) { // swap fault
		enum adc_pf_type type = NUM_ADC_PF_TYPE;
		if (get_adc_pf_bits(adc_pf_bits, ADC_PF_MAJOR_BIT)) {
			// Major swap fault
			type = ADC_MAJOR_SPF;
		} else { // Minor swap fault
			type = ADC_MINOR_SPF;
		}
		record_spf_lat(pf_breakdown);
		accum_adc_pf_breakdown(pf_breakdown, type);
		// fault with swap out
		if (get_adc_pf_bits(adc_pf_bits, ADC_PF_SWAPOUT_BIT)) {
			enum adc_pf_type type = NUM_ADC_PF_TYPE;
			if (get_adc_pf_bits(adc_pf_bits, ADC_PF_HERMIT_BIT)) {
				// hermit swapout path
				type = ADC_HMT_OUT_SPF;
			} else {
				// normal swapout path
				type = ADC_SWAPOUT_SPF;
			}
			accum_adc_pf_breakdown(pf_breakdown, type);
		}
		// accum_adc_pf_breakdown(pf_breakdown, ADC_ALL_SPF);
	}
}
#else
#endif // ADC_PROFILE_PF_BREAKDOWN

#ifdef HERMIT_DBG_PF_TRACE
inline void record_spf_lat(uint64_t pf_breakdown[])
{
	int idx = 0;
	int cnt;
	if (!pf_breakdown)
		return;
	cnt = atomic_inc_return(&spf_cnt) - 1;
	if (cnt >= SPF_BUF_LEN)
		return;
	spf_lat_buf[idx++][cnt] = pf_breakdown[ADC_TOTAL_PF];
	spf_lat_buf[idx++][cnt] = pf_breakdown[ADC_LOCK_GET_PTE];
	spf_lat_buf[idx++][cnt] =
		pf_breakdown[ADC_PAGE_IO] - pf_breakdown[ADC_CGROUP_ACCOUNT];
	spf_lat_buf[idx++][cnt] = pf_breakdown[ADC_POLL_LOAD];
	spf_lat_buf[idx++][cnt] = pf_breakdown[ADC_CGROUP_ACCOUNT];
	spf_lat_buf[idx++][cnt] = pf_breakdown[ADC_RET_TO_USER];
	spf_lat_buf[idx++][cnt] = pf_breakdown[ADC_SET_PAGEMAP_UNLOCK];
	// spf_lat_buf[idx++][cnt] = pf_breakdown[ADC_LOCKPAGE];
	// spf_lat_buf[idx++][cnt] = pf_breakdown[ADC_KSM];
	spf_lat_buf[idx++][cnt] = pf_breakdown[ADC_SETPTE];
}

#endif // HERMIT_DBG_PF_TRACE

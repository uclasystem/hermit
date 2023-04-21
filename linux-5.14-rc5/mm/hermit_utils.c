/*
 * mm/hermit_utils.c - hermit utils functions
 */
#include <linux/hermit_types.h>
#include <linux/hermit_utils.h>
#include <linux/hermit.h>
#include <linux/memcontrol.h>
#include <linux/swap.h>

void hmt_async_reclaim(struct mm_struct *mm, struct mem_cgroup *memcg)
{
	struct hmt_swap_ctrl *hmt_sc = &memcg->hmt_sc;
	int next_sthd_cnt;
	int i;
	// BUG_ON(mm != current->mm);
	if (!mm || mem_cgroup_is_root(memcg) ||
	    !spin_trylock_irq(&memcg->hmt_sc.lock))
		return;

	if (!hmt_sc->mm)
		hmt_sc->mm = mm;
	else if (hmt_sc->mm != mm) {
		if (false && is_hermit_app(current->comm)) {
			pr_err("memcg id %d, root memcg id %d\n", memcg->id.id,
			       root_mem_cgroup->id.id);
			pr_err("%s:%d Hermit only supports a single app per "
			       "cgroup for now\n"
			       "hmt_sc->cthd: %s mm %p, current: %s mm %p",
			       __func__, __LINE__, hmt_sc->mm->owner->comm,
			       hmt_sc->mm, current->comm, current->mm);
		}
		hmt_sc->mm = mm;
	}
	hmt_update_swap_ctrl(memcg, hmt_sc);
	next_sthd_cnt = hmt_get_sthd_cnt(memcg, hmt_sc);
	atomic_set(&hmt_sc->sthd_cnt, next_sthd_cnt);
	spin_unlock_irq(&hmt_sc->lock);
	for (i = 0; i < next_sthd_cnt; i++)
		schedule_work_on(hmt_sthd_cores[i], &memcg->sthds[i].work);
}

// copied from memcontrol.c
#define for_each_mem_cgroup_tree(iter, root)                                   \
	for (iter = mem_cgroup_iter(root, NULL, NULL); iter != NULL;           \
	     iter = mem_cgroup_iter(root, iter, NULL))

struct mm_struct *hmt_find_dominate_proc(struct mem_cgroup *memcg)
{
	struct mm_struct *ret_mm = NULL;
	struct mem_cgroup *iter;
	bool found = false;
	unsigned long max = READ_ONCE(memcg->memory.max);

	BUG_ON(memcg == root_mem_cgroup);
	for_each_mem_cgroup_tree(iter, memcg) {
		struct css_task_iter it;
		struct task_struct *task;

		css_task_iter_start(&iter->css, CSS_TASK_ITER_PROCS, &it);
		while (!found && (task = css_task_iter_next(&it))) {
			unsigned long anon, file, shmem;
			struct mm_struct *mm = get_task_mm(task);
			if (!mm)
				continue;
			anon = get_mm_counter(mm, MM_ANONPAGES);
			file = get_mm_counter(mm, MM_FILEPAGES);
			shmem = get_mm_counter(mm, MM_SHMEMPAGES);
			if (anon + file + shmem > max / 2) {
				ret_mm = mm;
				found = true;
				pr_err("%s:%d task %s\n", __func__, __LINE__,
				       task->comm);
			}
			mmput(mm);
		}
		css_task_iter_end(&it);
		if (found) {
			mem_cgroup_iter_break(memcg, iter);
			break;
		}
	}
	return ret_mm;
}

/***
 * async swapout inline utils
 */
static inline uint64_t hmt_calc_throughput(uint64_t nr_pgs, uint64_t dur)
{
	return nr_pgs * 1000 * 1000 * RMGRID_CPU_FREQ / dur; // #(pgs)/s
}

/***
 * async swapout dynamic control utilities
 */
inline unsigned hmt_get_sthd_cnt(struct mem_cgroup *memcg,
				 struct hmt_swap_ctrl *sc)
{
	const int ALPHA = 128;
	const int BETA = 16;
	int MAX_THD_CNT = hmt_ctl_vars[HMT_STHD_CNT];
	uint64_t mem_limit = READ_ONCE(memcg->memory.max);
	uint64_t nr_avail_pgs = mem_limit - page_counter_read(&memcg->memory);

	// 0 for Hermit scheduling. 1 and 2 to simulate Fastswap's policy.
	unsigned mode = hmt_ctl_vars[HMT_RECLAIM_MODE];
	if (mode == 0 && sc->swin_thrghpt && sc->swout_thrghpt) {
		int swap_intensity;
		int max_thd_cnt;
		int64_t low_watermark, high_watermark;
		swap_intensity = sc->swin_thrghpt / sc->swout_thrghpt;
		max_thd_cnt = min(MAX_THD_CNT, swap_intensity);

		high_watermark = max_thd_cnt * ALPHA;
		low_watermark = high_watermark * BETA;
		if (sc->low_watermark > low_watermark)
			low_watermark = sc->low_watermark;
		else
			sc->low_watermark = low_watermark;

		if (nr_avail_pgs > low_watermark) // phase 1: enough memory
			return 0;
		else if (nr_avail_pgs >= high_watermark)
			return 1; // phase 2: light memory pressure
		else { // phase 3: about to OOM
			int thd_cnt = (high_watermark - nr_avail_pgs) / ALPHA;
			thd_cnt = min(max(thd_cnt, 1), MAX_THD_CNT);
			return thd_cnt;
		}
	} else if (mode == 1) {
		if (nr_avail_pgs < 2048)
			return MAX_THD_CNT;
		else
			return 0;
	} else {
		if (nr_avail_pgs < 2048)
			return 1;
		return 0;
	}
}

static inline void hmt_update_high_watermark(struct mem_cgroup *memcg,
					     struct hmt_swap_ctrl *sc)
{
	sc->nr_pg_charged[1] = atomic64_read(&memcg->total_pg_charge);
	sc->swin_thrghpt = max(
		sc->swin_thrghpt,
		hmt_calc_throughput(sc->nr_pg_charged[1] - sc->nr_pg_charged[0],
				    sc->swin_ts[1] - sc->swin_ts[0]));

	sc->swin_ts[0] = sc->swin_ts[1];
	sc->nr_pg_charged[0] = sc->nr_pg_charged[1];
}

static inline void hmt_update_low_watermark(struct mem_cgroup *memcg,
					    struct hmt_swap_ctrl *sc)
{
	const int GAMMA = 2000; // == 1.0/5e-5. Sadly no float in kernel
	int cnt = atomic_read(&sc->rft_dist.cnt);
	if (cnt) {
		uint64_t total = atomic64_read(&sc->rft_dist.total);
		uint64_t avg_rft_dist = total / cnt;
		uint64_t step_size = page_counter_read(&memcg->memory) / GAMMA;
		if (sc->rft_dist.prev_val <= avg_rft_dist)
			sc->low_watermark += step_size;
		else
			sc->low_watermark = 0;

		sc->rft_dist.prev_val = avg_rft_dist;
		atomic64_set(&sc->rft_dist.total, 0);
		atomic_set(&sc->rft_dist.cnt, 0);

		if (false && sc->log_cnt % 1000 == 0) {
			pr_err("refault dist: %llu, "
			       "step size: %llu, "
			       "low watermark: %llu\n",
			       avg_rft_dist, step_size, sc->low_watermark);
		}
	}
}

// must hold sc->lock
inline void hmt_update_swap_ctrl(struct mem_cgroup *memcg,
				 struct hmt_swap_ctrl *sc)
{
	const int UPD_PERIOD = 1000; // update swap-in tput per UPD_PERIOD us
	if (sc->swin_ts[0] == 0) {
		memset(sc, 0, sizeof(struct hmt_swap_ctrl));
		sc->swin_ts[0] = get_cycles_light();
		sc->nr_pg_charged[0] = atomic64_read(&memcg->total_pg_charge);
		return;
	}
	sc->swin_ts[1] = get_cycles_light();
	if (sc->swin_ts[1] - sc->swin_ts[0] < UPD_PERIOD * RMGRID_CPU_FREQ)
		return;

	sc->log_cnt++;

	hmt_update_high_watermark(memcg, sc);
	hmt_update_low_watermark(memcg, sc);

	// log for debug
	if (false && sc->log_cnt % 10000 == 0 && sc->swin_thrghpt &&
	    sc->swout_dur.avg) {
		uint64_t nr_avail_pgs = 0;
		uint64_t reclaim_time_budget = 0;
		nr_avail_pgs = READ_ONCE(memcg->memory.max) -
			       page_counter_read(&memcg->memory);
		reclaim_time_budget =
			nr_avail_pgs * 1000 * 1000 / sc->swin_thrghpt; // in us
		spin_unlock_irq(&sc->lock);
		pr_err("swin_thrghput: %8llupg/s,"
		       "swout_thrghput: %8llupg/s, "
		       "swout_duration: %8lluus, "
		       "budget %8lluus, %llupgs\n",
		       sc->swin_thrghpt, sc->swout_thrghpt,
		       sc->swout_dur.avg / RMGRID_CPU_FREQ, reclaim_time_budget,
		       nr_avail_pgs);
		spin_lock_irq(&sc->lock);
		sc->log_cnt = 0;
	}
}

static inline void accum_swout_dur(struct hmt_swap_ctrl *sc, uint64_t dur,
				   unsigned nr_reclaimed)
{
	sc->swout_dur.nr_pages += nr_reclaimed;
	sc->swout_dur.total += dur;
	sc->swout_dur.cnt++;
	sc->swout_dur.avg = sc->swout_dur.total / sc->swout_dur.cnt;

	sc->swout_thrghpt = hmt_calc_throughput(sc->swout_dur.nr_pages,
						sc->swout_dur.total);
}

static unsigned long hermit_reclaim_high(struct task_struct *cthd,
					 struct hmt_swap_ctrl *sc, bool master,
					 unsigned int nr_pages, gfp_t gfp_mask)
{
	unsigned long total_reclaimed = 0;
	struct mem_cgroup *memcg = mem_cgroup_from_task(cthd);

	do {
#ifdef ADC_PROFILE_PF_BREAKDOWN
		uint64_t pf_breakdown[NUM_ADC_PF_BREAKDOWN_TYPE] = { 0 };
#else
		uint64_t *pf_breakdown = NULL;
#endif
		uint64_t swout_dur;
		unsigned long nr_reclaimed = 0;
		// unsigned long pflags;

		swout_dur = -pf_cycles_start();
		// psi_memstall_enter(&pflags);
		nr_reclaimed = hermit_try_to_free_mem_cgroup_pages(
			memcg, nr_pages, gfp_mask, true, cthd, NULL,
			pf_breakdown);
		total_reclaimed += nr_reclaimed;
		// psi_memstall_leave(&pflags);
		swout_dur += pf_cycles_end();
		adc_pf_breakdown_end(pf_breakdown, ADC_TOTAL_PF, swout_dur);
		accum_adc_pf_breakdown(pf_breakdown, ADC_HMT_OUT_SPF);
		if (master)
			accum_swout_dur(sc, swout_dur, nr_reclaimed);
	} while ((memcg = parent_mem_cgroup(memcg)) &&
		 !mem_cgroup_is_root(memcg));

	return total_reclaimed;
}

static void hermit_high_work_func(struct work_struct *work)
{
	struct hmt_work_struct *hmt_ws =
		container_of(work, struct hmt_work_struct, work);
	int id = hmt_ws->id;
	struct mem_cgroup *memcg =
		container_of(hmt_ws, struct mem_cgroup, sthds[id]);
	struct hmt_swap_ctrl *hmt_sc = &memcg->hmt_sc;
	struct mm_struct *mm = hmt_sc->mm;
	struct task_struct *cthd = NULL;

	if (READ_ONCE(hmt_sc->stop))
		return;
	if (!mm) {
		pr_err("%s:%d\n", __func__, __LINE__);
		return;
	}

	mmgrab(mm);
	rcu_read_lock();
	cthd = rcu_dereference(mm->owner);
	rcu_read_unlock();
	atomic_inc(&hmt_sc->active_sthd_cnt);
	css_get(&memcg->css);
	if (id < hmt_get_sthd_cnt(memcg, hmt_sc)) {
		hermit_reclaim_high(cthd, hmt_sc, /* master = */ id == 0,
				    MEMCG_CHARGE_BATCH, GFP_KERNEL);
	}
	if (id < hmt_get_sthd_cnt(memcg, hmt_sc)) {
		schedule_work_on(hmt_sthd_cores[id], &memcg->sthds[id].work);
	}
	css_put(&memcg->css);
	atomic_dec(&hmt_sc->active_sthd_cnt);
	mmdrop(mm);
}

void hermit_init_memcg(struct mem_cgroup *memcg)
{
	int i;
	if (!memcg)
		return;

	memset(&memcg->hmt_sc, 0, sizeof(struct hmt_swap_ctrl));
	memcg->hmt_sc.lock = __SPIN_LOCK_UNLOCKED(memcg->hmt_sc.lock);
	for (i = 0; i < HMT_MAX_NR_STHDS; i++) {
		INIT_WORK(&memcg->sthds[i].work, hermit_high_work_func);
		memcg->sthds[i].id = i;
	}
	memcg->hmt_sc.master_up = true;
}

void hermit_cleanup_memcg(struct mem_cgroup *memcg)
{
	if (!memcg)
		return;

	WRITE_ONCE(memcg->hmt_sc.stop, true);
	while (atomic_read(&memcg->hmt_sc.active_sthd_cnt))
		cond_resched();

	// int i;
	// for (i = 0; i < HMT_MAX_NR_STHDS; i++)
	// 	cancel_work_sync(&memcg->sthds[i].work);
}

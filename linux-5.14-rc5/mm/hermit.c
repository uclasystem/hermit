/*
 * mm/hermit.c - hermit virtual-address directed rmap & swap
 */
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/export.h>
#include <linux/mmu_notifier.h>
#include <linux/page_idle.h>
#include <linux/debugfs.h>
#include <linux/swap.h>
#include <linux/cpu.h>
#include <linux/delay.h>

#include "linux/hermit.h"
#include "linux/swap_stats.h"
#include "internal.h"

/*
 * global variables for profile and control
 */
uint32_t hmt_sthd_cores[NR_CPUS];

bool hmt_ctl_flags[NUM_HMT_CTL_FLAGS];
EXPORT_SYMBOL(hmt_ctl_flags);
const char *hmt_ctl_flag_names[NUM_HMT_CTL_FLAGS] = {
	"bypass_swapcache",
	"batch_swapout",
	"batch_tlb",
	"batch_io",
	"batch_account",
	"vaddr_swapout",
	"speculative_io",
	"speculative_lock",
	"lazy_poll",
	"apt_reclaim",
};

unsigned hmt_ctl_vars[NUM_HMT_CTL_VARS];
const char *hmt_ctl_var_names[NUM_HMT_CTL_VARS] = {
	"sthd_cnt",
	"reclaim_mode",
};

// profile swap-in faults
// #define HERMIT_DBG_PF_TRACE
#ifdef HERMIT_DBG_PF_TRACE
#define VADDR_BUF_LEN (20L * 1000 * 1000)
static atomic_t vaddr_cnt;
static unsigned long *vaddr_buf;

atomic_t spf_cnt;
unsigned int *spf_lat_buf[NUM_SPF_LAT_TYPE];
#endif // HERMIT_DBG_PF_TRACE

/** For Hermit reclamation */
static struct kmem_cache *vpage_cachep;

/** For Hermit prefetch */
DEFINE_PER_CPU(struct pref_request_queue, pref_request_queue);

/*
 * initialization
 */
static inline void hermit_debugfs_init(void)
{
	struct dentry *root = debugfs_create_dir("hermit", NULL);
	int i;
#ifdef HERMIT_DBG_PF_TRACE
	static struct debugfs_blob_wrapper vaddr_blob;
	static struct debugfs_blob_wrapper spf_lat_blob[NUM_SPF_LAT_TYPE];
	char fname[15] = "spf_lats0\0";
#endif // HERMIT_DBG_PF_TRACE

	if (!root)
		return;

#ifdef HERMIT_DBG_PF_TRACE
	vaddr_buf =
		kvmalloc(sizeof(unsigned long) * 4 * VADDR_BUF_LEN, GFP_KERNEL);
	vaddr_blob.data = (void *)vaddr_buf;
	vaddr_blob.size = sizeof(unsigned long) * 4 * VADDR_BUF_LEN;
	atomic_set(&vaddr_cnt, 0);
	pr_err("create hermit debugfs files %p", (void *)vaddr_buf);

	debugfs_create_atomic_t("vaddr_cnt", 0666, root, &vaddr_cnt);
	debugfs_create_blob("vaddr_list", 0666, root, &vaddr_blob);

	debugfs_create_atomic_t("spf_cnt", 0666, root, &spf_cnt);
	atomic_set(&spf_cnt, 0);
	for (i = 0; i < NUM_SPF_LAT_TYPE; i++) {
		spf_lat_buf[i] = kvmalloc(sizeof(unsigned int) * SPF_BUF_LEN,
					  GFP_KERNEL);
		spf_lat_blob[i].data = (void *)spf_lat_buf[i];
		spf_lat_blob[i].size = sizeof(unsigned int) * SPF_BUF_LEN;
		sprintf(fname, "spf_lats%d", i);
		debugfs_create_blob(fname, 0666, root, &spf_lat_blob[i]);
	}
#endif // HERMIT_DBG_PF_TRACE

	for (i = 0; i < NUM_HMT_CTL_FLAGS; i++)
		debugfs_create_bool(hmt_ctl_flag_names[i], 0666, root,
				    &hmt_ctl_flags[i]);

	for (i = 0; i < NUM_HMT_CTL_VARS; i++)
		debugfs_create_u32(hmt_ctl_var_names[i], 0666, root,
				   &hmt_ctl_vars[i]);
}

int __init hermit_init(void)
{
	int i;
	for (i = 0; i < NUM_HMT_CTL_FLAGS; i++)
		hmt_ctl_flags[i] = false;

	hmt_ctl_vars[HMT_STHD_CNT] = 16;
	hmt_ctl_vars[HMT_RECLAIM_MODE] = 0;

	for (i = 0; i < hmt_ctl_var(HMT_STHD_CNT); i++)
		hmt_sthd_cores[i] = num_online_cpus() - 1 - i;

	vpage_cachep = kmem_cache_create("hermit_vpage", sizeof(struct vpage),
					 0, SLAB_PANIC, NULL);

	hermit_debugfs_init();
	return 0;
}
__initcall(hermit_init);

/*
 * [Hermit] vaddr field for reverse mapping
 */
unsigned long *hermit_page_vaddrs = NULL;
static int __init hermit_init_page_vaddrs(void)
{
	hermit_page_vaddrs =
		kvmalloc(sizeof(unsigned long) * (RMGRID_MAX_MEM / PAGE_SIZE),
			 GFP_KERNEL);
	memset(hermit_page_vaddrs, 0,
	       sizeof(unsigned long) * (RMGRID_MAX_MEM / PAGE_SIZE));
	pr_err("hermit_page_vaddrs init at 0x%lx\n",
	       (unsigned long)hermit_page_vaddrs);
	return 0;
}
early_initcall(hermit_init_page_vaddrs);

/*
 * [Hermit] manage vpage
 */
static inline struct vpage *vpage_alloc(void)
{
	return kmem_cache_alloc(vpage_cachep, GFP_KERNEL);
}

inline struct vpage *create_vpage(struct vm_area_struct *vma,
				  unsigned long address, struct page *page,
				  pte_t *pte)
{
	struct vpage *vpage = NULL;
	vpage = vpage_alloc();
	if (!vpage)
		return NULL;
	vpage->vma = vma;
	vpage->address = address;
	vpage->page = page;
	vpage->pte = pte;
	return vpage;
}

inline void free_vpage(struct vpage *vpage)
{
	kmem_cache_free(vpage_cachep, vpage);
}

void free_vpages(struct list_head *vpage_list)
{
	struct list_head *pos, *tmp;
	list_for_each_safe (pos, tmp, vpage_list) {
		struct vpage *vpage = container_of(pos, struct vpage, node);
		list_del(pos);
		free_vpage(vpage);
	}
	// INIT_LIST_HEAD(vpage_list);
}

static inline void hmt_record_vaddr(struct task_struct *task,
				    struct vm_area_struct *vma,
				    unsigned long addr)
{
#ifdef HERMIT_DBG_PF_TRACE
	int cnt;
	if (is_hermit_app(task->comm)) {
		cnt = atomic_inc_return(&vaddr_cnt) - 1;
		// cnt %= VADDR_BUF_LEN;
		if (cnt >= VADDR_BUF_LEN)
			return;
		vaddr_buf[cnt * 4 + 0] = task_pid_vnr(task);
		vaddr_buf[cnt * 4 + 1] = addr;
		vaddr_buf[cnt * 4 + 2] = vma->vm_start;
		vaddr_buf[cnt * 4 + 3] = vma->vm_end - vma->vm_start;
	}
#endif // HERMIT_DBG_PF_TRACE
}

/*
 * [Hermit] Speculative IO
 */
struct hmt_spec_counter hmt_spec_cnter;

inline bool hmt_enable_spec_io(void)
{
	static const uint64_t RESET_PERIOD = 10ul * 1000 * 1000; // 10M
	static const uint64_t PROFILE_PERIOD = 100 * 1000; // 100K
	static const uint64_t FR_FACTOR = 100000; // work around float-point num
	static const uint64_t FR_THRESH = FR_FACTOR * 1 / 100; // 1% failures

	bool enable = false;
	uint64_t nr_swapin = 0;
	uint64_t nr_trail = 0;

	if (!hmt_ctl_flag(HMT_SPEC_IO))
		return false;
	nr_swapin = atomic64_fetch_inc(&hmt_spec_cnter.swapin);
	if (nr_swapin == RESET_PERIOD) { // only 1 thd can enter
		// try to reset counters and re-enable spec io
		atomic64_set(&hmt_spec_cnter.fail, 0);
		atomic64_set(&hmt_spec_cnter.trial, 0);
		atomic64_set(&hmt_spec_cnter.swapin, 0);
		enable = hmt_ctl_flag(HMT_SPEC_IO);
		hmt_spec_cnter.curr_state = enable;
		return enable;
	} else {
		enable = hmt_spec_cnter.curr_state;
		if (!enable) // spec IO is disabled in this reset period
			return false;
	}

	// check failure rate
	nr_trail = atomic64_fetch_inc(&hmt_spec_cnter.trial);
	if (nr_trail == PROFILE_PERIOD) {
		uint64_t nr_fail = 0;
		uint64_t failure_rate = 0;
		nr_fail = atomic64_read(&hmt_spec_cnter.fail);
		atomic64_set(&hmt_spec_cnter.fail, 0);
		atomic64_set(&hmt_spec_cnter.trial, 0);
		failure_rate = FR_FACTOR * nr_fail / nr_trail;
		if (failure_rate > FR_THRESH) { // disable spec io
			enable = false;
			hmt_spec_cnter.curr_state = false;
		}
	}
	return enable;
}

/*
 * utils
 */
void hmt_update_rft_dist(struct page *page, unsigned rft_dist)
{
	struct mem_cgroup *memcg = NULL;
	struct hmt_swap_ctrl *sc = NULL;
	if (!hmt_ctl_flag(HMT_APT_RECLAIM) || !page)
		return;
	memcg = page_memcg(page);
	if (!memcg)
		return;
	sc = &memcg->hmt_sc;
	atomic64_add(rft_dist, &sc->rft_dist.total);
	atomic_inc(&sc->rft_dist.cnt);
}

int hermit_page_referenced(struct vpage *vpage, struct page *page,
			   int is_locked, struct mem_cgroup *memcg,
			   unsigned long *vm_flags)
{
	int we_locked = 0;
	int referenced = 0;

	unsigned long address = vpage->address;
	struct vm_area_struct *vma = vpage->vma;
	pte_t *pte = vpage->pte;

	*vm_flags = 0;
	BUG_ON(!PageAnon(page));
	BUG_ON(!page_rmapping(page));
	// BUG_ON(total_mapcount(page) != 1);
	if (total_mapcount(page) > 1) {
		pr_err("%s:%d total_mapcount > 1!\n", __func__, __LINE__);
		return page_referenced(page, is_locked, memcg, vm_flags);
	}

	if (!is_locked && (!PageAnon(page) || PageKsm(page))) {
		we_locked = trylock_page(page);
		if (!we_locked)
			return 1;
	}

	if (vma->vm_flags & VM_LOCKED) {
		// page_vma_mapped_walk_done(&pvmw);
		if (pte && !PageHuge(page))
			pte_unmap(pte);
		*vm_flags |= VM_LOCKED;
		goto walk_done;
	}

	// page_referenced_one
	if (pte) {
		if (ptep_clear_flush_young_notify(vma, address, pte)) {
			/*
			 * Don't treat a reference through
			 * a sequentially read mapping as such.
			 * If the page has been used in another mapping,
			 * we will catch it; if this other mapping is
			 * already gone, the unmap path will have set
			 * PG_referenced or activated the page.
			 */
			if (likely(!(vma->vm_flags & VM_SEQ_READ)))
				referenced++;
		}
	}
	// NOTE: shouldn't get THP
	/* else if (IS_ENABLED(CONFIG_TRANSPARENT_HUGEPAGE)) {
		if (pmdp_clear_flush_young_notify(vma, address, pvmw.pmd))
			referenced++;
	} */
	else {
		/* unexpected pmd-mapped page? */
		WARN_ON_ONCE(1);
	}

walk_done:
	if (referenced)
		clear_page_idle(page);
	if (test_and_clear_page_young(page))
		referenced++;

	if (referenced) {
		*vm_flags |= vma->vm_flags;
	}

	return referenced;
}

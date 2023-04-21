/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_HERMIT_H
#define _LINUX_HERMIT_H
/*
 * Declarations for Hermit functions in mm/hermit.c
 */

#include <linux/rmap.h>
#include <linux/hermit_types.h>

/*
 * global variables for profile and control
 */
enum hmt_ctl_flag_type {
	HMT_BPS_SCACHE,
	HMT_BATCH_OUT,
	HMT_BATCH_TLB,
	HMT_BATCH_IO,
	HMT_BATCH_ACCOUNT,
	HMT_VADDR_OUT,
	HMT_SPEC_IO,
	HMT_SPEC_LOCK,
	HMT_LAZY_POLL,
	HMT_APT_RECLAIM,
	NUM_HMT_CTL_FLAGS
};

enum hmt_ctl_var_type {
	HMT_STHD_CNT,
	HMT_RECLAIM_MODE,
	NUM_HMT_CTL_VARS
};

extern bool hmt_ctl_flags[NUM_HMT_CTL_FLAGS];
extern unsigned hmt_ctl_vars[NUM_HMT_CTL_VARS];

extern uint32_t hmt_sthd_cores[];

static inline bool hmt_ctl_flag(enum hmt_ctl_flag_type type)
{
	return READ_ONCE(hmt_ctl_flags[type]);
}

static inline unsigned hmt_ctl_var(enum hmt_ctl_var_type type)
{
	return READ_ONCE(hmt_ctl_vars[type]);
}

static inline void hermit_set_sthd_cores(uint32_t *buf, unsigned long buf_len)
{
	int num_elems = buf_len / sizeof(uint32_t);
	int i;
	pr_err("%s:%d %d, %lu\n", __func__, __LINE__, num_elems, buf_len);
	for (i = 0; i < num_elems; i++)
		hmt_sthd_cores[i] = buf[i];

	printk("set sthd_cores to:\n");
	for (i = 0; i < 96; i++)
		printk("%d", hmt_sthd_cores[i]);
}

/*
 * [Hermit] Speculative IO
 */
struct hmt_spec_counter {
	atomic64_t fail; // keep track of spec IO results
	atomic64_t trial;
	atomic64_t swapin; // not all swapins will do spec IO
	bool curr_state;
};
extern struct hmt_spec_counter hmt_spec_cnter;

bool hmt_enable_spec_io(void);
static inline void hmt_record_spec_fail(void)
{
	atomic64_inc(&hmt_spec_cnter.fail);
}

/*
 * [Hermit] vaddr field for reverse mapping
 */
// vaddr array matches page struct array for phy pages
extern unsigned long *hermit_page_vaddrs;

static inline unsigned long hmt_get_page_vaddr(struct page *page) {
	if (!hermit_page_vaddrs)
		return 0;
	return hermit_page_vaddrs[page_to_pfn(page)];
}

static inline void hmt_set_page_vaddr(struct page *page, unsigned long vaddr) {
	if (!hermit_page_vaddrs)
		return;
	hermit_page_vaddrs[page_to_pfn(page)] = vaddr;
}

/*
 * [Hermit] per-thread virtual address list for swapping
 */
struct vaddr {
	// struct vm_area_struct *vma;
	unsigned long address;
	struct list_head node;
};

struct vpage {
	struct vm_area_struct *vma;
	unsigned long address;
	pte_t *pte;
	spinlock_t *ptl;
	struct page *page;
	struct list_head node;
};

/*
 * interfaces
 */
void page_add_vaddr(pte_t *pte, struct page *page, struct vm_area_struct *vma,
		    unsigned long);

void hmt_update_rft_dist(struct page *page, unsigned rft_dist);

/*
 * manage vpage
 */
struct vpage *create_vpage(struct vm_area_struct *vma, unsigned long address,
			   struct page *page, pte_t *pte);
void free_vpage(struct vpage *vpage);
void free_vpages(struct list_head *vpage_list);

/*
 * async prefetch thread
 */
struct pref_request_queue {
	struct pref_request *reqs;
	atomic_t cnt;
	unsigned size;
	unsigned head;
	unsigned tail;

	spinlock_t lock;
};
#define PREF_REQUEST_QUEUE_SIZE 4096
// per-cpu variable defined & initialized in hermit.c
extern bool pref_request_queue_initialized;

int pref_request_queue_init(unsigned int cpu);
int pref_request_queue_destroy(unsigned int cpu);
int pref_request_enqueue(struct pref_request *pr);
int pref_request_dequeue(struct pref_request *pr);

void hermit_pthd_run(void);
void hermit_dpthd_run(void);
void hermit_pthd_stop(void);

extern void pref_request_copy(struct pref_request *src,
			      struct pref_request *dst);

/*
 * utils
 */
int hermit_page_referenced(struct vpage *vpage, struct page *page,
			   int is_locked, struct mem_cgroup *memcg,
			   unsigned long *vm_flags);
// implemented in rmap.c & pagewalk.c
void hermit_try_to_unmap(struct vpage *vpage, struct page *page,
			 enum ttu_flags flags);

bool hermit_addr_vma_walk(struct page_vma_mapped_walk *pvmw, bool force_lock);
bool hermit_addr_vma_walk_nolock(struct page_vma_mapped_walk *pvmw);

/* [Hermit] for pretty debug logs */

// hard-coded thread names for different applications.
static inline bool is_cassandra_thd(const char *str)
{
	return strncmp(str, "Native-Transpor", 14) == 0 ||
	       strncmp(str, "ReadStage-", 10) == 0 ||
	       strncmp(str, "MutationStage-", 13) == 0;
}

static inline bool is_spark_thd(const char *str)
{
	return strncmp(str, "Executor ", 9) == 0;
}

static inline bool is_gc_thd(const char *str)
{
	return strncmp(str, "G1 Conc", 7) == 0 ||
	       strncmp(str, "GC Thread#", 10) == 0;
	//        strncmp(str, "G1 Refine", 9) == 0 ||
}

static inline bool is_hermit_app(const char *str)
{
	return strncmp(str, "xlinpack", 8) == 0 ||
	       strncmp(str, "xgboost", 7) == 0 ||
	       strncmp(str, "baseline", 8) == 0 ||
	       strncmp(str, "memcached", 9) == 0 ||
	       is_cassandra_thd(str) ||
	       is_spark_thd(str);
}

static inline bool is_specable_thd(const char *str)
{
	return is_gc_thd(str) ||
	       is_cassandra_thd(str) ||
	       is_spark_thd(str);
}

#endif // _LINUX_HERMIT_H

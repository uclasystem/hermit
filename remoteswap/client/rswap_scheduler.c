#include "rswap_scheduler.h"
#include <linux/atomic.h>
#include <linux/delay.h>
#include <linux/kthread.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/interrupt.h>
#include <linux/swap_stats.h>

// global variables
struct rswap_vqlist *global_rswap_vqlist = NULL;
struct rswap_scheduler *global_rswap_scheduler = NULL;

// enable/disable RDMA bandwidth control
bool _bw_control_enabled = true;

void rswap_activate_bw_control(int enable)
{
	_bw_control_enabled = !!enable;
	pr_info("Swap BW control: %s",
	        _bw_control_enabled ? "enabled" : "disabled");
}
EXPORT_SYMBOL(rswap_activate_bw_control);

inline bool is_bw_control_enabled(void) { return _bw_control_enabled; }

// virtual queue and scheduler
int rswap_vqueue_init(struct rswap_vqueue *vqueue)
{
	if (!vqueue) {
		return -EINVAL;
	}

	// invariant: (head + cnt) % max_cnt == tail
	atomic_set(&vqueue->cnt, 0);
	vqueue->max_cnt = RSWAP_VQUEUE_MAX_SIZE;
	vqueue->head = 0;
	vqueue->tail = 0;

	vqueue->reqs = (struct rswap_request *)vmalloc(
	    sizeof(struct rswap_request) * vqueue->max_cnt);

	spin_lock_init(&vqueue->lock);

	return 0;
}
EXPORT_SYMBOL(rswap_vqueue_init);

int rswap_vqueue_destroy(struct rswap_vqueue *vqueue)
{
	if (!vqueue) {
		return -EINVAL;
	}
	vfree(vqueue->reqs);
	memset(vqueue, 0, sizeof(struct rswap_vqueue));
	return 0;
}

// Deprecated
inline void rswap_vqueue_enlarge(struct rswap_vqueue *vqueue)
{
	unsigned long flags;
	unsigned head_len;
	unsigned tail_len;
	struct rswap_request *old_buf;
	spin_lock_irqsave(&vqueue->lock, flags);
	if (vqueue->head < vqueue->tail) {
		head_len = vqueue->tail - vqueue->head;
		old_buf = vqueue->reqs;
		// double the new buffer size
		vqueue->reqs = (struct rswap_request *)vmalloc(
		    sizeof(struct rswap_request) * vqueue->max_cnt * 2);
		// only one part
		memcpy(vqueue->reqs, old_buf + vqueue->head,
		       sizeof(struct rswap_request) * head_len);
	} else {
		head_len = vqueue->max_cnt - vqueue->head;
		tail_len = vqueue->head;
		old_buf = vqueue->reqs;
		// double the new buffer size
		vqueue->reqs = (struct rswap_request *)vmalloc(
		    sizeof(struct rswap_request) * vqueue->max_cnt * 2);
		// head part
		memcpy(vqueue->reqs, old_buf + vqueue->head,
		       sizeof(struct rswap_request) * head_len);
		// tail part
		memcpy(vqueue->reqs + head_len, old_buf,
		       sizeof(struct rswap_request) * tail_len);
	}
	// reset head and tail
	vqueue->head = 0;
	vqueue->tail = vqueue->max_cnt;
	vqueue->max_cnt *= 2;
	vfree(old_buf);
	spin_unlock_irqrestore(&vqueue->lock, flags);
	pr_info("Enlarge vqueue to %u\n", vqueue->max_cnt);
}

int rswap_vqueue_enqueue(struct rswap_vqueue *vqueue,
                         struct rswap_request *request)
{
	int cnt;
	if (!vqueue || !request) {
		return -EINVAL;
	}

	cnt = atomic_read(&vqueue->cnt);
	// cnt = (int)(&vqueue->int);
	if (cnt == vqueue->max_cnt) { // Slow path
		rswap_vqueue_enlarge(vqueue);
	}
	// Fast path. No contention if head != tail
	rswap_request_copy(&vqueue->reqs[vqueue->tail], request);
	vqueue->tail = (vqueue->tail + 1) % vqueue->max_cnt;
	atomic_inc(&vqueue->cnt);

	return 0;
}
EXPORT_SYMBOL(rswap_vqueue_enqueue);

int rswap_vqueue_dequeue(struct rswap_vqueue *vqueue,
                         struct rswap_request **request)
{
	int cnt;

	cnt = atomic_read(&vqueue->cnt);
	if (cnt == 0) { // Fast path 1
		return -1;
	} else if (cnt < vqueue->max_cnt) { // Fast path 2
		*request = &vqueue->reqs[vqueue->head];
		vqueue->head = (vqueue->head + 1) % vqueue->max_cnt;
		return 0;
	} else { // Slow path. cnt == max_cnt here.
		unsigned long flags;
		spin_lock_irqsave(&vqueue->lock, flags);
		*request = &vqueue->reqs[vqueue->head];
		vqueue->head = (vqueue->head + 1) % vqueue->max_cnt;
		// we shouldn't dec counter here.
		// counter is decremented after truly pushing the request to
		// RDMA queue
		spin_unlock_irqrestore(&vqueue->lock, flags);
		return 0;
	}

	return -1;
}
EXPORT_SYMBOL(rswap_vqueue_dequeue);

int rswap_vqueue_drain(int cpu, enum rdma_queue_type type)
{
	unsigned long flags;
	struct rswap_vqueue *vqueue;
	struct rswap_rdma_queue *rdma_queue;

	vqueue = rswap_vqlist_get(cpu, type);
	rdma_queue = get_rdma_queue(&rdma_session_global, cpu, type);
	while (atomic_read(&vqueue->cnt) > 0) {
		if (atomic_read(&rdma_queue->rdma_post_counter) > 0) {
			spin_lock_irqsave(&rdma_queue->cq_lock, flags);
			ib_process_cq_direct(rdma_queue->cq, 16);
			spin_unlock_irqrestore(&rdma_queue->cq_lock, flags);
		}
		// cpu_relax();
		cond_resched();
	}
	return 0;
}
EXPORT_SYMBOL(rswap_vqueue_drain);

int rswap_vqtriple_init(struct rswap_vqtriple *vqtri, int id)
{
	int ret;
	int type;
	if (!vqtri) {
		return -EINVAL;
	}

	vqtri->id = id;
	for (type = 0; type < NUM_QP_TYPE; type++) {
		ret = rswap_vqueue_init(&vqtri->qs[type]);
		if (ret) {
			print_err(ret);
			goto cleanup;
		}
	}
	return 0;

cleanup:
	for (; type >= 0; type--) {
		rswap_vqueue_destroy(&vqtri->qs[type]);
	}
	return -1;
}

int rswap_vqtriple_destroy(struct rswap_vqtriple *vqtri)
{
	int ret;
	int type;

	if (!vqtri) {
		return -EINVAL;
	}

	for (type = 0; type < NUM_QP_TYPE; type++) {
		ret = rswap_vqueue_destroy(&vqtri->qs[type]);
		if (ret)
			print_err(ret);
	}
	return 0;
}

int rswap_vqlist_init(void)
{
	int ret;
	int cpu;

	global_rswap_vqlist = kzalloc(sizeof(struct rswap_vqlist), GFP_ATOMIC);
	global_rswap_vqlist->cnt = online_cores;
	global_rswap_vqlist->vqtris =
	    kzalloc(sizeof(struct rswap_vqtriple) * online_cores, GFP_ATOMIC);
	spin_lock_init(&global_rswap_vqlist->lock);

	for (cpu = 0; cpu < online_cores; cpu++) {
		ret =
		    rswap_vqtriple_init(&global_rswap_vqlist->vqtris[cpu], cpu);
		if (ret) {
			print_err(ret);
			goto cleanup;
		}
	}

	return 0;

cleanup:
	for (; cpu >= 0; cpu--) {
		ret = rswap_vqtriple_destroy(&global_rswap_vqlist->vqtris[cpu]);
		if (ret)
			print_err(ret);
	}
	kfree(global_rswap_vqlist->vqtris);
	kfree(global_rswap_vqlist);
	return ret;
}

int rswap_vqlist_destroy(void)
{
	int ret = 0;
	int cpu;
	for (cpu = 0; cpu < online_cores; cpu++) {
		ret = rswap_vqtriple_destroy(&global_rswap_vqlist->vqtris[cpu]);
		if (ret)
			print_err(ret);
	}
	kfree(global_rswap_vqlist->vqtris);
	kfree(global_rswap_vqlist);
	return ret;
}

inline struct rswap_vqueue *rswap_vqlist_get(int qid, enum rdma_queue_type type)
{
	// fast path. [WARNING] only static allocated array here
	return &global_rswap_vqlist->vqtris[qid].qs[type];
}

inline struct rswap_vqtriple *rswap_vqlist_get_triple(int qid)
{
	return &global_rswap_vqlist->vqtris[qid];
}

int rswap_proc_init(struct rswap_proc *proc, char *name)
{
	unsigned long flag;
	if (!proc) {
		return -EINVAL;
	}

	strcpy(proc->name, name);
	proc->bw_weight = -1; // -1 for uninitialized
	proc->num_threads = 0;
	memset(proc->cores, 0, sizeof(proc->cores));
	memset(proc->priorities, 0, sizeof(proc->priorities));
	memset(proc->sent_pkts, 0, sizeof(proc->sent_pkts));
	memset(proc->total_pkts, 0, sizeof(proc->total_pkts));

	spin_lock_irqsave(&global_rswap_scheduler->lock, flag);
	list_add_tail(&proc->list_node, &global_rswap_scheduler->proc_list);
	spin_unlock_irqrestore(&global_rswap_scheduler->lock, flag);

	pr_info("init proc %s in rswap", proc->name);
	return 0;
}

int rswap_proc_destroy(struct rswap_proc *proc)
{
	unsigned long flag;
	if (!proc) {
		return -EINVAL;
	}

	spin_lock_irqsave(&global_rswap_scheduler->lock, flag);
	list_del(&proc->list_node);
	spin_unlock_irqrestore(&global_rswap_scheduler->lock, flag);

	rswap_proc_clr_weight(proc);
	pr_info("destroy proc %s in rswap", proc->name);
	kfree(proc);
	return 0;
}

int rswap_proc_clr_weight(struct rswap_proc *proc)
{
	int cpu;
	if (!proc) {
		return -EINVAL;
	}

	for (cpu = 0; cpu < online_cores; cpu++) {
		if (proc->cores[cpu]) {
			global_rswap_vqlist->vqtris[cpu].proc = NULL;
		}
	}
	if (proc->bw_weight > 0)
		global_rswap_scheduler->total_bw_weight /= proc->bw_weight;
	proc->bw_weight = -1; // reset to uninitialized state
	memset(proc->cores, 0, sizeof(proc->cores));
	memset(proc->priorities, 0, sizeof(proc->priorities));
	memset(proc->sent_pkts, 0, sizeof(proc->sent_pkts));

	pr_info("clear BW weight of proc %s, total BW weight now: %d",
	        proc->name, global_rswap_scheduler->total_bw_weight);
	return 0;
}

int rswap_proc_set_weight(struct rswap_proc *proc, int bw_weight,
                          int num_threads, int *cores)
{
	int thd;
	if (!proc || !cores) {
		return -EINVAL;
	}

	if (proc->bw_weight != -1) { // already initialized
		rswap_proc_clr_weight(proc);
	}

	proc->bw_weight = bw_weight;
	proc->num_threads = num_threads;
	memcpy(proc->cores, cores, sizeof(int) * num_threads);
	for (thd = 0; thd < num_threads; thd++) {
		global_rswap_vqlist->vqtris[cores[thd]].proc = proc;
		pr_info("Bind core %d to proc %s", cores[thd], proc->name);
	}
	if (bw_weight > 0)
		global_rswap_scheduler->total_bw_weight *= bw_weight;

	pr_info("set BW weight of proc %s, total BW weight now: %d", proc->name,
	        global_rswap_scheduler->total_bw_weight);
	return 0;
}

int rswap_proc_get_total_pkts(struct rswap_proc *proc, char *buf)
{
	int type;
	if (!proc || !buf) {
		return -EINVAL;
	}

	memcpy(buf, proc->name, RSWAP_PROC_NAME_LEN);
	buf += RSWAP_PROC_NAME_LEN;
	for (type = 0; type < NUM_QP_TYPE; type++) {
		*(int *)buf = proc->total_pkts[type];
		buf += sizeof(int);
	}
	return 0;
}

int rswap_proc_clr_total_pkts(struct rswap_proc *proc)
{
	int type;
	if (!proc) {
		return -EINVAL;
	}
	for (type = 0; type < NUM_QP_TYPE; type++) {
		proc->total_pkts[type] = 0;
	}
	return 0;
}

void rswap_get_all_procs_total_pkts(int *num_procs, char *buf)
{
	int _num_procs = 0;
	struct rswap_proc *proc, *tmp;

	list_for_each_entry_safe(proc, tmp, &global_rswap_scheduler->proc_list,
	                         list_node)
	{
		rswap_proc_get_total_pkts(proc, buf);
		buf += RSWAP_PROC_NAME_LEN + sizeof(proc->total_pkts);
		_num_procs++;
	}
	*num_procs = _num_procs;
}
EXPORT_SYMBOL(rswap_get_all_procs_total_pkts);

inline void rswap_proc_send_pkts_inc(struct rswap_proc *proc,
                                     enum rdma_queue_type type)
{
	if (!proc)
		return;
	atomic_inc(&proc->sent_pkts[type]);
	proc->total_pkts[type]++;
	atomic_inc(&global_rswap_scheduler->total_sent_pkts[type]);
	// pr_info("INC proc: %s, sent_pkts[%d] = %d, total sent pkts: %d",
	//         proc->name, type, atomic_read(&proc->sent_pkts[type]),
	//         atomic_read(&global_rswap_scheduler->total_sent_pkts[type]));
}

inline void rswap_proc_send_pkts_dec(struct rswap_proc *proc,
                                     enum rdma_queue_type type)
{
	if (!proc)
		return;
	atomic_dec(&proc->sent_pkts[type]);
	atomic_dec(&global_rswap_scheduler->total_sent_pkts[type]);
	// pr_info("DEC proc: %s, sent_pkts[%d] = %d, total sent pkts: %d",
	//         proc->name, type, atomic_read(&proc->sent_pkts[type]),
	//         atomic_read(&global_rswap_scheduler->total_sent_pkts[type]));
}

void rswap_register_procs(void)
{
#ifndef ADC_MAX_NUM_CORES
#define ADC_MAX_NUM_CORES 96
#endif

#define ADC_NUM_APPS 4
	int i;
	// 48 cores
	struct rswap_proc *procs[ADC_NUM_APPS];
	char *names[4] = { "snappy", "memcached", "xgboost", "spark" };
	int num_threads[ADC_NUM_APPS] = { 1, 4, 16, 24 };
	// below we assume hyperthread disabled to avoid huge frame size
	int cores[ADC_NUM_APPS][ADC_MAX_NUM_CORES / 2] = {
		{ 0 },
		{ 2, 4, 6, 8 },
		{ 10, 12, 14, 16, 18, 20, 22, 24, 26, 28, 30, 32, 34, 36, 38,
		  40 },
		{ 1,  3,  5,  7,  9,  11, 13, 15, 17, 19, 21, 23,
		  25, 27, 29, 31, 33, 35, 37, 39, 41, 43, 45, 47 }
	};
	int weights[ADC_NUM_APPS] = { 1, 1, 1, 1 };
	// int weights[ADC_NUM_APPS] = { 17, 7, 38, 38 };

	for (i = 0; i < ADC_NUM_APPS; i++) {
		procs[i] = kzalloc(sizeof(struct rswap_proc), GFP_ATOMIC);
		rswap_proc_init(procs[i], names[i]);
		rswap_proc_set_weight(procs[i], weights[i], num_threads[i],
				      cores[i]);
	}

#if defined(RSWAP_KERNEL_SUPPORT) && defined(RSWAP_RDMA_BW_CONTROL)
	// register BW control & monitor functions to kernel
	set_swap_bw_control = rswap_activate_bw_control;
	get_all_procs_swap_pkts = rswap_get_all_procs_total_pkts;
	pr_info("Swap RDMA bandwidth control functions registered.");
#else
	pr_info("Kernel doesn't support swap RDMA bandwidth control.");
#endif // RSWAP_KERNEL_SUPPORT
#undef ADC_NUM_APPS
}

void rswap_deregister_procs(void)
{
	struct rswap_proc *proc, *tmp;

	list_for_each_entry_safe(proc, tmp, &global_rswap_scheduler->proc_list,
	                         list_node)
	{
		rswap_proc_destroy(proc);
	}

#if defined(RSWAP_KERNEL_SUPPORT) && defined(RSWAP_RDMA_BW_CONTROL)
	set_swap_bw_control = NULL;
	get_all_procs_swap_pkts = NULL;
	pr_info("Swap RDMA bandwidth control functions deregistered.");
#else
	pr_info("Kernel doesn't support swap RDMA bandwidth control. Do nothing.");
#endif // RSWAP_KERNEL_SUPPORT
}

int rswap_scheduler_init(void)
{
	struct rdma_session_context *rdma_session = &rdma_session_global;
	int ret = 0;

	pr_info("%s starts.\n", __func__);
	// init physical queues (nothing here)

	// init virual queues
	pr_info("%s inits vqueues.\n", __func__);
	ret = rswap_vqlist_init();
	if (ret) {
		print_err(ret);
		return ret;
	}

	// start scheduler thread
	global_rswap_scheduler =
	    (struct rswap_scheduler *)vmalloc(sizeof(struct rswap_scheduler));

	global_rswap_scheduler->total_bw_weight = 1;
	memset(global_rswap_scheduler->total_sent_pkts, 0,
	       sizeof(global_rswap_scheduler->total_sent_pkts));
	spin_lock_init(&global_rswap_scheduler->lock);
	INIT_LIST_HEAD(&global_rswap_scheduler->proc_list);

	global_rswap_scheduler->vqlist = global_rswap_vqlist;
	global_rswap_scheduler->rdma_session = rdma_session;
	global_rswap_scheduler->scher_thd =
	    kthread_create(rswap_scheduler_thread,
	                   (void *)global_rswap_scheduler, "RSWAP scheduler");
	kthread_bind(global_rswap_scheduler->scher_thd, RSWAP_SCHEDULER_CORE);
	wake_up_process(global_rswap_scheduler->scher_thd);
	pr_info("%s launches scheduler thd.\n", __func__);

	// for simulation
	rswap_register_procs();
	return 0;
}

int rswap_scheduler_stop(void)
{
	int ret = 0;
	// int core;
	if (!global_rswap_scheduler) {
		return -EINVAL;
	}

	// for simulation
	rswap_deregister_procs();

	ret = kthread_stop(global_rswap_scheduler->scher_thd);
	if (global_rswap_vqlist) {
		rswap_vqlist_destroy();
		global_rswap_vqlist = NULL;
	}
	vfree(global_rswap_scheduler);
	pr_info("%s, line %d, after kthread stop\n", __func__, __LINE__);
	return ret;
}

static inline bool poll_all_vqueues(enum rdma_queue_type type)
{
	int ret;
	bool find;
	int cpu;

	struct rswap_vqtriple *vqtri;
	struct rswap_proc *proc, *tmp;
	struct rswap_vqueue *vqueue;
	struct rswap_request *vrequest;

	int min_active_pkts = 1 << 30;
	int min_weight = 1 << 30;
	int num_active_procs = 0;
	struct rswap_proc *baseline_proc = NULL;

	find = false;

	list_for_each_entry_safe(proc, tmp, &global_rswap_scheduler->proc_list,
	                         list_node)
	{
		int thd;
		int active_pkts = 0;
		proc->ongoing_pkts = atomic_read(&proc->sent_pkts[type]);
		active_pkts += proc->ongoing_pkts;
		for (thd = 0; thd < proc->num_threads; thd++) {
			vqueue = rswap_vqlist_get(proc->cores[thd], type);
			active_pkts += atomic_read(&vqueue->cnt);
		}
		if (active_pkts > 0) {
			int curr_weight;
			num_active_procs++;
			curr_weight = global_rswap_scheduler->total_bw_weight /
			              proc->bw_weight * active_pkts;
			if (curr_weight < min_weight) {
				min_weight = curr_weight;
				min_active_pkts = active_pkts;
				baseline_proc = proc;
			}
			// pr_info("%s active pkts %d", proc->name, active_pkts);
		}
	}

	if (num_active_procs == 0) {
		// return find;
	} else if (num_active_procs == 1) { // baseline_proc must exist
		int thd;
		for (thd = 0; thd < baseline_proc->num_threads; thd++) {
			cpu = baseline_proc->cores[thd];
			vqueue = rswap_vqlist_get(cpu, type);
			ret = rswap_vqueue_dequeue(vqueue, &vrequest);
			if (ret == 0) {
				rswap_proc_send_pkts_inc(baseline_proc, type);
				rswap_rdma_send(cpu, vrequest->offset,
				                vrequest->page, type);

				atomic_dec(&vqueue->cnt);
				find = true;
				// break;
				// pr_info("get request single %d %s",
				//         min_active_pkts, baseline_proc->name);
			} else if (ret != -1) {
				print_err(ret);
			}
		}
		// return find;
	} else if (num_active_procs > 1) {
		list_for_each_entry_safe(
		    proc, tmp, &global_rswap_scheduler->proc_list, list_node)
		{
			int budget;
			int prev_budget;
			int thd;
			if (is_bw_control_enabled()) {
				budget = min_active_pkts * proc->bw_weight /
						 baseline_proc->bw_weight -
					 proc->ongoing_pkts;
			} else {
				budget = proc->num_threads;
			}
			if (proc != baseline_proc)
				continue;
			prev_budget = budget;
			thd = 0;
			// pr_info("get request corun %d %d %s\n", budget,
			//         proc->ongoing_pkts, proc->name);
			while (budget > 0) {
				cpu = proc->cores[thd];
				vqtri = rswap_vqlist_get_triple(cpu);
				vqueue = &vqtri->qs[type];
				ret = rswap_vqueue_dequeue(vqueue, &vrequest);
				if (ret == 0) {
					rswap_proc_send_pkts_inc(proc, type);
					rswap_rdma_send(cpu, vrequest->offset,
					                vrequest->page, type);
					atomic_dec(&vqueue->cnt);
					budget--;
					find = true;
				} else if (ret != -1) {
					print_err(ret);
				}
				thd++;
				if (thd == proc->num_threads) {
					// break when proc is idle
					if (budget == prev_budget)
						break;
					thd = 0;
					prev_budget = budget;
				}
			}
		}
		// return find;
	}

	return find;
}

static inline void poll_idle_cores(void)
{
	int cpu;
	// unregistered cores
	for (cpu = 0; cpu < online_cores; cpu++) {
		int ret;
		int type;
		struct rswap_vqtriple *vqtri;
		struct rswap_vqueue *vqueue;
		struct rswap_request *vrequest;

		vqtri = rswap_vqlist_get_triple(cpu);
		if (cpu == RSWAP_SCHEDULER_CORE) // skip scheduler's core
			continue;
		if (vqtri->proc) // only schedule unregistered queues here.
			continue;
		for (type = 0; type < NUM_QP_TYPE; type++) {
			vqueue = rswap_vqlist_get(cpu, type);
			ret = rswap_vqueue_dequeue(vqueue, &vrequest);
			if (ret == 0) {
				rswap_rdma_send(cpu, vrequest->offset,
				                vrequest->page, type);
				atomic_dec(&vqueue->cnt);
				// pr_info("get store request\n");
			} else if (ret != -1) {
				print_err(ret);
			}
		}
	}
}

int rswap_scheduler_thread(void *args)
{
	struct rswap_vqlist *vqlist;
	struct rdma_session_context *rdma_session;

	vqlist = ((struct rswap_scheduler *)args)->vqlist;
	rdma_session = ((struct rswap_scheduler *)args)->rdma_session;
	while (!kthread_should_stop() &&
	       (!vqlist || !rdma_session || !vqlist->cnt)) {
		usleep_range(5000, 5001); // 5ms
		cond_resched();
	}

	pr_info("RSWAP scheduler starts polling...\n");
	// for fake vqueue simulation
	while (!kthread_should_stop() && vqlist->cnt < online_cores) {
		usleep_range(5000, 5001);
		cond_resched();
	}
	pr_info("RSWAP scheduler gets all vqueues.");

	while (!kthread_should_stop()) {
		int repeat;
		bool global_find;

		global_find = false;
		for (repeat = 0; repeat < 10; repeat++) {
			bool store_find, load_find;
			store_find = true;
			load_find = true;
			while (store_find || load_find) {
				store_find = poll_all_vqueues(QP_STORE);
				global_find |= store_find;

				load_find = poll_all_vqueues(QP_LOAD_SYNC);
				global_find |= load_find;
			}

			load_find = poll_all_vqueues(QP_LOAD_ASYNC);
			global_find |= load_find;
		}
		if (!global_find) {
			poll_idle_cores();

			// avoid CPU stuck
// #ifdef RSWAP_KERNEL_SUPPORT
// 			preempt_disable();
// 			if (local_softirq_pending())
// 				do_softirq();
// 			preempt_enable();
// #else
// 			usleep_range(1, 2);
// #endif // RSWAP_KERNEL_SUPPORT
			// cond_resched();
		}
		usleep_range(1, 2);
		cond_resched();
	}

	return 0;
}
EXPORT_SYMBOL(rswap_scheduler_thread);

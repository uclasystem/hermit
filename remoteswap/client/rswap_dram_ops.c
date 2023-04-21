#include <linux/swap_stats.h>

#include "rswap_dram.h"
#include "rswap_ops.h"
#include "utils.h"

/**
 * Synchronously write data to memory server.
 *
 * return
 *  0 : success
 *  non-zero : failed.
 *
 */
int rswap_frontswap_store(unsigned type, pgoff_t swap_entry_offset,
			  struct page *page)
{
	int ret = 0;

	ret = rswap_dram_write(page, swap_entry_offset << PAGE_SHIFT);
	if (unlikely(ret)) {
		pr_err("could not read page remotely\n");
		goto out;
	}
out:
	return ret;
}

int rswap_frontswap_store_on_core(unsigned type, pgoff_t swap_entry_offset,
				  struct page *page, int core)
{
	return rswap_frontswap_store(type, swap_entry_offset, page);
}

/**
 *
 * Synchronously read data from memory server.
 *
 *
 * return:
 *  0 : success
 *  non-zero : failed.
 */
int rswap_frontswap_load(unsigned type, pgoff_t swap_entry_offset,
			 struct page *page)
{
	int ret = 0;

	ret = rswap_dram_read(page, swap_entry_offset << PAGE_SHIFT);
	if (unlikely(ret)) {
		pr_err("could not read page remotely\n");
		goto out;
	}

out:
	return ret;
}

int rswap_frontswap_load_async(unsigned type, pgoff_t swap_entry_offset,
			       struct page *page)
{
	int ret = 0;

	ret = rswap_dram_read(page, swap_entry_offset << PAGE_SHIFT);
	if (unlikely(ret)) {
		pr_err("could not read page remotely\n");
		goto out;
	}

out:
	return ret;
}

/*
 * return 0 to indicate load|store has finished.
 */
int rswap_frontswap_poll_load(int cpu)
{
	return 0;
}
int rswap_frontswap_poll_store(int cpu)
{
	return 0;
}
/*
 * We don't want to mislead prefetcher to stop too early, so let
 * peek_[load|store] return non-success here.
 */
int rswap_frontswap_peek_load(int cpu)
{
	return 1;
}
int rswap_frontswap_peek_store(int cpu)
{
	return 1;
}

static void rswap_invalidate_page(unsigned type, pgoff_t offset)
{
#ifdef DEBUG_MODE_DETAIL
	pr_info("%s, remove page_virt addr 0x%lx\n", __func__,
		offset << PAGE_OFFSET);
#endif
	return;
}

static void rswap_invalidate_area(unsigned type)
{
#ifdef DEBUG_MODE_DETAIL
	pr_warn("%s, remove the pages of area 0x%x ?\n", __func__, type);
#endif
	return;
}

static void rswap_frontswap_init(unsigned type)
{
}

static struct frontswap_ops rswap_frontswap_ops = {
	.init = rswap_frontswap_init,
	.store = rswap_frontswap_store,
	.load = rswap_frontswap_load,
	.invalidate_page = rswap_invalidate_page,
	.invalidate_area = rswap_invalidate_area,
#ifdef RSWAP_KERNEL_SUPPORT
	.load_async = rswap_frontswap_load_async,
	.poll_load = rswap_frontswap_poll_load,
#if RSWAP_KERNEL_SUPPORT >= 2
	.store_on_core = rswap_frontswap_store_on_core,
	.poll_store = rswap_frontswap_poll_store,
#endif // RSWAP_KERNEL_SUPPORT >= 2
#if RSWAP_KERNEL_SUPPORT >= 3
	.peek_load = rswap_frontswap_peek_load,
	.peek_store = rswap_frontswap_peek_store,
#endif // RSWAP_KERNEL_SUPPORT >= 3
#endif
};

int rswap_register_frontswap(void)
{
	frontswap_register_ops(&rswap_frontswap_ops);
	pr_info("frontswap module loaded\n");
	return 0;
}

int rswap_replace_frontswap(void)
{
	frontswap_ops->init = rswap_frontswap_ops.init;
	frontswap_ops->store = rswap_frontswap_ops.store;
	frontswap_ops->load = rswap_frontswap_ops.load;
	frontswap_ops->invalidate_page = rswap_frontswap_ops.invalidate_page,
	frontswap_ops->invalidate_area = rswap_frontswap_ops.invalidate_area,
#ifdef RSWAP_KERNEL_SUPPORT
	frontswap_ops->load_async = rswap_frontswap_ops.load_async;
	frontswap_ops->poll_load = rswap_frontswap_ops.poll_load;
#if RSWAP_KERNEL_SUPPORT >= 2
	frontswap_ops->store_on_core = rswap_frontswap_ops.store_on_core;
	frontswap_ops->poll_store = rswap_frontswap_ops.poll_store;
#endif // RSWAP_KERNEL_SUPPORT >= 2
#if RSWAP_KERNEL_SUPPORT >= 3
	frontswap_ops->peek_load = rswap_frontswap_ops.peek_load;
	frontswap_ops->peek_store = rswap_frontswap_ops.peek_store;
#endif // RSWAP_KERNEL_SUPPORT >= 3
#endif
	pr_info("frontswap ops replaced\n");
	return 0;
}

void rswap_deregister_frontswap(void)
{
#ifdef RSWAP_KERNEL_SUPPORT
	frontswap_ops->init = NULL;
	frontswap_ops->store = NULL;
	frontswap_ops->load = NULL;
	frontswap_ops->load_async = NULL;
	frontswap_ops->poll_load = NULL;
#else
	frontswap_ops->init = NULL;
	frontswap_ops->store = NULL;
	frontswap_ops->load = NULL;
	frontswap_ops->poll_load = NULL;
#endif
	pr_info("frontswap ops deregistered\n");
}

int rswap_client_init(char *server_ip, int server_port, int mem_size)
{
	return rswap_init_local_dram(mem_size);
}

void rswap_client_exit(void)
{
	rswap_remove_local_dram();
}

#include <linux/swap_stats.h>
#include <linux/syscalls.h>
#include <linux/printk.h>
#include <linux/hermit.h>

SYSCALL_DEFINE0(reset_swap_stats)
{
	reset_adc_swap_stats();
	reset_adc_pf_breakdown();
	return 0;
}

SYSCALL_DEFINE3(get_swap_stats, int __user *, ondemand_swapin_num, int __user *,
		prefetch_swapin_num, int __user *, hit_on_prefetch_num)
{
	int dmd_swapin_num;
	int prf_swapin_num;
	int hit_prftch_num;
	int swapout_num;

	report_adc_time_stat();
	report_adc_counters();
	report_adc_pf_breakdown(NULL);

	dmd_swapin_num = get_adc_counter(ADC_ONDEMAND_SWAPIN);
	prf_swapin_num = get_adc_counter(ADC_PREFETCH_SWAPIN);
	hit_prftch_num = get_adc_counter(ADC_HIT_ON_PREFETCH);
	swapout_num = get_adc_counter(ADC_SWAPOUT);

	put_user(dmd_swapin_num, ondemand_swapin_num);
	put_user(prf_swapin_num, prefetch_swapin_num);
	put_user(hit_prftch_num, hit_on_prefetch_num);

	return 0;
}

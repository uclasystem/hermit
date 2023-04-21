/**
 * adc_timer.h - high-resolution timers for ADC|RMGrid|Canvas|Hermit
 */

#ifndef _LINUX_ADC_TIMER_H
#define _LINUX_ADC_TIMER_H

#include <linux/types.h>
#include "linux/adc_macros.h"

// time utils
// reference cycles.
// #1, Fix the clock cycles of CPU.
// #2, Divided by CPU frequency to calculate the wall time.
// 500 cycles/ 4.0GHz * 10^9 ns = 500/4.0 ns = xx ns.
// Use "__asm__" in header files (".h") and "asm" in source files (".c")
#ifndef ADC_VM
static inline uint64_t get_cycles_start(void)
{
	uint32_t cycles_high, cycles_low;
	__asm__ __volatile__("xorl %%eax, %%eax\n\t"
			     "CPUID\n\t"
			     "RDTSC\n\t"
			     "mov %%edx, %0\n\t"
			     "mov %%eax, %1\n\t"
			     : "=r"(cycles_high), "=r"(cycles_low)::"%rax",
			       "%rbx", "%rcx", "%rdx");
	return ((uint64_t)cycles_high << 32) + (uint64_t)cycles_low;
}

// More strict than get_cycles_start since "RDTSCP; read registers; CPUID"
// gurantee all instructions before are executed and all instructions after
// are not speculativly executed
// Refer to https://www.intel.com/content/dam/www/public/us/en/documents/white-papers/ia-32-ia-64-benchmark-code-execution-paper.pdf
static inline uint64_t get_cycles_end(void)
{
	uint32_t cycles_high, cycles_low;
	__asm__ __volatile__("RDTSCP\n\t"
			     "mov %%edx, %0\n\t"
			     "mov %%eax, %1\n\t"
			     "xorl %%eax, %%eax\n\t"
			     "CPUID\n\t"
			     : "=r"(cycles_high), "=r"(cycles_low)::"%rax",
			       "%rbx", "%rcx", "%rdx");
	return ((uint64_t)cycles_high << 32) + (uint64_t)cycles_low;
}
#else // ADC_VM
static inline uint64_t get_cycles_start(void)
{
	uint32_t cycles_high, cycles_low;
	__asm__ __volatile__("RDTSCP\n\t"
			     "mov %%edx, %0\n\t"
			     "mov %%eax, %1\n\t"
			     "xorl %%eax, %%eax\n\t"
			     : "=r"(cycles_high), "=r"(cycles_low)::"%rax",
			       "%rbx", "%rcx", "%rdx");
	return ((uint64_t)cycles_high << 32) + (uint64_t)cycles_low;
}

static inline uint64_t get_cycles_end(void)
{
	uint32_t cycles_high, cycles_low;
	__asm__ __volatile__("RDTSCP\n\t"
			     "mov %%edx, %0\n\t"
			     "mov %%eax, %1\n\t"
			     "xorl %%eax, %%eax\n\t"
			     : "=r"(cycles_high), "=r"(cycles_low)::"%rax",
			       "%rbx", "%rcx", "%rdx");
	return ((uint64_t)cycles_high << 32) + (uint64_t)cycles_low;
}
#endif // ADC_VM

static inline uint64_t get_cycles_light(void)
{
	uint32_t cycles_high, cycles_low;
	__asm__ __volatile__("RDTSCP\n\t"
			     "mov %%edx, %0\n\t"
			     "mov %%eax, %1\n\t"
			     "xorl %%eax, %%eax\n\t"
			     : "=r"(cycles_high), "=r"(cycles_low)::"%rax",
			       "%rbx", "%rcx", "%rdx");
	return ((uint64_t)cycles_high << 32) + (uint64_t)cycles_low;
}

static inline uint64_t timer_start_in_us(void)
{
	return get_cycles_start() / RMGRID_CPU_FREQ;
}
static inline uint64_t timer_end_in_us(void)
{
	return get_cycles_end() / RMGRID_CPU_FREQ;
}

static inline uint64_t pf_cycles_start(void)
{
#ifdef ADC_PROFILE_PF_BREAKDOWN
	return get_cycles_start();
#else
	return 0;
#endif
}
static inline uint64_t pf_cycles_end(void)
{
#ifdef ADC_PROFILE_PF_BREAKDOWN
	return get_cycles_end();
#else
	return 0;
#endif
}

#endif /* _LINUX_ADC_TIMER_H */

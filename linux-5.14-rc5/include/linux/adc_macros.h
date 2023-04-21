/**
 * adc_macros.h - global macros for Canvas|Hermit
 */

#ifndef _LINUX_ADC_MACROS_H
#define _LINUX_ADC_MACROS_H

#define RSWAP_KERNEL_SUPPORT 3
#define RMGRID_CPU_FREQ 2100 // in MHz
// # maximum memory supported in KB. For now support 400GB at most.
#define RMGRID_MAX_MEM (400UL * 1024 * 1024 * 1024)

#define ADC_PROFILE_PF_BREAKDOWN
// #define ADC_VM


/* utils */
#define adc_safe_div(n, base) ((base) ? ((n) / (base)) : 0)

#endif /* _LINUX_ADC_MACROS_H */

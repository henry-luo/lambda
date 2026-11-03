#ifndef C2MIR_BENCH_KERNEL_TIMER_H
#define C2MIR_BENCH_KERNEL_TIMER_H

/* Optional execution boundaries follow each Lambda benchmark's clock() pair. */
#ifdef C2MIR_KERNEL_TIMING
#include "bench_clock.h"
#define BENCH_KERNEL_BEGIN() double bench_kernel_started = c2mir_now_ms()
#define BENCH_KERNEL_END() double bench_kernel_elapsed = c2mir_now_ms() - bench_kernel_started
#define BENCH_KERNEL_REPORT() printf("__TIMING__:%.6f\n", bench_kernel_elapsed)
#else
#define BENCH_KERNEL_BEGIN() ((void)0)
#define BENCH_KERNEL_END() ((void)0)
#define BENCH_KERNEL_REPORT() ((void)0)
#endif
#endif

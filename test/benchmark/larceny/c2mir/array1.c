/* Native C2MIR port of larceny/array1.ls. */
extern int printf(const char *, ...);
#include "../../c2mir/bench_kernel_timer.h"
int main(void) {
    BENCH_KERNEL_BEGIN();
    int values[10000]; int i; int iter; int total = 0;
    for (i = 0; i < 10000; i++) values[i] = i;
    for (iter = 0; iter < 100; iter++) { int sum = 0; for (i = 0; i < 10000; i++) sum += values[i]; total = sum; }
    BENCH_KERNEL_END();
    printf(total == 49995000 ? "array1: PASS\n" : "array1: FAIL result=%d\n", total);
    BENCH_KERNEL_REPORT();
    return total != 49995000;
}

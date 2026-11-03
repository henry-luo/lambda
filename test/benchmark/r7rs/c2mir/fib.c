/* Native C2MIR port of r7rs/fib2.ls. */
extern int printf(const char *, ...);
#include "../../c2mir/bench_kernel_timer.h"

static int fib(int n) {
    if (n < 2) return n;
    return fib(n - 1) + fib(n - 2);
}

int main(void) {
    BENCH_KERNEL_BEGIN();
    int result = fib(27);
    BENCH_KERNEL_END();
    printf(result == 196418 ? "fib: PASS\n" : "fib: FAIL result=%d\n", result);
    BENCH_KERNEL_REPORT();
    return result != 196418;
}

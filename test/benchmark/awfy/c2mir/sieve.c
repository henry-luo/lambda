/* Native C2MIR port of awfy/sieve2.ls. */
extern int printf(const char *, ...);
#include "../../c2mir/bench_kernel_timer.h"

int main(void) {
    BENCH_KERNEL_BEGIN();
    int flags[5000];
    int i;
    int prime_count = 0;
    for (i = 0; i < 5000; i++) flags[i] = 1;
    for (i = 2; i <= 5000; i++) {
        int k;
        if (!flags[i - 1]) continue;
        prime_count++;
        for (k = i + i; k <= 5000; k += i) flags[k - 1] = 0;
    }
    BENCH_KERNEL_END();
    printf(prime_count == 669 ? "Sieve: PASS\n" : "Sieve: FAIL result=%d\n", prime_count);
    BENCH_KERNEL_REPORT();
    return prime_count != 669;
}

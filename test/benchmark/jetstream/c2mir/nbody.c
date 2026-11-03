/* Matched JetStream workload: eight fresh systems for each of four step counts. */
extern int printf(const char *, ...);
extern double floor(double);
#include "../../c2mir/bench_kernel_timer.h"
#include "../../c2mir/nbody_kernel.h"

int main(void) {
    BENCH_KERNEL_BEGIN();
    int iteration, n, pass = 1;
    for (iteration = 0; iteration < 8; iteration++) {
        double total = 0.0;
        for (n = 3; n <= 24; n *= 2) {
            double initial;
            double final = nbody_system(n * 100, &initial);
            total += initial + final;
        }
        if (floor(total * -10000000.0) != 13524862) pass = 0;
    }
    BENCH_KERNEL_END();
    printf(pass ? "nbody: PASS\n" : "nbody: FAIL\n");
    BENCH_KERNEL_REPORT();
    return !pass;
}

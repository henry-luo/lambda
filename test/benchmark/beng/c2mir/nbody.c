/* Matched BENG workload: setup, initial energy, 36,000 steps and final energy. */
extern int printf(const char *, ...);
#include "../../c2mir/bench_kernel_timer.h"
#include "../../c2mir/nbody_kernel.h"

int main(void) {
    BENCH_KERNEL_BEGIN();
    C2MIR_NBODY_INITIALIZE();
    printf("%.9f\n", energy(bx, by, bz, bvx, bvy, bvz, mass));
    for (i = 0; i < 36000; i++) advance(bx, by, bz, bvx, bvy, bvz, mass);
    printf("%.9f\n", energy(bx, by, bz, bvx, bvy, bvz, mass));
    BENCH_KERNEL_END();
    BENCH_KERNEL_REPORT();
    return 0;
}

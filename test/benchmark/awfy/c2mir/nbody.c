/* Native C2MIR port of awfy/nbody2.ls. */
extern int printf(const char *, ...);
#include "../../c2mir/bench_kernel_timer.h"
#include "../../c2mir/nbody_kernel.h"

int main(void) {
    BENCH_KERNEL_BEGIN();
    C2MIR_NBODY_INITIALIZE();
    for (i = 0; i < 36000; i++) advance(bx, by, bz, bvx, bvy, bvz, mass);
    result = energy(bx, by, bz, bvx, bvy, bvz, mass);
    BENCH_KERNEL_END();
    check = (int) (-result * 10000000.0);
    printf(check == 1690142 ? "NBody: PASS\n" : "NBody: FAIL check=%d\n", check);
    BENCH_KERNEL_REPORT();
    return check != 1690142;
}

#include "../../c2mir/bench_kernel_timer.h"
/* Native C2MIR port of r7rs/cpstak2.ls (the suite's two direct tak runs). */
extern int printf(const char *, ...);

static int tak(int x, int y, int z) {
    int a;
    int b;
    int c;
    if (y >= x) return z;
    a = tak(x - 1, y, z);
    b = tak(y - 1, z, x);
    c = tak(z - 1, x, y);
    return tak(a, b, c);
}

int main(void) {
    BENCH_KERNEL_BEGIN();
    int result = tak(18, 12, 6);
    result = tak(18, 12, 6);
    BENCH_KERNEL_END();
    printf(result == 7 ? "cpstak: PASS\n" : "cpstak: FAIL result=%d\n", result);
    BENCH_KERNEL_REPORT();
    return result != 7;
}

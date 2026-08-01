/* Timing entry point for the native C2MIR benchmark ports.
 *
 * The ports each define `int main(void)`. Compiling them together with this
 * file under `-Dmain=c2mir_bench_body` renames the port's entry point, so this
 * file supplies the real `main` and brackets the workload with a wall-clock
 * measurement. The `#undef main` below is what keeps this file's own `main`
 * from being renamed by the same -D.
 *
 * Timing only the body is what makes these numbers comparable with the Lambda
 * `__TIMING__` values: both exclude process startup and JIT compilation, which
 * for c2m would otherwise dominate every sub-100ms benchmark.
 */
#undef main

#include <sys/time.h>

extern int printf(const char *, ...);
extern int c2mir_bench_body(void);

static double c2mir_now_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, 0);
    return (double) tv.tv_sec * 1000.0 + (double) tv.tv_usec / 1000.0;
}

int main(void) {
    double started = c2mir_now_ms();
    int rc = c2mir_bench_body();
    double elapsed = c2mir_now_ms() - started;
    printf("__TIMING__:%.6f\n", elapsed);
    return rc;
}

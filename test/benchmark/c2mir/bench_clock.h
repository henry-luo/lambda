#ifndef C2MIR_BENCH_CLOCK_H
#define C2MIR_BENCH_CLOCK_H
#include <sys/time.h>

static double c2mir_now_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, 0);
    return (double) tv.tv_sec * 1000.0 + (double) tv.tv_usec / 1000.0;
}
#endif

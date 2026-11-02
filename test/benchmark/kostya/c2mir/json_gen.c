#include "../../c2mir/bench_kernel_timer.h"
/* Native C2MIR port of kostya/json_gen.ls. */
extern int printf(const char *, ...);
extern int snprintf(char *, unsigned long, const char *, ...);

#include "../../c2mir/bench_lcg.h"
extern double floor(double);

static int build_json(char output[65536]) {
    int seed = 42;
    int length = 0;
    int i;
    output[length++] = '[';
    for (i = 0; i < 1000; i++) {
        int written;
        int id;
        int x;
        int y;
        int score;
        if (i > 0) output[length++] = ',';
        seed = bench_lcg_next(seed); id = seed % 10000;
        /* Lambda floors negative coordinates; C integer division would truncate. */
        seed = bench_lcg_next(seed); x = (int) floor((seed % 20000 - 10000) / 100.0);
        seed = bench_lcg_next(seed); y = (int) floor((seed % 20000 - 10000) / 100.0);
        seed = bench_lcg_next(seed); score = seed % 100;
        written = snprintf(output + length, 65536 - length,
                           "{\"id\":%d,\"score\":%d,\"coord\":{\"x\":%d,\"y\":%d},\"active\":true}",
                           id, score, x, y);
        if (written < 0 || written >= 65536 - length) return 0;
        length += written;
    }
    output[length++] = ']';
    output[length] = 0;
    return length;
}

int main(void) {
    BENCH_KERNEL_BEGIN();
    char json[65536];
    int iteration;
    int length = 0;
    for (iteration = 0; iteration < 10; iteration++) length = build_json(json);
    BENCH_KERNEL_END();
    printf(length > 0 ? "json_gen: length=%d\njson_gen: PASS\n" : "json_gen: FAIL\n", length);
    BENCH_KERNEL_REPORT();
    return length <= 0;
}

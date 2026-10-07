/* Scalar C reference ports; all workloads and timing policies match SUITE.md. */
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
/* Apple's math.h uses compiler intrinsics unsupported by the pinned C frontend. */
extern double sqrt(double);
extern double floor(double);

typedef struct MicroResult { long long value[4]; } MicroResult;
enum { MICRO_DECIMAL_BYTES = 20, MICRO_OUTPUT_BATCH_LINES = 256 };

/* caller provides space for a signed 64-bit decimal plus its terminator. */
static int decimal_text(char *text, long long value) {
    int negative = value < 0, length = 0;
    long long divisor = 1, limit;
    if (negative) { text[length++] = '-'; value = -value; }
    /* find the leading divisor with one constant divide; share quotient/remainder. */
    limit = value / 10;
    while (divisor <= limit) divisor *= 10;
    while (divisor > 0) {
        long long digit = value / divisor;
        text[length++] = (char)(48 + digit);
        value -= digit * divisor;
        divisor /= 10;
    }
    text[length] = 0;
    return length;
}
static long long decimal_value(const char *text) {
    int negative = text[0] == '-', index = negative ? 1 : 0;
    long long value = 0;
    while (text[index]) value = value * 10 + text[index++] - 48;
    return negative ? -value : value;
}
static MicroResult parse_integers(void) {
    long long seed = 42, checksum = 0, size = 0, errors = 0;
    char text[MICRO_DECIMAL_BYTES + 1];
    int index;
    for (index = 0; index < 100000; index++) {
        long long value, parsed;
        int length;
        seed = seed * 16807 % 2147483647;
        value = index % 8 == 0 ? 0 : (index % 8 == 1 ? -seed : seed);
        length = decimal_text(text, value);
        parsed = decimal_value(text);
        errors += parsed != value;
        size += length;
        checksum = (checksum * 31 + parsed + 2147483647) % 1000000007;
    }
    { MicroResult result = {{checksum, size, seed, errors}}; return result; }
}
static void gram(const double *matrix, int rows, int columns, double *result) {
    int i, j, k;
    for (i = 0; i < columns; i++) for (j = 0; j < columns; j++) {
        double total = 0.0;
        for (k = 0; k < rows; k++) total += matrix[k * columns + i] * matrix[k * columns + j];
        result[i * columns + j] = total;
    }
}
static void square(const double *matrix, int n, double *result) {
    int i, j, k;
    for (i = 0; i < n; i++) for (j = 0; j < n; j++) {
        double total = 0.0;
        const double *row = matrix + i * n;
        for (k = 0; k < n; k++) total += row[k] * matrix[k * n + j];
        result[i * n + j] = total;
    }
}
static double trace_fourth(const double *matrix, int rows, int columns, double *scratch) {
    double *first = scratch, *second = scratch + columns * columns, total = 0.0;
    int i;
    /* each product overwrites a separate destination; reuse Gram storage for G^4. */
    gram(matrix, rows, columns, first);
    square(first, columns, second);
    square(second, columns, first);
    for (i = 0; i < columns; i++) total += first[i * columns + i];
    return total;
}
static double variation(const double *values, int count) {
    double total = 0.0, mean;
    int i;
    for (i = 0; i < count; i++) total += values[i];
    mean = total / count; total = 0.0;
    for (i = 0; i < count; i++) { double delta = values[i] - mean; total += delta * delta; }
    return sqrt(total / (count - 1)) / mean;
}
static MicroResult matrix_statistics(void) {
    long long seed = 42, digest = 0;
    /* all cells are overwritten; stack storage avoids allocation and zeroing. */
    double v[1000], w[1000], blocks[100], p[100], q[100], scratch[2 * 20 * 20];
    MicroResult result;
    int iteration, i, block, row, column;
    for (iteration = 0; iteration < 1000; iteration++) {
        for (i = 0; i < 100; i++) {
            seed = seed * 16807 % 2147483647;
            blocks[i] = (double)seed / 2147483647.0 * 2.0 - 1.0;
        }
        for (block = 0; block < 4; block++) for (row = 0; row < 5; row++) for (column = 0; column < 5; column++) {
            double value = blocks[block * 25 + row * 5 + column];
            p[row * 20 + block * 5 + column] = value;
            q[(block / 2 * 5 + row) * 10 + block % 2 * 5 + column] = value;
        }
        v[iteration] = trace_fourth(p, 5, 20, scratch); w[iteration] = trace_fourth(q, 10, 10, scratch);
        digest = (digest * 31 + (long long)floor(v[iteration] * 1000)) % 1000000007;
        digest = (digest * 31 + (long long)floor(w[iteration] * 1000)) % 1000000007;
    }
    result.value[0] = (long long)floor(variation(v, 1000) * 1e9);
    result.value[1] = (long long)floor(variation(w, 1000) * 1e9);
    result.value[2] = digest; result.value[3] = seed;
    return result;
}
static MicroResult iteration_pi_sum(void) {
    double values[500], digest = 0.0;
    MicroResult result;
    int iteration, k, i;
    for (iteration = 0; iteration < 500; iteration++) {
        double total = 0.0;
        for (k = 1; k <= 10000 + iteration; k++) total += 1.0 / ((double)k * (double)k);
        values[iteration] = total;
    }
    for (i = 0; i < 500; i++) digest += values[i] * (i + 1);
    result.value[0] = (long long)floor(values[0] * 1e12);
    result.value[1] = (long long)floor(values[499] * 1e12);
    result.value[2] = (long long)floor(digest * 1e6); result.value[3] = 5124750;
    return result;
}
static MicroResult formatted_output(void) {
    long long size = 0, digest = 0, writes = 0;
    /* append directly into one batch; explicit lengths avoid repeated scans/copies. */
    char buffer[MICRO_OUTPUT_BATCH_LINES * (2 * MICRO_DECIMAL_BYTES + 2) + 1];
    int length = 0;
    int i, j;
    for (i = 1; i <= 100000; i++) {
        int start = length;
        length += decimal_text(buffer + length, i);
        buffer[length++] = ' ';
        length += decimal_text(buffer + length, i + 1);
        buffer[length++] = '\n';
        for (j = start; j < length; j++) digest = (digest * 31 + buffer[j]) % 1000000007;
        size += length - start;
        if (i % MICRO_OUTPUT_BATCH_LINES == 0 || i == 100000) {
#ifdef _WIN32
            FILE *sink = fopen("NUL", "w");
#else
            FILE *sink = fopen("/dev/null", "w");
#endif
            if (!sink || fwrite(buffer, 1, length, sink) != (size_t)length) exit(1);
            if (fclose(sink) != 0) exit(1);
            writes++; length = 0;
        }
    }
    { MicroResult result = {{size, digest, writes, 100000}}; return result; }
}
static int verify_micro(MicroResult result, const long long *expected) {
    int i;
    for (i = 0; i < 4; i++) if (result.value[i] != expected[i]) return 0;
    return 1;
}
static double micro_now_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, 0);
    return (double)tv.tv_sec * 1000.0 + (double)tv.tv_usec / 1000.0;
}
static int run_micro(const char *name, MicroResult (*workload)(void), const long long *expected) {
    MicroResult warm = workload(), result;
    double started, elapsed;
    if (!verify_micro(warm, expected)) { printf("%s: FAIL warmup\n", name); return 1; }
    started = micro_now_ms(); result = workload(); elapsed = micro_now_ms() - started;
    if (!verify_micro(result, expected)) { printf("%s: FAIL\n", name); return 1; }
    printf("%s: PASS %lld %lld %lld %lld\n", name, result.value[0], result.value[1], result.value[2], result.value[3]);
    printf("__TIMING__:%.6f\n", elapsed);
    return 0;
}

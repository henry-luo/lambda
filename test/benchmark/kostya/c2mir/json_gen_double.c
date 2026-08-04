/* Native C2MIR double port of kostya/json_gen.ls.  Generated numeric fields are double. */
extern int printf(const char *, ...);
extern int snprintf(char *, unsigned long, const char *, ...);
extern double floor(double);

static double trunc_zero(double value) { return value < 0.0 ? -floor(-value) : floor(value); }
static double modulo(double value, double modulus) { return value - trunc_zero(value / modulus) * modulus; }
static double next_rand(double seed) {
    double wrapped = modulo(seed * 1664525.0 + 1013904223.0, 4294967296.0);
    if (wrapped < 0.0) wrapped += 4294967296.0;
    if (wrapped >= 2147483648.0) wrapped -= 4294967296.0;
    return modulo(wrapped, 1000000.0);
}

static int build_json(char output[65536]) {
    double seed = 42.0;
    int length = 0;
    int i;
    output[length++] = '[';
    for (i = 0; i < 1000; i++) {
        int written;
        double id;
        double x;
        double y;
        double score;
        if (i > 0) output[length++] = ',';
        seed = next_rand(seed); id = modulo(seed, 10000.0);
        seed = next_rand(seed); x = trunc_zero((modulo(seed, 20000.0) - 10000.0) / 100.0);
        seed = next_rand(seed); y = trunc_zero((modulo(seed, 20000.0) - 10000.0) / 100.0);
        seed = next_rand(seed); score = modulo(seed, 100.0);
        /* integer formatting is the ABI boundary; the generated fields remain double until serialization. */
        written = snprintf(output + length, 65536 - length,
                           "{\"id\":%d,\"score\":%d,\"coord\":{\"x\":%d,\"y\":%d},\"active\":true}",
                           (int) id, (int) score, (int) x, (int) y);
        if (written < 0 || written >= 65536 - length) return 0;
        length += written;
    }
    output[length++] = ']';
    output[length] = 0;
    return length;
}

int main(void) {
    char json[65536];
    int iteration;
    int length = 0;
    for (iteration = 0; iteration < 10; iteration++) length = build_json(json);
    printf(length > 0 ? "json_gen: length=%d\njson_gen: PASS\n" : "json_gen: FAIL\n", length);
    return length <= 0;
}

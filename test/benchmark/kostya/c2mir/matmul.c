/* Native C2MIR port of kostya/matmul.ls. */
extern int printf(const char *, ...);

static int next_rand(int seed) { return (seed * 1664525 + 1013904223) % 1000000; }

int main(void) {
    static double a[40000];
    static double b[40000];
    static double c[40000];
    int seed = 42;
    int i;
    int j;
    int k;
    double total = 0.0;
    for (i = 0; i < 40000; i++) {
        seed = next_rand(seed); a[i] = (seed % 2000) / 1000.0 - 1.0;
        seed = next_rand(seed); b[i] = (seed % 2000) / 1000.0 - 1.0;
    }
    for (i = 0; i < 200; i++) for (j = 0; j < 200; j++) {
        double sum = 0.0;
        for (k = 0; k < 200; k++) sum += a[i * 200 + k] * b[k * 200 + j];
        c[i * 200 + j] = sum;
    }
    for (i = 0; i < 40000; i++) total += c[i];
    printf("matmul: sum=%d\nmatmul: DONE\n", (int) total);
    return 0;
}

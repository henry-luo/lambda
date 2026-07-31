/* Native C2MIR port of r7rs/fibfp2.ls. */
extern int printf(const char *, ...);

static double fibfp(double n) {
    if (n < 2.0) return n;
    return fibfp(n - 1.0) + fibfp(n - 2.0);
}

int main(void) {
    double result = fibfp(27.0);
    printf(result == 196418.0 ? "fibfp: PASS\n" : "fibfp: FAIL\n");
    return result != 196418.0;
}

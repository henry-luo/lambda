/* Native C2MIR port of r7rs/sumfp2.ls. */
extern int printf(const char *, ...);

static double run_sumfp(double n) {
    double sum = 0.0;
    while (n >= 0.0) {
        sum += n;
        n -= 1.0;
    }
    return sum;
}

int main(void) {
    double result = run_sumfp(100000.0);
    double diff = result - 5000050000.0;
    if (diff < 0.0) diff = -diff;
    printf(diff < 1.0 ? "sumfp: PASS\n" : "sumfp: FAIL\n");
    return diff >= 1.0;
}

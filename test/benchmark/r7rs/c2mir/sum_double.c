/* Native C2MIR double port of r7rs/sum2.ls.  The accumulator and countdown are double. */
extern int printf(const char *, ...);

static double run_sum(double n) {
    double sum = 0.0;
    while (n >= 0.0) {
        sum += n;
        n -= 1.0;
    }
    return sum;
}

int main(void) {
    int iter;
    double result = 0.0;
    for (iter = 0; iter < 100; iter++) result = run_sum(10000.0);
    printf(result == 50005000.0 ? "sum: PASS\n" : "sum: FAIL result=%.0f\n", result);
    return result != 50005000.0;
}

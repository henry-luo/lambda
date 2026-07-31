/* Native C2MIR port of r7rs/sum2.ls. */
extern int printf(const char *, ...);

static int run_sum(int n) {
    int sum = 0;
    while (n >= 0) {
        sum += n;
        n--;
    }
    return sum;
}

int main(void) {
    int iter;
    int result = 0;
    for (iter = 0; iter < 100; iter++) result = run_sum(10000);
    printf(result == 50005000 ? "sum: PASS\n" : "sum: FAIL result=%d\n", result);
    return result != 50005000;
}

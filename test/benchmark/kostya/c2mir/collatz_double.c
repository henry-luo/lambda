/* Native C2MIR double port of kostya/collatz.ls.  Sequence values and lengths are double. */
extern int printf(const char *, ...);
extern double fmod(double, double);

static double collatz_len(double n) {
    double steps = 1.0;
    while (n != 1.0) {
        n = fmod(n, 2.0) == 0.0 ? n / 2.0 : 3.0 * n + 1.0;
        steps += 1.0;
    }
    return steps;
}

int main(void) {
    int i;
    double max_len = 0.0;
    double max_start = 0.0;
    for (i = 1; i < 1000000; i++) {
        double length = collatz_len((double) i);
        if (length > max_len) { max_len = length; max_start = (double) i; }
    }
    printf(max_start == 837799.0 ? "collatz: PASS (start=837799)\n" : "collatz: FAIL result=%.0f\n", max_start);
    return max_start != 837799.0;
}

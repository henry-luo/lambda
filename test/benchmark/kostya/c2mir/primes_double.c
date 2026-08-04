/* Native C2MIR double port of kostya/primes.ls.  Sieve flags and count are double. */
extern int printf(const char *, ...);

int main(void) {
    static double flags[1000001];
    int i;
    double count = 0.0;
    for (i = 0; i <= 1000000; i++) flags[i] = 1.0;
    flags[0] = flags[1] = 0.0;
    for (i = 2; i * i <= 1000000; i++) if (flags[i] != 0.0) {
        int j;
        for (j = i * i; j <= 1000000; j += i) flags[j] = 0.0;
    }
    for (i = 2; i <= 1000000; i++) count += flags[i];
    printf(count == 78498.0 ? "primes: PASS (78498)\n" : "primes: FAIL result=%.0f\n", count);
    return count != 78498.0;
}

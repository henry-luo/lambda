/* Native C2MIR double port of awfy/sieve2.ls.  Sieve flags and count are double. */
extern int printf(const char *, ...);

int main(void) {
    double flags[5000];
    int i;
    double prime_count = 0.0;
    for (i = 0; i < 5000; i++) flags[i] = 1.0;
    for (i = 2; i <= 5000; i++) {
        int k;
        if (flags[i - 1] == 0.0) continue;
        prime_count += 1.0;
        for (k = i + i; k <= 5000; k += i) flags[k - 1] = 0.0;
    }
    printf(prime_count == 669.0 ? "Sieve: PASS\n" : "Sieve: FAIL result=%.0f\n", prime_count);
    return prime_count != 669.0;
}

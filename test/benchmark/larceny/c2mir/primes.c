/* Native C2MIR port of larceny/primes.ls. */
extern int printf(const char *, ...);
int main(void) {
    static unsigned char flags[1000001]; int i; int count = 0;
    for (i = 0; i <= 1000000; i++) flags[i] = 1;
    flags[0] = flags[1] = 0;
    for (i = 2; i * i <= 1000000; i++) if (flags[i]) { int j; for (j = i * i; j <= 1000000; j += i) flags[j] = 0; }
    for (i = 2; i <= 1000000; i++) count += flags[i];
    printf(count == 78498 ? "primes: PASS\n" : "primes: FAIL result=%d\n", count);
    return count != 78498;
}

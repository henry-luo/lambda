/* Native C2MIR port of kostya/collatz.ls. */
extern int printf(const char *, ...);

static int collatz_len(unsigned long n) {
    int steps = 1;
    while (n != 1) { n = n % 2 == 0 ? n / 2 : 3 * n + 1; steps++; }
    return steps;
}

int main(void) {
    int i;
    int max_len = 0;
    int max_start = 0;
    for (i = 1; i < 1000000; i++) {
        int length = collatz_len((unsigned long) i);
        if (length > max_len) { max_len = length; max_start = i; }
    }
    printf(max_start == 837799 ? "collatz: PASS (start=837799)\n" : "collatz: FAIL result=%d\n", max_start);
    return max_start != 837799;
}

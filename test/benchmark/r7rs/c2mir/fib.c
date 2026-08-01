/* Native C2MIR port of r7rs/fib2.ls. */
extern int printf(const char *, ...);

static int fib(int n) {
    if (n < 2) return n;
    return fib(n - 1) + fib(n - 2);
}

int main(void) {
    int result = fib(27);
    printf(result == 196418 ? "fib: PASS\n" : "fib: FAIL result=%d\n", result);
    return result != 196418;
}

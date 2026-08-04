/* Native C2MIR double port of r7rs/fib2.ls.  The recursive result and argument are double. */
extern int printf(const char *, ...);

static double fib(double n) {
    if (n < 2.0) return n;
    return fib(n - 1.0) + fib(n - 2.0);
}

int main(void) {
    double result = fib(27.0);
    printf(result == 196418.0 ? "fib: PASS\n" : "fib: FAIL result=%.0f\n", result);
    return result != 196418.0;
}

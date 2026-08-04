/* Native C2MIR double port of r7rs/ack2.ls.  Indices and main's status stay int for the C ABI. */
extern int printf(const char *, ...);

static double ack(double m, double n) {
    if (m == 0.0) return n + 1.0;
    if (n == 0.0) return ack(m - 1.0, 1.0);
    return ack(m - 1.0, ack(m, n - 1.0));
}

int main(void) {
    double result = ack(3.0, 8.0);
    printf(result == 2045.0 ? "ack: PASS\n" : "ack: FAIL result=%.0f\n", result);
    return result != 2045.0;
}

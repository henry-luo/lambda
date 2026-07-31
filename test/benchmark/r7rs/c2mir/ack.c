/* Native C2MIR port of r7rs/ack2.ls. */
extern int printf(const char *, ...);

static int ack(int m, int n) {
    if (m == 0) return n + 1;
    if (n == 0) return ack(m - 1, 1);
    return ack(m - 1, ack(m, n - 1));
}

int main(void) {
    int result = ack(3, 8);
    printf(result == 2045 ? "ack: PASS\n" : "ack: FAIL result=%d\n", result);
    return result != 2045;
}

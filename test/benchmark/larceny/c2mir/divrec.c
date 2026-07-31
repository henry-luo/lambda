/* Native C2MIR port of larceny/divrec.ls. */
extern int printf(const char *, ...);
static int divide_recursive(int x, int y, int quotient) { return x < y ? quotient : divide_recursive(x - y, y, quotient + 1); }
static int modulus_recursive(int x, int y) { return x < y ? x : modulus_recursive(x - y, y); }
int main(void) {
    int iter; int result = 0;
    for (iter = 0; iter < 1000; iter++) { result += divide_recursive(1000, 2, 0); result -= modulus_recursive(1000, 2); }
    printf(result == 500000 ? "divrec: PASS\n" : "divrec: FAIL result=%d\n", result);
    return result != 500000;
}

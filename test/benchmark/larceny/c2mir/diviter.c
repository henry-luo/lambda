/* Native C2MIR port of larceny/diviter.ls. */
extern int printf(const char *, ...);
static int divide_by_subtraction(int x, int y) { int quotient = 0; while (x >= y) { x -= y; quotient++; } return quotient; }
static int modulus_by_subtraction(int x, int y) { while (x >= y) x -= y; return x; }
int main(void) {
    int iter; int result = 0;
    for (iter = 0; iter < 1000; iter++) { result += divide_by_subtraction(1000000, 2); result -= modulus_by_subtraction(1000000, 2); }
    printf(result == 500000000 ? "diviter: PASS\n" : "diviter: FAIL result=%d\n", result);
    return result != 500000000;
}

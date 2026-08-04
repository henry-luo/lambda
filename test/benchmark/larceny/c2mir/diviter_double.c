/* Native C2MIR double port of larceny/diviter.ls.  Division operands and result are double. */
extern int printf(const char *, ...);
static double divide_by_subtraction(double x, double y) { double quotient = 0.0; while (x >= y) { x -= y; quotient += 1.0; } return quotient; }
static double modulus_by_subtraction(double x, double y) { while (x >= y) x -= y; return x; }
int main(void) {
    int iter;
    double result = 0.0;
    for (iter = 0; iter < 1000; iter++) { result += divide_by_subtraction(1000000.0, 2.0); result -= modulus_by_subtraction(1000000.0, 2.0); }
    printf(result == 500000000.0 ? "diviter: PASS\n" : "diviter: FAIL result=%.0f\n", result);
    return result != 500000000.0;
}

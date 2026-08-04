/* Native C2MIR double port of larceny/divrec.ls.  Recursive division state is double. */
extern int printf(const char *, ...);
static double divide_recursive(double x, double y, double quotient) { return x < y ? quotient : divide_recursive(x - y, y, quotient + 1.0); }
static double modulus_recursive(double x, double y) { return x < y ? x : modulus_recursive(x - y, y); }
int main(void) {
    int iter;
    double result = 0.0;
    for (iter = 0; iter < 1000; iter++) { result += divide_recursive(1000.0, 2.0, 0.0); result -= modulus_recursive(1000.0, 2.0); }
    printf(result == 500000.0 ? "divrec: PASS\n" : "divrec: FAIL result=%.0f\n", result);
    return result != 500000.0;
}

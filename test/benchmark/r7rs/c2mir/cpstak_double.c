/* Native C2MIR double port of r7rs/cpstak2.ls.  Recursive numeric state is double. */
extern int printf(const char *, ...);

static double tak(double x, double y, double z) {
    double a;
    double b;
    double c;
    if (y >= x) return z;
    a = tak(x - 1.0, y, z);
    b = tak(y - 1.0, z, x);
    c = tak(z - 1.0, x, y);
    return tak(a, b, c);
}

int main(void) {
    double result = tak(18.0, 12.0, 6.0);
    result = tak(18.0, 12.0, 6.0);
    printf(result == 7.0 ? "cpstak: PASS\n" : "cpstak: FAIL result=%.0f\n", result);
    return result != 7.0;
}

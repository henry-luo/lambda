/* Native C2MIR double port of larceny/array1.ls.  Array elements and sums are double. */
extern int printf(const char *, ...);
int main(void) {
    double values[10000];
    int i;
    int iter;
    double total = 0.0;
    for (i = 0; i < 10000; i++) values[i] = (double) i;
    for (iter = 0; iter < 100; iter++) {
        double sum = 0.0;
        for (i = 0; i < 10000; i++) sum += values[i];
        total = sum;
    }
    printf(total == 49995000.0 ? "array1: PASS\n" : "array1: FAIL result=%.0f\n", total);
    return total != 49995000.0;
}

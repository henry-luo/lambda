/* Native C2MIR port of larceny/array1.ls. */
extern int printf(const char *, ...);
int main(void) {
    int values[10000]; int i; int iter; int total = 0;
    for (i = 0; i < 10000; i++) values[i] = i;
    for (iter = 0; iter < 100; iter++) { int sum = 0; for (i = 0; i < 10000; i++) sum += values[i]; total = sum; }
    printf(total == 49995000 ? "array1: PASS\n" : "array1: FAIL result=%d\n", total);
    return total != 49995000;
}

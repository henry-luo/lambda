/* Native C2MIR double port of awfy/permute2.ls.  Permutation values and count are double. */
extern int printf(const char *, ...);

static void swap(double values[6], int i, int j) {
    double value = values[i];
    values[i] = values[j];
    values[j] = value;
}

static void permute(double values[6], int n, double *count) {
    int i;
    (*count) += 1.0;
    if (n == 0) return;
    n--;
    permute(values, n, count);
    for (i = n; i >= 0; i--) {
        swap(values, n, i);
        permute(values, n, count);
        swap(values, n, i);
    }
}

int main(void) {
    double values[6] = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
    double count = 0.0;
    permute(values, 6, &count);
    printf(count == 8660.0 ? "Permute: PASS\n" : "Permute: FAIL result=%.0f\n", count);
    return count != 8660.0;
}

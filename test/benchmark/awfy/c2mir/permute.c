/* Native C2MIR port of awfy/permute2.ls. */
extern int printf(const char *, ...);

static void swap(int values[6], int i, int j) {
    int value = values[i];
    values[i] = values[j];
    values[j] = value;
}

static void permute(int values[6], int n, int *count) {
    int i;
    (*count)++;
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
    int values[6] = {0, 0, 0, 0, 0, 0};
    int count = 0;
    permute(values, 6, &count);
    printf(count == 8660 ? "Permute: PASS\n" : "Permute: FAIL result=%d\n", count);
    return count != 8660;
}

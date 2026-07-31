/* Native C2MIR port of larceny/quicksort.ls. */
extern int printf(const char *, ...);
static int next_rand(int seed) { return (seed * 1664525 + 1013904223) % 1000000; }
static int partition(int values[5000], int low, int high) {
    int pivot = values[high]; int i = low; int j;
    for (j = low; j < high; j++) if (values[j] <= pivot) { int temp = values[i]; values[i++] = values[j]; values[j] = temp; }
    { int temp = values[i]; values[i] = values[high]; values[high] = temp; }
    return i;
}
static void quicksort(int values[5000], int low, int high) { if (low < high) { int pivot = partition(values, low, high); quicksort(values, low, pivot - 1); quicksort(values, pivot + 1, high); } }
int main(void) {
    int values[5000]; int seed = 42; int i; int sorted = 1;
    for (i = 0; i < 5000; i++) { seed = next_rand(seed); values[i] = seed; }
    quicksort(values, 0, 4999);
    for (i = 1; i < 5000; i++) if (values[i] < values[i - 1]) sorted = 0;
    printf(sorted ? "quicksort: PASS\n" : "quicksort: FAIL\n");
    return !sorted;
}

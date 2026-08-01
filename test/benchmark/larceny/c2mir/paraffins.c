/* Native C2MIR port of larceny/paraffins.ls. */
extern int printf(const char *, ...);
static int ms2(int r) { return r * (r + 1) / 2; }
static int ms3(int r) { return r * (r + 1) * (r + 2) / 6; }
static int ms4(int r) { return r * (r + 1) * (r + 2) * (r + 3) / 24; }
static int count_radicals(const int counts[12], int size) {
    int first; int total = 0; int target = size - 1;
    for (first = 0; first * 3 <= target; first++) { int second; for (second = first; first + second * 2 <= target; second++) { int third = target - first - second; if (third >= second) { int a = counts[first], b = counts[second], c = counts[third]; if (first == second && second == third) total += ms3(a); else if (first == second) total += ms2(a) * c; else if (second == third) total += a * ms2(b); else total += a * b * c; } } }
    return total;
}
static int count_ccp(const int counts[12], int n) {
    int first; int total = 0; int sum = n - 1; int max_radical = sum / 2;
    for (first = 0; first * 4 <= sum; first++) { int second; for (second = first; first + second * 3 <= sum; second++) { int remaining = sum - first - second; int third; for (third = second; third * 2 <= remaining; third++) { int fourth = remaining - third; if (fourth >= third && fourth <= max_radical) { int a = counts[first], b = counts[second], c = counts[third], d = counts[fourth]; if (first == second && second == third && third == fourth) total += ms4(a); else if (first == second && second == third) total += ms3(a) * d; else if (second == third && third == fourth) total += a * ms3(b); else if (first == second && third == fourth) total += ms2(a) * ms2(c); else if (first == second) total += ms2(a) * c * d; else if (second == third) total += a * ms2(b) * d; else if (third == fourth) total += a * b * ms2(c); else total += a * b * c * d; } } } }
    return total;
}
static int nb(int n) {
    int counts[12] = {0}; int half = n / 2; int k; int bcp;
    counts[0] = 1;
    for (k = 1; k <= half; k++) counts[k] = count_radicals(counts, k);
    bcp = n % 2 == 0 ? ms2(counts[half]) : 0;
    return bcp + count_ccp(counts, n);
}
int main(void) {
    int iteration; int result = 0;
    for (iteration = 0; iteration < 10; iteration++) { int n; for (n = 1; n <= 23; n++) result = nb(n); }
    printf(result == 5731580 ? "paraffins: nb(23) = 5731580\nparaffins: PASS\n" : "paraffins: FAIL\n");
    return result != 5731580;
}

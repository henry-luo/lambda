#include "../../c2mir/bench_kernel_timer.h"
/* Native C2MIR port of kostya/levenshtein.ls. */
extern int printf(const char *, ...);

static int string_len(const char *text) { int n = 0; while (text[n] != 0) n++; return n; }
static int min3(int a, int b, int c) { if (b < a) a = b; return c < a ? c : a; }

static int levenshtein(const char *left, const char *right) {
    int rows[2][501];
    int *prev = rows[0];
    int *curr = rows[1];
    int n = string_len(left);
    int m = string_len(right);
    int i;
    int j;
    for (j = 0; j <= m; j++) prev[j] = j;
    for (i = 1; i <= n; i++) {
        curr[0] = i;
        for (j = 1; j <= m; j++) curr[j] = min3(prev[j] + 1, curr[j - 1] + 1, prev[j - 1] + (left[i - 1] == right[j - 1] ? 0 : 1));
        /* swap row owners, matching the Lambda algorithm. */
        { int *temp = prev; prev = curr; curr = temp; }
    }
    return prev[m];
}

int main(void) {
    BENCH_KERNEL_BEGIN();
    char a[501];
    char b[501];
    char ab[401];
    char ba[401];
    int i;
    int d1 = levenshtein("kitten", "sitting");
    int d2 = levenshtein("saturday", "sunday");
    for (i = 0; i < 500; i++) { a[i] = 'a'; b[i] = 'b'; }
    a[500] = b[500] = 0;
    for (i = 0; i < 400; i++) { ab[i] = i % 2 ? 'b' : 'a'; ba[i] = i % 2 ? 'a' : 'b'; }
    ab[400] = ba[400] = 0;
    {
        int d3 = levenshtein(a, b);
        int d4 = levenshtein(ab, ba);
        printf("levenshtein: d(kitten,sitting)=%d\n", d1);
        printf("levenshtein: d(saturday,sunday)=%d\n", d2);
        printf("levenshtein: d(aaa...,bbb...)=%d\n", d3);
        printf("levenshtein: d(ababab...,babab...)=%d\n", d4);
        printf(d1 == 3 && d2 == 3 ? "levenshtein: PASS\n" : "levenshtein: FAIL\n");
        BENCH_KERNEL_END();
        BENCH_KERNEL_REPORT();
        return d1 != 3 || d2 != 3 || d3 != 500 || d4 != 2;
    }
}

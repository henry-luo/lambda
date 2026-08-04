/* Native C2MIR double port of kostya/levenshtein.ls.  Edit-distance state is double. */
extern int printf(const char *, ...);

static int string_len(const char *text) { int n = 0; while (text[n] != 0) n++; return n; }
static double min3(double a, double b, double c) { if (b < a) a = b; return c < a ? c : a; }

static double levenshtein(const char *left, const char *right) {
    double prev[501];
    double curr[501];
    int n = string_len(left);
    int m = string_len(right);
    int i;
    int j;
    for (j = 0; j <= m; j++) prev[j] = (double) j;
    for (i = 1; i <= n; i++) {
        curr[0] = (double) i;
        for (j = 1; j <= m; j++) curr[j] = min3(prev[j] + 1.0, curr[j - 1] + 1.0, prev[j - 1] + (left[i - 1] == right[j - 1] ? 0.0 : 1.0));
        for (j = 0; j <= m; j++) prev[j] = curr[j];
    }
    return prev[m];
}

int main(void) {
    char a[501];
    char b[501];
    char ab[401];
    char ba[401];
    int i;
    double d1 = levenshtein("kitten", "sitting");
    double d2 = levenshtein("saturday", "sunday");
    for (i = 0; i < 500; i++) { a[i] = 'a'; b[i] = 'b'; }
    a[500] = b[500] = 0;
    for (i = 0; i < 400; i++) { ab[i] = i % 2 ? 'b' : 'a'; ba[i] = i % 2 ? 'a' : 'b'; }
    ab[400] = ba[400] = 0;
    if (d1 == 3.0 && d2 == 3.0 && levenshtein(a, b) == 500.0 && levenshtein(ab, ba) == 2.0) {
        printf("levenshtein: PASS\n");
        return 0;
    }
    printf("levenshtein: FAIL\n");
    return 1;
}

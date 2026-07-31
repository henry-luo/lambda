/* Native C2MIR port of kostya/levenshtein.ls. */
extern int printf(const char *, ...);

static int string_len(const char *text) { int n = 0; while (text[n] != 0) n++; return n; }
static int min3(int a, int b, int c) { if (b < a) a = b; return c < a ? c : a; }

static int levenshtein(const char *left, const char *right) {
    int prev[501];
    int curr[501];
    int n = string_len(left);
    int m = string_len(right);
    int i;
    int j;
    for (j = 0; j <= m; j++) prev[j] = j;
    for (i = 1; i <= n; i++) {
        curr[0] = i;
        for (j = 1; j <= m; j++) curr[j] = min3(prev[j] + 1, curr[j - 1] + 1, prev[j - 1] + (left[i - 1] == right[j - 1] ? 0 : 1));
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
    int d1 = levenshtein("kitten", "sitting");
    int d2 = levenshtein("saturday", "sunday");
    for (i = 0; i < 500; i++) { a[i] = 'a'; b[i] = 'b'; }
    a[500] = b[500] = 0;
    for (i = 0; i < 400; i++) { ab[i] = i % 2 ? 'b' : 'a'; ba[i] = i % 2 ? 'a' : 'b'; }
    ab[400] = ba[400] = 0;
    if (d1 == 3 && d2 == 3 && levenshtein(a, b) == 500 && levenshtein(ab, ba) == 2) {
        printf("levenshtein: PASS\n");
        return 0;
    }
    printf("levenshtein: FAIL\n");
    return 1;
}

/* Native C2MIR port of r7rs/nqueens2.ls.  List values are double.
 *
 * Same algorithm as the .ls and nqueens2.js: the Scheme candidate-list search,
 * which builds fresh candidate and rest lists on every call.  An earlier port
 * used a fixed board with nested loops and no allocation, so the C2MIR column
 * timed a different algorithm (Tune29 P0).  `placed` is shared across frames,
 * as in the JS reference: only placed[placed_len] is written and only lower
 * slots are read, so sharing is algorithmically identical. */
extern int printf(const char *, ...);
extern void *calloc(unsigned long, unsigned long);
extern void free(void *);

static double *fill_zero(int n) {
    /* fill(n, 0); a zero-length request still yields a distinct block */
    return (double *) calloc((unsigned long) (n > 0 ? n : 1), sizeof(double));
}

static int ok(double row, int dist, double *placed, int placed_len) {
    if (dist > placed_len) return 1;
    {
        double p = placed[placed_len - dist];
        if (p == row + dist) return 0;
        if (p == row - dist) return 0;
    }
    return ok(row, dist + 1, placed, placed_len);
}

static int solve(double *candidates, int cand_len, double *rest, int rest_len,
                 double *placed, int placed_len) {
    double row;
    int count = 0;
    int ci, ri, ni;
    double *new_rest;
    double *new_cands2;
    if (cand_len == 0) return rest_len == 0 ? 1 : 0;
    row = candidates[0];

    if (ok(row, 1, placed, placed_len) == 1) {
        double *new_cands = fill_zero(cand_len - 1 + rest_len);
        double *empty = fill_zero(1);
        ni = 0;
        for (ci = 1; ci < cand_len; ci++) new_cands[ni++] = candidates[ci];
        for (ri = 0; ri < rest_len; ri++) new_cands[ni++] = rest[ri];
        placed[placed_len] = row;
        count += solve(new_cands, ni, empty, 0, placed, placed_len + 1);
        free(empty);
        free(new_cands);
    }

    new_rest = fill_zero(rest_len + 1);
    for (ri = 0; ri < rest_len; ri++) new_rest[ri] = rest[ri];
    new_rest[rest_len] = row;

    new_cands2 = fill_zero(cand_len - 1);
    ni = 0;
    for (ci = 1; ci < cand_len; ci++) new_cands2[ni++] = candidates[ci];
    count += solve(new_cands2, cand_len - 1, new_rest, rest_len + 1, placed, placed_len);
    free(new_cands2);
    free(new_rest);
    return count;
}

static int nqueens(int n) {
    double *candidates = fill_zero(n);
    double *placed = fill_zero(n);
    double *empty = fill_zero(1);
    int i;
    int result;
    for (i = 0; i < n; i++) candidates[i] = i + 1;
    result = solve(candidates, n, empty, 0, placed, 0);
    free(empty);
    free(placed);
    free(candidates);
    return result;
}

int main(void) {
    int result = nqueens(8);
    printf(result == 92 ? "nqueens: PASS\n" : "nqueens: FAIL result=%d\n", result);
    return result != 92;
}

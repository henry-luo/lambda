/* Native C2MIR double port of r7rs/nqueens2.ls.  Board values and totals are double. */
extern int printf(const char *, ...);

static double count_solutions(double rows[8], int column) {
    int row;
    int prev;
    int delta;
    double total = 0.0;
    if (column == 8) return 1.0;
    for (row = 0; row < 8; row++) {
        int valid = 1;
        for (prev = 0; prev < column; prev++) {
            delta = column - prev;
            if (rows[prev] == (double) row || rows[prev] + (double) delta == (double) row || rows[prev] - (double) delta == (double) row) {
                valid = 0;
                break;
            }
        }
        if (valid) {
            rows[column] = (double) row;
            total += count_solutions(rows, column + 1);
        }
    }
    return total;
}

int main(void) {
    double rows[8];
    double result = count_solutions(rows, 0);
    printf(result == 92.0 ? "nqueens: PASS\n" : "nqueens: FAIL result=%.0f\n", result);
    return result != 92.0;
}

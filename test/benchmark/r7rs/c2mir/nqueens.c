/* Native C2MIR port of r7rs/nqueens2.ls. */
extern int printf(const char *, ...);

static int count_solutions(int rows[8], int column) {
    int row;
    int prev;
    int delta;
    int total = 0;
    if (column == 8) return 1;
    for (row = 0; row < 8; row++) {
        int valid = 1;
        for (prev = 0; prev < column; prev++) {
            delta = column - prev;
            if (rows[prev] == row || rows[prev] + delta == row || rows[prev] - delta == row) {
                valid = 0;
                break;
            }
        }
        if (valid) {
            rows[column] = row;
            total += count_solutions(rows, column + 1);
        }
    }
    return total;
}

int main(void) {
    int rows[8];
    int result = count_solutions(rows, 0);
    printf(result == 92 ? "nqueens: PASS\n" : "nqueens: FAIL result=%d\n", result);
    return result != 92;
}

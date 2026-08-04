/* Native C2MIR double port of larceny/puzzle.ls.  Search flags and solution count are double. */
extern int printf(const char *, ...);

static double solve(int row, double columns[10], double diagonals_forward[20], double diagonals_back[20]) {
    int column;
    double count = 0.0;
    if (row == 10) return 1.0;
    for (column = 0; column < 10; column++) {
        int forward = row + column;
        int back = row - column + 9;
        if (columns[column] == 0.0 && diagonals_forward[forward] == 0.0 && diagonals_back[back] == 0.0) {
            columns[column] = diagonals_forward[forward] = diagonals_back[back] = 1.0;
            count += solve(row + 1, columns, diagonals_forward, diagonals_back);
            columns[column] = diagonals_forward[forward] = diagonals_back[back] = 0.0;
        }
    }
    return count;
}

int main(void) {
    double columns[10] = {0.0};
    double diagonals_forward[20] = {0.0};
    double diagonals_back[20] = {0.0};
    double result = solve(0, columns, diagonals_forward, diagonals_back);
    printf(result == 724.0 ? "puzzle: PASS\n" : "puzzle: FAIL result=%.0f\n", result);
    return result != 724.0;
}

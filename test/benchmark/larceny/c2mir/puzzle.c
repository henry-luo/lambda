/* Native C2MIR port of larceny/puzzle.ls. */
extern int printf(const char *, ...);

static int solve(int row, int columns[10], int diagonals_forward[20], int diagonals_back[20]) {
    int column;
    int count = 0;
    if (row == 10) return 1;
    for (column = 0; column < 10; column++) {
        int forward = row + column;
        int back = row - column + 9;
        if (!columns[column] && !diagonals_forward[forward] && !diagonals_back[back]) {
            columns[column] = diagonals_forward[forward] = diagonals_back[back] = 1;
            count += solve(row + 1, columns, diagonals_forward, diagonals_back);
            columns[column] = diagonals_forward[forward] = diagonals_back[back] = 0;
        }
    }
    return count;
}

int main(void) {
    int columns[10] = {0};
    int diagonals_forward[20] = {0};
    int diagonals_back[20] = {0};
    int result = solve(0, columns, diagonals_forward, diagonals_back);
    printf(result == 724 ? "puzzle: PASS\n" : "puzzle: FAIL result=%d\n", result);
    return result != 724;
}

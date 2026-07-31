/* Native C2MIR port of awfy/queens2.ls. */
extern int printf(const char *, ...);

static int place_queen(int free_rows[8], int free_maxs[16], int free_mins[16], int queen_rows[8], int column) {
    int row;
    for (row = 0; row < 8; row++) {
        if (free_rows[row] && free_maxs[column + row] && free_mins[column - row + 7]) {
            queen_rows[row] = column;
            free_rows[row] = free_maxs[column + row] = free_mins[column - row + 7] = 0;
            if (column == 7 || place_queen(free_rows, free_maxs, free_mins, queen_rows, column + 1)) return 1;
            free_rows[row] = free_maxs[column + row] = free_mins[column - row + 7] = 1;
        }
    }
    return 0;
}

static int queens(void) {
    int free_rows[8];
    int free_maxs[16];
    int free_mins[16];
    int queen_rows[8];
    int i;
    for (i = 0; i < 8; i++) { free_rows[i] = 1; queen_rows[i] = -1; }
    for (i = 0; i < 16; i++) { free_maxs[i] = 1; free_mins[i] = 1; }
    return place_queen(free_rows, free_maxs, free_mins, queen_rows, 0);
}

int main(void) {
    int i;
    int result = 1;
    for (i = 0; i < 10; i++) if (!queens()) result = 0;
    printf(result ? "Queens: PASS\n" : "Queens: FAIL\n");
    return !result;
}

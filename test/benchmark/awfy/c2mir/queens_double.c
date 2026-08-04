/* Native C2MIR double port of awfy/queens2.ls.  Board flags and result are double. */
extern int printf(const char *, ...);

static double place_queen(double free_rows[8], double free_maxs[16], double free_mins[16], double queen_rows[8], int column) {
    int row;
    for (row = 0; row < 8; row++) {
        if (free_rows[row] != 0.0 && free_maxs[column + row] != 0.0 && free_mins[column - row + 7] != 0.0) {
            queen_rows[row] = (double) column;
            free_rows[row] = free_maxs[column + row] = free_mins[column - row + 7] = 0.0;
            if (column == 7 || place_queen(free_rows, free_maxs, free_mins, queen_rows, column + 1) != 0.0) return 1.0;
            free_rows[row] = free_maxs[column + row] = free_mins[column - row + 7] = 1.0;
        }
    }
    return 0.0;
}

static double queens(void) {
    double free_rows[8];
    double free_maxs[16];
    double free_mins[16];
    double queen_rows[8];
    int i;
    for (i = 0; i < 8; i++) { free_rows[i] = 1.0; queen_rows[i] = -1.0; }
    for (i = 0; i < 16; i++) { free_maxs[i] = 1.0; free_mins[i] = 1.0; }
    return place_queen(free_rows, free_maxs, free_mins, queen_rows, 0);
}

int main(void) {
    int i;
    double result = 1.0;
    for (i = 0; i < 10; i++) if (queens() == 0.0) result = 0.0;
    printf(result != 0.0 ? "Queens: PASS\n" : "Queens: FAIL\n");
    return result == 0.0;
}

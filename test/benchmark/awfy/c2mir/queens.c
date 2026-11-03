#include "../../c2mir/bench_kernel_timer.h"
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

/* validate every solved board, as the typed Lambda workload does. */
static int is_valid(const int queen_rows[8]) {
    int r1;
    for (r1 = 0; r1 < 8; r1++) {
        int c1 = queen_rows[r1];
        int r2;
        if (c1 < 0 || c1 > 7) return 0;
        for (r2 = r1 + 1; r2 < 8; r2++) {
            int c2 = queen_rows[r2];
            if (c1 == c2 || c1 - c2 == r2 - r1 || c2 - c1 == r2 - r1) return 0;
        }
    }
    return 1;
}

static int queens(void) {
    int free_rows[8];
    int free_maxs[16];
    int free_mins[16];
    int queen_rows[8];
    int i;
    for (i = 0; i < 8; i++) { free_rows[i] = 1; queen_rows[i] = -1; }
    for (i = 0; i < 16; i++) { free_maxs[i] = 1; free_mins[i] = 1; }
    if (!place_queen(free_rows, free_maxs, free_mins, queen_rows, 0)) return 0;
    return is_valid(queen_rows);
}

int main(void) {
    BENCH_KERNEL_BEGIN();
    int i;
    int result = 1;
    for (i = 0; i < 10; i++) if (!queens()) result = 0;
    BENCH_KERNEL_END();
    printf(result ? "Queens: PASS\n" : "Queens: FAIL\n");
    BENCH_KERNEL_REPORT();
    return !result;
}

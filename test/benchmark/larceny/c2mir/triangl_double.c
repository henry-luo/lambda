/* Native C2MIR double port of larceny/triangl.ls.  Board state and solution count are double. */
extern int printf(const char *, ...);

static const int move_from[36] = {0,0,1,1,2,2,3,3,3,3,4,4,5,5,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,12,12,13,13,14,14};
static const int move_over[36] = {1,2,3,4,4,5,1,4,6,7,7,8,2,4,8,9,3,7,4,8,4,7,5,8,6,11,7,12,7,8,11,13,8,12,9,13};
static const int move_to[36] = {3,5,6,8,7,9,0,5,10,12,11,13,0,3,12,14,1,8,2,9,1,6,2,7,3,12,4,13,3,5,10,14,4,11,5,12};

int main(void) {
    double board[15];
    int stack[14];
    int i;
    int depth = 0;
    int pegs = 14;
    double solutions = 0.0;
    for (i = 0; i < 15; i++) board[i] = 1.0;
    board[0] = 0.0;
    stack[0] = 0;
    while (depth >= 0) {
        int found = 0;
        if (pegs == 1) {
            int last;
            solutions += 1.0;
            depth--;
            if (depth < 0) break;
            last = stack[depth];
            board[move_from[last]] = 1.0; board[move_over[last]] = 1.0; board[move_to[last]] = 0.0;
            pegs++; stack[depth] = last + 1;
            continue;
        }
        for (i = stack[depth]; i < 36; i++) {
            if (board[move_from[i]] != 0.0 && board[move_over[i]] != 0.0 && board[move_to[i]] == 0.0) {
                board[move_from[i]] = 0.0; board[move_over[i]] = 0.0; board[move_to[i]] = 1.0;
                pegs--; stack[depth] = i; depth++;
                if (depth < 14) stack[depth] = 0;
                found = 1;
                break;
            }
        }
        if (!found) {
            depth--;
            if (depth >= 0) {
                int last = stack[depth];
                board[move_from[last]] = 1.0; board[move_over[last]] = 1.0; board[move_to[last]] = 0.0;
                pegs++; stack[depth] = last + 1;
            }
        }
    }
    printf("triangl: solutions=%.0f\n%s\n", solutions, solutions == 29760.0 ? "triangl: PASS" : "triangl: DONE");
    return solutions != 29760.0;
}

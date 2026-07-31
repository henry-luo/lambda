/* Native C2MIR port of awfy/towers2.ls. */
extern int printf(const char *, ...);

static void push_disk(int piles[42], int tops[3], int disk, int pile) {
    piles[pile * 14 + tops[pile]] = disk;
    tops[pile]++;
}

static int pop_disk(int piles[42], int tops[3], int pile) {
    tops[pile]--;
    return piles[pile * 14 + tops[pile]];
}

static void move_disks(int piles[42], int tops[3], int disks, int from_pile, int to_pile, int *moves) {
    int other_pile;
    if (disks == 1) {
        push_disk(piles, tops, pop_disk(piles, tops, from_pile), to_pile);
        (*moves)++;
        return;
    }
    other_pile = 3 - from_pile - to_pile;
    move_disks(piles, tops, disks - 1, from_pile, other_pile, moves);
    push_disk(piles, tops, pop_disk(piles, tops, from_pile), to_pile);
    (*moves)++;
    move_disks(piles, tops, disks - 1, other_pile, to_pile, moves);
}

int main(void) {
    int piles[42] = {0};
    int tops[3] = {0, 0, 0};
    int moves = 0;
    int disk;
    for (disk = 12; disk >= 0; disk--) push_disk(piles, tops, disk, 0);
    move_disks(piles, tops, 13, 0, 1, &moves);
    printf(moves == 8191 ? "Towers: PASS\n" : "Towers: FAIL result=%d\n", moves);
    return moves != 8191;
}

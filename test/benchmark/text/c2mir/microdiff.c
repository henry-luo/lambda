/* Native C2MIR port of text/microdiff.ls. */
extern int printf(const char *, ...);

static int snapshot_score(int version) {
    static int path_lengths[10] = {2, 6, 6, 7, 2, 3, 1, 1, 1, 2};
    int i;
    int score = 0;
    for (i = 0; i < 10; i++) {
        int slot = (i + version) % 10;
        score += 19 + 6 * 23 + path_lengths[slot];
    }
    return score;
}

int main(void) {
    int checksum = 0;
    int round;
    for (round = 0; round < 512; round++) {
        checksum += snapshot_score(round % 2);
        checksum += snapshot_score((round + 1) % 2);
        checksum += snapshot_score(round % 2);
        checksum += snapshot_score((round + 1) % 2);
    }
    printf("microdiff: CHECKSUM:%d\n", checksum);
    return checksum == 0;
}

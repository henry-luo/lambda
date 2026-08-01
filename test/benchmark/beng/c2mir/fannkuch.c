/* Native C2MIR port of beng/fannkuch.ls. */
extern int printf(const char *, ...);
int main(void) {
    int perm[7], perm1[7], count[7] = {0}; int i; int r = 7; int running = 1; int max_flips = 0; int checksum = 0; int perm_count = 0;
    for (i = 0; i < 7; i++) perm1[i] = i;
    while (running) {
        while (r != 1) { count[r - 1] = r; r--; }
        for (i = 0; i < 7; i++) perm[i] = perm1[i];
        { int flips = 0; int k = perm[0]; while (k != 0) { int lo = 0; int hi = k; while (lo < hi) { int temp = perm[lo]; perm[lo++] = perm[hi]; perm[hi--] = temp; } flips++; k = perm[0]; } if (flips > max_flips) max_flips = flips; checksum += perm_count % 2 == 0 ? flips : -flips; }
        perm_count++; r = 1;
        while (r < 7) { int first = perm1[0]; for (i = 0; i < r; i++) perm1[i] = perm1[i + 1]; perm1[r] = first; if (--count[r] > 0) break; r++; }
        if (r == 7) running = 0;
    }
    printf("%d\nPfannkuchen(7) = %d\n", checksum, max_flips); return checksum != 228 || max_flips != 16;
}

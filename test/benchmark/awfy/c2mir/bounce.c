/* Native C2MIR port of awfy/bounce2.ls. */
extern int printf(const char *, ...);

static int next_random(int *seed) {
    *seed = (*seed * 1309 + 13849) % 65536;
    return *seed;
}

static int abs_int(int value) { return value < 0 ? -value : value; }

int main(void) {
    int bx[100], by[100], bxv[100], byv[100];
    int seed = 74755;
    int bounces = 0;
    int i;
    int step;
    for (i = 0; i < 100; i++) {
        bx[i] = next_random(&seed) % 500; by[i] = next_random(&seed) % 500;
        bxv[i] = next_random(&seed) % 300 - 150; byv[i] = next_random(&seed) % 300 - 150;
    }
    for (step = 0; step < 50; step++) for (i = 0; i < 100; i++) {
        int bounced = 0;
        bx[i] += bxv[i]; by[i] += byv[i];
        if (bx[i] > 500) { bx[i] = 500; bxv[i] = -abs_int(bxv[i]); bounced = 1; }
        if (bx[i] < 0) { bx[i] = 0; bxv[i] = abs_int(bxv[i]); bounced = 1; }
        if (by[i] > 500) { by[i] = 500; byv[i] = -abs_int(byv[i]); bounced = 1; }
        if (by[i] < 0) { by[i] = 0; byv[i] = abs_int(byv[i]); bounced = 1; }
        bounces += bounced;
    }
    printf(bounces == 1331 ? "Bounce: PASS\n" : "Bounce: FAIL result=%d\n", bounces);
    return bounces != 1331;
}

/* Native C2MIR double port of awfy/bounce2.ls.  Positions, velocities, and counts are double. */
extern int printf(const char *, ...);

static int next_random(int *seed) {
    *seed = (*seed * 1309 + 13849) % 65536;
    return *seed;
}

static double abs_double(double value) { return value < 0.0 ? -value : value; }

int main(void) {
    double bx[100], by[100], bxv[100], byv[100];
    int seed = 74755;
    double bounces = 0.0;
    int i;
    int step;
    for (i = 0; i < 100; i++) {
        bx[i] = (double) (next_random(&seed) % 500); by[i] = (double) (next_random(&seed) % 500);
        bxv[i] = (double) (next_random(&seed) % 300 - 150); byv[i] = (double) (next_random(&seed) % 300 - 150);
    }
    for (step = 0; step < 50; step++) for (i = 0; i < 100; i++) {
        double bounced = 0.0;
        bx[i] += bxv[i]; by[i] += byv[i];
        if (bx[i] > 500.0) { bx[i] = 500.0; bxv[i] = -abs_double(bxv[i]); bounced = 1.0; }
        if (bx[i] < 0.0) { bx[i] = 0.0; bxv[i] = abs_double(bxv[i]); bounced = 1.0; }
        if (by[i] > 500.0) { by[i] = 500.0; byv[i] = -abs_double(byv[i]); bounced = 1.0; }
        if (by[i] < 0.0) { by[i] = 0.0; byv[i] = abs_double(byv[i]); bounced = 1.0; }
        bounces += bounced;
    }
    printf(bounces == 1331.0 ? "Bounce: PASS\n" : "Bounce: FAIL result=%.0f\n", bounces);
    return bounces != 1331.0;
}

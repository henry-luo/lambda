/* Native C2MIR double port of larceny/pnpoly.ls.  Classification state and total are double. */
extern int printf(const char *, ...);
static double pnpoly(const double xs[20], const double ys[20], double testx, double testy) {
    double inside = 0.0;
    int i;
    int j = 19;
    for (i = 0; i < 20; j = i++) if ((ys[i] > testy) != (ys[j] > testy) && testx < (xs[j] - xs[i]) * (testy - ys[i]) / (ys[j] - ys[i]) + xs[i]) inside = inside == 0.0 ? 1.0 : 0.0;
    return inside;
}
int main(void) {
    double xs[20] = {0,1,1,0,0,1,-.5,-1,-1,-2,-2.5,-2,-1.5,-.5,.5,1,.5,0,-.5,-1};
    double ys[20] = {0,0,1,1,2,3,2,3,0,-.5,.5,1.5,2,3,3,2,1,.5,-1,-1};
    int ix;
    double count = 0.0;
    for (ix = 0; ix < 500; ix++) { int iy; double x = -2.5 + ix * .008; for (iy = 0; iy < 200; iy++) count += pnpoly(xs, ys, x, -1.5 + iy * .025); }
    printf("pnpoly: total=100000 inside=%.0f\npnpoly: DONE\n", count);
    return 0;
}

/* Native C2MIR port of r7rs/mbrot2.ls. */
extern int printf(const char *, ...);

static int count(double r, double i, double step, int x, int y) {
    double cr = r + x * step;
    double ci = i + y * step;
    double zr = cr;
    double zi = ci;
    int c;
    for (c = 0; c < 64; c++) {
        double zr2 = zr * zr;
        double zi2 = zi * zi;
        double new_zr;
        if (zr2 + zi2 > 16.0) return c;
        new_zr = zr2 - zi2 + cr;
        zi = 2.0 * zr * zi + ci;
        zr = new_zr;
    }
    return 64;
}

int main(void) {
    int matrix[75][75];
    int x;
    int y;
    int result;
    for (y = 74; y >= 0; y--) for (x = 74; x >= 0; x--) matrix[x][y] = count(-1.0, -0.5, 0.005, x, y);
    result = matrix[0][0];
    printf(result == 5 ? "mbrot: PASS\n" : "mbrot: FAIL result=%d\n", result);
    return result != 5;
}

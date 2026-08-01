/* Native C2MIR port of awfy/nbody2.ls. */
extern int printf(const char *, ...);
extern double sqrt(double);

static const double PI = 3.141592653589793;
static const double DAYS_PER_YEAR = 365.24;

static void advance(double bx[5], double by[5], double bz[5], double bvx[5], double bvy[5], double bvz[5], double mass[5]) {
    int i;
    for (i = 0; i < 5; i++) {
        int j;
        for (j = i + 1; j < 5; j++) {
            double dx = bx[i] - bx[j];
            double dy = by[i] - by[j];
            double dz = bz[i] - bz[j];
            double d_squared = dx * dx + dy * dy + dz * dz;
            double mag = 0.01 / (d_squared * sqrt(d_squared));
            bvx[i] -= dx * mass[j] * mag; bvy[i] -= dy * mass[j] * mag; bvz[i] -= dz * mass[j] * mag;
            bvx[j] += dx * mass[i] * mag; bvy[j] += dy * mass[i] * mag; bvz[j] += dz * mass[i] * mag;
        }
    }
    for (i = 0; i < 5; i++) { bx[i] += 0.01 * bvx[i]; by[i] += 0.01 * bvy[i]; bz[i] += 0.01 * bvz[i]; }
}

static double energy(double bx[5], double by[5], double bz[5], double bvx[5], double bvy[5], double bvz[5], double mass[5]) {
    double result = 0.0;
    int i;
    for (i = 0; i < 5; i++) {
        int j;
        result += 0.5 * mass[i] * (bvx[i] * bvx[i] + bvy[i] * bvy[i] + bvz[i] * bvz[i]);
        for (j = i + 1; j < 5; j++) {
            double dx = bx[i] - bx[j];
            double dy = by[i] - by[j];
            double dz = bz[i] - bz[j];
            result -= mass[i] * mass[j] / sqrt(dx * dx + dy * dy + dz * dz);
        }
    }
    return result;
}

int main(void) {
    double solar_mass = 4.0 * PI * PI;
    double bx[5] = {0.0, 4.84143144246472090, 8.34336671824457987, 12.8943695621391310, 15.3796971148509165};
    double by[5] = {0.0, -1.16032004402742839, 4.12479856412430479, -15.1111514069918631, -25.9193146099879641};
    double bz[5] = {0.0, -0.103622044471123239, -0.403523417114321038, -0.223307578892655734, 0.179258772950371181};
    double bvx[5] = {0.0, 0.00166007664274403694 * DAYS_PER_YEAR, -0.00276742510726862411 * DAYS_PER_YEAR, 0.00296460137564761618 * DAYS_PER_YEAR, 0.00268067772490389322 * DAYS_PER_YEAR};
    double bvy[5] = {0.0, 0.00769901118419740425 * DAYS_PER_YEAR, 0.00499852801234917238 * DAYS_PER_YEAR, 0.00237847173959480950 * DAYS_PER_YEAR, 0.00162824170038242295 * DAYS_PER_YEAR};
    double bvz[5] = {0.0, -0.0000690460016972063023 * DAYS_PER_YEAR, 0.0000230417297573763926 * DAYS_PER_YEAR, -0.0000296589532392949896 * DAYS_PER_YEAR, -0.0000951592254519715870 * DAYS_PER_YEAR};
    double mass[5] = {0.0, 0.000954791938424326609 * solar_mass, 0.000285885980666130812 * solar_mass, 0.0000436624404335156298 * solar_mass, 0.0000515138902046611451 * solar_mass};
    int i;
    int check;
    double px = 0.0;
    double py = 0.0;
    double pz = 0.0;
    double result;
    mass[0] = solar_mass;
    for (i = 0; i < 5; i++) { px += bvx[i] * mass[i]; py += bvy[i] * mass[i]; pz += bvz[i] * mass[i]; }
    bvx[0] = -px / solar_mass; bvy[0] = -py / solar_mass; bvz[0] = -pz / solar_mass;
    for (i = 0; i < 36000; i++) advance(bx, by, bz, bvx, bvy, bvz, mass);
    result = energy(bx, by, bz, bvx, bvy, bvz, mass);
    check = (int) (-result * 10000000.0);
    printf(check == 1690142 ? "NBody: PASS\n" : "NBody: FAIL check=%d\n", check);
    return check != 1690142;
}

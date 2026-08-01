/* Native C2MIR port of larceny/ray.ls. */
extern int printf(const char *, ...);
extern double sqrt(double);

static double sphere_intersect(double dx, double dy, double dz, double cx, double cy, double cz, double radius) {
    double ex = -cx, ey = -cy, ez = -cz;
    double a = dx * dx + dy * dy + dz * dz;
    double b = 2.0 * (ex * dx + ey * dy + ez * dz);
    double c = ex * ex + ey * ey + ez * ez - radius * radius;
    double discriminant = b * b - 4.0 * a * c;
    double t;
    if (discriminant < 0.0) return -1.0;
    t = (-b - sqrt(discriminant)) / (2.0 * a);
    if (t > 0.001) return t;
    t = (-b + sqrt(discriminant)) / (2.0 * a);
    return t > 0.001 ? t : -1.0;
}

int main(void) {
    double sx[4] = {0.0, -2.0, 2.0, 0.0};
    double sy[4] = {0.0, 0.0, 0.0, 2.0};
    double sz[4] = {5.0, 5.0, 5.0, 5.0};
    int hits = 0;
    int py;
    for (py = 0; py < 100; py++) {
        int px;
        for (px = 0; px < 100; px++) {
            double dx = (px - 50.0) / 50.0;
            double dy = (py - 50.0) / 50.0;
            double length = sqrt(dx * dx + dy * dy + 1.0);
            double nearest = 999999.0;
            int sphere;
            dx /= length; dy /= length;
            for (sphere = 0; sphere < 4; sphere++) {
                double t = sphere_intersect(dx, dy, 1.0 / length, sx[sphere], sy[sphere], sz[sphere], 1.0);
                if (t > 0.0 && t < nearest) nearest = t;
            }
            if (nearest < 999999.0) hits++;
        }
    }
    printf("ray: hits=%d\n%s\n", hits, hits > 0 && hits < 10000 ? "ray: PASS" : "ray: FAIL");
    return hits <= 0 || hits >= 10000;
}

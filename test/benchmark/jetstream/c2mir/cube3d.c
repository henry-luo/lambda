/* Native C2MIR port of jetstream/cube3d.ls. */
extern int printf(const char *, ...);
extern double sin(double);
extern double cos(double);
extern double sqrt(double);

static const double PI = 3.141592653589793;

static double dabs(double x) { return x < 0.0 ? -x : x; }

static void mat4_identity(double m[16]) {
    int i;
    for (i = 0; i < 16; i++) m[i] = 0.0;
    m[0] = 1.0; m[5] = 1.0; m[10] = 1.0; m[15] = 1.0;
}

/* out = m1 * m2; out may alias m1 or m2 (computed into a temp first) */
static void mat4_mul(double out[16], const double m1[16], const double m2[16]) {
    double t[16];
    int i, j;
    for (i = 0; i < 4; i++) {
        for (j = 0; j < 4; j++) {
            t[i * 4 + j] = m1[i * 4 + 0] * m2[0 * 4 + j] +
                           m1[i * 4 + 1] * m2[1 * 4 + j] +
                           m1[i * 4 + 2] * m2[2 * 4 + j] +
                           m1[i * 4 + 3] * m2[3 * 4 + j];
        }
    }
    for (i = 0; i < 16; i++) out[i] = t[i];
}

/* r = m * v (4x4 by 4-vector); r must not alias v */
static void vmulti(double r[4], const double m[16], const double v[4]) {
    int i;
    for (i = 0; i < 4; i++) {
        r[i] = m[i * 4 + 0] * v[0] + m[i * 4 + 1] * v[1] +
               m[i * 4 + 2] * v[2] + m[i * 4 + 3] * v[3];
    }
}

/* r = 3x3 submatrix of m times 3-vector v */
static void vmulti2(double r[3], const double m[16], const double v[3]) {
    int i;
    for (i = 0; i < 3; i++) {
        r[i] = m[i * 4 + 0] * v[0] + m[i * 4 + 1] * v[1] + m[i * 4 + 2] * v[2];
    }
}

static void translate_mat(double out[16], const double m[16], double dx, double dy, double dz) {
    double t[16] = {1.0, 0.0, 0.0, dx,
                    0.0, 1.0, 0.0, dy,
                    0.0, 0.0, 1.0, dz,
                    0.0, 0.0, 0.0, 1.0};
    mat4_mul(out, t, m);
}

static void rotate_x(double out[16], const double m[16], double phi) {
    double a = phi * PI / 180.0;
    double c = cos(a);
    double s = sin(a);
    double r[16] = {1.0, 0.0, 0.0, 0.0,
                    0.0,   c,  -s, 0.0,
                    0.0,   s,   c, 0.0,
                    0.0, 0.0, 0.0, 1.0};
    mat4_mul(out, r, m);
}

static void rotate_y(double out[16], const double m[16], double phi) {
    double a = phi * PI / 180.0;
    double c = cos(a);
    double s = sin(a);
    double r[16] = {  c, 0.0,   s, 0.0,
                    0.0, 1.0, 0.0, 0.0,
                     -s, 0.0,   c, 0.0,
                    0.0, 0.0, 0.0, 1.0};
    mat4_mul(out, r, m);
}

static void rotate_z(double out[16], const double m[16], double phi) {
    double a = phi * PI / 180.0;
    double c = cos(a);
    double s = sin(a);
    double r[16] = {  c,  -s, 0.0, 0.0,
                      s,   c, 0.0, 0.0,
                    0.0, 0.0, 1.0, 0.0,
                    0.0, 0.0, 0.0, 1.0};
    mat4_mul(out, r, m);
}

static void calc_normal(double n[4], const double v0[3], const double v1[3], const double v2[3]) {
    double a0 = v0[0] - v1[0], a1 = v0[1] - v1[1], a2 = v0[2] - v1[2];
    double b0 = v2[0] - v1[0], b1 = v2[1] - v1[1], b2 = v2[2] - v1[2];
    double cx = a1 * b2 - a2 * b1;
    double cy = a2 * b0 - a0 * b2;
    double cz = a0 * b1 - a1 * b0;
    double length = sqrt(cx * cx + cy * cy + cz * cz);
    n[0] = cx / length; n[1] = cy / length; n[2] = cz / length; n[3] = 1.0;
}

/* draw line (just count pixels, no actual rendering) */
static int draw_line(double x1, double y1, double x2, double y2, int last_px) {
    double dx = dabs(x2 - x1);
    double dy = dabs(y2 - y1);
    double num_pix = dx >= dy ? dx : dy;
    /* positive values only, so floor(x + 0.5) matches round() */
    return (int) ((double) last_px + num_pix + 0.5);
}

/* per-face line table: {line index, from point, to point} x 4, matching the
 * explicit face blocks in cube3d.ls */
static const int face_lines[6][4][3] = {
    {{0, 0, 1}, {1, 1, 2}, {2, 2, 3}, {3, 3, 0}},
    {{2, 3, 2}, {9, 2, 6}, {6, 6, 7}, {10, 7, 3}},
    {{4, 4, 5}, {5, 5, 6}, {6, 6, 7}, {7, 7, 4}},
    {{4, 4, 5}, {8, 5, 1}, {0, 1, 0}, {11, 0, 4}},
    {{11, 4, 0}, {3, 0, 3}, {10, 3, 7}, {7, 7, 4}},
    {{8, 1, 5}, {5, 5, 6}, {9, 6, 2}, {1, 2, 1}}
};

static double run_cube(int cube_size) {
    double cs = (double) cube_size;
    double qv[36];
    int edges[18] = {0, 1, 2, 3, 2, 6, 7, 6, 5, 4, 5, 1, 4, 0, 3, 1, 5, 6};
    double normals[24];
    double mqube[16], mtrans[16], ident[16];
    double origin[4] = {150.0, 150.0, 20.0, 1.0};
    double sum;
    int fi, pi, loop_count, last_px;

    /* cube vertices: 8 corners + center, [x,y,z,1] each */
    qv[0] = -cs; qv[1] = -cs; qv[2] = cs;  qv[3] = 1.0;
    qv[4] = -cs; qv[5] = cs;  qv[6] = cs;  qv[7] = 1.0;
    qv[8] = cs;  qv[9] = cs;  qv[10] = cs;  qv[11] = 1.0;
    qv[12] = cs; qv[13] = -cs; qv[14] = cs;  qv[15] = 1.0;
    qv[16] = -cs; qv[17] = -cs; qv[18] = -cs; qv[19] = 1.0;
    qv[20] = -cs; qv[21] = cs;  qv[22] = -cs; qv[23] = 1.0;
    qv[24] = cs;  qv[25] = cs;  qv[26] = -cs; qv[27] = 1.0;
    qv[28] = cs;  qv[29] = -cs; qv[30] = -cs; qv[31] = 1.0;
    qv[32] = 0.0; qv[33] = 0.0; qv[34] = 0.0; qv[35] = 1.0;

    /* normals for 6 faces from the untransformed vertices */
    for (fi = 0; fi < 6; fi++) {
        int e0 = edges[fi * 3], e1 = edges[fi * 3 + 1], e2 = edges[fi * 3 + 2];
        calc_normal(&normals[fi * 4], &qv[e0 * 4], &qv[e1 * 4], &qv[e2 * 4]);
    }

    mat4_identity(mqube);
    mat4_identity(ident);
    translate_mat(mtrans, ident, origin[0], origin[1], origin[2]);
    mat4_mul(mqube, mtrans, mqube);

    for (pi = 0; pi < 9; pi++) {
        double old_v[4], new_v[4];
        old_v[0] = qv[pi * 4]; old_v[1] = qv[pi * 4 + 1];
        old_v[2] = qv[pi * 4 + 2]; old_v[3] = qv[pi * 4 + 3];
        vmulti(new_v, mtrans, old_v);
        qv[pi * 4] = new_v[0]; qv[pi * 4 + 1] = new_v[1];
        qv[pi * 4 + 2] = new_v[2]; qv[pi * 4 + 3] = new_v[3];
    }

    last_px = 0;
    for (loop_count = 0; loop_count <= 50; loop_count++) {
        double center_v[4];
        double cur_n[18];
        int line_drawn[12];
        int ni, f, k;

        center_v[0] = qv[32]; center_v[1] = qv[33];
        center_v[2] = qv[34]; center_v[3] = qv[35];
        translate_mat(mtrans, ident, -center_v[0], -center_v[1], -center_v[2]);
        rotate_x(mtrans, mtrans, 1.0);
        rotate_y(mtrans, mtrans, 3.0);
        rotate_z(mtrans, mtrans, 5.0);
        translate_mat(mtrans, mtrans, center_v[0], center_v[1], center_v[2]);
        mat4_mul(mqube, mtrans, mqube);

        for (pi = 8; pi >= 0; pi--) {
            double old2[4], new2[4];
            old2[0] = qv[pi * 4]; old2[1] = qv[pi * 4 + 1];
            old2[2] = qv[pi * 4 + 2]; old2[3] = qv[pi * 4 + 3];
            vmulti(new2, mtrans, old2);
            qv[pi * 4] = new2[0]; qv[pi * 4 + 1] = new2[1];
            qv[pi * 4 + 2] = new2[2]; qv[pi * 4 + 3] = new2[3];
        }

        for (ni = 0; ni < 6; ni++) {
            double nv[3];
            vmulti2(nv, mqube, &normals[ni * 4]);
            cur_n[ni * 3] = nv[0]; cur_n[ni * 3 + 1] = nv[1]; cur_n[ni * 3 + 2] = nv[2];
        }

        for (k = 0; k < 12; k++) line_drawn[k] = 0;
        last_px = 0;

        for (f = 0; f < 6; f++) {
            if (cur_n[f * 3 + 2] < 0.0) {
                for (k = 0; k < 4; k++) {
                    int line = face_lines[f][k][0];
                    int a = face_lines[f][k][1];
                    int b = face_lines[f][k][2];
                    if (line_drawn[line] == 0) {
                        last_px = draw_line(qv[a * 4], qv[a * 4 + 1],
                                            qv[b * 4], qv[b * 4 + 1], last_px);
                        line_drawn[line] = 1;
                    }
                }
            }
        }
    }

    /* verification: sum all vertex components */
    sum = 0.0;
    for (pi = 0; pi < 9; pi++) {
        sum = sum + qv[pi * 4] + qv[pi * 4 + 1] + qv[pi * 4 + 2] + qv[pi * 4 + 3];
    }
    return sum;
}

static int run(void) {
    int pass = 1;
    int sz;
    for (sz = 20; sz <= 160; sz *= 2) {
        double sum = run_cube(sz);
        double diff = sum - 2889.0;
        if (diff < 0.0) diff = -diff;
        if (diff > 0.001) {
            printf("3d-cube: FAIL for size=%d sum=%f\n", sz, sum);
            pass = 0;
        }
    }
    return pass;
}

int main(void) {
    int pass = 1;
    int iter;
    /* JetStream runs 8 iterations */
    for (iter = 0; iter < 8; iter++) {
        if (!run()) pass = 0;
    }
    printf(pass ? "3d-cube: PASS\n" : "3d-cube: FAIL\n");
    return !pass;
}

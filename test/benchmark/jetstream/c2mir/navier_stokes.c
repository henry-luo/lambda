/* Native C2MIR port of jetstream/navier_stokes.ls. */
extern int printf(const char *, ...);
extern double sqrt(double);
extern double floor(double);

#define WIDTH 128
#define HEIGHT 128
#define ROW_SIZE (WIDTH + 2)
#define GRID_SIZE ((WIDTH + 2) * (HEIGHT + 2))

static double dens[GRID_SIZE];
static double dens_prev[GRID_SIZE];
static double u_field[GRID_SIZE];
static double u_prev[GRID_SIZE];
static double v_field[GRID_SIZE];
static double v_prev[GRID_SIZE];

static void add_fields(double *x, double *s, double dt) {
    int i;
    for (i = 0; i < GRID_SIZE; i++) x[i] += dt * s[i];
}

static void set_bnd(int b, double *x) {
    int i, j, max_edge;
    if (b == 1) {
        for (i = 1; i <= WIDTH; i++) {
            x[i] = x[i + ROW_SIZE];
            x[i + (HEIGHT + 1) * ROW_SIZE] = x[i + HEIGHT * ROW_SIZE];
        }
        for (j = 1; j <= HEIGHT; j++) {
            x[j * ROW_SIZE] = 0.0 - x[1 + j * ROW_SIZE];
            x[(WIDTH + 1) + j * ROW_SIZE] = 0.0 - x[WIDTH + j * ROW_SIZE];
        }
    } else if (b == 2) {
        for (i = 1; i <= WIDTH; i++) {
            x[i] = 0.0 - x[i + ROW_SIZE];
            x[i + (HEIGHT + 1) * ROW_SIZE] = 0.0 - x[i + HEIGHT * ROW_SIZE];
        }
        for (j = 1; j <= HEIGHT; j++) {
            x[j * ROW_SIZE] = x[1 + j * ROW_SIZE];
            x[(WIDTH + 1) + j * ROW_SIZE] = x[WIDTH + j * ROW_SIZE];
        }
    } else {
        for (i = 1; i <= WIDTH; i++) {
            x[i] = x[i + ROW_SIZE];
            x[i + (HEIGHT + 1) * ROW_SIZE] = x[i + HEIGHT * ROW_SIZE];
        }
        for (j = 1; j <= HEIGHT; j++) {
            x[j * ROW_SIZE] = x[1 + j * ROW_SIZE];
            x[(WIDTH + 1) + j * ROW_SIZE] = x[WIDTH + j * ROW_SIZE];
        }
    }
    max_edge = (HEIGHT + 1) * ROW_SIZE;
    x[0] = 0.5 * (x[1] + x[ROW_SIZE]);
    x[max_edge] = 0.5 * (x[1 + max_edge] + x[HEIGHT * ROW_SIZE]);
    x[WIDTH + 1] = 0.5 * (x[WIDTH] + x[(WIDTH + 1) + ROW_SIZE]);
    x[(WIDTH + 1) + max_edge] = 0.5 * (x[WIDTH + max_edge] + x[(WIDTH + 1) + HEIGHT * ROW_SIZE]);
}

static void lin_solve(int b, double *x, double *x0, double a, double c, int iterations) {
    int i, j, k;
    double inv_c;
    if (a == 0.0 && c == 1.0) {
        for (j = 1; j <= HEIGHT; j++) {
            int cr = j * ROW_SIZE + 1;
            for (i = 0; i < WIDTH; i++) {
                x[cr] = x0[cr];
                cr++;
            }
        }
        set_bnd(b, x);
        return;
    }
    inv_c = 1.0 / c;
    for (k = 0; k < iterations; k++) {
        for (j = 1; j <= HEIGHT; j++) {
            int last_row = (j - 1) * ROW_SIZE;
            int current_row = j * ROW_SIZE;
            int next_row = (j + 1) * ROW_SIZE;
            double last_x = x[current_row];
            current_row++;
            for (i = 1; i <= WIDTH; i++) {
                last_x = (x0[current_row] + a * (last_x + x[current_row + 1] + x[last_row + 1] + x[next_row + 1])) * inv_c;
                x[current_row] = last_x;
                current_row++;
                last_row++;
                next_row++;
            }
        }
        set_bnd(b, x);
    }
}

static void diffuse(int b, double *x, double *x0, double dt, int iterations) {
    double a = 0.0;
    lin_solve(b, x, x0, a, 1.0 + 4.0 * a, iterations);
}

static void advect(int b, double *d, double *d0, double *u, double *v, double dt) {
    double w_dt0 = dt * (double) WIDTH;
    double h_dt0 = dt * (double) HEIGHT;
    double wp5 = (double) WIDTH + 0.5;
    double hp5 = (double) HEIGHT + 0.5;
    int i, j;
    for (j = 1; j <= HEIGHT; j++) {
        int pos = j * ROW_SIZE;
        for (i = 1; i <= WIDTH; i++) {
            double x, y, s0, s1, t0, t1;
            int i0, i1, j0, j1, row1, row2;
            pos++;
            x = (double) i - w_dt0 * u[pos];
            y = (double) j - h_dt0 * v[pos];
            if (x < 0.5) x = 0.5;
            else if (x > wp5) x = wp5;
            i0 = (int) floor(x);
            i1 = i0 + 1;
            if (y < 0.5) y = 0.5;
            else if (y > hp5) y = hp5;
            j0 = (int) floor(y);
            j1 = j0 + 1;
            s1 = x - (double) i0;
            s0 = 1.0 - s1;
            t1 = y - (double) j0;
            t0 = 1.0 - t1;
            row1 = j0 * ROW_SIZE;
            row2 = j1 * ROW_SIZE;
            d[pos] = s0 * (t0 * d0[i0 + row1] + t1 * d0[i0 + row2]) +
                     s1 * (t0 * d0[i1 + row1] + t1 * d0[i1 + row2]);
        }
    }
    set_bnd(b, d);
}

static void project(double *u, double *v, double *p, double *dv, int iterations) {
    double h = -0.5 / sqrt((double) (WIDTH * HEIGHT));
    double w_scale, h_scale;
    int i, j;
    for (j = 1; j <= HEIGHT; j++) {
        int row = j * ROW_SIZE;
        for (i = 1; i <= WIDTH; i++) {
            int idx = row + i;
            dv[idx] = h * (u[idx + 1] - u[idx - 1] + v[idx + ROW_SIZE] - v[idx - ROW_SIZE]);
            p[idx] = 0.0;
        }
    }
    set_bnd(0, dv);
    set_bnd(0, p);
    lin_solve(0, p, dv, 1.0, 4.0, iterations);
    w_scale = 0.5 * (double) WIDTH;
    h_scale = 0.5 * (double) HEIGHT;
    for (j = 1; j <= HEIGHT; j++) {
        int row = j * ROW_SIZE;
        for (i = 1; i <= WIDTH; i++) {
            int idx = row + i;
            u[idx] -= w_scale * (p[idx + 1] - p[idx - 1]);
            v[idx] -= h_scale * (p[idx + ROW_SIZE] - p[idx - ROW_SIZE]);
        }
    }
    set_bnd(1, u);
    set_bnd(2, v);
}

static void dens_step(double *x, double *x0, double *u, double *v, double dt, int iterations) {
    add_fields(x, x0, dt);
    diffuse(0, x0, x, dt, iterations);
    advect(0, x, x0, u, v, dt);
}

static void vel_step(double *u, double *v, double *u0, double *v0, double dt, int iterations) {
    int i;
    add_fields(u, u0, dt);
    add_fields(v, v0, dt);
    /* swap u,u0 and v,v0 by copying, as the .ls does */
    for (i = 0; i < GRID_SIZE; i++) {
        double tmp_u = u0[i];
        double tmp_v = v0[i];
        u0[i] = u[i];
        u[i] = tmp_u;
        v0[i] = v[i];
        v[i] = tmp_v;
    }
    diffuse(1, u, u0, dt, iterations);
    diffuse(2, v, v0, dt, iterations);
    project(u, v, u0, v0, iterations);
    for (i = 0; i < GRID_SIZE; i++) {
        double tmp_u = u0[i];
        double tmp_v = v0[i];
        u0[i] = u[i];
        u[i] = tmp_u;
        v0[i] = v[i];
        v[i] = tmp_v;
    }
    advect(1, u, u0, u0, v0, dt);
    advect(2, v, v0, u0, v0, dt);
    project(u, v, u0, v0, iterations);
}

static void add_points(double *d, double *u, double *v) {
    int n = 64;
    int i;
    for (i = 1; i <= n; i++) {
        double fn = (double) n;
        int idx1 = (i + 1) + (i + 1) * ROW_SIZE;
        int idx2 = (i + 1) + (n - i + 1) * ROW_SIZE;
        int idx3 = (128 - i + 1) + (n + i + 1) * ROW_SIZE;
        u[idx1] = fn;
        v[idx1] = fn;
        d[idx1] = 5.0;
        u[idx2] = 0.0 - fn;
        v[idx2] = 0.0 - fn;
        d[idx2] = 20.0;
        u[idx3] = 0.0 - fn;
        v[idx3] = 0.0 - fn;
        d[idx3] = 30.0;
    }
}

int main(void) {
    int iterations = 20;
    double dt = 0.1;
    int frames_till_add = 0;
    int frames_between = 5;
    int frame, k, ci;
    long result = 0;
    for (frame = 0; frame < 15; frame++) {
        for (k = 0; k < GRID_SIZE; k++) {
            u_prev[k] = 0.0;
            v_prev[k] = 0.0;
            dens_prev[k] = 0.0;
        }
        if (frames_till_add == 0) {
            add_points(dens_prev, u_prev, v_prev);
            frames_till_add = frames_between;
            frames_between++;
        } else {
            frames_till_add--;
        }
        vel_step(u_field, v_field, u_prev, v_prev, dt, iterations);
        dens_step(dens, dens_prev, u_field, v_field, dt, iterations);
    }
    /* checksum: truncate-toward-zero per element, like the .ls int() */
    for (ci = 7000; ci < 7100; ci++) result += (long) (dens[ci] * 10.0);
    if (result == 77) {
        printf("navier-stokes: PASS (checksum=%ld)\n", result);
        return 0;
    }
    printf("navier-stokes: FAIL (checksum=%ld, expected 77)\n", result);
    return 1;
}

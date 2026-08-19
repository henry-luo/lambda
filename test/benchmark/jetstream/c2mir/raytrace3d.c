/* Native C2MIR port of jetstream/raytrace3d.ls. */
extern int printf(const char *, ...);
extern double sqrt(double);

#define N_TRIANGLES 14
#define N_LIGHTS 3

static double dabs(double x) { return x < 0.0 ? -x : x; }

/* vector operations (3-element arrays) */
static void vec_add(double r[3], const double v1[3], const double v2[3]) {
    r[0] = v1[0] + v2[0]; r[1] = v1[1] + v2[1]; r[2] = v1[2] + v2[2];
}

static void vec_sub(double r[3], const double v1[3], const double v2[3]) {
    r[0] = v1[0] - v2[0]; r[1] = v1[1] - v2[1]; r[2] = v1[2] - v2[2];
}

static void vec_scale(double r[3], const double v[3], double s) {
    r[0] = v[0] * s; r[1] = v[1] * s; r[2] = v[2] * s;
}

static void vec_scalev(double r[3], const double v1[3], const double v2[3]) {
    r[0] = v1[0] * v2[0]; r[1] = v1[1] * v2[1]; r[2] = v1[2] * v2[2];
}

static double vec_dot(const double v1[3], const double v2[3]) {
    return v1[0] * v2[0] + v1[1] * v2[1] + v1[2] * v2[2];
}

static void vec_cross(double r[3], const double v1[3], const double v2[3]) {
    r[0] = v1[1] * v2[2] - v1[2] * v2[1];
    r[1] = v1[2] * v2[0] - v1[0] * v2[2];
    r[2] = v1[0] * v2[1] - v1[1] * v2[0];
}

static double vec_length(const double v[3]) {
    return sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
}

static void vec_normalise(double r[3], const double v[3]) {
    double l = vec_length(v);
    r[0] = v[0] / l; r[1] = v[1] / l; r[2] = v[2] / l;
}

/* matrix: flat 12-element array (3x4) */
static void transform_matrix(double r[3], const double m[12], const double v[3]) {
    double x = m[0] * v[0] + m[1] * v[1] + m[2] * v[2] + m[3];
    double y = m[4] * v[0] + m[5] * v[1] + m[6] * v[2] + m[7];
    double z = m[8] * v[0] + m[9] * v[1] + m[10] * v[2] + m[11];
    r[0] = x; r[1] = y; r[2] = z;
}

static void invert_matrix(double m[12]) {
    double temp[16];
    double tx = -m[3], ty = -m[7], tz = -m[11];
    int h, v, i;
    for (i = 0; i < 16; i++) temp[i] = 0.0;
    for (h = 0; h < 3; h++) {
        for (v = 0; v < 3; v++) temp[h + v * 4] = m[v + h * 4];
    }
    for (i = 0; i < 12; i++) m[i] = temp[i];
    m[3] = tx * m[0] + ty * m[1] + tz * m[2];
    m[7] = tx * m[4] + ty * m[5] + tz * m[6];
    m[11] = tx * m[8] + ty * m[9] + tz * m[10];
}

/* triangle with precomputed intersection data */
typedef struct {
    int axis;
    double normal[3];
    double nu, nv, nd;
    double eu, ev;
    double nu1, nv1, nu2, nv2;
    double material[3];
} Triangle;

static void create_triangle(Triangle *tri, const double p1[3], const double p2[3], const double p3[3]) {
    double edge1[3], edge2[3], normal[3];
    int axis, u, v;
    double u1, v1, u2, v2, det;
    vec_sub(edge1, p3, p1);
    vec_sub(edge2, p2, p1);
    vec_cross(normal, edge1, edge2);

    if (dabs(normal[0]) > dabs(normal[1])) {
        axis = dabs(normal[0]) > dabs(normal[2]) ? 0 : 2;
    } else {
        axis = dabs(normal[1]) > dabs(normal[2]) ? 1 : 2;
    }
    u = (axis + 1) % 3;
    v = (axis + 2) % 3;
    u1 = edge1[u]; v1 = edge1[v];
    u2 = edge2[u]; v2 = edge2[v];
    vec_normalise(tri->normal, normal);
    tri->axis = axis;
    tri->nu = normal[u] / normal[axis];
    tri->nv = normal[v] / normal[axis];
    tri->nd = vec_dot(normal, p1) / normal[axis];
    det = u1 * v2 - v1 * u2;
    tri->eu = p1[u];
    tri->ev = p1[v];
    tri->nu1 = u1 / det;
    tri->nv1 = -(v1 / det);
    tri->nu2 = v2 / det;
    tri->nv2 = -(u2 / det);
    tri->material[0] = 0.7; tri->material[1] = 0.7; tri->material[2] = 0.7;
}

static double triangle_intersect(const Triangle *tri, const double orig[3], const double dir[3],
                                 double near, double far) {
    int u = (tri->axis + 1) % 3;
    int v = (tri->axis + 2) % 3;
    double d = dir[tri->axis] + tri->nu * dir[u] + tri->nv * dir[v];
    double t = (tri->nd - orig[tri->axis] - tri->nu * orig[u] - tri->nv * orig[v]) / d;
    double pu, pv, a2, a3;
    if (t < near) return -1.0;
    if (t > far) return -1.0;
    pu = orig[u] + t * dir[u] - tri->eu;
    pv = orig[v] + t * dir[v] - tri->ev;
    a2 = pv * tri->nu1 + pu * tri->nv1;
    if (a2 < 0.0) return -1.0;
    a3 = pu * tri->nu2 + pv * tri->nv2;
    if (a3 < 0.0) return -1.0;
    if ((a2 + a3) > 1.0) return -1.0;
    return t;
}

typedef struct {
    double pos[3];
    double colour[3];
} Light;

typedef struct {
    Triangle triangles[N_TRIANGLES];
    Light lights[N_LIGHTS];
    double ambient[3];
    double background[3];
    int n_lights;
    int n_triangles;
} Scene;

typedef struct {
    double origin[3];
    double d0[3], d1[3], d2[3], d3[3];
} Camera;

/* keeps the per-pixel lighting result observable so the workload cannot be
 * dead-code eliminated (the .ls computes but discards it too) */
static double g_sink = 0.0;

static void scene_intersect(double l_out[3], const Scene *scene, const double origin[3],
                            const double dir[3], double near, double far) {
    const Triangle *closest = 0;
    double normal[3], hit[3], scaled[3], colour[3], l[3];
    int i;
    for (i = 0; i < scene->n_triangles; i++) {
        const Triangle *tri = &scene->triangles[i];
        double d = triangle_intersect(tri, origin, dir, near, far);
        if (d > 0.0) {
            far = d;
            closest = tri;
        }
    }
    if (closest == 0) {
        l_out[0] = scene->background[0];
        l_out[1] = scene->background[1];
        l_out[2] = scene->background[2];
        return;
    }
    normal[0] = closest->normal[0]; normal[1] = closest->normal[1]; normal[2] = closest->normal[2];
    vec_scale(scaled, dir, far);
    vec_add(hit, origin, scaled);
    if (vec_dot(dir, normal) > 0.0) {
        normal[0] = -normal[0]; normal[1] = -normal[1]; normal[2] = -normal[2];
    }
    colour[0] = closest->material[0]; colour[1] = closest->material[1]; colour[2] = closest->material[2];

    /* lighting */
    l[0] = scene->ambient[0]; l[1] = scene->ambient[1]; l[2] = scene->ambient[2];
    for (i = 0; i < scene->n_lights; i++) {
        const Light *light = &scene->lights[i];
        double to_light[3];
        double distance, nl;
        int blocked, bi;
        vec_sub(to_light, light->pos, hit);
        distance = vec_length(to_light);
        vec_scale(to_light, to_light, 1.0 / distance);
        /* shadow test */
        blocked = 0;
        for (bi = 0; bi < scene->n_triangles; bi++) {
            double sd = triangle_intersect(&scene->triangles[bi], hit, to_light, 0.0001, distance - 0.0001);
            if (sd > 0.0) { blocked = 1; break; }
        }
        if (!blocked) {
            nl = vec_dot(normal, to_light);
            if (nl > 0.0) {
                double contrib[3];
                vec_scale(contrib, light->colour, nl);
                l[0] += contrib[0]; l[1] += contrib[1]; l[2] += contrib[2];
            }
        }
    }
    vec_scalev(l_out, l, colour);
}

static void create_camera(Camera *cam, const double origin[3], const double lookat[3], const double up[3]) {
    double zaxis[3], xaxis[3], yaxis[3], neg_z[3], tmp[3];
    double m[12];
    double d0[3] = {-0.7, 0.7, 1.0};
    double d1[3] = {0.7, 0.7, 1.0};
    double d2[3] = {0.7, -0.7, 1.0};
    double d3[3] = {-0.7, -0.7, 1.0};
    int i;
    vec_sub(tmp, lookat, origin);
    vec_normalise(zaxis, tmp);
    vec_cross(tmp, up, zaxis);
    vec_normalise(xaxis, tmp);
    neg_z[0] = -zaxis[0]; neg_z[1] = -zaxis[1]; neg_z[2] = -zaxis[2];
    vec_cross(tmp, xaxis, neg_z);
    vec_normalise(yaxis, tmp);
    for (i = 0; i < 12; i++) m[i] = 0.0;
    m[0] = xaxis[0]; m[1] = xaxis[1]; m[2] = xaxis[2];
    m[4] = yaxis[0]; m[5] = yaxis[1]; m[6] = yaxis[2];
    m[8] = zaxis[0]; m[9] = zaxis[1]; m[10] = zaxis[2];
    invert_matrix(m);
    m[3] = 0.0; m[7] = 0.0; m[11] = 0.0;

    vec_normalise(tmp, d0); transform_matrix(cam->d0, m, tmp);
    vec_normalise(tmp, d1); transform_matrix(cam->d1, m, tmp);
    vec_normalise(tmp, d2); transform_matrix(cam->d2, m, tmp);
    vec_normalise(tmp, d3); transform_matrix(cam->d3, m, tmp);
    cam->origin[0] = origin[0]; cam->origin[1] = origin[1]; cam->origin[2] = origin[2];
}

static int render_scene(const Camera *cam, const Scene *scene, int size) {
    int pixel_count = 0;
    int x, y;
    for (y = 0; y < size; y++) {
        double yf = (double) y / (double) size;
        double ray0_dir[3], ray1_dir[3], s0[3], s1[3];
        vec_scale(s0, cam->d0, yf);
        vec_scale(s1, cam->d3, 1.0 - yf);
        vec_add(ray0_dir, s0, s1);
        vec_scale(s0, cam->d1, yf);
        vec_scale(s1, cam->d2, 1.0 - yf);
        vec_add(ray1_dir, s0, s1);
        for (x = 0; x < size; x++) {
            double xf = (double) x / (double) size;
            double origin[3], dir[3], l[3];
            vec_scale(s0, cam->origin, xf);
            vec_scale(s1, cam->origin, 1.0 - xf);
            vec_add(origin, s0, s1);
            vec_scale(s0, ray0_dir, xf);
            vec_scale(s1, ray1_dir, 1.0 - xf);
            vec_add(dir, s0, s1);
            vec_normalise(dir, dir);
            scene_intersect(l, scene, origin, dir, 0.0001, 1000000.0);
            g_sink += l[0] + l[1] + l[2];
            pixel_count++;
        }
    }
    return pixel_count;
}

static void set_light(Light *light, double px, double py, double pz, double r, double g, double b) {
    light->pos[0] = px; light->pos[1] = py; light->pos[2] = pz;
    light->colour[0] = r; light->colour[1] = g; light->colour[2] = b;
}

static int raytrace_scene(void) {
    /* build scene: a cube (12 triangles) + floor (2 triangles) */
    double tfl[3] = {-10.0, 10.0, -10.0};
    double tfr[3] = {10.0, 10.0, -10.0};
    double tbl[3] = {-10.0, 10.0, 10.0};
    double tbr[3] = {10.0, 10.0, 10.0};
    double bfl[3] = {-10.0, -10.0, -10.0};
    double bfr[3] = {10.0, -10.0, -10.0};
    double bbl[3] = {-10.0, -10.0, 10.0};
    double bbr[3] = {10.0, -10.0, 10.0};
    double ffl[3] = {-1000.0, -30.0, -1000.0};
    double ffr[3] = {1000.0, -30.0, -1000.0};
    double fbl[3] = {-1000.0, -30.0, 1000.0};
    double fbr[3] = {1000.0, -30.0, 1000.0};
    double cam_origin[3] = {-40.0, 40.0, 40.0};
    double cam_lookat[3] = {0.0, 0.0, 0.0};
    double cam_up[3] = {0.0, 1.0, 0.0};
    static Scene scene;
    Camera cam;

    create_triangle(&scene.triangles[0], tfl, tfr, bfr);
    create_triangle(&scene.triangles[1], tfl, bfr, bfl);
    create_triangle(&scene.triangles[2], tbl, tbr, bbr);
    create_triangle(&scene.triangles[3], tbl, bbr, bbl);
    create_triangle(&scene.triangles[4], tbl, tfl, bbl);
    create_triangle(&scene.triangles[5], tfl, bfl, bbl);
    create_triangle(&scene.triangles[6], tbr, tfr, bbr);
    create_triangle(&scene.triangles[7], tfr, bfr, bbr);
    create_triangle(&scene.triangles[8], tbl, tbr, tfr);
    create_triangle(&scene.triangles[9], tbl, tfr, tfl);
    create_triangle(&scene.triangles[10], bbl, bbr, bfr);
    create_triangle(&scene.triangles[11], bbl, bfr, bfl);
    create_triangle(&scene.triangles[12], fbl, fbr, ffr);
    create_triangle(&scene.triangles[13], fbl, ffr, ffl);
    scene.n_triangles = N_TRIANGLES;

    set_light(&scene.lights[0], 20.0, 38.0, -22.0, 0.7, 0.3, 0.3);
    set_light(&scene.lights[1], -23.0, 40.0, 17.0, 0.7, 0.3, 0.3);
    set_light(&scene.lights[2], 23.0, 20.0, 17.0, 0.7, 0.7, 0.7);
    scene.n_lights = N_LIGHTS;
    scene.ambient[0] = 0.1; scene.ambient[1] = 0.1; scene.ambient[2] = 0.1;
    scene.background[0] = 0.8; scene.background[1] = 0.8; scene.background[2] = 1.0;

    create_camera(&cam, cam_origin, cam_lookat, cam_up);
    return render_scene(&cam, &scene, 30);
}

int main(void) {
    int iter;
    int total_pixels = 0;
    /* JetStream runs 8 iterations */
    for (iter = 0; iter < 8; iter++) {
        total_pixels += raytrace_scene();
    }
    if (total_pixels == 7200) {
        printf("3d-raytrace: PASS (pixels=%d)\n", total_pixels);
    } else {
        printf("3d-raytrace: DONE (pixels=%d)\n", total_pixels);
    }
    return total_pixels != 7200;
}

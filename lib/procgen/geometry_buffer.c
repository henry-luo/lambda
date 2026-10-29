#include "geometry_buffer.h"
#include <math.h>
// the monolithic host also compiles this file; preserve the scalar oracle's rounding there.
#pragma STDC FP_CONTRACT OFF

// operation order follows transform.inverse/point and compact_buffer (S1.6, PG12.9).
bool procgen_transform_buffer(const double* m, const double* positions,
        const double* normals, size_t count, double* out_positions, double* out_normals) {
    double d = m[0]*(m[5]*m[10]-m[9]*m[6]) - m[4]*(m[1]*m[10]-m[9]*m[2]) +
        m[8]*(m[1]*m[6]-m[5]*m[2]);
    if (m[3] != 0 || m[7] != 0 || m[11] != 0 || m[15] != 1 || !(fabs(d) >= 1e-10)) return false;
    double inverse[9] = {
        (m[5]*m[10]-m[9]*m[6])/d, (m[9]*m[2]-m[1]*m[10])/d, (m[1]*m[6]-m[5]*m[2])/d,
        (m[8]*m[6]-m[4]*m[10])/d, (m[0]*m[10]-m[8]*m[2])/d, (m[4]*m[2]-m[0]*m[6])/d,
        (m[4]*m[9]-m[8]*m[5])/d, (m[8]*m[1]-m[0]*m[9])/d, (m[0]*m[5]-m[4]*m[1])/d
    };
    for (size_t i = 0; i < count; i++) {
        const double* p = positions + i*3;
        const double* n = normals + i*3;
        double v[3];
        for (int r = 0; r < 3; r++) {
            out_positions[i*3+r] = m[r]*p[0]+m[4+r]*p[1]+m[8+r]*p[2]+m[12+r];
            v[r] = inverse[r*3]*n[0]+inverse[r*3+1]*n[1]+inverse[r*3+2]*n[2];
        }
        double length = sqrt((v[0]*v[0]+v[1]*v[1])+v[2]*v[2]);
        for (int r = 0; r < 3; r++) out_normals[i*3+r] = length > 0 ? v[r]/length : v[r];
    }
    return true;
}

// chart.geometry.nearest_segment, including repeated/zero-length closing edges.
void procgen_polygon_distances(const double* points, size_t count,
        const double* outline, size_t corners, double* distances) {
    for (size_t i = 0; i < count; i++) {
        double best = INFINITY;
        const double* p = points + i*2;
        for (size_t j = 0; j < corners; j++) {
            const double* a = outline + j*2;
            const double* b = outline + ((j+1)%corners)*2;
            double dx = b[0]-a[0], dy = b[1]-a[1];
            double length = pow(dx,2)+pow(dy,2);
            double t = length == 0 ? 0 : fmax(0,fmin(1,((p[0]-a[0])*dx+(p[1]-a[1])*dy)/length));
            double x = p[0]-(a[0]+dx*t), y = p[1]-(a[1]+dy*t);
            double distance = sqrt(pow(x,2)+pow(y,2));
            if (distance < best) best = distance;
        }
        distances[i] = best;
    }
}

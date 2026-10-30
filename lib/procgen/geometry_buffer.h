// flat double-precision counterparts of Scene3D transforms and chart distances.
#ifndef LIB_PROCGEN_GEOMETRY_BUFFER_H
#define LIB_PROCGEN_GEOMETRY_BUFFER_H
#include <stddef.h>
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
bool procgen_transform_buffer(const double* matrix, const double* positions,
    const double* normals, size_t count, double* out_positions, double* out_normals);
void procgen_polygon_distances(const double* points, size_t count,
    const double* outline, size_t corners, double* distances);
#ifdef __cplusplus
}
#endif
#endif

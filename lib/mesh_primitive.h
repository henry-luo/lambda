// Box and plane primitive geometry, shared by the native scene renderer and glTF
// export so both produce the same vertices for `<geometry type: 'box'|'plane'>`.
#ifndef LIB_MESH_PRIMITIVE_H
#define LIB_MESH_PRIMITIVE_H
#include <stdbool.h>
#include <stdint.h>

enum { MESH_BOX_VERTICES = 24, MESH_BOX_INDICES = 36, MESH_PLANE_VERTICES = 4, MESH_PLANE_INDICES = 6 };

// Fills 3 position and normal components and 2 lower-left-origin UV components per
// vertex, plus the triangle indices. A box has six four-vertex faces centred on the
// origin; a plane is one face in z=0 facing +z. `size` is the full extent per axis.
static inline void mesh_primitive(bool box, const float size[3], float* positions, float* normals,
    float* uvs, uint32_t* indices) {
    static const float face_normals[6][3] = {{0,0,1},{0,0,-1},{1,0,0},{-1,0,0},{0,1,0},{0,-1,0}};
    static const float rights[6][3] = {{1,0,0},{-1,0,0},{0,0,-1},{0,0,1},{1,0,0},{1,0,0}};
    static const float ups[6][3] = {{0,1,0},{0,1,0},{0,1,0},{0,1,0},{0,0,-1},{0,0,1}};
    static const float corners[4][2] = {{-1,-1},{1,-1},{1,1},{-1,1}};
    static const unsigned triangles[6] = {0,1,2,0,2,3};
    for (unsigned face = 0; face < (box ? 6u : 1u); face++) {
        for (unsigned corner = 0; corner < 4; corner++) {
            unsigned v = face * 4 + corner;
            for (unsigned c = 0; c < 3; c++) {
                positions[v * 3 + c] = ((box ? face_normals[face][c] : 0) + corners[corner][0] * rights[face][c] +
                    corners[corner][1] * ups[face][c]) * size[c] * .5f;
                normals[v * 3 + c] = face_normals[face][c];
            }
            uvs[v * 2] = (corners[corner][0] + 1) * .5f; uvs[v * 2 + 1] = (corners[corner][1] + 1) * .5f;
        }
        for (unsigned i = 0; i < 6; i++) indices[face * 6 + i] = face * 4 + triangles[i];
    }
}

// Adds each triangle's unnormalized (area-weighted) face normal to its three corners
// in `sums` (3 zeroed components per vertex): the smooth normals of buffer geometry
// that has none. Without indices, consecutive position triples are the triangles.
static inline void mesh_accumulate_normals(const float* positions, const uint32_t* indices, unsigned count, float* sums) {
    for (unsigned i = 0; i + 2 < count; i += 3) {
        unsigned ids[3] = {indices ? indices[i] : i, indices ? indices[i + 1] : i + 1, indices ? indices[i + 2] : i + 2};
        const float* a = positions + ids[0] * 3; const float* b = positions + ids[1] * 3; const float* c = positions + ids[2] * 3;
        float u[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]}, v[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]};
        float n[3] = {u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0]};
        for (unsigned k = 0; k < 3; k++) for (unsigned axis = 0; axis < 3; axis++) sums[ids[k] * 3 + axis] += n[axis];
    }
}
#endif

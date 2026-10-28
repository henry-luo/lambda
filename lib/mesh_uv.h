#pragma once

// conjugate a row-major 2x3 affine UV matrix by v -> 1-v; import and export are inverses.
static inline void mesh_uv_flip_origin(double matrix[6]) {
    matrix[2]+=matrix[1];matrix[1]=-matrix[1];matrix[3]=-matrix[3];
    matrix[5]=1-matrix[4]-matrix[5];
}

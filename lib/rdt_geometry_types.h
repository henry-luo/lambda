#pragma once

// shared POD/opaque geometry records keep native host callbacks independent of rendering implementation headers.
typedef struct RdtPath RdtPath;

typedef enum {
    RDT_FILL_WINDING = 0,
    RDT_FILL_EVEN_ODD = 1,
} RdtFillRule;

// 3x3 transform; the last row is 0,0,1 for affine matrices.
typedef struct {
    float e11, e12, e13; // scale-x, shear-x, translate-x
    float e21, e22, e23; // shear-y, scale-y, translate-y
    float e31, e32, e33;
} RdtMatrix;

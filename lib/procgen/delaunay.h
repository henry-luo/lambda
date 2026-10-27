// incremental ghost-triangle Delaunay, preserving the Lambda reference's order.
#ifndef LIB_PROCGEN_DELAUNAY_H
#define LIB_PROCGEN_DELAUNAY_H
#include "predicates.h"
#ifdef __cplusplus
extern "C" {
#endif
// points is interleaved xy; ids contains unique point indices in input order;
// seed is three CCW indices. output has room for 3*(2*count+4) ints, including
// ghosts [u,v,-1]. The caller constructs the public mesh and halfedges.
bool procgen_delaunay(const double* points, int count, const int* ids, int used,
    const int seed[3], ProcgenExactPredicate exact, int* output, int* triangles);
#ifdef __cplusplus
}
#endif
#endif

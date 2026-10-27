// filtered signs with a caller-supplied exact fallback; no runtime dependency (D7.1.1).
#ifndef LIB_PROCGEN_PREDICATES_H
#define LIB_PROCGEN_PREDICATES_H
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef bool (*ProcgenExactPredicate)(const double*, int, int, bool, int*);
bool procgen_predicate_filter(const double* points, int count, int dimension, bool lifted, int* sign);
#ifdef __cplusplus
}
#endif
#endif

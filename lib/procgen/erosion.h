// batch hydraulic erosion; all storage and parameters are numeric (D7.1.1).
#ifndef LIB_PROCGEN_EROSION_H
#define LIB_PROCGEN_EROSION_H
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct ProcgenErosion {
    double inertia, capacity, min_capacity, erode, deposit, evaporate, gravity;
    int steps;
} ProcgenErosion;
// input/output have width*height finite doubles. Returns false on allocation
// failure or non-finite intermediate arithmetic; inputs are never mutated.
bool procgen_hydraulic(uint64_t key, const double* input, int width, int height,
    int droplets, int batch, const ProcgenErosion* settings, double* output);
#ifdef __cplusplus
}
#endif
#endif

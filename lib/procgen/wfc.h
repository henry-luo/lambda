// context-free WFC batch kernel; Lambda owns model construction and options (D7.1.1).
#ifndef LIB_PROCGEN_WFC_H
#define LIB_PROCGEN_WFC_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum ProcgenWfcResult {
    PROCGEN_WFC_OK,
    PROCGEN_WFC_CONTRADICTION,
    PROCGEN_WFC_NO_MEMORY
} ProcgenWfcResult;

// masks[(direction * patterns + source) * patterns + target] is 0 or 1.
// Caller validates: 1..1000 patterns, 1..10000 cells, positive finite weights
// summing to at most 2^53, and positive retries. Output has width*height ints.
ProcgenWfcResult procgen_wfc(uint64_t key, int width, int height, bool periodic,
    int64_t retries, int patterns, const double* weights, const uint8_t* masks, int* output);

#ifdef __cplusplus
}
#endif
#endif

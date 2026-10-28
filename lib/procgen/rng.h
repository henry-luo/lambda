// shared keyed draws for native ports of procart.rng (PG3, S4.1.5).
#ifndef LIB_PROCGEN_RNG_H
#define LIB_PROCGEN_RNG_H
#include "../math_utils.h"
static inline uint64_t procgen_key_at(uint64_t key, uint64_t index) {
    return math_splitmix64_mix((key ^ index) + UINT64_C(0x9e3779b97f4a7c15));
}
#endif

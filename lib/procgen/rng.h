// shared keyed draws for native ports of procart.rng (PG3, S4.1.5).
#ifndef LIB_PROCGEN_RNG_H
#define LIB_PROCGEN_RNG_H
#include "../math_utils.h"
#include "../str.h"
// PG3: positional SplitMix folding and FNV over Unicode scalars match rng.ls.
static inline uint64_t procgen_key_step(uint64_t key, uint64_t bits, uint64_t position) {
    return math_splitmix64_mix((key ^ bits) + UINT64_C(0x9e3779b97f4a7c15) * (position + 1));
}
static inline uint64_t procgen_key_at(uint64_t key, uint64_t index) {
    return procgen_key_step(key, index, 0);
}
static inline bool procgen_text_key(const char* text, size_t length, uint64_t* result) {
    uint64_t hash = UINT64_C(0xcbf29ce484222325);
    for (size_t offset = 0; offset < length;) {
        uint32_t point;
        int width = str_utf8_decode(text + offset, length - offset, &point);
        if (width <= 0) return false;
        hash = (hash ^ point) * UINT64_C(0x100000001b3);
        offset += (size_t)width;
    }
    *result = hash;
    return true;
}
#endif

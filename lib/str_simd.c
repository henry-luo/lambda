/**
 * str_simd.c — the SIMD tier of the str_find kernel (GRP8, vibe/Lambda_Lib_Grep.md §5.3).
 *
 * Packed pair: a needle's two rarest positions are tested together, 16
 * candidate starts at a time, and the needle is verified only where both
 * bytes agree. One byte alone stops at every occurrence of a common letter;
 * two bytes at a fixed distance rarely agree by chance. In fold mode each
 * position matches either case of its byte, in the same pass.
 *
 * NEON (ARM64) and SSE2 (always present on x86-64) need no runtime CPU
 * detection; other targets take the scalar loop, which is also the
 * reference the SIMD forms are tested against.
 */

#include "str.h"
#include <string.h>

#if defined(__SSE2__)
#include <emmintrin.h>
#elif defined(__ARM_NEON)
#include <arm_neon.h>
#endif

static inline bool pair_at(const StrNeedle* n, const unsigned char* p) {
    unsigned char a = p[n->rare_at], b = p[n->pair_at];
    return (a == n->rare || a == n->rare_alt) && (b == n->pair || b == n->pair_alt);
}

static inline int ctz64(uint64_t v) {
#if defined(__GNUC__)
    return __builtin_ctzll(v);
#else
    int c = 0;
    while (!(v & 1)) { v >>= 1; c++; }
    return c;
#endif
}

size_t str_needle_find_pair(const StrNeedle* n, const char* s, size_t s_len) {
    if (!s || n->len < 2 || n->len > s_len) return STR_NPOS;
    const unsigned char* base = (const unsigned char*)s;
    size_t starts = s_len - n->len + 1;   /* candidate starts are [0, starts) */
    size_t i = 0;
#if defined(__SSE2__)
    const __m128i r1 = _mm_set1_epi8((char)n->rare), r2 = _mm_set1_epi8((char)n->rare_alt);
    const __m128i p1 = _mm_set1_epi8((char)n->pair), p2 = _mm_set1_epi8((char)n->pair_alt);
    for (; i + 16 <= starts; i += 16) {
        /* loads end at most at s_len - 1: the last start plus the far offset */
        __m128i a = _mm_loadu_si128((const __m128i*)(base + i + n->rare_at));
        __m128i b = _mm_loadu_si128((const __m128i*)(base + i + n->pair_at));
        __m128i eq = _mm_and_si128(_mm_or_si128(_mm_cmpeq_epi8(a, r1), _mm_cmpeq_epi8(a, r2)),
                                   _mm_or_si128(_mm_cmpeq_epi8(b, p1), _mm_cmpeq_epi8(b, p2)));
        unsigned mask = (unsigned)_mm_movemask_epi8(eq);
        while (mask) {
            int k = ctz64(mask);
            if (str_needle_equal_at(n, s + i + k)) return i + k;
            mask &= mask - 1;
        }
    }
#elif defined(__ARM_NEON)
    const uint8x16_t r1 = vdupq_n_u8(n->rare), r2 = vdupq_n_u8(n->rare_alt);
    const uint8x16_t p1 = vdupq_n_u8(n->pair), p2 = vdupq_n_u8(n->pair_alt);
    for (; i + 16 <= starts; i += 16) {
        uint8x16_t a = vld1q_u8(base + i + n->rare_at);
        uint8x16_t b = vld1q_u8(base + i + n->pair_at);
        uint8x16_t eq = vandq_u8(vorrq_u8(vceqq_u8(a, r1), vceqq_u8(a, r2)),
                                 vorrq_u8(vceqq_u8(b, p1), vceqq_u8(b, p2)));
        /* no movemask on NEON: narrowing packs the 16 lanes into 16 nibbles */
        uint64_t mask = vget_lane_u64(vreinterpret_u64_u8(vshrn_n_u16(vreinterpretq_u16_u8(eq), 4)), 0);
        while (mask) {
            int k = ctz64(mask) >> 2;
            if (str_needle_equal_at(n, s + i + k)) return i + (size_t)k;
            mask &= ~((uint64_t)0xF << (4 * k));
        }
    }
#endif
    for (; i < starts; i++) {
        if (pair_at(n, base + i) && str_needle_equal_at(n, s + i)) return i;
    }
    return STR_NPOS;
}

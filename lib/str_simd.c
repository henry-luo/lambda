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

/* ── ASCII word runs (FTX3: lib/fts finds word boundaries 16 bytes at a time) ── */

static inline bool ascii_alnum(unsigned char c) {
    return (unsigned char)((c | 0x20) - 'a') <= 25 || (unsigned char)(c - '0') <= 9;
}

size_t str_ascii_alnum_span(const char* s, size_t len, bool* has_upper) {
    const unsigned char* p = (const unsigned char*)s;
    size_t i = 0;
    bool upper = false;
#if defined(__SSE2__)
    const __m128i a = _mm_set1_epi8('a'), A = _mm_set1_epi8('A'), zero = _mm_set1_epi8('0');
    const __m128i bit = _mm_set1_epi8(0x20), k25 = _mm_set1_epi8(25), k9 = _mm_set1_epi8(9);
    for (; i + 16 <= len; i += 16) {
        __m128i v = _mm_loadu_si128((const __m128i*)(p + i));
        /* x <= k unsigned: min(x, k) == x */
        __m128i la = _mm_sub_epi8(_mm_or_si128(v, bit), a);
        __m128i alpha = _mm_cmpeq_epi8(_mm_min_epu8(la, k25), la);
        __m128i d = _mm_sub_epi8(v, zero);
        __m128i digit = _mm_cmpeq_epi8(_mm_min_epu8(d, k9), d);
        __m128i ua = _mm_sub_epi8(v, A);
        __m128i up = _mm_cmpeq_epi8(_mm_min_epu8(ua, k25), ua);
        unsigned word = (unsigned)_mm_movemask_epi8(_mm_or_si128(alpha, digit));
        unsigned ups = (unsigned)_mm_movemask_epi8(up);
        if (word != 0xFFFF) {
            int k = ctz64(~word & 0xFFFF);
            upper = upper || (ups & ((1u << k) - 1)) != 0;
            if (has_upper) *has_upper = upper;
            return i + (size_t)k;
        }
        upper = upper || ups != 0;
    }
#elif defined(__ARM_NEON)
    const uint8x16_t a = vdupq_n_u8('a'), A = vdupq_n_u8('A'), zero = vdupq_n_u8('0');
    const uint8x16_t bit = vdupq_n_u8(0x20), k25 = vdupq_n_u8(25), k9 = vdupq_n_u8(9);
    for (; i + 16 <= len; i += 16) {
        uint8x16_t v = vld1q_u8(p + i);
        uint8x16_t word = vorrq_u8(vcleq_u8(vsubq_u8(vorrq_u8(v, bit), a), k25), vcleq_u8(vsubq_u8(v, zero), k9));
        uint8x16_t up = vcleq_u8(vsubq_u8(v, A), k25);
        /* no movemask on NEON: narrowing packs the 16 lanes into 16 nibbles */
        uint64_t stop = ~vget_lane_u64(vreinterpret_u64_u8(vshrn_n_u16(vreinterpretq_u16_u8(word), 4)), 0);
        uint64_t ups = vget_lane_u64(vreinterpret_u64_u8(vshrn_n_u16(vreinterpretq_u16_u8(up), 4)), 0);
        if (stop) {
            int k = ctz64(stop) >> 2;
            upper = upper || (k && (ups & (~(uint64_t)0 >> (64 - 4 * k))) != 0);
            if (has_upper) *has_upper = upper;
            return i + (size_t)k;
        }
        upper = upper || ups != 0;
    }
#endif
    for (; i < len && ascii_alnum(p[i]); i++) upper = upper || (unsigned char)(p[i] - 'A') <= 25;
    if (has_upper) *has_upper = upper;
    return i;
}

size_t str_ascii_nonalnum_span(const char* s, size_t len) {
    const unsigned char* p = (const unsigned char*)s;
    size_t i = 0;
#if defined(__SSE2__)
    const __m128i a = _mm_set1_epi8('a'), zero = _mm_set1_epi8('0');
    const __m128i bit = _mm_set1_epi8(0x20), k25 = _mm_set1_epi8(25), k9 = _mm_set1_epi8(9);
    for (; i + 16 <= len; i += 16) {
        __m128i v = _mm_loadu_si128((const __m128i*)(p + i));
        __m128i la = _mm_sub_epi8(_mm_or_si128(v, bit), a);
        __m128i alpha = _mm_cmpeq_epi8(_mm_min_epu8(la, k25), la);
        __m128i d = _mm_sub_epi8(v, zero);
        __m128i digit = _mm_cmpeq_epi8(_mm_min_epu8(d, k9), d);
        /* a stop: a letter or digit, or a byte >= 0x80 (the sign bit) */
        unsigned stop = (unsigned)_mm_movemask_epi8(_mm_or_si128(_mm_or_si128(alpha, digit), v));
        if (stop) return i + (size_t)ctz64(stop);
    }
#elif defined(__ARM_NEON)
    const uint8x16_t a = vdupq_n_u8('a'), zero = vdupq_n_u8('0'), high = vdupq_n_u8(0x80);
    const uint8x16_t bit = vdupq_n_u8(0x20), k25 = vdupq_n_u8(25), k9 = vdupq_n_u8(9);
    for (; i + 16 <= len; i += 16) {
        uint8x16_t v = vld1q_u8(p + i);
        uint8x16_t stop = vorrq_u8(vorrq_u8(vcleq_u8(vsubq_u8(vorrq_u8(v, bit), a), k25),
                                            vcleq_u8(vsubq_u8(v, zero), k9)),
                                   vcgeq_u8(v, high));
        uint64_t mask = vget_lane_u64(vreinterpret_u64_u8(vshrn_n_u16(vreinterpretq_u16_u8(stop), 4)), 0);
        if (mask) return i + (size_t)(ctz64(mask) >> 2);
    }
#endif
    for (; i < len && p[i] < 0x80 && !ascii_alnum(p[i]); i++) {}
    return i;
}

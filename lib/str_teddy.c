/**
 * str_teddy.c — Teddy multi-literal search (GRP29, vibe/Lambda_Lib_Grep.md §5.3).
 *
 * Teddy (from Intel's Hyperscan) finds the leftmost occurrence of any of a
 * small set of literals in one pass. Each literal goes into one of 8 buckets
 * and its first 1–4 bytes are its fingerprint. For fingerprint byte j, two
 * 16-entry tables map a byte's low and high nibble to the buckets that have
 * that nibble there; ANDing the two lookups, and the results for every
 * fingerprint byte, leaves the buckets that may start at a position. Only
 * those positions are verified against the bucket's literals. The nibble split
 * lets one shuffle instruction (`tbl` on NEON, `pshufb` on SSSE3) look up 16
 * bytes at once.
 *
 * NEON is always present on ARM64. On x86-64 the SSE2 baseline lacks pshufb,
 * so the SSSE3 kernel is compiled with a function target attribute and chosen
 * by one CPUID check per process. Other CPUs take the portable loop, which is
 * also the reference the SIMD forms are tested against.
 */

#include "str.h"
#include <string.h>

#if defined(__aarch64__) && defined(__ARM_NEON)
#include <arm_neon.h>
#define TEDDY_NEON 1
#elif defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#include <tmmintrin.h>
#if defined(_MSC_VER) && !defined(__clang__)
#include <intrin.h>
#else
#include <cpuid.h>
#endif
#define TEDDY_X86 1
#endif

static inline unsigned char teddy_lower(unsigned char c) { return (c >= 'A' && c <= 'Z') ? (unsigned char)(c + 32) : c; }
static inline unsigned char teddy_upper(unsigned char c) { return (c >= 'a' && c <= 'z') ? (unsigned char)(c - 32) : c; }

static inline int teddy_ctz(uint64_t v) {
#if defined(__GNUC__)
    return __builtin_ctzll(v);
#else
    int c = 0;
    while (!(v & 1)) { v >>= 1; c++; }
    return c;
#endif
}

bool str_teddy_init(StrTeddy* t, const char* const* bytes, const size_t* lens,
                    const bool* fold, int count) {
    memset(t, 0, sizeof(*t));
    if (!bytes || !lens || count <= 0 || count > STR_TEDDY_MAX) return false;
    t->count = count;
    t->min_len = SIZE_MAX;
    for (int i = 0; i < count; i++) {
        if (!bytes[i] || lens[i] == 0) return false;
        t->bytes[i] = bytes[i];
        t->lens[i] = lens[i];
        t->fold[i] = fold && fold[i];
        if (lens[i] < t->min_len) t->min_len = lens[i];
    }
    // each fingerprint byte divides the false candidates by up to 16: in
    // lowercase text the high nibbles barely differ, so with three literals in
    // a bucket a 3-byte fingerprint still passed about 5% of positions. With
    // one literal per bucket 3 bytes already pass ~0.2%, and a 4th load costs
    // more than it saves (measured: 3 literals 40 → 52 ms, 24: 206 → 98 ms).
    int want = count > 8 ? 4 : 3;
    t->fp_len = t->min_len < (size_t)want ? (int)t->min_len : want;

    // literals with similar fingerprints share a bucket, so a bucket's tables
    // mix few distinct nibbles: sort by fingerprint, then cut into 8 runs
    int order[STR_TEDDY_MAX];
    for (int i = 0; i < count; i++) order[i] = i;
    for (int i = 1; i < count; i++) {
        int v = order[i], j = i - 1;
        while (j >= 0) {
            int cmp = 0;
            for (int k = 0; k < t->fp_len && !cmp; k++) {
                unsigned char a = (unsigned char)bytes[order[j]][k], b = (unsigned char)bytes[v][k];
                if (t->fold[order[j]]) a = teddy_lower(a);
                if (t->fold[v]) b = teddy_lower(b);
                cmp = (int)a - (int)b;
            }
            if (cmp <= 0) break;
            order[j + 1] = order[j];
            j--;
        }
        order[j + 1] = v;
    }
    for (int r = 0; r < count; r++) {
        int idx = order[r];
        int b = count <= 8 ? r : (r * 8) / count;
        t->bucket_lits[b][t->bucket_count[b]++] = (uint8_t)idx;
        for (int j = 0; j < t->fp_len; j++) {
            unsigned char c = (unsigned char)bytes[idx][j];
            unsigned char variants[2] = {c, c};
            if (t->fold[idx]) {
                variants[0] = teddy_lower(c);
                variants[1] = teddy_upper(c);
            }
            for (int v = 0; v < 2; v++) {
                t->lo[j][variants[v] & 15] |= (uint8_t)(1u << b);
                t->hi[j][variants[v] >> 4] |= (uint8_t)(1u << b);
            }
        }
    }
    return true;
}

static inline bool teddy_literal_at(const StrTeddy* t, int idx, const unsigned char* at) {
    const unsigned char* lit = (const unsigned char*)t->bytes[idx];
    size_t n = t->lens[idx];
    // the fingerprint matched nibble by nibble, not byte by byte: most false
    // candidates fail on the first byte, before a call to memcmp
    if (!t->fold[idx]) return at[0] == lit[0] && memcmp(at, lit, n) == 0;
    for (size_t i = 0; i < n; i++) {
        if (teddy_lower(at[i]) != teddy_lower(lit[i])) return false;
    }
    return true;
}

// verify the literals of every bucket in `buckets` at pos
static bool teddy_verify(const StrTeddy* t, const unsigned char* s, size_t n, size_t pos,
                         unsigned buckets, int* which) {
    while (buckets) {
        int b = teddy_ctz(buckets);
        buckets &= buckets - 1;
        for (int k = 0; k < t->bucket_count[b]; k++) {
            int idx = t->bucket_lits[b][k];
            if (pos + t->lens[idx] <= n && teddy_literal_at(t, idx, s + pos)) {
                if (which) *which = idx;
                return true;
            }
        }
    }
    return false;
}

static size_t teddy_portable_from(const StrTeddy* t, const unsigned char* s, size_t n, size_t i, int* which) {
    const int fp = t->fp_len;
    // min_len >= fp, so every fingerprint byte read below is in range
    for (; i + t->min_len <= n; i++) {
        unsigned b = t->lo[0][s[i] & 15] & t->hi[0][s[i] >> 4];
        if (b && fp > 1) b &= t->lo[1][s[i + 1] & 15] & t->hi[1][s[i + 1] >> 4];
        if (b && fp > 2) b &= t->lo[2][s[i + 2] & 15] & t->hi[2][s[i + 2] >> 4];
        if (b && fp > 3) b &= t->lo[3][s[i + 3] & 15] & t->hi[3][s[i + 3] >> 4];
        if (b && teddy_verify(t, s, n, i, b, which)) return i;
    }
    return STR_NPOS;
}

#if TEDDY_NEON
static size_t teddy_neon(const StrTeddy* t, const unsigned char* s, size_t n, int* which) {
    const int fp = t->fp_len;
    const uint8x16_t nib = vdupq_n_u8(0x0F);
    const uint8x16_t lo0 = vld1q_u8(t->lo[0]), hi0 = vld1q_u8(t->hi[0]);
    const uint8x16_t lo1 = vld1q_u8(t->lo[1]), hi1 = vld1q_u8(t->hi[1]);
    const uint8x16_t lo2 = vld1q_u8(t->lo[2]), hi2 = vld1q_u8(t->hi[2]);
    const uint8x16_t lo3 = vld1q_u8(t->lo[3]), hi3 = vld1q_u8(t->hi[3]);
    size_t i = 0;
    // the loads at i + j (j < fp) end at i + 15 + fp - 1 < n
    for (; i + 16 + (size_t)(fp - 1) <= n; i += 16) {
        uint8x16_t c = vld1q_u8(s + i);
        uint8x16_t r = vandq_u8(vqtbl1q_u8(lo0, vandq_u8(c, nib)), vqtbl1q_u8(hi0, vshrq_n_u8(c, 4)));
        if (fp > 1) {
            c = vld1q_u8(s + i + 1);
            r = vandq_u8(r, vandq_u8(vqtbl1q_u8(lo1, vandq_u8(c, nib)), vqtbl1q_u8(hi1, vshrq_n_u8(c, 4))));
        }
        if (fp > 2) {
            c = vld1q_u8(s + i + 2);
            r = vandq_u8(r, vandq_u8(vqtbl1q_u8(lo2, vandq_u8(c, nib)), vqtbl1q_u8(hi2, vshrq_n_u8(c, 4))));
        }
        if (fp > 3) {
            c = vld1q_u8(s + i + 3);
            r = vandq_u8(r, vandq_u8(vqtbl1q_u8(lo3, vandq_u8(c, nib)), vqtbl1q_u8(hi3, vshrq_n_u8(c, 4))));
        }
        // one nibble per lane: no movemask on NEON
        uint64_t mask = vget_lane_u64(vreinterpret_u64_u8(
            vshrn_n_u16(vreinterpretq_u16_u8(vtstq_u8(r, r)), 4)), 0);
        if (!mask) continue;
        uint8_t lanes[16];
        vst1q_u8(lanes, r);
        while (mask) {
            int k = teddy_ctz(mask) >> 2;
            if (teddy_verify(t, s, n, i + (size_t)k, lanes[k], which)) return i + (size_t)k;
            mask &= ~((uint64_t)0xF << (4 * k));
        }
    }
    return teddy_portable_from(t, s, n, i, which);
}
#endif

#if TEDDY_X86
#if defined(__GNUC__) || defined(__clang__)
#define TEDDY_TARGET_SSSE3 __attribute__((target("ssse3")))
#else
#define TEDDY_TARGET_SSSE3
#endif

TEDDY_TARGET_SSSE3
static size_t teddy_ssse3(const StrTeddy* t, const unsigned char* s, size_t n, int* which) {
    const int fp = t->fp_len;
    const __m128i nib = _mm_set1_epi8(0x0F);
    const __m128i zero = _mm_setzero_si128();
    const __m128i lo0 = _mm_loadu_si128((const __m128i*)t->lo[0]), hi0 = _mm_loadu_si128((const __m128i*)t->hi[0]);
    const __m128i lo1 = _mm_loadu_si128((const __m128i*)t->lo[1]), hi1 = _mm_loadu_si128((const __m128i*)t->hi[1]);
    const __m128i lo2 = _mm_loadu_si128((const __m128i*)t->lo[2]), hi2 = _mm_loadu_si128((const __m128i*)t->hi[2]);
    const __m128i lo3 = _mm_loadu_si128((const __m128i*)t->lo[3]), hi3 = _mm_loadu_si128((const __m128i*)t->hi[3]);
    size_t i = 0;
    for (; i + 16 + (size_t)(fp - 1) <= n; i += 16) {
        // the 16-bit shift moves each byte's high nibble down; the mask drops
        // what crossed in from the neighbouring byte
        __m128i c = _mm_loadu_si128((const __m128i*)(s + i));
        __m128i r = _mm_and_si128(_mm_shuffle_epi8(lo0, _mm_and_si128(c, nib)),
                                  _mm_shuffle_epi8(hi0, _mm_and_si128(_mm_srli_epi16(c, 4), nib)));
        if (fp > 1) {
            c = _mm_loadu_si128((const __m128i*)(s + i + 1));
            r = _mm_and_si128(r, _mm_and_si128(_mm_shuffle_epi8(lo1, _mm_and_si128(c, nib)),
                                               _mm_shuffle_epi8(hi1, _mm_and_si128(_mm_srli_epi16(c, 4), nib))));
        }
        if (fp > 2) {
            c = _mm_loadu_si128((const __m128i*)(s + i + 2));
            r = _mm_and_si128(r, _mm_and_si128(_mm_shuffle_epi8(lo2, _mm_and_si128(c, nib)),
                                               _mm_shuffle_epi8(hi2, _mm_and_si128(_mm_srli_epi16(c, 4), nib))));
        }
        if (fp > 3) {
            c = _mm_loadu_si128((const __m128i*)(s + i + 3));
            r = _mm_and_si128(r, _mm_and_si128(_mm_shuffle_epi8(lo3, _mm_and_si128(c, nib)),
                                               _mm_shuffle_epi8(hi3, _mm_and_si128(_mm_srli_epi16(c, 4), nib))));
        }
        unsigned mask = (unsigned)_mm_movemask_epi8(_mm_cmpeq_epi8(r, zero)) ^ 0xFFFFu;
        if (!mask) continue;
        uint8_t lanes[16];
        _mm_storeu_si128((__m128i*)lanes, r);
        while (mask) {
            int k = teddy_ctz(mask);
            if (teddy_verify(t, s, n, i + (size_t)k, lanes[k], which)) return i + (size_t)k;
            mask &= mask - 1;
        }
    }
    return teddy_portable_from(t, s, n, i, which);
}

// SSSE3 is CPUID leaf 1, ECX bit 9. Checked once; the cached answer is the
// same for every thread, so a relaxed race on first use is harmless.
static bool teddy_has_ssse3(void) {
    static int cached = -1;
    int known = __atomic_load_n(&cached, __ATOMIC_RELAXED);
    if (known >= 0) return known != 0;
    unsigned int ecx = 0;
#if defined(_MSC_VER) && !defined(__clang__)
    int regs[4];
    __cpuid(regs, 1);
    ecx = (unsigned int)regs[2];
#else
    unsigned int eax = 0, ebx = 0, edx = 0;
    if (!__get_cpuid(1, &eax, &ebx, &ecx, &edx)) ecx = 0;
#endif
    known = (ecx >> 9) & 1u;
    __atomic_store_n(&cached, known, __ATOMIC_RELAXED);
    return known != 0;
}
#endif

size_t str_teddy_find(const StrTeddy* t, const char* s, size_t s_len, int* which) {
    if (!t || !s || t->count == 0 || s_len < t->min_len) return STR_NPOS;
    const unsigned char* bytes = (const unsigned char*)s;
#if TEDDY_NEON
    return teddy_neon(t, bytes, s_len, which);
#else
#if TEDDY_X86
    if (teddy_has_ssse3()) return teddy_ssse3(t, bytes, s_len, which);
#endif
    return teddy_portable_from(t, bytes, s_len, 0, which);
#endif
}

size_t str_teddy_find_portable(const StrTeddy* t, const char* s, size_t s_len, int* which) {
    if (!t || !s || t->count == 0 || s_len < t->min_len) return STR_NPOS;
    return teddy_portable_from(t, (const unsigned char*)s, s_len, 0, which);
}

const char* str_teddy_kernel(void) {
#if TEDDY_NEON
    return "neon";
#else
#if TEDDY_X86
    if (teddy_has_ssse3()) return "ssse3";
#endif
    return "portable";
#endif
}

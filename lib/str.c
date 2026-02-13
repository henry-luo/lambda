/**
 * str.c — Safe, convenient, high-performance C string library for Lambda.
 *
 * Implementation notes:
 * - SWAR (SIMD Within A Register) used for hot loops: byte-equality scans,
 *   ASCII checks, tolower/toupper transforms — all process 8 bytes per cycle.
 * - NULL inputs are treated as empty (length 0) — never crash.
 * - All outputs NUL-terminated where applicable.
 */

#include "str.h"
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <ctype.h>
#include <errno.h>
#include <math.h>

/* ── SWAR helpers ─────────────────────────────────────────────────── */

/* broadcast a single byte to all 8 positions of a uint64_t */
static inline uint64_t _swar_broadcast(uint8_t c) {
    return (uint64_t)c * 0x0101010101010101ULL;
}

/* detect which bytes in a word are zero (have their high bit set in result) */
static inline uint64_t _swar_has_zero(uint64_t v) {
    return (v - 0x0101010101010101ULL) & ~v & 0x8080808080808080ULL;
}

/* detect which bytes in a word equal `c` */
static inline uint64_t _swar_has_byte(uint64_t word, uint8_t c) {
    return _swar_has_zero(word ^ _swar_broadcast(c));
}

/* detect any byte with high bit set (non-ASCII) */
static inline uint64_t _swar_has_highbit(uint64_t v) {
    return v & 0x8080808080808080ULL;
}

/* safe unaligned 64-bit load */
static inline uint64_t _load_u64(const void* p) {
    uint64_t v;
    memcpy(&v, p, 8);
    return v;
}

/* safe unaligned 64-bit store */
static inline void _store_u64(void* p, uint64_t v) {
    memcpy(p, &v, 8);
}

/* count trailing zeros (byte-index of first match) */
static inline int _ctz64(uint64_t v) {
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_ctzll(v);
#elif defined(_MSC_VER) && defined(_WIN64)
    unsigned long idx;
    _BitScanForward64(&idx, v);
    return (int)idx;
#else
    int n = 0;
    if (!(v & 0xFFFFFFFF)) { n += 32; v >>= 32; }
    if (!(v & 0xFFFF))     { n += 16; v >>= 16; }
    if (!(v & 0xFF))       { n += 8;  v >>= 8;  }
    if (!(v & 0xF))        { n += 4;  v >>= 4;  }
    if (!(v & 0x3))        { n += 2;  v >>= 2;  }
    if (!(v & 0x1))        { n += 1; }
    return n;
#endif
}

/* count leading zeros */
static inline int _clz64(uint64_t v) {
#if defined(__GNUC__) || defined(__clang__)
    return v ? __builtin_clzll(v) : 64;
#else
    int n = 0;
    if (!(v & 0xFFFFFFFF00000000ULL)) { n += 32; v <<= 32; }
    if (!(v & 0xFFFF000000000000ULL)) { n += 16; v <<= 16; }
    if (!(v & 0xFF00000000000000ULL)) { n += 8;  v <<= 8;  }
    if (!(v & 0xF000000000000000ULL)) { n += 4;  v <<= 4;  }
    if (!(v & 0xC000000000000000ULL)) { n += 2;  v <<= 2;  }
    if (!(v & 0x8000000000000000ULL)) { n += 1; }
    return n;
#endif
}

/* ── static LUT for tolower / toupper ─────────────────────────────── */

static uint8_t _lut_lower[256];
static uint8_t _lut_upper[256];
static bool    _lut_inited = false;

static void _ensure_luts(void) {
    if (_lut_inited) return;
    for (int i = 0; i < 256; i++) {
        _lut_lower[i] = (uint8_t)i;
        _lut_upper[i] = (uint8_t)i;
    }
    for (int i = 'A'; i <= 'Z'; i++) _lut_lower[i] = (uint8_t)(i + 32);
    for (int i = 'a'; i <= 'z'; i++) _lut_upper[i] = (uint8_t)(i - 32);
    _lut_inited = true;
}

/* ══════════════════════════════════════════════════════════════════════
 *  1. Comparison
 * ══════════════════════════════════════════════════════════════════════ */

int str_cmp(const char* a, size_t a_len, const char* b, size_t b_len) {
    if (!a) a_len = 0;
    if (!b) b_len = 0;
    size_t min_len = a_len < b_len ? a_len : b_len;
    int r = min_len ? memcmp(a, b, min_len) : 0;
    if (r != 0) return r;
    return (a_len > b_len) - (a_len < b_len);
}

int str_icmp(const char* a, size_t a_len, const char* b, size_t b_len) {
    if (!a) a_len = 0;
    if (!b) b_len = 0;
    _ensure_luts();
    size_t min_len = a_len < b_len ? a_len : b_len;
    for (size_t i = 0; i < min_len; i++) {
        int ca = _lut_lower[(unsigned char)a[i]];
        int cb = _lut_lower[(unsigned char)b[i]];
        if (ca != cb) return ca - cb;
    }
    return (a_len > b_len) - (a_len < b_len);
}

bool str_eq(const char* a, size_t a_len, const char* b, size_t b_len) {
    if (!a) a_len = 0;
    if (!b) b_len = 0;
    if (a_len != b_len) return false;
    if (a_len == 0) return true;
    /* SWAR fast path for longer strings */
    size_t i = 0;
    for (; i + 8 <= a_len; i += 8) {
        if (_load_u64(a + i) != _load_u64(b + i)) return false;
    }
    /* tail bytes */
    for (; i < a_len; i++) {
        if (a[i] != b[i]) return false;
    }
    return true;
}

bool str_ieq(const char* a, size_t a_len, const char* b, size_t b_len) {
    if (!a) a_len = 0;
    if (!b) b_len = 0;
    if (a_len != b_len) return false;
    return str_icmp(a, a_len, b, b_len) == 0;
}

bool str_eq_lit(const char* s, size_t len, const char* lit) {
    if (!s) len = 0;
    if (!lit) return len == 0;
    size_t lit_len = strlen(lit);
    return str_eq(s, len, lit, lit_len);
}

bool str_ieq_lit(const char* s, size_t len, const char* lit) {
    if (!s) len = 0;
    if (!lit) return len == 0;
    size_t lit_len = strlen(lit);
    return str_ieq(s, len, lit, lit_len);
}

/* ══════════════════════════════════════════════════════════════════════
 *  2. Prefix / Suffix
 * ══════════════════════════════════════════════════════════════════════ */

bool str_starts_with(const char* s, size_t s_len,
                     const char* prefix, size_t prefix_len) {
    if (!s) s_len = 0;
    if (!prefix) prefix_len = 0;
    if (prefix_len > s_len) return false;
    return memcmp(s, prefix, prefix_len) == 0;
}

bool str_ends_with(const char* s, size_t s_len,
                   const char* suffix, size_t suffix_len) {
    if (!s) s_len = 0;
    if (!suffix) suffix_len = 0;
    if (suffix_len > s_len) return false;
    return memcmp(s + s_len - suffix_len, suffix, suffix_len) == 0;
}

bool str_starts_with_lit(const char* s, size_t s_len, const char* prefix) {
    if (!prefix) return true;
    return str_starts_with(s, s_len, prefix, strlen(prefix));
}

bool str_ends_with_lit(const char* s, size_t s_len, const char* suffix) {
    if (!suffix) return true;
    return str_ends_with(s, s_len, suffix, strlen(suffix));
}

bool str_istarts_with(const char* s, size_t s_len,
                      const char* prefix, size_t prefix_len) {
    if (!s) s_len = 0;
    if (!prefix) prefix_len = 0;
    if (prefix_len > s_len) return false;
    return str_icmp(s, prefix_len, prefix, prefix_len) == 0;
}

bool str_iends_with(const char* s, size_t s_len,
                    const char* suffix, size_t suffix_len) {
    if (!s) s_len = 0;
    if (!suffix) suffix_len = 0;
    if (suffix_len > s_len) return false;
    return str_icmp(s + s_len - suffix_len, suffix_len, suffix, suffix_len) == 0;
}

/* ══════════════════════════════════════════════════════════════════════
 *  3. Search
 * ══════════════════════════════════════════════════════════════════════ */

size_t str_find_byte(const char* s, size_t len, char c) {
    if (!s || len == 0) return STR_NPOS;
    /* SWAR scan for the byte */
    size_t i = 0;
    if (len >= 8) {
        uint64_t mask;
        for (; i + 8 <= len; i += 8) {
            mask = _swar_has_byte(_load_u64(s + i), (uint8_t)c);
            if (mask) return i + _ctz64(mask) / 8;
        }
    }
    for (; i < len; i++) {
        if (s[i] == c) return i;
    }
    return STR_NPOS;
}

size_t str_rfind_byte(const char* s, size_t len, char c) {
    if (!s || len == 0) return STR_NPOS;
    /* scan from end, SWAR on 8-byte chunks */
    size_t tail = len % 8;
    size_t i = len;
    /* handle tail bytes */
    while (tail-- > 0) {
        i--;
        if (s[i] == c) return i;
    }
    /* SWAR scan backwards */
    while (i >= 8) {
        i -= 8;
        uint64_t mask = _swar_has_byte(_load_u64(s + i), (uint8_t)c);
        if (mask) {
            /* highest set bit position → last matching byte */
            return i + 7 - _clz64(mask) / 8;
        }
    }
    return STR_NPOS;
}

size_t str_find(const char* s, size_t s_len,
                const char* needle, size_t needle_len) {
    if (!s) s_len = 0;
    if (!needle) needle_len = 0;
    if (needle_len == 0) return 0;
    if (needle_len > s_len) return STR_NPOS;
    if (needle_len == 1) return str_find_byte(s, s_len, needle[0]);

    /* two-byte filter + verify: scan for first byte, check second, then full memcmp */
    char first = needle[0];
    char second = needle[1];
    size_t limit = s_len - needle_len;

    for (size_t i = 0; i <= limit; i++) {
        /* find first byte using SWAR */
        size_t pos = str_find_byte(s + i, s_len - i, first);
        if (pos == STR_NPOS || pos + i > limit) return STR_NPOS;
        i += pos;
        /* quick check second byte before full compare */
        if (s[i + 1] == second &&
            memcmp(s + i + 2, needle + 2, needle_len - 2) == 0) {
            return i;
        }
    }
    return STR_NPOS;
}

size_t str_rfind(const char* s, size_t s_len,
                 const char* needle, size_t needle_len) {
    if (!s) s_len = 0;
    if (!needle) needle_len = 0;
    if (needle_len == 0) return s_len;
    if (needle_len > s_len) return STR_NPOS;
    if (needle_len == 1) return str_rfind_byte(s, s_len, needle[0]);

    for (size_t i = s_len - needle_len + 1; i > 0; ) {
        i--;
        if (s[i] == needle[0] && memcmp(s + i, needle, needle_len) == 0) {
            return i;
        }
    }
    return STR_NPOS;
}

size_t str_ifind(const char* s, size_t s_len,
                 const char* needle, size_t needle_len) {
    if (!s) s_len = 0;
    if (!needle) needle_len = 0;
    if (needle_len == 0) return 0;
    if (needle_len > s_len) return STR_NPOS;

    _ensure_luts();
    size_t limit = s_len - needle_len;
    uint8_t first_lo = _lut_lower[(unsigned char)needle[0]];

    for (size_t i = 0; i <= limit; i++) {
        if (_lut_lower[(unsigned char)s[i]] != first_lo) continue;
        bool match = true;
        for (size_t j = 1; j < needle_len; j++) {
            if (_lut_lower[(unsigned char)s[i + j]] !=
                _lut_lower[(unsigned char)needle[j]]) {
                match = false;
                break;
            }
        }
        if (match) return i;
    }
    return STR_NPOS;
}

bool str_contains(const char* s, size_t s_len,
                  const char* needle, size_t needle_len) {
    return str_find(s, s_len, needle, needle_len) != STR_NPOS;
}

bool str_contains_byte(const char* s, size_t s_len, char c) {
    return str_find_byte(s, s_len, c) != STR_NPOS;
}

size_t str_find_any(const char* s, size_t s_len,
                    const char* chars, size_t chars_len) {
    if (!s || s_len == 0 || !chars || chars_len == 0) return STR_NPOS;
    StrByteSet set;
    str_byteset_clear(&set);
    str_byteset_add_many(&set, chars, chars_len);
    return str_find_byteset(s, s_len, &set);
}

size_t str_find_not_any(const char* s, size_t s_len,
                        const char* chars, size_t chars_len) {
    if (!s || s_len == 0) return STR_NPOS;
    if (!chars || chars_len == 0) return 0; /* all chars are "not in empty set" */
    StrByteSet set;
    str_byteset_clear(&set);
    str_byteset_add_many(&set, chars, chars_len);
    return str_find_not_byteset(s, s_len, &set);
}

size_t str_count(const char* s, size_t s_len,
                 const char* needle, size_t needle_len) {
    if (!s || !needle || needle_len == 0 || needle_len > s_len) return 0;
    size_t count = 0;
    size_t pos = 0;
    while (pos <= s_len - needle_len) {
        size_t found = str_find(s + pos, s_len - pos, needle, needle_len);
        if (found == STR_NPOS) break;
        count++;
        pos += found + needle_len; /* non-overlapping */
    }
    return count;
}

size_t str_count_byte(const char* s, size_t s_len, char c) {
    if (!s) return 0;
    size_t count = 0;
    /* SWAR: use popcount on match mask for bulk counting */
    size_t i = 0;
    for (; i + 8 <= s_len; i += 8) {
        uint64_t mask = _swar_has_byte(_load_u64(s + i), (uint8_t)c);
        /* each matching byte has its high bit set — count those bits / 8 */
        if (mask) {
#if defined(__GNUC__) || defined(__clang__)
            count += (size_t)__builtin_popcountll(mask) / 8;
#else
            while (mask) { count++; mask &= mask - 1; } /* clear lowest set group */
            /* actually need to count set high-bits spaced 8 apart */
#endif
        }
    }
    for (; i < s_len; i++) {
        if (s[i] == c) count++;
    }
    return count;
}

/* ══════════════════════════════════════════════════════════════════════
 *  4. Byte-set
 * ══════════════════════════════════════════════════════════════════════ */

void str_byteset_clear(StrByteSet* set) {
    if (!set) return;
    set->bits[0] = set->bits[1] = set->bits[2] = set->bits[3] = 0;
}

void str_byteset_add(StrByteSet* set, unsigned char c) {
    set->bits[c >> 6] |= (1ULL << (c & 63u));
}

void str_byteset_add_range(StrByteSet* set, unsigned char lo, unsigned char hi) {
    for (unsigned int c = lo; c <= hi; c++) {
        str_byteset_add(set, (unsigned char)c);
    }
}

void str_byteset_add_many(StrByteSet* set, const char* chars, size_t len) {
    if (!chars) return;
    for (size_t i = 0; i < len; i++) {
        str_byteset_add(set, (unsigned char)chars[i]);
    }
}

void str_byteset_invert(StrByteSet* set) {
    if (!set) return;
    set->bits[0] = ~set->bits[0];
    set->bits[1] = ~set->bits[1];
    set->bits[2] = ~set->bits[2];
    set->bits[3] = ~set->bits[3];
}

bool str_byteset_test(const StrByteSet* set, unsigned char c) {
    return (set->bits[c >> 6] & (1ULL << (c & 63u))) != 0;
}

void str_byteset_whitespace(StrByteSet* set) {
    str_byteset_clear(set);
    str_byteset_add(set, ' ');
    str_byteset_add(set, '\t');
    str_byteset_add(set, '\n');
    str_byteset_add(set, '\r');
    str_byteset_add(set, '\f');
    str_byteset_add(set, '\v');
}

void str_byteset_digits(StrByteSet* set) {
    str_byteset_clear(set);
    str_byteset_add_range(set, '0', '9');
}

void str_byteset_alpha(StrByteSet* set) {
    str_byteset_clear(set);
    str_byteset_add_range(set, 'a', 'z');
    str_byteset_add_range(set, 'A', 'Z');
}

void str_byteset_alnum(StrByteSet* set) {
    str_byteset_clear(set);
    str_byteset_add_range(set, '0', '9');
    str_byteset_add_range(set, 'a', 'z');
    str_byteset_add_range(set, 'A', 'Z');
}

size_t str_find_byteset(const char* s, size_t len, const StrByteSet* set) {
    if (!s || !set) return STR_NPOS;
    for (size_t i = 0; i < len; i++) {
        if (str_byteset_test(set, (unsigned char)s[i])) return i;
    }
    return STR_NPOS;
}

size_t str_rfind_byteset(const char* s, size_t len, const StrByteSet* set) {
    if (!s || !set || len == 0) return STR_NPOS;
    for (size_t i = len; i > 0; ) {
        i--;
        if (str_byteset_test(set, (unsigned char)s[i])) return i;
    }
    return STR_NPOS;
}

size_t str_find_not_byteset(const char* s, size_t len, const StrByteSet* set) {
    if (!s || !set) return STR_NPOS;
    for (size_t i = 0; i < len; i++) {
        if (!str_byteset_test(set, (unsigned char)s[i])) return i;
    }
    return STR_NPOS;
}

/* ══════════════════════════════════════════════════════════════════════
 *  5. Trim
 * ══════════════════════════════════════════════════════════════════════ */

static inline bool _is_ws(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' ||
           c == '\f' || c == '\v';
}

void str_ltrim(const char** s, size_t* len) {
    if (!s || !len || !*s) return;
    const char* p = *s;
    size_t n = *len;
    while (n > 0 && _is_ws(*p)) { p++; n--; }
    *s = p;
    *len = n;
}

void str_rtrim(const char** s, size_t* len) {
    if (!s || !len || !*s) return;
    const char* p = *s;
    size_t n = *len;
    while (n > 0 && _is_ws(p[n - 1])) { n--; }
    *len = n;
}

void str_trim(const char** s, size_t* len) {
    str_ltrim(s, len);
    str_rtrim(s, len);
}

void str_trim_chars(const char** s, size_t* len,
                    const char* chars, size_t chars_len) {
    if (!s || !len || !*s || !chars || chars_len == 0) return;
    StrByteSet set;
    str_byteset_clear(&set);
    str_byteset_add_many(&set, chars, chars_len);

    const char* p = *s;
    size_t n = *len;
    while (n > 0 && str_byteset_test(&set, (unsigned char)*p)) { p++; n--; }
    while (n > 0 && str_byteset_test(&set, (unsigned char)p[n - 1])) { n--; }
    *s = p;
    *len = n;
}

/* ══════════════════════════════════════════════════════════════════════
 *  6. Case conversion (LUT-based, SWAR-accelerated)
 * ══════════════════════════════════════════════════════════════════════ */

void str_lut_identity(uint8_t lut[256]) {
    for (int i = 0; i < 256; i++) lut[i] = (uint8_t)i;
}

void str_lut_tolower(uint8_t lut[256]) {
    str_lut_identity(lut);
    for (int i = 'A'; i <= 'Z'; i++) lut[i] = (uint8_t)(i + 32);
}

void str_lut_toupper(uint8_t lut[256]) {
    str_lut_identity(lut);
    for (int i = 'a'; i <= 'z'; i++) lut[i] = (uint8_t)(i - 32);
}

void str_transform(char* dst, const char* src, size_t len,
                   const uint8_t lut[256]) {
    if (!dst || !src || len == 0) return;
    for (size_t i = 0; i < len; i++) {
        dst[i] = (char)lut[(unsigned char)src[i]];
    }
}

void str_to_lower(char* dst, const char* src, size_t len) {
    if (!dst || !src || len == 0) return;
    _ensure_luts();
    /* SWAR fast path: use arithmetic trick for ASCII range.
     * for each byte b in [A..Z] (0x41..0x5A), add 0x20 to get [a..z].
     * detect bytes in [A..Z]: byte - 0x41 < 26 using unsigned arithmetic. */
    size_t i = 0;
    for (; i + 8 <= len; i += 8) {
        uint64_t w = _load_u64(src + i);
        /* find uppercase bytes: subtract 'A', check < 26 using SWAR */
        uint64_t sub = w - _swar_broadcast(0x41);          /* byte - 'A' */
        uint64_t above = w - _swar_broadcast(0x5B);        /* byte - 'Z' - 1 */
        /* byte is in [A..Z] iff (byte-'A') didn't borrow AND (byte-'Z'-1) did borrow
         * a borrow is indicated by the high bit being set */
        uint64_t is_upper = (~sub & above) & 0x8080808080808080ULL;
        /* create mask: 0x20 in each byte that is uppercase */
        uint64_t mask = (is_upper >> 2) | (is_upper >> 5);
        mask &= _swar_broadcast(0x20);
        _store_u64(dst + i, w | mask);
    }
    /* scalar tail */
    for (; i < len; i++) {
        dst[i] = (char)_lut_lower[(unsigned char)src[i]];
    }
}

void str_to_upper(char* dst, const char* src, size_t len) {
    if (!dst || !src || len == 0) return;
    _ensure_luts();
    /* SWAR fast path: detect [a..z], clear bit 0x20 */
    size_t i = 0;
    for (; i + 8 <= len; i += 8) {
        uint64_t w = _load_u64(src + i);
        uint64_t sub = w - _swar_broadcast(0x61);          /* byte - 'a' */
        uint64_t above = w - _swar_broadcast(0x7B);        /* byte - 'z' - 1 */
        uint64_t is_lower = (~sub & above) & 0x8080808080808080ULL;
        uint64_t mask = (is_lower >> 2) | (is_lower >> 5);
        mask &= _swar_broadcast(0x20);
        _store_u64(dst + i, w & ~mask);
    }
    for (; i < len; i++) {
        dst[i] = (char)_lut_upper[(unsigned char)src[i]];
    }
}

void str_lower_inplace(char* s, size_t len) {
    str_to_lower(s, s, len);
}

void str_upper_inplace(char* s, size_t len) {
    str_to_upper(s, s, len);
}

bool str_is_ascii(const char* s, size_t len) {
    if (!s) return true;
    size_t i = 0;
    /* SWAR: check 8 bytes at a time for any high bit */
    for (; i + 8 <= len; i += 8) {
        if (_swar_has_highbit(_load_u64(s + i))) return false;
    }
    for (; i < len; i++) {
        if ((unsigned char)s[i] > 127) return false;
    }
    return true;
}

/* ══════════════════════════════════════════════════════════════════════
 *  7. Copy / Fill
 * ══════════════════════════════════════════════════════════════════════ */

size_t str_copy(char* dst, size_t dst_cap,
                const char* src, size_t src_len) {
    if (!dst || dst_cap == 0) return 0;
    if (!src) src_len = 0;
    size_t copy_len = src_len < dst_cap - 1 ? src_len : dst_cap - 1;
    if (copy_len > 0) memcpy(dst, src, copy_len);
    dst[copy_len] = '\0';
    return copy_len;
}

size_t str_cat(char* dst, size_t dst_len, size_t dst_cap,
               const char* src, size_t src_len) {
    if (!dst || dst_cap == 0 || dst_len >= dst_cap) return dst_len;
    if (!src) src_len = 0;
    size_t avail = dst_cap - dst_len - 1;
    size_t copy_len = src_len < avail ? src_len : avail;
    if (copy_len > 0) memcpy(dst + dst_len, src, copy_len);
    dst[dst_len + copy_len] = '\0';
    return dst_len + copy_len;
}

void str_fill(char* dst, size_t n, char c) {
    if (!dst || n == 0) return;
    memset(dst, c, n);
}

char* str_dup(const char* s, size_t len) {
    if (!s) len = 0;
    char* d = (char*)malloc(len + 1);
    if (!d) return NULL;
    if (len > 0) memcpy(d, s, len);
    d[len] = '\0';
    return d;
}

char* str_dup_lower(const char* s, size_t len) {
    if (!s) len = 0;
    char* d = (char*)malloc(len + 1);
    if (!d) return NULL;
    str_to_lower(d, s, len);
    d[len] = '\0';
    return d;
}

char* str_dup_upper(const char* s, size_t len) {
    if (!s) len = 0;
    char* d = (char*)malloc(len + 1);
    if (!d) return NULL;
    str_to_upper(d, s, len);
    d[len] = '\0';
    return d;
}

/* ══════════════════════════════════════════════════════════════════════
 *  8. Numeric parsing
 * ══════════════════════════════════════════════════════════════════════ */

bool str_to_int64(const char* s, size_t len, int64_t* out, const char** end) {
    if (!s || len == 0 || !out) return false;

    /* skip leading whitespace */
    size_t i = 0;
    while (i < len && _is_ws(s[i])) i++;
    if (i >= len) return false;

    /* sign */
    bool neg = false;
    if (s[i] == '-')      { neg = true; i++; }
    else if (s[i] == '+') { i++; }
    if (i >= len || s[i] < '0' || s[i] > '9') return false;

    /* accumulate digits with overflow check */
    uint64_t acc = 0;
    while (i < len && s[i] >= '0' && s[i] <= '9') {
        uint64_t d = (uint64_t)(s[i] - '0');
        if (acc > (UINT64_MAX - d) / 10) return false; /* overflow */
        acc = acc * 10 + d;
        i++;
    }

    if (neg) {
        if (acc > (uint64_t)INT64_MAX + 1) return false;
        *out = -(int64_t)acc;
    } else {
        if (acc > (uint64_t)INT64_MAX) return false;
        *out = (int64_t)acc;
    }
    if (end) *end = s + i;
    return true;
}

bool str_to_uint64(const char* s, size_t len, uint64_t* out, const char** end) {
    if (!s || len == 0 || !out) return false;

    size_t i = 0;
    while (i < len && _is_ws(s[i])) i++;
    if (i >= len || s[i] < '0' || s[i] > '9') return false;

    uint64_t acc = 0;
    while (i < len && s[i] >= '0' && s[i] <= '9') {
        uint64_t d = (uint64_t)(s[i] - '0');
        if (acc > (UINT64_MAX - d) / 10) return false;
        acc = acc * 10 + d;
        i++;
    }

    *out = acc;
    if (end) *end = s + i;
    return true;
}

bool str_to_double(const char* s, size_t len, double* out, const char** end) {
    if (!s || len == 0 || !out) return false;

    /* we need a NUL-terminated copy for strtod. use stack for short strings. */
    char stack_buf[64];
    char* buf = stack_buf;
    bool heap = false;
    if (len >= sizeof(stack_buf)) {
        buf = (char*)malloc(len + 1);
        if (!buf) return false;
        heap = true;
    }
    memcpy(buf, s, len);
    buf[len] = '\0';

    char* ep = NULL;
    errno = 0;
    double val = strtod(buf, &ep);
    bool ok = (ep != buf && errno != ERANGE);
    if (ok) {
        *out = val;
        if (end) *end = s + (ep - buf);
    }
    if (heap) free(buf);
    return ok;
}

int64_t str_to_int64_or(const char* s, size_t len, int64_t default_val) {
    int64_t v;
    return str_to_int64(s, len, &v, NULL) ? v : default_val;
}

double str_to_double_or(const char* s, size_t len, double default_val) {
    double v;
    return str_to_double(s, len, &v, NULL) ? v : default_val;
}

/* ══════════════════════════════════════════════════════════════════════
 *  9. Split / Tokenize
 * ══════════════════════════════════════════════════════════════════════ */

void str_split_init(StrSplitIter* it,
                    const char* s, size_t s_len,
                    const char* delim, size_t delim_len) {
    if (!it) return;
    it->src      = s ? s : "";
    it->src_len  = s ? s_len : 0;
    it->delim    = delim ? delim : "";
    it->delim_len = delim ? delim_len : 0;
    it->pos      = 0;
}

bool str_split_next(StrSplitIter* it, const char** tok, size_t* tok_len) {
    if (!it || it->pos > it->src_len) return false;

    if (it->delim_len == 0) {
        /* no delimiter → return entire remaining string as single token */
        if (it->pos == 0) {
            *tok = it->src;
            *tok_len = it->src_len;
            it->pos = it->src_len + 1;
            return true;
        }
        return false;
    }

    const char* start = it->src + it->pos;
    size_t remaining = it->src_len - it->pos;

    size_t found = str_find(start, remaining, it->delim, it->delim_len);
    if (found == STR_NPOS) {
        /* last token */
        *tok = start;
        *tok_len = remaining;
        it->pos = it->src_len + 1; /* mark exhausted */
        return true;
    }

    *tok = start;
    *tok_len = found;
    it->pos += found + it->delim_len;
    return true;
}

void str_split_byte_init(StrSplitIter* it,
                         const char* s, size_t s_len, char delim) {
    if (!it) return;
    it->src      = s ? s : "";
    it->src_len  = s ? s_len : 0;
    it->_dbuf[0] = delim;
    it->delim    = it->_dbuf;   /* point to embedded storage */
    it->delim_len = 1;
    it->pos      = 0;
}

size_t str_split_count(const char* s, size_t s_len,
                       const char* delim, size_t delim_len) {
    if (!s || s_len == 0) return 0;
    if (!delim || delim_len == 0) return 1;
    return str_count(s, s_len, delim, delim_len) + 1;
}

/* ══════════════════════════════════════════════════════════════════════
 *  10. Replace
 * ══════════════════════════════════════════════════════════════════════ */

char* str_replace_all(const char* s, size_t s_len,
                      const char* old, size_t old_len,
                      const char* new_s, size_t new_len,
                      size_t* out_len) {
    if (!s || s_len == 0 || !old || old_len == 0) {
        /* no replacements; return a copy */
        char* copy = str_dup(s, s_len);
        if (out_len) *out_len = s_len;
        return copy;
    }

    /* count occurrences first to compute exact result size */
    size_t cnt = str_count(s, s_len, old, old_len);
    if (cnt == 0) {
        char* copy = str_dup(s, s_len);
        if (out_len) *out_len = s_len;
        return copy;
    }

    size_t result_len = s_len - cnt * old_len + cnt * new_len;
    char* result = (char*)malloc(result_len + 1);
    if (!result) return NULL;

    char* dst = result;
    size_t pos = 0;
    while (pos < s_len) {
        size_t found = str_find(s + pos, s_len - pos, old, old_len);
        if (found == STR_NPOS) {
            /* copy remainder */
            memcpy(dst, s + pos, s_len - pos);
            dst += s_len - pos;
            break;
        }
        /* copy segment before match */
        if (found > 0) {
            memcpy(dst, s + pos, found);
            dst += found;
        }
        /* copy replacement */
        if (new_len > 0) {
            memcpy(dst, new_s, new_len);
            dst += new_len;
        }
        pos += found + old_len;
    }
    *dst = '\0';
    if (out_len) *out_len = result_len;
    return result;
}

char* str_replace_first(const char* s, size_t s_len,
                        const char* old, size_t old_len,
                        const char* new_s, size_t new_len,
                        size_t* out_len) {
    if (!s || s_len == 0 || !old || old_len == 0) {
        char* copy = str_dup(s, s_len);
        if (out_len) *out_len = s_len;
        return copy;
    }

    size_t found = str_find(s, s_len, old, old_len);
    if (found == STR_NPOS) {
        char* copy = str_dup(s, s_len);
        if (out_len) *out_len = s_len;
        return copy;
    }

    size_t result_len = s_len - old_len + new_len;
    char* result = (char*)malloc(result_len + 1);
    if (!result) return NULL;

    memcpy(result, s, found);
    if (new_len > 0) memcpy(result + found, new_s, new_len);
    memcpy(result + found + new_len, s + found + old_len,
           s_len - found - old_len);
    result[result_len] = '\0';
    if (out_len) *out_len = result_len;
    return result;
}

/* ══════════════════════════════════════════════════════════════════════
 *  11. File path helpers
 * ══════════════════════════════════════════════════════════════════════ */

const char* str_file_ext(const char* path, size_t path_len, size_t* ext_len) {
    if (!path || path_len == 0) { if (ext_len) *ext_len = 0; return NULL; }

    /* scan backwards for '.', but stop at '/' or '\\' */
    for (size_t i = path_len; i > 0; ) {
        i--;
        char c = path[i];
        if (c == '.') {
            if (ext_len) *ext_len = path_len - i;
            return path + i;
        }
        if (c == '/' || c == '\\') break;
    }
    if (ext_len) *ext_len = 0;
    return NULL;
}

const char* str_file_basename(const char* path, size_t path_len,
                              size_t* name_len) {
    if (!path || path_len == 0) { if (name_len) *name_len = 0; return NULL; }

    /* find last separator */
    size_t last_sep = STR_NPOS;
    for (size_t i = path_len; i > 0; ) {
        i--;
        if (path[i] == '/' || path[i] == '\\') {
            last_sep = i;
            break;
        }
    }

    if (last_sep == STR_NPOS) {
        if (name_len) *name_len = path_len;
        return path;
    }
    const char* base = path + last_sep + 1;
    if (name_len) *name_len = path_len - last_sep - 1;
    return base;
}

/* ══════════════════════════════════════════════════════════════════════
 *  12. Hashing (FNV-1a)
 * ══════════════════════════════════════════════════════════════════════ */

uint64_t str_hash(const char* s, size_t len) {
    if (!s) return 0;
    uint64_t h = 0xCBF29CE484222325ULL; /* FNV offset basis */
    for (size_t i = 0; i < len; i++) {
        h ^= (uint64_t)(unsigned char)s[i];
        h *= 0x100000001B3ULL;           /* FNV prime */
    }
    return h;
}

uint64_t str_ihash(const char* s, size_t len) {
    if (!s) return 0;
    _ensure_luts();
    uint64_t h = 0xCBF29CE484222325ULL;
    for (size_t i = 0; i < len; i++) {
        h ^= (uint64_t)_lut_lower[(unsigned char)s[i]];
        h *= 0x100000001B3ULL;
    }
    return h;
}

/* ══════════════════════════════════════════════════════════════════════
 *  13. UTF-8 utilities
 * ══════════════════════════════════════════════════════════════════════ */

size_t str_utf8_char_len(unsigned char lead) {
    if (lead < 0x80) return 1;
    if ((lead & 0xE0) == 0xC0) return 2;
    if ((lead & 0xF0) == 0xE0) return 3;
    if ((lead & 0xF8) == 0xF0) return 4;
    return 0; /* invalid lead byte */
}

size_t str_utf8_count(const char* s, size_t len) {
    if (!s) return 0;
    size_t count = 0;
    /* SWAR: count bytes that are NOT continuation bytes (0x80..0xBF).
     * a continuation byte has the top two bits = 10.
     * fast check: ((byte + 0x40) & 0x80) == 0 for continuation bytes. */
    size_t i = 0;
    for (; i + 8 <= len; i += 8) {
        uint64_t w = _load_u64(s + i);
        /* continuation bytes: 10xxxxxx → top bits are 10.
         * detect: byte & 0xC0 == 0x80, i.e. (~byte & 0x80) && (byte & 0x40) == 0
         * equivalently: ((byte + 0x40) & 0x80) == 0 for continuations.
         * count starts (non-continuations): ~continuation bits set in high pos. */
        /* Method: flip bit 6, check high bit:
         *   if original byte is 10xxxxxx → bit6=0 → flip → 11xxxxxx → high bit=1
         *   if original byte is 0xxxxxxx → bit6 varies → flip → high bit = 0 (ASCII) → count
         *   actually this doesn't work cleanly. simpler approach: */
        /* just count bytes where (byte & 0xC0) != 0x80 */
        uint64_t a = w & 0x8080808080808080ULL;          /* high bit of each byte */
        uint64_t b = (w << 1) & 0x8080808080808080ULL;   /* bit 6 shifted to high */
        /* continuation = high bit set AND bit 6 clear:  a & ~b */
        uint64_t cont = a & ~b;
        /* each continuation byte sets exactly 1 bit in cont (its 0x80 bit),
         * so popcount gives the number of continuation bytes directly. */
#if defined(__GNUC__) || defined(__clang__)
        count += 8 - (size_t)__builtin_popcountll(cont);
#else
        /* fallback: iterate each byte */
        for (int j = 0; j < 8; j++) {
            if (((unsigned char)s[i + j] & 0xC0) != 0x80) count++;
        }
#endif
    }
    for (; i < len; i++) {
        if (((unsigned char)s[i] & 0xC0) != 0x80) count++;
    }
    return count;
}

bool str_utf8_valid(const char* s, size_t len) {
    if (!s) return true;
    size_t i = 0;
    while (i < len) {
        unsigned char b = (unsigned char)s[i];
        size_t seq_len;
        uint32_t cp;

        if (b < 0x80) {
            i++; continue;
        } else if ((b & 0xE0) == 0xC0) {
            seq_len = 2; cp = b & 0x1F;
            if (cp < 2) return false;  /* overlong */
        } else if ((b & 0xF0) == 0xE0) {
            seq_len = 3; cp = b & 0x0F;
        } else if ((b & 0xF8) == 0xF0) {
            seq_len = 4; cp = b & 0x07;
        } else {
            return false; /* invalid lead byte */
        }

        if (i + seq_len > len) return false; /* truncated */

        for (size_t j = 1; j < seq_len; j++) {
            unsigned char c = (unsigned char)s[i + j];
            if ((c & 0xC0) != 0x80) return false;
            cp = (cp << 6) | (c & 0x3F);
        }

        /* check overlong encodings and range */
        if (seq_len == 2 && cp < 0x80) return false;
        if (seq_len == 3 && cp < 0x800) return false;
        if (seq_len == 4 && cp < 0x10000) return false;
        if (cp > 0x10FFFF) return false;
        /* reject surrogates */
        if (cp >= 0xD800 && cp <= 0xDFFF) return false;

        i += seq_len;
    }
    return true;
}

int str_utf8_decode(const char* s, size_t len, uint32_t* codepoint) {
    if (!s || len == 0 || !codepoint) return -1;
    unsigned char b = (unsigned char)s[0];

    if (b < 0x80) {
        *codepoint = b;
        return 1;
    }
    if ((b & 0xE0) == 0xC0) {
        if (len < 2) return -1;
        if (((unsigned char)s[1] & 0xC0) != 0x80) return -1;
        uint32_t cp = ((b & 0x1F) << 6) | ((unsigned char)s[1] & 0x3F);
        if (cp < 0x80) return -1;  /* overlong */
        *codepoint = cp;
        return 2;
    }
    if ((b & 0xF0) == 0xE0) {
        if (len < 3) return -1;
        if (((unsigned char)s[1] & 0xC0) != 0x80 ||
            ((unsigned char)s[2] & 0xC0) != 0x80) return -1;
        uint32_t cp = ((b & 0x0F) << 12) |
                      (((unsigned char)s[1] & 0x3F) << 6) |
                      ((unsigned char)s[2] & 0x3F);
        if (cp < 0x800) return -1;  /* overlong */
        if (cp >= 0xD800 && cp <= 0xDFFF) return -1;  /* surrogate */
        *codepoint = cp;
        return 3;
    }
    if ((b & 0xF8) == 0xF0) {
        if (len < 4) return -1;
        if (((unsigned char)s[1] & 0xC0) != 0x80 ||
            ((unsigned char)s[2] & 0xC0) != 0x80 ||
            ((unsigned char)s[3] & 0xC0) != 0x80) return -1;
        uint32_t cp = ((b & 0x07) << 18) |
                      (((unsigned char)s[1] & 0x3F) << 12) |
                      (((unsigned char)s[2] & 0x3F) << 6) |
                      ((unsigned char)s[3] & 0x3F);
        if (cp < 0x10000) return -1;  /* overlong */
        if (cp > 0x10FFFF) return -1; /* out of range */
        *codepoint = cp;
        return 4;
    }
    return -1;  /* invalid lead byte */
}

size_t str_utf8_encode(uint32_t codepoint, char* buf, size_t cap) {
    if (!buf) return 0;
    if (codepoint < 0x80) {
        if (cap < 1) return 0;
        buf[0] = (char)codepoint;
        return 1;
    }
    if (codepoint < 0x800) {
        if (cap < 2) return 0;
        buf[0] = (char)(0xC0 | (codepoint >> 6));
        buf[1] = (char)(0x80 | (codepoint & 0x3F));
        return 2;
    }
    if (codepoint < 0x10000) {
        if (codepoint >= 0xD800 && codepoint <= 0xDFFF) return 0; /* surrogate */
        if (cap < 3) return 0;
        buf[0] = (char)(0xE0 | (codepoint >> 12));
        buf[1] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
        buf[2] = (char)(0x80 | (codepoint & 0x3F));
        return 3;
    }
    if (codepoint <= 0x10FFFF) {
        if (cap < 4) return 0;
        buf[0] = (char)(0xF0 | (codepoint >> 18));
        buf[1] = (char)(0x80 | ((codepoint >> 12) & 0x3F));
        buf[2] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
        buf[3] = (char)(0x80 | (codepoint & 0x3F));
        return 4;
    }
    return 0;  /* invalid codepoint */
}

size_t str_utf8_char_to_byte(const char* s, size_t len, size_t char_index) {
    if (!s || len == 0) return char_index == 0 ? 0 : STR_NPOS;
    size_t ci = 0;
    size_t bi = 0;
    while (bi < len && ci < char_index) {
        unsigned char b = (unsigned char)s[bi];
        size_t seq;
        if (b < 0x80) seq = 1;
        else if ((b & 0xE0) == 0xC0) seq = 2;
        else if ((b & 0xF0) == 0xE0) seq = 3;
        else if ((b & 0xF8) == 0xF0) seq = 4;
        else seq = 1;  /* invalid lead → skip one byte */
        if (bi + seq > len) seq = 1;  /* truncated → skip one byte */
        bi += seq;
        ci++;
    }
    return (ci == char_index) ? bi : STR_NPOS;
}

size_t str_utf8_byte_to_char(const char* s, size_t len, size_t byte_offset) {
    if (!s) return 0;
    if (byte_offset > len) byte_offset = len;
    /* just count non-continuation bytes in [0, byte_offset) */
    return str_utf8_count(s, byte_offset);
}

/* ══════════════════════════════════════════════════════════════════════
 *  14. Escape / Unescape
 * ══════════════════════════════════════════════════════════════════════ */

static const char _hex_chars[] = "0123456789abcdef";

static size_t _escape_json(char* dst, const char* s, size_t len) {
    size_t w = 0;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)s[i];
        switch (c) {
            case '"':  if (dst) { dst[w]='\\'; dst[w+1]='"';  } w += 2; break;
            case '\\': if (dst) { dst[w]='\\'; dst[w+1]='\\'; } w += 2; break;
            case '\b': if (dst) { dst[w]='\\'; dst[w+1]='b';  } w += 2; break;
            case '\f': if (dst) { dst[w]='\\'; dst[w+1]='f';  } w += 2; break;
            case '\n': if (dst) { dst[w]='\\'; dst[w+1]='n';  } w += 2; break;
            case '\r': if (dst) { dst[w]='\\'; dst[w+1]='r';  } w += 2; break;
            case '\t': if (dst) { dst[w]='\\'; dst[w+1]='t';  } w += 2; break;
            default:
                if (c < 0x20) {
                    /* \u00XX */
                    if (dst) {
                        dst[w]='\\'; dst[w+1]='u'; dst[w+2]='0'; dst[w+3]='0';
                        dst[w+4] = _hex_chars[c >> 4];
                        dst[w+5] = _hex_chars[c & 0xF];
                    }
                    w += 6;
                } else {
                    if (dst) dst[w] = (char)c;
                    w++;
                }
                break;
        }
    }
    return w;
}

static size_t _escape_xml(char* dst, const char* s, size_t len) {
    size_t w = 0;
    for (size_t i = 0; i < len; i++) {
        switch (s[i]) {
            case '&':
                if (dst) memcpy(dst + w, "&amp;", 5);
                w += 5; break;
            case '<':
                if (dst) memcpy(dst + w, "&lt;", 4);
                w += 4; break;
            case '>':
                if (dst) memcpy(dst + w, "&gt;", 4);
                w += 4; break;
            case '"':
                if (dst) memcpy(dst + w, "&quot;", 6);
                w += 6; break;
            case '\'':
                if (dst) memcpy(dst + w, "&apos;", 6);
                w += 6; break;
            default:
                if (dst) dst[w] = s[i];
                w++; break;
        }
    }
    return w;
}

static inline bool _is_url_safe(unsigned char c) {
    /* unreserved chars per RFC 3986 */
    if (c >= 'A' && c <= 'Z') return true;
    if (c >= 'a' && c <= 'z') return true;
    if (c >= '0' && c <= '9') return true;
    return c == '-' || c == '_' || c == '.' || c == '~';
}

static size_t _escape_url(char* dst, const char* s, size_t len) {
    size_t w = 0;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)s[i];
        if (_is_url_safe(c)) {
            if (dst) dst[w] = (char)c;
            w++;
        } else {
            if (dst) {
                dst[w] = '%';
                dst[w + 1] = _hex_chars[c >> 4];
                dst[w + 2] = _hex_chars[c & 0xF];
            }
            w += 3;
        }
    }
    return w;
}

size_t str_escape(char* dst, const char* s, size_t s_len, StrEscapeMode mode) {
    if (!s) return 0;
    switch (mode) {
        case STR_ESC_JSON: return _escape_json(dst, s, s_len);
        case STR_ESC_XML:
        case STR_ESC_HTML: return _escape_xml(dst, s, s_len);
        case STR_ESC_URL:  return _escape_url(dst, s, s_len);
    }
    return 0;
}

size_t str_escape_len(const char* s, size_t s_len, StrEscapeMode mode) {
    return str_escape(NULL, s, s_len, mode);
}

/* ══════════════════════════════════════════════════════════════════════
 *  15. Span / Predicate helpers
 * ══════════════════════════════════════════════════════════════════════ */

bool str_is_space(char c) { return _is_ws(c); }
bool str_is_digit(char c) { return c >= '0' && c <= '9'; }
bool str_is_alpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
bool str_is_alnum(char c) { return str_is_alpha(c) || str_is_digit(c); }
bool str_is_upper(char c) { return c >= 'A' && c <= 'Z'; }
bool str_is_lower(char c) { return c >= 'a' && c <= 'z'; }
bool str_is_hex(char c) {
    return str_is_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

size_t str_span_whitespace(const char* s, size_t len) {
    if (!s) return 0;
    size_t i = 0;
    while (i < len && _is_ws(s[i])) i++;
    return i;
}

size_t str_span_digits(const char* s, size_t len) {
    if (!s) return 0;
    size_t i = 0;
    while (i < len && s[i] >= '0' && s[i] <= '9') i++;
    return i;
}

size_t str_span(const char* s, size_t len, bool (*pred)(char)) {
    if (!s || !pred) return 0;
    size_t i = 0;
    while (i < len && pred(s[i])) i++;
    return i;
}

bool str_all(const char* s, size_t len, bool (*pred)(char)) {
    if (!s || len == 0) return true;
    if (!pred) return false;
    for (size_t i = 0; i < len; i++) {
        if (!pred(s[i])) return false;
    }
    return true;
}

/* ══════════════════════════════════════════════════════════════════════
 *  16. Formatting helpers
 * ══════════════════════════════════════════════════════════════════════ */

int str_fmt(char* dst, size_t cap, const char* fmt, ...) {
    if (!dst || cap == 0) return 0;
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(dst, cap, fmt, args);
    va_end(args);
    if (n < 0) { dst[0] = '\0'; return -1; }
    if ((size_t)n >= cap) {
        /* truncated — ensure NUL and return what was written */
        dst[cap - 1] = '\0';
        return (int)(cap - 1);
    }
    return n;
}

char* str_hex_encode(char* dst, const char* s, size_t len) {
    if (!dst || !s) return dst;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)s[i];
        dst[i * 2]     = _hex_chars[c >> 4];
        dst[i * 2 + 1] = _hex_chars[c & 0xF];
    }
    dst[len * 2] = '\0';
    return dst;
}

static inline int _hex_val(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

size_t str_hex_decode(char* dst, const char* hex, size_t hex_len) {
    if (!dst || !hex) return 0;
    size_t out = 0;
    for (size_t i = 0; i + 1 < hex_len; i += 2) {
        int hi = _hex_val(hex[i]);
        int lo = _hex_val(hex[i + 1]);
        if (hi < 0 || lo < 0) break;
        dst[out++] = (char)((hi << 4) | lo);
    }
    return out;
}

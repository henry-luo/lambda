/**
 * str.c — Safe, convenient, high-performance C string library for Lambda.
 *
 * Implementation notes:
 * - Hot scans hand the work to vectorized code: libc memchr finds bytes and
 *   literal-search candidates (str_find), and block loops written without
 *   early exits or calls let the compiler vectorize ASCII checks and byte
 *   counts. Backward byte search uses SWAR (8 bytes per step) with an exact
 *   per-byte mask; byte-set scans test 8 bytes per branch; case transforms
 *   keep byte lanes independent.
 * - NULL inputs are treated as empty (length 0) — never crash.
 * - All outputs NUL-terminated where applicable.
 */

#include "str.h"
#include "hash.h"
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

/* the high bit of a byte is set iff that byte equals `c`. The classic
 * (v - 0x01..) & ~v & 0x80.. zero test can also flag the byte just above a
 * real match (when it equals c ^ 0x01) through its borrow; that is harmless
 * when only the lowest flag is read but wrong for backward scans (LR05-14).
 * Here each byte's sum stays below 0x100, so no flag reaches a neighbour. */
static inline uint64_t _swar_has_byte_exact(uint64_t word, uint8_t c) {
    uint64_t x = word ^ _swar_broadcast(c);
    return ~(((x & 0x7F7F7F7F7F7F7F7FULL) + 0x7F7F7F7F7F7F7F7FULL) | x |
             0x7F7F7F7F7F7F7F7FULL);
}

/* safe unaligned 64-bit load */
static inline uint64_t _load_u64(const void* p) {
    uint64_t v;
    memcpy(&v, p, 8);
    return v;
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

int str_icmp_cstr(const char* a, const char* b) {
    return str_icmp(a, a ? strlen(a) : 0, b, b ? strlen(b) : 0);
}

bool str_ieq_cstr(const char* a, const char* b) {
    return str_icmp_cstr(a, b) == 0;
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

bool str_eq_const(const char* s, size_t len, const char* lit) {
    if (!s) len = 0;
    if (!lit) return len == 0;
    size_t lit_len = strlen(lit);
    return str_eq(s, len, lit, lit_len);
}

bool str_ieq_const(const char* s, size_t len, const char* lit) {
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

bool str_starts_with_const(const char* s, size_t s_len, const char* prefix) {
    if (!prefix) return true;
    return str_starts_with(s, s_len, prefix, strlen(prefix));
}

bool str_ends_with_const(const char* s, size_t s_len, const char* suffix) {
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

bool str_istarts_with_const(const char* s, size_t s_len, const char* prefix) {
    if (!prefix) return true;
    return str_istarts_with(s, s_len, prefix, strlen(prefix));
}

bool str_istarts_with_cstr(const char* s, const char* prefix) {
    if (!prefix) return true;
    if (!s) return *prefix == '\0';
    _ensure_luts();
    while (*prefix) {
        if (!*s || _lut_lower[(unsigned char)*s] != _lut_lower[(unsigned char)*prefix]) {
            return false;
        }
        s++;
        prefix++;
    }
    return true;
}

bool str_iends_with_const(const char* s, size_t s_len, const char* suffix) {
    if (!suffix) return true;
    return str_iends_with(s, s_len, suffix, strlen(suffix));
}

/* ══════════════════════════════════════════════════════════════════════
 *  3. Search
 * ══════════════════════════════════════════════════════════════════════ */

size_t str_find_byte(const char* s, size_t len, char c) {
    if (!s || len == 0) return STR_NPOS;
    /* libc memchr is vectorized (NEON / SSE2 / AVX2): as fast as the 8-byte
     * SWAR scan it replaced on arm64, and wider on x86-64 */
    const char* hit = (const char*)memchr(s, (unsigned char)c, len);
    return hit ? (size_t)(hit - s) : STR_NPOS;
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
    /* SWAR scan backwards; the highest flag is read, so it must be exact */
    while (i >= 8) {
        i -= 8;
        uint64_t mask = _swar_has_byte_exact(_load_u64(s + i), (uint8_t)c);
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

    /* Candidate scan, the one literal-search kernel (T28-5): memchr jumps to
     * the next first byte among the positions a match can start at, the
     * second byte filters, and memcmp checks only the rest at a candidate.
     * The old loops called memcmp at every position. */
    const unsigned char first = (unsigned char)needle[0];
    const char second = needle[1];
    const char* p = s;
    const char* last = s + (s_len - needle_len);   /* last possible start */
    while (p <= last) {
        const char* hit = (const char*)memchr(p, first, (size_t)(last - p) + 1);
        if (!hit) return STR_NPOS;
        if (hit[1] == second && memcmp(hit + 2, needle + 2, needle_len - 2) == 0) {
            return (size_t)(hit - s);
        }
        p = hit + 1;
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

    /* backward candidate scan: the (exact) SWAR reverse byte search finds the
     * last first-byte hit among the possible starts; memcmp checks the rest */
    size_t end = s_len - needle_len + 1;   /* candidates start in [0, end) */
    while (end > 0) {
        size_t at = str_rfind_byte(s, end, needle[0]);
        if (at == STR_NPOS) return STR_NPOS;
        if (memcmp(s + at + 1, needle + 1, needle_len - 1) == 0) return at;
        end = at;
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
    const unsigned char* p = (const unsigned char*)s;
    const unsigned char b = (unsigned char)c;
    size_t count = 0;
    size_t i = 0;
    /* Count in 8-bit lanes over blocks of at most 255 bytes, then widen: the
     * compiler turns the block into one compare and one subtract per 16
     * bytes. A size_t accumulator would widen every compare result to 64
     * bits instead; this is ~2.3x the SWAR multiply trick it replaced. */
    while (i < s_len) {
        size_t n = s_len - i < 255 ? s_len - i : 255;
        uint8_t block = 0;
        for (size_t j = 0; j < n; j++) block += (uint8_t)(p[i + j] == b);
        count += block;
        i += n;
    }
    return count;
}

/* ══════════════════════════════════════════════════════════════════════
 *  4. Byte-set
 * ══════════════════════════════════════════════════════════════════════ */

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

/* Index of the first byte whose membership in `set` equals `member`, or
 * STR_NPOS. Past the first eight bytes, eight are tested per step without
 * branching and the loop branches once per block; the block holding the
 * answer is rescanned byte by byte. A per-byte early exit ran long clean runs
 * of the escapers at 0.6x; the byte-wise head keeps dense stops (every
 * escaped byte of CJK text in XML) from paying for a block test per hit.
 * Bit 0 of each shifted word is that byte's membership. */
static inline size_t _byteset_scan(const unsigned char* p, size_t len,
                                   const StrByteSet* set, bool member) {
    const uint64_t flip = member ? 0 : 1;
    size_t i = 0;
    size_t head = len < 8 ? len : 8;
    for (; i < head; i++) {
        if (str_byteset_test(set, p[i]) == member) return i;
    }
    for (; i + 8 <= len; i += 8) {
        uint64_t any = 0;
        for (int k = 0; k < 8; k++) {
            unsigned char c = p[i + k];
            any |= (set->bits[c >> 6] >> (c & 63u)) ^ flip;
        }
        if (any & 1) break;
    }
    for (; i < len; i++) {
        if (str_byteset_test(set, p[i]) == member) return i;
    }
    return STR_NPOS;
}

size_t str_find_byteset(const char* s, size_t len, const StrByteSet* set) {
    if (!s || !set) return STR_NPOS;
    return _byteset_scan((const unsigned char*)s, len, set, true);
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
    return _byteset_scan((const unsigned char*)s, len, set, false);
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

void str_rtrim_chars(const char** s, size_t* len,
                     const char* chars, size_t chars_len) {
    if (!s || !len || !*s || !chars || chars_len == 0) return;
    StrByteSet set;
    str_byteset_clear(&set);
    str_byteset_add_many(&set, chars, chars_len);

    const char* p = *s;
    size_t n = *len;
    while (n > 0 && str_byteset_test(&set, (unsigned char)p[n - 1])) { n--; }
    *len = n;
}

size_t str_collapse_ascii_whitespace(char* dst, size_t dst_cap,
                                     const char* s, size_t len,
                                     bool include_form_feed) {
    if (!dst || dst_cap == 0 || !s) return 0;

    size_t out_len = 0;
    bool previous_whitespace = true;
    for (size_t i = 0; i < len && out_len + 1 < dst_cap; i++) {
        char c = s[i];
        bool whitespace = str_char_is_ascii_space(c) &&
            (include_form_feed || c != '\f');
        if (whitespace) {
            if (!previous_whitespace) dst[out_len++] = ' ';
        } else {
            dst[out_len++] = c;
        }
        previous_whitespace = whitespace;
    }
    if (out_len > 0 && dst[out_len - 1] == ' ') out_len--;
    dst[out_len] = '\0';
    return out_len;
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
    str_rtrim_chars(&p, &n, chars, chars_len);
    *s = p;
    *len = n;
}

void str_trim_and_unquote(const char** s, size_t* len) {
    if (!s || !len) return;
    str_trim(s, len);
    if (*len < 2 || !*s) return;
    char quote = (*s)[0];
    if ((quote == '\'' || quote == '"') && (*s)[*len - 1] == quote) {
        (*s)++;
        *len -= 2;
    }
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
    // Keep range checks per byte: packed subtraction carries between lanes and
    // misclassifies an 'A'/'a' following another ASCII character.
    for (size_t i = 0; i < len; i++) {
        unsigned char ch = (unsigned char)src[i];
        dst[i] = (char)(ch + ((ch >= 'A' && ch <= 'Z') ? 0x20 : 0));
    }
}

void str_to_upper(char* dst, const char* src, size_t len) {
    if (!dst || !src || len == 0) return;
    // Keep range checks per byte: packed subtraction carries between lanes and
    // misclassifies an 'A'/'a' following another ASCII character.
    for (size_t i = 0; i < len; i++) {
        unsigned char ch = (unsigned char)src[i];
        dst[i] = (char)(ch - ((ch >= 'a' && ch <= 'z') ? 0x20 : 0));
    }
}

void str_lower_inplace(char* s, size_t len) {
    str_to_lower(s, s, len);
}

void str_upper_inplace(char* s, size_t len) {
    str_to_upper(s, s, len);
}

void str_capitalize_ascii(char* dst, const char* src, size_t len) {
    if (!dst || !src || len == 0) return;
    unsigned char first = (unsigned char)src[0];
    dst[0] = (char)(first - ((first >= 'a' && first <= 'z') ? 0x20 : 0));
    str_to_lower(dst + 1, src + 1, len - 1);
}

void str_swapcase_ascii(char* dst, const char* src, size_t len) {
    if (!dst || !src) return;
    for (size_t i = 0; i < len; i++) {
        unsigned char ch = (unsigned char)src[i];
        dst[i] = (char)(ch >= 'a' && ch <= 'z' ? ch - 0x20 :
                        (ch >= 'A' && ch <= 'Z' ? ch + 0x20 : ch));
    }
}

bool str_is_ascii(const char* s, size_t len) {
    if (!s) return true;
    const unsigned char* p = (const unsigned char*)s;
    size_t i = 0;
    /* OR each 32-byte block with no exit inside it, so the compiler vectorizes
     * the block and tests once: ~1.8x the 8-byte SWAR scan from 64 bytes up,
     * equal on short strings (every string creation calls this) */
    for (; i + 32 <= len; i += 32) {
        unsigned char acc = 0;
        for (size_t j = 0; j < 32; j++) acc |= p[i + j];
        if (acc & 0x80) return false;
    }
    unsigned char acc = 0;
    for (; i < len; i++) acc |= p[i];
    return (acc & 0x80) == 0;
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

char* str_join_parts_alloc(const char* const* parts, const size_t* lengths,
                           size_t count, StrAllocFn allocator, void* context) {
    if (!lengths || !allocator || (count > 0 && !parts)) return NULL;
    size_t total = 0;
    for (size_t i = 0; i < count; i++) {
        if ((lengths[i] > 0 && !parts[i]) || lengths[i] > SIZE_MAX - total - 1) return NULL;
        total += lengths[i];
    }
    char* result = (char*)allocator(context, total + 1);
    if (!result) return NULL;
    size_t offset = 0;
    for (size_t i = 0; i < count; i++) {
        if (lengths[i] > 0) memcpy(result + offset, parts[i], lengths[i]);
        offset += lengths[i];
    }
    result[total] = '\0';
    return result;
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
    // Accept the value if parsing consumed characters.
    // ERANGE is set for both overflow (val=±HUGE_VAL) and underflow (subnormal/zero).
    // Subnormal values are valid IEEE 754 doubles and should not be rejected.
    bool ok = (ep != buf);
    if (ok) {
        *out = val;
        if (end) *end = s + (ep - buf);
    }
    if (heap) free(buf);
    return ok;
}

size_t str_parse_float_list(const char* s, const char* separators,
                            float* values, size_t capacity, const char** end) {
    const char* p = s ? s : "";
    size_t count = 0;
    while (values && count < capacity) {
        p = str_skip_chars(p, separators);
        char* parsed_end = NULL;
        float value = strtof(p, &parsed_end);
        if (parsed_end == p) break;
        values[count++] = value;
        p = parsed_end;
    }
    if (end) *end = p;
    return count;
}

int64_t str_to_int64_default(const char* s, size_t len, int64_t default_val) {
    int64_t v;
    return str_to_int64(s, len, &v, NULL) ? v : default_val;
}

double str_to_double_default(const char* s, size_t len, double default_val) {
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
    if (ext_len) *ext_len = 0;
    if (!path || path_len == 0 || path[path_len - 1] == '/' || path[path_len - 1] == '\\') {
        return NULL;
    }

    size_t base_start = 0;
    for (size_t i = path_len; i > 0; i--) {
        if (path[i - 1] == '/' || path[i - 1] == '\\') {
            base_start = i;
            break;
        }
    }

    for (size_t i = path_len; i > base_start; i--) {
        if (path[i - 1] != '.') continue;
        if (i - 1 == base_start || i == path_len) return NULL;
        if (ext_len) *ext_len = path_len - i + 1;
        return path + i - 1;
    }
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
    return hash_fnv1a_64(s, len);
}

uint64_t str_ihash(const char* s, size_t len) {
    if (!s) return 0;
    _ensure_luts();
    uint64_t h = HASH_FNV1A_64_OFFSET_BASIS;
    for (size_t i = 0; i < len; i++) {
        h = hash_fnv1a_64_extend_byte(h, _lut_lower[(unsigned char)s[i]]);
    }
    return h;
}

/* ══════════════════════════════════════════════════════════════════════
 *  13. UTF-8 utilities — thin wrappers around lib/utf.h
 * ══════════════════════════════════════════════════════════════════════ */

#include "utf.h"

size_t str_utf8_char_len(unsigned char lead) { return utf8_char_len(lead); }
size_t str_utf8_count(const char* s, size_t len) { return utf8_count(s, len); }
bool   str_utf8_valid(const char* s, size_t len) { return utf8_valid(s, len); }
int    str_utf8_decode(const char* s, size_t len, uint32_t* cp) { return utf8_decode(s, len, cp); }

size_t str_utf8_encode(uint32_t codepoint, char* buf, size_t cap) {
    /* str_utf8_encode has a cap parameter; utf8_encode always writes up to 4 */
    if (!buf) return 0;
    char tmp[4];
    size_t n = utf8_encode(codepoint, tmp);
    if (n == 0 || n > cap) return 0;
    memcpy(buf, tmp, n);
    return n;
}

size_t str_utf8_char_to_byte(const char* s, size_t len, size_t char_index) {
    size_t r = utf8_char_to_byte(s, len, char_index);
    return r == (size_t)-1 ? STR_NPOS : r;
}

size_t str_utf8_byte_to_char(const char* s, size_t len, size_t byte_offset) {
    return utf8_byte_to_char(s, len, byte_offset);
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

static size_t _escape_lambda(char* dst, const char* s, size_t len) {
    size_t w = 0;
    for (size_t i = 0; i < len; i++) {
        char c = s[i];
        switch (c) {
            case '\\': case '"':
                if (dst) { dst[w] = '\\'; dst[w + 1] = c; }
                w += 2;
                break;
            case '\n': case '\r': case '\t':
                if (dst) { dst[w] = '\\'; dst[w + 1] = c == '\n' ? 'n' : c == '\r' ? 'r' : 't'; }
                w += 2;
                break;
            default:
                if (dst) dst[w] = c;
                w++;
                break;
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
        case STR_ESC_LAMBDA: return _escape_lambda(dst, s, s_len);
    }
    return 0;
}

size_t str_escape_len(const char* s, size_t s_len, StrEscapeMode mode) {
    return str_escape(NULL, s, s_len, mode);
}

char* str_escape_alloc(const char* s, size_t s_len, StrEscapeMode mode,
                       StrAllocFn allocator, void* context) {
    if (!s || !allocator) return NULL;
    size_t output_len = str_escape_len(s, s_len, mode);
    if (output_len == SIZE_MAX) return NULL;
    char* dst = (char*)allocator(context, output_len + 1);
    if (!dst) return NULL;
    str_escape(dst, s, s_len, mode);
    dst[output_len] = '\0';
    return dst;
}

size_t str_shell_quote_posix(char* dst, size_t cap, const char* s, size_t s_len) {
    if (!s) s_len = 0;
    size_t required = 2;
    for (size_t i = 0; i < s_len; i++) {
        if (s[i] == '\'') required += 4;
        else required++;
    }

    size_t written = 0;
#define STR_SHELL_QUOTE_APPEND(ch) do { \
    if (dst && written + 1 < cap) dst[written] = (ch); \
    written++; \
} while (0)
    STR_SHELL_QUOTE_APPEND('\'');
    for (size_t i = 0; i < s_len; i++) {
        if (s[i] == '\'') {
            STR_SHELL_QUOTE_APPEND('\'');
            STR_SHELL_QUOTE_APPEND('\\');
            STR_SHELL_QUOTE_APPEND('\'');
        }
        STR_SHELL_QUOTE_APPEND(s[i]);
    }
    STR_SHELL_QUOTE_APPEND('\'');
#undef STR_SHELL_QUOTE_APPEND
    if (dst && cap > 0) dst[written < cap ? written : cap - 1] = '\0';
    return required;
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
    return str_hex_val(c) >= 0;
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

size_t str_uint64_decimal_len(uint64_t value) {
    size_t length = 1;
    while (value >= 10) {
        value /= 10;
        length++;
    }
    return length;
}

size_t str_uint64_decimal_write(char* dst, uint64_t value) {
    size_t length = str_uint64_decimal_len(value);
    if (!dst) return length;

    char* cursor = dst + length;
    do {
        *--cursor = (char)('0' + value % 10);
        value /= 10;
    } while (value > 0);
    return length;
}

int str_decimal_significant_digits(const char* value) {
    bool seen_nonzero = false;
    bool saw_digit = false;
    int digits = 0;
    for (const char* p = value; p && *p; p++) {
        char c = *p;
        if (c == 'e' || c == 'E') break;
        if (!str_char_is_digit(c)) continue;
        saw_digit = true;
        if (c != '0') seen_nonzero = true;
        if (seen_nonzero) digits++;
    }
    return saw_digit ? (digits > 0 ? digits : 1) : 0;
}

static const char _hex_chars_upper[] = "0123456789ABCDEF";

// one encoder loop for both digit spellings
static char* str_hex_encode_digits(char* dst, const char* s, size_t len,
                                   const char* digits) {
    if (!dst || !s) return dst;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)s[i];
        dst[i * 2]     = digits[c >> 4];
        dst[i * 2 + 1] = digits[c & 0xF];
    }
    dst[len * 2] = '\0';
    return dst;
}

char* str_hex_encode(char* dst, const char* s, size_t len) {
    return str_hex_encode_digits(dst, s, len, _hex_chars);
}

char* str_hex_encode_upper(char* dst, const char* s, size_t len) {
    return str_hex_encode_digits(dst, s, len, _hex_chars_upper);
}

size_t str_hex_decode(char* dst, const char* hex, size_t hex_len) {
    if (!dst || !hex) return 0;
    size_t out = 0;
    for (size_t i = 0; i + 1 < hex_len; i += 2) {
        int hi = str_hex_val(hex[i]);
        int lo = str_hex_val(hex[i + 1]);
        if (hi < 0 || lo < 0) break;
        dst[out++] = (char)((hi << 4) | lo);
    }
    return out;
}

/* ──────────────────────────────────────────────────────────────────────
 *  17. Scanner tier (NUL-safe parser primitives)
 * ────────────────────────────────────────────────────────────────────── */

/* 17.1 — NUL-safe character classes */

bool str_char_in_set(char c, const char* chars) {
    /* NUL is never a member — this is the fix for the strchr(set, '\0') class */
    if (c == '\0' || !chars) return false;
    for (; *chars; chars++) {
        if (*chars == c) return true;
    }
    return false;
}

bool str_char_is_ascii_space(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

bool str_is_html_space(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}

bool str_char_is_line_space(char c) {
    return c == ' ' || c == '\t';
}

bool str_char_is_digit(char c) {
    return c >= '0' && c <= '9';
}

bool str_char_is_alpha(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

bool str_char_is_alnum(char c) {
    return str_char_is_digit(c) || str_char_is_alpha(c);
}

bool str_char_is_ident(char c) {
    return str_char_is_alnum(c) || c == '_';
}

/* 17.2 — Bounded scanners (the default tier) */

/* The skip/stop set as a bitmap, built once per scan, so each input byte
 * costs one lookup instead of a loop over the set string (CSV fields and
 * the kv/vcf/ics/eml/yaml line skips scan through these). NUL is never a
 * member, as in str_char_in_set. */
static inline void _byteset_from_chars(StrByteSet* set, const char* chars) {
    str_byteset_clear(set);
    if (!chars) return;
    for (; *chars; chars++) str_byteset_add(set, (unsigned char)*chars);
}

const char* strn_skip_chars(const char* p, const char* end, const char* chars) {
    if (!p || p >= end) return p;
    StrByteSet set;
    _byteset_from_chars(&set, chars);
    size_t at = _byteset_scan((const unsigned char*)p, (size_t)(end - p), &set, false);
    return at == STR_NPOS ? end : p + at;
}

const char* strn_skip_line_space(const char* p, const char* end) {
    if (!p) return p;
    while (p < end && str_char_is_line_space(*p)) p++;
    return p;
}

const char* strn_skip_ascii_space(const char* p, const char* end) {
    if (!p) return p;
    while (p < end && str_char_is_ascii_space(*p)) p++;
    return p;
}

const char* strn_skip_digits(const char* p, const char* end) {
    if (!p) return p;
    while (p < end && str_char_is_digit(*p)) p++;
    return p;
}

const char* strn_scan_until_char(const char* p, const char* end, char stop) {
    if (!p) return p;
    while (p < end && *p != stop) p++;
    return p;
}

const char* strn_scan_until_any(const char* p, const char* end, const char* stops) {
    if (!p || p >= end) return p;
    if (stops && stops[0] && !stops[1]) {   /* one stop byte: memchr */
        const char* hit = (const char*)memchr(p, (unsigned char)stops[0], (size_t)(end - p));
        return hit ? hit : end;
    }
    StrByteSet set;
    _byteset_from_chars(&set, stops);
    size_t at = _byteset_scan((const unsigned char*)p, (size_t)(end - p), &set, true);
    return at == STR_NPOS ? end : p + at;
}

const char* strn_scan_to_line_end(const char* p, const char* end) {
    if (!p) return p;
    while (p < end && *p != '\n' && *p != '\r') p++;
    return p;
}

const char* strn_scan_quoted(const char* p, const char* end, char quote,
                             bool skip_escaped, bool* closed) {
    if (closed) *closed = false;
    if (!p || p >= end || *p != quote) return p;
    p++;
    while (p < end) {
        if (skip_escaped && *p == '\\' && p + 1 < end) {
            p += 2;
        } else if (*p == quote) {
            if (closed) *closed = true;
            return p + 1;
        } else {
            p++;
        }
    }
    return p;
}

const char* strn_scan_balanced(const char* p, const char* end, char open, char close,
                               bool skip_escaped, bool* closed) {
    if (closed) *closed = false;
    if (!p || !end || p >= end || *p != open) return p;
    size_t depth = 0;
    while (p < end) {
        char ch = *p++;
        if (skip_escaped && ch == '\\' && p < end) {
            p++;
        } else if (ch == open) {
            depth++;
        } else if (ch == close && --depth == 0) {
            if (closed) *closed = true;
            return p;
        }
    }
    return p;
}

const char* strn_scan_balanced_quoted(const char* p, const char* end, char open, char close,
                                      const char* quotes, bool skip_escaped, bool* closed) {
    if (closed) *closed = false;
    if (!p || !end || p >= end || *p != open) return p;
    size_t depth = 0;
    char quote = '\0';
    while (p < end) {
        char c = *p++;
        if (skip_escaped && c == '\\' && p < end) {
            p++;
            continue;
        }
        if (quote) {
            if (c == quote) quote = '\0';
            continue;
        }
        if (quotes && str_char_in_set(c, quotes)) {
            quote = c;
        } else if (c == open) {
            depth++;
        } else if (c == close && --depth == 0) {
            if (closed) *closed = true;
            return p;
        }
    }
    return p;
}

const char* strn_scan_top_level(const char* p, const char* end, const char* stops,
                                char open, char close, const char* quotes,
                                bool skip_escaped) {
    if (!p || !end || p > end) return p;
    int depth = 0;
    char quote = '\0';
    while (p < end) {
        char c = *p;
        if (skip_escaped && c == '\\' && p + 1 < end) {
            p += 2;
            continue;
        }
        if (quote) {
            if (c == quote) quote = '\0';
            p++;
            continue;
        }
        if (quotes && str_char_in_set(c, quotes)) {
            quote = c;
            p++;
            continue;
        }
        if (c == open) {
            depth++;
            p++;
            continue;
        }
        if (c == close && depth > 0) {
            depth--;
            p++;
            continue;
        }
        if (depth == 0 && str_char_in_set(c, stops)) break;
        p++;
    }
    return p;
}

size_t strn_count_run(const char* p, const char* end, char marker) {
    if (!p || marker == '\0') return 0;
    size_t n = 0;
    while (p + n < end && p[n] == marker) n++;
    return n;
}

/* 17.3 — NUL-terminated scanners (convenience exception) */

const char* str_skip_chars(const char* p, const char* chars) {
    if (!p) return p;
    StrByteSet set;
    _byteset_from_chars(&set, chars);   /* NUL stays out, so the scan stops there */
    while (str_byteset_test(&set, (unsigned char)*p)) p++;
    return p;
}

const char* str_skip_line_space(const char* p) {
    if (!p) return p;
    while (str_char_is_line_space(*p)) p++;
    return p;
}

const char* str_skip_ascii_space(const char* p) {
    if (!p) return p;
    while (str_char_is_ascii_space(*p)) p++;
    return p;
}

const char* str_skip_digits(const char* p) {
    if (!p) return p;
    while (str_char_is_digit(*p)) p++;
    return p;
}

const char* str_scan_until_char(const char* p, char stop) {
    if (!p) return p;
    while (*p && *p != stop) p++;
    return p;
}

const char* str_scan_until_any(const char* p, const char* stops) {
    if (!p) return p;
    StrByteSet set;
    _byteset_from_chars(&set, stops);
    str_byteset_add(&set, 0);           /* the terminator ends the scan too */
    while (!str_byteset_test(&set, (unsigned char)*p)) p++;
    return p;
}

const char* str_scan_to_line_end(const char* p) {
    if (!p) return p;
    while (*p && *p != '\n' && *p != '\r') p++;
    return p;
}

const char* str_scan_quoted(const char* p, char quote, bool skip_escaped, bool* closed) {
    if (!p) return NULL;
    return strn_scan_quoted(p, p + strlen(p), quote, skip_escaped, closed);
}

const char* str_scan_balanced(const char* p, char open, char close,
                              bool skip_escaped, bool* closed) {
    if (!p) return NULL;
    return strn_scan_balanced(p, p + strlen(p), open, close, skip_escaped, closed);
}

const char* str_scan_balanced_quoted(const char* p, char open, char close,
                                     const char* quotes, bool skip_escaped, bool* closed) {
    if (!p) return NULL;
    return strn_scan_balanced_quoted(p, p + strlen(p), open, close, quotes,
                                     skip_escaped, closed);
}

const char* str_scan_top_level(const char* p, const char* stops,
                               char open, char close, const char* quotes,
                               bool skip_escaped) {
    if (!p) return NULL;
    return strn_scan_top_level(p, p + strlen(p), stops, open, close, quotes,
                               skip_escaped);
}

size_t str_count_run(const char* p, size_t max_len, char marker) {
    if (!p || marker == '\0') return 0;
    size_t n = 0;
    if (max_len == 0) {
        while (p[n] == marker) n++;       /* NUL-terminated mode; '\0' != marker stops */
    } else {
        while (n < max_len && p[n] == marker) n++;
    }
    return n;
}

/* 17.4 — Cursor */

bool str_cursor_at_end(const StrCursor* c) {
    return !c || c->p >= c->end;
}

char str_cursor_peek(const StrCursor* c) {
    return str_cursor_at_end(c) ? '\0' : *c->p;
}

void str_cursor_skip_line_space(StrCursor* c) {
    if (!c) return;
    c->p = strn_skip_line_space(c->p, c->end);
}

void str_cursor_skip_ascii_space(StrCursor* c) {
    if (!c) return;
    c->p = strn_skip_ascii_space(c->p, c->end);
}

size_t str_cursor_count_run(StrCursor* c, char marker) {
    if (!c) return 0;
    size_t n = strn_count_run(c->p, c->end, marker);
    c->p += n;
    return n;
}

const char* str_cursor_mark(const StrCursor* c) {
    return c ? c->p : NULL;
}

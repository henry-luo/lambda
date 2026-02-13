/**
 * str.h — Safe, convenient, high-performance C string library for Lambda.
 *
 * Design principles:
 *   1. Safety    — length-bounded everywhere; NULL-tolerant; no buffer overruns.
 *   2. Convenience — common patterns (prefix, suffix, trim, split, case, escape)
 *                    as one-call functions to eliminate code duplication.
 *   3. Performance — SWAR byte-parallel ops on 64-bit words; LUT-based transforms;
 *                    minimal branching; zero-copy where possible.
 *   4. Compatibility — works with StrView, StrBuf, and plain (ptr, len) pairs;
 *                      pure C99; extern "C" safe for C++ callers.
 *
 * All functions use explicit (const char* s, size_t len) pairs.
 * NULL pointers are treated as empty strings (length 0) — never crash.
 *
 * Naming: str_ prefix, snake_case, mirrors the lib/ convention.
 * Return: size_t for positions (STR_NPOS on not-found), bool for predicates,
 *         int for ordering (<0, 0, >0). Mutating functions return destination.
 */

#ifndef LIB_STR_H
#define LIB_STR_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* sentinel for "not found" — same convention as C++ std::string::npos */
#define STR_NPOS ((size_t)-1)

/* ──────────────────────────────────────────────────────────────────────
 *  1. Comparison
 * ────────────────────────────────────────────────────────────────────── */

/** lexicographic compare; returns <0, 0, or >0 (like memcmp/strcmp). */
int str_cmp(const char* a, size_t a_len, const char* b, size_t b_len);

/** case-insensitive compare (ASCII). */
int str_icmp(const char* a, size_t a_len, const char* b, size_t b_len);

/** exact equality. */
bool str_eq(const char* a, size_t a_len, const char* b, size_t b_len);

/** case-insensitive equality (ASCII). */
bool str_ieq(const char* a, size_t a_len, const char* b, size_t b_len);

/** compare with a NUL-terminated literal (convenience for the very common
 *  `strcmp(tag, "div") == 0` pattern). */
bool str_eq_lit(const char* s, size_t len, const char* lit);

/** case-insensitive compare with a NUL-terminated literal. */
bool str_ieq_lit(const char* s, size_t len, const char* lit);

/* ──────────────────────────────────────────────────────────────────────
 *  2. Prefix / Suffix
 * ────────────────────────────────────────────────────────────────────── */

bool str_starts_with(const char* s, size_t s_len,
                     const char* prefix, size_t prefix_len);
bool str_ends_with(const char* s, size_t s_len,
                   const char* suffix, size_t suffix_len);

/** convenience overloads for NUL-terminated prefix/suffix. */
bool str_starts_with_lit(const char* s, size_t s_len, const char* prefix);
bool str_ends_with_lit(const char* s, size_t s_len, const char* suffix);

/** case-insensitive prefix/suffix (ASCII). */
bool str_istarts_with(const char* s, size_t s_len,
                      const char* prefix, size_t prefix_len);
bool str_iends_with(const char* s, size_t s_len,
                    const char* suffix, size_t suffix_len);

/* ──────────────────────────────────────────────────────────────────────
 *  3. Search
 * ────────────────────────────────────────────────────────────────────── */

/** find first byte `c` in [s, s+len). returns offset or STR_NPOS. */
size_t str_find_byte(const char* s, size_t len, char c);

/** find last byte `c` in [s, s+len). returns offset or STR_NPOS. */
size_t str_rfind_byte(const char* s, size_t len, char c);

/** find first occurrence of needle in haystack. returns offset or STR_NPOS. */
size_t str_find(const char* s, size_t s_len,
                const char* needle, size_t needle_len);

/** find last occurrence. */
size_t str_rfind(const char* s, size_t s_len,
                 const char* needle, size_t needle_len);

/** case-insensitive find (ASCII). */
size_t str_ifind(const char* s, size_t s_len,
                 const char* needle, size_t needle_len);

/** does s contain needle? */
bool str_contains(const char* s, size_t s_len,
                  const char* needle, size_t needle_len);
bool str_contains_byte(const char* s, size_t s_len, char c);

/** find first byte that belongs to the given byte set. */
size_t str_find_any(const char* s, size_t s_len,
                    const char* chars, size_t chars_len);

/** find first byte that does NOT belong to the set. */
size_t str_find_not_any(const char* s, size_t s_len,
                        const char* chars, size_t chars_len);

/** count non-overlapping occurrences of needle in s. */
size_t str_count(const char* s, size_t s_len,
                 const char* needle, size_t needle_len);

/** count occurrences of byte c. */
size_t str_count_byte(const char* s, size_t s_len, char c);

/* ──────────────────────────────────────────────────────────────────────
 *  4. Byte-set (256-bit bitmap for fast character-class matching)
 * ────────────────────────────────────────────────────────────────────── */

typedef struct {
    uint64_t bits[4];     /* 256 bits — one per possible byte value */
} StrByteSet;

void str_byteset_clear(StrByteSet* set);
void str_byteset_add(StrByteSet* set, unsigned char c);
void str_byteset_add_range(StrByteSet* set, unsigned char lo, unsigned char hi);
void str_byteset_add_many(StrByteSet* set, const char* chars, size_t len);
void str_byteset_invert(StrByteSet* set);
bool str_byteset_test(const StrByteSet* set, unsigned char c);

/** pre-built byte sets (call once, reuse). */
void str_byteset_whitespace(StrByteSet* set);   /* SP, TAB, CR, LF, FF, VT */
void str_byteset_digits(StrByteSet* set);        /* '0'..'9' */
void str_byteset_alpha(StrByteSet* set);         /* a-z, A-Z */
void str_byteset_alnum(StrByteSet* set);         /* a-z, A-Z, 0-9 */

/** find first/last byte matching the set. */
size_t str_find_byteset(const char* s, size_t len, const StrByteSet* set);
size_t str_rfind_byteset(const char* s, size_t len, const StrByteSet* set);
size_t str_find_not_byteset(const char* s, size_t len, const StrByteSet* set);

/* ──────────────────────────────────────────────────────────────────────
 *  5. Trim
 * ────────────────────────────────────────────────────────────────────── */

/** trim ASCII whitespace from both ends. mutates *s and *len in place.
 *  the underlying buffer is not modified — just pointer/length adjustment. */
void str_trim(const char** s, size_t* len);
void str_ltrim(const char** s, size_t* len);
void str_rtrim(const char** s, size_t* len);

/** trim specific characters from both ends. */
void str_trim_chars(const char** s, size_t* len,
                    const char* chars, size_t chars_len);

/* ──────────────────────────────────────────────────────────────────────
 *  6. Case conversion (ASCII-only, in-place or into dst)
 * ────────────────────────────────────────────────────────────────────── */

/** LUT-based transform: apply lut[byte] to every byte in [s, s+len).
 *  operates in-place (dst == src is fine). SWAR-accelerated. */
void str_transform(char* dst, const char* src, size_t len,
                   const uint8_t lut[256]);

/** pre-built LUT initializers (fill a caller-provided uint8_t[256]). */
void str_lut_tolower(uint8_t lut[256]);
void str_lut_toupper(uint8_t lut[256]);
void str_lut_identity(uint8_t lut[256]);   /* identity mapping, base for custom LUTs */

/** convenience wrappers — write into dst (must be >= len bytes). */
void str_to_lower(char* dst, const char* src, size_t len);
void str_to_upper(char* dst, const char* src, size_t len);

/** in-place lower/upper on a mutable buffer. */
void str_lower_inplace(char* s, size_t len);
void str_upper_inplace(char* s, size_t len);

/** predicate: is the whole string ASCII? (SWAR-accelerated). */
bool str_is_ascii(const char* s, size_t len);

/* ──────────────────────────────────────────────────────────────────────
 *  7. Copy / Fill (safe replacements for strcpy, strncpy, memset)
 * ────────────────────────────────────────────────────────────────────── */

/** safe copy — copies up to dst_cap-1 bytes, always NUL-terminates.
 *  returns number of bytes written (excluding NUL), or 0 if dst_cap==0. */
size_t str_copy(char* dst, size_t dst_cap,
                const char* src, size_t src_len);

/** safe concatenate — appends to dst[dst_len], NUL-terminates.
 *  returns new total length, or dst_len if no room. */
size_t str_cat(char* dst, size_t dst_len, size_t dst_cap,
               const char* src, size_t src_len);

/** fill dst with `n` copies of byte `c`. */
void str_fill(char* dst, size_t n, char c);

/** duplicate [s, s+len) as a NUL-terminated malloc'd string.
 *  caller must free(). returns NULL on allocation failure. */
char* str_dup(const char* s, size_t len);

/** duplicate with lower-case conversion. */
char* str_dup_lower(const char* s, size_t len);

/** duplicate with upper-case conversion. */
char* str_dup_upper(const char* s, size_t len);

/* ──────────────────────────────────────────────────────────────────────
 *  8. Numeric parsing (safe wrappers with error reporting)
 * ────────────────────────────────────────────────────────────────────── */

/** parse decimal integer from [s, s+len). returns true on success.
 *  stops at first non-digit after optional leading sign & whitespace.
 *  *end (if non-NULL) receives pointer past last consumed byte. */
bool str_to_int64(const char* s, size_t len, int64_t* out, const char** end);
bool str_to_uint64(const char* s, size_t len, uint64_t* out, const char** end);
bool str_to_double(const char* s, size_t len, double* out, const char** end);

/** convenience: parse or return default_val. */
int64_t  str_to_int64_or(const char* s, size_t len, int64_t default_val);
double   str_to_double_or(const char* s, size_t len, double default_val);

/* ──────────────────────────────────────────────────────────────────────
 *  9. Split / Tokenize (zero-allocation iterator)
 * ────────────────────────────────────────────────────────────────────── */

typedef struct {
    const char* src;      /* original string */
    size_t      src_len;  /* original length */
    const char* delim;    /* delimiter string */
    size_t      delim_len;
    size_t      pos;      /* current scan position */
    char        _dbuf[4]; /* internal storage for single-byte delimiter */
} StrSplitIter;

/** initialize a split iterator. */
void str_split_init(StrSplitIter* it,
                    const char* s, size_t s_len,
                    const char* delim, size_t delim_len);

/** advance to next token. returns true if a token was found.
 *  *tok and *tok_len receive the current token (zero-copy). */
bool str_split_next(StrSplitIter* it, const char** tok, size_t* tok_len);

/** split on single byte (common case: ',', '/', '.', etc.). */
void str_split_byte_init(StrSplitIter* it,
                         const char* s, size_t s_len, char delim);

/** count how many tokens a split would produce. */
size_t str_split_count(const char* s, size_t s_len,
                       const char* delim, size_t delim_len);

/* ──────────────────────────────────────────────────────────────────────
 *  10. Replace
 * ────────────────────────────────────────────────────────────────────── */

/** replace all occurrences of `old` with `new_s` in [s, s_len).
 *  allocates result via malloc. caller must free().
 *  *out_len (if non-NULL) receives the result length.
 *  returns NULL on allocation failure. */
char* str_replace_all(const char* s, size_t s_len,
                      const char* old, size_t old_len,
                      const char* new_s, size_t new_len,
                      size_t* out_len);

/** replace first occurrence only. */
char* str_replace_first(const char* s, size_t s_len,
                        const char* old, size_t old_len,
                        const char* new_s, size_t new_len,
                        size_t* out_len);

/* ──────────────────────────────────────────────────────────────────────
 *  11. File path helpers (common in Lambda for format detection)
 * ────────────────────────────────────────────────────────────────────── */

/** return pointer to file extension including '.', or NULL.
 *  e.g. str_file_ext("doc.json", 8, &ext_len) → ".json", ext_len=5. */
const char* str_file_ext(const char* path, size_t path_len, size_t* ext_len);

/** return pointer to the base name (after last '/' or '\').
 *  e.g. str_file_basename("/a/b/c.txt", 10, &len) → "c.txt", len=5. */
const char* str_file_basename(const char* path, size_t path_len, size_t* name_len);

/* ──────────────────────────────────────────────────────────────────────
 *  12. Hashing
 * ────────────────────────────────────────────────────────────────────── */

/** fast non-cryptographic hash (FNV-1a based, good for hash maps). */
uint64_t str_hash(const char* s, size_t len);

/** case-insensitive hash (ASCII). */
uint64_t str_ihash(const char* s, size_t len);

/* ──────────────────────────────────────────────────────────────────────
 *  13. UTF-8 utilities
 * ────────────────────────────────────────────────────────────────────── */

/** count UTF-8 codepoints in [s, s+len). invalid sequences count as 1.
 *  SWAR-accelerated (~4-6× faster than scalar loop). */
size_t str_utf8_count(const char* s, size_t len);

/** return byte length of the UTF-8 sequence starting with lead byte.
 *  returns 0 for invalid lead byte (continuation or 0xFE/0xFF). */
size_t str_utf8_char_len(unsigned char lead);

/** validate UTF-8 encoding. returns true if [s, s+len) is valid UTF-8.
 *  checks overlong encodings, surrogate range, and max codepoint. */
bool str_utf8_valid(const char* s, size_t len);

/** decode one UTF-8 codepoint from [s, s+len).
 *  stores the codepoint in *codepoint and returns the number of bytes
 *  consumed (1-4), or -1 on invalid/truncated sequence.
 *  NULL-safe: returns -1 for NULL s or len==0. */
int str_utf8_decode(const char* s, size_t len, uint32_t* codepoint);

/** encode a Unicode codepoint to UTF-8 into buf[0..cap).
 *  returns the number of bytes written (1-4), or 0 if cap is too small
 *  or codepoint is invalid (> 0x10FFFF or surrogate). */
size_t str_utf8_encode(uint32_t codepoint, char* buf, size_t cap);

/** convert a character (codepoint) index to a byte offset in [s, s+len).
 *  returns the byte offset of the char_index-th codepoint, or STR_NPOS
 *  if char_index exceeds the number of codepoints. */
size_t str_utf8_char_to_byte(const char* s, size_t len, size_t char_index);

/** convert a byte offset to a character (codepoint) index.
 *  returns the number of codepoints in [s, s+byte_offset).
 *  byte_offset is clamped to len. */
size_t str_utf8_byte_to_char(const char* s, size_t len, size_t byte_offset);

/* ──────────────────────────────────────────────────────────────────────
 *  14. Escape / Unescape (consolidation of JSON, XML, HTML patterns)
 * ────────────────────────────────────────────────────────────────────── */

typedef enum {
    STR_ESC_JSON,     /* \n, \t, \", \\, \uXXXX */
    STR_ESC_XML,      /* &amp; &lt; &gt; &quot; &apos; */
    STR_ESC_HTML,     /* same as XML plus numeric entities */
    STR_ESC_URL,      /* percent-encoding for non-URL chars */
} StrEscapeMode;

/** escape [s, s_len) into dst (must be pre-allocated with enough space).
 *  returns bytes written. call str_escape_len() first for sizing.
 *  dst may be NULL to just compute length (same as str_escape_len). */
size_t str_escape(char* dst, const char* s, size_t s_len, StrEscapeMode mode);

/** compute required buffer size for escaping (excluding NUL). */
size_t str_escape_len(const char* s, size_t s_len, StrEscapeMode mode);

/* ──────────────────────────────────────────────────────────────────────
 *  15. Span / Predicate helpers
 * ────────────────────────────────────────────────────────────────────── */

/** length of leading bytes that are all ASCII whitespace. */
size_t str_span_whitespace(const char* s, size_t len);

/** length of leading bytes that are all digits. */
size_t str_span_digits(const char* s, size_t len);

/** length of leading bytes that satisfy predicate. */
size_t str_span(const char* s, size_t len, bool (*pred)(char));

/** check if all bytes satisfy predicate. */
bool str_all(const char* s, size_t len, bool (*pred)(char));

/** common char predicates. */
bool str_is_space(char c);
bool str_is_digit(char c);
bool str_is_alpha(char c);
bool str_is_alnum(char c);
bool str_is_upper(char c);
bool str_is_lower(char c);
bool str_is_hex(char c);

/* ──────────────────────────────────────────────────────────────────────
 *  16. Formatting helpers
 * ────────────────────────────────────────────────────────────────────── */

/** safe snprintf — always NUL-terminates, returns chars written
 *  (excluding NUL), or -1 on encoding error. never returns a value >= cap. */
#if defined(__GNUC__) || defined(__clang__)
int str_fmt(char* dst, size_t cap, const char* fmt, ...)
    __attribute__((format(printf, 3, 4)));
#else
int str_fmt(char* dst, size_t cap, const char* fmt, ...);
#endif

/** hex encode [s, s+len) into dst. dst must have 2*len+1 bytes.
 *  returns dst. */
char* str_hex_encode(char* dst, const char* s, size_t len);

/** hex decode [hex, hex+hex_len) into dst. returns bytes written. */
size_t str_hex_decode(char* dst, const char* hex, size_t hex_len);

#ifdef __cplusplus
}
#endif

#endif /* LIB_STR_H */

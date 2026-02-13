# Proposal: `lib/str.h` — Standard C String Library for Lambda

## 1. Motivation

Lambda is a document-oriented language. String operations account for the majority of
runtime activity — parsing 15+ input formats, formatting output, building ASTs,
matching tag names, resolving CSS selectors, escaping for JSON/XML/HTML/YAML, and
extracting file extensions. Today these operations rely on:

| Layer | Usage count | Problems |
|-------|------------|----------|
| C stdlib (`strcmp`, `strlen`, `strstr`, …) | ~4,000+ call sites | NUL-terminated assumption; no bounds checking; no prefix/suffix/trim/split helpers; unsafe `strcpy`/`strcat`/`sprintf` |
| `lib/strbuf.h` (heap-growable buffer) | ~1,850 sites | Good for building strings; no search/compare/transform API |
| `lib/strview.h` (non-owning slice) | ~80 sites | Limited utilities; no case-insensitive ops, no contains, no split |
| `lib/string.h` (`String` struct) | ~28 sites | Only creation; no manipulation utilities whatsoever |
| Ad-hoc inline code | hundreds of sites | Repetitive `strncmp` prefix checks, manual `tolower` loops, repeated escape routines, manual `strrchr` for file extensions |

Lambda's coding rules **exclude C++ `std::string`** and all C++ containers. The
project needs a **single, comprehensive C string utility layer** that:

1. Replaces unsafe C standard calls with length-bounded equivalents.
2. Provides the "missing" convenience functions (starts_with, trim, split, replace,
   case conversion, escape, file-path helpers) to eliminate code duplication.
3. Delivers high performance for the hot paths (byte search, equality, ASCII
   transforms) through SWAR (SIMD-Within-A-Register) techniques.
4. Works uniformly with `(const char*, size_t)` pairs, interoperable with `StrView`,
   `StrBuf`, and `String*`.

## 2. Codebase Survey

A comprehensive grep of the Lambda codebase reveals the following usage patterns and
frequencies. This data drives every API decision in the library.

### 2.1 C Standard String Function Usage

| Function | Count | Typical call site | Safety concern |
|----------|------:|-------------------|----------------|
| `strcmp` | ~2,020 | Tag name matching in formatters, parsers, runtime | NUL-required; no length bound |
| `strncmp` | ~625 | Prefix checks (e.g., `strncmp(s, "http://", 7)`) | Must manually pass length constant |
| `strlen` | ~494 | Every time a `const char*` enters a length-aware API | Redundant scanning for known-length strings |
| `memcpy` | ~320 | String building, struct copying | No NUL-termination guarantee |
| `snprintf` | ~376 | Number-to-string, error messages, buffer formatting | Good (bounded) but verbose |
| `strdup` | ~109 | Heap duplication of C strings | No length variant; relies on NUL |
| `strstr` | ~94 | Substring search in parsers | Returns pointer, not offset; no length |
| `memset` | ~92 | Buffer clearing | Fine for raw bytes; no char-fill variant |
| `strncpy` | ~72 | Bounded copy (mostly TeX) | Does NOT NUL-terminate if truncated |
| `strcpy` | ~70 | Unbounded copy (TeX, error paths) | ⚠️ Buffer overflow risk |
| `strchr` | ~63 | Single-character search | No length bound; scans past buffer |
| `strcasecmp` | ~67 | Case-insensitive compare (CSS, HTML) | POSIX-only; not in C99 |
| `strtol` | ~48 | Integer parsing from strings | Requires NUL-terminated input |
| `memcmp` | ~43 | Binary comparison | No ordering semantics for strings |
| `atoi` | ~28 | Quick int conversion | ⚠️ No error reporting; UB on overflow |
| `strtod` | ~27 | Float parsing | Requires NUL-terminated input |
| `strcat` | ~22 | String concatenation (CSS, markup) | ⚠️ Buffer overflow risk |
| `strncasecmp` | ~21 | Bounded case-insensitive compare | POSIX-only |
| `tolower` | ~20 | Single-char lowercase (CSS, markup) | Locale-dependent; slow per-char |
| `strrchr` | ~20 | File extension extraction | No length bound |
| `atof` | ~18 | Quick float conversion | ⚠️ No error reporting |
| `memmove` | ~14 | Overlapping copy | Fine |
| `sscanf` | ~11 | Formatted parsing | Requires NUL-terminated input |
| `sprintf` | ~2 | Unbounded formatted output | ⚠️ Should be `snprintf` |
| `toupper` | ~2 | Single-char uppercase | Locale-dependent |

**Total: ~4,500+ call sites** across ~200 source files.

### 2.2 Custom String Utility Usage

| Utility | Count | Location |
|---------|------:|----------|
| `strbuf_*` | ~1,849 | Heap-growable string buffer (append, format) |
| `StringBuf*` | ~619 | Pool-allocated variant of StrBuf |
| `strbuf_append_char` | ~452 | Single character append |
| `strbuf_append_format` | ~252 | printf-style formatted append |
| `strview_*` | ~80 | Non-owning string slice |
| `name_pool_*` | ~68 | Interned name strings |
| `create_string` | ~28 | Heap String from `char*/len` |

### 2.3 Common Patterns Lacking Library Support

These patterns appear **hundreds of times** as ad-hoc inline code:

#### A. Prefix / Suffix Checking (~600+ sites)
```c
// current: manual strncmp with error-prone length constants
if (strncmp(tag, "http://", 7) == 0) ...
if (strcmp(tag + len - 4, ".css") == 0) ...

// proposed:
if (str_starts_with_lit(tag, len, "http://")) ...
if (str_ends_with_lit(tag, len, ".css")) ...
```

#### B. Tag Name / Keyword Matching (~2,000+ sites)
```c
// current: massive if/else chains
if (strcmp(tag, "div") == 0) ...
else if (strcmp(tag, "span") == 0) ...

// proposed: same pattern but safer (length-bounded)
if (str_eq_lit(tag, tag_len, "div")) ...
```

#### C. Case-Insensitive Comparison (~90 sites)
```c
// current: POSIX-only strcasecmp or manual tolower loops
if (strcasecmp(name, "content-type") == 0) ...
for (int i = 0; i < len; i++) buf[i] = tolower(s[i]);

// proposed:
if (str_ieq_lit(name, name_len, "content-type")) ...
str_to_lower(buf, s, len);  // SWAR-accelerated
```

#### D. Trimming (~30+ sites)
```c
// current: manual while loops
while (*s == ' ' || *s == '\t') s++;
while (len > 0 && (s[len-1] == ' ' || s[len-1] == '\n')) len--;

// proposed:
str_trim(&s, &len);
```

#### E. File Extension Extraction (~6+ sites in input.cpp alone)
```c
// current: manual strrchr, repeated everywhere
const char* dot = strrchr(path, '.');
if (dot && strcmp(dot, ".json") == 0) ...

// proposed:
size_t ext_len;
const char* ext = str_file_ext(path, path_len, &ext_len);
if (str_eq_lit(ext, ext_len, ".json")) ...
```

#### F. String Escaping (duplicated across 5+ formatters)
```c
// current: each formatter (JSON, XML, HTML, YAML, SVG) has its own escape loop
// JSON: handle \n, \t, \", \\, \uXXXX
// XML:  handle &amp; &lt; &gt; &quot; &apos;
// each is ~30-50 lines, nearly identical structure

// proposed: single unified API
size_t needed = str_escape_len(s, s_len, STR_ESC_JSON);
str_escape(buf, s, s_len, STR_ESC_JSON);
```

#### G. Split / Replace (Lambda runtime `fn_split`, `fn_replace`, `fn_join`)
```c
// current: manual scan loops with malloc, count-then-copy patterns

// proposed: zero-allocation iterator
StrSplitIter it;
str_split_byte_init(&it, csv, csv_len, ',');
const char* tok; size_t tok_len;
while (str_split_next(&it, &tok, &tok_len)) { ... }
```

#### H. Unsafe `atoi`/`atof` Usage (~46 sites)
```c
// current: no error checking
int val = atoi(str);  // UB on overflow, no failure indication

// proposed: safe with error reporting
int64_t val;
if (str_to_int64(s, len, &val, NULL)) { ... }
```

### 2.4 Pain Point Summary

| # | Problem | Impact | `str_` solution |
|---|---------|--------|-----------------|
| 1 | No centralized `starts_with`/`ends_with` for `(ptr, len)` | Hundreds of manual `strncmp` | `str_starts_with`, `str_ends_with`, `_lit` variants |
| 2 | Repetitive `strcmp` chains for tag matching | 2000+ fragile sites | `str_eq_lit`, `str_ieq_lit` |
| 3 | Duplicate escape routines (JSON, XML, HTML, YAML, SVG) | ~150 LOC duplicated 5× | `str_escape(mode)` |
| 4 | No `str_contains`/`str_indexof` for C callers | Runtime has `fn_*` but no C utility | `str_find`, `str_contains` |
| 5 | `strcpy`/`strcat`/`sprintf` unsafe | 92 buffer-overflow-risk sites | `str_copy`, `str_cat`, `str_fmt` |
| 6 | `atoi`/`atof` with no error checking | 46 UB-risk sites | `str_to_int64`, `str_to_double` |
| 7 | Manual `tolower` loops everywhere | ~20+ sites in CSS/HTML | `str_to_lower` (SWAR-accelerated) |
| 8 | File extension extraction manual | 6+ sites in `input.cpp` | `str_file_ext` |
| 9 | `strcasecmp`/`strncasecmp` not in C99 | POSIX-only, portability risk | `str_icmp`, `str_ieq` |
| 10 | No `str_dup` with length | `strdup` requires NUL termination | `str_dup(s, len)` |

## 3. Design Principles

### 3.1 Length-Bounded Everywhere

Every function takes explicit `(const char* s, size_t len)` pairs. No function
assumes NUL-termination (though outputs are NUL-terminated where applicable). This
design, inspired by StringZilla's core philosophy, prevents:
- Buffer overruns from unterminated strings
- Redundant `strlen` calls on known-length strings
- Inability to work with substrings/embedded NULs

### 3.2 NULL-Tolerant (Never Crash)

All functions treat `NULL` pointers as empty strings (length 0) and return neutral
values. This eliminates defensive `if (s == NULL)` checks at every call site and
follows the existing `strbuf_*` convention.

### 3.3 Minimal Allocation

Most functions are **zero-allocation** — they work on caller-provided buffers or
return pointer/length pairs into the original string. Only `str_dup*` and
`str_replace*` allocate, and they use plain `malloc` so the caller controls lifetime.
This makes the library compatible with Lambda's pool/arena allocators at the call
site level.

### 3.4 SWAR Performance for Hot Paths

The most frequently called operations use SWAR (SIMD Within A Register) to process
**8 bytes per cycle** in a portable `uint64_t`:

| Operation | SWAR technique | Speedup vs scalar |
|-----------|---------------|-------------------|
| `str_find_byte` | Broadcast + XOR + has-zero check | ~4-8× |
| `str_eq` | 8-byte word comparison | ~4-8× |
| `str_to_lower`/`str_to_upper` | Range detection + mask OR | ~4-8× |
| `str_is_ascii` | High-bit check on 8 bytes at once | ~8× |
| `str_utf8_count` | Continuation-byte counting via popcount | ~4-6× |
| `str_count_byte` | Broadcast + popcount | ~6-8× |

These are the same techniques used by StringZilla's `_serial` backends, proven to
pass SMHasher-level quality tests while delivering near-SIMD throughput without any
platform-specific intrinsics.

### 3.5 Consistent Naming & Return Conventions

Following the `lib/` convention (`strbuf_`, `strview_`, `pool_`, `arena_`):

| Convention | Rule | Example |
|-----------|------|---------|
| Prefix | `str_` for all functions | `str_find`, `str_trim` |
| Types | `PascalCase` structs | `StrByteSet`, `StrSplitIter` |
| Constants | `UPPER_CASE` macros | `STR_NPOS` |
| Enums | `PascalCase` type, `STR_` prefix values | `StrEscapeMode`, `STR_ESC_JSON` |
| Position return | `size_t`, `STR_NPOS` for not-found | Like C++ `npos` |
| Predicate return | `bool` | `str_eq`, `str_contains` |
| Ordering return | `int` (<0, 0, >0) | `str_cmp`, `str_icmp` |
| Mutation | Via pointer-to-pointer for zero-copy | `str_trim(&s, &len)` |
| Allocation | Returns `malloc`'d pointer; NULL on failure | `str_dup`, `str_replace_all` |

### 3.6 C99 + `extern "C"` Compatibility

The library is pure C99. The header uses `extern "C"` guards for seamless inclusion
from Lambda's C++ files. The `__attribute__((format(...)))` annotation on `str_fmt`
is conditionally compiled for GCC/Clang only.

### 3.7 NUL-Terminated Output for Legacy Compatibility

All functions that write to a destination buffer **always NUL-terminate** the output,
even when the result is truncated (the only exception is a zero-capacity buffer).
This guarantees that any output from `str_*` functions can be passed directly to
legacy C APIs (`printf`, `fopen`, `strcmp`, etc.) without additional work.

The `(ptr, len)` pair convention coexists cleanly with NUL-termination:

- **Input**: functions accept `(const char* s, size_t len)` — the string need not
  be NUL-terminated; the length is authoritative.
- **Output**: functions that produce strings into caller buffers (`str_copy`,
  `str_to_lower`, `str_cat`, `str_fmt`, etc.) always write a trailing `'\0'`
  within the provided capacity.
- **Allocation**: functions that `malloc` a result (`str_dup*`, `str_replace_all`,
  `str_escape`) always include the NUL in the allocated size.

This design means callers can use `str_*` results as both length-bounded slices and
traditional C strings, making incremental migration smooth — existing code that
expects NUL-terminated strings continues to work without change.

### 3.8 Built-in UTF-8 Support & Conformance

Lambda strings are UTF-8 throughout, and the codebase previously had a small UTF-8
utility library (`lib/utf.h` / `lib/utf.c`) providing five functions:

| `utf.h` function | Purpose |
|------------------|---------|
| `utf8_to_codepoint(utf8, &cp)` | Decode one codepoint, return bytes consumed |
| `utf8_char_len(first_byte)` | Character byte-length from lead byte |
| `utf8_char_count(s)` | Count characters in NUL-terminated string |
| `utf8_char_count_n(s, byte_len)` | Count characters in bounded byte span |
| `utf8_char_to_byte_offset(s, char_idx)` | Convert character index to byte offset |

These are used widely — ~30+ call sites across Lambda core (`lambda-eval.cpp`),
Radiant layout (`layout_text.cpp`), rendering (`render_pdf.cpp`, `render_svg.cpp`),
events, and input processing.

`str.h` already provides three UTF-8 functions with SWAR acceleration:

| `str.h` function | Overlaps with | Advantage |
|------------------|---------------|----------|
| `str_utf8_count(s, len)` | `utf8_char_count` / `utf8_char_count_n` | SWAR popcount (~4-6× faster), length-bounded |
| `str_utf8_char_len(lead)` | `utf8_char_len` | Lookup table, returns `size_t` 0 on invalid |
| `str_utf8_valid(s, len)` | *(none — new)* | Full overlong/surrogate/range validation |

**Unification completed** (see §8 Phase 5): `utf.h`/`utf.c` have been absorbed into
`str.h`/`str.c` by adding the remaining primitives:

```c
// additions now present in str.h (UTF-8 extended)
int    str_utf8_decode(const char* s, size_t len, uint32_t* codepoint);
    // decode one codepoint, return bytes consumed (or -1 on error)
size_t str_utf8_encode(uint32_t codepoint, char* buf, size_t cap);
    // encode codepoint to buf, return bytes written (0 on error/cap)
size_t str_utf8_char_to_byte(const char* s, size_t len, size_t char_index);
    // convert character index to byte offset (STR_NPOS on out-of-range)
size_t str_utf8_byte_to_char(const char* s, size_t len, size_t byte_offset);
    // convert byte offset to character index
```

`utf.h` has been removed, and all UTF-8 call sites now include `str.h` and use
`str_utf8_*` directly. The key improvements over the old `utf.c`:

- **Length-bounded**: all functions take `(ptr, len)`, not NUL-terminated only
- **Validation**: `str_utf8_decode` rejects overlong sequences and surrogates
- **SWAR counting**: `str_utf8_count` is 4-6× faster than the scalar loop in `utf.c`
- **Bidirectional index conversion**: new `str_utf8_byte_to_char` complement
- **Encode**: new `str_utf8_encode` for codepoint → UTF-8 byte output

## 4. API Reference

### 4.1 Comparison (§1)

```c
int  str_cmp(a, a_len, b, b_len);      // lexicographic, like strcmp but length-bounded
int  str_icmp(a, a_len, b, b_len);     // case-insensitive (ASCII)
bool str_eq(a, a_len, b, b_len);       // exact equality (SWAR-accelerated)
bool str_ieq(a, a_len, b, b_len);      // case-insensitive equality
bool str_eq_lit(s, len, "literal");     // compare with NUL-terminated literal
bool str_ieq_lit(s, len, "literal");    // case-insensitive literal compare
```

**Rationale**: `strcmp` is the #1 most-called function (2,020 sites). The `_lit`
variants avoid the need for callers to pass `strlen("literal")` — the function
computes it once internally. `str_eq` uses SWAR 8-byte comparison for long strings.

### 4.2 Prefix / Suffix (§2)

```c
bool str_starts_with(s, s_len, prefix, prefix_len);
bool str_ends_with(s, s_len, suffix, suffix_len);
bool str_starts_with_lit(s, s_len, "prefix");
bool str_ends_with_lit(s, s_len, "suffix");
bool str_istarts_with(s, s_len, prefix, prefix_len);  // case-insensitive
bool str_iends_with(s, s_len, suffix, suffix_len);
```

**Rationale**: The second most common pattern (~625 `strncmp` sites). The `_lit`
variants replace `strncmp(s, "http://", 7)` with `str_starts_with_lit(s, len, "http://")`,
eliminating error-prone manual length constants.

### 4.3 Search (§3)

```c
size_t str_find_byte(s, len, c);           // SWAR-accelerated single byte
size_t str_rfind_byte(s, len, c);          // reverse byte search
size_t str_find(s, s_len, needle, n_len);  // substring (two-byte filter + memcmp)
size_t str_rfind(s, s_len, needle, n_len); // reverse substring
size_t str_ifind(s, s_len, needle, n_len); // case-insensitive
bool   str_contains(s, s_len, needle, n_len);
bool   str_contains_byte(s, s_len, c);
size_t str_find_any(s, s_len, chars, chars_len);     // first byte in char set
size_t str_find_not_any(s, s_len, chars, chars_len); // first byte NOT in char set
size_t str_count(s, s_len, needle, n_len);            // non-overlapping count
size_t str_count_byte(s, s_len, c);                   // SWAR byte count
```

**Rationale**: `strstr` (94 sites) and `strchr` (63 sites) have no length bounds.
`str_find_byte` uses SWAR to scan 8 bytes per iteration. `str_find` uses a
two-byte-filter heuristic (check first+second byte before full `memcmp`) inspired
by StringZilla's needle-anomaly approach.

### 4.4 Byte-Set (§4)

```c
typedef struct { uint64_t bits[4]; } StrByteSet;   // 256-bit bitmap

void   str_byteset_clear(set);
void   str_byteset_add(set, c);
void   str_byteset_add_range(set, lo, hi);
void   str_byteset_add_many(set, chars, len);
void   str_byteset_invert(set);
bool   str_byteset_test(set, c);

// pre-built sets
void   str_byteset_whitespace(set);   // SP,TAB,CR,LF,FF,VT
void   str_byteset_digits(set);       // 0-9
void   str_byteset_alpha(set);        // a-z,A-Z
void   str_byteset_alnum(set);        // a-z,A-Z,0-9

// search with byteset
size_t str_find_byteset(s, len, set);
size_t str_rfind_byteset(s, len, set);
size_t str_find_not_byteset(s, len, set);
```

**Rationale**: Borrowed from StringZilla's `sz_byteset_t`. A 256-bit bitmap gives
O(1) character class membership testing. Replaces `strcspn`/`strspn`, manual loops
checking `c == ' ' || c == '\t' || c == '\n'`, and similar patterns. The pre-built
sets cover Lambda's most common needs.

### 4.5 Trim (§5)

```c
void str_trim(const char** s, size_t* len);        // both ends, ASCII whitespace
void str_ltrim(const char** s, size_t* len);       // left only
void str_rtrim(const char** s, size_t* len);       // right only
void str_trim_chars(const char** s, size_t* len,   // custom character set
                    const char* chars, size_t chars_len);
```

**Rationale**: Trim is zero-copy — it adjusts the pointer and length without touching
the buffer. This is critical for Lambda's document parsing where strings are views
into input buffers. Currently ~30+ sites have manual while-loop trimming.

### 4.6 Case Conversion (§6)

```c
// LUT infrastructure (for custom transforms)
void str_lut_tolower(uint8_t lut[256]);
void str_lut_toupper(uint8_t lut[256]);
void str_lut_identity(uint8_t lut[256]);
void str_transform(dst, src, len, lut);

// convenience (SWAR-accelerated for ASCII)
void str_to_lower(dst, src, len);
void str_to_upper(dst, src, len);
void str_lower_inplace(s, len);
void str_upper_inplace(s, len);
bool str_is_ascii(s, len);
```

**Rationale**: Case conversion is the #1 requested utility. The CSS engine, HTML
parser, and markup formatters all do per-character `tolower` loops. The SWAR
implementation processes 8 bytes per cycle using range detection: for each byte
in a 64-bit word, it detects if the byte is in `[A..Z]` using unsigned subtraction,
then ORs in the 0x20 bit. This is 4-8× faster than scalar `tolower()`.

The LUT infrastructure (inspired by StringZilla's `sz_lookup`) enables custom
byte-to-byte mappings. Users can build a LUT once and apply it to many strings.

### 4.7 Copy / Fill (§7)

```c
size_t str_copy(dst, dst_cap, src, src_len);       // safe strcpy replacement
size_t str_cat(dst, dst_len, dst_cap, src, src_len); // safe strcat replacement
void   str_fill(dst, n, c);                         // memset wrapper
char*  str_dup(s, len);                             // length-aware strdup
char*  str_dup_lower(s, len);                       // dup + lowercase in one pass
char*  str_dup_upper(s, len);                       // dup + uppercase in one pass
```

**Rationale**: `strcpy` (70 sites) and `strcat` (22 sites) are the most dangerous
functions in the codebase. `str_copy` always NUL-terminates and never writes past
`dst_cap`. `str_dup_lower`/`str_dup_upper` fuse allocation + transform (common in
CSS: allocate a buffer, loop tolower, use it — now a single call).

### 4.8 Numeric Parsing (§8)

```c
bool    str_to_int64(s, len, &out, &end);     // safe integer parse
bool    str_to_uint64(s, len, &out, &end);
bool    str_to_double(s, len, &out, &end);
int64_t str_to_int64_or(s, len, default_val); // parse-or-default
double  str_to_double_or(s, len, default_val);
```

**Rationale**: `atoi` (28 sites) and `atof` (18 sites) have undefined behavior on
overflow and no failure indication. `strtol`/`strtod` (75 sites) require
NUL-terminated input. These wrappers work on `(ptr, len)` pairs, use stack buffers
for short strings (avoiding malloc), and return `bool` for explicit success/failure.

### 4.9 Split / Tokenize (§9)

```c
typedef struct {
    const char* src; size_t src_len;
    const char* delim; size_t delim_len;
    size_t pos;
    char _dbuf[4];  // internal storage for single-byte delimiter
} StrSplitIter;

void   str_split_init(it, s, s_len, delim, delim_len);
bool   str_split_next(it, &tok, &tok_len);
void   str_split_byte_init(it, s, s_len, delim_char); // convenience for single byte
size_t str_split_count(s, s_len, delim, delim_len);
```

**Rationale**: Lambda's runtime `fn_split` uses a manual scan loop. The iterator
design is zero-allocation — tokens are returned as `(ptr, len)` views into the
original string. The `_byte_init` variant stores the delimiter character in an
embedded `_dbuf` field so the caller doesn't need to manage lifetime of a 1-byte
string.

Usage:
```c
StrSplitIter it;
str_split_byte_init(&it, "a,b,c", 5, ',');
const char* tok; size_t tok_len;
while (str_split_next(&it, &tok, &tok_len)) {
    // tok = "a" (len=1), "b" (len=1), "c" (len=1)
}
```

### 4.10 Replace (§10)

```c
char* str_replace_all(s, s_len, old, old_len, new, new_len, &out_len);
char* str_replace_first(s, s_len, old, old_len, new, new_len, &out_len);
```

**Rationale**: The runtime `fn_replace` counts occurrences first, allocates once,
then copies. These functions use the same count-then-allocate pattern. Returns a
`malloc`'d string. The caller can use their preferred allocator for the result.

### 4.11 File Path Helpers (§11)

```c
const char* str_file_ext(path, path_len, &ext_len);      // ".json" with dot
const char* str_file_basename(path, path_len, &name_len); // "file.json"
```

**Rationale**: `strrchr(path, '.')` appears 6+ times in `input.cpp` alone for format
detection. `str_file_ext` scans backwards, stops at `/` or `\\`, returns a zero-copy
pointer into the original path.

### 4.12 Hashing (§12)

```c
uint64_t str_hash(s, len);    // FNV-1a, 64-bit
uint64_t str_ihash(s, len);   // case-insensitive FNV-1a
```

**Rationale**: Lambda's name pool and hash maps need string hashing. FNV-1a is simple,
fast, and has excellent distribution. The case-insensitive variant uses the internal
`_lut_lower` table to normalize before hashing — useful for CSS property names and
HTML tag matching.

### 4.13 UTF-8 Utilities (§13)

```c
size_t str_utf8_count(s, len);          // codepoint count (SWAR-accelerated)
size_t str_utf8_char_len(lead_byte);    // 1, 2, 3, or 4 (0 for invalid)
bool   str_utf8_valid(s, len);          // full validation
```

**Rationale**: Lambda processes Unicode text extensively. `str_utf8_count` uses SWAR
to count non-continuation bytes (8 bytes per cycle via popcount). The validator
checks overlong encodings, surrogate codepoints, and range limits.

### 4.14 Escape (§14)

```c
typedef enum { STR_ESC_JSON, STR_ESC_XML, STR_ESC_HTML, STR_ESC_URL } StrEscapeMode;

size_t str_escape(dst, s, s_len, mode);      // escape into dst (NULL for sizing)
size_t str_escape_len(s, s_len, mode);        // compute required size
```

**Rationale**: JSON, XML, HTML, YAML, and SVG formatters each implement nearly
identical escape loops (~30-50 LOC each, duplicated 5 times). A single mode-driven
function eliminates ~150 LOC of duplication. The `dst=NULL` convention (like
`snprintf(NULL, 0, ...)`) enables two-pass sizing.

### 4.15 Span / Predicates (§15)

```c
size_t str_span_whitespace(s, len);   // count leading whitespace bytes
size_t str_span_digits(s, len);       // count leading digit bytes
size_t str_span(s, len, predicate);   // custom predicate
bool   str_all(s, len, predicate);    // all bytes satisfy predicate

// character predicates
bool str_is_space(c);  bool str_is_digit(c);  bool str_is_alpha(c);
bool str_is_alnum(c);  bool str_is_upper(c);  bool str_is_lower(c);
bool str_is_hex(c);
```

**Rationale**: The character predicates are `static inline`-quality simple functions
that avoid locale-dependent `<ctype.h>` behavior. `str_span_*` replaces common
`while (isspace(*s)) s++` loops.

### 4.16 Formatting Helpers (§16)

```c
int   str_fmt(dst, cap, fmt, ...);              // safe snprintf wrapper
char* str_hex_encode(dst, s, len);              // binary → hex string
size_t str_hex_decode(dst, hex, hex_len);       // hex string → binary
```

**Rationale**: `str_fmt` wraps `vsnprintf` with guaranteed NUL-termination and
clamped return value. The hex functions support Lambda's binary data handling and
`\uXXXX` escape processing.

## 5. Design Decisions & Alternatives Considered

### 5.1 Why Not Use StringZilla Directly?

StringZilla (included in `./StringZilla/`) is an excellent library, but:

| Concern | Detail |
|---------|--------|
| **Scope** | StringZilla is a full-featured library (~20K LOC) with SIMD backends for 6 architectures, Python/Rust/JS bindings, sort/intersect operations. Lambda needs ~1,300 LOC. |
| **API surface** | StringZilla's API is designed for library consumers, not for replacing C stdlib in a large codebase. It lacks convenience functions like `_lit` variants, escape modes, file path helpers, numeric parsing. |
| **Build integration** | StringZilla's header-only design with multi-architecture dispatch adds build complexity. Lambda needs a simple `.c`/`.h` pair that compiles in the existing Premake build. |
| **Naming convention** | `sz_` prefix doesn't match Lambda's `str_`/`strbuf_`/`strview_` naming. |

**What we borrow from StringZilla**:
- Length-bounded API philosophy (never NUL-terminated)
- `StrByteSet` (256-bit bitmap) for character class matching
- SWAR byte-equality detection (`_swar_has_zero`, `_swar_has_byte`)
- SWAR range detection for case conversion
- LUT-based transform architecture (`str_transform` / `str_lut_*`)
- Branchless comparison patterns

### 5.2 Why `(const char*, size_t)` Instead of `StrView`?

Using raw `(ptr, len)` pairs instead of `StrView` structs:
- Avoids coupling `str.h` to `strview.h`
- Works with any string representation (StrView, StrBuf, String*, raw char*)
- No struct-passing overhead for small functions
- The caller can trivially wrap: `str_find(sv.str, sv.length, ...)`

### 5.3 Why `malloc` for Allocating Functions?

Functions like `str_dup`, `str_replace_all` use plain `malloc` rather than taking a
`Pool*` or `Arena*` parameter because:
- The library should be independent of Lambda's memory system
- Callers can easily wrap: `arena_strdup` already exists for arena allocation
- Most call sites free the result quickly (temporary during formatting)

### 5.4 Why Not Thread-Safe LUT Init?

The internal `_ensure_luts()` uses a simple boolean guard, not `pthread_once`. This
is acceptable because:
- Lambda is single-threaded in its core execution model
- The LUT init is idempotent (multiple inits produce the same result)
- The first call to any case-conversion function initializes the LUTs

## 6. Performance Analysis

### 6.1 SWAR Techniques Used

**Byte broadcast**: `c * 0x0101010101010101ULL` — replicates byte `c` to all 8
positions of a 64-bit word.

**Has-zero detection**: `(v - 0x01...01) & ~v & 0x80...80` — detects any zero byte
in a word. Combined with XOR (for matching a specific byte), this finds matching
bytes 8 at a time.

**ASCII range detection** (for `str_to_lower`):
```
sub = word - broadcast('A')        // underflows for bytes < 'A'
above = word - broadcast('Z'+1)    // underflows for bytes <= 'Z'
is_upper = (~sub & above) & 0x80..80   // high bit set where byte ∈ [A..Z]
mask = (is_upper >> 2) | (is_upper >> 5)  // propagate to bit 5 position
word |= (mask & broadcast(0x20))   // set bit 5 → convert to lowercase
```

**UTF-8 codepoint counting**: Count non-continuation bytes (bytes where top 2 bits
≠ `10`). Uses `popcount` on the byte-level high-bit mask after masking out bit 6.

### 6.2 Expected Performance Gains

| Function           | Replaces                | Expected improvement                               |
| ------------------ | ----------------------- | -------------------------------------------------- |
| `str_find_byte`    | `memchr`/`strchr`       | Comparable to libc (SWAR); safer (bounded)         |
| `str_eq`           | `strcmp` + `strlen`     | Eliminates redundant `strlen`; SWAR 8-byte compare |
| `str_to_lower`     | Per-char `tolower()`    | 4-8× (SWAR processes 8 bytes/cycle)                |
| `str_is_ascii`     | Per-char check          | 8× (check 8 bytes at once)                         |
| `str_utf8_count`   | Per-byte loop           | 4-6× (popcount on 8-byte word)                     |
| `str_byteset_test` | Linear scan of char set | O(1) vs O(n) for set membership                    |
| `str_count_byte`   | Loop + increment        | 4-6× (SWAR + popcount)                             |

## 7. Relationship to Existing `lib/` Components

```
┌──────────────────────────────────────────────────────────┐
│  Application Code (parsers, formatters, runtime, etc.)   │
│  (may call str.h directly, or through higher layers)     │
├──────────────┬──────────────┬─────────────┬──────────────┤
│  strbuf.h    │  strview.h   │  string.h   │  utf.h       │
│  (building)  │  (views)     │  (ref-cnt)  │  (compat)    │
│              │              │             │              │
│  append_str  │  eq, find    │  String*    │  utf8_to_*   │
│  append_char │  trim        │  create     │  utf8_char_* │
│  append_fmt  │  start_with  │  from_sv    │  (wrappers)  │
│  ensure_cap  │  sub         │             │              │
├──────────────┴──────────────┴─────────────┴──────────────┤
│  str.h  — foundational string utilities (§3 principles)  │
│                                                          │
│  compare · search · transform · trim · split · escape    │
│  copy · parse · path · hash · utf8 · byteset · format    │
│                                                          │
│  SWAR-accelerated · NULL-tolerant · NUL-terminated       │
│  length-bounded · zero-allocation · pure C99             │
├──────────────────────────────────────────────────────────┤
│  mempool.h / arena.h (memory allocation)                 │
└──────────────────────────────────────────────────────────┘
```

After Phase 2 migration, the higher-level libraries (`strbuf.h`, `strview.h`,
`string.h`) delegate their string logic to `str.h`. After Phase 5, `utf.h`
becomes a thin compatibility header forwarding to `str_utf8_*` functions.

**`str.h` does NOT replace `strbuf.h` or `strview.h`**. It complements them:

- **`str.h`** — stateless utility functions on `(ptr, len)` pairs. No ownership.
- **`strbuf.h`** — owned, growable, heap-allocated string builder. Uses `str.h`
  internally for its operations.
- **`strview.h`** — non-owning `(ptr, len)` pair with a few methods. `str.h`
  subsumes and extends its utility functions.
- **`string.h`** — Lambda's ref-counted `String` struct. Uses pool allocation.

Over time, `strview.h` utility functions can be reimplemented as thin wrappers
around `str.h` to avoid duplication.

## 8. Migration Strategy

Migration should be **incremental** — the library is additive and doesn't break
existing code. The key insight is to **consolidate the secondary string utility
libraries first**, since they are small, self-contained, and their callers will
automatically benefit from the improved internals.

### Phase 1: Integration (immediate)
- Add `str.c` to the Premake build (`build_lambda_config.json`)
- Include `str.h` in new code; use it for all new string operations
- Write unit tests (`test/test_str.cpp` using GTest)

### Phase 2: Secondary String Library Migration (high priority)

Before touching the broader Lambda codebase, migrate the four secondary string
utility libraries to use `str.h` internally. This serves as both a proving ground
for the new API and an immediate quality improvement to these foundational layers.

**StrView** (`lib/strview.h`):
- Reimplement `strview_eq` → delegates to `str_eq`
- Reimplement `strview_find` → delegates to `str_find_byte` / `str_find`
- Reimplement `strview_trim` → delegates to `str_trim`
- Reimplement `strview_start_with` → delegates to `str_starts_with`
- Reimplement `strview_sub` → bounds-check + pointer arithmetic (already trivial)
- Reimplement `strview_to_int` → delegates to `str_to_int64`
- Reimplement `strview_to_cstr` → delegates to `str_copy`
- **API expansion**: consider adding `strview_ends_with`, `strview_contains`,
  `strview_to_double`, `strview_split` as thin wrappers
- **Code review**: verify all functions are NULL-tolerant and consistent with
  `str.h` conventions

**StrBuf** (`lib/strbuf.h` / `strbuf.c`):
- Replace internal `memcpy`/`memmove` append logic with `str_copy` / `str_cat`
  where appropriate
- Replace `strbuf_find_*` manual loops with `str_find` / `str_find_byte`
- Replace `strbuf_trim` internal implementation with `str_trim`
- Use `str_to_lower` / `str_to_upper` for case conversion if added
- **API expansion**: consider `strbuf_replace`, `strbuf_insert`, `strbuf_fmt`
  leveraging `str_replace_all` and `str_fmt`
- **Code review**: ensure growth strategy and NUL-termination are consistent;
  review error handling paths

**String** (`lib/string.h`):
- This is Lambda's ref-counted, pool-allocated `String` struct (`{len, ref_cnt, chars[]}`)
- Review `create_string` and `string_from_strview` for correctness
- **API expansion**: consider `string_eq`, `string_cmp`, `string_hash` that
  delegate to `str_eq`, `str_cmp`, `str_hash` on the embedded `chars`
- **Code review**: verify ref-counting edge cases, pool allocation alignment

**StringBuf** (pool-growable variant):
- Same treatment as StrBuf but using pool allocation
- Review growth semantics with `pool_realloc` compatibility
- Ensure `str.h` functions integrate cleanly with pool-backed buffers

**Success criteria for Phase 2:**
- All secondary string libraries pass their existing tests after refactor
- No duplicated string logic remains in `strview.h`, `strbuf.c`, etc.
- New API additions have unit tests
- Code review notes documented

### Phase 3: Unsafe Call Replacement (gradual)
- Replace `strcpy` → `str_copy` (70 sites, highest risk)
- Replace `strcat` → `str_cat` (22 sites)
- Replace `atoi`/`atof` → `str_to_int64`/`str_to_double` (46 sites)
- Replace `sprintf` → `str_fmt` (2 sites)

### Phase 4: Convenience Adoption (ongoing)
- Replace `strncmp(s, "prefix", N)` → `str_starts_with_lit(s, len, "prefix")`
- Replace manual `tolower` loops → `str_to_lower` / `str_dup_lower`
- Replace manual trim loops → `str_trim`
- Consolidate escape routines → `str_escape(mode)`
- Replace `strrchr(path, '.')` → `str_file_ext`

### Phase 5: UTF-8 Unification (completed)

`lib/utf.h` / `lib/utf.c` have been absorbed into `str.h` / `str.c` (see §3.8).

- Added `str_utf8_decode`, `str_utf8_encode`, `str_utf8_char_to_byte`,
  `str_utf8_byte_to_char` to `str.h` / `str.c`
- Migrated call sites to `str_utf8_*` in Lambda and Radiant
- Removed the duplicate `utf8_char_len` definition in `lambda/tex/tex_doc_model_text.cpp`
- Removed `lib/utf.c` from the build (all logic lives in `str.c`)
- Removed `lib/utf.h` (callers must include `str.h`)
- **Follow-up**: consolidate `lib/cmdedit_utf8.h` / `cmdedit_utf8.c` — its
  `cmdedit_utf8_char_count` and `cmdedit_utf8_char_to_byte_offset` should become
  trivial wrappers around the `str_utf8_*` equivalents

## 9. Testing Plan

### Unit Tests (`test/test_str.cpp`)
- **Comparison**: empty, equal, unequal, different lengths, case-insensitive, NUL-embedded
- **Prefix/Suffix**: exact match, too long, empty prefix, case-insensitive
- **Search**: single byte, multi-byte needle, not found, at boundaries, reverse
- **ByteSet**: add, range, many, invert, test, find, rfind
- **Trim**: whitespace, custom chars, all-whitespace, empty
- **Case conversion**: ASCII, mixed, non-ASCII pass-through, in-place, dup
- **Copy/Fill**: exact fit, truncation, zero capacity, NUL-termination
- **Numeric parsing**: valid, overflow, leading whitespace, trailing junk, default
- **Split**: single byte, multi-byte delimiter, empty tokens, no delimiter
- **Replace**: all, first, no match, overlapping pattern, grow/shrink
- **File path**: extension extraction, basename, no extension, trailing slash
- **Hash**: deterministic, case-insensitive equality
- **UTF-8**: count, char_len, validation (valid, overlong, surrogate, truncated)
- **Escape**: JSON, XML, URL modes, control chars, high bytes
- **Span/Predicate**: whitespace, digits, custom predicate, all
- **Formatting**: str_fmt truncation, hex encode/decode

### Fuzz Testing
- Feed random byte sequences to `str_utf8_valid`, `str_to_int64`, `str_escape`
- Feed random lengths to all functions to verify bounds safety

### Performance Benchmarks
- `str_find_byte` vs `memchr` on 1KB, 64KB, 1MB buffers
- `str_to_lower` vs per-char `tolower` on typical document sizes
- `str_eq` vs `strcmp` + `strlen` on typical tag names (3-10 bytes)
- `str_utf8_count` vs byte-loop on multilingual text

## 10. Files

| File | Lines | Description |
|------|------:|-------------|
| `lib/str.h` | ~380 | Header with all declarations, doc comments, `extern "C"` |
| `lib/str.c` | ~1,284 | Full implementation with SWAR optimizations |

Both files compile cleanly under:
- `cc -std=c99 -Wall -Wextra -Wpedantic` (zero warnings)
- `c++ -std=c++17 -Wall -Wextra` (zero warnings, via `extern "C"`)

## 11. Summary

`lib/str.h` provides Lambda with a **safe, convenient, and fast** C string foundation:

- **86 functions** across 16 categories — covering comparison, search, prefix/suffix,
  trim, case conversion, copy, parsing, split, replace, file paths, hashing, UTF-8,
  escaping, predicates, and formatting.
- **Zero unsafe patterns** — all length-bounded, NULL-tolerant, NUL-terminating.
- **NUL-terminated output** — every output buffer is NUL-terminated for seamless
  interop with legacy C APIs during incremental migration.
- **Built-in UTF-8** — SWAR-accelerated counting/validation, with a clear path to
  absorb `utf.h`/`utf.c` and provide full decode/encode/index-conversion primitives.
- **SWAR acceleration** — 4-8× speedup on hot paths (byte search, equality, case
  conversion, ASCII check, UTF-8 counting).
- **Zero external dependencies** — pure C99 with optional GCC/Clang attributes.
- **~1,660 LOC total** — minimal footprint, easy to audit.
- Directly addresses **10 identified pain points** from the codebase survey.
- **5-phase incremental migration** — starting with build integration, then
  consolidating secondary string libraries (StrView, StrBuf, String, StringBuf),
  then unsafe call replacement, convenience adoption, and UTF-8 unification.

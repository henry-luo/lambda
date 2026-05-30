# `./lib` Enhancement Proposal 2

Follow-up scan of `lambda/` and `radiant/` after the first `Lib_Enhance.md`
round. The first round is now substantively complete; this document captures
larger or newer centralization opportunities that should be picked up only when
their risk/benefit balance is favorable.

**Status legend:** Done · Partial · Deferred

---

## 1. `lib/escape.h` / `lib/escape.c` — Done

### Rationale

`lambda/format/format-utils.{h,cpp}` already owns a strong table-driven escape
system (`EscapeRule`, `EscapeCtrlMode`, `format_escaped_string_ex`) for JSON,
HTML, XML, LaTeX, YAML, JSX, INI, and properties. Radiant and other subsystems
still have local escapers, for example SVG subscene text/attribute escaping in
`radiant/render_svg_inline.cpp`.

### Proposed API

```c
typedef struct {
    char from;
    const char* to;
} EscapeRule;

typedef enum {
    ESCAPE_CTRL_NONE = 0,
    ESCAPE_CTRL_JSON_UNICODE,
    ESCAPE_CTRL_XML_NUMERIC,
    ESCAPE_CTRL_DROP
} EscapeCtrlMode;

void escape_append(StrBuf* out, const char* s, size_t len,
                   const EscapeRule* rules, int rule_count,
                   EscapeCtrlMode ctrl_mode);

extern const EscapeRule ESCAPE_RULES_JSON[];
extern const int ESCAPE_RULES_JSON_COUNT;
extern const EscapeRule ESCAPE_RULES_HTML_TEXT[];
extern const int ESCAPE_RULES_HTML_TEXT_COUNT;
extern const EscapeRule ESCAPE_RULES_HTML_ATTR[];
extern const int ESCAPE_RULES_HTML_ATTR_COUNT;
extern const EscapeRule ESCAPE_RULES_XML_ATTR[];
extern const int ESCAPE_RULES_XML_ATTR_COUNT;
extern const EscapeRule ESCAPE_RULES_LATEX[];
extern const int ESCAPE_RULES_LATEX_COUNT;
extern const EscapeRule ESCAPE_RULES_YAML[];
extern const int ESCAPE_RULES_YAML_COUNT;
extern const EscapeRule ESCAPE_RULES_JSX_TEXT[];
extern const int ESCAPE_RULES_JSX_TEXT_COUNT;
extern const EscapeRule ESCAPE_RULES_JSX_ATTR[];
extern const int ESCAPE_RULES_JSX_ATTR_COUNT;
```

### Migration targets

- ✅ Added `lib/escape.h` / `lib/escape.c` with `escape_append()` and shared
  JSON, HTML text, HTML attr, and XML attr rule tables.
- ✅ Added `lib/escape.c` to the `lambda-lib` target.
- ✅ `radiant/render_svg_inline.cpp`: replaced local SVG subscene text and
  attribute escapers.
- ✅ Added `escape_append_stringbuf()` so formatters can reuse the shared escape
  loop without changing `StringBuf` ownership.
- ✅ `lambda/format/format-utils.{h,cpp}`: generic escape helpers now delegate
  to `lib/escape`; JSON, HTML, XML, LaTeX, YAML, and JSX rule tables now alias
  shared lib tables.
- Future HTTP/log/debug output paths that need JSON or HTML-safe emission.

### Risk

Medium. Escaping bugs are quiet output bugs. The current formatter API uses
`StringBuf*`, while `lib/strbuf.h` uses `StrBuf*`; the migration either needs a
small adapter layer or a deliberate string-buffer unification step.

### Verification

`test_strbuf_gtest` covers JSON control escaping and XML attribute escaping.
`test_stringbuf_gtest` covers the `StringBuf` adapter path and formatter rule
tables for LaTeX, YAML, and JSX.

---

## 2. `lib/digest.h` / `lib/digest.c` — Done

### Rationale

SHA logic used to be split:

- `lambda/js/js_crypto.cpp` had native SHA-256 and SHA-384/SHA-512
  implementations for JS crypto and HMAC.
- `lambda/network/enhanced_file_cache.cpp` uses mbedTLS SHA-256 for cache keys.

A small digest facade would make future cache/security callers avoid copying
hash code, and could let `js_crypto` use mbedTLS where linked.

### Proposed API

```c
size_t digest_output_len_bits(int bits);
bool digest_compute_bits(int bits, const void* data, size_t len,
                         uint8_t* out, size_t out_len);
bool digest_sha256(const void* data, size_t len, uint8_t out[32]);
bool digest_sha384(const void* data, size_t len, uint8_t out[48]);
bool digest_sha512(const void* data, size_t len, uint8_t out[64]);

typedef struct DigestCtx DigestCtx;
DigestCtx* digest_ctx_new(int bits);
bool digest_update(DigestCtx* ctx, const void* data, size_t len);
bool digest_finalize(DigestCtx* ctx, uint8_t* out, size_t out_len);
void digest_ctx_free(DigestCtx* ctx);
```

### Risk

Low to medium. Algorithm output should be identical, but JS crypto conformance
needs targeted verification, especially empty input, block-boundary input, and
large input. Also consider whether pulling mbedTLS into a generic lib object
has unwanted link impact for small test binaries.

### Migration targets

- ✅ `lambda/js/js_crypto.cpp`: removed the local SHA-256/SHA-384/SHA-512 block
  implementations from the JS crypto module.
- ✅ Added `lib/digest.h` / `lib/digest.c` with mbedTLS `md` backed one-shot
  SHA-1/SHA-224/SHA-256/SHA-384/SHA-512 dispatch and streaming context support.
- ✅ Added `lib/digest.c` to `lambda-lib` and linked `mbedcrypto` there.
- ✅ `lambda/js/js_crypto.cpp`: native SHA helpers, `createHash()`, and
  `crypto.subtle.digest()` now use the shared digest facade.
- ✅ `lambda/network/enhanced_file_cache.cpp`: cache-key SHA-256 now uses the
  shared digest facade instead of direct `mbedtls_sha256_*` calls.
- ✅ HMAC and PBKDF2 continue to use mbedTLS directly because `lib/digest` owns
  plain digest operations, not MAC or KDF APIs.

### Recommendation

Keep `lib/digest` deliberately small. Add HMAC/KDF wrappers only if another
non-JS caller needs those APIs; otherwise JS crypto can stay close to mbedTLS
for Node-compatible MAC and PBKDF2 behavior.

---

## 3. `lib/sort.h` O(n log n) sort — Done

### Rationale

The first round replaced local bubble/insertion loops with shared
`insertion_sort()`. That is simpler and tested, but still O(n^2). User-facing
sort paths can receive large arrays.

### Proposed API

```c
void sort_qsort_r(void* base, size_t count, size_t stride,
                  SortCmpFn cmp, void* udata);

void introsort(void* base, size_t count, size_t stride,
               SortCmpFn cmp, void* udata);
```

`sort_qsort_r` is implemented as a portable heap-sort based helper using the
existing `SortCmpFn` signature and user-data argument. `introsort` is currently
an alias to the O(n log n) helper, keeping a stable API name for future tuning.

### Migration targets

- ✅ Added public `sort_qsort_r()` / `introsort()` entry points in `lib/sort.h`.
- ⏳ Existing small-array callers remain on `insertion_sort()`; migrate vector
  and Python sort paths when profiling says large inputs matter.

### Risk

Low for a wrapper, medium for a local introsort implementation. Comparator
semantics must remain exactly compatible with current `SortCmpFn`.

### Recommendation

Add the `qsort_r` shim first if sort performance shows up in profiles. Keep
`insertion_sort()` for tiny and nearly sorted arrays.

`test_sort_gtest` covers user-data comparators and the `introsort()` alias.

---

## 4. Composite-key hashmap helper macros — Done

### Rationale

The first round eliminated many single-field hashmap compare/hash pairs, but
several composite-key maps remain:

- `radiant/state_store.cpp`: pointer plus interned state-name pointer.
- `lambda/render_map.cpp`: item plus template pointer.
- `lambda/template_state.cpp`: item plus template pointer plus state-name
  pointer.

### Proposed API

```c
HASHMAP_DEFINE_FIELD2_KEY(name, struct_type, first_field, second_field)
HASHMAP_DEFINE_FIELD3_KEY(name, struct_type, first_field, second_field, third_field)
```

The implemented macros accept scalar or pointer identity fields, including
nested field expressions such as `key.source_item.item`.

### Migration targets

- ✅ Added `HASHMAP_DEFINE_FIELD2_KEY()` and `HASHMAP_DEFINE_FIELD3_KEY()` to
  `lib/hashmap_helpers.h`.
- ✅ `radiant/state_store.cpp`: `StateEntry` key `(node, name)` now uses
  `HASHMAP_DEFINE_FIELD2_KEY()`.
- ✅ `lambda/render_map.cpp`: reverse result lookup and `(source_item,
  template_ref)` render-map keys now use hashmap helper macros.
- ✅ `lambda/template_state.cpp`: `(model_item, template_ref, state_name)` now
  uses `HASHMAP_DEFINE_FIELD3_KEY()`.

### Risk

Medium. The API can quickly become a macro zoo, and changing hash mixing changes
bucket layout. Correctness should hold, but performance and iteration order can
shift.

### Verification

`test_hashmap_helpers_gtest` covers two-field and three-field composite identity
keys.

---

## 5. Path extension helpers — Done

### Rationale

`lib/file.h` already has `file_path_ext()`, `file_path_basename()`, and
`file_path_dirname()`, but some code still uses raw `strrchr(path, '.')` for
extension detection. Those call sites can accidentally treat dots in directory
names as file extensions.

### Proposed API

```c
const char* file_path_ext_len(const char* path, size_t path_len,
                              size_t* ext_len);
bool file_path_has_ext_ci(const char* path, const char* ext);
```

### Migration targets

- ✅ Added `file_path_ext_len()` and `file_path_has_ext_ci()` to `lib/file`.
- ✅ `radiant/window.cpp` document-format detection now uses `file_path_ext_len()`.
- ✅ `radiant/webview_child_linux.cpp` MIME detection now uses
  `file_path_has_ext_ci()`.
- ✅ `lambda/input/markup/format_registry.cpp` filename format detection now
  uses `file_path_ext_len()`.

### Risk

Low. This is a narrow extension of existing path utilities.

### Recommendation

Good small follow-up. Prefer this before introducing another file-format
detection helper.

`test_file_module_gtest` covers explicit-length extension lookup, dots in
directories, trailing separators, trailing dots, and case-insensitive checks.

---

## 6. Non-mutating string split iterator — Done

### Rationale

`lib/str.h` already has `StrSplitIter`, but some parser code still uses
`strtok()`, which mutates input and has global iteration state. TOML dotted
section parsing is one current example.

### Proposed API

If the existing `StrSplitIter` is sufficient, document and migrate to it. If not,
add a tiny wrapper:

```c
typedef struct {
    StrView rest;
    char delimiter;
} StrViewSplitIter;

void strview_split_init(StrViewSplitIter* it, StrView input, char delimiter);
bool strview_split_next(StrViewSplitIter* it, StrView* token);
```

### Migration targets

- ✅ Added `StrViewSplitIter` to `lib/strview`.
- ✅ `lambda/input/input-toml.cpp`: dotted section path traversal no longer uses
  `strtok()` or a fixed mutable section buffer.
- CSS/DOM class-token parsing sites that currently use copied buffers plus
  `strtok()`.

### Risk

Low, provided callers retain current empty-token behavior where it matters.

### Recommendation

Worth doing as a contained parser-safety cleanup.

`test_strview_gtest` covers normal split iteration and empty-token preservation.

---

## Priority order

Implemented:

1. Path extension helpers and three call-site migrations.
2. String split iterator and TOML dotted-section migration.
3. `lib/escape` and SVG subscene escaping migration.
4. Formatter escape helpers using the shared `lib/escape` loop and shared
   JSON/HTML/XML/LaTeX/YAML/JSX tables.
5. Sort wrapper entry points.
6. JS crypto SHA/HMAC reuse of mbedTLS `md` APIs.
7. Composite-key hashmap helper macros and three local map migrations.
8. Generic digest facade and JS/cache SHA migrations.

Still deferred:

1. HMAC/KDF facade wrappers: wait for a second non-JS caller.

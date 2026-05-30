# `./lib` Enhancement Proposal 2

Follow-up scan of `lambda/` and `radiant/` after the first `Lib_Enhance.md`
round. The first round is now substantively complete; this document captures
larger or newer centralization opportunities that should be picked up only when
their risk/benefit balance is favorable.

**Status legend:** Proposed · Defer · Do when needed

---

## 1. `lib/escape.h` / `lib/escape.c` — Proposed

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
```

### Migration targets

- `lambda/format/format-utils.{h,cpp}`: move rule tables and core escape loop.
- `radiant/render_svg_inline.cpp`: replace local XML/SVG text and attribute
  escapers.
- Future HTTP/log/debug output paths that need JSON or HTML-safe emission.

### Risk

Medium. Escaping bugs are quiet output bugs. The current formatter API uses
`StringBuf*`, while `lib/strbuf.h` uses `StrBuf*`; the migration either needs a
small adapter layer or a deliberate string-buffer unification step.

### Recommendation

Do this when a second non-format caller needs robust escaping. Do not fold it
into unrelated formatter work.

---

## 2. `lib/digest.h` / `lib/digest.c` — Do when needed

### Rationale

SHA logic is still split:

- `lambda/js/js_crypto.cpp` has native SHA-256 and SHA-384/SHA-512
  implementations for JS crypto and HMAC.
- `lambda/network/enhanced_file_cache.cpp` uses mbedTLS SHA-256 for cache keys.

A small digest facade would make future cache/security callers avoid copying
hash code, and could let `js_crypto` use mbedTLS where linked.

### Proposed API

```c
void digest_sha256(const void* data, size_t len, uint8_t out[32]);
void digest_sha384(const void* data, size_t len, uint8_t out[48]);
void digest_sha512(const void* data, size_t len, uint8_t out[64]);

typedef struct DigestCtx DigestCtx;
DigestCtx* digest_ctx_new(int bits);
void digest_update(DigestCtx* ctx, const void* data, size_t len);
bool digest_finalize(DigestCtx* ctx, uint8_t* out, size_t out_len);
void digest_ctx_free(DigestCtx* ctx);
```

### Risk

Low to medium. Algorithm output should be identical, but JS crypto conformance
needs targeted verification, especially empty input, block-boundary input, and
large input. Also consider whether pulling mbedTLS into a generic lib object
has unwanted link impact for small test binaries.

### Recommendation

Worth doing if another SHA caller appears or JS crypto becomes a measured
hotspot.

---

## 3. `lib/sort.h` O(n log n) sort — Proposed

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

`sort_qsort_r` can be a portability shim over platform `qsort_r`; `introsort`
can wait until there is a measured need for a local algorithm.

### Migration targets

- `lambda/lambda-vector.cpp`: `fn_sort1`, median helpers.
- `lambda/py/py_builtins.cpp`: `sorted()` and `list.sort()`.

### Risk

Low for a wrapper, medium for a local introsort implementation. Comparator
semantics must remain exactly compatible with current `SortCmpFn`.

### Recommendation

Add the `qsort_r` shim first if sort performance shows up in profiles. Keep
`insertion_sort()` for tiny and nearly sorted arrays.

---

## 4. Composite-key hashmap helper macros — Defer

### Rationale

The first round eliminated many single-field hashmap compare/hash pairs, but
several composite-key maps remain:

- `radiant/state_store.cpp`: pointer plus interned state-name pointer.
- `lambda/render_map.cpp`: item plus template pointer.
- `lambda/template_state.cpp`: item plus template pointer plus state-name
  pointer.

### Proposed API

```c
HASHMAP_DEFINE_PTR_PTR_KEY(name, struct_type, first_field, second_field)
HASHMAP_DEFINE_INT_PTR_KEY(name, struct_type, int_field, ptr_field)
HASHMAP_DEFINE_PTR_STR_KEY(name, struct_type, ptr_field, str_field)
HASHMAP_DEFINE_CUSTOM_KEY(name, struct_type, hash_body, cmp_body)
```

### Risk

Medium. The API can quickly become a macro zoo, and changing hash mixing changes
bucket layout. Correctness should hold, but performance and iteration order can
shift.

### Recommendation

Defer until a new composite map appears. If implemented, start with exactly one
shape that has two or more current callers.

---

## 5. Path extension helpers — Proposed

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

- `radiant/window.cpp` document-format detection.
- `radiant/webview_child_linux.cpp` MIME detection.
- `lambda/input/markup/format_registry.cpp` filename format detection.

### Risk

Low. This is a narrow extension of existing path utilities.

### Recommendation

Good small follow-up. Prefer this before introducing another file-format
detection helper.

---

## 6. Non-mutating string split iterator — Proposed

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

- `lambda/input/input-toml.cpp`: dotted section path traversal.
- CSS/DOM class-token parsing sites that currently use copied buffers plus
  `strtok()`.

### Risk

Low, provided callers retain current empty-token behavior where it matters.

### Recommendation

Worth doing as a contained parser-safety cleanup.

---

## Priority order

1. Path extension helpers: small, low-risk, likely bug prevention.
2. String split iterator migration: small parser-safety improvement.
3. `lib/escape`: larger, valuable once another caller needs it.
4. Sort wrapper: performance-driven.
5. Digest wrapper: dependency/performance-driven.
6. Composite hashmap macros: defer until a new repeated shape appears.

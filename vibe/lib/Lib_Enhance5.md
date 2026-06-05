# `./lib` Enhancement Proposal 5 — Centralized String Scanners

> **Implementation status (2026-06-05).**
> - **Phase 0 — done.** `lib/str.h`/`lib/str.c` §17 scanner tier landed:
>   NUL-safe classes (`str_char_in_set`, `str_char_is_*`), bounded `strn_*`,
>   NUL-terminated `str_*`, and `StrCursor`. Covered by
>   `test/lib/test_str_scan_gtest.cpp` (29 cases, incl. the AsciiDoc regression),
>   wired into `build_lambda_config.json`.
> - **Enforcement — done.** `utils/check_string_scans.py` + `make check-string-scan`
>   (errors: `strchr(set, *p)` membership and `ctype(*char*)` UB; `--all` adds
>   soft run-loop / index-form warnings; `// STR_SCAN_LOCAL_OK:` suppresses).
> - **Phase 1 (markup) — done.** `asciidoc_adapter`, `rst_adapter` (incl. a real
>   *unguarded* `detectThematicBreak` over-read), `block_list`, `block_quote`,
>   `block_code`, `inline_special` converted; the markup tree is lint-clean.
> - **Phase 2 (CSS/Radiant) — done.** 42 `ctype(*char*)` UB sites in
>   `css_tokenizer`, `selector_matcher`, `render_svg_inline`, `layout_counters`,
>   `layout_block`, `cmd_layout` converted to `str_char_is_*` (the CSS tokenizer
>   `isalpha(ch)` swap also fixes a latent bug where high bytes could miss the
>   UTF-8 identifier branch). CSS+Radiant scope lint-clean; radiant layout
>   baseline 4245/4245.
> - **Phase 3 (serve/npm/url/log + remaining parsers) — done.** 44 `ctype(*char*)`
>   sites across 19 files (input-ics/pdf/toml/mdx/latex/graph, format-yaml/xml/
>   markup/utils, lib/log/url_parser/datetime/mime-detect, print) → `str_char_is_*`,
>   plus the `bash_builtins` `strchr(set, *p2)` → `str_char_in_set`. **The whole
>   tree is now `make check-string-scan`-clean (0 errors).** Verified: build clean,
>   and TOML/ICS/YAML/XML/PDF/DOT conversions are byte-identical to clean HEAD
>   (ASCII-equivalence confirmed empirically).
>
> Test-infra note: `make build-test`/the ASan gtests are broken on this branch
> (avl_tree ASan link error; CSS-tokenizer heap_get_span crash) — both pre-existing
> and unrelated. Validate via `lambda.exe`-driven conversions and `make layout
> suite=baseline`, not the ASan gtests.


Fifth-round scan of `lambda/`, `radiant/`, and `lib/` for string/character
scanning logic that should live in `./lib` instead of being open-coded in parser
loops. This proposal was triggered by the AsciiDoc header crash fixed in
`lambda/input/markup/format/asciidoc_adapter.cpp`: an empty next line reached
`strchr("=-~^+", *ul)`, and `strchr` matched the string literal's terminating
NUL. The following `while (*ul == ul_char)` loop then walked beyond the line.

The failure shape is small but reusable: **delimiter membership and run-count
loops must never let `'\0'` become a valid delimiter by accident**.

Two clarifications shape the rest of this proposal:

1. **This makes the pattern lintable; it does not retroactively make the
   codebase safe.** Both live call sites of the AsciiDoc shape are already
   guarded today — `asciidoc_adapter.cpp` and `rst_adapter.cpp` each `return`
   on an empty next line before reaching `strchr`. The goal here is to prevent
   *recurrence* across ~144 `strchr`, ~318 `while (*p == X)`, and ~211
   `while (*p && *p != X)` sites, not to close an open crash.
2. **NUL-terminated scanning is not the safe tier — bounded `(ptr, end)`
   scanning is.** Lambda already carries length everywhere (`String.len`,
   `StrView.length`), so trusting a NUL terminator is a *choice*, not a
   necessity. The bounded tier is therefore the default API in this proposal,
   and the NUL-terminated tier is the convenience exception (§3).

---

## 1. Survey summary

Existing `lib/str.h` already has the right foundation: length-bounded search,
`StrByteSet`, `str_find_any`, `str_find_not_any`, trim helpers, and safe numeric
parsing. The gap is a pointer-advancing scanner tier for the common parser
idioms that still appear inline.

Survey command classes used:

- `strchr(..., *p)` / `strchr(..., p[i])` membership checks in production code:
  **19 matches in 15 files**. Most are harmless string searches, but the risky
  class is delimiter-set membership on the current byte.
- Pointer-advancing `while` loops over strings in high-churn parser areas:
  **702 matches in 77 files** across markup, CSS, serve, radiant, npm, bash, and
  core `lib`.
- Simple skip/run loops such as `while (*p == ' ') p++` or
  `while (*p == marker) p++`: **333 matches in 60 files**.
- Delimiter scans such as `while (*p && *p != ']') p++` or bounded
  `while (p < end && *p != delim)`: **228 matches in 55 files**.

High-density areas:

| Area | Examples | Why centralize |
|------|----------|----------------|
| `lambda/input/markup/**` | format adapters, block parsers, inline parsers | Many delimiter sets, run counts, line-end scans; directly hit by AsciiDoc crash |
| `lambda/input/css/**` | `css_font_face`, tokenizer, selector matcher, `dom_element` | Repeated whitespace/comment/delimiter scans; multiple bounded and unbounded styles |
| `radiant/**` | `css_animation`, `render_svg_inline`, `render_clip`, `cmd_layout`, `surface` | CSS/SVG attribute mini-parsers repeat skip/scan code |
| `lambda/serve/**` | router, schema validator, OpenAPI, cookies, ASGI bridge | Small JSON/header/router scanners with duplicated quote and delimiter walking |
| `lambda/npm/semver.cpp`, `lib/url_parser.c`, `lib/log.c` | numeric and token scans | Smaller but central primitives still apply |

Excluded from sweep:

- third-party or generated code such as SQLite and tree-sitter scanners.
- domain parsers that already use explicit `(ptr, end)` state machines where a
  generic helper would obscure grammar state.

---

## 2. Two bug shapes, not one

This proposal targets two distinct latent-bug classes. The AsciiDoc crash is the
first; the second is more widespread and is a correctness/safety bug in its own
right, not a style issue.

### 2.1 C string membership includes NUL

Unsafe pattern:

```c
if (strchr(chars, *p)) {
    char marker = *p;
    while (*p == marker) p++;
}
```

If `*p == '\0'`, `strchr(chars, '\0')` returns the terminator of `chars`. The
loop then counts a run of NUL bytes and can leave the allocation. The fix at the
call site is simple, but the durable fix is to remove raw `strchr` membership
from parser code.

Desired pattern:

```c
if (str_char_in_set(*p, "=-~^+")) {
    size_t run = str_count_run(p, 0, *p);
    p += run;
}
```

`str_char_in_set('\0', ...)` must always return false, and `str_count_run`
must return 0 when the marker is `'\0'`.

### 2.2 Signed-char `ctype` is undefined behavior on UTF-8

Survey found **92 sites** of the form `isspace(*p)` / `isdigit(*p)` /
`isalnum(*p)` where `*p` is a plain (signed on most platforms) `char`, e.g.
`input-ics.cpp` date parsing and `print.cpp`. Passing a `char` directly to a
`<ctype.h>` function is **undefined behavior for any byte ≥ 0x80** — the standard
requires the argument to be representable as `unsigned char` or `EOF`. On UTF-8
input, bytes ≥ 0x80 are routine, so this is not a theoretical concern; it is a
real, high-frequency UB that happens to be benign on common libc implementations
only by luck.

This is a co-equal motivation with §2.1, not a lint nicety. The fix is the same
shape: route every classification through `str_char_is_*`, which casts to
`unsigned char` internally and applies fixed ASCII semantics (high-bit bytes
classify as false). Call sites stop deciding signedness on their own.

```c
if (str_char_is_digit(*p)) { ... }   // safe regardless of char signedness
```

---

## 3. Proposed `lib/str.h` scanner tier

Add these small C99 helpers to `lib/str.h` / `lib/str.c`; all are ASCII byte
scanners, not Unicode semantics.

### 3.1 NUL-safe character classes

```c
bool str_char_in_set(char c, const char* chars);
bool str_char_is_ascii_space(char c);      // ' ', '\t', '\n', '\r', '\f', '\v'
bool str_char_is_line_space(char c);       // ' ', '\t'
bool str_char_is_digit(char c);            // '0'..'9'
bool str_char_is_alpha(char c);            // a-z, A-Z
bool str_char_is_alnum(char c);            // digit or alpha
bool str_char_is_ident(char c);            // alnum or '_'
```

Rules:

- `str_char_in_set('\0', chars)` returns false.
- `chars == NULL` returns false.
- All `str_char_is_*` helpers cast internally or use range checks, so callers do
  not repeat unsafe `isspace(*p)` / `isdigit(*p)` patterns.

### 3.2 Bounded scanners — the default tier

These take an explicit `end` and **cannot over-read**, regardless of whether the
buffer is NUL-terminated. They are the primary API: new parser code, and any
parser that already carries an `end` (PDF streams, mmap'd input, network slices,
the CSS tokenizer, the Bash parser, SVG/Radiant mini-parsers), should reach for
these first.

```c
const char* strn_skip_chars(const char* p, const char* end, const char* chars);
const char* strn_skip_line_space(const char* p, const char* end);
const char* strn_skip_ascii_space(const char* p, const char* end);
const char* strn_skip_digits(const char* p, const char* end);
const char* strn_scan_until_char(const char* p, const char* end, char stop);
const char* strn_scan_until_any(const char* p, const char* end, const char* stops);
const char* strn_scan_to_line_end(const char* p, const char* end);
size_t strn_count_run(const char* p, const char* end, char marker);
```

Every helper clamps to `end`; none ever returns a pointer past `end`.

### 3.3 NUL-terminated scanners — convenience exception

Identical semantics, but trusting a NUL terminator instead of an `end`. Use
these **only** when the buffer is known-NUL-terminated and no length is available
(rare in this codebase, since `String` and `StrView` both carry length). They
remain a footgun on non-terminated buffers — `str_scan_until_char(p, stop)` still
returns a pointer the caller dereferences and still walks until it finds the
terminator. They are offered for ergonomics, not safety.

```c
const char* str_skip_chars(const char* p, const char* chars);
const char* str_skip_line_space(const char* p);
const char* str_skip_ascii_space(const char* p);
const char* str_skip_digits(const char* p);
const char* str_scan_until_char(const char* p, char stop);
const char* str_scan_until_any(const char* p, const char* stops);
const char* str_scan_to_line_end(const char* p);
size_t str_count_run(const char* p, size_t max_len, char marker);
```

`max_len == 0` for `str_count_run` means NUL-terminated mode; if `marker` is
`'\0'`, it returns 0. This directly prevents the AsciiDoc failure class.

### 3.4 Cursor — the recommended interface for new parsers

The pointer-returning helpers above re-expose a raw pointer at every call and so
re-invite the original mistake. The cursor is the only primitive that
*structurally* prevents over-read: it owns `end`, and `str_cursor_peek` returns
`'\0'` safely at the boundary, so call sites never branch on a raw dereference.
New parser code should prefer the cursor; the standalone helpers are a
transitional convenience for incremental migration.

```c
typedef struct {
    const char* p;
    const char* end;  // required; the cursor is always bounded
} StrCursor;

bool   str_cursor_at_end(const StrCursor* c);
char   str_cursor_peek(const StrCursor* c);   // returns '\0' at end — never over-reads
void   str_cursor_skip_line_space(StrCursor* c);
size_t str_cursor_count_run(StrCursor* c, char marker);
const char* str_cursor_mark(const StrCursor* c);
```

---

## 4. Migration plan

### Phase 0 — add primitives and tests

- Add helper APIs above to `lib/str.h` / `lib/str.c`.
- Add `test/lib/test_str_scan_gtest.cpp`:
  - `str_char_in_set('\0', "abc") == false`
  - `str_count_run("", 0, '\0') == 0`
  - `str_skip_line_space(" \tX")` lands on `X`
  - bounded helpers never advance past `end`
  - line-end scans stop on `\n`, `\r`, or NUL/end

### Phase 1 — markup parser safety sweep

Highest priority because it caused the segfault and has the densest scan count.

Target files:

- `lambda/input/markup/format/*.cpp`
- `lambda/input/markup/block/*.cpp`
- `lambda/input/markup/inline/*.cpp`
- `lambda/input/markup/markup_parser.cpp`
- `lambda/input/markup/markup_common.hpp`

Replace:

```c
while (*p == ' ' || *p == '\t') p++;
while (*p == marker) p++;
while (*p && *p != '\n' && *p != '\r') p++;
strchr(chars, *p)
isdigit(*p)
```

with:

```c
p = str_skip_line_space(p);
size_t n = str_count_run(p, 0, marker); p += n;
p = str_scan_to_line_end(p);
str_char_in_set(*p, chars)
str_char_is_digit(*p)
```

The markup adapters operate on per-line NUL-terminated `const char*`, so the
NUL-terminated tier (§3.3) is acceptable here; prefer the bounded tier (§3.2)
wherever a line `end`/length is already in scope. Fold the §2.2 ctype fix into
the same sweep — `input-ics.cpp`-style `isdigit(*ptr)` sites are in this class.

### Phase 2 — CSS and Radiant mini-parsers

Target high-density files:

- `lambda/input/css/css_font_face.cpp`
- `lambda/input/css/css_engine.cpp`
- `lambda/input/css/selector_matcher.cpp`
- `lambda/input/css/css_tokenizer.cpp`
- `radiant/css_animation.cpp`
- `radiant/render_svg_inline.cpp`
- `radiant/render_clip.cpp`
- `radiant/cmd_layout.cpp`
- `radiant/surface.cpp`

Use bounded helpers where an `end` pointer exists; avoid converting grammar
state loops that need custom nesting/quote logic unless the leaf scanner is
exactly equivalent.

### Phase 3 — serve/npm/url/log adoption

Target smaller but recurring patterns:

- `lambda/serve/schema_validator.cpp`
- `lambda/serve/router.cpp`
- `lambda/serve/openapi.cpp`
- `lambda/serve/asgi_bridge.cpp`
- `lambda/npm/semver.cpp`
- `lib/url_parser.c`
- `lib/log.c`

Focus on whitespace, digit, delimiter, and line-end primitives. Leave
format-specific state machines local.

---

## 5. Enforcement

Add a lightweight checker, e.g. `utils/check_string_scans.py`, exposed as
`make check-string-scan`.

Initial warnings:

| Pattern | Rule |
|---------|------|
| `strchr(<literal-or-set>, *p)` / `strchr(set, p[i])` | use `str_char_in_set` unless this is a true search in a NUL string |
| `while (*p == marker)` | require prior `marker != '\0'` guard or use `str_count_run` |
| `while (*p == ' ' || *p == '\t')` | use `str_skip_line_space` |
| `while (*p && *p != '\n' && *p != '\r')` | use `str_scan_to_line_end` |
| `isspace(*p)` / `isdigit(*p)` / `isalnum(*p)` on a `char*` | **UB for bytes ≥ 0x80** (§2.2) — use `str_char_is_*`, or cast to `unsigned char` with a local exemption |

Allow intentional local parsers with a searchable comment:

```c
// STR_SCAN_LOCAL_OK: nested quote/comment state machine; helper would obscure grammar.
```

The checker should ignore:

- `lib/str.c` itself.
- third-party/generated directories.
- tests initially, until production sweeps are done.

---

## 6. Acceptance criteria

This proposal is complete when:

- `lib/str.h` exposes the bounded scanner tier (§3.2), the cursor (§3.4), and the
  NUL-safe character classes (§3.1), with focused gtests.
- The AsciiDoc crash pattern is represented by a regression test.
- Markup adapter/parser code no longer uses raw `strchr(..., *p)` membership, and
  no longer passes `char*` derefs to `<ctype.h>` (§2.2).
- New parser code can fail `make check-string-scan` when it adds risky raw loops.
- Existing high-churn parser files either use the helpers or carry
  `STR_SCAN_LOCAL_OK` comments for truly grammar-specific loops.

The goal is not to make every loop disappear. The goal is to make the dangerous
leaf operations boring, centralized, and lintable.

---

## 7. On making the string type "opaque"

A natural follow-on question: can the string type be made opaque so that all
character looping is contained inside the `lib` string module? **Full opacity is
not achievable here; bounded containment is.** It helps to separate the two
"string types" involved:

- **Raw `const char*` parser inputs.** These come from files, mmap, and the
  network as byte buffers — the *buffer* cannot be made opaque, because its
  contents are external. What *can* be contained is traversal: funnel every walk
  through `StrCursor` (§3.4) so no parser code dereferences a raw pointer. That is
  the realistic reading of "opaque" — not an opaque type, an opaque **cursor**
  that owns bounds. It is achievable but large (~600+ scan sites); partial,
  incremental adoption is the expected outcome, with the lint preventing
  regressions.

- **The runtime `String` and `StrView`.** Both already carry length
  (`String{uint32_t len; uint8_t is_ascii; char chars[]}`, `StrView{str, length}`),
  and ~600 sites index `->chars[…]` directly. Accessor/iterator helpers
  (`string_byte_at`, a codepoint iterator) can *discourage* raw indexing, but
  `String` **cannot become truly opaque**: it is a flexible-array struct shared
  across the MIR JIT C ABI (`lambda.h`), and JIT-generated C indexes `chars[]`
  directly. Opacity cannot be enforced across that ABI boundary.

So the honest target is **containment, not opacity**: a bounded cursor as the
recommended interface for new parser code, accessor helpers for `String`/`StrView`,
and `make check-string-scan` catching raw loops. That is the same conclusion as
§6 — make the dangerous leaf operations boring, centralized, and lintable — now
stated explicitly for the type-design question.

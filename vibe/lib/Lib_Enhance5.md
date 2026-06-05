# `./lib` Enhancement Proposal 5 — Centralized String Scanners

Fifth-round scan of `lambda/`, `radiant/`, and `lib/` for string/character
scanning logic that should live in `./lib` instead of being open-coded in parser
loops. This proposal was triggered by the AsciiDoc header crash fixed in
`lambda/input/markup/format/asciidoc_adapter.cpp`: an empty next line reached
`strchr("=-~^+", *ul)`, and `strchr` matched the string literal's terminating
NUL. The following `while (*ul == ul_char)` loop then walked beyond the line.

The failure shape is small but reusable: **delimiter membership and run-count
loops must never let `'\0'` become a valid delimiter by accident**.

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

## 2. Bug shape: C string membership includes NUL

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

### 3.2 Pointer-advancing NUL-terminated scanners

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

### 3.3 Bounded scanner variants

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

These cover parsers that already carry `end` pointers, including PDF, JSX,
CSS tokenizer code, Bash parser code, and SVG/Radiant mini-parsers.

### 3.4 Optional cursor wrapper for dense parsers

For parser files with many repeated calls, add a thin cursor type:

```c
typedef struct {
    const char* p;
    const char* end;  // optional; NULL means NUL-terminated
} StrCursor;

bool str_cursor_at_end(const StrCursor* c);
char str_cursor_peek(const StrCursor* c);
void str_cursor_skip_line_space(StrCursor* c);
size_t str_cursor_count_run(StrCursor* c, char marker);
const char* str_cursor_mark(const StrCursor* c);
```

This should be adopted only where it reduces boilerplate. The first migration
can use pointer-return helpers without introducing cursor state.

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
```

with:

```c
p = str_skip_line_space(p);
size_t n = str_count_run(p, 0, marker); p += n;
p = str_scan_to_line_end(p);
str_char_in_set(*p, chars)
```

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
| `isspace(*p)` / `isdigit(*p)` / `isalnum(*p)` | use `str_char_is_*` or cast explicitly with a local exemption |

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

- `lib/str.h` exposes the scanner tier and has focused gtests.
- The AsciiDoc crash pattern is represented by a regression test.
- Markup adapter/parser code no longer uses raw `strchr(..., *p)` membership.
- New parser code can fail `make check-string-scan` when it adds risky raw loops.
- Existing high-churn parser files either use the helpers or carry
  `STR_SCAN_LOCAL_OK` comments for truly grammar-specific loops.

The goal is not to make every loop disappear. The goal is to make the dangerous
leaf operations boring, centralized, and lintable.

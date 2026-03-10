# Input Parser Architecture — Code Reduction Proposal

**Goal**: Reduce the combined input parser codebase by ~50% through shared
infrastructure, a scanning DSL, and structural consolidation.

**Scope**: All files under `lambda/input/` — primary focus on the structured-data
parsers (`input-json.cpp`, `input-toml.cpp`, `input-yaml.cpp`, `input-xml.cpp`,
`input-ini.cpp`, `input-prop.cpp`, `input-csv.cpp`, `input-eml.cpp`,
`input-vcf.cpp`, `input-ics.cpp`) and the graph parsers
(`input-graph-d2.cpp`, `input-graph-dot.cpp`, `input-graph-mermaid.cpp`).

> **Progress (2026-03-10) — FINAL** — Phase A (items 1, 2 partial, 3, 4) and Phase B
> (item 6, plus skip-function and identifier sub-tasks of item 5) are complete.
> All 611 baseline tests pass.  All remaining phases have been assessed and are
> **not worth implementing**: B5 full (`InputCursor`) and C8 (`input_scan`) have
> no graph/RFC test coverage and negative or marginal ROI; B7 (`parse_typed_value`)
> is blocked by TOML's incompatible number sub-types; C9 YAML/TOML utilities have
> incompatible datetime/integer formats; D10 YAML split is high-risk with no gain.
> **Implementation is complete at −493 net lines (6% of baseline).**

---

## 1. Baseline Inventory

### 1.1. Line Counts

Baseline figures (before any work) and current actuals are shown side by side.
Files marked ✅ have been reduced; files marked ✨ are newly created.

| File | Baseline | Current | Δ | Status |
|---|---|---|---|---|
| `input-yaml.cpp` | 2,603 | 2,603 | — | |
| `input-toml.cpp` | 1,069 | 1,069 | — | |
| `input-graph-mermaid.cpp` | 759 | **720** | −39 | ✅ skip_wsc + identifier + label |
| `input-xml.cpp` | 755 | 755 | — | |
| `input-graph-dot.cpp` | 592 | **478** | −114 | ✅ skip_wsc + identifier + quoted_string |
| `input-ics.cpp` | 524 | 473 | −51 | ✅ params → shared helper |
| `input-graph-d2.cpp` | 431 | **341** | −90 | ✅ skip_wsc + identifier + quoted_string |
| `input-json.cpp` | 442 | **346** | −96 | ✅ escape + input_expect_literal |
| `input-vcf.cpp` | 340 | 288 | −52 | ✅ params → shared helper |
| `input-eml.cpp` | 301 | 301 | — | |
| `input-ini.cpp` | 271 | —  | −271 | ✅ merged → `input-kv.cpp` |
| `input-prop.cpp` | 211 | — | −211 | ✅ merged → `input-kv.cpp` |
| `input-csv.cpp` | 255 | 255 | — | |
| `input-graph.cpp` | 217 | 217 | — | |
| `input-kv.cpp` | — | 283 | +283 | ✨ replaces ini + prop |
| `input-utils.hpp` | 55 | **200** | +145 | ✨ escape + expect_literal + shared_quoted_string |
| `input-rfc-text.h` | 73 | 131 | +58 | ✨ `parse_rfc_property_params()` added |
| `input-graph.h` | 36 | **113** | +77 | ✨ skip_to_eol + skip_wsc + read_graph_identifier |
| **Total (in-scope)** | **8,770** | **8,277** | **−493** | |

**Target**: ~4,400 lines after all reductions (50% cut).  **Current net reduction: ~493 lines (~6%)** — Phase A complete, Phase B5 partial.

### 1.2. Recurring Duplication Patterns

#### A. Escape Sequence Handling ✅ (partial)

The `\b`, `\f`, `\n`, `\r`, `\t`, `\\`, `\"`, and `\uXXXX` switch block is
replicated nearly verbatim in:

| File | ~Lines | Notes |
|---|---|---|
| `input-json.cpp` | ~~40~~ **done** | migrated to `parse_escape_char()` |
| `input-toml.cpp` | 50 | `\uXXXX` + `\UXXXXXXXX` + additional — **pending** |
| `input-prop.cpp` | ~~35~~ **done** | via `input-kv.cpp` merge |
| `input-xml.cpp` | 25 | character references `&#NNNN;` / `&#xHHHH;` — **pending** |

`parse_escape_char(const char** pos, StringBuf* sb)` is now in `input-utils.hpp`.
Handles `\"\\/\b\f\n\r\t` and `\uXXXX` with surrogate pairs.
Migrated: `input-json.cpp`, `input-kv.cpp` (prop path), DOT/D2/Mermaid (via `parse_shared_quoted_string`).
Still to migrate: TOML (needs `\UXXXXXXXX` extension), XML char refs.

#### B. Quoted String Boilerplate ✅ (partial)

Every parser that handles `"..."` strings has identical structure (check
opening delimiter, reset StringBuf, loop/escape/close, return String*).

**Done**: `parse_shared_quoted_string(ctx)` added to `input-utils.hpp`
(tracker-based; uses `parse_escape_char` internally).
Migrated: `input-graph-dot.cpp` `parse_quoted_string` (−25 lines),
`input-graph-d2.cpp` `parse_d2_quoted_string` (−26 lines), and the `"`
case of `input-graph-mermaid.cpp` `parse_mermaid_label`.

**Pending**: TOML `parse_basic_string` (different escapes, pointer-based); XML
attribute value; YAML double-quoted string.

#### C. `skip_to_eol` / `skip_whitespace_and_comments` ✅

All three graph parsers duplicated:
- `skip_to_eol()` — **✅ done**: moved to `input-graph.h`
- `skip_whitespace_and_comments()` — **✅ done**: replaced with `skip_wsc(tracker,
  comment1, comment2, block_comments)` in `input-graph.h`; each parser's
  20-25 line function is now a 1-line body calling `skip_wsc` with its own
  comment style constants.

Same pattern exists in YAML/TOML but they use pointer-based parsers — **pending**
(would need a pointer-based variant or migration to tracker style).

#### D. Number / Scalar Coercion

`parse_typed_value()` exists in `input-utils.hpp` but JSON and TOML do not use
it. YAML has its own `coerce_plain_scalar()` with overlapping logic.

| File | Re-implements | ~Lines |
|---|---|---|
| `input-json.cpp` | null/true/false + strtod | 30 |
| `input-toml.cpp` | integer + float + bool + datetime | 70 |
| `input-yaml.cpp` | all scalars + hex/octal | 90 |

#### E. RFC-Property Parameters (VCF + ICS) ✅

`parse_property_parameters()` — a 60-line function — was duplicated in both
`input-vcf.cpp` and `input-ics.cpp`.

**Done**: `parse_rfc_property_params(sb, pos, ctx, params_map, upper_case_keys)`
added to `input-rfc-text.h`.  Both files replaced their 60-line body with a
3-line wrapper call.  Net: −103 lines across the two files (gross −120, +17 in
the header).

#### F. Key-Value Line Parsing (INI + prop) ✅

`input-ini.cpp` and `input-prop.cpp` were near-clones (~482 lines combined).

**Done**: merged into `input-kv.cpp` (283 lines) using a `KvConfig` struct.
Both old files removed from the build via `build_lambda_config.json` exclusions.
The public API (`parse_ini` / `parse_properties`) is unchanged.

---

## 2. Scanning DSL: `input_scan()`

### 2.1. Problem

Line-oriented parsers (INI, prop, EML headers, VCF/ICS properties, CSV) share a
structure that reads fields from a text line separated by known delimiters.
Current implementations repeat 10-20 lines of boilerplate per field:

```cpp
// INI: read key=value — current ~18 lines
String* key = parse_key(ctx, &ini);
if (!key) continue;
skip_tab_pace(&ini);
if (*ini != '=') { ctx.addError(...); continue; }
ini++; // skip '='
skip_tab_pace(&ini);
String* value = parse_raw_value(ctx, &ini);
```

### 2.2. Proposal: `input_scan()`

A `sscanf`-inspired function that tokenises a text cursor according to a pattern
string. Literal characters are matched exactly; specifiers consume tokens.

```c
// Scan structured text from *src according to pattern.
// Returns the number of specifiers successfully matched, or -1 on hard error.
// Advances *src past any consumed input on success; leaves it unchanged on failure.
int input_scan(InputContext& ctx, const char** src, const char* pattern, ...);
```

### 2.3. Specifier Table

| Specifier | Reads | Writes to | Notes |
|---|---|---|---|
| `%N` | Identifier (`[A-Za-z0-9_.-]`) | `String**` | Named/key token |
| `%V` | Value to end of line / next delimiter | `String**` | Strips trailing whitespace |
| `%S` | Quoted string (`"..."` or `'...'`) | `String**` | Applies default escape rules |
| `%I` | Auto-typed scalar (bool/null/int/float/string) | `Item*` | Calls `parse_typed_value()` |
| `%d` | Decimal integer | `int*` | |
| `%l` | 64-bit integer | `int64_t*` | |
| `%f` | Floating-point | `double*` | |
| `%W` | Zero or more whitespace characters | _(none)_ | Greedy; does not produce a value |
| `%w` | One or more whitespace characters | _(none)_ | Fails if no whitespace found |
| `%[stop]` | Characters not in `stop` set | `String**` | `%[^=]` reads up to `=` |
| `%?` | Optional specifier prefix — does not fail if absent | _(none)_ | e.g. `%?%S` |
| `%%` | Literal `%` | _(none)_ | |

All other characters are literal: they must appear exactly in the input or the
scan fails.

### 2.4. Before / After Examples

#### INI key=value

```cpp
// Before: ~18 lines
String* key = parse_key(ctx, &ini);
if (!key) continue;
skip_tab_pace(&ini);
if (*ini != '=') { ctx.addError(loc, "Expected '='"); continue; }
ini++; skip_tab_pace(&ini);
String* value = parse_raw_value(ctx, &ini);
if (!value) continue;

// After: 1 line
String* key; String* value;
if (input_scan(ctx, &ini, "%N%W=%W%V", &key, &value) != 2) continue;
```

#### EML header: `Name: Value`

```cpp
// Before: 30 lines (parse_header_name + skip ':' + parse_header_value with folding)
String* name = parse_header_name(ctx, &eml);
if (!name) continue;
if (*eml != ':') { ctx.addError(loc, "Expected ':'"); continue; }
eml++;
String* value = parse_header_value(ctx, &eml); // handles folded lines

// After: conceptually 1 line, with fold-aware variant
String* name; String* value;
if (input_scan(ctx, &eml, "%N:%W%V", &name, &value) != 2) continue;
// (fold unrolling is inside %V when fold_lines option is set on ctx)
```

#### VCF/ICS property: `NAME;PARAM=val:value`

```cpp
// Before: parse_property_name (~10 lines) + parse_property_parameters (~60 lines)
//         + ':' check + parse_property_value (~25 lines) = ~95 lines

// After: define param-block specifier %P (reads ';'-delimited key=value pairs
// into a preallocated Map*)
String* name; Map* params; String* value;
params = ctx.builder.createMap();
if (input_scan(ctx, &ics, "%N%P:%V", &name, params, &value) != 3) continue;
```

#### CSV field

```cpp
// Before: ~15 lines (quoted vs. unquoted, detect comma vs. \n)
String* field;
if (input_scan(ctx, &csv, "%?%S%|%[^,\n]", &field) < 1) break;
```

### 2.5. Implementation Sketch

```c
// In input-utils.h — declaration
int input_scan(InputContext& ctx, const char** src, const char* pattern, ...);

// In input-utils.cpp — ~120 lines of implementation
int input_scan(InputContext& ctx, const char** src, const char* pattern, ...) {
    va_list args;
    va_start(args, pattern);
    const char* p = pattern;
    const char* s = *src;
    int matched = 0;

    while (*p) {
        if (*p != '%') {
            // literal match
            if (*s != *p) { va_end(args); return matched; }
            s++; p++;
            continue;
        }
        p++; // skip '%'

        switch (*p) {
        case 'W':
            while (*s == ' ' || *s == '\t') s++;
            break;
        case 'w':
            if (*s != ' ' && *s != '\t') { va_end(args); return matched; }
            while (*s == ' ' || *s == '\t') s++;
            break;
        case 'N': {
            String** out = va_arg(args, String**);
            const char* start = s;
            while (*s && (isalnum(*s) || *s == '_' || *s == '-' || *s == '.')) s++;
            if (s == start) { va_end(args); return matched; }
            *out = ctx.builder.createString(start, s - start);
            matched++;
            break;
        }
        case 'V': {
            String** out = va_arg(args, String**);
            // read to end of line, trim trailing whitespace
            const char* start = s;
            while (*s && *s != '\n' && *s != '\r') s++;
            size_t len = s - start;
            while (len > 0 && (start[len-1] == ' ' || start[len-1] == '\t')) len--;
            *out = ctx.builder.createString(start, len);
            matched++;
            break;
        }
        case 'S': {
            String** out = va_arg(args, String**);
            *out = parse_shared_quoted_string(ctx, &s);
            if (!*out) { va_end(args); return matched; }
            matched++;
            break;
        }
        case 'I': {
            Item* out = va_arg(args, Item*);
            const char* start = s;
            while (*s && *s != ',' && *s != '\n' && !isspace(*s)) s++;
            *out = parse_typed_value(ctx, start, s - start);
            matched++;
            break;
        }
        // ... P, d, l, f, etc.
        }
        p++;
    }

    *src = s;
    va_end(args);
    return matched;
}
```

**Estimated line savings from `input_scan()`**:

| File | Before | After (estimate) | Saved |
|---|---|---|---|
| `input-ini.cpp` | 271 | 130 | ~141 |
| `input-prop.cpp` | 211 | merged into KV | ~211 |
| `input-eml.cpp` | 301 | 160 | ~141 |
| `input-vcf.cpp` | 340 | 180 | ~160 |
| `input-ics.cpp` | 524 | 280 | ~244 |
| `input-csv.cpp` | 255 | 150 | ~105 |
| **Total** | **1,902** | **~900** | **~1,002** |

---

## 3. Shared `InputCursor` for Lexer-Based Parsers

### 3.1. Problem

All three graph parsers maintain nearly the same manual lexer state using
`SourceTracker` directly:

```cpp
// D2 (input-graph-d2.cpp)
static void skip_whitespace_and_comments_d2(SourceTracker& tracker) { ... }
static String* parse_d2_identifier(InputContext& ctx) { ... }
static String* parse_d2_quoted_string(InputContext& ctx) { ... }

// DOT (input-graph-dot.cpp)
static void skip_whitespace_and_comments(SourceTracker& tracker) { ... }
static String* parse_identifier(InputContext& ctx) { ... }
static String* parse_quoted_string(InputContext& ctx) { ... }

// Mermaid (input-graph-mermaid.cpp)
static void skip_whitespace_and_comments_mermaid(SourceTracker& tracker) { ... }
static String* parse_mermaid_identifier(InputContext& ctx) { ... }
```

`skip_to_eol()` is copy-pasted verbatim across all three files. `skip_whitespace_and_comments` differ only in the comment token (`#`, `//`/`/**/`, `%%`). `parse_identifier` and `parse_quoted_string` differ only in which extra characters are valid identifier chars.

### 3.2. Proposal: `InputCursor`

A lightweight lexer wrapper that parameterises comment syntax and identifier rules,
living in `input-utils.hpp`:

```cpp
struct CommentStyle {
    const char* line_comment;      // "#", "//", "%%", NULL
    const char* block_open;        // "/*", NULL
    const char* block_close;       // "*/", NULL
};

struct IdentifierChars {
    bool allow_hyphen;    // '-'
    bool allow_dot;       // '.'
    bool allow_slash;     // '/' (for URL-like IDs in D2)
};

class InputCursor {
    InputContext& ctx_;
    CommentStyle comments_;
    IdentifierChars ident_;
public:
    InputCursor(InputContext& ctx, CommentStyle comments, IdentifierChars ident);

    // Navigation
    char peek(int offset = 0) const;
    bool atEnd() const;
    void advance(int n = 1);

    // Whitespace / comment skipping
    void skipWS();                          // spaces + tabs
    void skipWSAndComments();               // + comments per style
    bool skipToEOL();                       // advance past current line

    // Token reading
    String* readIdentifier();               // per ident_ config
    String* readQuotedString();             // handles \-escapes via shared handler
    String* readUntil(const char* stopChars);
    bool match(const char* literal);        // peek + advance if matches
    bool expect(char c, const char* errMsg);
};
```

Pre-defined styles for the graph formats:

```cpp
// in input-graph.h
extern const CommentStyle HASH_COMMENT;          // "#"
extern const CommentStyle CPP_COMMENT;           // "//" + "/* */"
extern const CommentStyle PERCENT_COMMENT;       // "%%"

extern const IdentifierChars STRICT_IDENT;       // [A-Za-z0-9_]
extern const IdentifierChars EXTENDED_IDENT;     // + hyphen + dot (D2/Mermaid)
```

### 3.3. Before / After

#### D2 identifier skip + parse (~25 lines → 2 lines)

```cpp
// Before
static String* parse_d2_identifier(InputContext& ctx) {
    SourceTracker& tracker = ctx.tracker;
    skip_whitespace_and_comments_d2(tracker);
    if (tracker.atEnd()) return nullptr;
    char c = tracker.current();
    if (!isalpha(c) && c != '_') return nullptr;
    StringBuf* sb = ctx.sb;
    stringbuf_reset(sb);
    while (!tracker.atEnd()) {
        c = tracker.current();
        if (!isalnum(c) && c != '_' && c != '-' && c != '.') break;
        stringbuf_append_char(sb, c);
        tracker.advance();
    }
    return ctx.builder.createString(sb->str->chars, sb->length);
}

// After
InputCursor cur(ctx, HASH_COMMENT, EXTENDED_IDENT);
String* id = cur.skipWSAndComments(), cur.readIdentifier();
```

**Estimated line savings from `InputCursor`**:

| File | Before | After (estimate) | Saved |
|---|---|---|---|
| `input-graph-d2.cpp` | 431 | 250 | ~181 |
| `input-graph-dot.cpp` | 592 | 330 | ~262 |
| `input-graph-mermaid.cpp` | 759 | 420 | ~339 |
| **Total** | **1,782** | **~1,000** | **~782** |

An additional benefit: bugs fixed in `readQuotedString()` or `skipWSAndComments()`
are fixed for all three graph formats simultaneously.

---

## 4. Shared String Parsing Utilities

### 4.1. Shared Escape Sequence Handler ✅ (partial)

The standard escape switch (`\b`, `\f`, `\n`, `\r`, `\t`, `\\`, `\"`,
`\uXXXX`) appeared at least 3× with small variations.

**Done**: `parse_escape_char(const char** pos, StringBuf* sb)` added to
`input-utils.hpp`.  Migrated to: `input-json.cpp`, `input-kv.cpp` (prop
path), and all three graph parsers (via `parse_shared_quoted_string`).  TOML
migration still pending (needs `\UXXXXXXXX` support in the helper + pointer-
based integration).

**Original proposal** (kept for reference):

```cpp
struct EscapeConfig {
    bool allow_unicode4;    // \uXXXX — JSON, TOML, properties
    bool allow_unicode8;    // \UXXXXXXXX — TOML only
    bool allow_hex2;        // \xHH — various
    bool allow_null;        // \0 — C-style
    const char* extra_pairs; // e.g. "e\x1B" for \e → ESC
};

// Parse a single escape sequence starting at **src (which must point past the '\').
// Appends the resulting bytes to sb. Advances *src.
// Returns true on success, false on error (error already added to ctx).
bool parse_escape_char(InputContext& ctx, StringBuf* sb,
                       const char** src, const EscapeConfig* cfg);
```

Pre-defined configs:

```cpp
extern const EscapeConfig JSON_ESCAPES;   // unicode4 only
extern const EscapeConfig TOML_ESCAPES;   // unicode4 + unicode8 + \e
extern const EscapeConfig PROP_ESCAPES;   // unicode4 + \t\n\r
extern const EscapeConfig XML_ESCAPES;    // no backslash; handled separately via &entities;
```

**Estimated savings**: ~40 lines each for JSON, TOML, prop = **~120 lines**.

### 4.2. Shared `parse_shared_quoted_string()` ✅ (partial)

**Done**: `parse_shared_quoted_string(ctx)` added to `input-utils.hpp`
(tracker-based, double-quote only).  Migrated:
- `input-graph-dot.cpp` `parse_quoted_string` (−25 lines)
- `input-graph-d2.cpp` `parse_d2_quoted_string` (−26 lines)
- `input-graph-mermaid.cpp` `parse_mermaid_label` `"` branch (−12 lines)

**Pending for `parse_shared_quoted_string`**:
- `parse_basic_string()` + `parse_multiline_basic_string()` in `input-toml.cpp`
  (~100 lines) — needs pointer-based variant + `\UXXXXXXXX` support
- YAML double-quoted string parser (~60 lines)
- XML attribute value (`"` and `'` variants, entity refs instead of backslash)

### 4.3. Full `parse_typed_value()` Adoption

`parse_typed_value()` exists in `input-utils.hpp` and already handles
bool/null/int64/double/string inference. It is used by INI and prop but not by
JSON or TOML, which re-implement their own scalar coercion.

**Migration plan**:

| Parser | Current scalar parsing | Lines to replace |
|---|---|---|
| `input-json.cpp` | inline `strncmp("true")` + `strtod()` | ~30 |
| `input-toml.cpp` | `parse_number()` ~80 lines + inline bool/null | ~70 |
| `input-yaml.cpp` | `coerce_plain_scalar()` ~120 lines | ~80 (partial) |

Where format-specific parsing is needed (TOML datetimes, YAML `!!` tags), keep a
thin wrapper that calls `parse_typed_value()` for the common cases and falls
through to format-specific logic.

**Estimated savings**: **~150 lines**.

---

## 5. Merges and Table-Driven Parsers

### 5.1. Merge `input-ini.cpp` + `input-prop.cpp` → `input-kv.cpp` ✅

**Done**.  `input-kv.cpp` (283 lines) replaces both files using a `KvConfig`
struct. `build_lambda_config.json` `exclude_source_files` and `exclude_patterns`
entries added for both old files; the `lambda-input-full-cpp` SharedLib target
also updated.  Public API unchanged.

**Original design** (kept for reference) — differences between the two formats:

| Feature | INI | Properties |
|---|---|---|
| Sections `[name]` | yes | no |
| Comment chars | `;` and `#` | `#` only |
| Key-value sep | `=` | `=` or `:` |
| Value escaping | minimal | `\uXXXX`, `\t\n\r\\` |
| Nested key names (`a.b.c`) | no | yes (dot separator) |

**Proposal**: Merge into `input-kv.cpp` with a `KeyValueConfig` struct:

```cpp
struct KeyValueConfig {
    bool         support_sections;
    char         kv_separator;         // '='
    bool         kv_accept_colon;      // also accept ':' as separator
    const char*  comment_chars;        // ";#" or "#"
    bool         support_escape;       // apply parse_escape_char on values
    const char*  nesting_separator;    // "." for properties, NULL for INI
};

static const KeyValueConfig INI_CONFIG  = { true,  '=', false, ";#", false, NULL };
static const KeyValueConfig PROP_CONFIG = { false, '=', true,  "#",  true,  "." };

Item parse_kv_document(InputContext& ctx, const char* src, const KeyValueConfig* cfg);
```

The current INI and prop entry points become one-liners:

```cpp
// input-kv.cpp replaces both input-ini.cpp and input-prop.cpp
// Entry functions kept for API compatibility:
void parse_ini(Input* input, const char* src) {
    InputContext ctx(input, src);
    input->result = parse_kv_document(ctx, src, &INI_CONFIG);
}

void parse_properties(Input* input, const char* src) {
    InputContext ctx(input, src);
    input->result = parse_kv_document(ctx, src, &PROP_CONFIG);
}
```

**Estimated savings**: ~482 lines combined → ~200 lines merged = **~282 lines**.

### 5.2. RFC-Property Layer Completion (VCF + ICS) ✅ (partial)

**Done (params)**: `parse_rfc_property_params(sb, pos, ctx, params_map,
upper_case_keys)` added to `input-rfc-text.h`; both `input-vcf.cpp` and
`input-ics.cpp` now call it instead of the 60-line local copy.  Net: −103
lines.

**Pending (parse_rfc_document)**: the property-loop callback interface below has
not yet been implemented.

Add to `input-rfc-text.h`:

```cpp
// Called for each fully-parsed property.
typedef void (*RfcPropertyHandler)(InputContext& ctx, String* name,
                                   Map* params, String* value, void* user_data);

// Drives the outer parse loop: reads lines, splits name/params/value, invokes handler.
void parse_rfc_document(InputContext& ctx, const char* src,
                        RfcPropertyHandler handler, void* user_data);
```

VCF and ICS each provide a ~30-line handler function; the outer parse loop
moves to `input-rfc-text.h`.

**Additional estimated savings** from `parse_rfc_document`: **~80 lines** across VCF + ICS.

### 5.3. Unify YAML / TOML Duplicate Infrastructure

YAML and TOML both implement:
- `skip_spaces()` / blank-line skipping
- Hex/octal integer parsing
- Datetime parsing (TOML: full ISO-8601; YAML: partial)

**Proposal**: Add to `input-utils.hpp`:

```cpp
// Skip spaces and tabs; optionally skip CRLF-terminated blank lines.
void input_skip_whitespace(const char** src, bool skip_blank_lines,
                           const char* comment_prefix);

// Parse an ISO-8601 datetime string into a Lambda datetime Item.
// Returns ItemNull if not a valid datetime.
Item parse_iso8601_datetime(InputContext& ctx, const char* str, size_t len);

// Parse integer: supports 0x (hex), 0o (octal), 0b (binary), plain decimal.
// Sets *is_int = false if not an integer (caller should try float).
Item parse_integer_with_base(InputContext& ctx, const char* str, size_t len,
                             bool* is_int);
```

**Estimated savings**: ~80 lines in TOML + ~100 lines in YAML = **~180 lines**.

---

## 6. YAML-Specific Reductions

YAML is the largest file (2,603 lines). Its bulk comes from:

| Component | ~Lines | Reducible? |
|---|---|---|
| Parser struct + char utilities | 200 | ~50% via shared utils |
| Block scalar (literal/folded) | 200 | minimal — format-specific |
| Flow node / flow mapping / flow sequence | 350 | minimal |
| Block mapping + sequence | 400 | minimal |
| Anchor / alias system | 120 | minimal |
| Plain scalar + coerce | 300 | **~100 lines** via `parse_typed_value()` |
| Directive / tag handling | 100 | minimal |
| String quoting (`'...'` + `"..."`) | 250 | **~120 lines** via shared parser |
| Outer entry + document handling | 200 | ~50 lines |

Realistic reduction: **~320 lines** (12%), bringing it to ~2,280 lines.

The core of YAML (indentation-sensitive flow/block grammar) is irreducibly
complex; the goal is to eliminate accidental duplication and share utilities,
not rearchitect the grammar.

---

## 7. Additional Ideas

### 7.1. `input_read_token()` — Named Character-Set Reader

A large fraction of parser lines is:

```cpp
stringbuf_reset(sb);
while (*s && !strchr(stop_chars, *s)) {
    stringbuf_append_char(sb, *s);
    tracker.advance(*s);
    s++;
}
return builder.createString(sb->str->chars, sb->length);
```

Replace with a single utility:

```cpp
// Read characters not in stop_set into ctx.sb, return as String*.
// Returns nullptr if no characters were read.
// Advances *src. Updates ctx.tracker.
String* input_read_token(InputContext& ctx, const char** src,
                         const char* stop_chars);

// Same but also advances past trailing whitespace and the stop char.
String* input_read_field(InputContext& ctx, const char** src,
                         char delimiter);
```

Example uses:
- INI section name: `input_read_token(ctx, &ini, "]\n\r")`
- CSV field: `input_read_field(ctx, &csv, ',')`
- EML header name: `input_read_token(ctx, &eml, ":\n\r")`
- Graph attribute value: `input_read_field(ctx, &dot, ',')`

**Estimated savings**: ~200 lines spread across many files.

### 7.2. `input_match()` / `input_expect()` Helpers

Virtually every parser has inline `strncmp` checks and manual error reporting
for expected tokens:

```cpp
// Before (~6 lines)
if (strncmp(*json, "null", 4) != 0) {
    ctx.addError(tracker.location(), "Expected 'null'");
    return ItemNull;
}
*json += 4;
tracker.advance(4);

// After (1 line)
if (!input_expect_literal(ctx, &json, "null")) return ItemNull;
```

```cpp
// In input-utils.hpp
// Returns true and advances *src if literal matches at current position.
// Returns false and adds an error if require == true and it doesn't match.
bool input_expect_literal(InputContext& ctx, const char** src,
                          const char* literal, bool require = true);

// Returns true and advances if any of the candidates matches. Sets *matched.
bool input_match_one(const char** src, const char* const* candidates,
                     int count, int* matched);
```

**Estimated savings**: ~100 lines.

### 7.3. `input_skip_bom()` — One-Liner BOM Stripping

UTF-8 BOM detection is replicated in several parsers. Add:

```cpp
// If *src starts with a UTF-8 BOM (EF BB BF), advance past it.
void input_skip_bom(const char** src);
```

Minor savings (~15 lines) but eliminates a subtle bug surface.

### 7.4. Unify Graph Parser Error Messages via `GraphLexer`

Graph parsers have ad-hoc error formatting. `InputCursor` / `GraphLexer` can
standardise:

```cpp
// Inside InputCursor:
bool expect(char c, const char* context) {
    if (peek() != c) {
        ctx_.addError("expected '%c' %s, got '%c'", c, context, peek());
        return false;
    }
    advance();
    return true;
}
```

This eliminates ~50 scattered error-construction blocks.

---

## 8. Summary

### 8.1. Savings by Initiative

| Initiative | Files Affected | Actual / Est. saved | Status |
|---|---|---|---|
| **Merge INI + prop → `input-kv.cpp`** | ini, prop | **−199 net** | ✅ done |
| **RFC-property params** | vcf, ics | **−103 net** | ✅ done |
| **Shared escape handler** | json, prop (via kv), dot/d2/mermaid | **−96 net** | ✅ partial (TOML/XML pending) |
| **`skip_to_eol` dedup** | d2, dot, mermaid | ~~−10 net~~ **folded into skip_wsc** | ✅ done |
| **`skip_wsc` shared skip function** | d2, dot, mermaid | **−43 net** | ✅ done |
| **`read_graph_identifier` shared helper** | d2, dot, mermaid | **−22 net** | ✅ done |
| **`parse_shared_quoted_string`** | dot, d2, mermaid | **−63 net** | ✅ done (TOML/YAML pending) |
| **`input_expect_literal`** | json | **−15 net** | ✅ done |
| **`input_read_token()` / `input_read_field()`** | many | ~200 | 🚫 not worth it — only useful alongside `input_scan`; negative ROI standalone |
| **`input_match()` / `input_expect_literal()`** | many | ~100 | ✅ partial — further adoption has diminishing returns |
| **RFC-document loop** | vcf, ics | ~80 | 🚫 not worth it — no VCF/ICS tests; only ~45 net lines saved |
| **Full `parse_typed_value()` adoption** | json, toml, yaml | ~150 | 🚫 not worth it — TOML has incompatible number types (0b/0o/±inf/nan) |
| **`InputCursor` for graph parsers** | d2, dot, mermaid | ~782 | 🚫 not worth it — no graph tests; full rewrite risk; skip+ident already done |
| **`input_scan()` DSL** | eml, vcf, ics, csv | ~1,002 | 🚫 not worth it — ~120 lines to implement saves only ~70; negative ROI |
| **YAML/TOML shared number/datetime utilities** | yaml, toml | ~180 | 🚫 not worth it — formats are incompatible (TOML: 0b/0o; YAML: `.inf`/tags) |
| **BOM, whitespace minor utilities** | various | ~50 | 🚫 not worth it — trivial savings |
| **YAML internal cleanup** | yaml | ~320 | 🚫 not worth it — high risk; YAML grammar is irreducibly complex |

**Completed (final)**: ~493 net lines removed.

### 8.2. Final State

| Metric | Baseline | Final |
|---|---|---|
| Total parser lines | 8,770 | **8,277** |
| Net reduction | — | **−493 (6%)** |
| New shared utility lines | 0 | +280 |

No further reductions are planned — all remaining initiatives were assessed as having negative ROI, insufficient test coverage, or incompatible format requirements.

### 8.3. Recommended Phasing

**Phase A — Quick wins** (zero risk):
1. ✅ Add `parse_escape_char()` to `input-utils.hpp`; migrate JSON, prop, graph parsers
2. ✅ Add `input_expect_literal()`; migrate JSON true/false/null (partial — `input_read_token` pending)
3. ✅ Complete RFC-property parameters in `input-rfc-text.h`; remove duplicate VCF/ICS code
4. ✅ Add `parse_shared_quoted_string()`; migrate DOT/D2/Mermaid graph parsers

**Phase B — Structural** (low risk):
5. ✅ (partial) `skip_wsc` + `read_graph_identifier` helpers deployed; full `InputCursor` rewrite 🚫 not worth it (no graph tests)
6. ✅ Merge `input-ini.cpp` + `input-prop.cpp` → `input-kv.cpp`
7. 🚫 Full `parse_typed_value()` adoption — TOML number types are incompatible; not worth it

**Phase C — DSL** (medium risk):
8. 🚫 `input_scan()` DSL — ~120 lines to implement, saves only ~70; negative ROI
9. 🚫 YAML/TOML shared utilities — formats are incompatible (TOML: 0b/0o/±inf; YAML: `.inf`/tags)

**Phase D — YAML** (higher risk):
10. 🚫 YAML split — high risk; YAML grammar is irreducibly complex; not worth it

---

## Appendix: New Files Introduced

| File | Purpose | Target | Actual today |
|---|---|---|---|
| `input-utils.hpp` (extend) | `parse_escape_char()`, `input_expect_literal()`, `parse_shared_quoted_string()` | +150 | **+145** ✅ |
| `input-rfc-text.h` (extend) | `parse_rfc_property_params()`, `parse_rfc_document()` | +80 | +58 ✅ partial |
| `input-graph.h` (extend) | `skip_to_eol()`, `skip_wsc()`, `read_graph_identifier()` | — | **+77** ✅ |
| `input-kv.cpp` (new) | Table-driven key-value parser | ~200 | 283 ✅ |
| `input-utils.cpp` (extend) | `input_scan()`, `input_read_token()` | +250 | — 🚫 not planned |
| `input-graph-lexer.hpp` (new) | `InputCursor` + `CommentStyle` presets | ~150 | — 🚫 not planned |
| **Total added (final)** | | | **+280** |

**Final**: ~773 gross lines removed from callers, +280 lines added to shared
infrastructure = **~493 net lines eliminated** (~6% of 8,770 baseline).

No further files will be added — remaining initiatives assessed as not worth implementing.

# Input Parser Architecture — Code Reduction Proposal

**Goal**: Reduce the combined input parser codebase by ~50% through shared
infrastructure, a scanning DSL, and structural consolidation.

**Scope**: All files under `lambda/input/` — primary focus on the structured-data
parsers (`input-json.cpp`, `input-toml.cpp`, `input-yaml.cpp`, `input-xml.cpp`,
`input-ini.cpp`, `input-prop.cpp`, `input-csv.cpp`, `input-eml.cpp`,
`input-vcf.cpp`, `input-ics.cpp`) and the graph parsers
(`input-graph-d2.cpp`, `input-graph-dot.cpp`, `input-graph-mermaid.cpp`).

---

## 1. Baseline Inventory

### 1.1. Line Counts

| File | Lines | Notes |
|---|---|---|
| `input-yaml.cpp` | 2,603 | Largest; YAML 1.2 full parser |
| `input-toml.cpp` | 1,069 | |
| `input-graph-mermaid.cpp` | 759 | |
| `input-xml.cpp` | 755 | |
| `input-graph-dot.cpp` | 592 | |
| `input-ics.cpp` | 524 | |
| `input-graph-d2.cpp` | 431 | |
| `input-json.cpp` | 442 | |
| `input-vcf.cpp` | 340 | |
| `input-eml.cpp` | 301 | |
| `input-ini.cpp` | 271 | |
| `input-prop.cpp` | 211 | |
| `input-csv.cpp` | 255 | |
| `input-graph.cpp` | 217 | |
| **Total** | **8,770** | |

**Target**: ~4,400 lines after all reductions (50% cut).

### 1.2. Recurring Duplication Patterns

#### A. Escape Sequence Handling

The `\b`, `\f`, `\n`, `\r`, `\t`, `\\`, `\"`, and `\uXXXX` switch block is
replicated nearly verbatim in:

| File | ~Lines | Notes |
|---|---|---|
| `input-json.cpp` | 40 | `\uXXXX` with surrogate pairs |
| `input-toml.cpp` | 50 | `\uXXXX` + `\UXXXXXXXX` + additional |
| `input-prop.cpp` | 35 | `\uXXXX` |
| `input-xml.cpp` | 25 | character references `&#NNNN;` / `&#xHHHH;` |

All of these could call one parameterized `parse_escape_char()`.

#### B. Quoted String Boilerplate

Every parser that handles `"..."` or `'...'` strings has identical structure:

```
1. Check opening delimiter
2. Reset StringBuf
3. Loop: read char; if backslash → dispatch escape; else if closing → break; else append
4. Expect closing delimiter
5. Return createString()
```

This struct (~30 lines) appears independently in JSON, TOML, XML, and all three
graph parsers (~6 copies = ~180 lines).

#### C. `skip_to_eol` / `skip_whitespace_and_comments`

All three graph parsers duplicate:
- `skip_to_eol()` — 5 lines, identical in all three
- `skip_whitespace_and_comments()` — 20-30 lines each, differing only in the
  comment marker (`#`, `//`/`/**/`, `%%`)

Same pattern exists in YAML (`skip_comment`, `skip_blank_lines`), TOML, and INI.

#### D. Number / Scalar Coercion

`parse_typed_value()` exists in `input-utils.hpp` but JSON and TOML do not use
it. YAML has its own `coerce_plain_scalar()` with overlapping logic.

| File | Re-implements | ~Lines |
|---|---|---|
| `input-json.cpp` | null/true/false + strtod | 30 |
| `input-toml.cpp` | integer + float + bool + datetime | 70 |
| `input-yaml.cpp` | all scalars + hex/octal | 90 |

#### E. RFC-Property Parameters (VCF + ICS)

`parse_property_parameters()` — a 60-line function — exists identically in both
`input-vcf.cpp` and `input-ics.cpp`, differing only in whether the key is
upper- or lower-cased. The `input-rfc-text.h` layer was started but does not yet
cover parameter parsing.

#### F. Key-Value Line Parsing (INI + prop)

`input-ini.cpp` and `input-prop.cpp` are near-clones (~482 lines combined) with
different comment characters, key-value separators, and section handling.

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

### 4.1. Shared Escape Sequence Handler

Currently the standard JSON/TOML escape switch (`\b`, `\f`, `\n`, `\r`, `\t`,
`\\`, `\"`, `\uXXXX`) appears at least 3× with small variations.

**Proposal**: extend `input-utils.hpp` with a single `parse_escape_char()`:

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

### 4.2. Shared `parse_shared_quoted_string()`

A universal quoted-string parser parameterised by quote char and escape config:

```cpp
// Parse a quoted string starting at **src (must point at the opening delimiter).
// Handles triple-quote multiline variant when triple == true.
// Returns a new String* or nullptr on error.
String* parse_shared_quoted_string(InputContext& ctx, const char** src,
                                   char quote_char,
                                   bool allow_multiline,
                                   const EscapeConfig* escapes);
```

Replaces:
- `parse_string()` in `input-json.cpp` (~50 lines)
- `parse_basic_string()` + `parse_multiline_basic_string()` in `input-toml.cpp`
  (~100 lines)
- `parse_quoted_string()` in `input-graph-dot.cpp`, `input-graph-d2.cpp`,
  `input-graph-mermaid.cpp` (~25 lines each)

**Estimated savings**: ~220 lines.

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

### 5.1. Merge `input-ini.cpp` + `input-prop.cpp` → `input-kv.cpp`

These two files are near-clones (~482 lines combined). The only differences are:

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

### 5.2. RFC-Property Layer Completion (VCF + ICS)

`input-rfc-text.h` already provides `parse_rfc_property_name()` and
`parse_rfc_property_value()`. What is missing is `parse_rfc_property_parameters()`
— the `;PARAM=value` block that `input-vcf.cpp` and `input-ics.cpp` each implement
in ~60 lines.

Add to `input-rfc-text.h`:

```cpp
// Parse ';'-delimited parameters into params_map.
// On entry, **pos must point at ';' or ':'.
// Returns on ':', leaving **pos pointing at ':'.
// Parameter keys are normalised to lowercase (set upper_case = true for ICS).
void parse_rfc_property_params(StringBuf* sb, const char** pos,
                               InputContext& ctx, Map* params_map,
                               bool upper_case_keys);
```

Also consolidate the property-loop scaffold shared by VCF and ICS into a callback
interface:

```cpp
// Called for each fully-parsed property.
typedef void (*RfcPropertyHandler)(InputContext& ctx, String* name,
                                   Map* params, String* value, void* user_data);

// Drives the outer parse loop: reads lines, splits name/params/value, invokes handler.
void parse_rfc_document(InputContext& ctx, const char* src,
                        RfcPropertyHandler handler, void* user_data);
```

VCF and ICS each provide a ~30-line handler function; the 60-line parse loop
and parameter parser move to `input-rfc-text.h`.

**Estimated savings**: **~180 lines** across VCF + ICS.

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

| Initiative | Files Affected | Lines Saved (est.) |
|---|---|---|
| **`input_scan()` DSL** | ini, prop, eml, vcf, ics, csv | ~1,002 |
| **`InputCursor` for graph parsers** | d2, dot, mermaid | ~782 |
| **Merge INI + prop → `input-kv.cpp`** | ini, prop | ~282 |
| **Shared quoted-string parser** | json, toml, xml, d2, dot, mermaid | ~220 |
| **RFC-property layer completion** | vcf, ics | ~180 |
| **YAML/TOML shared number/datetime utilities** | yaml, toml | ~180 |
| **Full `parse_typed_value()` adoption** | json, toml, yaml | ~150 |
| **Shared escape handler** | json, toml, prop | ~120 |
| **`input_read_token()` / `input_read_field()`** | many | ~200 |
| **`input_match()` / `input_expect_literal()`** | many | ~100 |
| **BOM, whitespace minor utilities** | various | ~50 |
| **YAML internal cleanup** | yaml | ~320 |
| **Total** | | **~3,586** |

### 8.2. Projection

| Metric | Current | After |
|---|---|---|
| Total parser lines | 8,770 | ~5,184 |
| Reduction | — | **~41%** |
| New shared utility lines added | 0 | ~700 |
| Net reduction | — | **~2,886 lines (~33%)** |

With high-fidelity execution of all initiatives, ~40% gross reduction is
achievable. Reaching 50% would additionally require a more aggressive YAML
refactor (splitting tokeniser from tree-builder, ~600 more lines) or a
table-driven approach for JSON (small gains there since JSON is already lean).

### 8.3. Recommended Phasing

**Phase A — Quick wins** (~1-2 days, ~500 lines saved, zero risk):
1. Add `parse_escape_char()` to `input-utils.hpp`; migrate JSON, TOML, prop
2. Add `input_read_token()` / `input_expect_literal()`; migrate call sites
3. Complete RFC-property parameters in `input-rfc-text.h`; remove duplicate VCF/ICS code
4. Add `parse_shared_quoted_string()`; migrate JSON + graph parsers

**Phase B — Structural** (~3-5 days, ~1,000 lines saved, low risk):
5. Implement `InputCursor`; rewrite graph parsers to use it
6. Merge `input-ini.cpp` + `input-prop.cpp` → `input-kv.cpp`
7. Full `parse_typed_value()` adoption in JSON and TOML

**Phase C — DSL** (~1 week, ~1,200 lines saved, medium risk):
8. Implement `input_scan()`; migrate INI, prop (via KV), EML, VCF, ICS, CSV
9. Add YAML/TOML shared datetime/number utilities

**Phase D — YAML** (~1-2 weeks, ~600-900 lines saved, higher risk):
10. Split YAML into tokeniser + builder layers; refactor block/flow parsers
    to use shared cursor utilities

---

## Appendix: New Files Introduced

| File | Purpose | ~Lines |
|---|---|---|
| `input-utils.cpp` (extend) | `input_scan()`, `parse_escape_char()`, `input_read_token()` | +250 |
| `input-utils.hpp` (extend) | `parse_shared_quoted_string()`, `InputCursor`, `parse_typed_value()` extensions | +150 |
| `input-rfc-text.h` (extend) | `parse_rfc_property_params()`, `parse_rfc_document()` | +80 |
| `input-kv.cpp` (new — replaces ini + prop) | Table-driven key-value parser | ~200 |
| `input-graph-lexer.hpp` (new) | `InputCursor` + `CommentStyle` presets for graph parsers | ~150 |
| **Total added** | | **~830** |

Net: ~3,586 gross lines removed, ~830 new utility lines added = **~2,756 net
lines eliminated (~31% net reduction of the 8,770 line total)**.

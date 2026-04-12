# Input System Cleanup Proposal

Comprehensive restructuring proposal for `lambda/input/` and `lambda/format/` to improve consistency, eliminate duplication, and share more code between parsers and formatters.

---

## 1. File/Module Inventory

### Input Parsers (`lambda/input/`)

| Parser | File | Style | LOC (est.) |
|--------|------|-------|------------|
| JSON | `input-json.cpp` | Hand-written recursive descent, `InputContext` | ~600 |
| XML | `input-xml.cpp` | Hand-written recursive descent, `InputContext` | ~700 |
| CSV | `input-csv.cpp` | Hand-written, `InputContext` | ~350 |
| TOML | `input-toml.cpp` | Hand-written recursive descent, `InputContext` | ~900 |
| YAML | `input-yaml.cpp` | Hand-written, `InputContext` | ~800 |
| INI | `input-ini.cpp` | Hand-written, `InputContext` | ~200 |
| Properties | `input-prop.cpp` | Hand-written, `InputContext` | ~200 |
| EML | `input-eml.cpp` | Hand-written, `InputContext` | ~300 |
| VCF | `input-vcf.cpp` | Hand-written, `InputContext` | ~250 |
| ICS | `input-ics.cpp` | Hand-written, `InputContext` | ~300 |
| RTF | `input-rtf.cpp` | Hand-written, `InputContext` | ~400 |
| Mark | `input-mark.cpp` | Hand-written, Mark format | ~500 |
| Math | `input-math.cpp` | Hand-written, math expressions | ~400 |
| CSS | `input-css.cpp` | Delegates to `css/` engine | thin |
| JSX | `input-jsx.cpp` | Hand-written | ~300 |
| MDX | `input-mdx.cpp` | Delegates to markup pipeline | thin |
| LaTeX | `input-latex-ts.cpp` | Tree-sitter CST, `extern "C"` | ~800 |
| HTML5 | `html5/` (4 files) | Spec-compliant state machine tokenizer + tree builder | ~3000 |
| Markup | `markup/` (multi-file) | Modular block/inline pipeline | ~2000 |
| PDF | `input-pdf.cpp` + `pdf_decompress.cpp` | Hand-written | ~600 |
| Graph | `input-graph.cpp` + 3 sub-parsers | Dispatcher + DOT/Mermaid/D2 | ~800 |
| Dir | `input-dir.cpp` | File system listing | ~200 |

### Shared Input Utilities

| File | Content | Used By |
|------|---------|---------|
| `input-utils.h` | C API: `codepoint_to_utf8`, `decode_surrogate_pair`, `parse_hex_codepoint`, `try_parse_int64`, `try_parse_double`, `input_strncasecmp` | Most parsers |
| `input-utils.hpp` | C++ extensions: `append_codepoint_utf8`, `parse_typed_value` | TOML, YAML, CSV, INI, etc. |
| `input-utils.cpp` | Implementations for both headers above (~184 lines) | — |
| `input-common.cpp` | **LaTeX-only** tables (`greek_letters[]`, `math_operators[]`, etc.) with lazy-init binary search (~192 lines) | LaTeX parser only |
| `input-context.hpp` | `InputContext` class + LaTeX function declarations (misplaced) | All C++ parsers |
| `input-context.cpp` | `InputContext` method implementations | — |
| `html_entities.cpp/.h` | ~150 named HTML entity lookup (linear scan) | XML, Markup, Radiant |
| `parse_error.cpp/.hpp` | `ParseErrorList`, `SourceLocation` | via `InputContext` |
| `source_tracker.cpp/.hpp` | Line/column tracking | via `InputContext` |

### Output Formatters (`lambda/format/`)

| Formatter | File | Architecture |
|-----------|------|-------------|
| JSON | `format-json.cpp` | `JsonContext` subclass |
| XML | `format-xml.cpp` | `XmlContext` subclass |
| HTML | `format-html.cpp` | `HtmlContext` subclass |
| YAML | `format-yaml.cpp` | `YamlContext` subclass |
| CSS | `format-css.cpp` | `CssContext` subclass |
| LaTeX | `format-latex.cpp` | `LaTeXContext` subclass |
| Text | `format-text.cpp` | `TextContext` subclass |
| TOML | `format-toml.cpp` | Standalone (no context class) |
| JSX | `format-jsx.cpp` | Standalone |
| MDX | `format-mdx.cpp` | Standalone |
| Markdown | `format-md.cpp` | thin wrapper → `MarkupEmitter` |
| RST | `format-rst.cpp` | thin wrapper → `MarkupEmitter` |
| Org | `format-org.cpp` | thin wrapper → `MarkupEmitter` |
| Wiki | `format-wiki.cpp` | thin wrapper → `MarkupEmitter` |
| Textile | `format-textile.cpp` | thin wrapper → `MarkupEmitter` |
| KV (INI/Prop) | `format-kv.cpp` | `KeyValueFormatConfig` |
| Graph | `format-graph.cpp` | DOT/Mermaid/D2 output |
| Math | `format-math.cpp`, `-ascii.cpp`, `-latex.cpp` | Math expression output |

### Shared Format Utilities

| File | Content |
|------|---------|
| `format-utils.h` (267 lines) | C API: `TextEscapeConfig`, `EscapeRule`, `TableInfo`, `MarkupOutputRules` |
| `format-utils.hpp` (849 lines) | C++ base class `FormatterContextCpp` + 11 subclass definitions |
| `format-utils.cpp` (990 lines) | Implementations: escape configs, `EscapeRule` tables, `MarkupOutputRules` data |
| `format.h` (62 lines) | Public API declarations (`format_json`, `format_xml`, etc.) |
| `format.cpp` | Dispatcher: `format_to_string()` with static dispatch tables |

---

## 2. Structural Consistency Improvements

### 2.1 Standardize Parser Entry Points

Currently, parser entry point signatures are declared ad-hoc via forward declarations in `input.cpp`:

```cpp
// Current: scattered forward declarations
void parse_json(Input* input, const char* json_string);
void parse_xml(Input* input, const char* xml_string);
extern "C" void parse_latex_ts(Input* input, const char* latex_string);  // different linkage!
extern "C" Item input_markup_modular(Input* input, ...);                 // different signature!
```

**Proposal**: Standardize on a single parser function signature and declare all parsers in a shared `input-parsers.h`:

```cpp
// input-parsers.h — registry of all parser entry points
#ifndef INPUT_PARSERS_H
#define INPUT_PARSERS_H

#include "../lambda-data.hpp"

// Standard parser signature: void parse_XXX(Input*, const char*)
void parse_json(Input* input, const char* source);
void parse_xml(Input* input, const char* source);
void parse_csv(Input* input, const char* source);
void parse_toml(Input* input, const char* source);
void parse_yaml(Input* input, const char* source);
void parse_ini(Input* input, const char* source);
void parse_properties(Input* input, const char* source);
void parse_eml(Input* input, const char* source);
void parse_vcf(Input* input, const char* source);
void parse_ics(Input* input, const char* source);
void parse_rtf(Input* input, const char* source);
void parse_mark(Input* input, const char* source);
void parse_latex_ts(Input* input, const char* source);
void parse_math_expression(Input* input, const char* source);
// ... etc.

#endif
```

Remove the forward declarations from `input.cpp` and include `input-parsers.h` instead. For parsers that currently use `extern "C"` linkage or different signatures, add thin C++ wrappers to conform.

### 2.2 Standardize Formatter Architecture

Two formatter styles coexist:
1. **Context-class formatters**: `JsonContext`, `HtmlContext`, `YamlContext`, etc. — subclass `FormatterContextCpp`, implement format-specific escape/indent methods.
2. **Data-driven markup emitters**: `format-md.cpp`, `format-rst.cpp`, etc. — delegate to `MarkupEmitter` driven by `MarkupOutputRules` tables.

Style 2 is the better design for lightweight markup formats. Style 1 is fine for structured formats (JSON, XML, etc.) but several subclasses are **dead code** (see §6.1).

**Proposal**: Keep both styles but ensure each subclass is actually used. Every context class should follow a consistent pattern:

```cpp
class FooContext : public FormatterContextCpp {
public:
    FooContext(Pool* pool, StringBuf* output);

    // Use base class write_indent() — do NOT redeclare
    // Use base class indent helpers — do NOT shadow

    // Format-specific escaping
    void write_escaped_text(const char* text, size_t len);
    void write_escaped_attribute(const char* text, size_t len);
};
```

### 2.3 Unify Formatter Public API Signatures

Current API has two patterns:
```cpp
String* format_json(Pool* pool, const Item root_item);   // returns String*
void format_json_to_strbuf(StringBuf* sb, Item root_item); // internal
```

Some formats only have one variant. The `String*` versions should be the canonical API; the `StringBuf` versions are internal. This is fine but should be documented as a convention.

---

## 3. Maximize Code Sharing

### 3.1 Shared Line-Oriented Parsing Helpers

Five parsers independently implement identical line-parsing functions:

| Function | Files |
|----------|-------|
| `skip_to_newline()` | `input-prop.cpp`, `input-eml.cpp`, `input-ini.cpp`, `input-ics.cpp`, `input-vcf.cpp` |
| `skip_line_whitespace()` | `input-eml.cpp`, `input-ics.cpp`, `input-vcf.cpp` |
| `is_folded_line()` / `is_continuation_line()` | `input-eml.cpp`, `input-ics.cpp`, `input-vcf.cpp` |
| `is_comment()` | `input-ini.cpp`, `input-prop.cpp` |
| `parse_key()` | `input-ini.cpp`, `input-prop.cpp`, `input-toml.cpp` |

**Proposal**: Extract a `input-line-utils.h` for line-oriented text parsing:

```cpp
// input-line-utils.h — shared helpers for line-oriented parsers
#ifndef INPUT_LINE_UTILS_H
#define INPUT_LINE_UTILS_H

#include "source_tracker.hpp"

// Skip past the current line (advance past \n or \r\n)
void skip_to_newline(const char** pos, SourceTracker* tracker = nullptr);

// Skip spaces and tabs on the current line (not past newlines)
void skip_line_whitespace(const char** pos);

// Check if next line is a continuation/folded line (RFC 5322/5545 style)
bool is_continuation_line(const char* pos);

// Check if character starts a comment (configurable: #, ;, !)
bool is_comment_char(char c, const char* comment_chars);

// Parse a key token until any of the given delimiters
// Returns length of key, or 0 if empty
size_t parse_key_until(const char* pos, const char* delimiters);

#endif
```

This eliminates ~5 copies of `skip_to_newline`, ~3 copies of `skip_line_whitespace`, ~3 copies of `is_folded_line`.

### 3.2 Shared RFC Text-Record Parsing Base

EML (RFC 5322), ICS (RFC 5545), and VCF (RFC 6350) share the same fundamental structure: header lines with `Key: Value` or `KEY;PARAM=val:VALUE`, line folding, and structured values. They all independently implement:
- Line unfolding
- Header field parsing
- Parameter extraction
- Quoted-string handling

**Proposal**: Create `input-rfc-text.h/.cpp` with shared RFC text-record parsing:

```cpp
// input-rfc-text.h — shared RFC-style record parser
struct RfcField {
    const char* name;
    size_t name_len;
    const char* value;
    size_t value_len;
    // Parameters (for VCF/ICS: KEY;TYPE=work:VALUE)
    const char* params;
    size_t params_len;
};

// Parse a single RFC header field from the current position
// Handles line unfolding (continuation lines starting with space/tab)
bool parse_rfc_field(const char** pos, RfcField* field, SourceTracker* tracker);

// Unfold a multi-line value (remove CRLF + whitespace continuations)
size_t unfold_rfc_value(const char* src, size_t len, char* dst, size_t dst_cap);
```

### 3.3 Consolidate Escape Logic in Formatters

The same character escape logic is **triplicated** for JSON, XML, HTML, and LaTeX:

1. `EscapeRule` tables in `format-utils.cpp` (defined but **unused**)
2. Inline methods in context classes (`write_string_escaped`, `write_xml_escaped_text`, etc.)
3. Direct inline code in formatter `.cpp` files

**Proposal**: Pick ONE mechanism and use it everywhere. The `EscapeRule` table approach is cleanest:

```cpp
// Already exists in format-utils.h:
typedef struct {
    char from;
    const char* to;
} EscapeRule;

// Already exists in format-utils.cpp but UNUSED:
static const EscapeRule JSON_ESCAPE_RULES[] = { ... };
static const EscapeRule XML_TEXT_ESCAPE_RULES[] = { ... };
// etc.
```

Refactor context classes to use `format_escaped_string(sb, text, len, rules, nrules)` instead of inline escape loops. Then remove the duplicate inline methods. This turns ~12 independent escape implementations into calls to a single table-driven function.

### 3.4 Unify Indent Helpers

Five context subclasses define their own indent methods (`write_indent`, `write_xml_indent`, `write_css_indent`, `write_yaml_indent`, `write_latex_indent`). All are functionally identical to the base class `FormatterContextCpp::write_indent()`.

**Proposal**: Remove all format-specific indent methods. Use the base class `write_indent()` everywhere. The few that differ slightly (e.g., 4-space vs 2-space) can use a configurable `indent_string` in the base class:

```cpp
class FormatterContextCpp {
    // ...
    const char* indent_string_ = "  ";  // default 2-space indent
public:
    void set_indent_string(const char* s) { indent_string_ = s; }
    void write_indent() {
        for (int i = 0; i < indent_level_; i++)
            write_text(indent_string_);
    }
};
```

### 3.5 Relocate Misplaced Utility Functions from `input.cpp`

These general-purpose functions currently live in the dispatcher (`input.cpp`) but belong in `input-utils`:

- `skip_whitespace()` — skips space/tab/newline
- `skip_tab_pace()` — skips spaces/tabs only
- `input_is_whitespace_char()` — character classification
- `input_count_leading_chars()` — count repeated leading chars
- `input_free_lines()` — free line array

**Proposal**: Move to `input-utils.h/cpp`.

---

## 4. Shared Tag and Name Definitions

### 4.1 HTML Tag Definitions

HTML tag names, void element lists, and boolean attribute lists are defined independently in multiple places:

| Location | Definition | Lookup |
|----------|-----------|--------|
| `format-html.cpp` | `void_elements[]`, `raw_text_elements[]`, `boolean_attributes[]` | Linear scan, static local |
| `html5/html5_tree_builder.cpp` | Inline checks for void/rawtext elements | Hardcoded `if`/`switch` |
| `html5/html5_parser.h` | HTML namespace tag enum | Enum values |

**Proposal**: Create `html-defs.h` with shared HTML vocabulary:

```cpp
// html-defs.h — shared HTML tag/attribute definitions
#ifndef HTML_DEFS_H
#define HTML_DEFS_H

#include <stdbool.h>

// Void elements (self-closing, no end tag)
bool html_is_void_element(const char* tag);

// Raw text elements (content not parsed as HTML)
bool html_is_raw_text_element(const char* tag);

// Boolean attributes (no value needed)
bool html_is_boolean_attribute(const char* attr);

// Block-level elements (for formatting decisions)
bool html_is_block_element(const char* tag);

// Heading elements (h1-h6)
bool html_is_heading(const char* tag);
int  html_heading_level(const char* tag);  // 0 if not heading

#endif
```

Use sorted arrays with binary search (same pattern as `input-common.cpp`) for O(log n) lookups. Replace the linear scans in `format-html.cpp` and inline checks in the HTML5 parser.

### 4.2 LaTeX Command Definitions

Currently in `input-common.cpp` with declarations incorrectly placed in `input-context.hpp` **outside** the `namespace lambda` block.

**Proposal**: See §5 — rename and relocate.

### 4.3 Entity Name Definitions

Two completely independent entity systems exist (see §6 for full analysis and unification proposal). The html5 tokenizer's 2,125-entry table is a strict superset of the ~200-entry `html_entities.cpp` table. Every entity in `html_entities.cpp` exists in the html5 table with identical UTF-8 encoding.

**Proposal**: Unify into a single shared entity module. See §6 for the detailed unification design.

---

## 5. Merge `input-utils` into `input-common` (Renamed)

### Current State

| File | Content | Problem |
|------|---------|---------|
| `input-utils.h` | General-purpose C API (UTF-8, numeric parsing) | Good, stay |
| `input-utils.hpp` | C++ extensions (`parse_typed_value`, `append_codepoint_utf8`) | Good, stay |
| `input-utils.cpp` | Implementations for both headers (184 lines) | **Merge target** |
| `input-common.cpp` | LaTeX-only tables (192 lines) | **Misleading name** |

### Proposal

**Step 1**: Rename `input-common.cpp` → `input-latex-tables.cpp` to reflect its actual content (only LaTeX command/environment classification tables).

**Step 2**: Move the LaTeX function declarations from `input-context.hpp` (bottom, outside `namespace lambda`) into a new `input-latex-tables.h`.

**Step 3**: Merge `input-utils.cpp` into a new `input-common.cpp` that serves as the **actual** common utility module. This new file combines:
- Everything currently in `input-utils.cpp` (UTF-8, numeric parsing, etc.)
- The misplaced utilities from `input.cpp` (`skip_whitespace`, `skip_tab_pace`, etc.)
- The new line-oriented helpers proposed in §3.1
- The `unicode_to_utf8()` wrapper currently duplicated in `html_entities.cpp`

**Step 4**: Update headers accordingly:
- `input-utils.h` remains the C API header (unchanged content)
- `input-utils.hpp` remains the C++ extensions header (unchanged content)
- Both now document that their implementations live in `input-common.cpp`
- Alternatively, rename `input-utils.h` → `input-common.h` and `input-utils.hpp` → `input-common.hpp` for naming consistency

### Result

```
Before:                          After:
  input-utils.h          →        input-common.h          (C API, renamed)
  input-utils.hpp        →        input-common.hpp         (C++ API, renamed)
  input-utils.cpp        ─┐       input-common.cpp         (merged implementations)
  input-common.cpp       ─┘
                                   input-latex-tables.h     (new, declarations for LaTeX)
                                   input-latex-tables.cpp   (renamed from old input-common.cpp)
```

---

## 6. Unify HTML Entity Systems

### 6.1 Two Independent Entity Systems Today

Two completely independent entity lookup systems exist:

#### System 1: `html_entities.h/.cpp` (~200 curated entries)

| Property | Detail |
|----------|--------|
| Location | `lambda/input/html_entities.cpp` |
| Entries | ~200 hand-maintained named entities |
| Storage | 4 separate tables: `ascii_escapes[]` (5), `unicode_spaces[]` (4), `multi_codepoint_entities[]` (1), `html_entity_table[]` (~190) |
| Lookup | O(n) linear scan across each table in priority order |
| Return type | Typed `EntityResult` with `EntityType` enum (5 categories) |
| Reverse lookup | `html_entity_name_for_codepoint()` — codepoint → entity name |

Consumers:

| Consumer | File | What it uses |
|----------|------|-------------|
| XML parser | `input-xml.cpp` | `html_entity_resolve()` for `&entity;` decoding |
| Markup parser | `markup/markup_parser.cpp` | Entity resolution in inline HTML |
| Markup inline | `markup/inline/inline_special.cpp` | Entity resolution |
| Markup inline | `markup/inline/inline_link.cpp` | Entity in link text |
| Markup block | `markup/block/block_code.cpp` | Entity in code blocks |
| Radiant symbol resolver | `radiant/symbol_resolver.cpp` | Codepoint → entity name lookup |

#### System 2: `html5_tokenizer.cpp` (2,125 WHATWG-complete entries)

| Property | Detail |
|----------|--------|
| Location | `lambda/input/html5/html5_tokenizer.cpp` (inline, ~2,100 lines) |
| Entries | 2,125 auto-generated from WHATWG spec (`utils/generate_html5_entities.py`) |
| Storage | Single sorted `NamedEntity` array: `{ const char* name; const char* replacement; }` |
| Lookup | O(log n) binary search via `html5_lookup_named_entity()` |
| Return type | Simple `const char*` — pre-encoded UTF-8 replacement bytes |
| Reverse lookup | Not supported |
| Extra features | Legacy entity matching (without semicolons), Windows-1252 fixups |

Consumer: HTML5 tokenizer only.

### 6.2 Why Unification Is Warranted

**The html5 table is a strict superset.** Every entity in `html_entities.cpp` exists in the html5 table with identical UTF-8 encoding. There are zero entries unique to `html_entities`.

**The `EntityType` categorization is dead weight.** The enum distinguishes `ENTITY_ASCII_ESCAPE`, `ENTITY_UNICODE_SPACE`, `ENTITY_UNICODE_MULTI`, and `ENTITY_NAMED` — but every consumer decodes all types identically to UTF-8 bytes. Nobody creates Lambda Symbol values from `ENTITY_NAMED`. The only practical difference: two types use a pre-encoded `decoded` string pointer, two use a `codepoint` field + `unicode_to_utf8()`. Both paths produce the same output.

**Internal inconsistencies in System 1:**
- `ensp`, `emsp`, `thinsp` appear in both `unicode_spaces[]` and `html_entity_table[]`. `html_entity_resolve()` checks `unicode_spaces` first (returns `ENTITY_UNICODE_SPACE`), so duplicates in `html_entity_table` are unreachable. But `html_entity_codepoint()` only searches `html_entity_table`, creating inconsistent behavior.
- `unicode_to_utf8()` is a trivial wrapper around `codepoint_to_utf8()` — an unnecessary layer of indirection.
- Linear scan across 4 tables vs binary search in System 2.

**Duplicate UTF-8 encoders:** `html5_tokenizer.cpp` has its own `html5_encode_utf8()` — identical logic to `codepoint_to_utf8()` in `input-utils.cpp`.

### 6.3 Unified Design

Extract the HTML5 entity table into a shared module that both the HTML5 tokenizer and all other consumers use:

```
lambda/input/
  html_entities.h               # Unified API header
  html_entities.cpp             # Thin API layer + reverse lookup index
  html_entities_table.inc       # Auto-generated 2,125-entry table (from generate script)
```

#### Unified API

```cpp
// html_entities.h — unified HTML entity resolution
#ifndef HTML_ENTITIES_H
#define HTML_ENTITIES_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ── Core lookup (replaces both systems) ──────────────────────

// Name → UTF-8 replacement string (O(log n) binary search)
// Returns NULL if not found. Handles all 2,125 WHATWG entities.
const char* html_entity_lookup(const char* name, size_t len);

// ── Convenience functions ────────────────────────────────────

// Is this one of the 5 structural XML entities? (lt, gt, amp, quot, apos)
bool html_entity_is_ascii_escape(const char* name, size_t len);

// Reverse lookup: codepoint → entity name (for formatters/Radiant)
const char* html_entity_name_for_codepoint(uint32_t codepoint);

#ifdef __cplusplus
}
#endif

#endif // HTML_ENTITIES_H
```

#### What changes for each consumer

| Consumer | Current call | New call |
|----------|-------------|----------|
| HTML5 tokenizer | `html5_lookup_named_entity(name, len)` → `const char*` | `html_entity_lookup(name, len)` — same return type, drop-in |
| XML parser | `html_entity_resolve(name, len)` → branch on `EntityType` → decode | `html_entity_lookup(name, len)` — direct UTF-8, no branching |
| Markup parser | Same as XML — branch on type, then decode | Same simplification — direct `const char*` |
| Radiant symbol resolver | `html_entity_resolve()` → extract `named.codepoint` | `html_entity_lookup()` for UTF-8 + decode codepoint from UTF-8 (see below) |

#### Radiant codepoint concern

The Radiant symbol resolver stores `result.named.codepoint` into `SymbolResolution.codepoint`. With the unified API returning only UTF-8 bytes, two options:

1. **Decode UTF-8 → codepoint at runtime** (trivial 3-line helper for single-codepoint entities, which is all Radiant handles):
    ```cpp
    // Extract first codepoint from UTF-8 string
    static uint32_t utf8_first_codepoint(const char* utf8) {
        uint8_t b = (uint8_t)utf8[0];
        if (b < 0x80) return b;
        if ((b & 0xE0) == 0xC0) return ((b & 0x1F) << 6) | (utf8[1] & 0x3F);
        if ((b & 0xF0) == 0xE0) return ((b & 0x0F) << 12) | ((utf8[1] & 0x3F) << 6) | (utf8[2] & 0x3F);
        if ((b & 0xF8) == 0xF0) return ((b & 0x07) << 18) | ((utf8[1] & 0x3F) << 12) | ((utf8[2] & 0x3F) << 6) | (utf8[3] & 0x3F);
        return 0;
    }
    ```
2. **Build a reverse index** from the entity table during initialization — needed anyway for `html_entity_name_for_codepoint()`.

Option 1 is simplest. Option 2 is needed if reverse lookup is required, and both can coexist.

#### What stays in `html5_tokenizer.cpp` (not shared)

These HTML5-spec-specific behaviors remain as consumer-side logic in the tokenizer:
- **Legacy entity matching** — entities without semicolons (e.g., `&amp` in old HTML)
- **Windows-1252 fixups** — numeric references 0x80–0x9F mapped to correct codepoints
- **Attribute context sensitivity** — different matching rules inside attribute values per WHATWG spec

These call the shared `html_entity_lookup()` but implement spec-specific matching logic around it.

#### What gets deleted

| Deleted | Location | Lines saved |
|---------|----------|-------------|
| `NamedEntity named_entities[]` (2,125 entries) | `html5_tokenizer.cpp` | ~2,130 |
| `html5_lookup_named_entity()` | `html5_tokenizer.cpp` | ~30 |
| `html5_encode_utf8()` | `html5_tokenizer.cpp` | ~25 |
| `EntityType` enum + `EntityResult` struct | `html_entities.h` | ~25 |
| 4 separate tables (`ascii_escapes`, `unicode_spaces`, `multi_codepoint_entities`, `html_entity_table`) | `html_entities.cpp` | ~175 |
| `html_entity_resolve()` | `html_entities.cpp` | ~50 |
| `html_entity_codepoint()` | `html_entities.cpp` | ~15 |
| `unicode_to_utf8()` wrapper | `html_entities.cpp` | ~10 |
| **Total** | | **~2,460 lines** |

#### What gets added

| Added | Location | Lines |
|-------|----------|-------|
| `html_entities_table.inc` (auto-generated) | `lambda/input/` | ~2,130 (generated, not hand-maintained) |
| `html_entity_lookup()` (binary search) | `html_entities.cpp` | ~30 |
| `html_entity_name_for_codepoint()` (reverse index) | `html_entities.cpp` | ~40 |
| `html_entity_is_ascii_escape()` (5-entry check) | `html_entities.cpp` | ~15 |
| Optional: `utf8_first_codepoint()` helper | `input-utils.h` or inline | ~8 |
| **Total** | | **~95 lines** (new hand-written code) |

**Net result**: ~2,300 lines removed from `html5_tokenizer.cpp`, ~150 lines removed from old `html_entities.cpp`, one shared ~2,130-line auto-generated `.inc` table, and simpler consumer code everywhere.

### 6.4 Generation Script Update

The existing `utils/generate_html5_entities.py` generates the entity table inline into `html5_tokenizer.cpp`. Update it to:
1. Output a standalone `html_entities_table.inc` file instead
2. Generate the `NamedEntity` struct and sorted array with the same format
3. Optionally generate a reverse-index array (codepoint → name) for `html_entity_name_for_codepoint()`

### 6.5 Migration Steps

| Step | Description | Risk |
|------|-------------|------|
| 1 | Update `generate_html5_entities.py` to output `html_entities_table.inc` | Low |
| 2 | Rewrite `html_entities.cpp` to `#include` the `.inc` table and implement `html_entity_lookup()` with binary search | Low |
| 3 | Update `html_entities.h` — new simplified API, remove `EntityType`/`EntityResult` | Medium |
| 4 | Update HTML5 tokenizer to call `html_entity_lookup()`, remove inline table and `html5_encode_utf8()` | Low |
| 5 | Simplify XML parser — replace `EntityResult` branching with direct `const char*` | Low |
| 6 | Simplify Markup parser consumers (4 files) — same simplification | Low |
| 7 | Update Radiant `symbol_resolver.cpp` — add `utf8_first_codepoint()` for codepoint extraction | Low |
| 8 | Remove `unicode_to_utf8()` wrapper, update any remaining callers to use `codepoint_to_utf8()` | Low |
| 9 | Run entity/emoji tests + HTML5 WPT tests to verify | — |

---

## 7. Additional Cleanup Recommendations

### 7.1 Remove Dead Code in `format-utils.hpp`

Four context classes are completely unused (all replaced by `MarkupEmitter`):

| Class | Lines | Status |
|-------|-------|--------|
| `WikiContext` | ~30 lines (L137–L175) | Dead — `format-wiki.cpp` uses `MarkupEmitter` |
| `RstContext` | ~50 lines (L178–L227) | Dead — `format-rst.cpp` uses `MarkupEmitter` |
| `MarkdownContext` | ~65 lines (L228–L293) | Dead — `format-md.cpp` uses `MarkupEmitter` |
| `OrgContext` | ~55 lines (L294–L347) | Dead — `format-org.cpp` uses `MarkupEmitter` |

**Total**: ~200 lines of dead code. Remove them.

### 7.2 Remove Unused `EscapeRule` Tables

Six `EscapeRule` tables are defined in `format-utils.cpp` but never referenced:
- `JSON_ESCAPE_RULES[]`
- `XML_TEXT_ESCAPE_RULES[]`
- `XML_ATTR_ESCAPE_RULES[]`
- `HTML_TEXT_ESCAPE_RULES[]`
- `HTML_ATTR_ESCAPE_RULES[]`
- `LATEX_ESCAPE_RULES[]`

Either **adopt them** (refactor context classes to use them, per §3.3) or **delete them**. No point defining tables that nothing reads.

### 7.3 Fix `JsonContext` Shadowed Members

`JsonContext` in `format-utils.hpp` declares a private `indent_level_` that shadows the base class member. It also re-declares `indent_level()`, `increase_indent()`, and `decrease_indent()`. The actual `format-json.cpp` passes indent as a function parameter and doesn't use either set.

**Proposal**: Remove the shadowed members from `JsonContext`.

### 7.4 Consolidate `markup-parser.h` and `markup-format.h`

These two top-level headers in `lambda/input/` just declare the `extern "C"` entry points for the markup system. They could be replaced by entries in the proposed `input-parsers.h` (§2.1) and existing `format.h`.

### 7.5  Normalize `extern "C"` Linkage

Three parsers use `extern "C"` (`parse_latex_ts`, `input_markup_modular`, `input_markup_commonmark`) while all others use C++ linkage. Unless these are called from C code (they aren't — the dispatcher is C++), normalize all to C++ linkage.

### 7.6 Consider Shared `Format + Parse` Tag Registry

Some formats need the same tag knowledge for both parsing and formatting. For example:
- **HTML**: Both `input-xml.cpp` and `format-html.cpp` need to know void elements
- **Markdown**: Both `markup/` parser and `format-md.cpp` need heading/inline tag definitions
- **LaTeX**: Both `input-latex-ts.cpp` and `format-latex.cpp` need command classification

A shared `tags/` or `defs/` directory could host format-neutral definition files:
```
lambda/defs/
  html-defs.h      — void elements, boolean attrs, block vs inline
  latex-defs.h     — command/environment classification (from input-common.cpp)
  entity-defs.h    — HTML entity table (from html_entities.cpp)
```

This keeps format knowledge DRY across both `input/` and `format/` trees.

---

## 8. Proposed File Layout (After Cleanup)

```
lambda/input/
  input.cpp                     # Dispatcher (cleaned: no misplaced utilities)
  input.hpp                     # Input type declarations
  input-utils.h                 # Shared C API (UTF-8, numeric, whitespace, line-oriented helpers)
  input-utils.hpp               # Shared C++ API (parse_typed_value, etc.)
  input-utils.cpp               # Implementations for both headers
  input-rfc-text.h              # NEW: RFC text-record helpers (EML/ICS/VCF)
  input-parsers.h               # NEW: all parser entry point declarations
  input-context.hpp             # InputContext class (cleaned: no LaTeX decls)
  input-context.cpp             # InputContext implementations
  input-latex-tables.h          # NEW: LaTeX classification declarations
  input-latex-tables.cpp        # Renamed from input-common.cpp
  html_entities.h               # Unified entity API (simplified, no EntityType/EntityResult)
  html_entities.cpp             # Unified entity implementation (binary search)
  html_entities_table.inc       # Auto-generated 2,125-entry WHATWG table
  parse_error.hpp/.cpp          # Unchanged
  source_tracker.hpp/.cpp       # Unchanged
  input-json.cpp                # Individual parsers (unchanged)
  input-xml.cpp
  input-csv.cpp
  ...
  html5/                        # HTML5 parser (entity table extracted to shared module)
  markup/                       # Markup pipeline (unchanged)
  css/                          # CSS engine (unchanged)
  latex/                        # LaTeX resources (unchanged)

lambda/format/
  format.cpp                    # Dispatcher (unchanged)
  format.h                      # Public API (unchanged)
  format-utils.h                # C escape/table API (unchanged)
  format-utils.hpp              # Base class + USED subclasses only (~600 lines, was 849)
  format-utils.cpp              # Implementations (clean up unused tables or adopt them)
  format-json.cpp               # Individual formatters
  format-xml.cpp
  format-html.cpp
  ...

lambda/defs/                    # NEW: shared definitions (optional, future)
  html-defs.h                   # Void elements, boolean attrs, etc.
  latex-defs.h                  # Command/env classification
```

---

## 9. Implementation Priority

| Priority | Task                                                                                              | Effort | Impact                                  | Status                              |
| -------- | ------------------------------------------------------------------------------------------------- | ------ | --------------------------------------- | ----------------------------------- |
| **P0**   | Remove 4 dead context classes from `format-utils.hpp`                                             | Small  | Cleanliness                             | ✅ Done                              |
| **P0**   | Fix `JsonContext` shadowed `indent_level_`                                                        | Small  | Correctness                             | ✅ Done                              |
| **P1**   | Rename `input-common.cpp` → `input-latex-tables.cpp`, move LaTeX decls out of `input-context.hpp` | Small  | Clarity                                 | ✅ Done                              |
| **P1**   | Relocate misplaced utilities from `input.cpp` to `input-utils.cpp`                                | Small  | Organization                            | ✅ Done                              |
| **P1**   | Unify HTML entity systems — extract shared table, simplify API (§6)                               | Medium | Dedup ~2,300 lines, fix inconsistencies | ✅ Done                              |
| **P2**   | Extract `input-line-utils.h` — shared `skip_to_newline`, `skip_line_whitespace`, etc.             | Medium | Dedup ~15 copies                        | ✅ Done                              |
| **P2**   | Delete unused `EscapeRule` tables in `format-utils.cpp`                                           | Medium | Clean up ~65 lines dead code            | ✅ Done                              |
| **P2**   | Create `input-parsers.h` with all parser declarations                                             | Medium | Consistency                             | ✅ Done                              |
| **P2**   | Normalize `extern "C"` linkage                                                                    | Small  | Consistency                             | ✅ Done                              |
| **P2**   | Unify indent helpers across formatter context classes                                             | Small  | Consistency                             | ✅ Done                              |
| **P3**   | Extract `input-rfc-text.h` for ICS/VCF shared parsing                                             | Medium | Dedup ~60 lines                         | ✅ Done                              |
| **P3**   | Create `html-defs.h` shared between parser and formatter                                          | Medium | DRY tag knowledge                       | ✅ Done                              |
| **P3**   | Consolidate `markup-parser.h` and `markup-format.h`                                               | Small  | Consistency                             | ⊘ N/A — circular dep prevents merge |
| **P3**   | Consolidate formatter escape logic via table-driven `format_escaped_string()`                     | Large  | Dedup ~12 escape impls                  | ✅ Done                              |
| **P4**   | Consider `lambda/defs/` directory for all shared definitions                                      | Large  | Architecture                            | — Not started                       |

---

## 10. Implementation Log

All work verified against full test suites: **466/466 Lambda**, **2302/2303 Radiant** (1 pre-existing failure: `table-anonymous-block-006`).

### §6 — Entity Unification (P1)

Unified two independent HTML entity systems into a single shared module. The 2,125-entry WHATWG table in `html5_tokenizer.cpp` was extracted to `html_entities_table.inc` (auto-generated). The old 4-table system in `html_entities.cpp` (~200 entries, linear scan, `EntityResult`/`EntityType` branching) was replaced with a single `html_entity_lookup()` using O(log n) binary search over the shared table. All 6 consumers (XML parser, markup parser, Radiant symbol resolver, HTML5 tokenizer) updated to the simplified API. Net: ~2,300 lines removed, simpler consumer code.

### P0 — Dead Code Removal

- **Removed 4 dead context classes** from `format-utils.hpp`: `WikiContext`, `RstContext`, `MarkdownContext`, `OrgContext` (~210 lines). All were replaced by `MarkupEmitter` but never deleted.
- **Fixed `JsonContext` shadowed `indent_level_`**: removed the private `indent_level_` that shadowed the base class member, along with redundant `indent_level()`, `increase_indent()`, `decrease_indent()` re-declarations.

### P1 — File Reorganization

- **Renamed `input-common.cpp` → `input-latex-tables.cpp`** with new header `input-latex-tables.h`. The file only contained LaTeX-specific classification tables, not common utilities. LaTeX function declarations moved out of `input-context.hpp` into the new header.
- **Relocated 8 utility functions** from `input.cpp` to `input-utils.cpp`: `skip_whitespace`, `skip_tab_pace`, `input_is_whitespace_char`, `input_count_leading_chars`, `input_detect_indent`, `input_count_indent_columns`, `input_free_lines`, `input_split_lines`. Corresponding declarations moved to `input-utils.h`.

### P2 — Shared Infrastructure

- **Created `input-parsers.h`**: consolidates ~20 parser forward declarations. Uses C++ linkage for most parsers; `extern "C"` only for `parse_latex_ts`, `input_markup_modular`, `input_markup_commonmark` (called from MIR JIT). Removed scattered declarations from `input.cpp`.
- **Created `input-line-utils.h`** (later merged into `input-utils.h`): shared inline helpers `skip_to_newline()`, `skip_line_whitespace()`, `is_folded_line()`. Used by EML, VCF, ICS, INI, Properties parsers. Eliminated ~15 duplicate implementations.
- **Deleted 6 unused `EscapeRule` tables** from `format-utils.cpp` (~65 lines): `JSON_ESCAPE_RULES`, `XML_TEXT_ESCAPE_RULES`, `XML_ATTR_ESCAPE_RULES`, `HTML_TEXT_ESCAPE_RULES`, `HTML_ATTR_ESCAPE_RULES`, `LATEX_ESCAPE_RULES`.
- **Normalized `extern "C"` linkage**: standardized across `input-parsers.h`; only 3 functions that are called from C/MIR code retain `extern "C"`.
- **Unified indent helpers**: added `write_indent(int level)` overload to `FormatterContextCpp` base class. Removed 5 duplicate format-specific indent methods (`write_xml_indent`, `write_css_indent`, `write_yaml_indent`, `write_latex_indent`, second `write_indent` in LaTeX).

### P3 — Shared Definitions

- **Created `html-defs.h` / `html-defs.cpp`**: shared HTML tag/attribute classification with sorted arrays + O(log n) binary search. Provides `html_is_void_element()`, `html_is_raw_text_element()`, `html_is_boolean_attribute()`, `html_is_block_element()`, `html_is_heading()`, `html_heading_level()`. Updated `format-html.cpp` to use shared API — removed ~50 lines of local arrays and linear-scan lookup functions.
- **Created `input-rfc-text.h`**: shared RFC-style `parse_rfc_property_name()` and `parse_rfc_property_value()` (with line-unfolding). Updated `input-vcf.cpp` and `input-ics.cpp` to use shared helpers, eliminating ~60 lines of duplicated parsing code. EML left unchanged (different delimiter rules and trailing-whitespace trim).
- **§7.4 markup header consolidation**: investigated and determined `markup-format.h` must remain separate to break circular dependency (`input.hpp` → `markup-format.h` ← `markup-parser.h` → `input.hpp`). The extern "C" entry points were already moved to `input-parsers.h` in P2. No further consolidation possible.

### P3 — Escape Logic Consolidation

- **Defined 8 predefined `EscapeRule` tables** in `format-utils.cpp` (declared in `format-utils.h`): `JSON_ESCAPE_RULES`, `HTML_TEXT_ESCAPE_RULES`, `HTML_ATTR_ESCAPE_RULES`, `XML_ATTR_ESCAPE_RULES`, `LATEX_ESCAPE_RULES`, `YAML_ESCAPE_RULES`, `JSX_TEXT_ESCAPE_RULES`, `JSX_ATTR_ESCAPE_RULES`.
- **Added `format_escaped_string_ex()`** with `EscapeCtrlMode` enum — extends `format_escaped_string()` to also escape control characters < 0x20 as `\uXXXX` (JSON) or `&#xNN;` (XML). Uses the same flush-buffer approach for performance.
- **Refactored 7 context class escape methods** in `format-utils.hpp` to delegate to table-driven `format_escaped_string[_ex]()` instead of inline per-character switch loops: `JsonContext::write_string_escaped`, `YamlContext::write_yaml_string`, `HtmlContext::write_html_escaped_text/attribute`, `LaTeXContext::write_latex_escaped_text`, `XmlContext::write_xml_escaped_text/attribute`.
- **Refactored 4 standalone escape functions** in formatter `.cpp` files:
  - `format-json.cpp`: `format_string()` and key escaping in `format_map_reader_contents()` (2 copies → `format_escaped_string_ex` + `ESCAPE_CTRL_JSON_UNICODE`)
  - `format-jsx.cpp`: `format_jsx_text_content()` and `format_jsx_attribute_value()` (added `#include "format-utils.h"`, replaced inline switch loops)
  - `format-yaml.cpp`: escape loop in `format_yaml_string()` (replaced 16-line switch with `format_escaped_string` call)
- **Left as-is** (complex logic beyond simple char→string mapping): `format_html_string_safe()` (entity-preservation), `format_xml_string()` (entity-preservation + control chars), `format_graph_string()` (format-dependent branching + needs_quotes), `MarkupEmitter` (`TextEscapeConfig` system — different mechanism).

### Files Created

| File | Purpose |
|------|---------|
| `lambda/input/input-parsers.h` | Consolidated parser forward declarations |
| `lambda/input/input-latex-tables.h` | LaTeX classification declarations (extracted from input-context.hpp) |
| `lambda/input/input-rfc-text.h` | Shared RFC key:value parsing with line-unfolding |
| `lambda/input/html_entities_table.inc` | Auto-generated 2,125-entry WHATWG entity table |
| `lambda/format/html-defs.h` | Shared HTML tag/attribute classification API |
| `lambda/format/html-defs.cpp` | Sorted arrays + binary search implementation |

### Files Merged

| Source | Merged Into |
|--------|-------------|
| `lambda/input/input-line-utils.h` | `lambda/input/input-utils.h` (line-oriented helpers: `skip_to_newline`, `skip_line_whitespace`, `is_folded_line`) |

### Files Renamed

| Before | After |
|--------|-------|
| `lambda/input/input-common.cpp` | `lambda/input/input-latex-tables.cpp` |

---

## 11. Summary

The input/format codebase has been significantly cleaned up. Of the 15 original proposal items:

- **13 completed** (all P0, P1, P2, and P3 items)
- **1 investigated and closed** (§7.4 markup header consolidation — circular dependency prevents merge)
- **1 remaining** (P4 `lambda/defs/` directory — deferred as a longer-term architectural change)

Key improvements delivered:
1. **~2,500 lines of dead/duplicate code removed** (entity unification, dead context classes, unused escape tables)
2. **11 independent escape implementations consolidated** into 8 shared `EscapeRule` tables + `format_escaped_string[_ex]()` calls
3. **6 new shared headers/files** created for code reuse across parsers and formatters
4. **Consistent parser declarations** via `input-parsers.h`
5. **O(log n) lookups** replace linear scans for HTML entities, tags, and attributes
6. **Unified indent helpers** in formatter base class
7. **Correct file naming** (`input-latex-tables.cpp` instead of misleading `input-common.cpp`)

# Formatter Architecture — Next-Generation Refactoring Proposal

## Executive Summary

This proposal analyzes the current state of Lambda Script's 20+ output formatters under `lambda/format/` and recommends improvements across four major themes:

1. **Code Style Unification** — eliminate the three generations of code that coexist; enforce a single, modern, consistent pattern across all formatters
2. **Common Utilities & Code Reuse** — extract duplicated logic into shared functions, reducing ~1,500+ lines of redundancy
3. **Unified Lightweight Markup Formatter** — mirror the input parser's `FormatAdapter` architecture on the output side, enabling a table-driven markup emitter for Markdown, RST, Org, Wiki, Textile
4. **`stringbuf_emit()` — a printf-style formatting function** — a lightweight template engine that replaces verbose `stringbuf_append_*` call sequences with concise format strings, substantially simplifying routine formatting code

Additional proposals: dispatcher table adoption, recursion guard standardization, logging cleanup, dispatch table for `format_data()`, and common element-routing skeleton.

**Prior Art**: Phases 1–8.1 from the earlier `Input_Formatter.md` (now backed up as `.bak`) are complete. All 20 formatters are migrated to MarkReader API. `FormatterContextCpp` class hierarchy and `FormatterDispatcher` infrastructure are in place and tested. This proposal builds on that foundation.

**Current Status (All Phases A–D complete)**: Foundation utilities (`stringbuf_emit`, heading helpers, escape tables, logging cleanup), code deduplication (~2,200 lines removed), the unified markup emitter (1,245-line shared emitter replacing ~3,950 lines across 5 per-format formatters), and architecture polish (API unification, dispatch tables, RecursionGuard standardization, dead code removal) are all implemented and tested. Total formatter codebase reduced from ~9,149 to ~6,936 lines (24% reduction). 362/362 baseline tests + 43 markup-specific tests pass.

---

## Table of Contents

- [1. Current State Analysis](#1-current-state-analysis)
- [2. Code Style Unification](#2-code-style-unification)
- [3. Common Utilities & Code Reuse](#3-common-utilities--code-reuse)
- [4. Unified Lightweight Markup Formatter](#4-unified-lightweight-markup-formatter)
- [5. `stringbuf_emit()` — Printf-Style Formatting](#5-stringbuf_emit--printf-style-formatting)
- [6. Additional Improvements](#6-additional-improvements)
- [7. Implementation Roadmap](#7-implementation-roadmap)

---

## 1. Current State Analysis

### 1.1. Three Generations of Code Coexist

The formatters currently exhibit three distinct implementation generations:

| Generation | Pattern | Files |
|---|---|---|
| **Gen 1** — Raw `Element*` / C-style | Directly accesses `Element*`, `List*`, `TypeElmt*`; uses `get_type_id()` switch; static thread-unsafe buffers | format-org.cpp (predominantly), format-latex.cpp (partial), format-jsx.cpp (partial), format-mdx.cpp (partial), format-graph.cpp (partial) |
| **Gen 2** — MarkReader + bare `StringBuf*` | Uses `ItemReader`/`ElementReader`/`MapReader` but passes `StringBuf*` directly, no context class | format-toml.cpp, format-ini.cpp, format-prop.cpp, format-graph.cpp (reader half) |
| **Gen 3** — MarkReader + C++ Context | Uses typed `XxxContext` subclass extending common `FormatterContext` base (with common fields: output, pool, indent, recursion depth, compact mode) + format-specific fields. MarkReader API. RAII `RecursionGuard`. Most modern. | format-json.cpp, format-yaml.cpp, format-xml.cpp, format-html.cpp, format-md.cpp, format-rst.cpp, format-wiki.cpp, format-textile.cpp, format-text.cpp |

### 1.2. Public API Inconsistency

Two incompatible function signature patterns:

```cpp
// Pattern A: Returns String* (most formatters)
String* format_json(Pool* pool, Item root_item);

// Pattern B: Writes to StringBuf*, separate _string wrapper (markup formatters)
void format_markdown(StringBuf* sb, Item root_item);
String* format_rst_string(Pool* pool, Item root_item);
```

### 1.3. Duplicated Old+New APIs

Several formatters contain **both** old `Element*`-based functions AND new MarkReader-based functions, often with the reader version delegating right back to the old code:

| File | Duplication | Est. wasted lines |
|---|---|---|
| format-org.cpp (991 lines) | MarkReader functions delegate to old `Element*` functions | ~400 |
| format-graph.cpp (838 lines) | Every function (node, edge, cluster) duplicated: `Element*` + `ElementReader` | ~340 |
| format-textile.cpp (980 lines) | `format_element_reader()` + `format_map_as_element_reader()` near-identical | ~450 |
| format-latex.cpp (375 lines) | Old `Element*` + MarkReader coexist, partial delegation | ~100 |
| format-jsx.cpp (386 lines) | Old + new coexist | ~80 |
| format-mdx.cpp (344 lines) | Old + new coexist; duplicated JSX content extraction | ~80 |
| **Total** | | **~1,450** |

### 1.4. Other Duplications

- **format-ini.cpp (217 lines) + format-prop.cpp (187 lines)**: near-clones. Identical `format_item_reader()` structure, identical array handling, identical element fallback. Only difference: section headers in INI vs dot-notation in properties, and slightly different escape chars.
- **Heading level extraction** (`get level from "level" attr → fallback: parse h1-h6 tag name`): independently implemented in md, rst, wiki, textile.
- **Element tag name extraction**: Gen 1 files have local `get_element_type_name()` with static char buffers (thread-unsafe).

### 1.5. Inconsistent Recursion/Depth Guards

| Mechanism | Files |
|---|---|
| `if (indent/depth > 10)` hard-coded | ~~json, yaml,~~ toml |
| `FormatterContextCpp::RecursionGuard` (max 50) | md, rst, wiki, textile, text, **json** ✅, **yaml** ✅, **html** ✅, **xml** ✅, **latex** ✅ |
| **None** | org, ~~html, xml, latex,~~ jsx, mdx, graph, ~~ini, prop~~ kv |

> **Phase D.3 update**: RecursionGuard standardized across all Tier 1 formatters (those with existing context classes). JSON and YAML ad-hoc depth checks replaced with RAII RecursionGuard. HTML, XML, LaTeX gained recursion protection. Tier 2 files (toml, jsx, kv, graph) without context classes retain existing guards or remain unguarded (shallow recursion patterns).

### 1.6. Logging Violations

Per coding rules, `printf`/`fprintf`/`std::cout` must not be used; use `log_debug()` / `log_error()`.

| Violation | Files |
|---|---|
| Uses `printf()` / `fflush()` | format-md.cpp (~30 statements), format-mdx.cpp, format-ini.cpp, format-graph.cpp, format.cpp, format-org.cpp |
| Uses `log_debug()` correctly | format-yaml.cpp, format-xml.cpp |
| No logging | json, toml, html, css, rst, wiki, textile, text, latex, jsx, prop |

### 1.7. Dispatcher Adoption Gap

> **Phase D.4 update**: The `FormatterDispatcher` infrastructure (hashmap-based element routing) was dead code — no formatter used it after the Phase C markup unification. It has been **removed entirely** from `format-utils.h` and `format-utils.cpp` (~170 lines deleted), along with the unused C-level `FormatterContext` struct, `CHECK_RECURSION`/`END_RECURSION` macros, `ElementFormatterFunc` typedef, `formatter_context_create()`/`formatter_context_destroy()`, and the `hashmap.h` include. All formatters use plain `if/else` chains or the unified `MarkupEmitter` for element routing.

---

## 2. Code Style Unification

### 2.1. Goal

Every formatter should follow a single canonical pattern (Gen 3 modernized).

#### FormatterContext Base Class Design

All formatter contexts inherit from a common `FormatterContext` base class (currently named `FormatterContextCpp` to avoid C struct conflict). The base class owns **all shared state** that every formatter needs:

```cpp
// Base class — common context for ALL formatters
class FormatterContext {
protected:
    StringBuf* output_;           // output buffer
    Pool* pool_;                  // memory pool
    int recursion_depth_;         // current recursion depth
    int indent_level_;            // current indentation level
    int max_recursion_depth_;     // depth limit (default 50)
    bool compact_mode_;           // suppress whitespace when true

public:
    // Common operations available to ALL formatters:
    void write_text(const char* text);
    void write_text(String* str);
    void write_char(char c);
    void write_indent();
    void write_newline();
    void emit(const char* fmt, ...);   // printf-style (§5)
    void increase_indent();
    void decrease_indent();

    // RAII recursion guard
    class RecursionGuard { ... };
};
```

Each format then extends with **format-specific context fields** only:

```cpp
// Format-specific extensions — only add what's unique to this format
class MarkdownContext : public FormatterContext {
    int list_depth_;       // markdown-specific: nested list tracking
    bool in_table_;        // markdown-specific: table state
    bool in_code_block_;   // markdown-specific: code fence state
public:
    void write_heading_prefix(int level);  // # / ## / ###
    void write_list_marker(bool ordered, int index);
    void write_code_fence(const char* lang);
    void write_link(const char* url, String* text);
};

class YamlContext : public FormatterContext {
    // yaml-specific: no extra fields beyond base (indent is in base)
public:
    void write_yaml_key(const char* key);
    void write_yaml_string(const char* s, size_t len, bool force_quotes);
    static bool needs_yaml_quotes(const char* s, size_t len);
};

class HtmlContext : public FormatterContext {
    int depth_;            // html-specific: nesting depth for indentation
public:
    void write_tag_open(const char* tag);
    void write_closing_tag(const char* tag);
    void write_attribute(const char* name, const char* value);
    void write_html_escaped_text(const char* text);
};
// ... etc. for Json, Xml, Rst, Wiki, Textile, Org, LaTeX, Css, Toml, KeyValue
```

**Design principle**: Common fields (`output_`, `pool_`, `indent_level_`, `recursion_depth_`, `compact_mode_`) live in the base. Format-specific state (e.g., `list_depth_` in Markdown, `depth_` in HTML, `in_table_` in Markdown) and format-specific helper methods live in the subclass. This keeps the base lean and avoids polluting it with fields only one format needs.

#### Canonical Formatter Pattern

```cpp
// Canonical pattern for all formatters:
// 1. Use XxxContext (subclass of FormatterContext with format-specific fields)
// 2. Use MarkReader API exclusively (no raw Element*)
// 3. Use RecursionGuard for depth protection
// 4. Use log_debug()/log_error() (no printf)
// 5. Return String* via a uniform public API

String* format_xxx(Pool* pool, Item root_item) {
    StringBuf* sb = stringbuf_new(pool);
    XxxContext ctx(pool, sb);         // extends FormatterContext

    ItemReader reader(root_item);
    format_item(ctx, reader);         // or use dispatcher

    String* result = stringbuf_to_string(sb);
    stringbuf_free(sb);
    return result;
}
```

### 2.2. Tasks

#### Task 2.2.1: Complete Gen 1 → Gen 3 Migration

**Files**: format-org.cpp, format-latex.cpp, format-jsx.cpp, format-mdx.cpp, format-graph.cpp

For each file:
1. Identify old `Element*`-based functions
2. Rewrite to use `ElementReader` / `ItemReader` / `MapReader`
3. Remove old functions and the delegation wrappers
4. Ensure the `XxxContext` class is used (move `TextileContext` from inline in `.cpp` to `format-utils.hpp`)
5. Add `RecursionGuard` at every recursive entry point

**Priority files** (largest duplication):
- format-org.cpp — ~400 lines of dead Gen 1 code
- format-graph.cpp — ~340 lines of duplication
- format-textile.cpp — ~450 lines of map-element duplication

#### Task 2.2.2: Unify Public API Signatures

Standardize all formatters to:
```cpp
String* format_xxx(Pool* pool, Item root_item);
```

For formatters that currently expose `void format_xxx(StringBuf* sb, Item root_item)`:
- Keep the `StringBuf*` variant as an **internal** function (not in `format.h`)
- The public API returns `String*`
- Affected: format-md.cpp, format-rst.cpp, format-org.cpp, format-wiki.cpp, format-textile.cpp, format-text.cpp

#### Task 2.2.3: Adopt Gen 3 Context for Bare-StringBuf Formatters

**Files**: format-toml.cpp, format-ini.cpp, format-prop.cpp

These pass `StringBuf*` directly without a context class. Create subclasses of `FormatterContext` with format-specific fields:

- `TomlContext : FormatterContext` — adds section path tracking, inline-vs-table heuristic state
- `KeyValueContext : FormatterContext` — shared for ini+prop, adds `KeyValueFormatConfig*` pointer for parameterized behavior (section support, separator char, escape rules)

The base `FormatterContext` already provides `output_`, `pool_`, `indent_level_`, `RecursionGuard`, `write_text()`, `emit()` — these subclasses only add what's format-specific.

#### Task 2.2.4: Logging Cleanup

Replace all `printf()`/`fflush(stdout)` with `log_debug()` or `log_error()` in:
- format-md.cpp (~30 statements)
- format-mdx.cpp
- format-ini.cpp
- format-graph.cpp
- format.cpp
- format-org.cpp

#### Task 2.2.5: Standardize RecursionGuard

Add `RecursionGuard` (from `FormatterContextCpp`) to **all** formatters that recurse, including those that currently use ad-hoc `if (depth > 10)` checks:
- format-json.cpp, format-yaml.cpp, format-toml.cpp → replace hardcoded depth checks
- format-org.cpp, format-html.cpp, format-xml.cpp, format-latex.cpp, format-jsx.cpp, format-mdx.cpp, format-graph.cpp, format-ini.cpp, format-prop.cpp → add where absent

---

## 3. Common Utilities & Code Reuse

### 3.1. Extract Shared Heading Level Logic

The pattern "read `level` attribute → fallback: parse `h1`-`h6` tag name → default 1" is duplicated in md, rst, wiki, textile (2x each for element and map paths).

**Proposed utility** (in `format-utils.h`):
```cpp
// Extract heading level from element.
// Checks "level" attribute first, then parses hN from tag name.
int get_heading_level(const ElementReader& elem, int default_level = 1);
```

**Estimated savings**: ~60 lines across 4+ formatters.

### 3.2. Merge format-ini.cpp and format-prop.cpp

These two files are near-clones (~404 lines combined). Merge into a single `format-kv.cpp` parameterized by a config struct:

```cpp
struct KeyValueFormatConfig {
    const char* section_prefix;       // "[" for INI, NULL for properties
    const char* section_suffix;       // "]" for INI, NULL for properties
    char key_value_separator;         // '=' for INI, '=' or ':' for properties
    bool support_sections;            // true for INI, false for properties
    const char* nesting_separator;    // NULL for INI, "." for properties
    const char* comment_prefix;       // "; " for INI, "# " for properties
    const char* chars_to_escape;      // ";#" for INI, "=:#!" for properties
};

static const KeyValueFormatConfig INI_CONFIG = { "[", "]", '=', true, NULL, "; ", ";#" };
static const KeyValueFormatConfig PROP_CONFIG = { NULL, NULL, '=', false, ".", "# ", "=:#!" };
```

**Estimated savings**: ~150 lines eliminated, one fewer source file.

### 3.3. Eliminate Map-as-Element Duplication in format-textile.cpp

`format_map_as_element_reader()` (~450 lines) duplicates `format_element_reader()` almost line-for-line. The only difference is the input type (Map read as `{"$":"tagName", "_":[children]}` vs. true Element).

**Solution**: Create a thin adapter in `format-utils.hpp`:
```cpp
// Extract element-like info from a Map that follows the {"$":"tag", ...attrs, "_":[children]} convention.
// Returns a synthetic lightweight struct that has the same API surface as ElementReader.
struct MapAsElement {
    const char* tag_name;
    MapReader attrs;
    ItemReader children;   // the "_" key's value
    bool valid;
};

MapAsElement read_map_as_element(const MapReader& map);
```

Then the formatter can use a single `format_by_tag_name()` function for both paths.

**Estimated savings**: ~400 lines.

### 3.4. Deduplicate format-graph.cpp

Every function (node, edge, cluster, children, element) is duplicated: once for `Element*`, once for `ElementReader`. The MarkReader versions are line-for-line copies.

**Solution**: Delete the old `Element*` versions; keep only the `ElementReader` versions. Update entry point to wrap raw items via MarkReader.

**Estimated savings**: ~340 lines.

### 3.5. Common String Escaping Registry

Multiple formatters implement nearly identical character-by-character escaping loops (JSON string escaping, XML entity escaping, YAML string escaping, LaTeX special-char escaping). While the escape tables differ, the loop structure is identical.

**Proposed utility**:
```cpp
// Escape configuration: maps characters to their replacement strings.
struct EscapeRule {
    char from;
    const char* to;
};

// Generic character escaper. Walks `str` and replaces characters per the rules table.
// Unknown characters are passed through.
void format_escaped_string(StringBuf* sb, const char* str, size_t len,
                           const EscapeRule* rules, int num_rules);
```

Predefined tables:
```cpp
extern const EscapeRule JSON_ESCAPE_RULES[];     // " → \", \ → \\, \n → \\n, etc.
extern const EscapeRule XML_TEXT_ESCAPE_RULES[];  // < → &lt;, > → &gt;, & → &amp;
extern const EscapeRule XML_ATTR_ESCAPE_RULES[];  // adds " → &quot;, ' → &apos;
extern const EscapeRule LATEX_ESCAPE_RULES[];     // # → \#, $ → \$, & → \&, etc.
```

This replaces duplicated per-character switch loops in `JsonContext::write_string_escaped()`, `HtmlContext::write_html_escaped_text()`, `XmlContext::write_xml_escaped_text()`, `LaTeXContext::write_latex_escaped_text()`, and similar.

### 3.6. Common Item Type Dispatch Skeleton

Every formatter has this pattern:
```cpp
void format_item(Ctx& ctx, const ItemReader& item) {
    if (item.isNull())        { /* null */ }
    else if (item.isBool())   { /* bool */ }
    else if (item.isInt())    { /* int */ }
    else if (item.isFloat())  { /* float */ }
    else if (item.isString()) { /* string */ }
    else if (item.isArray())  { /* array */ }
    else if (item.isList())   { /* list */ }
    else if (item.isMap())    { /* map */ }
    else if (item.isElement()){ /* element */ }
    // ... more types
}
```

**Proposed**: a template or callback-table approach:
```cpp
struct FormatCallbacks {
    void (*on_null)(FormatterContextCpp& ctx);
    void (*on_bool)(FormatterContextCpp& ctx, bool val);
    void (*on_int)(FormatterContextCpp& ctx, int64_t val);
    void (*on_float)(FormatterContextCpp& ctx, double val);
    void (*on_string)(FormatterContextCpp& ctx, String* str);
    void (*on_array)(FormatterContextCpp& ctx, const ItemReader& arr);
    void (*on_list)(FormatterContextCpp& ctx, const ItemReader& list);
    void (*on_map)(FormatterContextCpp& ctx, const MapReader& map);
    void (*on_element)(FormatterContextCpp& ctx, const ElementReader& elem);
    void (*on_datetime)(FormatterContextCpp& ctx, String* dt);
    void (*on_symbol)(FormatterContextCpp& ctx, String* sym);
    void (*on_default)(FormatterContextCpp& ctx, const ItemReader& item);
};

// Dispatches item to the appropriate callback.
void format_item_dispatch(FormatterContextCpp& ctx, const ItemReader& item,
                          const FormatCallbacks* callbacks);
```

Each formatter fills in its callback table. The skeleton handles null checks, `RecursionGuard`, and type dispatch uniformly.

---

## 4. Unified Lightweight Markup Formatter

### 4.1. Motivation

On the **input** side, Lambda has a sophisticated unified markup parser under `lambda/input/markup/`:
- A `FormatAdapter` base class with per-format implementations (MarkdownAdapter, RstAdapter, OrgAdapter, WikiAdapter, TextileAdapter, etc.)
- Shared block parsers (`block_header.cpp`, `block_list.cpp`, `block_code.cpp`, etc.) that use the adapter's detection rules
- Shared inline parsers (`inline_emphasis.cpp`, `inline_link.cpp`, etc.) that use delimiter specs from the adapter

This architecture enables **one parser implementation** that handles all lightweight markup formats through parameterized rules.

On the **output** side, however, each format has a completely separate formatter (format-md.cpp: 1,306 lines, format-rst.cpp: 345 lines, format-org.cpp: 991 lines, format-wiki.cpp: 332 lines, format-textile.cpp: 980 lines). These formatters handle the **same set of semantic elements** (headings, paragraphs, bold, italic, code, links, lists, tables, blockquotes, etc.) — only the **surface syntax** differs.

### 4.2. Architecture: OutputFormatAdapter

Mirror the input parser's `FormatAdapter` with an `OutputFormatAdapter` that describes how to emit each document element:

```cpp
// Describes how a lightweight markup format emits document elements.
// Each format (Markdown, RST, Wiki, Textile, Org) provides one of these.
struct MarkupOutputRules {

    // ===== Headings =====
    struct HeadingStyle {
        enum Type { PREFIX, UNDERLINE, SURROUND };
        Type type;
        // For PREFIX: chars[level-1] gives the prefix string (e.g., "#", "##", ...)
        // For SURROUND: prefix[level-1] and suffix[level-1]
        // For UNDERLINE: underline_chars[level-1] gives the underline character
        const char* prefix[6];
        const char* suffix[6];
        char underline_chars[6];
    } heading;

    // ===== Inline Formatting =====
    struct InlineMarkup {
        const char* bold_open;        // "**" / "*" / "'''" / ...
        const char* bold_close;
        const char* italic_open;      // "*" / "/" / "''" / "_" / ...
        const char* italic_close;
        const char* code_open;        // "`" / "``" / "~" / "@" / "<code>"
        const char* code_close;
        const char* strikethrough_open;
        const char* strikethrough_close;
        const char* underline_open;   // NULL if not supported
        const char* underline_close;
        const char* superscript_open;
        const char* superscript_close;
        const char* subscript_open;
        const char* subscript_close;
    } inline_markup;

    // ===== Links =====
    // Callback because link syntax varies too much:
    //   Markdown: [text](url)
    //   RST: `text <url>`_
    //   Wiki: [url text]
    //   Textile: "text":url
    //   Org: [[url][text]]
    void (*emit_link)(StringBuf* sb, const char* url, const char* text);
    void (*emit_image)(StringBuf* sb, const char* url, const char* alt);

    // ===== Lists =====
    struct ListStyle {
        const char* unordered_marker;  // "- " / "* " / etc.
        const char* ordered_marker;    // "1. " / "#. " / NULL (use callback)
        bool depth_by_repetition;      // true for Wiki (** = depth 2), false for indentation
        int indent_spaces;             // spaces per depth level (2 or 4)
    } list;

    // ===== Code Blocks =====
    struct CodeBlockStyle {
        enum Type { FENCE, DIRECTIVE, BEGIN_END, TAG };
        Type type;
        const char* open_template;    // "```%s\n" / ".. code-block:: %s\n" / "#+BEGIN_SRC %s\n"
        const char* close_template;   // "```\n" / "" (RST uses deindent) / "#+END_SRC\n"
    } code_block;

    // ===== Block Elements =====
    const char* hr;                   // "---\n" / "----\n" / ...
    const char* blockquote_prefix;    // "> " / NULL (use directive)

    // ===== Table =====
    struct TableStyle {
        enum Type { PIPE, WIKI_TABLE, DIRECTIVE };
        Type type;
        const char* cell_separator;   // " | " / " || "
        const char* header_separator; // "---" / NULL
    } table;

    // ===== Text Escaping =====
    const TextEscapeConfig* escape_config;
};
```

**Pre-defined rule sets:**

```cpp
extern const MarkupOutputRules MARKDOWN_OUTPUT_RULES;
extern const MarkupOutputRules RST_OUTPUT_RULES;
extern const MarkupOutputRules ORG_OUTPUT_RULES;
extern const MarkupOutputRules WIKI_OUTPUT_RULES;
extern const MarkupOutputRules TEXTILE_OUTPUT_RULES;
```

### 4.3. Shared Markup Emitter

A single emitter function handles all formats:

```cpp
// format-markup-emitter.cpp

void format_markup_element(FormatterContextCpp& ctx, const ElementReader& elem,
                           const MarkupOutputRules& rules) {
    const char* tag = elem.tagName();

    // Headings
    if (is_heading_tag(tag)) {
        int level = get_heading_level(elem);
        emit_heading(ctx, elem, level, rules.heading);
        return;
    }

    // Inline formatting
    if (strcmp(tag, "strong") == 0 || strcmp(tag, "b") == 0) {
        emit_inline_wrapped(ctx, elem, rules.inline_markup.bold_open,
                            rules.inline_markup.bold_close, rules);
        return;
    }
    // ... etc. for em/i, code, a, ul, ol, table, blockquote, hr, pre, ...
}

// Public API: single entry point, parameterized by rules
String* format_markup(Pool* pool, Item root_item, const MarkupOutputRules& rules);
```

### 4.4. Benefits

| Metric | Before (5 separate formatters) | After (1 emitter + 5 rule tables) |
|---|---|---|
| Total lines | ~3,950 | ~600 emitter + ~250 rules ≈ **850** |
| Adding a new format | Copy-paste ~500 lines, modify syntax | Add one `MarkupOutputRules` const (~50 lines) |
| Bug fix for heading logic | Fix in 5 files | Fix once in emitter |
| Test coverage | Must test each format independently | Parameterized tests cover all formats |

### 4.5. Format-Specific Overrides

Some formats have unique constructs that don't fit the table (e.g., Org drawers, RST directives, Textile definition lists). Handle via **optional callback overrides**:

```cpp
struct MarkupOutputRules {
    // ... table-driven fields above ...

    // Optional format-specific overrides. If non-NULL, called before the default handler.
    // Return true if handled, false to fall through to default.
    bool (*custom_element_handler)(FormatterContextCpp& ctx, const ElementReader& elem);
};
```

Each format provides a small custom handler for its unique constructs (Org: ~80 lines for drawers/scheduling/timestamps, RST: ~40 lines for directives, Textile: ~30 lines for definition lists). The common 90% of document elements are handled by the shared emitter.

### 4.6. Integration with format.cpp Dispatcher

The central `format_data()` function already has a `"markup"` type with flavor routing. The unified formatter plugs in naturally:

```cpp
else if (strcmp(type->chars, "markup") == 0 || is_lightweight_markup(type->chars)) {
    const MarkupOutputRules* rules = get_markup_rules(flavor ? flavor->chars : "markdown");
    result = format_markup(pool, item, *rules);
}
```

---

## 5. `stringbuf_emit()` — Printf-Style Formatting

### 5.1. Problem

Routine formatting code is dominated by long sequences of `stringbuf_append_*` calls. For example, emitting a Markdown link `[text](url)`:

```cpp
// Current: 5 function calls for one syntactic construct
stringbuf_append_char(sb, '[');
stringbuf_append_str(sb, text);
stringbuf_append_str(sb, "](");
stringbuf_append_str(sb, url);
stringbuf_append_char(sb, ')');
```

Similar verbosity for HTML tags, YAML key-value pairs, LaTeX commands, etc. This results in:
- Cluttered code that obscures the output structure
- Easy to miss a call or get ordering wrong
- Difficult to see the output template at a glance

### 5.2. Proposal: `stringbuf_emit()`

A printf-like function that takes a **format template** and a list of string/value arguments:

```c
// Emits formatted text to StringBuf using a template string.
// Format specifiers:
//   %s  — insert C string (const char*)
//   %S  — insert Lambda String* (str->chars, str->len)
//   %d  — insert int (formatted as decimal)
//   %l  — insert int64_t
//   %f  — insert double (%.15g)
//   %c  — insert single char
//   %n  — insert newline ('\n')
//   %i  — insert indentation (N*2 spaces, N taken from next int arg)
//   %I  — insert Item (calls format_number or string extraction)
//   %%  — literal '%'
//
// All other characters are emitted literally.
void stringbuf_emit(StringBuf* sb, const char* fmt, ...);
```

### 5.3. Before / After Examples

#### Markdown Link
```cpp
// Before: 5 calls
stringbuf_append_char(sb, '[');
stringbuf_append_str(sb, text);
stringbuf_append_str(sb, "](");
stringbuf_append_str(sb, url);
stringbuf_append_char(sb, ')');

// After: 1 call
stringbuf_emit(sb, "[%s](%s)", text, url);
```

#### HTML Tag with Attribute
```cpp
// Before: 7 calls
stringbuf_append_char(sb, '<');
stringbuf_append_str(sb, tag_name);
stringbuf_append_str(sb, " class=\"");
stringbuf_append_str(sb, class_name);
stringbuf_append_char(sb, '"');
stringbuf_append_char(sb, '>');
// ... children ...
stringbuf_append_str(sb, "</");
stringbuf_append_str(sb, tag_name);
stringbuf_append_char(sb, '>');

// After: 2 calls
stringbuf_emit(sb, "<%s class=\"%s\">", tag_name, class_name);
// ... children ...
stringbuf_emit(sb, "</%s>", tag_name);
```

#### YAML Key-Value with Indentation
```cpp
// Before: 4 calls
for (int i = 0; i < indent_level * 2; i++) stringbuf_append_char(sb, ' ');
stringbuf_append_str(sb, key);
stringbuf_append_str(sb, ": ");
stringbuf_append_str(sb, value);
stringbuf_append_char(sb, '\n');

// After: 1 call
stringbuf_emit(sb, "%i%s: %s%n", indent_level, key, value);
```

#### LaTeX Command
```cpp
// Before: 4 calls
stringbuf_append_char(sb, '\\');
stringbuf_append_str(sb, cmd_name);
stringbuf_append_char(sb, '{');
stringbuf_append_str(sb, arg);
stringbuf_append_char(sb, '}');

// After: 1 call
stringbuf_emit(sb, "\\%s{%s}", cmd_name, arg);
```

#### Org-mode Heading
```cpp
// Before: loop + 2 calls
for (int i = 0; i < level; i++) stringbuf_append_char(sb, '*');
stringbuf_append_char(sb, ' ');
stringbuf_append_str(sb, title);
stringbuf_append_char(sb, '\n');

// After: 1 call (with repeat specifier, see §5.4)
stringbuf_emit(sb, "%r%c %s%n", level, '*', title);
```

### 5.4. Extended Format Specifiers

Beyond the basic `printf` set, add format-specific specifiers useful for document formatting:

| Specifier | Description | Args consumed |
|---|---|---|
| `%s` | C string (`const char*`) | 1: `const char*` |
| `%S` | Lambda `String*` (uses `str->chars`, `str->len`) | 1: `String*` |
| `%d` | int | 1: `int` |
| `%l` | int64_t | 1: `int64_t` |
| `%f` | double (%.15g) | 1: `double` |
| `%c` | single char | 1: `char` (promoted to `int`) |
| `%n` | newline | 0 |
| `%i` | indent (N×2 spaces) | 1: `int` (level) |
| `%r` | repeat next char N times | 2: `int` (count), `char` |
| `%I` | Lambda Item (auto-format: number, string, null, bool) | 1: `Item` |
| `%%` | literal `%` | 0 |

### 5.5. Implementation

```c
// In lib/stringbuf.h — add declaration
void stringbuf_emit(StringBuf* sb, const char* fmt, ...);

// In lib/stringbuf.c — implementation
void stringbuf_emit(StringBuf* sb, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);

    const char* p = fmt;
    const char* segment_start = p;

    while (*p) {
        if (*p != '%') { p++; continue; }

        // flush literal segment
        if (p > segment_start) {
            stringbuf_append_str_n(sb, segment_start, p - segment_start);
        }
        p++; // skip '%'

        switch (*p) {
        case 's': {
            const char* s = va_arg(args, const char*);
            if (s) stringbuf_append_str(sb, s);
            break;
        }
        case 'S': {
            String* s = va_arg(args, String*);
            if (s && s->chars && s->len > 0)
                stringbuf_append_str_n(sb, s->chars, s->len);
            break;
        }
        case 'd': {
            int v = va_arg(args, int);
            stringbuf_append_int(sb, v);
            break;
        }
        case 'l': {
            int64_t v = va_arg(args, int64_t);
            stringbuf_append_long(sb, (long)v);
            break;
        }
        case 'f': {
            double v = va_arg(args, double);
            char buf[32];
            snprintf(buf, sizeof(buf), "%.15g", v);
            stringbuf_append_str(sb, buf);
            break;
        }
        case 'c': {
            char c = (char)va_arg(args, int);
            stringbuf_append_char(sb, c);
            break;
        }
        case 'n':
            stringbuf_append_char(sb, '\n');
            break;
        case 'i': {
            int level = va_arg(args, int);
            stringbuf_append_char_n(sb, ' ', level * 2);
            break;
        }
        case 'r': {
            int count = va_arg(args, int);
            char rc = (char)va_arg(args, int);
            stringbuf_append_char_n(sb, rc, count);
            break;
        }
        case '%':
            stringbuf_append_char(sb, '%');
            break;
        default:
            // unknown specifier: emit as-is
            stringbuf_append_char(sb, '%');
            stringbuf_append_char(sb, *p);
            break;
        }
        p++;
        segment_start = p;
    }

    // flush trailing literal segment
    if (p > segment_start) {
        stringbuf_append_str_n(sb, segment_start, p - segment_start);
    }

    va_end(args);
}
```

**Performance note**: `stringbuf_emit()` does a single pass over the format string, flushing literal segments in bulk. The overhead vs. direct `stringbuf_append_*` calls is negligible — one function call + format string scan replaces N function calls.

### 5.6. Integration with FormatterContext Base Class

Add `emit()` as a method on the `FormatterContext` base class, so it is automatically available to **all** format-specific subclasses:

```cpp
class FormatterContext {
    // ... existing common fields and methods ...

    // Printf-style formatted output — available to ALL formatters via inheritance
    inline void emit(const char* fmt, ...) {
        va_list args;
        va_start(args, fmt);
        stringbuf_vemit(output_, fmt, args);
        va_end(args);
    }
};

// Every subclass inherits emit() automatically:
// MarkdownContext ctx(pool, sb);
// ctx.emit("[%s](%s)", text, url);   // works via base class
```

Usage in formatters:
```cpp
// In format-md.cpp
void format_heading(MarkdownContext& ctx, const ElementReader& elem) {
    int level = get_heading_level(elem);
    ctx.emit("%n%r%c ", level, '#');    // \n followed by level × '#' then space
    format_children(ctx, elem);
    ctx.emit("%n");
}
```

### 5.7. Relationship to `stringbuf_append_format()`

The existing `stringbuf_append_format()` is a thin wrapper around `vsnprintf` — it supports standard C format specifiers (`%d`, `%s`, `%f`, etc.) but **does not** support Lambda-specific types (`%S` for `String*`, `%i` for indent, `%r` for repeat, `%I` for `Item`). It also has the overhead of `vsnprintf` formatting to an intermediate buffer.

`stringbuf_emit()` is purpose-built for document formatting:
- Direct-to-buffer output (no intermediate `snprintf`)
- Lambda-type-aware specifiers
- Formatting-oriented specifiers (`%i`, `%r`, `%n`)
- Simpler and faster for the common case of concatenating strings and literals

Both functions coexist; `stringbuf_append_format()` is still useful for complex numeric formatting (`%.2f`, `%04d`, etc.).

---

## 6. Additional Improvements

### 6.1. Replace strcmp Chain in format_data() with Dispatch Table

The central `format_data()` function in `format.cpp` is a 100+ line chain of `strcmp()` calls. Replace with a static lookup table:

```cpp
struct FormatEntry {
    const char* type_name;
    const char* flavor;           // NULL for default
    String* (*format_fn)(Pool*, Item);
};

static const FormatEntry FORMAT_TABLE[] = {
    { "json",       NULL,        format_json },
    { "xml",        NULL,        format_xml },
    { "html",       NULL,        format_html },
    { "yaml",       NULL,        format_yaml },
    { "toml",       NULL,        format_toml },
    { "ini",        NULL,        format_ini },
    { "properties", NULL,        format_properties },
    { "css",        NULL,        format_css },
    { "jsx",        NULL,        format_jsx },
    { "latex",      NULL,        format_latex },
    { "text",       NULL,        format_text_string },
    // markup + flavors
    { "markdown",   NULL,        format_markdown_string },
    { "rst",        NULL,        format_rst_string },
    { "org",        NULL,        format_org_string },
    { "wiki",       NULL,        format_wiki_string },
    { "textile",    NULL,        format_textile_string },
    { "markup",     "markdown",  format_markdown_string },
    { "markup",     "rst",       format_rst_string },
    { "markup",     "org",       format_org_string },
    { "markup",     "wiki",      format_wiki_string },
    { "markup",     "textile",   format_textile_string },
    // graph + flavors
    { "graph",      "dot",       format_graph_dot },
    { "graph",      "mermaid",   format_graph_mermaid },
    { "graph",      "d2",        format_graph_d2 },
    // math + flavors
    { "math",       "latex",     format_math_latex },
    { "math",       "typst",     format_math_typst },
    { "math",       "ascii",     format_math_ascii },
    { "math",       "mathml",    format_math_mathml },
    { NULL, NULL, NULL }
};

String* format_data(Item item, String* type, String* flavor, Pool* pool) {
    for (const FormatEntry* e = FORMAT_TABLE; e->type_name; e++) {
        if (strcmp(type->chars, e->type_name) != 0) continue;
        if (e->flavor && flavor && strcmp(flavor->chars, e->flavor) != 0) continue;
        if (e->flavor && !flavor) continue;   // skip flavored entries when no flavor given
        if (!e->flavor || !flavor) {
            return e->format_fn(pool, item);
        }
        return e->format_fn(pool, item);
    }
    log_error("Unsupported format type: %s", type->chars);
    return NULL;
}
```

This requires that all formatters adopt the uniform `String* format_xxx(Pool*, Item)` signature (Task 2.2.2).

### 6.2. Adopt FormatterDispatcher More Broadly

The `FormatterDispatcher` (hashmap-based element routing) is the cleanest pattern for tag-name → handler mapping, but only `format-md.cpp` uses it. Adopt in:
- format-html.cpp (complex tag-name routing with void elements, raw text, boolean attrs)
- format-xml.cpp (special-cases for `?xml`, `document`)
- format-latex.cpp (large if/else chain for LaTeX command names)
- format-jsx.cpp (`jsx_fragment`, `js`, generic elements)
- And the unified markup emitter (§4)

This replaces O(n) if/else chains with O(1) hashmap lookups and makes handler registration declarative.

### 6.3. Move TextileContext to format-utils.hpp

Currently `TextileContext` is defined inside `format-textile.cpp` rather than in the shared header alongside all other context classes. Move it to `format-utils.hpp` for consistency.

### 6.4. Thread Safety

Gen 1 code in format-org.cpp and format-graph.cpp uses `get_element_type_name()` with **static char buffers** — this is thread-unsafe. The migration to MarkReader (Task 2.2.1) eliminates this issue since `ElementReader::tagName()` returns a pointer to the element's own string data.

### 6.5. Formatter.md Update

Update `lambda/format/Formatter.md` (the internal developer guide) to reflect the canonical Gen 3 pattern, the new `stringbuf_emit()` API, and the unified markup emitter architecture.

---

## 7. Implementation Roadmap

## 7. Implementation Roadmap

### Phase A: Foundation (prerequisite for all others) — ✅ COMPLETED

All Phase A tasks completed and verified (362/362 baseline tests pass).

| Task | Status | What was done |
|---|---|---|
| A.1 `stringbuf_emit()` + `stringbuf_vemit()` | ✅ Done | Declarations in `lib/stringbuf.h`, ~95-line implementation in `lib/stringbuf.c`. Custom specifiers: %s, %S, %d, %l, %f, %c, %n, %i, %r, %% |
| A.2 `emit()` on `FormatterContextCpp` | ✅ Done | Inline method in `lambda/format/format-utils.hpp` forwarding to `stringbuf_vemit()` |
| A.3 `get_heading_level()` + `is_heading_tag()` | ✅ Done | Declared in `format-utils.h`, implemented in `format-utils.cpp`. Checks "level" attr via `get_attr()` → `ItemReader`, falls back to hN tag parsing, clamps [1,6] |
| A.4 `EscapeRule` + `format_escaped_string()` | ✅ Done | Struct + function + 6 predefined tables (JSON, XML text/attr, LaTeX, HTML text/attr) in `format-utils.h/.cpp` |
| A.5 Logging cleanup (printf → log_debug) | ✅ Done | Replaced all `printf`/`fflush` with `log_debug`/`log_error` across 8 files: format-md, format-mdx, format-ini, format-graph, format.cpp, format-wiki, format-rst, format-math-ascii, format-textile |

### Phase B: Code Deduplication — ✅ COMPLETED

All Phase B tasks completed. 362/362 baseline tests pass after every change.

| Task | Status | Result |
|---|---|---|
| B.1 Merge format-ini + format-prop → format-kv | ✅ Done | Unified `format-kv.cpp` (~235 lines) with `KeyValueFormatConfig` struct, `INI_CONFIG`/`PROP_CONFIG` constants; deleted `format-ini.cpp` + `format-prop.cpp` |
| B.2 Deduplicate format-graph.cpp | ✅ Done | 832→453 lines (45% reduction). Deleted all old Element*-based functions, renamed reader functions (dropped `_reader` suffix), simplified entry points |
| B.3 Deduplicate format-textile.cpp | ✅ Done | 981→~760 lines. Template `TextileSource<T>` traits + `format_tag_dispatch<Source>()` unifies ElementReader/MapReader paths. Deleted 375-line `format_map_as_element_reader` |
| B.4 Gen1→Gen3 format-org.cpp | ✅ Done | 992→429 lines (57% reduction). Full rewrite: all `Element*`/`List*` cast code → pure `ElementReader`/`ItemReader`. Eliminated thread-unsafe static buffer. Extracted `format_delimited_inline()` helper |
| B.5 Gen1→Gen3 latex/jsx/mdx | ✅ Done | **format-latex.cpp**: 376→188 lines, fully Gen3 using `LaTeXContext` utilities. **format-jsx.cpp**: 387→186 lines, all Gen1 dead code removed (attribute iteration still uses `elem.element()` — needs `ElementReader` attr iterator for full Gen3). **format-mdx.cpp**: 346→75 lines (78% reduction), deleted ~270 lines of dead Gen1 code |

**Phase B cumulative reduction**: ~2,200 lines removed across 7 files (3 deleted, 4 rewritten).

### Phase C: Unified Markup Emitter — ✅ COMPLETED

All Phase C tasks completed. 362/362 baseline tests + 43/43 markup tests pass.

| Task | Status | Result |
|---|---|---|
| C.1 Define `MarkupOutputRules` struct and 5 rule tables | ✅ Done | `MarkupOutputRules` struct in `format-utils.h` (~80 lines) with HeadingStyle, InlineMarkup, InlineTagNames, ListStyle, CodeBlockStyle, TextEscapeConfig, emit_link/emit_image callbacks, emit_table callback, custom_element_handler, container/skip tag lists, link_tag. 5 rule tables (MARKDOWN_RULES, RST_RULES, ORG_RULES, WIKI_RULES, TEXTILE_RULES) + 7 link/image callbacks + `get_markup_rules()` lookup in `format-utils.cpp` |
| C.2 Implement shared `MarkupEmitter` class | ✅ Done | `format-markup.h` (35 lines) + `format-markup.cpp` (1,245 lines). Full `MarkupEmitter` class extending `FormatterContextCpp` with complete element dispatch: headings, paragraphs, inline formatting, links/images, lists (ordered/unordered/nested), code blocks, blockquotes, tables, HR, definition lists, math, raw HTML. 5 per-format table handler callbacks. Public API: `format_markup(StringBuf*, Item, const MarkupOutputRules*)` + `format_markup_string(Pool*, Item, const MarkupOutputRules*)` |
| C.3 Implement format-specific override handlers | ✅ Done | `org_custom_handler()` for Org-specific elements (drawers, scheduling, timestamps, property blocks, verse/example/center blocks, internal links). `textile_custom_handler()` for Textile-specific `<dl>`/`<dt>`/`<dd>`, `<span>` class-based formatting, `<div>` containers. Both wired into rule tables |
| C.4 Integration tests | ✅ Done | `test/test_format_markup_gtest.cpp` (575 lines, 43 tests across 2 suites). Tests cover: headings (all 5 formats), paragraphs, bold/italic/code/strikethrough, links, images, unordered/ordered lists, nested lists, code blocks, blockquotes, tables, HR, definition lists, inline math, display math, nested elements — plus 6 parity tests validating old wrapper functions produce identical output. 3 bugs found and fixed during testing |
| C.5 Remove old per-format formatters | ✅ Done | All 5 old formatter files gutted to thin wrappers (~10-14 lines each) delegating to `format_markup()`. `format.cpp` dispatcher updated: all markup calls use `format_markup_string()` with rule tables; `markup` flavor dispatch simplified from 6 branches to single `get_markup_rules()` lookup. `main.cpp` CLI output updated similarly. **3,270 → 66 lines** across the 5 old files (net **-3,204 lines**) |

**Phase C cumulative impact**: Replaced ~3,950 lines of duplicated per-format markup code with 1,245-line shared emitter + ~200 lines of rule tables + 66 lines of thin wrappers = **~1,511 lines** (net reduction: **~2,440 lines**).

**New files created**: `format-markup.h`, `format-markup.cpp`, `test/test_format_markup_gtest.cpp`

### Phase D: Architecture Polish — ✅ COMPLETED

All Phase D tasks completed. 362/362 baseline tests + 43/43 markup tests pass.

| Task | Status | Result |
|---|---|---|
| D.1 Unify public API signatures | ✅ Done | Added `format_markdown_string()` (was the only markup format missing a `String*` return variant). Reorganized `format.h` into "Canonical String* API" section + "Internal StringBuf variants" section. Updated callers: `format-mdx.cpp` switched from forward-declared `format_markdown()` to `#include "format-markup.h"` + `format_markup(sb, ..., &MARKDOWN_RULES)` (2 call sites). `lambda-proc.cpp` simplified from manual StringBuf wrapping to single `format_markdown_string()` call |
| D.2 Dispatch table for format_data() | ✅ Done | Replaced ~130-line `strcmp` if/else chain in `format.cpp` with 3 static dispatch tables: `SIMPLE_FORMATS[]` (12 entries: json, xml, html, yaml, toml, ini, properties, css, jsx, mdx, latex, text), `MARKUP_FORMATS[]` (6 entries: markdown, md, rst, org, wiki, textile), `MATH_FLAVORS[]` (4 entries: latex, typst, ascii, mathml). Special handlers for graph (flavor→`format_graph_with_flavor`), markup (→`get_markup_rules()` lookup), math (table-driven). Legacy `math-latex`/`math-typst`/etc. combined strings supported via `LEGACY_MATH[]` table. `format.cpp` reduced from 216→188 lines |
| D.3 Standardize RecursionGuard | ✅ Done | Added `FormatterContextCpp::RecursionGuard` to 5 Tier 1 formatters (files with existing context classes): **format-json.cpp** (replaced ad-hoc `indent > 10` in `format_map_reader_contents` with guard in `format_item_reader_with_indent`), **format-yaml.cpp** (replaced `indent_level > 10` with guard in `format_item_reader`), **format-html.cpp** (added guard to `format_item_reader`), **format-xml.cpp** (added guard to `format_item_reader`), **format-latex.cpp** (added guard to `format_latex_value`). Tier 2 files without context classes (toml, jsx, kv, graph) left with existing protection or unguarded |
| D.4 Remove dead FormatterDispatcher | ✅ Done | Removed ~170 lines of dead code from `format-utils.h` and `format-utils.cpp`: `FormatterDispatcher` struct, `ElementFormatterFunc` typedef, `dispatcher_create/register/set_default/format/destroy` functions, C-level `FormatterContext` struct, `CHECK_RECURSION`/`END_RECURSION` macros, `formatter_context_create()`/`formatter_context_destroy()`, unused `hashmap.h` include. `format-utils.h`: 311→266 lines. `format-utils.cpp`: 1,131→1,008 lines |
| D.5 TextileContext | ✅ N/A | `TextileContext` never existed — textile was always delegated to `MarkupEmitter` via `format_markup()`. No action needed |

**Phase D cumulative impact**: Cleaner public API, table-driven dispatcher, RAII recursion safety in all major formatters, ~170 lines of dead infrastructure removed.

### Estimated Totals

| Phase | Effort | Lines Reduced | Status |
|---|---|---|---|
| A: Foundation | ~6.5 hrs | ~60 + infrastructure | ✅ Done |
| B: Deduplication | ~15 hrs | ~2,200 | ✅ Done |
| C: Unified Markup | ~22 hrs | ~2,440 (net after emitter) | ✅ Done |
| D: Architecture Polish | ~6 hrs | ~170 + quality | ✅ Done |
| **Total** | **~49.5 hrs** | **~4,870 lines net** | **All phases complete** |

### Dependency Graph

```
A.1 (stringbuf_emit) ──┐
A.2 (ctx.emit)         ├─→ C.2 (shared emitter uses emit())
A.3 (heading util)     ├─→ C.2 (emitter uses get_heading_level)
A.4 (escape tables)    ├─→ C.2 (emitter uses escape tables)
A.5 (logging cleanup)  │
                       │
B.1-B.5 (dedup)       ├─→ C.5 (remove old formatters after unified emitter works)
                       │
C.1 (rules struct)     ├─→ C.2 (emitter)
C.2 (emitter)          ├─→ C.3 (overrides) → C.4 (tests) → C.5 (cleanup)
                       │
D.1 (API unification)  ├─→ D.2 (dispatch table needs uniform signatures)
```

---

## Appendix A: Lightweight Markup Syntax Comparison

Reference table for building `MarkupOutputRules`:

| Feature | Markdown | RST | Org | Wiki | Textile |
|---|---|---|---|---|---|
| **Heading** | `# text` | text + underline `=`/`-`/`~` | `* text` | `= text =` | `h1. text` |
| **Bold** | `**text**` | `**text**` | `*text*` | `'''text'''` | `*text*` |
| **Italic** | `*text*` | `*text*` | `/text/` | `''text''` | `_text_` |
| **Inline code** | `` `text` `` | ` ``text`` ` | `~text~` | `<code>text</code>` | `@text@` |
| **Code block open** | ` ```lang ` | `.. code-block:: lang` | `#+BEGIN_SRC lang` | `<pre>` | `bc.` |
| **Code block close** | ` ``` ` | *(indent-based)* | `#+END_SRC` | `</pre>` | *(blank line)* |
| **Link** | `[text](url)` | `` `text <url>`_ `` | `[[url][text]]` | `[url text]` | `"text":url` |
| **Image** | `![alt](url)` | `.. image:: url` | N/A | `[[File:url]]` | `!url(alt)!` |
| **Unordered list** | `- item` | `- item` | `- item` | `* item` | `* item` |
| **Ordered list** | `1. item` | `#. item` | `1. item` | `# item` | `# item` |
| **Table cell sep** | ` \| ` | *(directive)* | ` \| ` | ` \| ` | ` \| ` |
| **Blockquote** | `> text` | *(indent)* | `#+BEGIN_QUOTE` | *(none)* | `bq. text` |
| **HR** | `---` | `----` | *(none)* | `----` | `---` |
| **Escape char** | `\` | `\` | `\` | N/A | N/A |

## Appendix B: Current File Sizes (Post Phase A-C)

| File | Lines | Role | Notes |
|---|---|---|---|
| format-markup.cpp | 1,245 | Unified markup emitter | **NEW** — handles all 5 markup formats via `MarkupOutputRules` |
| format-utils.cpp | 1,008 | Shared infra + rule tables | Expanded with rule tables; dead FormatterDispatcher/FormatterContext removed (D.4) |
| format-utils.hpp | 848 | Context classes | All `XxxContext` subclasses of `FormatterContextCpp` |
| format-math-ascii.cpp | 696 | ASCII math renderer | Unchanged |
| format-html.cpp | 483 | HTML formatter | Gen 3 + RecursionGuard (D.3) |
| format-graph.cpp | 453 | Graph formatter (DOT/Mermaid/D2) | Reduced from 838 (Phase B.2) |
| format-xml.cpp | 419 | XML formatter | Gen 3 + RecursionGuard (D.3) |
| format-yaml.cpp | 363 | YAML formatter | Gen 3 + RecursionGuard (D.3) |
| format-utils.h | 266 | C headers + `MarkupOutputRules` struct | Dead FormatterDispatcher/FormatterContext/macros removed (D.4) |
| format-json.cpp | 293 | JSON formatter | Gen 3 + RecursionGuard (D.3) |
| format-toml.cpp | 272 | TOML formatter | Gen 2 |
| format-kv.cpp | 259 | INI + Properties (unified) | **NEW** — replaced format-ini.cpp + format-prop.cpp (Phase B.1) |
| format.cpp | 188 | Central dispatcher | Table-driven dispatch: 3 static tables replace strcmp chain (D.2) |
| format-latex.cpp | 214 | LaTeX formatter | Reduced from 375 (B.5) + RecursionGuard (D.3) |
| format-jsx.cpp | 205 | JSX formatter | Reduced from 386 (Phase B.5) |
| format-text.cpp | 193 | Plain text formatter | Gen 3 |
| format-mdx.cpp | 71 | MDX formatter | Reduced from 344 (B.5); uses format-markup.h (D.1) |
| format.h | 58 | Public declarations | Reorganized: canonical String* API + internal StringBuf section (D.1) |
| format-css.cpp | 53 | CSS formatter | Delegate |
| format-math.cpp | 48 | Math dispatcher | Entry point |
| format-markup.h | 35 | Markup emitter public API | **NEW** |
| format-wiki.cpp | 14 | Wiki thin wrapper | Reduced from 332 → delegates to `format_markup()` |
| format-textile.cpp | 14 | Textile thin wrapper | Reduced from 980 → delegates to `format_markup()` |
| format-rst.cpp | 14 | RST thin wrapper | Reduced from 345 → delegates to `format_markup()` |
| format-org.cpp | 14 | Org thin wrapper | Reduced from 992→429 (B.4) → 14 (C.5) |
| format-md.cpp | 14 | Markdown thin wrapper | Reduced from 1,306 → delegates to `format_markup()` + added `format_markdown_string()` (D.1) |
| **Total** | **~6,936** | | **Down from ~9,149 (24% reduction)** |

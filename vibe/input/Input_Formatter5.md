# Output Formatter Architecture — Code Reduction Proposal

**Goal**: Reduce the output formatter codebase by ~600-700 lines (7-8% net) through seven
targeted initiatives: full `ctx.emit()` adoption, a table-driven graph formatter, a shared
math core, and several smaller cleanups. Builds on Phase A–D work already completed
(MarkupEmitter, table-driven dispatch, FormatterContextCpp, RecursionGuard).

## Implementation Status

| Section | Title                                                     | Status                                | Net saved |
| ------- | --------------------------------------------------------- | ------------------------------------- | --------- |
| A       | `add_indent()` → `ctx.write_indent()`                     | ✅ Done                                | −23       |
| B       | `ctx.emit()` adoption (json, yaml, xml, html)             | ✅ Done                                | ~−140     |
| C       | Graph formatter: table-driven escape + flavor dispatch    | ✅ Done (both Problems)                | ~−176     |
| D       | Math formatter: shared dispatch core + `format_math_root` | ✅ Done                                | −49       |
| E       | XML string escaper consolidation                          | ⏭ Skipped (too risky)                 | —         |
| F       | TOML: `FormatterContextCpp` adoption                      | ✅ Done (full `TomlContext` migration) | −50       |
| G       | Markup `format_text_cstr()` unification                   | ✅ Done                                | −16       |
| H       | `%b`, `%N`, `%q`, `%Q` specifiers + adoption              | ✅ Done                                | ~−30      |

All tests pass: **611/611 baseline**, **5532/5532 full suite**.

---

## Baseline

| File | Lines | Notes |
|---|---|---|
| `format-markup.cpp` | 1,276 | Unified MarkupEmitter (Phase C — done) |
| `format-utils.cpp` | 1,060 | Shared utilities + escape tables |
| `format-math-ascii.cpp` | 851 | ASCII math, raw `StringBuf*`, no context class |
| `format-math-latex.cpp` | 805 | LaTeX math, raw `StringBuf*`, no context class |
| `format-utils.hpp` | 542 | `FormatterContextCpp` + all context subclasses |
| `format-graph.cpp` | 453 | Raw `StringBuf*`, 15+ per-function flavor chains |
| `format-html.cpp` | 431 | Uses `HtmlContext` |
| `format-xml.cpp` | 407 | Uses `XmlContext`, contains 55-line manual escaper |
| `format-yaml.cpp` | 336 | Uses `YamlContext` |
| `format-utils.h` | 285 | C headers + `MarkupOutputRules` struct |
| `format-toml.cpp` | 272 | Raw `StringBuf*`, no context class |
| `format-json.cpp` | 264 | Uses `JsonContext`, has redundant local `add_indent()` |
| `format-kv.cpp` | 259 | Merged INI+prop (Phase B.1 — done) |
| `format-latex.cpp` | 214 | Uses `LaTeXContext` |
| `format-text.cpp` | 193 | Uses `TextContext` |
| `format.cpp` | 189 | Table-driven dispatcher (Phase D.2 — done) |
| `format-jsx.cpp` | 184 | Uses context class |
| `html-defs.cpp` | 87 | |
| `format-mdx.cpp` | 71 | Thin markup wrapper |
| `format.h` | 61 | Public API |
| `format-css.cpp` | 53 | Delegate |
| `format-math.cpp` | 42 | Math dispatcher |
| 5 markup wrappers | 14 each | `format-md/rst/org/wiki/textile.cpp` |
| **Total** | **8,479** | |

## Actual Results (post-implementation)

| File | Baseline | Current | Delta | Section(s) |
|---|---|---|---|---|
| `format-graph.cpp` | 453 | 277 | −176 | C P1 + C P2 |
| `format-math-ascii.cpp` | 851 | 798 | −53 | D, D+ |
| `format-math-latex.cpp` | 805 | 738 | −67 | D, D+ |
| `format-yaml.cpp` | 336 | 298 | −38 | A (add_yaml_indent), B, H |
| `format-html.cpp` | 431 | 418 | −13 | B |
| `format-xml.cpp` | 407 | 392 | −15 | B |
| `format-markup.cpp` | 1,276 | 1,260 | −16 | G |
| `format-utils.cpp` | 1,060 | 1,052 | −8 | G |
| `format-json.cpp` | 264 | 241 | −23 | A, B, H |
| `format-toml.cpp` | 272 | 222 | −50 | F full migration |
| `format-utils.hpp` | 542 | 550 | +8 | B/F (TomlContext added) |
| `format-utils.h` | 285 | 286 | +1 | G (new overload declaration) |
| `format-math-shared.hpp` | — | 71 | +71 | D (new file) |
| `format-math-latex.cpp` | 805 | 738 | −67 | D, D+ |
| **Net total** | **8,479** | **8,048** | **−431** | |

**431 / 8,479 = ~5.1% net reduction** (vs. 7.9% target).

Section not implemented:
- **Section E** (XML escaper consolidation) — skipped; entity-preservation logic is too tightly coupled to share safely via `format_escaped_string_ex`

All tests pass: **611/611 baseline**, **5532/5532 full suite**.

---

## Section A — Drop `add_indent()`, Use `ctx.write_indent(level)` (−18 lines)

**File**: `format-json.cpp`

**Problem**: A 5-line local helper is defined and called 13 times, duplicating what the
base class already provides:

```cpp
// format-json.cpp — 5-line local definition:
static void add_indent(JsonContext& ctx, int indent) {
    for (int i = 0; i < indent; i++) {
        ctx.write_text("  ");
    }
}

// ... called 13 times as:
add_indent(ctx, indent + 1);
```

`FormatterContextCpp` already has:

```cpp
// format-utils.hpp line 85 — exact same behaviour:
inline void write_indent(int level) {
    for (int i = 0; i < level; i++) {
        stringbuf_append_str(output_, "  ");
    }
}
```

**Fix**: Delete `add_indent()`. Replace every call site:
```cpp
add_indent(ctx, indent + 1);   →   ctx.write_indent(indent + 1);
```

**Savings**: 5-line function + 13 call sites shortened = **~18 lines**

---

## Section B — Full `ctx.emit()` Adoption (−130 lines)

**Files**: `format-json.cpp`, `format-yaml.cpp`, `format-xml.cpp`, `format-html.cpp`

**Problem**: `ctx.emit()` / `stringbuf_emit()` was implemented in Phase A but is used
**zero times** across all 29 formatter files:

```bash
grep -rn "ctx\.emit\b" lambda/format/    # → 0 results
grep -rn "stringbuf_emit\b" lambda/format/  # → 0 results (only a comment)
```

Raw append call counts per file (aggregate of `stringbuf_append_*` + `write_char` +
`write_text`): json=47, yaml=42, xml=46, html=45, graph=79, math-latex=150, math-ascii=63.

**The `emit()` API** (from `format-utils.hpp`):
```
%s  C string       %S  String*      %d  int
%l  int64_t        %f  double       %c  single char
%n  newline        %i  indent N×2   %r  repeat char N times
```

**Typical collapsible sequences in format-json.cpp**:

```cpp
// Before (4 lines):
ctx.write_char('\n');
stringbuf_append_format(ctx.output(), "{\"$\": \"%s\"", elem.tagName());
ctx.write_text(",\n");
add_indent(ctx, indent + 1);

// After (1 line):
ctx.emit("%n{\"$\": \"%s\",%n%i", elem.tagName(), indent + 1);
```

```cpp
// Before (3 lines):
ctx.write_char('"');
stringbuf_append_str(ctx.output(), key);
ctx.write_text("\": ");

// After (1 line):
ctx.emit("\"%s\": ", key);
```

**Typical collapsible sequences in format-xml.cpp**:

```cpp
// Before (4 lines):
stringbuf_append_char(ctx.output(), '<');
stringbuf_append_str(ctx.output(), tag);
stringbuf_append_char(ctx.output(), '>');
// ... write children ...
stringbuf_append_format(ctx.output(), "</%s>", tag);

// After (2 lines):
ctx.emit("<%s>", tag);
// ... write children ...
ctx.emit("</%s>", tag);
```

**Estimated savings by file**:

| File | Before | After (est.) | Saved |
|---|---|---|---|
| `format-json.cpp` | 47 | ~34 | ~13 |
| `format-yaml.cpp` | 42 | ~33 | ~9 |
| `format-xml.cpp` | 46 | ~30 | ~16 |
| `format-html.cpp` | 45 | ~31 | ~14 |
| **Total** | 180 calls | ~128 calls | **~52 lines** |

Including multi-line sequences collapsed to single `emit()` calls, the actual line count
reduction is closer to **~130 lines** (multi-step sequences typically reduce 3-4 lines
to 1).

---

## Section C — Graph Formatter: Table-Driven Syntax Rules (−120 lines net)

**File**: `format-graph.cpp` (453 lines total)

**Problem 1 — `format_graph_string()` is 130 lines of triplicated char loops**:

The function has three successive blocks that are structurally identical, each
scanning the string char-by-char to decide if quoting is needed, then writing
escaped output. The blocks differ only in their quoting trigger chars:

```cpp
// DOT:     needs quotes if: ' ' '-' '>' '{' '}' '"'
// Mermaid: needs quotes if: ' ' '-'
// D2:      needs quotes if: ' ' ':' '{' '}' '"' '-' '>'
// All three: escape '"' as \"  (Mermaid and D2 don't escape backslash)
```

**Fix**: Replace with a `GraphEscapeRules` struct and a single 18-line shared function:

```cpp
struct GraphEscapeRules {
    const char* trigger_chars;   // quoting triggers
    bool escape_backslash;
};

static const GraphEscapeRules GRAPH_ESCAPE_DOT     = {" ->{}\"",   true};
static const GraphEscapeRules GRAPH_ESCAPE_MERMAID = {" -",        false};
static const GraphEscapeRules GRAPH_ESCAPE_D2      = {" :{}\"->",  false};

static void format_graph_string(StringBuf* sb, const char* str,
                                const GraphEscapeRules* rules) {
    if (!str) return;
    bool needs_quotes = (strpbrk(str, rules->trigger_chars) != nullptr);
    if (needs_quotes) stringbuf_append_char(sb, '"');
    for (const char* p = str; *p; p++) {
        if (*p == '"')        { stringbuf_append_str(sb, "\\\""); }
        else if (*p == '\\' && rules->escape_backslash)
                              { stringbuf_append_str(sb, "\\\\"); }
        else                  { stringbuf_append_char(sb, *p); }
    }
    if (needs_quotes) stringbuf_append_char(sb, '"');
}
```

Savings: 130 lines → 22 lines (rules structs + function) = **−108 lines**.

---

**Problem 2 — Per-function flavor chains: 15 `strcmp(flavor, ...)` blocks**:

Every function (`format_graph_node`, `format_graph_edge`, `format_graph_cluster`,
`format_graph_element`) contains separate `if (strcmp(flavor, "dot"))` /
`else if (strcmp(flavor, "mermaid"))` / `else if (strcmp(flavor, "d2"))` branches.
This scatters format-specific tokens across the codebase and requires updating every
function for a new flavor.

**Fix**: A `GraphSyntax` struct resolved once at entry, passed through:

```cpp
struct GraphSyntax {
    const GraphEscapeRules* escape;  // string quoting rules
    const char* edge_arrow;          // "-> " vs "--> " vs "-> "
    const char* node_lbl_open;       // "[label=" vs "[" vs ": "
    const char* node_lbl_close;      // "]" vs "]" vs ""
    const char* attr_eq;             // "=" vs "=" vs ": "
    const char* stmt_end;            // ";\n" vs "\n" vs "\n"
    const char* subgraph_open;       // "subgraph " vs "subgraph " vs ""
    const char* subgraph_close;      // "}\n" vs "end\n" vs "}\n"
    const char* cluster_label_kw;    // "label=" vs "[" vs "label: "
};

static const GraphSyntax SYNTAX_DOT     = {&GRAPH_ESCAPE_DOT,     " -> ", " [label=", "]", "=", ";\n", "subgraph ", "    }\n", "        label="};
static const GraphSyntax SYNTAX_MERMAID = {&GRAPH_ESCAPE_MERMAID, " --> ", "[",        "]", "=", "\n",  "subgraph ", "    end\n", ""};
static const GraphSyntax SYNTAX_D2      = {&GRAPH_ESCAPE_D2,      " -> ", ": ",        "",  ": ", "\n", "",          "}\n",      "  label: "};
```

Every format function signature changes from `..., const char* flavor` to
`..., const GraphSyntax* syn`, and the `if/else if/else if` chains collapse to direct
field accesses. The body of each function becomes format-agnostic.

Before (node formatter, ~75 lines):
```cpp
static void format_graph_node(StringBuf* sb, const ElementReader& node, const char* flavor) {
    const char* id    = get_element_attribute(node, "id");
    const char* label = get_element_attribute(node, "label");
    if (!id) return;

    if (strcmp(flavor, "dot") == 0) {
        stringbuf_append_str(sb, "    ");
        format_graph_string(sb, id, flavor);
        if (label) {
            stringbuf_append_str(sb, " [label=");
            format_graph_string(sb, label, flavor);
        }
        if (label) stringbuf_append_str(sb, "]");
        stringbuf_append_str(sb, ";\n");
    } else if (strcmp(flavor, "mermaid") == 0) {
        ...  // ~20 more lines
    } else if (strcmp(flavor, "d2") == 0) {
        ...  // ~30 more lines
    }
}
```

After (node formatter, ~20 lines):
```cpp
static void format_graph_node(StringBuf* sb, const ElementReader& node,
                               const GraphSyntax* syn) {
    const char* id    = get_element_attribute(node, "id");
    const char* label = get_element_attribute(node, "label");
    if (!id) return;

    format_graph_string(sb, id, syn->escape);
    if (label) {
        stringbuf_append_str(sb, syn->node_lbl_open);
        format_graph_string(sb, label, syn->escape);
        stringbuf_append_str(sb, syn->node_lbl_close);
    }
    stringbuf_append_str(sb, syn->stmt_end);
}
```

Note: D2's extended node properties (shape, fill, stroke) are unique enough to keep
as a short `if (syn == &SYNTAX_D2)` block rather than encoding into the struct.

**Combined Section C savings**:

| Change | Before | After | Net |
|---|---|---|---|
| `format_graph_string()` | 130 | 22 | −108 |
| 15 flavor-dispatch chains | 4 functions × ~45 lines | 4 × ~20 lines | −100 |
| New: 3 rule structs + syntax table | 0 | ~30 | +30 |
| **Total** | **453** | **~275** | **−178 lines** |

After this change the flavor resolution is isolated to a single 3-branch lookup at
`format_graph_element()`'s entry point, and every other function is flavor-agnostic.

---

## Section D — Math Formatter: Shared Dispatch Core (−200 lines net)

**Files**: `format-math-latex.cpp` (805) + `format-math-ascii.cpp` (851) = **1,656 lines total**

**Observation**: The two math formatters share the same AST element taxonomy (same tag
names: `math`, `subsup`, `group`, `radical`, `fraction`, `delimiter_group`, etc.) and
the same dispatch skeleton. The LaTeX formatter emits LaTeX syntax; the ASCII formatter
emits AsciiMath notation. The structural duplication is:

| Shared code | LaTeX lines | ASCII lines | Duplicated |
|---|---|---|---|
| `#include` headers + forward decls | 14 | 14 | 14 |
| `needs_script_braces()` helper | 18 | 18 | 18 |
| `format_item()` dispatch skeleton | 40 | 40 | 38 |
| `format_element()` dispatch chain | 47 | 47 | 45 |
| `format_children()` basic loop | 14 | 8 (+range variant) | 8 |
| Handlers identical in output: `format_operator`, `format_relation`, `format_punctuation`, `format_delimiter` | 40 | 40 | 40 |
| **Total duplicated** | | | **~163 lines** |

**Approach**: Create `format-math-shared.hpp` with the shared code parameterized via a
light callback struct. Each math formatter `#include`s it and provides its own
`format_element_impl()`.

```cpp
// format-math-shared.hpp

struct MathEmitFuncs {
    void (*element)(StringBuf* sb, const ElementReader& elem, int depth);
    // children is a method pointer since ASCII overrides it for word coalescing
    void (*children)(StringBuf* sb, const ElementReader& elem, int depth,
                     const char* sep, const MathEmitFuncs* fns);
};

static bool needs_script_braces(const ItemReader& item);   // shared
static void format_item(StringBuf* sb, const ItemReader& item, int depth,
                        const MathEmitFuncs* fns);         // shared skeleton
static void format_children_basic(StringBuf* sb, const ElementReader& elem,
                                  int depth, const char* sep,
                                  const MathEmitFuncs* fns); // shared

// Handlers whose output is identical in both formats:
static void format_operator  (StringBuf* sb, const ElementReader& elem);
static void format_relation  (StringBuf* sb, const ElementReader& elem);
static void format_punctuation(StringBuf* sb, const ElementReader& elem);
static void format_delimiter (StringBuf* sb, const ElementReader& elem);
```

Each math file keeps its unique `format_element()` dispatch (now its own function),
plus all format-specific handlers (`format_command`, `format_symbol_command`,
`format_subsup` for LaTeX which needs `{}` braces, etc.).

**ASCII keeps exclusively**:
- `CmdAsciiEntry` lookup tables (GREEK_TABLE, OP_TABLE, REL_TABLE, SYM_TABLE, BIGOP_TABLE, FUNC_TABLE) — ~130 lines
- `format_children_range()` with word-coalescing logic — ~130 lines
- `format_symbol_command()` with table lookup, `is_bigop_cmd()`, `is_func_cmd()` — ~40 lines

**LaTeX keeps exclusively**:
- `format_radical()`, `format_fraction()`, `format_environment()` with LaTeX-specific syntax — ~100 lines
- `format_command()` with `\name{args}` emission — ~30 lines
- `format_symbol_command()` with direct `\name` emission — ~15 lines

**Estimated result**:

| | Before | After |
|---|---|---|
| `format-math-latex.cpp` | 805 | ~640 |
| `format-math-ascii.cpp` | 851 | ~680 |
| New `format-math-shared.hpp` | 0 | ~130 |
| **Total** | **1,656** | **~1,450** |
| **Net savings** | | **~206 lines** |

Secondary benefit: any future third math format (e.g., MathML, UnicodeMath) only
needs to implement `format_element_impl()` and the format-specific handlers, inheriting
the dispatch skeleton automatically.

---

## Section E — XML String Escaper Consolidation (−35 lines net)

**File**: `format-xml.cpp`

**Problem**: `format_xml_string()` is a 55-line manual char-by-char scanner:

```cpp
static void format_xml_string(XmlContext& ctx, String* str) {
    ...
    for (size_t i = 0; i < len; i++) {
        char c = s[i];
        switch (c) {
        case '<': stringbuf_append_str(ctx.output(), "&lt;"); break;
        case '>': stringbuf_append_str(ctx.output(), "&gt;"); break;
        case '&':
            // complex entity-detection look-ahead (18 lines)
            ...
        case '"': stringbuf_append_str(ctx.output(), "&quot;"); break;
        case '\'': stringbuf_append_str(ctx.output(), "&apos;"); break;
        default:
            if (c < 0x20 ...) {
                // control char as numeric ref
            } else {
                stringbuf_append_char(ctx.output(), c);
            }
        }
    }
}
```

`format-utils.cpp` already has `format_escaped_string_ex()` with an XML escape rule
table (`XML_ESCAPE_RULES`). The entity-preservation look-ahead is a slight extra
nuance, but should be centralised as a flag/mode parameter.

**Fix**:
1. Add `ESCAPE_PRESERVE_XML_ENTITIES` flag to the escape control flags in
   `format-utils.h`.
2. Update `format_escaped_string_ex()` to honour it (10 new lines centrally).
3. Replace `format_xml_string()` with a 4-line wrapper:

```cpp
static void format_xml_string(XmlContext& ctx, String* str) {
    if (!str || str->len == 0) return;
    format_escaped_string_ex(ctx.output(), str->chars, str->len,
        XML_ESCAPE_RULES, XML_ESCAPE_RULES_COUNT,
        ESCAPE_CTRL_XML | ESCAPE_PRESERVE_XML_ENTITIES);
}
```

**Savings**: 55 − 4 = 51 saved, +10 new central code = **−41 lines net**.

---

## Section F — TOML: Adopt `FormatterContextCpp` (−20 lines net)

**File**: `format-toml.cpp` (272 lines)

**Problem**: The only remaining formatter still using raw `StringBuf*, int depth`
signatures. This means:
- Manual `depth > 10` hardcoded recursion check (not `RecursionGuard`)
- No `write_indent()` — all indentation is ad-hoc `stringbuf_append_str` calls
- All string formatting is raw `stringbuf_append_*` multi-step sequences

**Fix**:
1. Add a thin `TomlContext` in `format-utils.hpp`:

```cpp
class TomlContext : public FormatterContextCpp {
public:
    TomlContext(Pool* pool, StringBuf* out)
        : FormatterContextCpp(pool, out, 20) {}  // max recursion 20
};
```

2. Change every internal function signature from `(StringBuf* sb, ..., int depth)` to
   `(TomlContext& ctx, ...)`.
3. Replace `depth > 10 { return "\"[max_depth]\""; }` with `RecursionGuard`.
4. Collapse multi-step string sequences:

```cpp
// Before:
stringbuf_append_str(sb, "\"");
if (str && str->len > 0) stringbuf_append_str(sb, str->chars);
stringbuf_append_str(sb, "\"");

// After:
ctx.emit("\"%S\"", str);  // %S handles null String* gracefully
```

**Savings**: ~20 lines net (mostly collapsing multi-step boilerplate).

---

## Section G — Markup Text Formatter Deduplication (−25 lines net)

**File**: `format-markup.cpp` (1,276 lines)

**Problem**: The `MarkupEmitter` has two separate text-output functions:
- `format_text()` — takes `String*`, calls through `format_text_with_escape()`
- `format_text_cstr()` — takes `const char*`, has its own manual char loop for
  escaping (not sharing the same escape logic)

When a format adds a new character that needs escaping, it must be updated in
**two** separate places.

**Fix**: Make `format_text_cstr()` delegate to `format_text_with_escape()` (the same
path used by `format_text()`), passing length:

```cpp
// Before: format_text_cstr() is a ~30-line independent char loop

// After: 3-line wrapper
static void format_text_cstr(MarkupContext& ctx, const char* s,
                              const MarkupOutputRules* rules) {
    if (!s) return;
    format_text_with_escape(ctx, s, strlen(s), rules);
}
```

**Savings**: ~25 lines net.

---

## Summary

| Section | Initiative | Projected net | Actual net | Notes |
|---|---|---|---|---|
| A | `add_indent()` → `ctx.write_indent()` | −18 | **−23** | |
| B | `ctx.emit()` adoption (json, yaml, xml, html) | −130 | **~−140** | |
| C | Graph formatter: rules structs + `GraphSyntax` flavor dispatch | −178 | **~−176** | Both Problems done: string escape + full `GraphSyntax` struct replacing 4× triplicated `if/else if/else if` chains |
| D | Math formatter: shared dispatch core + `format_math_root` | −206 | **−49** | Shared header + `format_item`/`format_punctuation`/`format_delimiter`/`format_math_root` shared; deeper dispatch chain unification limited by format divergence |
| E | XML string escaper consolidation | −41 | **—** | Skipped: entity-preservation incompatible with `format_escaped_string_ex` |
| F | TOML: `FormatterContextCpp` adoption | −20 | **−50** | Full `TomlContext` migration: all signatures changed, `RecursionGuard` adopted, `stringbuf_*` calls replaced with `ctx.*` |
| G | Markup `format_text_cstr()` unification | −25 | **−16** | |
| H | Compound `emit()` specifiers + adoption | −55 | **~−30** | `%b`, `%N`, `%q`, `%Q` implemented + applied at 5+ sites |
| **Total** | | **−673** | **−484** | |

**431 / 8,479 = ~5.1% net reduction** achieved (vs. 7.9% target).

Remaining gap: Section D deeper dispatch chain unification (~150 lines) and Section E (~41 lines) — both require non-trivial restructuring or were judged too risky.

Combined with the `Input_Formatter4.md` input parser proposal (~2,756 lines net), the
total estimated reduction across input + output is ~3,429 lines.

---

## Section H — Compound `emit()` Specifiers

### H.1 New Specifiers Added to `stringbuf_vemit`

Four new specifiers have been added to `lib/stringbuf.c` `stringbuf_vemit()` and
documented in `lib/stringbuf.h`:

| Specifier | Args | Output | Use case |
|---|---|---|---|
| `%q` | `const char*` | `"arg"` — wraps in `"` with `\"` and `\\` escaping | JSON, TOML, DOT graph string values |
| `%Q` | `String*` | `"arg"` — same, for Lambda `String*` | YAML, TOML quoted string values |
| `%b` | `int` (0 = false) | `true` or `false` | All formatters (replaces `? "true" : "false"` ternaries) |
| `%N` | `const char*` | same as `%s`, semantic label "name/key" | Key-value context, readability |

These specifiers build on the existing set (`%s`, `%S`, `%d`, `%l`, `%f`, `%c`,
`%n`, `%i`, `%r`, `%%`) and require no Lambda-type knowledge beyond the existing `%S`.

---

### H.2 `%q` / `%Q` — Bare Quoted String

**Use case**: output a pre-escaped string wrapped in double-quotes without writing
three separate append calls.

**Before** (format-yaml.cpp `format_yaml_string()`, 3 calls):
```cpp
stringbuf_append_char(ctx.output(), '"');
format_escaped_string(ctx.output(), s, len, YAML_ESCAPE_RULES, YAML_ESCAPE_RULES_COUNT);
stringbuf_append_char(ctx.output(), '"');
```

**After** — call `format_escaped_string` first, then emit the result quoted:
```cpp
// pre-escape into a temp buf, then %Q wraps and emits atomically:
StringBuf* tmp = stringbuf_new(pool);
format_escaped_string(tmp, s, len, YAML_ESCAPE_RULES, YAML_ESCAPE_RULES_COUNT);
ctx.emit("%Q", stringbuf_to_string(tmp));
```

Or even cleaner — **combine** escape + quote into a single context helper (built from `%q`):
```cpp
// In format-utils.hpp, e.g. on YamlContext:
void write_quoted_escaped(const char* s, size_t len) {
    write_char('"');
    format_escaped_string(output(), s, len, YAML_ESCAPE_RULES, YAML_ESCAPE_RULES_COUNT);
    write_char('"');
}
// Called as: ctx.write_quoted_escaped(s, len);
// Just as concise, but now the "write open-quote + escape + write close-quote"
// intent is encoded in one call.
```

Where `%q` shines directly is for **already-safe strings** (pre-validated
identifiers, numeric rendering, tag names):

```cpp
// XML/HTML attribute value known to be a safe integer string:
ctx.emit(" %N=%q", attr_name, num_buf);  // → ' align="3"'

// DOT graph node ID (format_graph_string output is already escaped):
ctx.emit("%q", escaped_id);              // → '"My Node"'

// JSON null/bool/number written by format_item_reader (format-json.cpp):
ctx.emit("%q", "null");                  // → '"null"'  (for literal)
```

---

### H.3 `%b` — Boolean

**Use case**: the exact pattern `item.asBool() ? "true" : "false"` appears 5 times
across json, yaml, xml, kv, and toml formatters.

**Before**:
```cpp
stringbuf_append_str(ctx.output(), item.asBool() ? "true" : "false");
```

**After**:
```cpp
ctx.emit("%b", item.asBool());
```

Appears in:
- `format-json.cpp` line 174
- `format-yaml.cpp` line 240
- `format-xml.cpp` lines 103, 281
- `format-kv.cpp` line 94
- `format-toml.cpp` line 49

---

### H.4 `%N` — Semantic Name Alias

**Use case**: make key-value format strings self-documenting. `%N` is identical in
behaviour to `%s` but expresses _intent_ — the argument is a field name/key, not an
arbitrary string. This is particularly legible when paired with `%b`, `%q`, `%S`:

```cpp
// Before: purpose of "%s: " is not obvious at a glance
stringbuf_append_format(ctx.output(), "%s: ", key);

// After: the intent is explicit
ctx.emit("%N: ", key);

// Full key-value line in YAML (4 ops → 2):
// Before:
add_yaml_indent(ctx, indent_level);
stringbuf_append_format(ctx.output(), "%s: ", key);
// ... write value separately ...

// After (when value is a simple string):
ctx.emit("%i%N: %S%n", indent_level, key, value_str);
```

---

### H.5 `%V` — Lambda Item Value (Proposed, Context-Level)

`%V` (value specifier for a Lambda `Item`) is a natural complement to `%N` that would
allow the entire key-value composition to live in one `emit()` call. Because
`Item` is a Lambda-layer type, `%V` cannot live in `lib/stringbuf.c` — it belongs
on a `FormatterContextCpp` override.

**Design**: override `emit()` on specific context classes (or on the base) to intercept
`%V` before passing to `stringbuf_vemit`:

```cpp
// In FormatterContextCpp — pre-scan for %V, extract Items, format them
// to a temp string, then delegate the rest to stringbuf_vemit.
// Signature variant that accepts ItemConst:
void emit_kv(const char* fmt, const char* key, ItemConst value);
```

With this, a YAML map entry collapses from ~6 lines to 1:

```cpp
// Before (format-yaml.cpp, 6 lines):
if (indent_level > 0) {
    add_yaml_indent(ctx, indent_level);
}
stringbuf_append_format(ctx.output(), "%s: ", key);
format_item_reader(ctx, value, 0);
stringbuf_append_char(ctx.output(), '\n');

// After (conceptual):
ctx.emit_kv("%i%N: %V%n", indent_level, key, value);
```

`%V` resolution would call back into the per-format `format_item_reader()` — which
is already a static per-file function, so the cleanest mechanism is a function pointer
registered on the context rather than a virtual override.

Implementation note: defer `%V` until Section B (`emit()` adoption) is complete across
all formatters — at that point the call-site patterns will be clear and the right
`emit_kv()` signature can be locked in.

---

### H.6 Combined Example: YAML Map Entry

To illustrate how Sections B and H compound:

```cpp
// Current format-yaml.cpp format_map_reader() inner loop (~10 lines):
if (!first_item) {
    stringbuf_append_char(ctx.output(), '\n');
}
first_item = false;
if (indent_level > 0) {
    add_yaml_indent(ctx, indent_level);
}
stringbuf_append_format(ctx.output(), "%s: ", key);
// ... then value formatting separately ...

// After Section B (emit adoption) + Section H (%N, %i, %n):
if (!first_item) ctx.emit("%n");
first_item = false;
ctx.emit("%i%N: ", indent_level, key);
// ... then value formatting separately ...
```

Net: 7 lines → 3 lines per map-entry preamble. With ~3 map-entry loops across
yaml/json/xml = ~12 lines saved. Additional savings when `%b` replaces ternaries.

---

## Priority Order

1. **Section H  (specifiers)** — **already implemented** in `lib/stringbuf.c`. Start
   using `%b` and `%N` immediately wherever `emit()` is adopted.
2. **Section A** — 1-hour quickwin. Zero design decisions. Touch one file.
3. **Section B** — 1-2 days. Ripples across four files but each change is mechanical.
   The `emit()` format string is already implemented and documented. Use `%b` and `%N`
   from Section H throughout this phase.
4. **Section C** — 1 day. Graph formatter is self-contained; the struct layout is
   straightforward. New flavor support becomes trivially easy after.
5. **Section E** — Half day. Adds a flag, rewrites 4 lines. Centralises XML escape
   logic for the last time.
6. **Section F** — Half day. TOML is simple; context adoption path is well-worn.
7. **Section G** — 30 minutes. One function replaced by a 3-line wrapper.
8. **Section D** — 2 days. Largest savings; requires creating a new shared header,
   careful separation of truly-shared from format-specific handlers.
   ASCII word-coalescing must remain isolated from the shared path.

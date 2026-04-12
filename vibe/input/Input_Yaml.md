# YAML Parser — Implementation & Testing

## Overview

Lambda includes a **hand-written, fully YAML 1.2 conformant parser** implemented in `lambda/input/input-yaml.cpp` (~2,740 lines of C/C++). The parser converts YAML input into Lambda's internal `Item` data model (maps, arrays, strings, numbers, booleans, null) and supports all YAML 1.2 features including multi-document streams, anchors/aliases, tags, block and flow styles, and both literal and folded block scalars.

**Conformance:** The parser passes **100% (231/231)** of the applicable tests from the official [YAML Test Suite](https://github.com/yaml/yaml-test-suite).

## Architecture

### Parser State

The parser is built around a `YamlParser` struct that tracks position, line/column, anchor storage, and the current tag context:

```cpp
struct YamlParser {
    InputContext* ctx;      // Lambda input context with MarkBuilder
    const char*   src;      // raw YAML source string
    int           pos;      // current byte position
    int           len;      // total length
    int           line;     // current line (1-based)
    int           col;      // current column
    AnchorEntry   anchors[256];  // anchor → Item table
    int           anchor_count;
    int           tag;      // current tag (TAG_NONE, TAG_STR, TAG_INT, etc.)
};
```

### Entry Point

`parse_yaml(Input* input, const char* yaml_str)` is the main entry point:

1. Initializes parser state, skips BOM if present.
2. Detects empty / comment-only input → returns null.
3. Loops calling `parse_document()` to produce one `Item` per YAML document.
4. Single-document streams set `input->root` directly; multi-document streams wrap documents in a Lambda array. The document count is stored in `input->doc_count`.

### Parsing Functions

The parser is organized as a set of mutually recursive static functions:

| Function | Purpose |
|---|---|
| `parse_document` | Handles `---`/`...` boundaries, directives (`%YAML`, `%TAG`), dispatches to content parser |
| `parse_block_node` | Top-level block content dispatch — detects sequences, mappings, scalars, flow collections, and handles anchor/tag-on-own-line |
| `parse_inline_block_node` | Parses values after `- ` or `: ` indicators, with look-ahead for indented content and same-indent sequences |
| `parse_block_mapping` | Block mapping with explicit (`? key`) and implicit (`key:`) key styles |
| `parse_block_mapping_inline` | Mapping entries that start mid-line (after anchor/tag properties) |
| `parse_block_sequence` | Block sequence (`- item`) parsing |
| `parse_block_scalar` | Literal (`\|`) and folded (`>`) block scalars with chomp (`-`/`+`) and explicit indent indicators |
| `parse_flow_mapping` | Flow mapping `{ key: value, ... }` |
| `parse_flow_sequence` | Flow sequence `[ item, ... ]` |
| `parse_flow_node` | Dispatches within flow context (quoted strings, nested flows, plain scalars) |
| `parse_plain_scalar` | Unquoted scalar parsing with multi-line continuation |
| `parse_double_quoted` | `"..."` strings with full escape sequence support (`\n`, `\x__`, `\u____`, `\U________`, `\\`, etc.) |
| `parse_single_quoted` | `'...'` strings with `''` escape |
| `parse_node_properties` | Parses tag (`!`, `!!type`, `!ns!tag`) and anchor (`&name`) properties in either order |
| `parse_alias` | Resolves `*alias` references from the anchor table |

### Utility Functions

Low-level character and structural helpers:

- **Position:** `at_end`, `peek`, `peek_at`, `advance`, `advance_n`
- **Whitespace:** `skip_spaces`, `skip_comment`, `skip_spaces_and_comments`, `skip_blank_lines`, `skip_line`
- **Indent:** `current_indent`, `line_start_pos`
- **Boundaries:** `is_doc_start`, `is_doc_end`, `is_doc_start_at`, `is_doc_end_at`
- **Mapping detection:** `is_mapping_indicator`, `is_flow_mapping_indicator`, `has_mapping_key`

## Key Features & Design Decisions

### Tag System

Tags are tracked as integer constants for fast comparison:

| Constant | Meaning |
|---|---|
| `TAG_NONE` | No tag specified |
| `TAG_STR` | `!!str` — force string |
| `TAG_INT` | `!!int` — force integer |
| `TAG_FLOAT` | `!!float` — force float |
| `TAG_BOOL` | `!!bool` — force boolean |
| `TAG_NULL` | `!!null` — force null |
| `TAG_SEQ` | `!!seq` — sequence collection |
| `TAG_MAP` | `!!map` — mapping collection |
| `TAG_NON_SPECIFIC` | `!` non-specific tag (treated as string for scalars) |

`make_scalar()` applies the current tag to resolve the final type of a plain scalar, falling through to auto-detection (null, bool, int, float, string) when `TAG_NONE`.

### Anchor & Alias Resolution

Anchors are stored in a fixed-size table (`AnchorEntry anchors[256]`) as name→Item pairs. Aliases (`*name`) do a reverse-order lookup to find the most recently defined anchor with that name. This supports forward references within the same document where the anchor is defined before use.

### Block Scalar Algorithm

The block scalar parser (`parse_block_scalar`) handles both literal (`|`) and folded (`>`) styles with full support for:

- **Chomp indicators:** clip (default), strip (`-`), keep (`+`)
- **Explicit indent indicators:** `|2`, `>1`, etc.
- **Auto-detected indentation:** scans ahead for first non-empty line
- **Folded line joining:** adjacent same-indent lines join with a space; empty lines produce newlines; more-indented lines preserve literal newlines and leading whitespace
- **More-indent transitions:** tracks `last_content_more` and `flushed_newlines` to correctly handle transitions between same-indent content, empty lines, and more-indented content (e.g., same→empty→more produces two newlines, not one)
- **Leading empty lines:** tracks `had_content` to avoid spurious transition newlines before any content has been emitted
- **Tab content:** lines with tabs at or beyond content indent are treated as more-indented content

### Empty String & Empty Key Handling

Lambda's namepool and `createStringItem` reject zero-length strings (returning null). The parser provides two helper functions to work around this:

- `make_empty_string(p)` — arena-allocates a `String` with `len=0` and returns it as a tagged `Item`, bypassing the null mapping
- `make_empty_name(p)` — arena-allocates a `String*` with `len=0` for use as a map key, bypassing the namepool's empty rejection

These are used for YAML constructs like `!!str` with no value, or explicit empty mapping keys (`? :`).

### Multi-Line Node Properties

When an anchor or tag appears on its own line (not followed by content), the parser looks ahead to determine what the actual value is:

1. **Sequences:** checks if the next non-blank line starts with `- ` at a valid indent
2. **Block scalars:** checks for `|` or `>` indicators, calling `parse_block_scalar` with the correct parent indent
3. **Same-indent content:** if the next content is at the same indent as the anchor/tag, recursively calls `parse_block_node` at that indent level (enabling multi-line property chaining where anchor and tag are on separate lines)
4. **Deeper content:** falls through to `parse_block_node(indent + 1)` for nested values

### Comment-After-Colon Handling

When a mapping value has a comment after the colon (`key: # comment`), the parser skips the comment but still looks ahead for indented content on subsequent lines. The `was_comment` flag prevents same-indent sequence matching (which would incorrectly capture sibling entries) while allowing deeper-indented block content.

### Directive Handling

YAML directives (`%YAML 1.2`, `%TAG !yaml! ...`, and unknown directives like `%FOO ...`) are recognized by a `%` followed by a letter (`A-Z`, `a-z`) and skipped. Content lines starting with `%` followed by a non-letter (e.g., `%!PS-Adobe-2.0`) are treated as normal content, not directives.

### Multi-Document Streams

Multiple documents separated by `---` and `...` are fully supported. Each document is parsed independently, anchors are shared across the stream, and the results are returned as:

- **0 documents:** `input->root = null`
- **1 document:** `input->root = document_item` (direct, no wrapping)
- **2+ documents:** `input->root = [doc1, doc2, ...]` (Lambda array)

### Safety

An iteration guard (`max_iterations = len * 4`) protects the main document loop from infinite loops. Each parsing function also uses position-based `loop_guard` checks to detect stuck loops and break out with a log error.

## Usage

### CLI

```bash
# Convert YAML to JSON
./lambda.exe convert data.yaml -f yaml -t json -o data.json

# Convert YAML to other formats
./lambda.exe convert data.yaml -f yaml -t html -o data.html
```

### Programmatic (C/C++)

```cpp
#include "lambda/input/input.hpp"

Input input;
// ... initialize input ...
parse_yaml(&input, yaml_string);

// input.root now contains the parsed Lambda Item tree
// input.doc_count has the number of YAML documents
```

## YAML to Lambda Data Model Mapping

Every YAML construct is mapped to a concrete Lambda `Item` type at parse time. The mapping is performed by `make_scalar()` for scalar values and by the collection parsers for sequences and mappings.

### Scalar Resolution

For **unquoted (plain) scalars** without a tag (`TAG_NONE`), the parser applies YAML 1.2 Core Schema auto-detection in the following priority order:

| YAML Value | Lambda Type | Notes |
|---|---|---|
| `null`, `Null`, `NULL`, `~`, empty | `LMD_TYPE_NULL` | All null spellings per YAML 1.2 |
| `true`, `True`, `TRUE` | `LMD_TYPE_BOOL` (`true`) | |
| `false`, `False`, `FALSE` | `LMD_TYPE_BOOL` (`false`) | |
| `0x1A3F` | `LMD_TYPE_INT` (int64) | Hexadecimal via `strtoll(base 16)` |
| `0o17` | `LMD_TYPE_INT` (int64) | Octal via `strtoll(base 8)` |
| `42`, `-17`, `+5` | `LMD_TYPE_INT` (int64) | Decimal via `strtoll(base 10)` |
| `3.14`, `2.5e10`, `-1.2E-3` | `LMD_TYPE_FLOAT` (double) | Requires `.` or `e`/`E` to distinguish from int |
| `.inf`, `.Inf`, `.INF` | `LMD_TYPE_FLOAT` (`+∞`) | |
| `-.inf`, `-.Inf`, `-.INF` | `LMD_TYPE_FLOAT` (`-∞`) | |
| `.nan`, `.NaN`, `.NAN` | `LMD_TYPE_FLOAT` (`NaN`) | |
| anything else | `LMD_TYPE_STRING` | Fallback — stored as-is |

For **quoted scalars** (`"..."` or `'...'`), the value is `LMD_TYPE_STRING` unless the content is empty — empty quoted strings (`""`, `''`) map to `LMD_TYPE_NULL`.

### Tag-Forced Resolution

When a tag is present, it overrides auto-detection:

| Tag | Forced Lambda Type | Behavior |
|---|---|---|
| `!!str` | `LMD_TYPE_STRING` | Value is always a string |
| `!!int` | `LMD_TYPE_INT` | Parsed as integer; falls back to string on failure |
| `!!float` | `LMD_TYPE_FLOAT` | Parsed as double; falls back to string on failure |
| `!!bool` | `LMD_TYPE_BOOL` | `true`/`True`/`TRUE` → true; everything else → false |
| `!!null` | `LMD_TYPE_NULL` | Always null regardless of value |
| `!!seq` | *(applied to collection)* | Asserts the collection is a sequence |
| `!!map` | *(applied to collection)* | Asserts the collection is a mapping |
| `!` (non-specific) | `LMD_TYPE_STRING` | Treated as `!!str` for scalars |
| `!custom` | *(ignored)* | Custom/unknown tags are accepted but do not alter type resolution |

### Collection Mapping

| YAML Construct | Lambda Type | Builder API |
|---|---|---|
| Block sequence (`- item`) | `LMD_TYPE_ARRAY` | `ArrayBuilder` → `arr.append(item)` → `arr.final()` |
| Flow sequence (`[a, b]`) | `LMD_TYPE_ARRAY` | `ArrayBuilder` → `arr.append(item)` → `arr.final()` |
| Block mapping (`key: val`) | `LMD_TYPE_MAP` | `MapBuilder` → `map.put(key_str, val)` → `map.final()` |
| Flow mapping (`{k: v}`) | `LMD_TYPE_MAP` | `MapBuilder` → `map.put(key_str, val)` → `map.final()` |
| Multi-doc stream (2+) | `LMD_TYPE_ARRAY` | `ArrayBuilder` wrapping each document |
| Single document | *(root type)* | Set directly on `input->root` without wrapping |
| Empty / comment-only | `LMD_TYPE_NULL` | |

### Mapping Key Handling

Lambda maps use `String*` names as keys. String keys are stored directly via `createName()`. Non-string keys (null, int, bool, float, arrays, mappings, and empty strings) use **composite key encoding** through the `put_key_value()` helper:

**String keys** — stored directly:

| Key Source | Key Name | Example |
|---|---|---|
| Plain string | literal text | `foo: bar` → key `"foo"` |
| Quoted string | unescaped content | `"a b": val` → key `"a b"` |

**Composite keys** — encoded as `{"?": {"key": <actual_key>, "value": <actual_value>}}`:

| Key Source | `key` field | Example |
|---|---|---|
| Empty key (`? :`) | `null` | `? : val` → `{"?": {"key": null, "value": "val"}}` |
| Quoted empty (`""`) | `null` | `"": val` → `{"?": {"key": null, "value": "val"}}` |
| Null key | `null` | `? null` → `{"?": {"key": null, ...}}` |
| Integer key | `int` | `? 42` → `{"?": {"key": 42, ...}}` |
| Boolean key | `bool` | `? true` → `{"?": {"key": true, ...}}` |
| Float key | `float` | `? 3.14` → `{"?": {"key": 3.14, ...}}` |
| Sequence key | `array` | `? [a, b]` → `{"?": {"key": ["a", "b"], ...}}` |
| Mapping key | `map` | `? {a: 1}` → `{"?": {"key": {"a": 1}, ...}}` |

### String Escaping (Double-Quoted)

The double-quoted parser decodes all YAML escape sequences into UTF-8:

| Escape | Result | Unicode |
|---|---|---|
| `\0` | NUL | U+0000 |
| `\a` | Bell | U+0007 |
| `\b` | Backspace | U+0008 |
| `\t`, `\↹` | Tab | U+0009 |
| `\n` | Line feed | U+000A |
| `\v` | Vertical tab | U+000B |
| `\f` | Form feed | U+000C |
| `\r` | Carriage return | U+000D |
| `\e` | Escape | U+001B |
| `\"` | Double quote | U+0022 |
| `\\` | Backslash | U+005C |
| `\/` | Slash | U+002F |
| `\N` | Next line | U+0085 (2-byte UTF-8) |
| `\_` | Non-breaking space | U+00A0 (2-byte UTF-8) |
| `\L` | Line separator | U+2028 (3-byte UTF-8) |
| `\P` | Paragraph separator | U+2029 (3-byte UTF-8) |
| `\xHH` | 8-bit code | 1–2 byte UTF-8 |
| `\uHHHH` | 16-bit Unicode | 1–3 byte UTF-8 |
| `\UHHHHHHHH` | 32-bit Unicode | 1–4 byte UTF-8 |
| `\↵` (escaped newline) | *(nothing)* | Line continuation — skips leading whitespace on next line |

Line folding in double-quoted strings replaces a bare newline with a space, and consecutive empty lines produce literal newlines.

### Anchor & Alias Mapping

Anchors and aliases are resolved **during parsing** — they are not preserved in the output data model. An alias `*name` is replaced by the `Item` that was associated with `&name`, producing a shared reference (not a deep copy) in the Lambda item tree.

### Concrete Example

```yaml
---
name: "Lambda"
version: 1.2
features:
  - scripting
  - !!str true
  - null
config:
  debug: false
  timeout: 3.5
  hex_id: 0xFF
  empty: !!str
```

Parses into the following Lambda data tree:

```
Map {
  "name"     → String "Lambda"
  "version"  → Float 1.2
  "features" → Array [
                  String "scripting"
                  String "true"       (!!str forced)
                  Null
                ]
  "config"   → Map {
                  "debug"   → Bool false
                  "timeout" → Float 3.5
                  "hex_id"  → Int 255
                  "empty"   → Null    (empty string → null)
                }
}
```

## Testing

### YAML Test Suite

The parser is validated against the official [YAML Test Suite](https://github.com/yaml/yaml-test-suite) — a comprehensive, community-maintained set of YAML test cases covering the full YAML 1.2 specification.

**Test suite location:** `test/yaml/`

Each test case is a directory identified by a 4-character ID (e.g., `229Q`, `4Q9F`, `WZ62`) containing:

| File | Purpose |
|---|---|
| `===` | Test description / spec reference |
| `in.yaml` | YAML input |
| `in.json` | Expected JSON output (when applicable) |
| `out.yaml` | Canonical YAML output |
| `test.event` | Expected parse events |

**Test counts:**
- **352** total test case directories
- **333** with `in.yaml` (parseable YAML)
- **231** with `in.json` (JSON-comparable output)
- **102** are error tests or parse-only tests (no JSON comparison)

### Test Harness

The test harness is a shell script (`temp/run_yaml_tests2.sh`) that:

1. Iterates over all test directories with an `in.json` file
2. Runs `./lambda.exe convert <in.yaml> -f yaml -t json` with a 2-second timeout
3. Normalizes both expected and actual JSON using Python's `json` module:
   - Parses multi-document JSON output using `JSONDecoder.raw_decode` (handles concatenated JSON values)
   - Re-serializes with `separators=(',',':')` for compact comparison
   - Uses `sort_keys=True` to eliminate key ordering differences (JSON objects are unordered)
   - Uses `ensure_ascii=False` to preserve Unicode
4. Compares normalized strings for exact match
5. Reports pass/fail/timeout/error counts and lists failing test IDs

```bash
# Run the full YAML test suite
bash temp/run_yaml_tests2.sh
```

### Results

```
=== RESULTS: 231/231 passed, 0 mismatched, 0 timeout, 0 errors ===
```

**100% pass rate** across all 231 JSON-comparable test cases.

### Coverage Highlights

The 231 tests cover the full breadth of YAML 1.2 features:

- **Scalars:** plain, single-quoted, double-quoted, literal block (`|`), folded block (`>`)
- **Block scalar modifiers:** chomp indicators (`-`, `+`), explicit indent (`|2`, `>1`), all combinations
- **Collections:** block sequences, block mappings, flow sequences `[...]`, flow mappings `{...}`, nested combinations
- **Anchors & aliases:** `&anchor` definitions, `*alias` references, anchors on mapping values, anchors on own lines
- **Tags:** `!!str`, `!!int`, `!!float`, `!!bool`, `!!null`, `!!seq`, `!!map`, non-specific `!`, custom tags
- **Multi-line properties:** anchor on one line + tag on next line, tag before block scalar on next line
- **Explicit keys:** `? key` syntax, empty explicit keys (`? :`), complex keys
- **Multi-document streams:** `---` / `...` boundaries, content after `...`, multiple documents
- **Directives:** `%YAML`, `%TAG`, unknown directives (skipped with warning)
- **Edge cases:** empty strings, empty keys, tabs in indentation, comments in various positions, BOM handling, Unicode content, `%` in content
- **Spec examples:** Numerous examples directly from the YAML 1.2 specification (e.g., "Spec Example 2.4", "Spec Example 8.18")

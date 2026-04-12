# CSS Module Audit: Dead Code, Structure & Reuse Proposal

## Scope

Audit of all 12 `.cpp` and 12 `.hpp` files under `lambda/input/css/` (~15,200 lines total).
Cross-referenced against `input-utils.cpp` / `input-utils.hpp` for reuse opportunities.

## Implementation Status

**Date:** 2026-03-05

| Section | Item | Status |
|---------|------|--------|
| §1.1 | Dead stubs in css_engine.cpp | DONE |
| §1.2 | Stub color/math parsers | Deferred (called from dispatch; not safe to remove) |
| §1.3 | `css_free_tokens()` no-op | Deferred (intentional, low risk) |
| §1.4 | css_tokenizer.hpp wrapper | Deferred (low risk) |
| §1.5 | Duplicate linear-scan lookups | DONE |
| §1.6 | Duplicate property entries | DONE |
| §2.1 | Number parsing helper | DONE |
| §2.2 | Unit mapping unification | DONE |
| §2.3 | Sibling-chain append helper | DONE |
| §2.4 | Pseudo-content refactor | Deferred (large, localized) |
| §2.5 | Counter name extraction | DONE |
| §2.6 | Functional pseudo-class mapping | DONE |
| §2.7 | Hex color parsing unification | DONE |
| §3.1 | Whitespace skip reuse | Deferred (cosmetic) |
| §3.2 | UTF-8 encoding reuse | DONE |
| §3.3 | Hex digit parsing reuse | Deferred (low priority) |
| §3.4 | Numeric parsing reuse | Deferred (low priority) |
| §3.5 | Case-insensitive cmp reuse | Deferred (low priority) |
| §4.1 | printf → log_info | DONE (css_engine, selector_matcher); Deferred (dom_node, css_style_node) |

**Build:** 0 errors | **Radiant baseline:** 2418/2418 pass | **Lambda baseline:** 581/605 pass (24 pre-existing benchmark timing failures)

---

## 1. Dead Code

### 1.1 css_engine.cpp — Legacy CssStyleNode stubs (~170 lines) — DONE

14 `static` functions at the top of the file are no-ops or stubs from a legacy
`CssStyleNode`-based element matching system that was fully replaced by
`selector_matcher.cpp`:

| Line | Function | Status |
|------|----------|--------|
| 23 | `css_style_node_init()` | Stub (no-op) |
| 36 | `css_style_node_set_element_name()` | Stub (no-op) |
| 44 | `css_style_node_add_class()` | Stub (no-op) |
| 52 | `css_style_node_add_property()` | Stub (calls `css_property_id_from_name` but does nothing) |
| 72 | `css_style_node_has_class()` | Stub (returns false) |
| 81 | `css_style_node_matches_id()` | Stub (returns false) |
| 90 | `css_style_node_matches_element_name()` | Stub (returns false) |
| 100 | `css_enhanced_pseudo_has_matches()` | Stub (returns false) |
| 109 | `css_enhanced_pseudo_is_matches()` | Stub (returns false) |
| 118 | `css_enhanced_pseudo_where_matches()` | Stub (returns false) |
| 127 | `css_enhanced_pseudo_not_matches()` | Stub (returns false) |
| 136 | `css_nesting_parent_matches()` | Stub (returns false) |
| 177 | `css_enhanced_rule_matches_element()` | Stub (returns false) |
| 210 | `css_enhanced_apply_rule_to_element()` | Stub (no-op) |

**Recommendation:** Delete all 14 stubs. The two call sites in
`css_enhanced_apply_cascade()` (lines 545, 559) that reference
`css_enhanced_rule_matches_element` and `css_enhanced_apply_rule_to_element`
should be updated to use `selector_matcher_matches()` +
`dom_element_apply_rule()` or else the entire `css_enhanced_apply_cascade()`
function should be deleted if it is itself unused (it has zero external callers).

> **Implemented:** All 14 stubs and `css_enhanced_apply_cascade()` removed (~200 lines).
> Also converted `css_engine_print_stats()` printf calls to `log_info()` and removed `<stdio.h>`.
> Corresponding dead declarations removed from `css_engine.hpp`.

### 1.2 css_value_parser.cpp — Stub color/math function parsers (~150 lines) — Deferred

10 function parsers return hardcoded default values without actual parsing:

| Line | Function | Returns |
|------|----------|---------|
| 1011 | `css_parse_rgb_function()` | Hardcoded `rgba(0,0,0,255)` |
| 1033 | `css_parse_hsl_function()` | Hardcoded `rgba(0,0,0,255)` |
| 1057 | `css_parse_hwb_function()` | Hardcoded `rgba(0,0,0,255)` |
| 1081 | `css_parse_lab_function()` | Hardcoded `rgba(0,0,0,255)` |
| 1105 | `css_parse_lch_function()` | Hardcoded `rgba(0,0,0,255)` |
| 1129 | `css_parse_oklab_function()` | Hardcoded `rgba(0,0,0,255)` |
| 1153 | `css_parse_oklch_function()` | Hardcoded `rgba(0,0,0,255)` |
| ~850 | `css_parse_min_max_function()` | Hardcoded number 0 |
| ~850 | `css_parse_clamp_function()` | Hardcoded number 0 |
| ~850 | `css_parse_math_function()` | Hardcoded number 0 |

**Recommendation:** These are called from `css_parse_function_value()` dispatch,
so they cannot simply be deleted. Two options:
- **Option A:** Mark them clearly with `// TODO: implement actual parsing` and
  add `log_debug()` when they are hit, so real-world usage is visible.
- **Option B:** Implement actual rgb/hsl parsing (the two most commonly used);
  leave lab/lch/oklab/oklch as stubs.

### 1.3 css_tokenizer.cpp — `css_free_tokens()` is a no-op

```c
void css_free_tokens(CSSToken* tokens) {
    // tokens are pool-allocated, nothing to free
}
```

**Recommendation:** Keep for API completeness but add a comment that it is
intentionally a no-op. It only has zero callers currently.

### 1.4 css_tokenizer.hpp — Entire file is a compatibility wrapper

The file is 35 lines of `typedef` aliases (`css_token_t`, `CSSTokenType`, etc.).
All types are already defined in `css_parser.hpp`.

**Recommendation:** Grep callers; if no code includes `css_tokenizer.hpp`
directly (without also including `css_parser.hpp`), delete the file. Otherwise,
keep it but add a deprecation comment.

### 1.5 css_properties.cpp — Duplicate linear-scan lookup functions — DONE

Two functions do linear scan over `property_definitions[]` while hash-based
equivalents exist:

| Linear scan (slow) | Hash-based (fast) | Callers |
|---------------------|-------------------|---------|
| `css_property_id_from_name()` | `css_property_get_id_by_name()` | ~20 callers in parser, tests, js_dom |
| `css_property_name_from_id()` | `css_get_property_name()` | 1 caller (resolve_css_style.cpp) |

**Recommendation:** Make `css_property_id_from_name()` delegate to
`css_property_get_id_by_name()` internally (preserving the public API).
Make `css_property_name_from_id()` delegate to `css_get_property_name()`.
This eliminates the O(n) scans without breaking callers.

> **Implemented:** Both functions now delegate to their hash-based equivalents.
> Linear-scan bodies replaced with single-line delegation calls.

### 1.6 css_properties.cpp — Duplicate property_definitions entries — DONE

`CSS_PROPERTY_APPEARANCE` and `CSS_PROPERTY_USER_SELECT` each appear twice
in the `property_definitions[]` array.

**Recommendation:** Remove the duplicate entries.

> **Implemented:** Duplicate entries for `CSS_PROPERTY_APPEARANCE` and `CSS_PROPERTY_USER_SELECT` removed.

---

## 2. Structural Improvements (Shared Helpers)

### 2.1 css_tokenizer.cpp — Extract number parsing helper (~500→100 lines) — DONE

The number tokenization block (integer → decimal → scientific notation →
unit/percentage classification) is copy-pasted **5 times** in
`css_tokenizer_tokenize()` for these contexts:
1. Digit as first character
2. `+` prefix
3. `-` prefix
4. `.` prefix
5. Inside function context

Each copy is ~100 lines of nearly identical code.

**Recommendation:** Extract a single helper:

```c
// parse a CSS number starting at `pos`, updating `pos` on return.
// returns token type: CSS_TOKEN_NUMBER, CSS_TOKEN_DIMENSION, or CSS_TOKEN_PERCENTAGE
static CssTokenType tokenize_number(const char* input, size_t length,
                                     size_t* pos, CssToken* token);
```

This would reduce `css_tokenizer_tokenize()` by ~400 lines and make the
number parsing logic testable in isolation.

> **Implemented:** Extracted `tokenize_number()` static helper (~65 lines).
> Replaced 4 copy-pasted number parsing blocks (digit, `+`, `-`, `.` prefix cases)
> with calls to the helper. Net savings: ~250 lines.
> The 5th instance (function context) was not a separate copy — it shared the digit case.

### 2.2 Unify unit string↔enum mapping (3 copies → 1) — DONE

Three independent unit-to-string/string-to-unit conversion tables exist:

| File | Function | Direction |
|------|----------|-----------|
| `css_tokenizer.cpp` L9 | `parse_css_unit()` | string → enum |
| `css_value_parser.cpp` L160 | `css_unit_from_string()` | string → enum |
| `css_tokenizer.cpp` L428 | `css_unit_type_to_str()` | enum → string |
| `css_formatter.cpp` L115 | `unit_to_string()` | enum → string |

There is also a declared `css_unit_to_string()` in `css_style.hpp` L937.

**Recommendation:** Create a single canonical pair in a shared location
(e.g., `css_value.cpp` alongside the existing `css_enum_by_name()` registry):

```c
CssUnit     css_unit_from_string(const char* unit_str, size_t length);
const char* css_unit_to_string(CssUnit unit);
```

All 4 current functions should become thin wrappers or be replaced.

> **Implemented:** Created canonical `css_unit_from_string()` and `css_unit_to_string()`
> in `css_value.cpp` with a data-driven `css_unit_table[]` (46 entries).
> Declarations added to `css_style.hpp`. All 4 previous implementations replaced:
> - `css_tokenizer.cpp` `parse_css_unit()` → delegates to canonical version
> - `css_tokenizer.cpp` `css_unit_type_to_str()` → removed entirely (dead, no callers)
> - `css_value_parser.cpp` `css_unit_from_string()` → renamed to wrapper, delegates
> - `css_formatter.cpp` `unit_to_string()` → body replaced with delegation

### 2.3 dom_element.cpp — Extract sibling-chain append helper (~105→15 lines) — DONE

The "append a node to parent's child list, updating first_child/last_child
and prev_sibling/next_sibling pointers" block is copy-pasted **7 times**
across:
- `dom_element_append_text()` (text nodes)
- `dom_element_append_comment()` (comment nodes)
- `build_dom_tree_from_element()` for text, symbol, and comment nodes
- Various inline creations

**Recommendation:** Extract:

```c
static void dom_element_link_child_node(DomElement* parent, DomNode* child);
```

Replace all 7 inline copies with calls to this helper. Note:
`dom_element_link_child()` already exists but has different semantics —
the new helper should handle the full first_child/last_child + sibling chain.

> **Implemented:** Extracted `dom_append_to_sibling_chain()` (~16 lines).
> Replaced 7 identical sibling-chain blocks with calls to the helper.
> Net savings: ~80 lines.

### 2.4 dom_element.cpp — Factor pseudo-element content generation — Deferred

`dom_element_get_pseudo_element_content_with_counters()` is **~420 lines** and
largely duplicates `dom_element_get_pseudo_element_content()` (~100 lines),
adding counter/quote/attr list handling on top.

**Recommendation:** Refactor the shared iteration/string-building logic into
a private helper, with the counter-aware version calling into the base logic
and extending it. Expected savings: ~150 lines.

> **Deferred:** Large refactor (~150 lines savings) but localized to one function.
> The two functions share iteration structure but diverge significantly in
> counter/quote/attr handling, making a clean extraction complex.

### 2.5 dom_element.cpp — Factor counter name extraction (6 copies) — DONE

The pattern of extracting a counter name from a CssValue (checking for STRING,
KEYWORD, or CUSTOM type and reading the appropriate union field) appears
**6 times** in the counter handling code.

**Recommendation:** Extract:

```c
static const char* extract_counter_name(const CssValue* value);
```

> **Implemented:** Extracted `css_value_extract_name()` (~10 lines).
> Replaced 6 identical STRING/KEYWORD/CUSTOM type-check blocks with calls to the helper.
> Net savings: ~50 lines.

### 2.6 css_parser.cpp — Factor functional pseudo-class mapping (2 copies) — DONE

In `css_parse_simple_selector_from_tokens()`, the `strcmp` chain for
functional pseudo-classes (`nth-child`, `not`, `is`, `where`, `has`, etc.)
appears **twice**: once for bare `FUNCTION` tokens (~L945–985) and again
for `:FUNCTION` tokens (~L1125–1165).

Similarly, the token argument extraction loop (collecting tokens inside `()`
and building the argument string) also appears twice.

**Recommendation:** Extract:

```c
static CssSelectorType resolve_functional_pseudo_class(const char* name);
static char* collect_function_arguments(const CssToken* tokens, int* pos,
                                        int token_count, Pool* pool);
```

> **Implemented:** Extracted `css_functional_pseudo_type()` (~16 lines) mapping
> 12 functional pseudo-class names to `CssSelectorType` enum values.
> Replaced 2 identical 28-line if/else-if chains with calls to the helper.
> The argument collection loop was not extracted as the two instances differ
> in token context handling.

### 2.7 css_parser.cpp / css_properties.cpp — Unify hex color parsing (2 copies) — DONE

Hex color parsing with `sscanf` for `#rgb`, `#rrggbb`, `#rgba`, `#rrggbbaa`
exists in:
- `css_parse_token_to_value()` in `css_parser.cpp` (~L490–560)
- `css_parse_color()` in `css_properties.cpp` (~L1100–1250)

Both handle the same formats with nearly identical logic.

**Recommendation:** Create a single shared function:

```c
// parse hex color string (with or without #), returning RGBA.
// handles 3, 4, 6, 8 digit formats.
bool css_parse_hex_color(const char* hex, size_t len,
                         uint8_t* r, uint8_t* g, uint8_t* b, uint8_t* a);
```

This function should also use `parse_hex_codepoint()` from `input-utils.cpp`
instead of `sscanf`. Place it in `css_value.cpp` or a new `css_color.cpp`.

> **Implemented:** Created `css_parse_hex_to_rgba()` in `css_properties.cpp` (~32 lines),
> declared in `css_style.hpp`. Handles #rgb, #rrggbb, #rgba, #rrggbbaa formats.
> - `css_parser.cpp`: Replaced ~40-line hex block in `css_parse_token_to_value()` with
>   ~10-line call to the shared function.
> - `css_properties.cpp`: Simplified `css_parse_color()` hex branch to delegate.
> Used `sscanf`-based parsing (consistent with existing codebase) rather than
> `parse_hex_codepoint()` which has different semantics (streaming hex parser).

---

## 3. input-utils.cpp Reuse Opportunities

### 3.1 Whitespace skipping — `skip_whitespace()` / `input_is_whitespace_char()`

| File | Current Code | Replacement |
|------|-------------|-------------|
| `css_font_face.cpp`, `trim_and_unquote()` | `while (*p == ' ' \|\| *p == '\t' \|\| *p == '\n') p++;` | `skip_whitespace(&p);` |
| `css_font_face.cpp`, `parse_src_entries()` | Same inline pattern, multiple locations | `skip_whitespace(&p);` |
| `css_engine.cpp`, `parse_media_length()` | `while (*value == ' ' \|\| *value == '\t') value++;` | `skip_tab_pace(&value);` |
| `css_engine.cpp`, `evaluate_media_feature()` | Same pattern | `skip_tab_pace(&value);` |
| `dom_element.cpp`, `dom_element_apply_inline_style()` | `while (*str == ' ' \|\| *str == '\t' \|\| *str == '\n' \|\| *str == '\r') str++;` | `skip_whitespace(&str);` |
| `css_tokenizer.cpp` | `css_is_whitespace()` custom function | Could delegate to `input_is_whitespace_char()` |
| `dom_node.cpp`, `print()` | Inline whitespace char checks | `input_is_whitespace_char(c)` |
| `selector_matcher.cpp` | `isspace()` calls in nth-formula parsing | `input_is_whitespace_char()` |

**Estimated touch points:** ~12 locations across 6 files.

### 3.2 Codepoint → UTF-8 encoding — `codepoint_to_utf8()` — DONE

`css_tokenizer.cpp` has two functions that manually encode codepoints to UTF-8:

| Function | Line | Current Approach |
|----------|------|------------------|
| `css_decode_unicode_escapes()` | ~L162–180 | Manual bit-shifting to UTF-8 bytes |
| `css_unescape_string()` | ~L715 | Same manual encoding |

Both should call `codepoint_to_utf8()` from `input-utils.cpp` instead.
The function is already available via `#include "../input-utils.h"`.

> **Implemented:** Replaced both inline UTF-8 encoding blocks (each 10-12 lines
> of manual bit manipulation) with `codepoint_to_utf8()` calls.
> Added `#include "../input-utils.hpp"` to css_tokenizer.cpp.
> Net savings: ~20 lines.

### 3.3 Hex digit parsing — `parse_hex_codepoint()` — Deferred

| File | Current Code | Replacement |
|------|-------------|-------------|
| `css_tokenizer.cpp`, Unicode escape parsing | Manual hex accumulation loop | `parse_hex_codepoint(&pos, ndigits)` |
| `css_parser.cpp`, hex color parsing | `sscanf` with `%2x` format | `parse_hex_codepoint()` for each component |
| `css_properties.cpp`, `css_parse_color()` | `sscanf` with `%2x` format | Same |

### 3.4 Numeric parsing — `try_parse_double()` / `try_parse_int64()`

| File | Current Code | Potential Replacement |
|------|-------------|----------------------|
| `css_engine.cpp`, `parse_media_length()` | `strtod()` + manual end check | `try_parse_double()` |
| `css_properties.cpp`, validators | `strtod()` / `strtol()` + validation | `try_parse_double()` / `try_parse_int64()` |

Note: The CSS tokenizer's own number parsing is more complex (handles units,
percentages, scientific notation) so `try_parse_double()` is not a direct fit
there. It is suitable for simpler validation contexts.

### 3.5 Case-insensitive comparison — `input_strncasecmp()`

`selector_matcher.cpp` defines a local `strcasecmp_local()` that wraps
`str_icmp()`. This is functionally correct and arguably cleaner than using
`input_strncasecmp()`, so this is a **low-priority** change.

---

## 4. Convention Violations

### 4.1 `printf` usage instead of `log_debug()` / `log_info()` — Partial

Per project rules: "NEVER use `printf`/`fprintf`/`std::cout` for debugging."

| File | Lines | Function | Status |
|------|-------|----------|--------|
| `css_engine.cpp` | 582–596 | `css_engine_print_stats()` — 15 `printf()` calls | DONE |
| `selector_matcher.cpp` | ~1220 | `selector_matcher_print_info()` — `printf()` calls | DONE |
| `dom_node.cpp` | 496–540 | `DomNode::print()` (console overload) — ~20 `printf()` calls | Deferred |
| `css_style_node.cpp` | various | Debug print functions — ~18 `printf()` calls | Deferred |

**Recommendation:** Replace all `printf` with `log_info()` or `log_debug()`.
For `DomNode::print()` the StrBuf overload is the primary one; the console
overload should use `log_debug()` or be removed if unused.

> **Implemented:** `css_engine.cpp` (converted to `log_info`, removed `<stdio.h>`) and
> `selector_matcher.cpp` (converted 9 printf calls to `log_info`, removed `<stdio.h>`).
>
> **Deferred:** `dom_node.cpp` and `css_style_node.cpp` use incremental
> line-building patterns (multiple printf calls constructing a single logical line)
> that would require a StringBuf refactor to convert cleanly to log_info.

---

## 5. Summary: Actual Impact

| Category | Status | Files Changed | ~Lines Removed |
|----------|--------|---------------|----------------|
| Dead code removal (§1.1, §1.5, §1.6) | DONE | 3 | ~210 |
| Number parsing extract (§2.1) | DONE | 1 | ~250 |
| Unit mapping unification (§2.2) | DONE | 4 | ~120 |
| DOM sibling-chain helper (§2.3) | DONE | 1 | ~80 |
| Counter name extract (§2.5) | DONE | 1 | ~50 |
| Pseudo-class mapping (§2.6) | DONE | 1 | ~30 |
| Hex color unification (§2.7) | DONE | 3 | ~30 |
| UTF-8 encoding reuse (§3.2) | DONE | 1 | ~20 |
| printf → log (§4, partial) | DONE | 2 | ~0 (convention) |
| **Implemented total** | | **10 files** | **~790 lines** |
| Pseudo-content refactor (§2.4) | Deferred | — | — |
| Stub parsers (§1.2) | Deferred | — | — |
| Whitespace/hex/numeric reuse (§3.1, §3.3, §3.4) | Deferred | — | — |
| printf cleanup remainder (§4 dom_node, css_style_node) | Deferred | — | — |

### Verification

- **Build:** 0 compilation errors
- **Radiant baseline tests:** 2418/2418 pass (100%)
- **Lambda baseline tests:** 581/605 pass — 24 failures are pre-existing benchmark
  timing mismatches (all `*2.ls` awfy/r7rs scripts output `__TIMING__` lines not
  in expected output files; completely unrelated to CSS changes)

### Files Modified

| File | Changes |
|------|--------|
| `css_engine.cpp` | Removed ~200 dead stubs, printf→log_info |
| `css_engine.hpp` | Removed dead declarations |
| `css_tokenizer.cpp` | Extracted `tokenize_number()`, delegated `parse_css_unit()`, removed dead `css_unit_type_to_str()`, replaced UTF-8 encoding with `codepoint_to_utf8()` |
| `css_value.cpp` | Added canonical `css_unit_from_string()`/`css_unit_to_string()` with data table |
| `css_style.hpp` | Added `css_unit_from_string()` and `css_parse_hex_to_rgba()` declarations |
| `css_value_parser.cpp` | Replaced `css_unit_from_string()` with delegation wrapper |
| `css_formatter.cpp` | Replaced `unit_to_string()` with delegation wrapper |
| `css_properties.cpp` | Delegated linear-scan lookups, removed duplicate entries, added `css_parse_hex_to_rgba()` |
| `css_parser.cpp` | Extracted `css_functional_pseudo_type()`, simplified hex color parsing |
| `dom_element.cpp` | Extracted `dom_append_to_sibling_chain()` and `css_value_extract_name()` helpers |
| `selector_matcher.cpp` | Converted printf→log_info, removed `<stdio.h>` |

### Priority Order (original, with status)

1. ~~**High:** Dead code removal (§1.1, §1.5, §1.6)~~ — DONE
2. ~~**High:** Number parsing extraction (§2.1)~~ — DONE
3. ~~**High:** Unit mapping unification (§2.2)~~ — DONE
4. ~~**Medium:** DOM helpers (§2.3, §2.5)~~ — DONE
5. ~~**Medium:** Hex color unification (§2.7) + input-utils reuse (§3.2)~~ — DONE
6. **Low:** Pseudo-content refactor (§2.4) — Deferred
7. **Low:** input-utils whitespace reuse (§3.1) — Deferred
8. ~~**Low:** printf cleanup (§4)~~ — Partial (2/4 files done)

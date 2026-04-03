# Proposal: WPT CSS-Syntax Conformance Testing for Radiant

**Date:** 2026-04-03
**Status:** In Progress — Phase 1 Complete
**Goal:** Run WPT `css-syntax` tests against Radiant's CSS parser, achieve 100% pass rate
**Commit:** `0290b074` (`master`)

---

## 1. Scope

The WPT `css-syntax` suite (`ref/wpt/css/css-syntax/`) contains **26 test files** (~500+ individual test cases) covering CSS tokenization, parsing, serialization, and error recovery per the [CSS Syntax Module Level 3](https://drafts.csswg.org/css-syntax/) spec.

- **24 JavaScript-based tests** — exercise CSS parsing via CSSOM roundtripping
- **2 visual reference tests** — `missing-semicolon.html` / `missing-semicolon-ref.html`

### Test Categories

| Category | Files | Test Pattern |
|----------|-------|-------------|
| Token parsing | `cdc-vs-ident-tokens`, `decimal-points-in-numbers`, `non-ascii-codepoints`, `ident-three-code-points` | Parse CSS → read back via CSSOM → verify |
| Selector parsing | `anb-parsing`, `anb-serialization`, `inclusive-ranges`, `unicode-range-selector` | Set `selectorText` → read back canonical form |
| Declaration parsing | `at-rule-in-declaration-list`, `declarations-trim-whitespace`, `var-with-blocks` | `insertRule()` or `<style>` → read `rule.style.*` |
| Error recovery | `unclosed-constructs`, `unclosed-url-at-eof`, `trailing-braces`, `invalid-nested-rules`, `escaped-eof` | Invalid CSS → verify graceful handling |
| Serialization | `serialize-consecutive-tokens`, `serialize-escape-identifiers` | Parse → serialize → re-parse → compare |
| Preprocessing | `input-preprocessing`, `whitespace` | NULL/surrogate replacement, whitespace classification |
| At-rules | `charset-is-not-a-rule`, `urange-parsing`, `custom-property-rule-ambiguity` | @-rule handling |
| Visual | `missing-semicolon` | Reference test (out of scope for GTest) |

---

## 1b. Implementation Progress

### Phase 1: CSSOM Foundation — COMPLETE ✅

All infrastructure is built and operational. The GTest runner discovers and executes all 25 JS-based tests.

#### Files Created

| File | Purpose | Status |
|------|---------|--------|
| `lambda/js/js_cssom.h` | CSSOM wrapper API declarations | ✅ Complete |
| `lambda/js/js_cssom.cpp` | Full CSSOM bridge (stylesheet/rule/declaration wrappers) | ✅ Complete |
| `test/wpt_testharness_shim.js` | Lightweight WPT testharness.js replacement | ✅ Complete |
| `test/test_wpt_css_syntax_gtest.cpp` | GTest runner discovering & executing WPT tests | ✅ Complete |

#### Files Modified

| File | Changes | Status |
|------|---------|--------|
| `lambda/js/js_dom.cpp` | Added `document.styleSheets`, `<style>.sheet` | ✅ |
| `lambda/js/js_runtime.cpp` | Added CSSOM type dispatch in property get/set/method | ✅ |
| `lambda/main.cpp` | Added `extract_and_collect_css()` in `--document` path | ✅ |
| `build_lambda_config.json` | Added test entry (category: extended) | ✅ |

#### CSSOM APIs Implemented

| API | Status | Notes |
|-----|--------|-------|
| `document.styleSheets` | ✅ | Returns array of CSSStyleSheet wrappers |
| `document.styleSheets[N]` | ✅ | Numeric index access |
| `<style>.sheet` | ✅ | Finds style element index, returns corresponding stylesheet |
| `CSSStyleSheet.cssRules` / `.rules` | ✅ | Returns array of CSSStyleRule wrappers |
| `CSSStyleSheet.insertRule()` | ✅ | Parses rule text via css_tokenize + css_parse_rule_from_tokens |
| `CSSStyleSheet.deleteRule()` | ✅ | Removes rule at index, shifts array |
| `CSSStyleRule.selectorText` (read) | ✅ | Uses css_format_selector_group (has An+B serialization bug) |
| `CSSStyleRule.selectorText` (write) | ✅ | Tokenizes + parses new selector, replaces on rule |
| `CSSStyleRule.cssText` | ✅ | Uses css_format_rule for full rule serialization |
| `CSSStyleRule.style` | ✅ | Returns CSSStyleDeclaration wrapper for rule declarations |
| `CSSStyleRule.type` | ✅ | Maps CssRuleType to CSSOM type constants |
| `CSSStyleDeclaration.cssText` | ✅ | Serializes all declarations |
| `CSSStyleDeclaration.<prop>` | ✅ | camelCase → hyphenated CSS property lookup |
| `CSSStyleDeclaration.length` | ✅ | Declaration count |
| `CSSStyleDeclaration.getPropertyValue()` | ✅ | Lookup by property name |

#### Bugs Fixed During Implementation

| Bug | Root Cause | Fix |
|-----|-----------|-----|
| DomElement child traversal crash | Used `el->children[i]` (indexed) — DomElement uses linked-list (`first_child`/`next_sibling`) | Changed to linked-list traversal |
| `css_parse_rule` linker error | Function declared in header but never defined | Replaced with `css_tokenize()` + `css_parse_rule_from_tokens()` |
| `css_tokenize` signature mismatch | Passed `int*` for token_count, API expects `size_t*` | Fixed type |
| **Stack overflow on selectorText** | `Map.data_cap` is `int` (32-bit) — storing 64-bit `Pool*` pointer truncated it, corrupting memory | Removed `data_cap` pool storage; use `CssRule::pool` / `CssStylesheet::pool` directly |

---

## 1c. Test Results (2026-04-03, commit `0290b074`)

**Overall: 51 / 394 sub-tests passing (13%) across 25 test files**

```
make build && cd build/premake && make config=debug_native test_wpt_css_syntax_gtest
./test/test_wpt_css_syntax_gtest.exe
```

| Test File | Pass/Total | Status |
|-----------|-----------|--------|
| `serialize_escape_identifiers` | **1/1** | ✅ 100% |
| `unclosed_constructs` | **4/4** | ✅ 100% |
| `unclosed_url_at_eof` | **2/2** | ✅ 100% |
| `whitespace` | 26/31 | 84% |
| `non_ascii_codepoints` | 15/32 | 47% |
| `decimal_points_in_numbers` | 2/6 | 33% |
| `urange_parsing` | 1/95 | 1% |
| `anb_parsing` | 0/67 | 0% — An+B serialization bug |
| `anb_serialization` | 0/20 | 0% — An+B serialization bug |
| `at_rule_in_declaration_list` | 0/6 | 0% |
| `cdc_vs_ident_tokens` | 0/1 | 0% |
| `charset_is_not_a_rule` | 0/1 | 0% |
| `custom_property_rule_ambiguity` | 0/4 | 0% |
| `declarations_trim_whitespace` | 0/9 | 0% |
| `escaped_eof` | 0/5 | 0% |
| `ident_three_code_points` | 0/8 | 0% |
| `input_preprocessing` | 0/10 | 0% |
| `invalid_nested_rules` | 0/1 | 0% |
| `serialize_consecutive_tokens` | 0/72 | 0% |
| `trailing_braces` | 0/1 | 0% |
| `unicode_range_selector` | 0/1 | 0% |
| `url_whitespace_consumption` | 0/1 | 0% |
| `var_with_blocks` | 0/14 | 0% |
| `inclusive_ranges` | —/— | ❌ Crash (no WPT_RESULT) |
| `missing_semicolon` | —/— | ❌ Crash (no WPT_RESULT) |

### Failure Categories

| Category | Sub-tests Blocked | Root Cause |
|----------|-------------------|------------|
| **An+B serialization** | ~87 | `css_format_selector_group` outputs pseudo name ("nth-child") instead of An+B value ("2n+1") for `:nth-child()`. `simple->value` stores the name, not the argument. |
| **Element-ID-as-global** | ~30+ | WPT tests use `plain_var.sheet` where `plain_var` is `<style id="plain_var">`. Browsers create global JS variables for elements with IDs. Not supported. |
| **Declaration value serialization** | ~20+ | `rule.style.color` returns raw CSS values but some tests expect normalized forms (e.g., named colors → `rgb()`, whitespace trimming). |
| **CSS tokenizer edge cases** | ~15 | Surrogate replacement, NULL handling, CDC token classification. |
| **Custom property var() with blocks** | 14 | `var()` values containing `{}` blocks need special tokenizer handling. |
| **insertRule parsing** | ~10 | Some rule types (at-rules, nested rules) not parsed correctly by `css_parse_rule_from_tokens`. |
| **JS crashes** | 2 | `inclusive_ranges` and `missing_semicolon` crash before producing output. |

---

## 1d. Next Actions

### Priority 1: Fix An+B Serialization (~87 sub-tests blocked)

The biggest single fix. `css_format_selector_group()` in `css_formatter.cpp` serializes `:nth-child()` by outputting `simple->value`, but that field stores the pseudo-class *name* ("nth-child"), not the An+B *argument* ("2n+1"). The An+B argument is likely stored in a separate field on the selector simple (e.g., `simple->nth_a`, `simple->nth_b`, or a child node).

**Action:** Investigate `CssSelectorSimple` struct for An+B storage, then fix `css_format_selector_group` to output the canonical An+B form per CSSOM spec (e.g., `2n+1`, `even` → `2n`, `odd` → `2n+1`).

**Unblocks:** `anb_parsing` (67 tests), `anb_serialization` (20 tests).

### Priority 2: Element-ID-as-Global Variables (~30+ sub-tests blocked)

WPT tests rely on browsers creating global JS variables for elements with `id` attributes (e.g., `<style id="s">` makes `s` accessible as a global). Our JS transpiler doesn't support this.

**Action:** In the GTest runner (`test_wpt_css_syntax_gtest.cpp`), when extracting scripts, also scan the HTML for elements with `id` attributes and prepend `var id = document.getElementById("id");` lines to the composed JS. This avoids modifying the JS transpiler.

**Unblocks:** Tests using `plain_var.sheet`, `s.sheet.cssRules`, etc.

### Priority 3: Fix Crashing Tests (2 tests)

`inclusive_ranges` and `missing_semicolon` crash before producing any output.

**Action:** Run each test in isolation under `lldb` to get backtrace. Likely a CSS parser crash on unusual input, or an unhandled at-rule type.

### Priority 4: CSS Tokenizer Edge Cases (~15 sub-tests)

- `input_preprocessing` (10 tests): NULL → U+FFFD replacement, surrogate handling
- `cdc_vs_ident_tokens` (1 test): CDC (`-->`) token classification
- `whitespace` (5 remaining failures): Whitespace normalization in specific contexts

**Action:** Review CSS tokenizer in `css_tokenizer.cpp` against spec for NULL/surrogate handling. Add `U+FFFD` replacement for NULL bytes. Check CDC token production.

### Priority 5: Declaration Value Serialization (~20+ sub-tests)

Tests expect specific normalized forms for CSS values (e.g., `rgb()` for named colors, trimmed whitespace).

**Action:** Improve `css_format_value()` output to match CSSOM canonical forms. Lower priority since this requires many small fixes.

### Priority 6: `var()` with `{}` Blocks (14 sub-tests)

Custom property values containing `{}` blocks need the tokenizer to preserve matched braces.

**Action:** Investigate `var_with_blocks` test expectations and modify CSS tokenizer/parser to handle block preservation in custom properties.

### Estimated Impact

| After Fix | Projected Pass Rate |
|-----------|-------------------|
| Priority 1 (An+B) | ~35% (138/394) |
| Priority 1+2 (+ element ID globals) | ~45% (170/394) |
| Priority 1+2+4 (+ tokenizer fixes) | ~55% (200/394) |
| All priorities | ~75-85% |

---

## 2. Gap Analysis: Required CSSOM APIs

The css-syntax tests rely heavily on CSSOM (CSS Object Model) APIs to exercise the parser. Below is the current state and what must be implemented.

### 2.1 Already Implemented

| API | Status | File |
|-----|--------|------|
| `getComputedStyle(elem)` | ✅ | `lambda/js/js_dom.cpp` |
| `document.querySelector()` / `querySelectorAll()` | ✅ | `lambda/js/js_dom.cpp` |
| `getElementsByClassName()` | ✅ | `lambda/js/js_dom.cpp` |
| `element.style.*` (get/set) | ✅ | `lambda/js/js_dom.cpp` |
| `style.setProperty()` / `getPropertyValue()` / `removeProperty()` | ✅ | `lambda/js/js_dom.cpp` |
| `style.cssText` (inline) | ✅ | `lambda/js/js_dom.cpp` |
| `getAttribute()` / `setAttribute()` | ✅ | `lambda/js/js_dom.cpp` |
| `element.className` | ✅ | `lambda/js/js_dom.cpp` |

### 2.2 Implemented (Phase 1) ✅

#### A. `document.styleSheets` Collection (Used by ~20 tests)

Expose the document's parsed stylesheets as a JS-accessible array.

**Status:** ✅ Implemented in `js_dom.cpp` → `js_cssom_get_document_stylesheets()`

#### B. `CSSStyleSheet.cssRules` / `.rules` (Used by ~18 tests)

**Status:** ✅ Implemented in `js_cssom.cpp` → `js_cssom_stylesheet_get_property()`

#### C. `CSSStyleRule.selectorText` (Read/Write) (Used by ~10 tests)

**Status:** ✅ Implemented. Read uses `css_format_selector_group()`. Write uses `css_tokenize()` + `css_parse_selector_group_from_tokens()`.

**Known bug:** An+B arguments inside `:nth-child()` are not serialized correctly (outputs pseudo name instead of argument).

#### D. `CSSStyleSheet.insertRule()` / `deleteRule()` (Used by ~5 tests)

**Status:** ✅ Implemented. `insertRule` uses `css_tokenize()` + `css_parse_rule_from_tokens()`. `deleteRule` shifts array.

#### E. `CSSRule.cssText` (Read) (Used by ~3 tests)

**Status:** ✅ Implemented using `css_format_rule()`.

#### F. `<style>` Element `.sheet` Property (Used by ~12 tests)

**Status:** ✅ Implemented in `js_dom.cpp` → `js_cssom_get_style_element_sheet()`.
Walks DOM tree counting `<style>` elements to find index into `DomDocument::stylesheets[]`.

#### G. `CSSStyleRule.style` (Declaration Block) (Used by ~10 tests)

**Status:** ✅ Implemented as `js_rule_decl_marker` sentinel wrapper. Supports camelCase property access, `.length`, `.cssText`, `.getPropertyValue()`.

### 2.3 Low Priority (Needed for Full 100%)

| API | Tests Using It | Complexity |
|-----|---------------|------------|
| `CSSNestedDeclarations` type check (`instanceof`) | 1 test | Medium — new rule type |
| `assert_throws_dom("SyntaxError")` | 1 test | Low — JS error throwing |
| `element.head.style.animationName` | 1 test | Low — existing style API |
| `@page` rule `.style` access | 2 tests | Medium — new rule wrapper |
| `@font-face` rule `.style` access | 2 tests | Medium — new rule wrapper |

---

## 3. Implementation Plan

### Phase 1: CSSOM Foundation (Enables ~60% of Tests)

Build the core CSSOM bridge connecting Radiant's internal CSS structures to JS.

#### 1a. Stylesheet & Rule Wrappers (`lambda/js/js_cssom.cpp`)

New file implementing CSSOM object wrappers using the same sentinel-marker Map pattern as `getComputedStyle`:

```
CSSStyleSheet wrapper:
  - Map with js_stylesheet_marker sentinel
  - data → CssStylesheet*
  - Properties: cssRules, rules, insertRule(), deleteRule()

CSSStyleRule wrapper:
  - Map with js_style_rule_marker sentinel
  - data → CssRule*
  - Properties: selectorText (r/w), style, cssText, type

CSSStyleDeclaration wrapper (for rule declarations):
  - Map with js_rule_decl_marker sentinel
  - data → CssRule*
  - Properties: camelCase CSS property getters, getPropertyValue(), setProperty()
```

#### 1b. HTMLStyleElement `.sheet` Access

In `js_dom_element_get_property()` (js_dom.cpp), intercept property access on `<style>` elements for the `"sheet"` property name.

Link: During HTML parsing, when a `<style>` element's content is parsed into a `CssStylesheet`, store the stylesheet pointer on the `DomElement` (or in a side map keyed by element pointer).

#### 1c. `document.styleSheets` Property

In `js_document_get_property()`, expose the document's stylesheet collection as an array of CSSStyleSheet wrappers.

### Phase 2: Selector Serialization (Enables ~85% of Tests)

#### 2a. Selector-to-Text Serializer

New function `css_selector_to_string(CssSelector*)` → canonical text:

- Compound selector serialization (type, class, ID, attribute, pseudo-class)
- Combinator serialization (` `, `>`, `+`, `~`)
- An+B notation serialization (canonical form: `2n+1`, not `odd`; or spec-matching normalization)
- Pseudo-element serialization (`::before`, `::after`)
- Handle specificity-ordered output

#### 2b. Selector Re-Parsing for `selectorText` Setter

When `rule.selectorText = "new-selector"` is assigned:
1. Parse new selector text via existing CSS selector parser
2. If parse succeeds, replace rule's selector with new parsed result
3. If parse fails, silently ignore (per CSSOM spec)

### Phase 3: Dynamic Rule Manipulation (Enables ~95% of Tests)

#### 3a. `insertRule(ruleText, index)`

1. Parse `ruleText` as a complete CSS rule using existing parser
2. Validate index bounds (0 ≤ index ≤ cssRules.length)
3. Insert parsed `CssRule*` into stylesheet's `rules[]` array at position
4. Shift subsequent rule indices
5. Throw `SyntaxError` DOMException if parsing fails

#### 3b. `deleteRule(index)`

1. Validate index bounds
2. Remove `CssRule*` from `rules[]` array
3. Shift subsequent entries

#### 3c. `CSSRule.cssText` Serialization

Per-rule serialization (extract from existing `css_stylesheet_to_string()` logic):
- Style rules: `selector { property: value; ... }`
- At-rules: `@name prelude { block }` or `@name prelude;`
- Nested rules: recursive serialization

### Phase 4: Edge Cases & 100% (Remaining ~5%)

- `CSSNestedDeclarations` type and `instanceof` support
- `@page` / `@font-face` rule `.style` declarations
- CSS escape roundtripping in identifiers
- `var()` with `{}` blocks in declaration values
- Surrogate/NULL preprocessing in CSS tokenizer

---

## 4. GTest Runner Design

### 4a. Test Architecture

File: `test/test_wpt_css_syntax_gtest.cpp`

The GTest runner will:
1. Discover all `.html` files in `ref/wpt/css/css-syntax/`
2. For each test file, parse the HTML document (using existing HTML parser)
3. Extract `<style>` elements and parse their CSS content
4. Execute `<script>` elements via the JS transpiler with DOM context
5. Capture test results from testharness.js output

### 4b. WPT Testharness Shim

Rather than loading the full WPT `testharness.js` (which requires a browser environment), implement a lightweight shim that provides:

```javascript
// Minimal testharness.js shim
var _test_results = [];
function test(func, name) {
    try {
        func();
        _test_results.push({name: name, status: "PASS"});
    } catch (e) {
        _test_results.push({name: name, status: "FAIL", message: e.message});
    }
}
function assert_equals(actual, expected, desc) {
    if (actual !== expected)
        throw new Error("assert_equals: got " + JSON.stringify(actual)
            + ", expected " + JSON.stringify(expected) + (desc ? " - " + desc : ""));
}
function assert_not_equals(actual, expected, desc) { ... }
function assert_true(val, desc) { ... }
function assert_false(val, desc) { ... }
function assert_throws_dom(type, func, desc) { ... }
```

This shim is prepended to each test's script before execution.

### 4c. Results Collation

The GTest runner will:
1. Parse `_test_results` array after script execution
2. Create one GTest assertion per WPT test case
3. Report per-file and aggregate pass/fail counts
4. Output a summary table:

```
WPT css-syntax Results:
  anb-parsing.html:           95/105 PASS (90.5%)
  unclosed-constructs.html:    4/4   PASS (100%)
  ...
  TOTAL:                     480/520 PASS (92.3%)
```

### 4d. Build Configuration

Add to `build_lambda_config.json`:
```json
{
    "source": "test/test_wpt_css_syntax_gtest.cpp",
    "name": "WPT CSS Syntax Tests",
    "category": "extended",
    "dependencies": [],
    "binary": "test_wpt_css_syntax_gtest.exe",
    "libraries": ["gtest"],
    "icon": "🎨"
}
```

Category `"extended"` — runs with `make test-extended` and `make test-all`, not baseline.

---

## 5. Execution Order

| Step | Work | Unlocks |
|------|------|---------|
| **Step 1** | Testharness shim + GTest runner scaffold | Can run tests and see failures |
| **Step 2** | `<style>.sheet`, `document.styleSheets` | Tests that only read stylesheets |
| **Step 3** | `cssRules` / `.rules` + `CSSStyleRule.style` | `var-with-blocks`, `trailing-braces`, `custom-property-rule-ambiguity` |
| **Step 4** | `selectorText` read (selector serializer) | `anb-serialization`, `cdc-vs-ident-tokens`, `inclusive-ranges`, `input-preprocessing` |
| **Step 5** | `selectorText` write (selector re-parser) | `anb-parsing` (full), `whitespace` |
| **Step 6** | `insertRule()` / `deleteRule()` | `at-rule-in-declaration-list`, `decimal-points-in-numbers` |
| **Step 7** | `CSSRule.cssText` serialization | `serialize-escape-identifiers`, `serialize-consecutive-tokens` |
| **Step 8** | Edge cases: nested declarations, @page/@font-face `.style`, escape roundtripping | Remaining tests |
| **Step 9** | CSS parser fixes for any failing tokenization/parsing cases | 100% pass |

---

## 6. Risk Analysis

| Risk | Impact | Mitigation |
|------|--------|------------|
| Selector serialization is complex (An+B, pseudo-classes, combinators, specificity) | High — ~105 tests depend on it | Implement incrementally; An+B first (most tests), then compound selectors |
| CSS parser may have tokenizer differences from spec | Medium — some tests may fail due to parsing bugs | WPT tests will expose exact issues; fix incrementally |
| `var()` with `{}` blocks is a recent spec addition | Low — 12 tests | May require tokenizer changes to preserve `{}` in custom property values |
| `CSSNestedDeclarations` is very new (CSS Nesting spec) | Low — 1 test | Can defer or skip initially |
| testharness shim may not cover all assertion types | Low | Add assertion functions as needed when tests use them |

---

## 7. New Files

| File | Purpose |
|------|---------|
| `lambda/js/js_cssom.cpp` | CSSOM wrappers (CSSStyleSheet, CSSStyleRule, CSSStyleDeclaration) |
| `lambda/js/js_cssom.h` | CSSOM wrapper declarations |
| `lambda/input/css/css_selector_serialize.cpp` | Selector → canonical text serializer |
| `lambda/input/css/css_rule_serialize.cpp` | Per-rule CSS text serialization |
| `test/test_wpt_css_syntax_gtest.cpp` | GTest runner for WPT css-syntax suite |
| `test/wpt_testharness_shim.js` | Lightweight testharness.js replacement |

---

## 8. Success Criteria

- **Phase 1 complete:** GTest runner executes all 24 JS-based tests, reports per-test pass/fail
- **Phase 2 complete:** ≥70% pass rate (stylesheet access + declaration reading working)
- **Phase 3 complete:** ≥90% pass rate (selector serialization + dynamic rules working)
- **Phase 4 complete:** 100% pass rate (all edge cases resolved)

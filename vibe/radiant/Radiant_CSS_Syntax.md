# Proposal: WPT CSS-Syntax Conformance Testing for Radiant

**Date:** 2026-04-03
**Status:** In Progress — Phase 6 Complete (453/456, 99.3%)
**Goal:** Run WPT `css-syntax` tests against Radiant's CSS parser, achieve 100% pass rate
**Commit:** `873cb09e` (`master`)

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
| `CSSStyleRule.selectorText` (read) | ✅ | Uses css_format_selector_group (An+B bug fixed) |
| `CSSStyleRule.selectorText` (write) | ✅ | Tokenizes + parses new selector, replaces on rule |
| `CSSStyleRule.cssText` | ✅ | Uses css_format_rule for full rule serialization |
| `CSSStyleRule.style` | ✅ | Returns CSSStyleDeclaration wrapper for rule declarations |
| `CSSStyleRule.type` | ✅ | Maps CssRuleType to CSSOM type constants |
| `CSSStyleDeclaration.cssText` | ✅ | Serializes all declarations |
| `CSSStyleDeclaration.<prop>` | ✅ | camelCase → hyphenated CSS property lookup |
| `CSSStyleDeclaration.length` | ✅ | Declaration count |
| `CSSStyleDeclaration.getPropertyValue()` | ✅ | Lookup by property name |
| `CSSStyleDeclaration.<prop>` (set) | ✅ | Parses value, replaces or appends declaration |

#### Bugs Fixed During Implementation

| Bug | Root Cause | Fix |
|-----|-----------|-----|
| DomElement child traversal crash | Used `el->children[i]` (indexed) — DomElement uses linked-list (`first_child`/`next_sibling`) | Changed to linked-list traversal |
| `css_parse_rule` linker error | Function declared in header but never defined | Replaced with `css_tokenize()` + `css_parse_rule_from_tokens()` |
| `css_tokenize` signature mismatch | Passed `int*` for token_count, API expects `size_t*` | Fixed type |
| **Stack overflow on selectorText** | `Map.data_cap` is `int` (32-bit) — storing 64-bit `Pool*` pointer truncated it, corrupting memory | Removed `data_cap` pool storage; use `CssRule::pool` / `CssStylesheet::pool` directly |
| **An+B serialization** | `CSS_SELECTOR_PSEUDO_NTH_CHILD` case output `simple->value` (func name "nth-child") instead of `simple->argument` (formula "2n+1") | Changed to use `simple->argument`; added explicit cases for `NTH_LAST_CHILD`, `NTH_OF_TYPE`, `NTH_LAST_OF_TYPE` |
| **inclusive_ranges crash** | `rule.style.zIndex = "12345"` hit assertion `mp->data_cap > 0` — rule_decl sentinel Map had `data_cap=0`, fell through to regular map SET | Added `js_cssom_rule_decl_set_property()` and dispatch in `js_runtime.cpp` |
| **Unresolved element IDs** | WPT tests use bare element IDs as globals (`s.sheet`); transpiler returned `undefined` for unresolved identifiers | Implemented browser-like Window named access: walk DOM tree at document load, register element IDs on global object, fallback to `js_get_global_property()` |

---

## 1c. Test Results

### Run 6 (2026-04-05) — 453/456 (99.3%)

| Test File | Pass/Total | Status | Δ from Run 5 |
|-----------|-----------|--------|---------------|
| `anb_parsing` | **67/67** | ✅ 100% | — |
| `anb_serialization` | **20/20** | ✅ 100% | — |
| `at_rule_in_declaration_list` | **6/6** | ✅ 100% | **+6** (was 0) |
| `cdc_vs_ident_tokens` | **1/1** | ✅ 100% | — |
| `charset_is_not_a_rule` | **1/1** | ✅ 100% | **+1** (was 0) |
| `decimal_points_in_numbers` | **6/6** | ✅ 100% | **+2** (was 4) |
| `declarations_trim_whitespace` | **9/9** | ✅ 100% | — |
| `escaped_eof` | **5/5** | ✅ 100% | — |
| `ident_three_code_points` | **8/8** | ✅ 100% | — |
| `inclusive_ranges` | **38/38** | ✅ 100% | — |
| `input_preprocessing` | **10/10** | ✅ 100% | — |
| `non_ascii_codepoints` | **58/58** | ✅ 100% | — |
| `serialize_consecutive_tokens` | **72/72** | ✅ 100% | **+5** (was 67) |
| `serialize_escape_identifiers` | **1/1** | ✅ 100% | — |
| `trailing_braces` | **1/1** | ✅ 100% | — |
| `unclosed_constructs` | **4/4** | ✅ 100% | **+1** (was 3) |
| `unclosed_url_at_eof` | **2/2** | ✅ 100% | — |
| `unicode_range_selector` | **1/1** | ✅ 100% | — |
| `urange_parsing` | **95/95** | ✅ 100% | — |
| `url_whitespace_consumption` | **1/1** | ✅ 100% | **+1** (was 0) |
| `var_with_blocks` | **14/14** | ✅ 100% | **+2** (was 12) |
| `whitespace` | **31/31** | ✅ 100% | **+5** (was 26) |
| `custom_property_rule_ambiguity` | 2/4 | 50% | — |
| `invalid_nested_rules` | 0/1 | 0% | — |
| `missing_semicolon` | —/— | ⬚ No scripts | visual ref test |

**Summary:** +23 sub-tests gained from Run 5, zero regressions. Fixed all 8 regressions from P10/P11 and resolved 15 additional pre-existing failures. Only 3 remaining sub-test failures all require CSS Nesting (a significant new spec feature). Lambda baseline: 539/560 (21 pre-existing failures, none from P12 changes — verified by stashing all changes). Radiant baseline: 34/34 ✅.

#### Fixes Applied (P12)

| Category | Tests Fixed | Root Cause & Fix |
|----------|------------|------------------|
| Comment-preserving serialization | 5+2 (`serialize_consecutive_tokens` 67→72, `var_with_blocks` 12→14) | Hybrid approach in `serialize_declaration_value`: prefer raw `value_text` for custom properties (`CSS_PROPERTY_CUSTOM`) and values containing `var(`, UNLESS backslash present (needs escape resolution). Standard properties use parsed value for normalization. `memchr(decl->value_text, '\\', decl->value_text_len)` gates the decision. |
| Whitespace object comparison | 5 (`whitespace` 26→31) | Fixed `js_strict_equal()` in js_runtime.cpp: two Map wrappers with same `type` marker and same `data` pointer return `true` for `===`. Enables `querySelector` results to be identity-comparable. |
| URL whitespace normalization | 1 (`url_whitespace_consumption` 0→1) | Standard properties like `background-image` use parsed values, normalizing `url( "foo")` → `url("foo")` per CSSOM spec. |
| @charset filtering | 1 (`charset_is_not_a_rule` 0→1) | Filter `CSS_RULE_CHARSET` from `cssRules` getter and `length` in js_cssom.cpp. |
| Unclosed paren in selector | 1 (`unclosed_constructs` 3→4) | Added `CSS_TOKEN_EOF` break in both paren-collection loops in css_parser.cpp. |
| Decimal trailing dot | 2 (`decimal_points_in_numbers` 4→6) | `tokenize_number()` now requires digit after `.`: `pos + 1 < length && isdigit(input[pos + 1])`. |
| At-rule in declaration list | 6 (`at_rule_in_declaration_list` 0→6) | Multiple fixes: (1) Empty `<style>` gets empty CSSStylesheet on `.sheet` access. (2) `.sheet` check moved before `native_element` guard in js_dom.cpp. (3) @-rule skipping in declaration parser (`css_parse_rule_from_tokens_internal`). (4) @-rule skipping in `get_font_face_as_style_rule()`. (5) `@page` rule type recognition. (6) Extended font-face helper to accept `CSS_RULE_PAGE`. |

#### Remaining Failures (3 sub-tests — CSS Nesting required)

| Test File | Sub-test | Requirement |
|-----------|----------|-------------|
| `custom_property_rule_ambiguity` | Nested rule that looks like a custom property declaration | `CSSStyleRule.cssRules` (nested rules), `CSSNestedDeclarations` type |
| `custom_property_rule_ambiguity` | Nested rule that looks like an invalid custom property declaration | Same — CSS nesting parsing and CSSOM |
| `invalid_nested_rules` | Continues parsing after block on invalid rule error | `CSSStyleRule.cssRules`, nested rule error recovery |

These 3 failures require implementing CSS Nesting (CSS Nesting Module Level 1): parsing nested rules inside style blocks, exposing `cssRules` on `CSSStyleRule`, implementing `CSSNestedDeclarations` rule type, and nested rule error recovery. This is deferred as it represents a significant new spec feature.

#### Files Modified (P12)

| File | Changes |
|------|---------|
| `lambda/js/js_cssom.cpp` | Hybrid serialization (custom property + var() check), @charset filtering, empty stylesheet creation, @-rule skip in font-face parser, @page support |
| `lambda/js/js_dom.cpp` | Custom property getter (hybrid approach), `.sheet` before `native_element` guard |
| `lambda/js/js_runtime.cpp` | `js_strict_equal()` wrapper identity comparison for Map objects |
| `lambda/input/css/css_parser.cpp` | EOF break in paren loops, @-rule skip in declaration blocks, @page rule type |
| `lambda/input/css/css_tokenizer.cpp` | Decimal point lookahead (require digit after `.`) |
| `test/test_wpt_css_syntax_gtest.cpp` | GTEST_SKIP for reftests (no inline scripts) |

### Run 5 (2026-04-04) — 431/456 (94.5%)

| Test File | Pass/Total | Status | Δ from Run 4 |
|-----------|-----------|--------|---------------|
| `anb_parsing` | **67/67** | ✅ 100% | — |
| `anb_serialization` | **20/20** | ✅ 100% | — |
| `cdc_vs_ident_tokens` | **1/1** | ✅ 100% | — |
| `declarations_trim_whitespace` | **9/9** | ✅ 100% | — |
| `escaped_eof` | **5/5** | ✅ 100% | **+5** (was 0) |
| `ident_three_code_points` | **8/8** | ✅ 100% | **+8** (was 0) |
| `inclusive_ranges` | **38/38** | ✅ 100% | **+28** (was 10) |
| `input_preprocessing` | **10/10** | ✅ 100% | — |
| `non_ascii_codepoints` | **58/58** | ✅ 100% | **+43** (was 15/32; +26 new tests from HTML fix) |
| `serialize_escape_identifiers` | **1/1** | ✅ 100% | — |
| `trailing_braces` | **1/1** | ✅ 100% | — |
| `unclosed_url_at_eof` | **2/2** | ✅ 100% | — |
| `unicode_range_selector` | **1/1** | ✅ 100% | **+1** (was 0) |
| `urange_parsing` | **95/95** | ✅ 100% | — |
| `url_whitespace_consumption` | **1/1** | ✅ 100% | **+1** (was 0) |
| `serialize_consecutive_tokens` | 67/72 | 93% | -5 (regression) |
| `var_with_blocks` | 12/14 | 86% | -2 (regression) |
| `whitespace` | 26/31 | 84% | — |
| `decimal_points_in_numbers` | 4/6 | 67% | **+2** (was 2) |
| `custom_property_rule_ambiguity` | 2/4 | 50% | — |
| `unclosed_constructs` | 3/4 | 75% | -1 (regression) |
| `at_rule_in_declaration_list` | 0/6 | 0% | — |
| `charset_is_not_a_rule` | 0/1 | 0% | — |
| `invalid_nested_rules` | 0/1 | 0% | — |
| `missing_semicolon` | —/— | ⬚ No scripts | visual ref test |

**Summary:** +88 sub-tests gained, -8 regressions, net +80. Total sub-tests increased from 430→456 due to `non_ascii_codepoints` HTML fix (was missing comma, only ran 32 tests instead of 58). Lambda baseline: 762/762 ✅. Radiant baseline: 100% ✅.

#### New Passes (P10-P11)

| Test File | Change | Root Cause Fixed |
|-----------|--------|------------------|
| `inclusive_ranges` | 10→**38/38** | selectorText setter rejects partial selectors with trailing non-printable chars |
| `non_ascii_codepoints` | 15/32→**58/58** | CSS tokenizer ranges per §4.2, multi-byte UTF-8 `css_consume_name()`, inline style validation, `CSS_VALUE_TYPE_CUSTOM` getter |
| `ident_three_code_points` | 0→**8/8** | Hash token `would-start-an-identifier` check, `CSS_HASH_ID` for ID selectors, `css_named_color_to_rgba()` (148 colors), shorthand `background`→`background-color` cascade |
| `escaped_eof` | 0→**5/5** | `css_starts_escape()` returns true for `\<EOF>`, `css_unescape_string()` produces U+FFFD at EOF, unclosed string/url token handling per §4.3.4, CSSOM `serialize_declaration_value` prefers parsed values over raw `value_text`, function argument parser stops at EOF tokens |
| `unicode_range_selector` | 0→**1/1** | Same color normalization fix as ident_three_code_points |
| `url_whitespace_consumption` | 0→**1/1** | CSSOM `serialize_declaration_value` now uses formatted parsed value |
| `decimal_points_in_numbers` | 2→**4/6** | CSSOM `serialize_declaration_value` fix |

#### Regressions (8 sub-tests)

| Test File | Change | Root Cause |
|-----------|--------|------------|
| `serialize_consecutive_tokens` | 72→67 (-5) | CSSOM `serialize_declaration_value` now prefers parsed values over raw `value_text`; 5 comment-related tests lose raw comment text during parse→format roundtrip (e.g., `a/* comment */b` → `a /* comment */ b`) |
| `var_with_blocks` | 14→12 (-2) | Same cause: `{ var(--x) }` formatted with spaces (`{ var(--x) }` vs `{var(--x)}`) when raw text would have preserved original |
| `unclosed_constructs` | 4→3 (-1) | `querySelector(":nth-child(1")` — unclosed `(` now treated as syntax error after EOF handling changes |

#### Changes Made (Run 5)

| File | Change | Impact |
|------|--------|--------|
| `lambda/input/css/css_tokenizer.cpp` | (1) `css_is_name_start_char_unicode()` proper CSS spec §4.2 ranges. (2) `css_consume_name()` multi-byte UTF-8 via `css_parse_unicode_char()`. (3) Hash token `#` would-start-an-identifier check for `CSS_HASH_ID`. (4) `css_starts_escape()` returns true for `\<EOF>`. (5) `css_unescape_string()` `\` at end → U+FFFD. (6) String token unclosed/EOF handling per §4.3.4. (7) `tokenize_number()` uses `css_consume_name` for dimension units. | P11: all non-ASCII, ident, escape fixes |
| `lambda/input/css/css_parser.cpp` | (1) `hash_type == CSS_HASH_ID` check for ID selectors. (2) EOF implicitly closes rules (missing `}` at EOF). (3) DIMENSION with `CSS_UNIT_NONE` → `CSS_VALUE_TYPE_CUSTOM`. (4) `css_parse_function_from_tokens` stops at `CSS_TOKEN_EOF` in argument loops. | P11: escaped_eof url token fix |
| `lambda/js/js_dom.cpp` | (1) `css_shorthand_covers_longhand()` helper (background→background-color/etc). (2) `js_match_element_property()` shorthand coverage. (3) `querySelector` throws SyntaxError. (4) Non-ASCII codepoint validation in style setter. (5) `CSS_VALUE_TYPE_CUSTOM` in style getter. (6) `css_named_color_to_rgba()` color normalization. | P10: inclusive_ranges. P11: ident, non-ASCII |
| `lambda/js/js_cssom.cpp` | (1) selectorText setter rejects partial selectors with trailing non-printable chars. (2) `serialize_declaration_value` prefers parsed value over raw `value_text`. | P10: inclusive_ranges. P11: escaped_eof, url_whitespace |
| `lambda/input/css/css_value.cpp` | Added `css_named_color_to_rgba()` — 148 CSS4 named colors → hex RGB | P11: ident_three_code_points, unicode_range_selector |
| `lambda/input/css/css_value.hpp` | Declared `css_named_color_to_rgba()` | P11 |
| `ref/wpt/css/css-syntax/non-ascii-codepoints.html` | Fixed missing comma in test array (was 32 tests, now 58) | P11: non_ascii_codepoints |
| `test/test_wpt_css_syntax_gtest.cpp` | Re-enabled temp file cleanup (`unlink`) | Cleanup |

### Run 4 (2026-04-05) — 351/430 (81.6%)

| Test File | Pass/Total | Status | Δ from Run 3 |
|-----------|-----------|--------|---------------|
| `anb_parsing` | **67/67** | ✅ 100% | — |
| `anb_serialization` | **20/20** | ✅ 100% | — |
| `cdc_vs_ident_tokens` | **1/1** | ✅ 100% | — |
| `declarations_trim_whitespace` | **9/9** | ✅ 100% | — |
| `input_preprocessing` | **10/10** | ✅ 100% | — |
| `serialize_consecutive_tokens` | **72/72** | ✅ 100% | **+72** (was 0) |
| `serialize_escape_identifiers` | **1/1** | ✅ 100% | — |
| `trailing_braces` | **1/1** | ✅ 100% | — |
| `unclosed_constructs` | **4/4** | ✅ 100% | — |
| `unclosed_url_at_eof` | **2/2** | ✅ 100% | — |
| `urange_parsing` | **95/95** | ✅ 100% | **+94** (was 1) |
| `var_with_blocks` | **14/14** | ✅ 100% | — |
| `whitespace` | 26/31 | 84% | — |
| `non_ascii_codepoints` | 15/32 | 47% | — |
| `inclusive_ranges` | 10/38 | 26% | — |
| `decimal_points_in_numbers` | 2/6 | 33% | — |
| `custom_property_rule_ambiguity` | 2/4 | 50% | — |
| `at_rule_in_declaration_list` | 0/6 | 0% | — |
| `charset_is_not_a_rule` | 0/1 | 0% | — |
| `escaped_eof` | 0/5 | 0% | — |
| `ident_three_code_points` | 0/8 | 0% | — |
| `invalid_nested_rules` | 0/1 | 0% | — |
| `unicode_range_selector` | 0/1 | 0% | — |
| `url_whitespace_consumption` | 0/1 | 0% | — |
| `missing_semicolon` | —/— | ⬚ No scripts | visual ref test |

**Summary:** +166 sub-tests from P8 (serialize_consecutive_tokens: 72) and P9 (urange_parsing: 94). No regressions. Lambda baseline: 760/762 (2 pre-existing JS failures: `dom_style`, `v18_computed_fields` — unrelated to CSS changes).

#### Changes Made (Run 4)

| File | Change | Impact |
|------|--------|--------|
| `lambda/input/css/css_parser.cpp` | Skip `decl->value == NULL` rejection for custom properties (`--*`), allowing DIMENSION tokens with unknown units like `123foo` | P8: fixed 8 `123foo` sub-tests |
| `lambda/input/css/dom_element.cpp` | Replaced comment-stripping in `dom_element_apply_inline_style` with comment-aware semicolon splitting that preserves comments within custom property values | P8: fixed comment-preservation sub-tests |
| `lambda/js/js_dom.cpp` | (1) Added CSSOM rule fallback in `js_dom_style_method` when element is NULL — detects CSS rule wrappers and delegates to `js_cssom_rule_decl_method`. (2) Added exterior comment stripping at var() substitution boundaries | P8: fixed `a/* comment */var(--t1)` → `a/**/b`. P9: fixed `rule.style.setProperty` dispatch for font-face rules |
| `lambda/js/js_cssom.cpp` | (Prior session) Unicode-range parser, font-face shadow rule, setProperty/removeProperty for CSSFontFaceRule | P9: 95/95 unicode-range sub-tests |

### Run 3 (2026-04-04) — 185/430 (43.0%)

| Test File | Pass/Total | Status | Δ from Run 2 |
|-----------|-----------|--------|---------------|
| `anb_parsing` | **67/67** | ✅ 100% | **+45** (was 22) |
| `anb_serialization` | **20/20** | ✅ 100% | **+15** (was 5) |
| `cdc_vs_ident_tokens` | **1/1** | ✅ 100% | **+1** (was 0) |
| `declarations_trim_whitespace` | **9/9** | ✅ 100% | **+9** (was 0) |
| `input_preprocessing` | **10/10** | ✅ 100% | **+10** (was 0) |
| `serialize_escape_identifiers` | **1/1** | ✅ 100% | — |
| `trailing_braces` | **1/1** | ✅ 100% | **+1** (was 0) |
| `unclosed_constructs` | **4/4** | ✅ 100% | — |
| `unclosed_url_at_eof` | **2/2** | ✅ 100% | — |
| `var_with_blocks` | **14/14** | ✅ 100% | **+13** (was 1) |
| `whitespace` | 26/31 | 84% | — |
| `non_ascii_codepoints` | 15/32 | 47% | — |
| `inclusive_ranges` | 10/38 | 26% | — |
| `decimal_points_in_numbers` | 2/6 | 33% | — |
| `custom_property_rule_ambiguity` | 2/4 | 50% | — |
| `urange_parsing` | 1/95 | 1% | — |
| `at_rule_in_declaration_list` | 0/6 | 0% | — |
| `charset_is_not_a_rule` | 0/1 | 0% | — |
| `escaped_eof` | 0/5 | 0% | — |
| `ident_three_code_points` | 0/8 | 0% | — |
| `invalid_nested_rules` | 0/1 | 0% | — |
| `serialize_consecutive_tokens` | 0/72 | 0% | — |
| `unicode_range_selector` | 0/1 | 0% | — |
| `url_whitespace_consumption` | 0/1 | 0% | — |
| `missing_semicolon` | —/— | ⬚ No scripts | visual ref test |

Detailed Failure Breakdown (25 remaining after Run 5; 245 at Run 3)

> **Note:** Sections for `inclusive_ranges`, `non_ascii_codepoints`, `ident_three_code_points`, `escaped_eof`, `unicode_range_selector`, and `url_whitespace_consumption` are now resolved (Run 5). Their breakdowns below are kept for historical reference.

#### `whitespace` — 26/31 (5 failures)

All 5 failures are object comparison issues — `assert_equals: got {}, expected {}`. The test compares rule objects via deep equality; our serialization returns structurally identical objects but JS `===` comparison fails on object references.

| # | Sub-test | Error |
|---|----------|-------|
| 1 | U+0009 is CSS whitespace | `got {}, expected {}` |
| 2 | U+000a is CSS whitespace | `got {}, expected {}` |
| 3 | U+000c is CSS whitespace | `got {}, expected {}` |
| 4 | U+000d is CSS whitespace | `got {}, expected {}` |
| 5 | U+0020 is CSS whitespace | `got {}, expected {}` |

**Root cause:** Tests use `assert_equals` on parsed rule objects. Our shim likely needs deep object comparison (`assert_object_equals`) or the CSSOM needs to return structurally comparable values. All 5 test the same pattern with different whitespace characters.

---

#### ~~`non_ascii_codepoints` — 15/32 (17 failures)~~ ✅ RESOLVED (58/58 in Run 5)

Tests whether specific Unicode codepoints are valid in CSS identifiers by setting `document.head.style.animationName = "f"+String.fromCodePoint(cp)+"oo"` and checking if the value persists. The 17 failures are codepoints that our CSS parser incorrectly rejects or accepts.

**Root cause:** Two issues: (1) `animationName` property may not be mapped in our CSSOM layer, (2) the CSS tokenizer's `is_name_code_point()` check may not match the CSS Syntax spec's definition of "non-ASCII ident code point" (any codepoint ≥ U+00B7 in the valid ranges, including U+00B7, U+00C0-U+00D6, U+00D8-U+00F6, U+00F8-U+02FF, etc.).

---

#### ~~`inclusive_ranges` — 10/38 (28 failures)~~ ✅ RESOLVED (38/38 in Run 5)

All 28 failures follow the same pattern: `assert_equals: got "foo", expected "parse error"`. The tests set a CSS property to an out-of-range value (e.g., `zIndex` to a value outside the inclusive range) and expect that the parser rejects it (returning the previous value "parse error" / empty string).

| Pattern | Count | Error |
|---------|-------|-------|
| `"foo" becomes "parse error"` | 27 | `got "foo", expected "parse error"` |
| `"fo" becomes "parse error"` | 1 | `got "foo", expected "parse error"` |

**Root cause:** The `setProperty` path on rule.style declarations doesn't validate property values against CSS grammar. It accepts any syntactically valid CSS tokens, even if the value is semantically invalid for the property (e.g., `z-index: foo` should be rejected since `z-index` only accepts integers/auto). Needs property-level value validation in `js_cssom_rule_decl_set_property()`.

---

#### `decimal_points_in_numbers` — 2/6 (4 failures)

| # | Sub-test | Error |
|---|----------|-------|
| 1 | decimal point between digits is valid in a number | `got "", expected "1"` |
| 2 | decimal point before digits is valid in a number | `got "", expected "0.1"` |
| 3 | decimal point between digits is valid in a dimension | `got "", expected "1px"` |
| 4 | decimal point before digits is valid in a dimension | `got "", expected "0.1px"` |

**Root cause:** These tests use `rule.style.setProperty("--foo", "1.0")` on rule declarations. The `setProperty` method is not yet dispatched for rule.style wrappers (only inline style `setProperty` works). The rule.style SET path goes through `js_cssom_rule_decl_set_property()` which handles `rule.style.prop = "value"` assignment but not the explicit `setProperty("name", "value")` method call. The 2 passing tests use `getComputedStyle` without `setProperty`.

---

#### `custom_property_rule_ambiguity` — 2/4 (2 failures)

| # | Sub-test | Error |
|---|----------|-------|
| 1 | Nested rule that looks like a custom property declaration | `got 2, expected 1` |
| 2 | Nested rule that looks like an invalid custom property declaration | `got 2, expected 1` |

**Root cause:** Tests check `sheet.cssRules.length` expecting 1 rule (the outer rule, with the `--custom: ...` parsed as a nested rule inside it). We return 2 rules because CSS nesting support (`CSSNestedDeclarations`) is not implemented — the parser creates the outer rule + an extra rule from the nested content instead of treating the nested declaration as part of the outer rule's declaration list.

---

#### `at_rule_in_declaration_list` — 0/6 (6 failures)

| # | Sub-test | Error |
|---|----------|-------|
| 1 | Allow @-rule with block inside style rule | `Cannot read properties of null (reading 'cssRules')` |
| 2 | Allow @-rule with semi-colon inside style rule | `Cannot read properties of null (reading 'cssRules')` |
| 3 | Allow @-rule with block inside page rule | `Cannot read properties of null (reading 'cssRules')` |
| 4 | Allow @-rule with semi-colon inside page rule | `Cannot read properties of null (reading 'cssRules')` |
| 5 | Allow @-rule with block inside font-face rule | `Cannot read properties of null (reading 'cssRules')` |
| 6 | Allow @-rule with semi-colon inside font-face rule | `Cannot read properties of null (reading 'cssRules')` |

**Root cause:** Tests use `insertRule()` to add style/@page/@font-face rules containing nested @-rules, then access `rule.cssRules` to verify nested rules are preserved. `insertRule` returns `null` for @page/@font-face rules (not implemented as CSSOM wrappers), and even for style rules, the `.cssRules` property of individual rules isn't exposed (needed for CSS Nesting / nested rules API).

---

#### ~~`escaped_eof` — 0/5 (5 failures)~~ ✅ RESOLVED (5/5 in Run 5)

| # | Sub-test | Error |
|---|----------|-------|
| 1 | Escaped EOF → U+FFFD in hash token (ID type) | `assert_throws_dom: expected SyntaxError but no exception thrown` |
| 2 | Escaped EOF → U+FFFD in ident token | `is not a function` |
| 3 | Escaped EOF → U+FFFD in dimension token | `is not a function` |
| 4 | Escaped EOF → U+FFFD in url token | `is not a function` |
| 5 | Escaped EOF in string is ignored | `is not a function` |

**Root cause:** Two issues: (1) Test #1 needs `assert_throws_dom` support — our shim doesn't throw `SyntaxError` for invalid selectors. (2) Tests #2-5 call `CSS.supports()` which is not implemented (`CSS` is not a function/object in our JS environment). The `CSS.supports()` API needs to be added.

---

#### ~~`ident_three_code_points` — 0/8 (8 failures)~~ ✅ RESOLVED (8/8 in Run 5)

| # | Sub-test | Error |
|---|----------|-------|
| 1 | one should be green | `got "red", expected "rgb(0, 128, 0)"` |
| 2 | two should be green | `got "red", expected "rgb(0, 128, 0)"` |
| 3-8 | three-eight should be green | `got "green", expected "rgb(0, 128, 0)"` |

**Root cause:** Two distinct issues: (1) Tests 1-2 return `"red"` — the CSS tokenizer doesn't recognize certain 3-codepoint sequences as valid ident-start (e.g., `--` followed by a non-ASCII char, or `\` escape at start). The selector doesn't match, so the element keeps its red default. (2) Tests 3-8 return `"green"` — the selector matches correctly but `getComputedStyle` returns the named color `"green"` instead of the canonical `"rgb(0, 128, 0)"` form. Needs color value normalization in computed style.

---

#### `serialize_consecutive_tokens` — 0/72 (72 failures)

All 72 failures return empty string `""` instead of the expected serialized value. The test pattern is:
1. Set `--t1` custom property to token A, `--t2` to token B
2. Set `--result` to `var(--t1) var(--t2)` (or with comments)
3. Read back `getComputedStyle(elem).getPropertyValue("--result")`
4. Expect correct serialization with necessary whitespace/comments between ambiguous token pairs

| Token A group | × Token B variants | Failures |
|---------------|-------------------|----------|
| `foo` (ident) | bar, bar(), url(bar), -, 123, 123%, 123em, -->, () | 9 |
| `@foo` (at-keyword) | bar, bar(), url(bar), -, 123, 123%, 123em, --> | 8 |
| `#foo` (hash) | bar, bar(), url(bar), -, 123, 123%, 123em, --> | 8 |
| `123foo` (dimension) | bar, bar(), url(bar), -, 123, 123%, 123em, --> | 8 |
| `#` (delim hash) | bar, bar(), url(bar), -, 123, 123%, 123em | 7 |
| `-` (delim minus) | bar, bar(), url(bar), -, 123, 123%, 123em | 7 |
| `123` (number) | bar, bar(), url(bar), 123, 123%, 123em, % | 7 |
| `@` (delim at) | bar, bar(), url(bar), - | 4 |
| `.` (delim dot) | 123, 123%, 123em | 3 |
| `+` (delim plus) | 123, 123%, 123em | 3 |
| `/` (delim slash) | * | 1 |
| Comment-related | various var() compositions | 7 |

**Root cause:** The `getComputedStyle` for custom properties with `var()` references returns empty string instead of performing CSS variable substitution. The `var()` resolution engine is not implemented — `getComputedStyle` doesn't substitute `var(--t1)` with the computed value of `--t1`. This is the largest remaining gap (72 sub-tests). Implementing `var()` resolution would unblock all 65 token-pair tests, plus the 7 comment-handling tests need additional serialization logic per CSS Syntax spec §9.2.

---

#### `urange_parsing` — 1/95 (94 failures)

All 94 failures return `null` (no matching selector) instead of the expected `U+XXXX` range string. The test pattern:
1. Insert a `@font-face` rule with `unicode-range: <value>`
2. Read back the canonical form via CSSOM

| Category | Count | Example |
|----------|-------|---------|
| Simple hex values (`u+abc` → `U+ABC`) | 20 | `"u+abc" => "U+ABC"` |
| Wildcard ranges (`u+a?` → `U+A0-AF`) | 12 | `"u+a??" => "U+A00-AFF"` |
| Explicit ranges (`u+0-1` → `U+0-1`) | 15 | `"u+0-10ffff" => "U+0-10FFFF"` |
| Zero-padded values (`u+0a` → `U+A`) | 15 | `"u+00000a" => "U+A"` |
| Invalid (should fallback) | 28 | `"u+efg" is invalid` → expect `U+1357` |
| With CSS comments (`u/**/+/**/a/**/?" → "U+A0-AF"`) | 4 | `"u/**/+0/**/?" => "U+0-F"` |

**Root cause:** `@font-face` rules are not wrapped as CSSOM objects. `insertRule` with `@font-face { unicode-range: ... }` doesn't produce a CSSFontFaceRule wrapper, so accessing `.style.unicodeRange` returns null. Needs: (1) CSSFontFaceRule CSSOM wrapper, (2) unicode-range descriptor parsing and canonical serialization.

---

#### `charset_is_not_a_rule` — 0/1 (1 failure)

| # | Sub-test | Error |
|---|----------|-------|
| 1 | @charset isn't a valid rule | `got 4, expected 1` |

**Root cause:** CSS parser treats `@charset "utf-8";` as an at-rule and creates a CssRule for it. Per spec, `@charset` is not a real CSS rule and should be silently consumed by the parser without creating a rule object. The stylesheet reports 4 rules instead of 1.

---

#### `invalid_nested_rules` — 0/1 (1 failure)

| # | Sub-test | Error |
|---|----------|-------|
| 1 | Continues parsing after block on invalid rule error | `got 0, expected 1` |

**Root cause:** An invalid nested rule causes the parser to stop processing the stylesheet. After encountering an error recovery block, it should continue parsing subsequent valid rules. The stylesheet reports 0 rules instead of 1.

---

#### ~~`unicode_range_selector` — 0/1 (1 failure)~~ ✅ RESOLVED (1/1 in Run 5)

| # | Sub-test | Error |
|---|----------|-------|
| 1 | Unicode range is not a token | `got "green", expected "rgb(0, 128, 0)"` |

**Root cause:** Same as `ident_three_code_points` tests 3-8 — selector matches correctly (element gets green) but `getComputedStyle` returns named color `"green"` instead of canonical `"rgb(0, 128, 0)"`. Needs color normalization.

---

#### ~~`url_whitespace_consumption` — 0/1 (1 failure)~~ ✅ RESOLVED (1/1 in Run 5)

| # | Sub-test | Error |
|---|----------|-------|
| 1 | whitespace optional between url( and string | `got "", expected "url(\"foo\")"` |

**Root cause:** Test uses `rule.style.getPropertyValue("background-image")` after `insertRule`. The background-image value containing `url( "foo")` (with whitespace) is not returned. Likely the `getPropertyValue` path for standard properties doesn't work correctly on rule.style wrappers, or the CSS parser drops the URL value during parsing.

</details>

### Run 2 (2026-04-03, commit `873cb09e`) — 91/430 (21%)

| Test File | Pass/Total | Status | Δ from Run 1 |
|-----------|-----------|--------|---------------|
| `serialize_escape_identifiers` | **1/1** | ✅ 100% | — |
| `unclosed_constructs` | **4/4** | ✅ 100% | — |
| `unclosed_url_at_eof` | **2/2** | ✅ 100% | — |
| `whitespace` | 26/31 | 84% | — |
| `anb_parsing` | 22/67 | 33% | **+22** (was 0) |
| `non_ascii_codepoints` | 15/32 | 47% | — |
| `inclusive_ranges` | 10/38 | 26% | **+10** (was crash) |
| `anb_serialization` | 5/20 | 25% | **+5** (was 0) |
| `decimal_points_in_numbers` | 2/6 | 33% | — |
| `custom_property_rule_ambiguity` | 2/4 | 50% | **+2** (was 0) |
| `urange_parsing` | 1/95 | 1% | — |
| `var_with_blocks` | 1/14 | 7% | **+1** (was 0) |
| `at_rule_in_declaration_list` | 0/6 | 0% | — |
| `cdc_vs_ident_tokens` | 0/1 | 0% | — |
| `charset_is_not_a_rule` | 0/1 | 0% | — |
| `declarations_trim_whitespace` | 0/9 | 0% | — |
| `escaped_eof` | 0/5 | 0% | — |
| `ident_three_code_points` | 0/8 | 0% | — |
| `input_preprocessing` | 0/10 | 0% | — |
| `invalid_nested_rules` | 0/1 | 0% | — |
| `serialize_consecutive_tokens` | 0/72 | 0% | — |
| `trailing_braces` | 0/1 | 0% | — |
| `unicode_range_selector` | 0/1 | 0% | — |
| `url_whitespace_consumption` | 0/1 | 0% | — |
| `missing_semicolon` | —/— | ⬚ No scripts | visual ref test |

### Run 1 (2026-04-03, commit `0290b074`) — 51/394 (13%)

<details><summary>Expand Run 1 results</summary>

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
| `custom_property_rule_ambiguity` | 0/4 | 0% |
| `inclusive_ranges` | —/— | ❌ Crash |
| `missing_semicolon` | —/— | ❌ Crash |
| *(12 more at 0%)* | | |

</details>

### Remaining Failure Categories (after Run 6)

| Category | Sub-tests Blocked | Root Cause |
|----------|-------------------|------------|
| **CSS Nesting** | 3 | `custom_property_rule_ambiguity` 2/4 + `invalid_nested_rules` 0/1 — Requires `CSSStyleRule.cssRules`, nested rule parsing, `CSSNestedDeclarations` type, and error recovery for invalid nested rules. Significant new spec feature (CSS Nesting Module Level 1). |

---

## 1d. Next Actions

### ~~Priority 1: Fix An+B Serialization~~ ✅ DONE

Fixed `css_format_selector_group()` to use `simple->argument` instead of `simple->value`. Added explicit cases for `NTH_LAST_CHILD`, `NTH_OF_TYPE`, `NTH_LAST_OF_TYPE`. Result: `anb_parsing` 0→22/67, `anb_serialization` 0→5/20.

### ~~Priority 2: Element-ID-as-Global Variables~~ ✅ DONE

Implemented browser-like Window named access. `js_dom_register_named_elements()` walks DOM tree at `js_dom_set_document()` and registers each element with an `id` as a property on the global object via `js_get_global_this()`. Changed transpiler identifier fallback from returning `undefined` to calling `js_get_global_property()`. Result: `custom_property_rule_ambiguity` 0→2/4, `var_with_blocks` 0→1/14.

### ~~Priority 3: Fix Crashing Tests~~ ✅ DONE

- **`inclusive_ranges`**: Crash was `rule.style.zIndex = "12345"` writing to sentinel Map with `data_cap=0`. Added `js_cssom_rule_decl_set_property()` and runtime dispatch. Now runs: 10/38.
- **`missing_semicolon`**: Visual reference test with no inline scripts. Runner correctly skips.

### ~~Priority 4: An+B Canonical Normalization~~ ✅ DONE

Added `css_format_anb_canonical()` using `CssNthFormula` struct for canonical An+B output. Added strict token-level `css_parse_anb_from_tokens()` parser (~230 lines) handling dimension, ident, and DELIM edge cases. Result: `anb_parsing` 22→**67/67**, `anb_serialization` 5→**20/20**. (+60 sub-tests)

### ~~Priority 5: CSS Tokenizer Edge Cases~~ ✅ DONE

- **Input preprocessing**: Added mutable buffer copy with NULL→U+FFFD and lone surrogate→U+FFFD replacement. Result: `input_preprocessing` 0→**10/10**.
- **CDC token**: Added `-->` check before `--` in tokenizer, CDC/CDO skip in engine rule parsing. Result: `cdc_vs_ident_tokens` 0→**1/1**.
- (+11 sub-tests)

### ~~Priority 6: Declaration Value Serialization~~ ✅ DONE

Fixed critical bug: `css_property_get_id_by_name()` returns `0` for unknown properties (not `-1`/`CSS_PROPERTY_UNKNOWN`). Added `prop_id == 0` check alongside `CSS_PROPERTY_UNKNOWN`. Implemented custom property matching via `js_match_custom_property()` with selector matching and specificity. Added whitespace trimming. Result: `declarations_trim_whitespace` 0→**9/9**. (+9 sub-tests)

### ~~Priority 7: `var()` with `{}` Blocks~~ ✅ DONE

Added `value_text`/`value_text_len` fields to `CssDeclaration` for raw source text preservation. Modified serialization to prefer raw text. Added `!important` exclusion from raw value. Added standard property `{}` block validation (reject braces that don't wrap entire value). Result: `var_with_blocks` 1→**14/14**, `trailing_braces` 0→**1/1**. (+14 sub-tests)

### ~~Priority 8: Serialize Consecutive Tokens (72 sub-tests)~~ ✅ DONE

`serialize_consecutive_tokens` 0→**72/72** — Implemented var() resolution engine with `CssTokenClass` classification, ambiguous token-pair detection, and `/**/` comment insertion. Fixed custom property DIMENSION token parsing, comment-preserving inline style splitting, and exterior comment stripping at var() substitution boundaries. (+72 sub-tests)

### ~~Priority 9: Unicode Range Parsing (94 sub-tests)~~ ✅ DONE

`urange_parsing` 1→**95/95** — Implemented `@font-face` CSSOM wrappers (CSSFontFaceRule), unicode-range canonical parser with wildcard/range/zero-padding normalization, font-face shadow rule mechanism, and fixed `rule.style.setProperty` JS dispatch to delegate CSSOM rules. (+94 sub-tests)

### ~~Priority 10: Inclusive Ranges Remaining (28 sub-tests)~~ ✅ DONE

`inclusive_ranges` 10→**38/38** — Fixed selectorText setter to reject partial selectors with trailing non-printable chars. Added `css_shorthand_covers_longhand()` for background→longhand cascade. (+28 sub-tests)

### ~~Priority 11: Non-ASCII & Escape Handling (30 sub-tests)~~ ✅ DONE

- `non_ascii_codepoints` 15/32→**58/58** — Fixed CSS tokenizer ranges per §4.2, multi-byte UTF-8 in `css_consume_name()`, inline style validation, `CSS_VALUE_TYPE_CUSTOM` getter. Also fixed test HTML (missing comma, now 58 tests instead of 32). (+43 sub-tests)
- `ident_three_code_points` 0→**8/8** — Fixed hash token `would-start-an-identifier` check, `CSS_HASH_ID` for ID selectors, added `css_named_color_to_rgba()` (148 named colors), fixed shorthand→longhand cascade. (+8 sub-tests)
- `escaped_eof` 0→**5/5** — Fixed `css_starts_escape()` for `\<EOF>`, `css_unescape_string()` EOF→U+FFFD, unclosed string/url per §4.3.4, CSSOM `serialize_declaration_value` prefers parsed values, function argument parser stops at EOF. (+5 sub-tests)

### ~~Priority 12: Fix Regressions & Remaining Failures (25 sub-tests)~~ ✅ DONE (22/25 fixed)

Fixed all 8 regressions from P10/P11 and 14 additional pre-existing failures. 3 remaining failures require CSS Nesting (deferred).

| Sub-category | Sub-tests | Result |
|-------------|-----------|--------|
| Comment-preserving serialization | 7 (5+2) | ✅ Hybrid `serialize_declaration_value`: raw `value_text` for custom/var() properties, parsed for standard. `memchr` backslash gate. |
| Whitespace object comparison | 5 | ✅ `js_strict_equal()` Map wrapper identity (same `type` + `data` pointer → `===` true) |
| At-rule in declaration list | 6 | ✅ Empty stylesheet creation, @-rule skipping in declaration blocks, @page support |
| Decimal trailing dot | 2 | ✅ `tokenize_number()` requires digit after `.` |
| Unclosed paren in selector | 1 | ✅ EOF break in paren-collection loops |
| @charset filtering | 1 | ✅ Filter `CSS_RULE_CHARSET` from cssRules/length |
| CSS Nesting | 3 | ⬜ Deferred — requires `CSSStyleRule.cssRules`, `CSSNestedDeclarations`, nested rule parsing |

### Priority 13: CSS Nesting (3 sub-tests) — Future

Requires implementing CSS Nesting Module Level 1:
- Nested rule parsing inside style blocks (not just declarations)
- `CSSStyleRule.cssRules` property exposing nested rules
- `CSSNestedDeclarations` rule type
- Error recovery for invalid nested rules (skip and continue)

### Estimated Impact

| Milestone | Pass Rate |
|-----------|-----------|
| Run 1 (Phase 1 baseline) | 13% (51/394) |
| Run 2 (Phase 2: P1-P3) | 21% (91/430) |
| Run 3 (Phase 3: P4-P7) | 43% (185/430) |
| Run 4 (Phase 4: P8-P9) | 81% (351/430) |
| Run 5 (Phase 5: P10-P11) | 94.5% (431/456) |
| **Run 6 (Phase 6: P12)** | **99.3% (453/456)** |
| + Priority 13 (CSS Nesting) | 100% (456/456) |

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

**Fixed:** An+B arguments inside `:nth-child()` now use `simple->argument`. Remaining issue: raw argument string is output but not canonicalized (e.g., `-1n-1` should become `-n-1`).

#### D. `CSSStyleSheet.insertRule()` / `deleteRule()` (Used by ~5 tests)

**Status:** ✅ Implemented. `insertRule` uses `css_tokenize()` + `css_parse_rule_from_tokens()`. `deleteRule` shifts array.

#### E. `CSSRule.cssText` (Read) (Used by ~3 tests)

**Status:** ✅ Implemented using `css_format_rule()`.

#### F. `<style>` Element `.sheet` Property (Used by ~12 tests)

**Status:** ✅ Implemented in `js_dom.cpp` → `js_cssom_get_style_element_sheet()`.
Walks DOM tree counting `<style>` elements to find index into `DomDocument::stylesheets[]`.

#### G. `CSSStyleRule.style` (Declaration Block) (Used by ~10 tests)

**Status:** ✅ Implemented as `js_rule_decl_marker` sentinel wrapper. Supports camelCase property access (read & write), `.length`, `.cssText`, `.getPropertyValue()`.

**Phase 2 fix:** Added `js_cssom_rule_decl_set_property()` for property SET (e.g., `rule.style.zIndex = "12345"`). Parses value as CSS, replaces or appends declaration on the underlying `CssRule`.

#### H. Window Named Access (Element-ID-as-Global) (Used by ~30+ tests)

**Status:** ✅ Implemented (Phase 2). `js_dom_register_named_elements()` walks DOM tree at document load and registers elements with `id` attributes as properties on the global object (`js_get_global_this()`). Transpiler identifier resolution now falls back to `js_get_global_property()` for unresolved names.

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

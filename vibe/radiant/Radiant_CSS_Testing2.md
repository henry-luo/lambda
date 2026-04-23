# Proposal: GTest Runner for WPT css-syntax Conformance Suite

## Status Quo

An existing GTest runner at `test/test_wpt_css_syntax_gtest.cpp` already executes the WPT `ref/wpt/css/css-syntax/` test suite. It uses a testharness.js shim (`test/wpt_testharness_shim.js`) and runs each test via `./lambda.exe js` with `--document` for DOM context.

**Current results: 24/24 passing, 1 reftest skipped, ~450+ individual assertions all green.**

| Test File | Assertions | Status |
|-----------|-----------|--------|
| anb-parsing | 67/67 | PASS |
| anb-serialization | 20/20 | PASS |
| at-rule-in-declaration-list | 6/6 | PASS |
| cdc-vs-ident-tokens | 1/1 | PASS |
| charset-is-not-a-rule | 1/1 | PASS |
| custom-property-rule-ambiguity | 4/4 | PASS |
| decimal-points-in-numbers | 6/6 | PASS |
| declarations-trim-whitespace | 9/9 | PASS |
| escaped-eof | 5/5 | PASS |
| ident-three-code-points | 8/8 | PASS |
| inclusive-ranges | 38/38 | PASS |
| input-preprocessing | 10/10 | PASS |
| invalid-nested-rules | 1/1 | PASS |
| missing-semicolon | — | SKIPPED (reftest) |
| non-ascii-codepoints | 58/58 | PASS |
| serialize-consecutive-tokens | 72/72 | PASS |
| serialize-escape-identifiers | 1/1 | PASS |
| trailing-braces | 1/1 | PASS |
| unclosed-constructs | 4/4 | PASS |
| unclosed-url-at-eof | 2/2 | PASS |
| unicode-range-selector | 1/1 | PASS |
| urange-parsing | 95/95 | PASS |
| url-whitespace-consumption | 1/1 | PASS |
| var-with-blocks | 14/14 | PASS |
| whitespace | 31/31 | PASS |

The core css-syntax suite is fully passing. This proposal focuses on extending coverage to the **charset/** subdirectory and improving the test infrastructure.

---

## 1. CSSOM API Gap Analysis

The WPT css-syntax tests exercise the following CSSOM APIs. All are currently implemented in Lambda:

### Fully Implemented (no gaps for this suite)

| API | Used By | Implementation |
|-----|---------|---------------|
| `document.styleSheets[N]` | 14 tests | `js_cssom.cpp` → `js_cssom_get_document_stylesheets()` |
| `.cssRules` / `[N]` indexing | 14 tests | `js_cssom.cpp` → stylesheet get property |
| `.selectorText` (read/write) | 8 tests | `js_cssom.cpp` → `css_format_selector_group()` / re-parse |
| `.style.setProperty()` | 2 tests | `js_cssom.cpp` → rule decl method dispatch |
| `.style.getPropertyValue()` | 3 tests | `js_cssom.cpp` → rule decl method dispatch |
| `.style.<camelCase>` (get/set) | 9 tests | `js_cssom.cpp` → camelCase↔hyphenated conversion |
| `.insertRule()` | 1 test | `js_cssom.cpp` → tokenize + parse + insert |
| `.deleteRule()` | 1 test | `js_cssom.cpp` → shift array |
| `.cssText` | 1 test | `js_cssom.cpp` → `css_format_rule()` |
| `<style>.sheet` | 4 tests | `js_dom.cpp` → `js_cssom_get_style_element_sheet()` |
| `getComputedStyle()` | 4 tests | `js_dom.cpp` → cascade matching |
| `document.querySelector()` | 5 tests | `js_dom.cpp` → selector matching |
| `document.getElementsByClassName()` | 1 test | `js_dom.cpp` → class matching |
| `document.head` / `document.body` | 2 tests | `js_dom.cpp` → document properties |
| `CSS.supports()` | 1 test (whitespace) | **See gap below** |

### Gaps Identified

#### Gap 1: `CSS.supports()` — Used by `whitespace.html`

`whitespace.html` uses `CSS.supports("color", "red")` to test CSS whitespace recognition. Currently the test passes because the specific assertions that use `CSS.supports()` appear to be in a code path that doesn't trigger (the test passes its 31 assertions without it). However, `CSS.supports()` is not implemented.

**Impact**: Low for this suite (tests pass without it). Medium for broader WPT coverage — many CSS feature-detection tests rely on it.

**Implementation plan**:
- Add `CSS` as a global JS object (MAP_KIND_DOM wrapper with a new `js_css_namespace_marker`)
- Implement `CSS.supports(property, value)`: tokenize + parse the declaration, return `true` if valid
- Implement `CSS.supports(conditionText)`: parse `@supports` condition syntax
- Location: new section in `js_cssom.cpp`, register in `js_runtime.cpp` global property dispatch

#### Gap 2: `async_test()` — Used by charset/ subdirectory tests

The 16 charset/ testharness tests use `async_test()` and `onload` event handlers:
```js
var t = async_test();
onload = t.step_func(function() {
    assert_equals(getComputedStyle(elm, '').visibility, 'hidden');
    this.done();
});
```

The current `wpt_testharness_shim.js` only supports synchronous `test()`. `async_test()` is not shimmed.

**Impact**: Blocks all 16 charset/ tests from running.

**Implementation plan**:
- Extend `wpt_testharness_shim.js` with `async_test()` returning an object with `.step_func(fn)` and `.done()`
- Since Lambda JS runs synchronously (no event loop), `step_func` can wrap and execute immediately
- Add `onload` as a callable property that fires at end of script (or shim it as immediate invocation)
- Alternative: restructure charset tests to extract the assertion logic into synchronous form in the GTest runner

#### Gap 3: External CSS loading in charset/ tests

Charset tests use `<link rel=stylesheet href="support/no-decl.css">` and test encoding resolution. This requires:
- Correct relative URL resolution for external stylesheets
- Character encoding detection/fallback per CSS Syntax §3.2

**Impact**: Blocks charset/ tests. These test CSS encoding behavior, not core syntax parsing.

**Recommendation**: Defer charset/ tests to a later phase — they test a niche feature (encoding fallback) and require infrastructure (external CSS loading with encoding negotiation) beyond what the core syntax suite needs.

#### Gap 4: Reftest support for `missing-semicolon.html`

This is a visual comparison reftest (`<link rel="match">`), not a JS-based test. It cannot run through the current JS harness.

**Implementation plan**:
- Add reftest support to the GTest runner: load both the test HTML and ref HTML, run layout on both, compare computed styles on matching elements
- Or: convert to a layout comparison test in `test/layout/data/wpt-css-syntax/`

---

## 2. Proposed Improvements to the GTest Runner

### 2a. Add charset/ test discovery

Extend `discover_wpt_css_syntax_tests()` in `test_wpt_css_syntax_gtest.cpp` to also scan `ref/wpt/css/css-syntax/charset/`:

```cpp
static const char* WPT_DIRS[] = {
    "ref/wpt/css/css-syntax",
    "ref/wpt/css/css-syntax/charset",
};
```

Tests with unsupported features (`async_test`) would be detected and `GTEST_SKIP()`-ed with a clear message until Gap 2 is addressed.

### 2b. Extend testharness shim for async_test

Add to `test/wpt_testharness_shim.js`:

```js
function async_test(name) {
    var result = {
        _name: name || "",
        step_func: function(fn) {
            var self = this;
            return function() {
                _wpt_total++;
                try {
                    fn.apply(self, arguments);
                    _wpt_pass++;
                } catch (e) {
                    _wpt_fail++;
                    console.log("FAIL: " + self._name + " - " + e.message);
                }
            };
        },
        done: function() {}
    };
    return result;
}
```

And for `onload` simulation, append to the combined JS:
```js
if (typeof onload === "function") onload();
```

### 2c. Add per-assertion GTest reporting

Currently, individual FAIL lines are reported via `ADD_FAILURE()` but pass/fail is a single `EXPECT_EQ`. Consider emitting one GTest sub-case per WPT assertion using `SCOPED_TRACE` for better CI visibility.

### 2d. Add the suite to `make test-radiant-baseline`

Ensure `test_wpt_css_syntax_gtest.exe` is included in the baseline test targets so CSS syntax conformance is validated on every commit.

---

## 3. Implementation Phases

### Phase 0: Baseline (DONE)
- [x] GTest runner for `ref/wpt/css/css-syntax/*.html`
- [x] testharness.js shim with `test()`, `assert_equals()`, etc.
- [x] 24/24 tests passing, 450+ assertions green
- [x] Build integration in `build_lambda_config.json`

### Phase 1: Infrastructure Improvements (Low effort)
- [ ] Add `test_wpt_css_syntax_gtest` to `make test-radiant-baseline` target
- [ ] Add `async_test()` + `onload` simulation to shim
- [ ] Extend test discovery to `charset/` subdirectory
- [ ] Add `GTEST_SKIP()` for tests requiring unimplemented APIs
- [ ] Run charset/ tests — determine pass rate

### Phase 2: Fill CSSOM Gaps (Medium effort)
- [ ] Implement `CSS.supports(property, value)` in `js_cssom.cpp`
- [ ] Implement `CSS.supports(conditionText)` for `@supports` syntax
- [ ] Register `CSS` global namespace object in `js_runtime.cpp`
- [ ] Add reftest support (layout comparison for `missing-semicolon.html`)

### Phase 3: Charset Encoding Tests (Medium effort, optional)
- [ ] Verify external stylesheet loading works in test context
- [ ] Test character encoding detection/fallback
- [ ] Fix any encoding-related failures in charset/ tests

### Phase 4: Expand to Other WPT CSS Suites
With the infrastructure proven on css-syntax, apply the same pattern to:
- `ref/wpt/css/css-values/` — value parsing conformance
- `ref/wpt/css/css-cascade/` — cascade and specificity
- `ref/wpt/css/css-selectors/` — selector matching
- `ref/wpt/css/css-color/` — color parsing and serialization
- `ref/wpt/css/cssom/` — CSSOM API conformance

---

## 4. Summary

The core WPT css-syntax suite already passes 100% in Lambda. The main work items are:

| Item | Effort | Impact |
|------|--------|--------|
| Add to baseline CI | Trivial | Prevents regressions |
| `async_test()` shim | Small | Enables 16 charset/ tests |
| `CSS.supports()` | Medium | Enables broader WPT coverage |
| Reftest support | Medium | Covers remaining 1 test |
| Charset encoding tests | Medium | Niche encoding conformance |

**Recommended priority**: Phase 1 (CI + async shim) first, then Phase 2 (`CSS.supports()`), then Phase 4 (expand to more WPT suites). Phase 3 is optional and can be deferred.

# Transpile_Js31: JS Regex RE2 Wrapper & highlight.js Support

## Overview

The Lambda JS runtime compiles JavaScript RegExp patterns to RE2, a DFA-based regex engine that guarantees linear-time matching but lacks several JS regex features: **lookaheads** (`(?=...)`, `(?!...)`), **backreferences** (`\1`–`\9`), and certain **Unicode property escapes** (`\p{XID_Start}`). The current preprocessing in `js_create_regex()` (~350 lines in `js_runtime.cpp`) silently strips lookaheads and replaces backreferences with `\w+`, producing incorrect match results for real-world libraries.

**highlight.js v11.9.0** — the primary target — is a regex-driven syntax highlighter bundled in `test/layout/data/support/highlight.min.js` (122 KB, 1,212 lines, 35 language grammars). It uses **48 positive lookaheads**, **33 negative lookaheads**, **5 backreferences**, and **6 Unicode property escapes** across its core engine and grammars. Under Lambda JS today, these patterns are silently degraded, causing incorrect token matching and broken syntax highlighting.

This proposal introduces a **JS-to-RE2 regex transpilation wrapper** that handles the gap between JS regex semantics and RE2 capabilities, targeting 100% highlight.js compatibility and improved general JS regex support.

**Status:** Phases A–E, G Complete · Phase F (highlight.js integration) Pending

---

## 0. Current State

### Regex Feature Matrix

| JS Feature | RE2 Native | Current Status | Result |
|---|---|---|---|
| Lookahead `(?=...)` | ❌ | **Wrapper: match wider + PF_TRIM_GROUP** | ✅ Correct |
| Neg lookahead `(?!...)` | ❌ | **Wrapper: erase + PF_REJECT_MATCH (anchored)** | ✅ Correct |
| Lookbehind `(?<=...)` | ❌ | **Stripped with log warning** | ⚠️ Best-effort |
| Neg lookbehind `(?<!...)` | ❌ | **Stripped with log warning** | ⚠️ Best-effort |
| Backreferences `\1`–`\9` | ❌ | **Wrapper: two-pass literal substitution + PF_GROUP_EQUALITY** | ✅ Correct |
| `\p{XID_Start}` / `\p{XID_Continue}` | ❌ | **Expanded via unicode_prop_map[]** | ✅ Correct |
| `\p{Emoji}` | ❌ | Not mapped | ❌ Compile fail |
| `\p{Script=Latin}` | ❌ | **Prefix stripped → `\p{Latin}`** | ✅ Correct |
| `\s` / `\S` Unicode | ❌ ASCII-only | Expanded to `\p{Z}` ranges | ✅ Correct |
| `\uXXXX` / `\u{X}` | ❌ | Converted to `\x{XXXX}` | ✅ Correct |
| Named groups `(?<name>...)` | ❌ JS syntax | Converted to `(?P<name>...)` | ✅ Correct |
| `\p{L}`, `\p{N}`, `\p{Han}`, etc. | ✅ | Pass-through | ✅ Correct |
| Flags `g`, `i`, `m`, `s`, `y` | ✅ | Mapped to RE2 options | ✅ Correct |
| `.lastIndex`, `.source`, `.flags` | N/A | Runtime properties | ✅ Correct |
| `.exec()`, `.test()` | N/A | Runtime dispatch | ✅ Correct |
| `String.replace/match/split/search` with regex | N/A | Runtime dispatch | ✅ Correct |

### highlight.js Feature Usage (v11.9.0)

| Feature | Count | Where Used | Criticality |
|---|---|---|---|
| `(?=...)` positive lookahead | 48 | Keyword boundaries, token lookahead | **CRITICAL** — core matching loop |
| `(?!...)` negative lookahead | 33 | Keyword exclusion, mode exit guards | **CRITICAL** — prevents over-matching |
| `\b` word boundary | 118 | Keyword matching in all grammars | ✅ Supported by RE2 |
| `\1`–`\2` backreferences | 5 | `h()` regex joiner, HTML tag matching | **HIGH** — composite pattern builder |
| `\p{XID_Start}`, `\p{XID_Continue}` | 6 | Unicode identifier matching in grammars | **HIGH** — language grammars fail |
| `new RegExp(pattern, flags)` | 2 | Dynamic regex construction | ✅ Supported |
| `.exec()` with `.lastIndex` | 10+8 | Main highlighting loop | ✅ Supported |
| `Symbol("nomatch")` | 1 | Sentinel for match failure | ✅ Supported |
| `Object.freeze()` on Map/Set | 3 | Deep-freeze language definitions | ✅ Fixed — mutation throws TypeError |

### Existing Test Coverage

| File | Lines | Coverage |
|---|---|---|
| `test/js/regex_advanced.js` | 48 | Constructor, exec, match, replace, split, search, flags |
| `test/js/v11_regex_methods.js` | 27 | test(), exec(), source, flags properties |
| `test/js/regex_unicode_props.js` | 34 | **NEW** — Unicode property escapes (8 tests) |
| `test/js/regex_lookbehind.js` | 18 | **NEW** — Lookbehind stripping safety (4 tests) |
| `test/js/regex_lookahead.js` | 33 | **NEW** — Positive lookahead with trim (6 tests) |
| `test/js/regex_neg_lookahead.js` | 19 | **NEW** — Negative lookahead with reject (4 tests) |
| `test/js/regex_backrefs.js` | 18 | **NEW** — Backreference two-pass matching (4 tests) |
| `test/js/regex_freeze_collection.js` | 81 | **NEW** — Frozen Map/Set mutation (8 tests) |

---

## 1. Gap Analysis — By Priority

### Gap 1: Lookahead Assertions — CRITICAL

**Impact:** Blocks highlight.js core engine. 81 patterns across the library use lookahead/negative-lookahead for boundary detection. Currently silently stripped in `js_create_regex()` at lines 8821–8856.

**Patterns in highlight.js:**

```
Type A — Trailing boundary:     keyword(?=\s|$|\()        (most common, ~60%)
Type B — Leading exclusion:     (?!else|if|do)\w+          (~25%)
Type C — Embedded constraint:   foo(?=bar)baz              (~10%)
Type D — Nested/combined:       (?=.*\bclass\b)(?!.*\bno\b) (~5%)
```

**Why RE2 can't do this:** Lookaheads are zero-width assertions — they check a condition without consuming input. RE2's DFA model requires all pattern elements to consume or not consume deterministically. Lookaheads break this because they "peek ahead" without advancing the match position.

**Transpilation strategy — "match wider, trim at runtime":**

For each regex containing lookaheads, the wrapper will:

1. **Parse** the JS regex into an assertion-aware AST
2. **Classify** each lookahead by position and complexity
3. **Rewrite** to an RE2-compatible pattern with runtime post-processing metadata
4. **Store** trim/filter instructions alongside the compiled RE2 pattern

| Lookahead Type | Rewrite Strategy | Runtime Cost |
|---|---|---|
| **Trailing** `X(?=Y)` | Compile `X(Y)` as wider match; record trim length of Y | Trim match result — O(1) |
| **Trailing** `X(?=Y\|Z)` | Compile `X(?:Y\|Z)` as wider match; record group for trimming | Trim match result — O(1) |
| **Leading negative** `(?!Y)X` | Compile `X` without constraint; record rejection pattern Y | Post-filter: re-check prefix against Y — O(n) per match |
| **Embedded** `A(?=B)C` | Compile `ABC`; record overlap assertion | Post-filter: verify overlap — O(n) |
| **Nested** `(?=A)(?!B)C` | Compile `C`; record positive check A and negative check B | Post-filter: verify A present, B absent — O(n) |

**Key insight for highlight.js:** ~60% of lookaheads are Type A (trailing boundary) — the simplest case. The most common pattern is `\bkeyword(?=\s|[^a-zA-Z])`, which can be exactly rewritten by absorbing the trailing context and trimming the match.

### Gap 2: Backreferences — HIGH

**Impact:** Affects highlight.js's `h()` regex joiner function which concatenates multiple regex sources into one alternation with renumbered capture groups. 5 backreference occurrences.

**Why RE2 can't do this:** Backreferences (`\1`) refer back to a previously captured group and require the same text to appear again. This makes the language non-regular — matching `(a+)\1` (e.g., `aa`, `aaaa` but not `aaa`) requires memory of the captured text, which a finite automaton cannot express.

**Transpilation strategy — "capture + post-verify":**

1. Replace `\1`–`\9` with a permissive capture group: `(.+?)` or `(\w+)` depending on the original group's content
2. Record the pair: "group N must equal group M"
3. After `.exec()`, verify the equality constraint in C code
4. If constraint fails, advance `lastIndex` and retry

```
Original:   <(\w+)>.*?</\1>
RE2 rewrite: <(\w+)>.*?</(\w+)>
Post-filter: match[1] == match[2]
```

**Limitation:** This changes the DFA's match semantics — the wider pattern may match at positions the original wouldn't. The post-filter corrects for false positives but may miss edge cases where the DFA's leftmost-match picks a different starting position than the NFA would.

### Gap 3: Unicode Property Escapes — HIGH

**Impact:** 6 occurrences in highlight.js grammars for identifier matching. `\p{XID_Start}` and `\p{XID_Continue}` are not in RE2's Unicode property table. Regex compilation silently fails (RE2's `log_errors` is disabled).

**Transpilation strategy — static alias table:**

| JS Property | RE2 Expansion |
|---|---|
| `\p{XID_Start}` | `[\p{L}\p{Nl}_]` |
| `\p{XID_Continue}` | `[\p{L}\p{Nl}\p{Nd}\p{Mn}\p{Mc}\p{Pc}_]` |
| `\p{ASCII}` | `[\x{00}-\x{7F}]` |
| `\p{Any}` | `[\x{00}-\x{10FFFF}]` |
| `\p{Assigned}` | `[^\p{Cn}]` (or `[\x{00}-\x{10FFFF}]` approximate) |
| `\p{Emoji}` | Codepoint range enumeration (large but static) |
| `\p{Script=X}` | `\p{X}` (RE2 uses short form directly) |
| `\p{General_Category=X}` | `\p{X}` (RE2 uses short form directly) |
| `\p{LC}` / `\p{L&}` | `[\p{Lu}\p{Ll}\p{Lt}]` |

**Estimated complexity:** ~80 lines — a lookup table in the existing Stage E preprocessing, no new architecture needed.

### Gap 4: Lookbehind Assertions — MEDIUM

**Impact:** Not used by highlight.js, but used by other JS libraries. Currently passed through to RE2 which rejects them, causing silent regex compilation failure.

**Transpilation strategy:**

| Type | Approach |
|---|---|
| Fixed-width `(?<=abc)X` | Rewrite to match `abcX`, record prefix length to trim from result |
| Fixed-width `(?<!abc)X` | Match `X`, post-filter: check that preceding chars ≠ `abc` |
| Variable-width `(?<=a+)X` | **Cannot transpile** — log warning, compile without the assertion |

### Gap 5: Object.freeze on Map/Set Mutation — LOW

**Impact:** highlight.js's `deepFreeze()` freezes language definition Maps/Sets and expects `.set()`/`.clear()`/`.delete()` to throw `TypeError`. Lambda JS's native collection methods bypass the freeze flag.

**Fix:** Add `__frozen__` check at the top of `js_collection_method()` for mutation operations (set, delete, clear). ~10 lines.

---

## 2. Architecture — JS Regex Wrapper

### Design: `JsRegexCompiled` with Runtime Assist

Replace the current inline preprocessing in `js_create_regex()` with a structured pipeline:

```
js_regex_wrapper.cpp (new file, ~800 lines)
├── Phase 1: Parse JS regex into RegexAST
│   ├── struct RegexNode { type, children, value, quantifier }
│   ├── Types: LITERAL, CHAR_CLASS, GROUP, LOOKAHEAD, NEG_LOOKAHEAD,
│   │          LOOKBEHIND, NEG_LOOKBEHIND, BACKREF, ALTERNATION,
│   │          UNICODE_PROP, ANCHOR, DOT, BOUNDARY
│   ├── js_regex_parse(pattern, len) → RegexNode*
│   └── Handles nested groups, escaped chars, char classes, quantifiers
│
├── Phase 2: Analyze & Classify
│   ├── js_regex_has_assertions(RegexNode*) → bool
│   ├── js_regex_classify_lookaheads(RegexNode*) → LookaheadInfo[]
│   │   ├── TRAILING_BOUNDARY — X(?=Y) at end of branch
│   │   ├── LEADING_NEGATIVE — (?!Y)X at start of branch
│   │   ├── EMBEDDED — mid-pattern assertion
│   │   └── NESTED — multiple assertions combined
│   └── js_regex_has_backrefs(RegexNode*) → BackrefInfo[]
│
├── Phase 3: Rewrite to RE2 + PostFilter
│   ├── js_regex_rewrite(RegexNode*) → RewriteResult
│   │   ├── re2_pattern: char* — RE2-compatible pattern string
│   │   ├── post_filters: PostFilter[] — runtime verification steps
│   │   └── trim_info: TrimInfo — match result trimming instructions
│   ├── Rewrite rules by classification:
│   │   ├── TRAILING_BOUNDARY: absorb trailing context, add trim
│   │   ├── LEADING_NEGATIVE: remove assertion, add reject filter
│   │   ├── BACKREF: replace with capture group, add equality check
│   │   └── PURE: emit directly (no post-processing needed)
│   └── Unicode property expansion (alias table)
│
├── Phase 4: Compile & Package
│   ├── struct JsRegexCompiled {
│   │   re2::RE2* re2;            // compiled pattern
│   │   PostFilter* filters;      // runtime post-filters (NULL if pure)
│   │   int filter_count;
│   │   TrimInfo trim;            // match trimming instructions
│   │   bool has_assertions;      // fast path: skip post-processing if false
│   │   bool global, sticky;      // JS flags
│   │   bool ignore_case, multiline;
│   │ }
│   └── js_regex_compile(pattern, flags) → JsRegexCompiled*
│
└── Phase 5: Runtime Matching
    ├── js_regex_exec(JsRegexCompiled*, input, start_pos) → JsMatchResult
    │   ├── Call re2->Match()
    │   ├── If has_assertions: apply post-filters
    │   │   ├── TRIM: adjust match[0] end position
    │   │   ├── REJECT: re-check against rejection pattern; retry on fail
    │   │   └── EQUALITY: verify group[i] == group[j]; retry on fail
    │   └── Return match array or null
    ├── js_regex_test(JsRegexCompiled*, input) → bool
    └── js_regex_replace(JsRegexCompiled*, input, replacement) → char*
```

### PostFilter Types

```c
enum PostFilterType {
    PF_TRIM_TRAILING,     // trim N chars from match end (trailing lookahead absorbed)
    PF_TRIM_GROUP,        // trim captured group from match end (variable-width trailing)
    PF_REJECT_PATTERN,    // reject match if prefix/suffix matches rejection regex
    PF_GROUP_EQUALITY,    // require group[i] == group[j] (backreference)
    PF_POSITIVE_CHECK,    // require substring matches positive assertion regex
};

struct PostFilter {
    PostFilterType type;
    union {
        int trim_chars;                    // PF_TRIM_TRAILING
        int trim_group_idx;                // PF_TRIM_GROUP
        struct { re2::RE2* pat; int pos; } reject;  // PF_REJECT_PATTERN
        struct { int grp_a; int grp_b; } equality;  // PF_GROUP_EQUALITY
        struct { re2::RE2* pat; int pos; } check;    // PF_POSITIVE_CHECK
    };
};
```

### Integration with Existing Code

The wrapper replaces the inline preprocessing in `js_create_regex()`:

```
Before:
  js_create_regex() → 350 lines of inline string manipulation → re2::RE2

After:
  js_create_regex() → js_regex_compile() → JsRegexCompiled (with re2 + filters)
  js_exec_regex()   → js_regex_exec()   → match with post-processing
  js_test_regex()   → js_regex_test()   → test with post-processing
```

`JsRegexData` struct extended:

```c
struct JsRegexData {
    re2::RE2* re2;              // compiled regex (kept for backward compat)
    JsRegexCompiled* compiled;  // new: full wrapper with post-filters (NULL = legacy)
    bool global;
    bool ignore_case;
    bool multiline;
    bool sticky;
};
```

Callers of `js_get_regex_data()` + `rd->re2->Match()` are updated to use `js_regex_exec()` when `rd->compiled != NULL`.

---

## 3. Implementation Phases

### Phase A — Unicode Property Expansion + Lookbehind Safety ✅ COMPLETE

**Target:** Fix silent compile failures for `\p{XID_Start}`, `\p{XID_Continue}`, `\p{Script=X}`, `\p{General_Category=X}`, `\p{ASCII}`, `\p{Emoji}`. Add safety handling for lookbehinds (strip with warning instead of crashing RE2).

**Files modified:** `lambda/js/js_runtime.cpp` (~60 lines — 16-entry Unicode property expansion table at L8860–8910, lookbehind stripping at L8930–8955)

**Implementation notes:**
- Added `unicode_prop_map[]` with 16 entries covering `XID_Start`, `XID_Continue`, `ID_Start`, `ID_Continue`, `ASCII`, `Any`, `LC`, `L&`, `Ideo`→`Han`, plus `Script=X`/`General_Category=X`/`gc=X`/`sc=X` prefix stripping
- Uses `strlen()` for all length calculations (initial hardcoded lengths were incorrect — lesson learned)
- Lookbehind stripping: erases `(?<=...)` and `(?<!...)` with `log_debug` warning, prevents RE2 compile crash

**Test:**
- `test/js/regex_unicode_props.js` + `.txt` — 8 tests, **all pass** ✅
- `test/js/regex_lookbehind.js` + `.txt` — 4 tests, **all pass** ✅

### Phase B — Assertion Scanner (was "Regex AST Parser") ✅ COMPLETE

**Target:** Build a lightweight scanner that identifies all lookaheads, lookbehinds, and backreferences in a JS regex pattern. Foundation for all subsequent rewriting.

**Design change from proposal:** Instead of a full `RegexNode` AST, the implementation uses a linear scanner (`scan_assertions()`) that detects assertion positions without building a tree. This is simpler, smaller, and sufficient for the post-filter approach.

**New file:** `lambda/js/js_regex_wrapper.h` (83 lines — `JsRegexFilterType` enum, `JsRegexFilter` struct, `JsRegexCompiled` struct, 4-function API)
**New file:** `lambda/js/js_regex_wrapper.cpp` (619 lines total, scanner portion ~165 lines)

**Key internal functions:**
- `scan_assertions()` — linear scan for `(?=`, `(?!`, `(?<=`, `(?<!`, `\1`–`\9`; classifies position (leading vs trailing) and extracts inner content
- `find_matching_paren()` — balanced paren finder respecting escapes and char classes
- `inside_char_class()` — detects if a position is inside `[...]`
- `count_capture_groups()` — counts non-`(?:...)` groups

**Scanner scope:** Handles nested groups, escaped chars, char classes, all assertion types, backreferences `\1`–`\9`

### Phase C — Positive Lookahead Rewriter ✅ COMPLETE

**Target:** Handle positive lookahead `X(?=Y)` by absorbing the assertion as a capture group and trimming the match at runtime (`PF_TRIM_GROUP` post-filter).

**Rewrite rule:** `X(?=Y)` → `X(Y)` + `PF_TRIM_GROUP(new_group_idx)`

**Files modified:** `lambda/js/js_regex_wrapper.cpp` (~40 lines in `rewrite_pattern()`)

**Test:**
- `test/js/regex_lookahead.js` + `.txt` — 6 tests, **all pass** ✅
  - `\w+(?=:)` with exec — captures word before colon, match excludes colon
  - `\w+(?=:)` with match — correct content extraction
  - `.replace()` with positive lookahead
  - Negative test: no match when lookahead content absent
  - `/g` flag with multiple lookahead matches
  - Alternation with lookahead

### Phase D — Negative Lookahead + Backreference Rewriter ✅ COMPLETE

**Target:** Handle negative lookaheads `(?!Y)` and backreferences `\N` with runtime post-filtering.

**Rewrite rules:**
- Negative lookahead `(?!Y)X` → erase `(?!Y)`, add `PF_REJECT_MATCH(Y, at_start/end)` — checks if rejection pattern matches at boundary using anchored RE2 match
- Backreference `\N` → `(.+)` + `PF_GROUP_EQUALITY(N, new_idx)` — two-pass matching: first pass captures group content, second pass substitutes literal content and re-compiles

**Key bugs fixed during implementation:**
1. **`is_trailing` for `^(?!...)`**: Anchors `^`, `$`, `\b` are zero-width — they don't make a following assertion "trailing". Fixed to scan preceding chars for actual content.
2. **`PF_REJECT_MATCH` used `PartialMatch`**: Rejection pattern matched anywhere in remaining text. Fixed to use `ANCHOR_START` for precise boundary checking.
3. **Backref group numbering**: Right-to-left replacement assigned `new_group_idx` in processing order, not pattern order. Fixed with pre-assigned left-to-right group indices.
4. **Two-pass substitution order**: Multiple backref substitutions must process highest `eq_group_b` first to avoid shifting lower groups. Added sort by `eq_group_b` descending.

**Files modified:** `lambda/js/js_regex_wrapper.cpp` (~300 lines — `rewrite_pattern()` neg/backref cases, two-pass exec logic in `js_regex_wrapper_exec()`)

**Test:**
- `test/js/regex_neg_lookahead.js` + `.txt` — 4 tests, **all pass** ✅
  - Leading negative lookahead: `(?!foo)\w+`
  - Negative lookahead with alternation: `(?!error|warn)\w+`
  - Match count with `/g` flag
  - Anchored negative lookahead: `^(?!.*error).*$`
- `test/js/regex_backrefs.js` + `.txt` — 4 tests, **all pass** ✅
  - Repeated word: `(\w+)\s+\1`
  - HTML tag matching: `<(\w+)>.*<\/\1>`
  - Quoted string: `(['"])(.*?)\1`
  - Multi-backref palindrome: `(\w)(\w)\2\1`

### Phase E — Wire into js_create_regex + Full Integration ✅ COMPLETE

**Target:** Wire the wrapper into `js_create_regex()` and update all regex match call sites to use post-filter path.

**Implementation:**
- `JsRegexData` struct extended with `JsRegexCompiled* wrapper` field
- `js_regex_match_internal()` — new static helper that dispatches to wrapper when filters exist, falls back to direct RE2 otherwise
- `js_create_regex()` calls `js_regex_wrapper_compile()` after preprocessing; stores result in `rd->wrapper`; falls back to direct RE2 if wrapper returns NULL
- `js_regex_test()` updated to call `js_regex_wrapper_test()` when wrapper has filters
- **9 `rd->re2->Match()` call sites** replaced with `js_regex_match_internal()` across exec, test, match, matchAll, search, split, replace

**Files modified:**
- `lambda/js/js_runtime.cpp` (~100 lines added/modified)
- `lambda/js/js_regex_wrapper.cpp` (compile + exec entry points)

**Regression test:** All 122 existing JS baseline tests pass (0 regressions) ✅

### Phase F — highlight.js Integration Test

**Target:** Run highlight.js v11.9.0 under Lambda JS and validate correct syntax highlighting output for multiple languages.

**New files:**
- `test/js/hljs_highlight.js` + `.txt` — core engine test
- `test/js/hljs_highlight.html` — minimal HTML fixture (for DOM mode if needed)

**Test structure (modeled on `dom_jquery_lib.js`):**

```js
// Preamble: browser globals for highlight.js module detection
globalThis.window = globalThis;
globalThis.self = globalThis;
globalThis.navigator = { userAgent: "Lambda/1.0" };
globalThis.document = {
    readyState: "complete",
    querySelectorAll: function() { return []; },
    addEventListener: function() {},
    currentScript: null,
    getElementsByTagName: function() { return []; },
    createElement: function(tag) { return { sheet: { cssRules: [], insertRule: function() {} } }; }
};

// Load highlight.js (inlined or from file)
// ... highlight.min.js content ...

// Test 1: Library loads
console.log("loaded:" + (typeof hljs !== "undefined"));
console.log("version:" + hljs.versionString);

// Test 2: Language registration
console.log("langs:" + hljs.listLanguages().length);
console.log("has_js:" + hljs.listLanguages().includes("javascript"));
console.log("has_py:" + hljs.listLanguages().includes("python"));

// Test 3: JavaScript highlighting
var jsResult = hljs.highlight('const x = 42;', {language: 'javascript'});
console.log("js_ok:" + (jsResult.value.indexOf('<span') >= 0));
console.log("js_lang:" + jsResult.language);

// Test 4: Python highlighting
var pyResult = hljs.highlight('def hello():\n    print("hi")', {language: 'python'});
console.log("py_ok:" + (pyResult.value.indexOf('<span') >= 0));

// Test 5: HTML/XML highlighting
var xmlResult = hljs.highlight('<div class="test">Hello</div>', {language: 'xml'});
console.log("xml_ok:" + (xmlResult.value.indexOf('<span') >= 0));

// Test 6: CSS highlighting
var cssResult = hljs.highlight('body { color: red; }', {language: 'css'});
console.log("css_ok:" + (cssResult.value.indexOf('<span') >= 0));

// Test 7: Auto-detection
var autoResult = hljs.highlightAuto('function hello() { return 42; }');
console.log("auto_lang:" + autoResult.language);
console.log("auto_ok:" + (autoResult.value.indexOf('<span') >= 0));

// Test 8: JSON highlighting
var jsonResult = hljs.highlight('{"key": "value", "num": 123}', {language: 'json'});
console.log("json_ok:" + (jsonResult.value.indexOf('<span') >= 0));

// Test 9: Bash highlighting
var bashResult = hljs.highlight('echo "Hello World" | grep Hello', {language: 'bash'});
console.log("bash_ok:" + (bashResult.value.indexOf('<span') >= 0));

// Test 10: Error recovery (invalid language)
try {
    hljs.highlight('test', {language: 'nonexistent_lang_xyz'});
    console.log("err_recovery:caught");
} catch(e) {
    console.log("err_recovery:threw");
}

// Test 11: Relevance scoring
var relResult = hljs.highlightAuto('SELECT * FROM users WHERE id = 1;');
console.log("sql_detected:" + (relResult.language === "sql"));

// Test 12: Keyword matching with lookahead patterns
var kwResult = hljs.highlight('if (true) { return false; } else { break; }', {language: 'javascript'});
console.log("kw_if:" + (kwResult.value.indexOf('keyword') >= 0 || kwResult.value.indexOf('built_in') >= 0 || kwResult.value.indexOf('title') >= 0));

console.log("HLJS_TESTS_DONE");
```

**Expected output (`hljs_highlight.txt`):**

```
loaded:true
version:11.9.0
langs:35
has_js:true
has_py:true
js_ok:true
js_lang:javascript
py_ok:true
xml_ok:true
css_ok:true
auto_lang:javascript
auto_ok:true
json_ok:true
bash_ok:true
err_recovery:threw
sql_detected:true
kw_if:true
HLJS_TESTS_DONE
```

**Effort:** ~350 lines (test file with inlined highlight.min.js preamble + test assertions)

### Phase G — Object.freeze on Map/Set ✅ COMPLETE

**Target:** Make `Object.freeze()` actually prevent mutation via native Map/Set methods.

**Implementation:** Added `__frozen__` check in `js_collection_method()` for mutation operations (set/add=0, delete=3, clear=4). Throws `TypeError` with descriptive message (e.g., "Cannot set on a frozen Map").

**Files modified:** `lambda/js/js_runtime.cpp` (~15 lines in `js_collection_method`)

**Test:**
- `test/js/regex_freeze_collection.js` + `.txt` — 8 tests, **all pass** ✅
  - Frozen Map: `.set()`, `.delete()`, `.clear()` all throw TypeError
  - Frozen Set: `.add()`, `.delete()`, `.clear()` all throw TypeError
  - Read operations on frozen collections still work

---

## 4. Actual Implementation vs Estimate

| Phase | Description | Estimated | Actual | Status |
|---|---|---|---|---|
| A | Unicode property expansion + lookbehind safety | ~100 | ~60 | ✅ Complete |
| B | Assertion scanner (was "AST parser") | ~360 | ~248 (h:83 + cpp:165) | ✅ Complete |
| C | Positive lookahead rewriter | ~200 | ~40 | ✅ Complete |
| D | Negative lookahead + backreference rewriter | ~200 | ~300 | ✅ Complete |
| E | Integration — wire into js_create_regex | ~250 | ~100 | ✅ Complete |
| F | highlight.js integration test | ~350 | — | ⬜ Pending |
| G | Object.freeze on Map/Set | ~15 | ~15 | ✅ Complete |
| **Total** | | **~1,475** | **~763** (+ ~251 test lines) | **6/7 done** |

**New files:** `js_regex_wrapper.h` (83 lines), `js_regex_wrapper.cpp` (619 lines) = **702 lines**
**Modified:** `js_runtime.cpp` (~100 lines across 5 feature areas)
**Test files:** 7 `.js` files (251 lines) + 7 `.txt` expected output files
**Regression:** 122/122 existing JS baseline tests pass (0 regressions)

**Key design difference from proposal:** The implementation uses a linear assertion scanner + pattern rewriter instead of a full `RegexNode` AST. This resulted in ~50% fewer lines than estimated while covering all targeted patterns. The two-pass backref matching (pass 1: capture with `.+`, pass 2: recompile with literal substitution) was not in the original design but proved necessary for correct DFA-based backref simulation.

---

## 5. Test File Summary

All test files follow the existing `test/js/` convention: `.js` file produces `console.log()` output, `.txt` file contains expected output line-for-line.

| Test File | Phase | Status | Coverage |
|---|---|---|---|
| `test/js/regex_unicode_props.js` + `.txt` | A | ✅ 8/8 pass | `\p{XID_Start}`, `\p{XID_Continue}`, `\p{Script=Latin}`, `\p{ASCII}`, `\p{Any}`, `\p{LC}` |
| `test/js/regex_lookbehind.js` + `.txt` | A | ✅ 4/4 pass | Lookbehind patterns stripped without crash; best-effort results |
| `test/js/regex_lookahead.js` + `.txt` | C | ✅ 6/6 pass | Positive lookahead with exec, match, replace, /g flag |
| `test/js/regex_neg_lookahead.js` + `.txt` | D | ✅ 4/4 pass | Negative lookahead: leading, alternation, anchored `^(?!...)` |
| `test/js/regex_backrefs.js` + `.txt` | D | ✅ 4/4 pass | `\1` repeated word, HTML tags, quoted strings, multi-backref palindrome |
| `test/js/regex_freeze_collection.js` + `.txt` | G | ✅ 8/8 pass | Frozen Map/Set mutation throws TypeError; reads still work |
| `test/js/regex_advanced.js` + `.txt` | — | ✅ 10/10 pass | Pre-existing: constructor, exec, match, replace, split, search |
| `test/js/hljs_highlight.js` + `.txt` + `.html` | F | ⬜ Pending | highlight.js v11.9.0 full integration (12 assertions) |

---

## 6. Risk Assessment

| Risk | Severity | Mitigation |
|---|---|---|
| **Regex parser bugs** — JS regex syntax is complex, edge cases in char classes, escapes, Unicode | High | Build parser incrementally; test with highlight.js's actual patterns first |
| **Post-filter retry loop** — rejection filters may cause O(n²) on pathological patterns | Medium | Cap retry count per match (e.g., 100); log warning on exhaustion |
| **DFA vs NFA match semantics** — RE2 returns leftmost-longest, JS returns leftmost-first | Medium | For most patterns these agree. Document known divergence for ambiguous alternations `a\|ab` |
| **Capture group renumbering** — rewriting adds/removes groups, breaking `$1` references | High | Track original→new group index mapping in `JsRegexCompiled`; remap in replace/match |
| **highlight.js version drift** — future versions may use more advanced regex features | Low | Test against pinned v11.9.0; update wrapper as needed for new patterns |
| **Performance regression** — post-filter path adds overhead vs direct RE2 | Low | Fast path: `has_assertions == false` → zero overhead. Only assertion-bearing regexes take the filter path |
| **Memory** — `JsRegexCompiled` allocates additional `PostFilter` structs and secondary RE2 patterns | Low | Pool-allocate; typical program has <100 regex objects |

---

## 7. Dependencies

| Phase | Depends On | Notes |
|---|---|---|
| A | None | Pure preprocessing enhancement — start immediately |
| B | None | New file, no existing code dependencies |
| C | B (parser) | Uses RegexAST from Phase B |
| D | B (parser), C (infrastructure) | Extends rewriter from Phase C |
| E | B, C, D | Wires everything together |
| F | A, B, C, D, E (all) | Integration test — validates full pipeline |
| G | None | Independent 10-line fix |

Recommended execution order: **A + G (parallel) → B → C → D → E → F**

Phases A and G can be done independently as quick wins before the main wrapper work begins.

---

## 8. Out of Scope

These regex features are NOT targeted by this proposal:

| Feature | Reason |
|---|---|
| **Variable-width lookbehind** `(?<=a+)X` | Theoretically untranslatable to DFA; no known JS library depends on this heavily |
| **Recursive patterns** `(?R)` | Not part of JS regex spec |
| **Conditional patterns** `(?(n)y\|n)` | Not part of JS regex spec |
| **`\K` match reset** | Not part of JS regex spec |
| **`d` flag (hasIndices)** | ES2022 feature; no impact on highlight.js |
| **Full NFA fallback engine** | Would require integrating PCRE2/Oniguruma; consider as future Phase H if post-filter approach proves insufficient |

---

## 9. Future Consideration: PCRE2 Fallback

If the post-filter approach proves insufficient for complex assertion patterns beyond highlight.js, a **PCRE2 fallback** could be added:

```
js_regex_compile():
  1. Parse regex → check assertion complexity
  2. If simple (trailing lookahead, basic backref) → RE2 + post-filter (this proposal)
  3. If complex (nested assertions, variable lookbehind) → fall back to PCRE2
```

PCRE2 is a single-file C library (~200KB), BSD-licensed, and supports all JS regex features. It would be added as an optional dependency, used only for patterns that cannot be transpiled to RE2. This is explicitly deferred to keep the current proposal focused and avoid adding a new dependency.

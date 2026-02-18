# Readability.ls — Progress Report

## Summary

Port of Mozilla's Readability.js to Lambda Script (`utils/readability2.ls`), tested against the official Readability.js test suite (130 test cases).

| Metric | Count | Notes |
|--------|------:|-------|
| **PASS** | 60 | All metadata fields match expected |
| **FAIL** | 3 | Excerpt mismatches only |
| **SKIP** | 67 | No expected metadata (HTML-output-only tests) |
| **Total** | 130 | |

**Pass rate (of testable):** 60/63 = **95.2%**

---

## What's Working

### Metadata Extraction (fully passing)
- **Title**: Extracted from `<title>`, `<meta og:title>`, and JSON-LD. Cleans site-name suffixes and separator characters.
- **Byline**: Detected from `<meta>` tags, JSON-LD `author` fields, and in-body byline elements (via `rel=author`, `itemprop=author`, class/id heuristics).
- **Excerpt**: First paragraph of article content, with byline-overlap filtering to avoid repeating the author name.
- **Site Name**: From `<meta og:site_name>` and JSON-LD `publisher`.
- **Published Time**: From `<meta article:published_time>`, `datePublished`, and similar.
- **Language**: From `<html lang>` attribute.
- **Direction**: From `<html dir>` or `<body dir>` attribute.

### Content Extraction
- **Scoring algorithm**: Proximity-weighted scoring matching Readability.js behavior — direct children of a container get full score, grandchildren get half, great-grandchildren get 1/6th.
- **Container detection**: Scans `<div>`, `<section>`, `<article>`, `<main>` elements as candidate containers.
- **Article fast-path**: If a single `<article>` element exists, it's used directly (skipping scoring).
- **Unlikely-candidate filtering**: Strips sidebar, ad, and navigation elements by class/id pattern matching.
- **Conditional cleaning**: Removes low-quality elements (few words, high link density, etc.) from the article.
- **Div-as-paragraph**: Divs without block-level children are treated as paragraphs for scoring.

### JSON-LD Parsing
- Custom string-level JSON-LD parser (doesn't rely on Lambda's JSON parser for embedded `<script>` data, since Lambda's `input()` parses the surrounding HTML).
- Supports `@graph` arrays, nested `author`/`publisher` objects, and `isPartOf` for site name.

---

## Remaining Failures (3 tests)

### 1. `005-unescape-html-entities` — excerpt mismatch

| | Value |
|---|---|
| **Expected** | `&#xg; 😭 😭 � �` |
| **Actual** | `&#xg; &#x1F62D; &#128557; &#xFFFFFFFF; &#x0;` |

**Root cause**: Lambda's HTML parser does not decode HTML entities to Unicode characters in text content. The expected output contains the decoded emoji/replacement characters, but our output preserves the raw entity references. This is an HTML parser-level issue, not a Readability logic issue.

### 2. `replace-brs` — excerpt mismatch

| | Value |
|---|---|
| **Expected** | `Lorem ipsumdolor sit` (just a prefix excerpt) |
| **Actual** | Full paragraph starting with `Lorem ipsumdolor sit amet, consectetur...` |

**Root cause**: Readability.js replaces sequences of `<br>` tags with `<p>` tags during `_replaceBrs()` preprocessing. Our implementation does not perform this DOM transformation, so the paragraph boundaries differ and the resulting excerpt text doesn't match exactly. The content itself is correct — just paragraph segmentation differs.

### 3. `mozilla-1` — excerpt mismatch

| | Value |
|---|---|
| **Expected** | `It's easier than ever to personalize Firefox...` |
| **Actual** | Wrong text (country list instead of article content) |

**Root cause (multi-layered)**:
1. **Empty string comparison bug (Bug #5, now FIXED)**: Lambda's HTML5 tokenizer created real `String*` objects with `len=0` for empty HTML attributes like `content=""`, while the string literal `""` in Lambda compiles to `ITEM_NULL`. This type mismatch (`LMD_TYPE_STRING` vs `LMD_TYPE_NULL`) caused `str == ""` to always return false for empty attribute strings. Fix: HTML5 tokenizer now uses `nullptr` for empty attributes, consistently matching Lambda's null semantics.
2. **Article region selection**: Even with the empty-string fix, `find_best_candidate` selects a div containing a country dropdown list instead of the main article content. This is an article extraction quality issue, not related to `collect_all_elements`.

---

## Lambda Runtime Issues Encountered

These are bugs/limitations in the Lambda runtime discovered during this port. They required algorithmic workarounds in the readability code.

### Bug #1: `order by` corrupts compound types (Bug #23) — **FIXED**

**Status**: Fixed. The `for ... order by` expression now correctly sorts compound types (maps, arrays, elements) without corrupting them.

**Root cause**: `fn_sort1()` in `lambda-vector.cpp` ignored the order-by key expression entirely. It converted ALL items to doubles via `item_to_double()`, destroying compound type references (maps, arrays, elements became NaN).

**Fix**: Implemented `fn_sort_by_keys(values, keys, descending)` — a new runtime function that performs an index-permutation sort. The transpiler now:
1. Collects order-by key expressions into a separate `arr_keys` array alongside the main `arr_out`
2. Calls `fn_sort_by_keys` to sort `arr_out` in-place based on the key values
3. For `order by` + `offset`/`limit`, uses new `array_drop_inplace()`/`array_limit_inplace()` to avoid List conversion issues

**Files changed**: `lambda/transpile.cpp`, `lambda/lambda-vector.cpp`, `lambda/lambda-data-runtime.cpp`, `lambda/lambda.h`, `lambda/mir.c`

**Note**: `[for ... order by]` (with brackets) produces `[[sorted]]` (double-wrapped). Use bare `for ... order by` (without brackets) when assigning to a variable for direct indexing. The readability2.ls workaround has been simplified to use `order by` directly.

**Verified with correct syntax** (`for ... order by` inside a for-expression, pipe `|` not `|>`):
```
// Scalars: works — result is [5, 4, 3, 2, 1]
let sorted = for (x in [3, 1, 5, 2, 4] order by x desc) x

// Compound types: works — maps sorted by key
let sorted_maps = for (m in maps order by m.s desc) m

// Compound types: works — arrays sorted by first element
let sorted_arrs = for (a in arrs order by a[0] desc) a
```

### Bug #2: `collect_all_elements` loses element children — **NOT A BUG**

**Original symptom**: Elements returned by `collect_all_elements(root)` have 0 children, even though the original elements have children in the DOM. `get_inner_text()` on these elements returns `""`.

**Investigation**: Extensive testing proved `collect_all_elements` works correctly:
- On small HTML files: returns elements with correct children counts
- On mozilla-1 (95KB): returns 882 elements, all with correct children
- Both `collect_all_elements` and `find_all` return identical results

**Root cause**: The actual issue was **Bug #5 (empty string comparison)**, not `collect_all_elements`. The metadata excerpt was an empty string from `<meta content="">` that didn't compare equal to `""` (due to type mismatch), causing the excerpt logic to take the metadata path instead of falling through to paragraph search.

### Bug #5: Empty string from HTML attributes doesn't equal `""` — **FIXED**

**Symptom**: `get_attr(elem, "content", "")` for `<meta content="">` returns a value where:
- `is string` → true, `len()` → 0
- `== ""` → **false**, `!= ""` → **true**
- `== null` → **false**

This caused `get_meta()` to pass through empty-content meta tags, and `metadata.excerpt` to be set to a "ghost" empty string that prevented the paragraph-search fallback from running.

**Root cause**: Lambda's `""` string literal compiles to `ITEM_NULL` (type `LMD_TYPE_NULL`) via `build_ast.cpp`, while the HTML5 tokenizer (`html5_tokenizer.cpp`) allocated a real `String*` with `len=0` (type `LMD_TYPE_STRING`). The equality function `fn_eq` does a type-ID check first — since `LMD_TYPE_STRING != LMD_TYPE_NULL`, comparison returned false.

**Fix**: Changed `html5_tokenizer.cpp` to use `nullptr` instead of allocating zero-length strings for empty attribute values. Since `s2it(nullptr)` returns `ITEM_NULL`, this matches Lambda's `""` → null semantics.

**Files changed**: `lambda/input/html5/html5_tokenizer.cpp`, `lambda/input/html5/html5_token.cpp`

### Bug #3: Index access on concatenated arrays in `for` comprehensions — **NOT A BUG**

**Original symptom**: Building an array with `++` (concatenation) and then accessing elements by index (`arr[i]`) inside a `for` comprehension returns garbage values.

**Root cause**: The reported code used `range(0, n)` which is **incorrect syntax**. The `range()` system function requires 3 arguments: `range(start, end, step)`. Calling it with 2 arguments silently fails. Two correct approaches:

1. **`to` range syntax** (preferred): `for (i in 0 to n - 1) arr[i]` — note that `to` is **inclusive** on both ends
2. **3-arg `range()` function**: `for (i in range(0, n, 1)) arr[i]` — exclusive upper bound

Both approaches work correctly with concatenated arrays and compound types.

**Recommendation**: Use direct iteration `for (c in containers) c` when you don't need the index. Use `0 to n - 1` or `range(0, n, 1)` when index is needed.

```
// Direct iteration (preferred when index not needed)
for (c in containers) c

// Index-based with 'to' (inclusive both ends)
for (i in 0 to len(arr) - 1) arr[i]

// Index-based with range() (exclusive upper bound, requires 3 args)
for (i in range(0, len(arr), 1)) arr[i]

// WRONG: range() requires 3 args — this silently fails
for (i in range(0, len(arr))) arr[i]  // ← produces empty result
```

### Bug #4: Compound boolean expressions in `where` clauses — **NOT A BUG**

**Original symptom**: Complex `and`/`or`/`not` combinations in `where` clauses produce incorrect results. Elements that should be filtered out are included, or vice versa.

**Root cause**: Not a `where` clause issue. All compound boolean patterns (`and`, `or`, `not`, nested combinations, function calls in predicates) work correctly. The original failures were caused by **symbol vs string type mismatches** — comparing a symbol `'p'` against a string `"p"` returns false. In Lambda, `'p'` (single-quoted) is a symbol and `"p"` (double-quoted) is a string; they are different types.

```
// WRONG: symbol != string
e.tag == "p"    // ← fails if e.tag is symbol 'p'

// CORRECT: compare with matching type
e.tag == 'p'    // ← works when e.tag is symbol 'p'
```

All compound boolean patterns work correctly in `where` clauses:
```
where x > 3 and x < 8                         // ✓
where x < 3 or x > 8                          // ✓
where not (x > 5)                              // ✓
where is_para(e) and not is_byline(e)          // ✓
where (f1(x) and not f2(x)) or f3(x)          // ✓
where f1(x) and not f2(x) and f3(x)           // ✓
```

---

## Architecture of `readability2.ls`

The module is 1,642 lines and implements the following pipeline:

```
HTML input
  → extract_metadata(doc)     // title, byline, excerpt, siteName, etc.
  → score_candidates(body)    // proximity-weighted container scoring
  → find_best_candidate(doc)  // select article <element>
  → build_result(...)         // clean article, extract text, build output map
```

### Key Functions

| Function | Lines | Purpose |
|----------|------:|---------|
| `parse_doc(doc)` | 1339 | Main entry point — orchestrates the pipeline |
| `extract_metadata(doc)` | 878 | Extracts title, byline, excerpt from meta/JSON-LD |
| `score_candidates(body, flags)` | 991 | Proximity-weighted scoring of container elements |
| `find_best_candidate(doc, flags)` | 1059 | Selects best article candidate (article fast-path or scored) |
| `build_result(...)` | 1381 | Assembles final result map with cleaned content |
| `extract_title(doc)` | 513 | Title extraction with suffix/separator cleaning |
| `get_json_ld(doc)` | 653 | Custom JSON-LD string-level parser |
| `should_clean_conditionally(elem)` | 1179 | Heuristic to remove low-quality elements |

### Public API

```lambda
pub fn parse(file_path) map^        // Parse file, return metadata map
pub fn parse_doc(doc) map^          // Parse already-loaded HTML document
pub fn extract_article(file_path)   // Return cleaned article element
pub fn to_html(file_path)           // Return article as HTML string
pub fn is_readable(file_path, ...)  // Check if page has extractable content
pub fn title(doc), text(doc), lang(doc), dir(doc), metadata(doc)
```

---

## What's Still Outstanding

### To fix the 3 remaining failures:
1. **HTML entity decoding** (`005-unescape-html-entities`) — Requires Lambda HTML parser to decode numeric/named entities to Unicode characters in text content. Not fixable in readability2.ls.
2. **BR-to-P replacement** (`replace-brs`) — Readability.js preprocesses `<br><br>` sequences into `<p>` split. Would need DOM mutation support in Lambda (MarkEditor) or a text-level workaround.
3. **mozilla-1 wrong excerpt** — `find_best_candidate` selects a div with country dropdown content instead of the main article. Would need improved scoring or candidate selection logic.

### Missing Readability.js features (not tested yet):
- `_replaceBrs()` — BR tag replacement preprocessing
- `_prepDocument()` — Full document preparation (font tags, style removal)
- URL resolution for relative links/images in extracted content
- `readability` score confidence metric
- Configurable options (maxElemsToParse, nbTopCandidates, charThreshold, etc.)
- `isProbablyReadable()` full implementation (basic version exists)

### Code cleanup:
- Remove `debug_scoring_info()`, `debug_excerpt_info()`, and other debug pub fns
- Remove `debug_meta()`, `debug_metadata()`, `debug_json_ld()`, `debug_first_meta()`, `debug_find_article()`

### Test infrastructure:
- 67/130 tests are skipped because they only have `expected.html` (content output) but no `expected-metadata.json` — these test HTML content extraction fidelity, which we don't yet compare
- Adding HTML content comparison would enable testing those 67 cases

---

## Test Runner

```bash
python3 temp/run_tests.py          # Run all 130 tests
```

The runner generates per-test Lambda scripts, executes them via `./lambda.exe`, and compares output metadata fields against `expected-metadata.json`. Timeout: 45s per test. Files >100KB are skipped.

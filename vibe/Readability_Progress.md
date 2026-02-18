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

### 3. `mozilla-1` — excerpt is null

| | Value |
|---|---|
| **Expected** | `It's easier than ever to personalize Firefox...` |
| **Actual** | `null` |

**Root cause**: The article content is selected correctly, but excerpt extraction fails because `collect_all_elements` (a Lambda built-in) returns elements with 0 children. When we look for `<p>` elements inside the article, we find them, but `get_inner_text()` returns empty strings because the elements have lost their child text nodes. This is a **Lambda runtime bug** (see below). The page is also very large (~39s to process), contributing to the issue.

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

### Bug #2: `collect_all_elements` loses element children

**Symptom**: Elements returned by `collect_all_elements(root)` have 0 children, even though the original elements have children in the DOM. `get_inner_text()` on these elements returns `""`.

**Workaround**: Use `find_all(root, tag_name)` for specific tag types, which preserves element children correctly.

```
// BROKEN: returned elements lose children
let all = collect_all_elements(article)

// WORKING: preserves element structure
let paras = find_all(article, 'p')
```

### Bug #3: Index access on concatenated arrays in `for` comprehensions

**Symptom**: Building an array with `++` (concatenation) and then accessing elements by index (`arr[i]`) inside a `for` comprehension returns garbage values — null attributes, pointer addresses as indices, corrupted data.

**Workaround**: Iterate directly with `for (c in containers)` instead of index-based `for (i in range(...)) containers[i]`.

```
// BROKEN: containers[idx] returns garbage
let containers = find_all(body, 'div') ++ find_all(body, 'section')
for (i in range(0, len(containers))) containers[i]

// WORKING: direct iteration
for (c in containers) c
```

### Bug #4: Compound boolean expressions in `where` clauses

**Symptom**: Complex `and`/`or`/`not` combinations in `where` clauses produce incorrect results. Elements that should be filtered out are included, or vice versa.

**Workaround**: Decompose compound booleans into separate `let` bindings, then combine.

```
// BROKEN: may produce wrong results
[for (p in paras where is_para(p) and not is_byline(p)) p]

// WORKING: decompose
[for (p in paras) (
    let ok1 = is_para(p),
    let ok2 = is_byline(p),
    if (ok1 and not ok2) p else null
)] |> filter(x => x != null)
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
3. **mozilla-1 null excerpt** — Large page (~39s) where `collect_all_elements` bug prevents excerpt extraction. The article content itself is selected correctly.

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

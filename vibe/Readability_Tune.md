# Readability.ls — Performance Analysis

## Test Timing Results

**21 tests take >5s**, 26 tests take >2s, out of 63 run. Total suite time: **364s** (avg 5.78s, median 0.87s).

| Test | Time | Size | Status |
|------|-----:|-----:|--------|
| mozilla-1 | 39.5s | 95KB | FAIL |
| mercurial | 28.7s | 65KB | PASS |
| lwn-1 | 24.0s | 87KB | PASS |
| dropbox-blog | 22.8s | 80KB | PASS |
| herald-sun-1 | 22.3s | 62KB | PASS |
| google-sre-book-1 | 22.3s | 70KB | PASS |
| keep-tabular-data | 20.5s | 64KB | PASS |
| firefox-nightly-blog | 13.8s | 83KB | PASS |
| ehow-1 | 13.7s | 67KB | PASS |
| gitlab-blog | 11.9s | 72KB | PASS |
| tmz-1 | 11.7s | 92KB | PASS |
| simplyfound-1 | 11.1s | 33KB | PASS |
| lemonde-1 | 10.5s | 87KB | PASS |
| v8-blog | 10.1s | 33KB | PASS |
| ebb-org | 9.3s | 40KB | PASS |
| ars-1 | 8.8s | 56KB | PASS |
| ietf-1 | 8.5s | 65KB | PASS |
| mozilla-2 | 8.0s | 25KB | PASS |
| hukumusume | 7.7s | 25KB | PASS |
| heise | 7.2s | 62KB | PASS |
| la-nacion | 6.9s | 63KB | PASS |

Fast tests (< 1s): `replace-brs` (0.87s, 1.2KB), `embedded-videos` (0.85s, 2KB), etc.

---

## Profiling Methodology

Each test was profiled by isolating sub-operations in separate Lambda scripts, each importing the `readability2` module and running a single function on the test's HTML. Timing was measured from Python via `subprocess`. A ~0.4s baseline (module compilation + import) is subtracted to get net operation time.

Profiling hooks (`prof_*`) were temporarily added to `readability2.ls` as `pub fn` wrappers around internal functions, then removed after profiling.

---

## Root Cause Analysis

Three user functions dominate the cost, together accounting for ~85% of total time:

### 1. `find_best_candidate` → `score_candidates` (40–54% of pipeline)

The core bottleneck. Inside `score_candidates` (line ~991):

- Collects all `<div>`, `<section>`, `<article>`, `<main>` via 4 × `find_all` (recursive tree walks)
- For **each** container, calls **`get_inner_text(c, true)`** — a full recursive text extraction of the entire subtree
- Then calls **`get_link_density(c)`** on each container — which does `find_all(elem, "a")` + `get_inner_text` yet again
- Then calls `calc_para_score(p)` on each paragraph child — which calls `get_inner_text` **again**

For mozilla-1 (882 elements, 178 div/section/article/main containers), many are deeply nested. The outer `<body>` contains all inner divs. `get_inner_text` on the outer div traverses the entire 882-element tree; each inner div re-traverses its subtree. **This creates O(n²) complexity** — the same text is extracted redundantly from nested elements.

### 2. `extract_metadata` (25–36% of pipeline)

Searches for `<meta>` tags via `find_all` + iterates over many metadata keys. The `get_meta` function (line ~631) calls `find_all(head, "meta")` once per metadata key query (title, byline, excerpt, siteName, publishedTime — via `get_first_meta` with multiple fallback keys). Each call does a fresh tree traversal. Plus JSON-LD parsing via `get_json_ld` which calls `find_all(head, "script")`.

### 3. `find_byline_in_body` (12–30% of pipeline)

Does `collect_elements_skip_unlikely(body)` (full body walk), then if no byline found, falls back to `collect_all_elements(body)` (a **second** full body walk). Then for each element, calls `is_valid_byline` which invokes `get_match_string` + `matches_any` + `get_text`.

---

## The Lambda Feature Contributing Most to Slowness

### Recursive `get_inner_text` / `get_text`

The `get_inner_text` function (line ~271) calls `get_text` (line ~239) which recurses through the entire element subtree. In `score_candidates`, this is called on **every container**, causing massive redundant computation on nested elements.

`get_text` recurses depth-first through all children, concatenating strings. For a page with N elements where containers are nested k levels deep, the total work is O(N × k) — effectively O(N²) for typical HTML where most divs are children of other divs.

### `find_all` (recursive DOM search)

`find_all` (line ~332) does a full recursive tree walk every time it's called. It's invoked:
- 4× in `score_candidates` (once per tag type: div, section, article, main)
- 1× per container in `get_link_density` (searching for `<a>` tags)
- Multiple times in `extract_metadata` (meta tags, script tags)
- In `should_clean_conditionally` (p, img, li, input, embed, object, iframe — 7 calls per element)

### `matches_any` (linear pattern search)

`matches_any` (line ~98) does a linear scan of pattern arrays using `contains()` on each. Called in `is_unlikely_candidate`, `get_class_weight`, `is_valid_byline`, `is_ad_or_noise`, `is_share_element`. For each element scored, this is called 2–4× with arrays of 10–30 patterns.

---

## Probe Data (Net Times)

Times after subtracting ~0.4s import/parse overhead:

| Operation | mozilla-1 | mercurial | lwn-1 | simplyfound-1 | hukumusume |
|-----------|----------:|----------:|------:|--------------:|-----------:|
| `get_inner_text` (all body elems) | 9.2s | 4.5s | **15.8s** | 1.7s | 2.6s |
| `find_best_candidate` | **22.1s** | **14.8s** | 7.9s | **5.0s** | 1.7s |
| `extract_metadata` | 10.3s | 6.6s | 6.5s | 3.5s | 2.5s |
| `find_byline_in_body` | 4.9s | 3.7s | 7.0s | 1.6s | 2.1s |
| `collect_all_elements(body)` | 1.2s | 0.6s | 1.0s | 0.3s | 0.3s |
| `find_all(4 tags)` | 1.7s | 1.3s | 1.0s | 0.6s | 0.4s |
| `matches_any (all elems)` | 1.8s | 1.2s | 1.6s | 0.6s | 0.6s |
| `get_clean_text(body)` | 0.6s | 0.6s | 0.6s | 0.2s | 0.3s |
| `normalize_whitespace(body)` | 0.8s | 0.9s | 1.2s | 0.3s | 0.4s |
| **Full pipeline** | **41.0s** | **28.1s** | **23.4s** | **10.7s** | **7.0s** |

Note: The sum of individual operations exceeds the full pipeline time because `find_best_candidate` internally calls `get_inner_text`, `get_link_density` (which calls `find_all` + `get_inner_text`), `matches_any`, etc. — the probes measure overlapping work.

### Per-Test Breakdowns

**mozilla-1** (882 elements, 178 containers, 41.0s net):
```
find_best_candidate              22.1s (53.9%)  ##########################
extract_metadata                 10.3s (25.1%)  ############
get_inner_text (all elems)        9.2s (22.5%)  ###########
find_byline_in_body               4.9s (11.9%)  #####
matches_any (all elems)           1.8s  (4.5%)  ##
find_all(4 tags)                  1.7s  (4.2%)  ##
```

**lwn-1** (697 elements, 11 containers, 23.4s net):
```
get_inner_text (all elems)       15.8s (67.7%)  #################################
find_best_candidate               7.9s (33.7%)  ################
find_byline_in_body               7.0s (30.1%)  ###############
extract_metadata                  6.5s (27.6%)  #############
```

**hukumusume** (291 elements, 1 container, 7.0s net):
```
get_inner_text (all elems)        2.6s (37.7%)  ##################
extract_metadata                  2.5s (35.8%)  #################
find_byline_in_body               2.1s (29.8%)  ##############
find_best_candidate               1.7s (23.9%)  ###########
```

---

## Potential Optimizations

### 1. Cache `get_inner_text` results
The same element's text is computed multiple times across `score_candidates`, `get_link_density`, `calc_para_score`, and `should_clean_conditionally`. Computing text once per element and storing it in a map would eliminate O(n²) → O(n). This is the **highest-impact** optimization.

### 2. Single `find_all("meta")` call for metadata
Instead of calling `find_all(head, "meta")` per metadata key, collect all meta tags once and iterate over them for each key lookup. Same for `find_all(head, "script")` in JSON-LD parsing.

### 3. Avoid redundant body traversals
`find_byline_in_body` does up to 2 full body walks (`collect_elements_skip_unlikely` + `collect_all_elements`). The element list from scoring could be reused, or the byline search could be integrated into the scoring pass.

### 4. Skip `get_inner_text` for container filtering
The `where text_len >= 25` filter in `score_candidates` does full text extraction just to check length. A cheaper heuristic (e.g., check if element has any text children, or use a bounded-length extraction that stops after 25 chars) would avoid extracting megabytes of text only to discard short containers.

### 5. Combine `find_all` calls
`score_candidates` calls `find_all` 4 times (div, section, article, main). A single `collect_by_tags` call with all 4 tag symbols would halve the DOM traversal cost.

### 6. Pre-compile pattern matching
`matches_any` does linear `contains()` on each pattern. Combining patterns into a single regex (e.g., `"banner|sidebar|comment|..."`) would reduce per-element cost from O(patterns) to O(1) amortized. Alternatively, pre-compute a single concatenated match string per element.

### 7. Reduce `should_clean_conditionally` cost
This function (line ~1175) calls `find_all` 7 times per element (p, img, li, input, embed, object, iframe) + `collect_by_tags` for headings + `get_link_density` + `get_text_density`. For pages with many containers, this is called during `build_result` on every child of the article — multiplicative with container count.

---

## Phase 1 Caching Results (VMap)

### Changes Applied

Three caching optimizations were implemented using Lambda's new `map([k1,v1,...])` dynamic map (VMap) support:

1. **Meta tag index** (`build_meta_index`) — Single `find_all(doc, "meta")` call builds a VMap keyed by normalized meta name/property. All `extract_metadata` lookups become O(1) VMap reads instead of repeated tree traversals (addresses optimization #2).

2. **Text cache in `score_candidates`** — Builds a VMap with element references as keys and pre-computed `get_inner_text` results as values. The scoring loop and `get_link_density_cached` read from this cache instead of recomputing text (addresses optimization #1 partially — within `score_candidates`).

3. **Combined `collect_by_tags`** — Replaces 4× `find_all(body, tag)` with a single `collect_by_tags(body, ['div', 'section', 'article', 'main'])` recursive walk (addresses optimization #5).

### Before vs After (21 slowest tests)

| Test | Before | After | Speedup |
|------|-------:|------:|--------:|
| mozilla-1 | 40.8s | 21.8s | 1.9x |
| mercurial | 28.4s | 17.7s | 1.6x |
| lwn-1 | 24.7s | 14.8s | 1.7x |
| google-sre-book-1 | 24.5s | 14.0s | 1.7x |
| herald-sun-1 | 24.1s | 12.2s | 2.0x |
| dropbox-blog | 23.2s | 11.8s | 2.0x |
| keep-tabular-data | 19.7s | 11.9s | 1.7x |
| firefox-nightly-blog | 13.9s | 7.3s | 1.9x |
| ehow-1 | 13.0s | 5.9s | 2.2x |
| tmz-1 | 11.7s | 5.0s | 2.3x |
| gitlab-blog | 11.6s | 6.2s | 1.9x |
| simplyfound-1 | 11.3s | 6.2s | 1.8x |
| lemonde-1 | 10.3s | 4.1s | 2.5x |
| v8-blog | 9.9s | 6.2s | 1.6x |
| ars-1 | 9.8s | 4.4s | 2.2x |
| ebb-org | 9.5s | 4.4s | 2.2x |
| ietf-1 | 9.1s | 4.0s | 2.3x |
| mozilla-2 | 8.4s | 4.1s | 2.1x |
| hukumusume | 7.4s | 4.5s | 1.6x |
| heise | 7.1s | 2.2s | 3.3x |
| la-nacion | 6.7s | 2.7s | 2.5x |

### Summary

| Metric | Before | After | Change |
|--------|-------:|------:|-------:|
| Total suite time | 371s | 203s | **1.8x faster** |
| Tests >5s | 21 | 13 | −8 |
| Tests >2s | 26 | 23 | −3 |
| Average | 5.89s | 3.23s | **1.8x** |
| Median | 0.89s | 0.66s | 1.3x |
| Max (mozilla-1) | 40.8s | 21.8s | **1.9x** |

Biggest individual speedups: **heise** (3.3x), **lemonde-1** (2.5x), **la-nacion** (2.5x), **tmz-1** (2.3x), **ietf-1** (2.3x), **ehow-1** (2.2x).

### Remaining Bottlenecks

The text cache only covers `score_candidates` — `get_inner_text` is still called redundantly in `should_clean_conditionally` (during `build_result`), `find_byline_in_body`, and `calc_para_score`. Extending the cache to these call sites would further reduce the O(n²) behavior.

Other potential gains from the original list remain unaddressed: redundant body traversals (#3), bounded-length text extraction (#4), pattern pre-compilation (#6), and `should_clean_conditionally` cost (#7).

---

## Phase 2 Investigation: Caching get_inner_text() Beyond Containers

**Goal:** Extend the container-only VMap cache (Phase 1) to cover ALL `get_inner_text` call sites — paragraphs in `calc_para_score`, links in `get_link_density`, and all other body elements.

### Approaches Tested

#### 1. Naive Body-Wide Cache (O(n²) — REJECTED)

Pre-collect all body elements via `collect_all_elements(body)`, then call `get_inner_text(e, true)` independently for each. Build VMap from all pairs.

**Problem:** Each `get_inner_text` call recursively traverses an element's subtree. For overlapping container subtrees, the same nodes are visited repeatedly. Total work: O(n × avg_subtree_size) ≈ O(n²).

**Result — mozilla-1:** 24.4s (worse than Phase 1's 21.8s). The upfront cost of computing text for all 882 elements exceeds the savings from cached lookups.

#### 2. Bottom-Up Text Computation (O(n×depth) — MIXED)

Single recursive post-order traversal composing each element's raw text from its children's already-computed text. Each element visited exactly once. Returns `{raw, pairs}` where `raw` is used for parent composition and `pairs` accumulates `[element, normalized_text]` for VMap construction.

**Key correctness point:** Must compose from RAW child text (preserving inter-child whitespace), then normalize only at cache storage. Normalizing at each level loses whitespace between children.

**Problem:** Pair accumulation `[for (r in children) for (p in r.pairs) p]` copies all descendant pairs at each node. Total work: O(n × tree_depth). For deep trees, this approaches O(n²).

| Test | Phase 1 | Bottom-Up | Baseline |
|------|--------:|----------:|---------:|
| mozilla-1 (depth 13, 883 elems) | 21.8s | **20.7s** ✓ | 40.8s |
| mercurial (depth ?, 65KB) | 17.7s | **15.2s** ✓ | 28.4s |
| lwn-1 (depth 19, 698 elems) | **14.8s** | 24.3s ✗ | 24.7s |

**Observation:** Bottom-up helps wide-shallow trees (mozilla-1: −1.1s, mercurial: −2.5s) but devastates deep trees (lwn-1: +9.5s, back to baseline). lwn-1's depth-19 tree causes excessive pair array copying.

#### 3. Selective Block-Level Caching (O(n×depth) reduced — INSUFFICIENT)

Modified bottom-up to only add block-level elements (div, p, section, h1-h6, td, etc.) to the pairs array. Inline elements (span, a, em, strong) still participate in text computation but don't generate pair entries. Reduces total pairs from ~700 to ~200.

**Result — lwn-1:** 23.8s (only 0.5s better than full bottom-up, still 9s worse than Phase 1). The overhead isn't just pair accumulation — it's the recursive `{raw, pairs}` map allocation + `str_join` at every node of the depth-19 tree.

#### 4. Link Text Cache Addition

Added `find_all(body, "a")` to pre-compute link text alongside container text. Intended to eliminate per-link `get_inner_text` calls inside `get_link_density_cached`.

**Result:** Net negative — the upfront traversal cost (~1s) exceeded savings from cached link text lookups. Links are small (leaf-like elements), so their text is cheap to compute per-access.

### Root Cause: Pure Functional Caching Limitations

In Lambda's `fn` context (pure functions, no mutable state), comprehensive text caching faces a fundamental tradeoff:

| Approach | Complexity | Works When |
|----------|-----------|------------|
| Independent per-element | O(n²) | Never efficient for all elements |
| Bottom-up with pair accumulation | O(n × depth) | Wide, shallow trees only |
| Container-only (Phase 1) | O(n × containers) | Always — containers ≪ n |

Without mutable memoization (prohibited in `fn`), there is no O(n) way to build a flat `element → text` cache for all elements. The container-only approach is optimal because containers are the most expensive elements to compute (largest subtrees) while being few in number.

### Conclusion

**Phase 1 container-only cache remains the optimal approach.** Further `get_inner_text` caching was attempted via four strategies but none improved the overall suite performance. The remaining scoring loop cost is dominated by inherent algorithmic complexity (O(containers × children_per_container) traversals) rather than redundant text computation.

| Metric | Baseline | Phase 1 | Best Phase 2 Attempt |
|--------|-------:|------:|------:|
| mozilla-1 | 40.8s | 21.8s | 20.7s (bottom-up) |
| mercurial | 28.4s | 17.7s | 15.2s (bottom-up) |
| lwn-1 | 24.7s | **14.8s** | 24.3s (bottom-up, regression) |
| Total suite | 371s | **203s** | 206s (bottom-up, net negative) |

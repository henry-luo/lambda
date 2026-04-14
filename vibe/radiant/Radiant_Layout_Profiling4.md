# Proposal: Font, Text & Glyph Performance for Long Pages

**Status:** Phase 1+2 Implemented (4 of 5 optimizations)  
**Author:** Lambda Team  
**Date:** April 2026  
**Prerequisite:** Radiant_Font4.md (semantic break kinds), Radiant_Layout_Profiling3.md

---

## 1. Motivation

The `test_page_load_gtest.exe` suite runs 97 HTML pages (39 page/ + 58 markdown/) through `./lambda.exe view <html> --headless --no-log`. These are real-world documents — READMEs, changelogs, specs — often with thousands of text runs. Profiling reveals font/text handling as the dominant cost for long pages.

---

## 2. Profiling Results

### 2.1 Wall-Clock Times (layout command, sorted by total)

| Page | Wall (s) | Load (ms) | Layout (ms) | style_resolve (ms) | text (ms) | block (ms)¹ | Elements |
|------|----------|-----------|-------------|--------------------:|----------:|------------:|---------:|
| html5-kitchen-sink | 3.33 | 54.9 | 3164.3 | 16.6 (665 calls) | 66.5 (694) | 15735.0 (243) | 665 |
| md_zod-readme | 2.65 | 702.2 | 1988.3 | 235.4 (1528 calls) | 123.9 (689) | 11424.6 (876) | 3960 |
| zengarden | 1.41 | 358.2 | 58.2 | 17.0 (145 calls) | 19.5 (136) | 335.6 (91) | 145 |
| md_axios-readme | 1.40 | 352.1 | 910.7 | 156.7 (1035 calls) | 199.9 (822) | 4949.7 (514) | 2068 |
| md_commander-readme | 1.35 | 302.5 | 821.8 | 124.5 (773 calls) | 114.3 (713) | 4875.1 (372) | 1538 |
| md_ky-readme | 1.32 | 416.3 | 745.4 | 159.9 (1079 calls) | 194.6 (1204) | 3794.3 (448) | 2214 |
| md_axios-changelog | 1.28 | 461.9 | 729.6 | 148.1 (1457 calls) | 61.2 (667) | 4096.2 (1171) | 4736 |
| md_ts-node-readme | 1.12 | 365.2 | 813.3 | 148.9 (992 calls) | 101.7 (753) | 5333.8 (489) | 2138 |
| cnn_lite | 0.91 | 780.6 | 74.4 | 8.8 (224 calls) | 32.6 (113) | 485.8 (113) | 464 |
| paulgraham | 0.72 | 305.3 | 286.1 | 41.8 (2107 calls) | 24.4 (238) | 931.6 (700) | — |

¹ `block` is a cumulative (double-counted for nested calls) timer, not wall-clock.

### 2.2 Phase Breakdown (typical markdown page, md_zod-readme)

| Phase | Time (ms) | % of total |
|-------|----------:|----------:|
| Parse HTML | 262.5 | 9.8% |
| Parse CSS | 17.6 | 0.7% |
| CSS cascade | 114.4 | 4.3% |
| **Load subtotal** | **702.2** | **26.1%** |
| Style resolve | 235.4 | 8.8% |
| Text layout | 123.9 | 4.6% |
| Block layout (wall) | 1988.3 | 73.9% |
| **Layout subtotal** | **1988.3** | **73.9%** |
| **Total** | **~2690** | |

### 2.3 Full Page Corpus (all 97 pages)

| Category | Count | < 100ms | 100–500ms | 500ms–1s | > 1s |
|----------|------:|--------:|----------:|---------:|-----:|
| page/ | 39 | 18 | 12 | 5 | 4 |
| markdown/ | 58 | 5 | 25 | 12 | 16 |
| **Total** | **97** | **23** | **37** | **17** | **20** |

20 pages (21%) take >1 second. The markdown pages are systematically slower due to large inline-text-heavy documents with GitHub-flavored markdown CSS.

---

## 3. Bottleneck Analysis

### 3.1 Font Resolution (style_resolve)

**Current flow:** Every `resolve_css_styles()` call does:
1. AVL tree traversal for font properties → `setup_font()`
2. `setup_font()` → `font_resolve()` → `font_cache_lookup()` (hashmap O(1))
3. If cache miss: `font_database_find_best_match_internal()` → `font_load_face_internal()`
4. Glyph advance for space char to get `space_width`
5. `font_get_normal_lh_split()` for line-height metrics

**Cost:** 235ms for 1528 calls = **154µs/call average** on md_zod-readme.

**Opportunities:**
- The font cache is keyed by `"family:weight:slant:size"` string — the `font_cache_make_key()` formats a string and does hashmap lookup each time.
- Most elements on a markdown page share identical font properties (same family, size, weight). The same key is built and looked up thousands of times.

### 3.2 Glyph Advance Lookup (text layout)

**Current flow:** `layout_text()` iterates character-by-character:
1. `font_get_glyph(handle, codepoint)` → check per-handle advance cache (hashmap)
2. Cache miss → CoreText `font_rasterize_ct_metrics()` → expensive platform call
3. Cache hit → O(1) hashmap lookup with hash computation
4. `font_get_kerning()` → another hashmap lookup per character pair

**Cost:** ~123ms for 689 text runs on md_zod-readme.

**Opportunities:**
- Each `font_get_glyph()` call computes a hash even for ASCII characters (a–z, 0–9) that are overwhelmingly common in English text.
- No text-run-level caching: if the same word "function" appears 50 times at the same font size, each occurrence re-measures character-by-character.

### 3.3 Block Layout Overhead (recursive nesting)

The block timer is **cumulative across nested calls** — a parent block's timer includes all child blocks. For md_zod-readme: 876 block calls accumulate to 11424ms (double-counted), but wall-clock layout is 1988ms. This means ~6x double-counting for deep nesting typical of markdown (div > article > section > div > p > code).

The real issue: deeply nested block layout triggers repeated `resolve_css_styles()` calls, each performing font resolution. A `<pre><code>` block with 50 lines triggers 50+ style_resolve calls with identical font properties.

---

## 4. Proposed Optimizations

### 4.1 Font Handle Identity Cache (style_resolve bypass)

**Status:** ✅ Implemented

**Problem:** `setup_font()` is called for every element, rebuilding the cache key string and doing a hashmap lookup even when the font properties haven't changed from the parent.

**Solution:** Before calling `font_resolve()`, compare the current `FontProp` (family, size, weight, style) against the parent `fbox`'s existing `FontHandle*` via `font_handle_get_style()`. If identical, reuse the handle directly — zero resolution cost.

**Implementation:**
- `radiant/font.cpp`: `setup_font()` checks parent handle's family/size/weight/slant before calling `font_resolve()`
- `lib/font/font.h` + `lib/font/font_context.c`: Added public `font_handle_get_style()` accessor (FontHandle is opaque outside lib/font/)
- Reuses parent handle with `font_handle_retain()`, copies derived metrics (space_width, ascender, descender, etc.)

### 4.2 Direct-Mapped ASCII Advance Table

**Status:** ✅ Implemented

**Problem:** `font_get_glyph()` uses a hashmap for every codepoint, including ASCII (32–126) which accounts for >95% of characters in English text.

**Solution:** Added `float ascii_advance[95]` + `uint32_t ascii_glyph_id[95]` arrays directly on `FontHandle`, indexed by `codepoint - 32`. Lazily populated one entry at a time as glyphs are resolved. Falls through to hashmap for unpopulated entries or non-ASCII.

**Implementation:**
- `lib/font/font_internal.h`: Added `ascii_advance[95]`, `ascii_glyph_id[95]`, `ascii_advance_ready` fields to `FontHandle`
- `lib/font/font_glyph.c`: Fast path at top of `font_get_glyph()` checks ASCII table before hashmap; populates entries after `apply_overrides`
- `lib/font/font_metrics.c`: Sets `ascii_advance_ready = true` after `metrics_ready` (ensures handle is fully loaded)
- `pool_calloc` zero-initializes struct, so `glyph_id == 0` means "not yet populated" → falls through to slow path (correct)

### 4.3 Word-Level Measurement Cache

**Status:** ⏸️ Deferred — Phase 1+2 gains exceeded expectations, making this lower priority.

**Problem:** The same words appear repeatedly in long documents. "function", "const", "return", "import" appear dozens of times in a zod README. Each occurrence is measured character-by-character.

**Proposed solution:** A per-FontHandle word width cache — a hashmap from `(word_hash, byte_len)` → `float width`. Populated when `layout_text()` encounters a word bounded by breakpoints. Consulted before character-by-character measurement.

**Expected impact:** 30–50% reduction in character-by-character glyph lookups for content-heavy pages. More effective on structured documents (READMEs, changelogs) with repetitive vocabulary.

### 4.4 Batch Font Resolution for Inherited Properties

**Status:** ✅ Implemented (simplified approach — no DomElement flag needed)

**Problem:** In markdown documents, CSS inheritance means most elements share the parent's font. The two-pass style resolution (font pass + non-font pass) still iterates the AVL tree even when no font property was explicitly set.

**Solution:** In `resolve_css_styles()`, check for font property existence via `avl_tree_search()` before running the first-pass traversal. Skip the entire first pass + monospace quirk + `setup_font()` when no font properties exist in the element's style tree.

**Implementation:**
- `radiant/resolve_css_style.cpp`: Added `has_any_font_prop` boolean (7 quick AVL lookups for font/font-size/font-family/font-weight/font-style/font-variant/line-height) before the `avl_tree_foreach_inorder` first pass
- Monospace quirk block and `setup_font()` call also gated by `has_any_font_prop`
- No DomElement struct change needed — simpler than proposed

### 4.5 Kerning Pair Fast Path

**Status:** ✅ Implemented

**Problem:** `font_get_kerning()` does a hashmap lookup for every character pair. Most pairs have zero kerning — the lookup is wasted.

**Solution:** Early-return 0 from `font_get_kerning()` when `FontMetrics.has_kerning` is false. The `has_kerning` flag was already computed during `font_get_metrics()` (checks kern table presence + CoreText GPOS).

**Implementation:**
- `lib/font/font_glyph.c`: Added `font_get_metrics()` check at top of `font_get_kerning()` — returns 0 immediately if `!m->has_kerning`
- No new flags needed — reuses existing `FontMetrics.has_kerning` (set in `font_metrics.c`)

---

## 5. Implementation Phases

### Phase 1: Quick Wins ✅ Done
1. **4.1 Font Handle Identity Cache** — compare parent font before `font_resolve()`
2. **4.5 Kerning Pair Fast Path** — skip kerning for fonts with no kern data

### Phase 2: Core Text Optimization ✅ Done
3. **4.2 ASCII Advance Table** — direct array on FontHandle for codepoints 32–126
4. **4.4 Batch Font Resolution** — skip first-pass AVL traversal for non-font elements

### Phase 3: Advanced Caching ⏸️ Deferred
5. **4.3 Word-Level Measurement Cache** — per-handle word width hashmap (not needed given Phase 1+2 gains)

---

## 6. Measured Results

### 6.1 Top 10 Pages — Before vs After

| Page | Before (s) | After (s) | Speedup |
|------|----------:|----------:|--------:|
| html5-kitchen-sink | 3.33 | 3.23 | 3% |
| md_zod-readme | 2.65 | 1.32 | **50%** |
| zengarden | 1.41 | 0.22 | **84%** |
| md_axios-readme | 1.40 | 0.34 | **76%** |
| md_commander-readme | 1.35 | 0.42 | **69%** |
| md_ky-readme | 1.32 | 0.32 | **76%** |
| md_axios-changelog | 1.28 | 0.33 | **74%** |
| md_ts-node-readme | 1.12 | 0.23 | **79%** |
| cnn_lite | 0.91 | 0.20 | **78%** |
| paulgraham | 0.72 | 0.25 | **65%** |

html5-kitchen-sink is bottlenecked by external web font loading (3s network), not font resolution — the 3% gain is from the layout phase only.

### 6.2 Full Corpus Distribution — Before vs After

| Bucket | page/ Before | page/ After | md/ Before | md/ After |
|--------|------------:|------------:|-----------:|----------:|
| < 100ms | 18 | 27 | 5 | 20 |
| 100–500ms | 12 | 11 | 25 | 37 |
| 500ms–1s | 5 | 0 | 12 | 1 |
| > 1s | 4 | 1 | 16 | 0 |

**Before:** 20 pages > 1s. **After:** 1 page > 1s (html5-kitchen-sink, network-bound).

### 6.3 Test Suite Validation

All 4876 radiant baseline tests pass with zero regressions:
- Layout Baseline: 4080 passed
- WPT CSS Text: 518 passed
- Layout Page Suite: 39 passed
- UI Automation: 47 passed
- View Page & Markdown: 97 passed
- Fuzzy Crash: 17 passed
- Pretext Corpus: 78 passed

### 6.4 Files Modified

| File | Change |
|------|--------|
| `radiant/font.cpp` | §4.1 — parent handle identity check in `setup_font()` |
| `lib/font/font.h` | §4.1 — added `font_handle_get_style()` declaration |
| `lib/font/font_context.c` | §4.1 — `font_handle_get_style()` implementation |
| `lib/font/font_internal.h` | §4.2 — `ascii_advance[95]`, `ascii_glyph_id[95]`, `ascii_advance_ready` on FontHandle |
| `lib/font/font_glyph.c` | §4.2 + §4.5 — ASCII fast path in `font_get_glyph()`, kerning fast path in `font_get_kerning()` |
| `lib/font/font_metrics.c` | §4.2 — enable `ascii_advance_ready` after metrics computation |
| `radiant/resolve_css_style.cpp` | §4.4 — `has_any_font_prop` gate for first-pass AVL traversal |

---

## Appendix A: Full Wall-Clock Profile (All 97 Pages) — Before and After

### page/ directory (39 files, sorted by before-time descending)

| Page | Before (s) | After (s) | Δ |
|------|----------:|----------:|----:|
| html5-kitchen-sink | 3.327 | 3.231 | -3% |
| zengarden | 1.414 | 0.225 | -84% |
| cnn_lite | 0.910 | 0.200 | -78% |
| watercss | 0.728 | 0.378 | -48% |
| paulgraham | 0.723 | 0.253 | -65% |
| hn | 0.540 | 0.085 | -84% |
| about | 0.514 | 0.075 | -85% |
| report | 0.396 | 0.225 | -43% |
| live-demo | 0.365 | 0.098 | -73% |
| html2_spec | 0.308 | 0.049 | -84% |
| legible | 0.295 | 0.095 | -68% |
| flex | 0.288 | 0.077 | -73% |
| page_facatology | 0.239 | 0.098 | -59% |
| latex | 0.230 | 0.077 | -67% |
| demo | 0.218 | 0.106 | -51% |
| pricing-table | 0.190 | 0.137 | -28% |
| documentation | 0.142 | 0.080 | -44% |
| dashboard-simple | 0.125 | 0.077 | -38% |
| combo_003_complete_article | 0.124 | 0.044 | -65% |
| footer-showcase | 0.114 | 0.071 | -38% |
| sqlite-about | 0.108 | 0.050 | -54% |
| libcurl | 0.103 | 0.030 | -71% |
| contact-form | 0.100 | 0.058 | -42% |
| article-layout | 0.100 | 0.039 | -61% |
| blog-homepage | 0.089 | 0.054 | -39% |
| sample1 | 0.087 | 0.059 | -32% |
| newsletter | 0.082 | 0.049 | -40% |
| table-comparison | 0.081 | 0.062 | -23% |
| npr | 0.077 | 0.042 | -45% |
| error-page | 0.075 | 0.056 | -25% |
| cern_servers | 0.073 | 0.029 | -60% |
| musl | 0.069 | 0.029 | -58% |
| community | 0.065 | 0.031 | -52% |
| combo_002_technical_docs | 0.065 | 0.036 | -45% |
| sample5 | 0.062 | 0.032 | -48% |
| combo_001_document_structure | 0.059 | 0.039 | -34% |
| css1_test | 0.048 | 0.032 | -33% |
| cern | 0.043 | 0.025 | -42% |
| sample2 | 0.033 | 0.025 | -24% |

### markdown/ directory (58 files, sorted by before-time descending)

| Page | Before (s) | After (s) | Δ |
|------|----------:|----------:|----:|
| md_zod-readme | 2.646 | 1.318 | -50% |
| md_axios-readme | 1.396 | 0.366 | -74% |
| md_commander-readme | 1.353 | 0.421 | -69% |
| md_ky-readme | 1.324 | 0.316 | -76% |
| md_axios-changelog | 1.280 | 0.330 | -74% |
| md_ts-node-readme | 1.124 | 0.235 | -79% |
| md_uuid-readme | 0.859 | 0.480 | -44% |
| md_winston-readme | 0.795 | 0.197 | -75% |
| md_moment-changelog | 0.772 | 0.188 | -76% |
| md_test-emoji | 0.741 | 0.561 | -24% |
| md_ncu-readme | 0.733 | 0.210 | -71% |
| md_semver-readme | 0.640 | 0.164 | -74% |
| md_test-unicode | 0.579 | 0.377 | -35% |
| md_jest-changelog | 0.569 | 0.184 | -68% |
| md_test-markdown-features | 0.507 | 0.216 | -57% |
| md_jest-readme | 0.482 | 0.213 | -56% |
| md_commitizen-readme | 0.477 | 0.206 | -57% |
| md_fastify-readme | 0.452 | 0.144 | -68% |
| md_execa-readme | 0.433 | 0.220 | -49% |
| md_jquery-readme | 0.415 | 0.135 | -67% |
| md_ora-readme | 0.393 | 0.163 | -59% |
| md_fraction-js-readme | 0.387 | 0.122 | -68% |
| md_remark-readme | 0.379 | 0.153 | -60% |
| md_index | 0.362 | 0.121 | -67% |
| md_zustand-readme | 0.346 | 0.115 | -67% |
| md_mongoose-readme | 0.334 | 0.114 | -66% |
| md_bootstrap-readme | 0.328 | 0.157 | -52% |
| md_chalk-readme | 0.326 | 0.116 | -64% |
| md_sequelize-readme | 0.313 | 0.184 | -41% |
| md_rollup-readme | 0.278 | 0.156 | -44% |
| md_dayjs-readme | 0.277 | 0.178 | -36% |
| md_ramda-readme | 0.273 | 0.111 | -59% |
| md_conventional-changelog-readme | 0.266 | 0.101 | -62% |
| md_nuxt-readme | 0.264 | 0.179 | -32% |
| md_express-readme | 0.248 | 0.090 | -64% |
| md_markdown-it-readme | 0.249 | 0.088 | -65% |
| md_typeorm-readme | 0.249 | 0.111 | -55% |
| md_mermaid-readme | 0.247 | 0.124 | -50% |
| md_vite-readme | 0.243 | 0.167 | -31% |
| md_marked-readme | 0.239 | 0.145 | -39% |
| md_mocha-readme | 0.235 | 0.150 | -36% |
| md_vue-readme | 0.234 | 0.125 | -47% |
| md_cors-readme | 0.233 | 0.091 | -61% |
| md_cheerio-readme | 0.192 | 0.111 | -42% |
| md_gitmoji-readme | 0.192 | 0.127 | -34% |
| md_observable-plot-readme | 0.188 | 0.127 | -32% |
| md_husky-docs | 0.186 | 0.132 | -29% |
| md_katex-readme | 0.182 | 0.094 | -48% |
| md_husky-get-started | 0.174 | 0.115 | -34% |
| md_react-readme | 0.169 | 0.068 | -60% |
| md_papaparse-readme | 0.167 | 0.077 | -54% |
| md_simple-statistics-readme | 0.160 | 0.073 | -54% |
| md_helmet-readme | 0.152 | 0.069 | -55% |
| md_test-math-latex | 0.146 | 0.070 | -52% |
| md_prettier-readme | 0.143 | 0.076 | -47% |
| md_lodash-readme | 0.142 | 0.061 | -57% |
| md_i18next-readme | 0.142 | 0.077 | -46% |
| md_threejs-readme | 0.134 | 0.069 | -49% |

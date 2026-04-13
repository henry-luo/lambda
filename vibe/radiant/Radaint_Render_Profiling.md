# Radiant Render Pipeline Profiling & Optimization Proposal

This document captures performance profiling of the Radiant rasterization/rendering pipeline, identifies critical bottlenecks, and proposes targeted optimizations.

## Test Setup

- **Platform**: macOS (Apple Silicon), CoreText font subsystem
- **Build**: Release (`make release`, -O2, NDEBUG, LTO, dead-strip)
- **Viewport**: 1200×800 headless surface
- **Test pages**: 97 HTML pages (39 page/ + 58 markdown/) via `./release/lambda view <html> --headless --no-log`
- **Profiling method**: `std::chrono::high_resolution_clock` timers in render.cpp + macOS `sample` CPU profiler

## Render Pipeline Overview

```
render_html_doc()
  └─ render_block_view()           [recursive tree walk]
       ├─ setup_font()             [CoreText font resolution]
       ├─ render_bound()           [backgrounds, borders, shadows, outlines]
       ├─ save_clip_region()       [overflow:hidden + border-radius pixel save]
       ├─ render_children()        [dispatch loop over child views]
       │    ├─ render_text_view()  [glyph loading, positioning, drawing]
       │    ├─ render_inline_view()
       │    ├─ render_block_view() [recursive]
       │    └─ render_marker_view()
       ├─ apply_clip_mask()        [*** BOTTLENECK: per-pixel rounded rect test ***]
       ├─ apply_css_filters()
       └─ opacity / blend / clip-path
```

---

## Profiling Results: Full 97-Page Summary

### Top 20 Slowest Pages (Release Build)

| Page | Render (ms) | overflow_clip (ms) | clip % | font_metrics (ms) | fm % | text (ms) | bound (ms) |
|------|------------|-------------------|--------|-------------------|------|-----------|------------|
| md_axios-changelog | 2531.9 | 2331.3 | **92%** | 63.2 | 2% | 78.1 | 15.3 |
| md_test-emoji | 2518.9 | 175.7 | 7% | 24.0 | 1% | **2327.5** | 5.6 |
| md_zod-readme | 2186.2 | 1860.2 | **85%** | 172.7 | 8% | 223.4 | 16.0 |
| md_moment-changelog | 1647.9 | 1467.6 | **89%** | 90.2 | 5% | 103.5 | 8.9 |
| md_axios-readme | 1534.7 | 1055.1 | **69%** | 198.7 | 13% | 407.3 | 18.0 |
| md_ky-readme | 1273.7 | 934.6 | **73%** | 200.4 | 16% | 280.7 | 12.4 |
| md_ts-node-readme | 1224.3 | 1015.9 | **83%** | 128.8 | 11% | 148.3 | 11.3 |
| md_test-markdown-features | 1202.8 | 1026.9 | **85%** | 48.2 | 4% | 111.5 | 15.4 |
| md_commander-readme | 1003.2 | 766.3 | **76%** | 171.8 | 17% | 191.3 | 10.0 |
| md_ncu-readme | 911.8 | 696.8 | **76%** | 147.5 | 16% | 164.9 | 14.2 |
| md_winston-readme | 847.4 | 669.4 | **79%** | 108.8 | 13% | 128.3 | 11.5 |
| report | 802.6 | 583.4 | **73%** | 19.3 | 2% | 145.6 | 38.4 |
| md_uuid-readme | 678.5 | 562.5 | **83%** | 55.4 | 8% | 71.9 | 15.0 |
| md_semver-readme | 666.9 | 524.0 | **79%** | 92.8 | 14% | 110.2 | 8.3 |
| md_jest-changelog | 631.0 | 528.9 | **84%** | 54.7 | 9% | 69.6 | 6.5 |
| md_fastify-readme | 584.0 | 462.8 | **79%** | 73.0 | 12% | 86.0 | 7.2 |
| md_index | 521.8 | 473.7 | **91%** | 14.4 | 3% | 19.7 | 5.6 |
| md_jquery-readme | 510.9 | 400.1 | **78%** | 71.2 | 14% | 84.8 | 7.7 |
| md_fraction-js-readme | 493.9 | 378.4 | **77%** | 79.0 | 16% | 91.4 | 8.1 |
| md_execa-readme | 487.4 | 277.3 | **57%** | 23.2 | 5% | 151.3 | 6.7 |

### Fast Pages (no overflow clipping)

| Page | Render (ms) | clip % | Notes |
|------|------------|--------|-------|
| sample2 | 0.1 | 0% | Trivial page |
| css1_test | 0.8 | 0% | No border-radius |
| cern | 1.1 | 0% | Simple text |
| about | 3.5 | 0% | No code blocks |
| paulgraham | 4.1 | 0% | Plain text layout |
| blog-homepage | 17.7 | 0% | CSS-heavy but no clip |

### Aggregate Statistics (97 pages)

| Metric | Value |
|--------|-------|
| Total render time (all pages) | ~31,760 ms |
| Total overflow_clip time | ~24,260 ms (**76.4%**) |
| Total font_metrics time | ~3,785 ms (11.9%) |
| Total text render time | ~9,460 ms (29.8%) |
| Total bound (bg+border) time | ~510 ms (1.6%) |
| Pages with >50% clip time | 54 of 97 (56%) |
| Pages with >80% clip time | 17 of 97 (18%) |

---

## Bottleneck Analysis

### Bottleneck #1: `apply_clip_mask` — Per-Pixel Rounded Rect Clipping (76% of render time)

**Location**: `render.cpp`, `apply_clip_mask()` → `point_in_rounded_rect()`

**What it does**: When a block has `overflow:hidden` (or `overflow:auto/scroll`) AND `border-radius > 0`, the renderer:
1. **Saves** all pixels in the block's bounding rect (`save_clip_region`)
2. Renders all children into the block area
3. **Restores** original pixels for every pixel OUTSIDE the rounded rect (`apply_clip_mask`)

**Why it's slow**: The `apply_clip_mask` function iterates over **every pixel** in the clipped region and calls `point_in_rounded_rect()` for each pixel. This involves:
- Per-pixel floating-point coordinate computation
- 4 corner distance checks (each with multiply + compare)
- A function call per pixel (not inlined in debug, inlined but still per-pixel in release)

For a typical markdown code block at 1200px viewport width:
- Block region: ~1150 × 200 pixels = **230,000 pixels** per code block
- With ~50 code blocks per markdown page: **~11.5 million pixel tests**
- Each test: ~5-10 float operations → **~100 million float ops** just for clipping

**Impact**: md_axios-changelog has 1089 clip operations taking 2331ms (92% of 2532ms render time). Even small pages like table-comparison: 59 clips = 138ms (82% of 168ms).

**Key insight**: The vast majority of pixels are in the **straight-edge interior** of the rounded rect where no corner check is needed. Only ~4 small corner regions require the distance test.

### Bottleneck #2: `font_get_rendering_ascender` — Per-Glyph CTFont Creation (12% of render time)

**Location**: `font_metrics.c:508` → `font_platform.c:396` → `CTFontCreateWithName()`

**What it does**: `render_text_view()` calls `font_get_rendering_ascender()` for every glyph to get the font's ascent metric for baseline positioning. Each call:
1. Creates a `CFStringRef` from the font family name
2. Calls `CTFontCreateWithName()` to create a brand-new CoreText font object
3. Queries `CTFontGetAscent()`, `CTFontGetDescent()`, `CTFontGetLeading()`
4. Releases the CTFont

**Why it's slow**: `CTFontCreateWithName` is expensive (~8-10μs per call on Apple Silicon). It involves:
- Font descriptor matching and variation instantiation
- `TInstanceFont::InitWithVariation` → `CGFontCreateWithVariations` → font table parsing
- File system operations (font URL resolution)

**Impact**: For md_zod-readme: 20,047 calls × ~8.6μs = 172.7ms (8%). For md_ky-readme: 200.4ms (16%). The value returned is **always the same** for a given (family, size) pair — pure waste.

### Bottleneck #3: Emoji Font Loading (dominates emoji-heavy pages)

**Location**: `font_load_glyph_emoji()` in render_text_view

**Impact**: md_test-emoji: 4,894 glyph loads taking 2,500ms (0.51ms per emoji glyph vs 0.003ms for normal glyphs — **170× slower**). This is a CoreText/system font issue but caching could help for repeated emoji.

---

## Optimization Proposals

### Optimization 1: Scanline-Based Overflow Clip (Target: ~90% reduction in clip time)

**Current**: Per-pixel `point_in_rounded_rect()` for entire clipped region.

**Proposed**: Row-scanline approach that skips interior rows entirely:

```
For each row in clip region:
  if row is in straight-edge zone (between corners):
    → All pixels inside rect boundary are "inside" — skip entirely (no restore needed)
  if row is in a corner zone:
    → Compute exact x-entry and x-exit from circle equation: x = cx ± sqrt(r² - (y-cy)²)
    → Restore only pixels outside [x_entry, x_exit] using memcpy
```

**Benefits**:
- **Interior rows** (typically 85-95% of rows): Zero per-pixel work
- **Corner rows**: One sqrt + two memcpy per row instead of W point-in-rect tests
- Estimated speedup: Current 2331ms → ~100-200ms for md_axios-changelog (**10-20× faster**)

**Alternative**: Precompute a 1-bit scanline mask (array of [x_start, x_end] per row) during `save_clip_region`, use it during `apply_clip_mask` for O(1) per-row restoration.

### Optimization 2: Cache `font_get_rendering_ascender` (Target: ~95% reduction in metrics time)

**Current**: Creates a new `CTFontRef` per call via `CTFontCreateWithName()`.

**Proposed**: Add a small lookup cache in `font_get_rendering_ascender()`:

```c
float font_get_rendering_ascender(FontHandle* handle) {
    if (!handle) return 0;
    // Check if cached on the handle
    if (handle->cached_rendering_ascender != 0)
        return handle->cached_rendering_ascender;
    // ... existing CTFont creation logic ...
    handle->cached_rendering_ascender = ascent;
    return ascent;
}
```

**Benefits**:
- Eliminates ~20,000 `CTFontCreateWithName` calls per markdown page
- md_zod-readme: 172.7ms → ~0.5ms (**345× faster**)
- Total across 97 pages: ~3,785ms → ~20ms

**Alternative**: Hoist the `font_get_rendering_ascender()` call out of the per-glyph loop in `render_text_view()` — compute once per text_rect instead of per character. This is simpler and equally effective since the font doesn't change within a single text run.

### Optimization 3: Viewport Culling in Overflow Clip (Target: further 30-50% clip reduction)

**Current**: Save/clip the entire block area even when most of it is off-screen.

**Proposed**: Intersect the clip region with the viewport before saving/applying:

```c
// In save_clip_region, clamp to viewport
int x0 = max(mask_x, 0);
int y0 = max(mask_y, 0);
int x1 = min(mask_x + mask_w, surface->width);
int y1 = min(mask_y + mask_h, surface->height);
```

**Benefits**: For a 1200×800 viewport, a block that extends 5000px tall only needs clipping for the visible 800px. This is especially impactful for markdown pages with very tall content where code blocks extend well below the viewport.

### Optimization 4: Emoji Glyph Cache (Target: ~90% reduction for emoji pages)

**Current**: Each emoji glyph rendering calls `font_load_glyph_emoji()` which invokes CoreText.

**Proposed**: Cache loaded emoji glyphs by codepoint, similar to the existing ASCII glyph cache. Emoji have a small active set per document (typically <200 unique emoji).

**Benefits**: md_test-emoji: 2,500ms → ~250ms

---

## Expected Impact Summary

| Optimization | Affected Time | Expected Reduction | Pages Impacted |
|-------------|---------------|-------------------|----------------|
| 1. Scanline clip | 24,260ms (76%) | **~90% → save ~21,800ms** | 54 of 97 |
| 2. Cache font metrics | 3,785ms (12%) | **~99% → save ~3,750ms** | All 97 |
| 3. Viewport clip cull | ~5,000ms est. | **~40% → save ~2,000ms** | Tall pages |
| 4. Emoji glyph cache | ~2,500ms | **~90% → save ~2,250ms** | Emoji pages |
| **Total** | **~31,760ms** | **→ ~4,000ms** | **~87% reduction** |

### Per-Page Expected Improvement (Top 5)

| Page | Current (ms) | After Opt 1+2 (ms) | Speedup |
|------|-------------|---------------------|---------|
| md_axios-changelog | 2532 | ~250 | **10×** |
| md_zod-readme | 2186 | ~200 | **11×** |
| md_moment-changelog | 1648 | ~180 | **9×** |
| md_axios-readme | 1535 | ~350 | **4.4×** |
| md_ts-node-readme | 1224 | ~150 | **8×** |

---

## Implementation Priority

1. **Optimization 2 (Cache font metrics)** — Simplest change, immediate ~12% total speedup, ~1 hour
2. **Optimization 1 (Scanline clip)** — Largest impact, ~76% total speedup, ~2-4 hours
3. **Optimization 3 (Viewport cull)** — Additional gains for tall pages, ~1 hour
4. **Optimization 4 (Emoji cache)** — Specialized but dramatic for emoji content, ~2 hours

---

## Profiling Infrastructure

Timing counters added to `render.cpp` (to be removed after optimization):
- `g_render_overflow_clip_time/count` — apply_clip_mask time
- `g_render_font_metrics_time/count` — font_get_rendering_ascender time
- `g_render_bound_time/count` — render_bound (backgrounds, borders)
- `g_render_text_total_time/count` — render_text_view total
- `g_render_block_self_time` — render_block_view self-time (excl. children)
- `g_render_inline_time/count` — render_inline_view
- `stderr_render_stats()` — outputs all counters to stderr (works with `--no-log`)

Full data: `temp/render_profile_v2.tsv` (97 pages, 18 columns)

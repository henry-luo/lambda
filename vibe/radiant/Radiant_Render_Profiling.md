# Radiant Render Pipeline Profiling & Optimization Proposal

This document captures performance profiling of the Radiant rasterization/rendering pipeline, identifies critical bottlenecks, and proposes targeted optimizations.

## Test Setup

- **Platform**: macOS (Apple Silicon), CoreText font subsystem
- **Build**: Release (`make release`, -O2, NDEBUG, LTO, dead-strip)
- **Viewport**: 1200×800 headless surface
- **Test pages**: 97 HTML pages (39 page/ + 58 markdown/) via `./release/lambda view <html> --headless --no-log`
- **Profiling method**: `std::chrono::high_resolution_clock` timers in render.cpp + layout.cpp, output via `[RENDER_PROF]` and `[LAYOUT_PROF]` to stderr

## Render Pipeline Overview

```
render_html_doc()
  └─ render_block_view()           [recursive tree walk]
       ├─ setup_font()             [CoreText font resolution]
       ├─ render_bound()           [backgrounds, borders, shadows, outlines]
       ├─ rdt_push_clip()          [overflow:hidden via ThorVG clip shape stack]
       ├─ render_children()        [dispatch loop over child views]
       │    ├─ render_text_view()  [glyph loading, positioning, drawing]
       │    ├─ render_inline_view()
       │    ├─ render_block_view() [recursive]
       │    └─ render_marker_view()
       ├─ rdt_pop_clip()           [restore clip state]
       ├─ apply_css_filters()
       └─ opacity / blend / clip-path
```

---

## Profiling Results (Post-Clipping Optimization)

After resolving Bottleneck #1 (overflow clip), `overflow_clip` time is now ~0ms across all pages. The remaining render bottlenecks are `font_metrics` (33.8% of render) and text rendering.

### Top 20 Slowest Pages by Render Time (Release Build)

| Page | Layout (ms) | Render (ms) | font_metrics (ms) | fm % | text (ms) | bound (ms) |
|------|------------|------------|-------------------|------|-----------|------------|
| md_test-emoji | 484.5 | **2454.4** | 24.9 | 1% | **2447.3** | 5.6 |
| md_axios-readme | 145.9 | **478.2** | 200.8 | 42% | 441.0 | 18.3 |
| md_ky-readme | 127.8 | **330.3** | 202.6 | 61% | 304.3 | 12.2 |
| md_zod-readme | 866.2 | **276.6** | 175.2 | 63% | 227.0 | 15.6 |
| md_commander-readme | 261.0 | **234.9** | 180.0 | 77% | 212.1 | 10.1 |
| md_execa-readme | 153.7 | **209.5** | 23.1 | 11% | 164.9 | 6.9 |
| md_ncu-readme | 58.0 | **194.5** | 145.0 | 75% | 169.6 | 14.0 |
| md_ts-node-readme | 52.4 | **183.5** | 126.9 | 69% | 153.3 | 11.1 |
| report | 135.3 | **168.4** | 19.2 | 11% | 126.8 | 38.8 |
| md_nuxt-readme | 117.0 | **162.7** | 20.1 | 12% | 151.0 | 5.5 |
| md_winston-readme | 48.9 | **160.3** | 107.6 | 67% | 139.8 | 11.5 |
| md_axios-changelog | 67.2 | **160.4** | 65.2 | 41% | 79.8 | 14.7 |
| md_semver-readme | 40.7 | **155.7** | 94.3 | 61% | 135.0 | 8.2 |
| md_mermaid-readme | 78.7 | **152.4** | 107.6 | 71% | 146.3 | 4.6 |
| md_test-unicode | 287.1 | **149.0** | 40.4 | 27% | 141.1 | 6.5 |
| md_commitizen-readme | 125.2 | **144.4** | 81.3 | 56% | 111.4 | 8.0 |
| md_moment-changelog | 40.3 | **143.2** | 88.7 | 62% | 101.4 | 8.8 |
| md_test-markdown-features | 106.9 | **139.2** | 49.1 | 35% | 115.2 | 15.0 |
| md_sequelize-readme | 135.9 | **134.0** | 17.0 | 13% | 123.6 | 5.0 |
| md_jest-readme | 125.1 | **127.7** | 37.2 | 29% | 97.5 | 6.2 |

### Top 10 Slowest Pages by Layout Time (Release Build)

| Page | Layout (ms) | Render (ms) | Notes |
|------|------------|------------|-------|
| html5-kitchen-sink | **3345.1** | 8.2 | Layout pathology (complex CSS) |
| md_zod-readme | **866.2** | 276.6 | Many inline code spans |
| md_test-emoji | **484.5** | 2454.4 | Emoji text shaping |
| watercss | **316.4** | 62.9 | Complex selectors |
| md_uuid-readme | **340.1** | 112.5 | |
| md_test-unicode | **287.1** | 149.0 | Unicode text shaping |
| md_commander-readme | **261.0** | 234.9 | |
| md_execa-readme | **153.7** | 209.5 | |
| md_axios-readme | **145.9** | 478.2 | |
| md_dayjs-readme | **143.8** | 118.5 | |

### Aggregate Statistics (97 pages, Release Build)

| Metric | Value |
|--------|-------|
| Total layout time (all pages) | ~10,039 ms |
| Total render time (all pages) | ~9,061 ms |
| Total overflow_clip time | **~0 ms (RESOLVED)** |
| Total font_metrics time | ~3,062 ms (**33.8% of render**) |
| Total text render time | ~7,709 ms (85.1% of render) |
| Total bound (bg+border) time | ~631 ms (7.0% of render) |

### Comparison: Before vs After All Optimizations

| Metric | Original | After Clip Fix | After All Opts | Overall |
|--------|----------|---------------|----------------|---------|
| Total render (97 pages) | ~31,760 ms | ~9,061 ms | **~6,067 ms** | **5.2× faster** |
| overflow_clip time | ~24,260 ms | ~0 ms | ~0 ms | **eliminated** |
| font_metrics time | ~3,785 ms | ~3,062 ms | **~14 ms** | **270× faster** |
| font_metrics % of render | 11.9% | 33.8% | **0.2%** | **eliminated** |
| md_axios-changelog render | 2,532 ms | 160 ms | **96 ms** | **26× faster** |
| md_zod-readme render | 2,186 ms | 277 ms | **107 ms** | **20× faster** |
| md_commander-readme render | 1,003 ms | 235 ms | **54 ms** | **19× faster** |
| md_ky-readme render | 1,274 ms | 330 ms | **108 ms** | **12× faster** |
| report render | 803 ms | 168 ms | **139 ms** | **5.8× faster** |

---

## Bottleneck Analysis

### Bottleneck #1: `apply_clip_mask` — ✅ RESOLVED

**Status**: Fully resolved. See `Radiant_Render_Clipping.md` for implementation details.

**What was done**: Replaced per-pixel `save_clip_region`/`apply_clip_mask` with ThorVG clip shape stack (`rdt_push_clip`/`rdt_pop_clip`) for vector operations and per-operation scanline clipping for direct-pixel operations (glyphs, fills, blits). All clip shape types supported: rounded-rect, circle, ellipse, inset, polygon.

**Result**: overflow_clip time reduced from ~24,260ms → ~0ms across all 97 pages. Total render time reduced from ~31,760ms → ~9,061ms (**3.5× overall speedup**). All 4876 radiant baseline tests pass.

### Bottleneck #2: `font_get_rendering_ascender` — ✅ RESOLVED

**Location**: `font_metrics.c`, `font_get_rendering_ascender()`

**What it did**: Created a new `CTFontRef` per call via `CTFontCreateWithName()` for every glyph rendered — ~8-10μs per call on Apple Silicon.

**Fix**: Added `cached_rendering_ascender` + `cached_rendering_ascender_ready` fields to `FontHandle` struct (`font_internal.h`). First call computes and caches; subsequent calls return cached value in O(1).

**Result**: font_metrics time reduced from ~3,062ms → ~14ms across 97 pages (**219× faster**). Was 33.8% of render time, now 0.2%.

### Bottleneck #3: Emoji Font Loading — ✅ RESOLVED

**Location**: `font_glyph.c`, `font_load_glyph_emoji()`

**What it did**: Two separate costs per unique emoji glyph:
1. **Font resolution**: `font_platform_find_emoji_font()` called `CTFontCreateForString()` per codepoint to locate Apple Color Emoji — expensive CoreText font matching.
2. **Face creation**: `font_load_face_internal()` opened the font file, detected format, and created CoreText font objects — even though all emoji use the same font.
3. **No glyph cache**: The emoji path bypassed `loaded_glyph_cache` entirely.

**Fixes applied (two phases)**:
- **Phase 1 — Glyph cache**: Added `loaded_glyph_cache` lookup at the start of `font_load_glyph_emoji()` and cache store after successful load. Repeated emoji (same codepoint) return from cache in O(1).
- **Phase 2 — FontHandle cache**: Added `cached_emoji_handle` on `FontContext` (`font_internal.h`). The emoji `FontHandle` is created once and reused across all subsequent `font_load_glyph_emoji()` calls when the style matches, eliminating per-glyph `font_platform_find_emoji_font()` + `font_load_face_internal()` overhead.

**Result**: md_test-emoji render **2,451ms → 223ms (11× faster)**. The remaining ~200ms is the irreducible CoreText bitmap rasterization for 446 unique color emoji glyphs (~0.45ms each).

---

## Optimization Proposals

### Optimization 1: Overflow Clip Redesign — ✅ DONE

Replaced per-pixel `save_clip_region`/`apply_clip_mask` with ThorVG clip shape stack + per-operation scanline clipping. See `Radiant_Render_Clipping.md`.

**Result**: overflow_clip time reduced from ~24,260ms → ~0ms. Total render 3.5× faster.

### Optimization 2: Cache `font_get_rendering_ascender` — ✅ DONE

Added `cached_rendering_ascender` + `cached_rendering_ascender_ready` fields to `FontHandle` struct in `font_internal.h`. First call computes via CoreText and caches; subsequent calls return cached value in O(1). FontHandle is allocated via `pool_calloc` so fields start zero-initialized.

**Result**: font_metrics time reduced from ~3,062ms → ~14ms across 97 pages (**219× faster**). Was 33.8% of render time, now 0.2%.

- md_commander-readme: 180.0ms → 0.6ms (300×)
- md_ky-readme: 202.6ms → 0.8ms (253×)

### Optimization 3: Viewport Culling — ✅ N/A (superseded by Optimization 1)

Viewport culling was proposed for the old pixel-save clipping approach. With the ThorVG clip shape stack, clipping operations are essentially free (~0ms), making viewport culling unnecessary.

### Optimization 4: Emoji Rendering — ✅ DONE

Two-phase optimization:

**Phase 1 — Glyph cache**: Added `loaded_glyph_cache` lookup/store in `font_load_glyph_emoji()`, same pattern as `font_load_glyph()`. Repeated emoji return from cache — 24× faster (0.022ms vs 0.54ms per emoji).

**Phase 2 — FontHandle cache**: Added `cached_emoji_handle` + style fields on `FontContext` (`font_internal.h`). The emoji `FontHandle` (Apple Color Emoji at the requested size) is created once and reused, eliminating per-glyph `font_platform_find_emoji_font()` (CoreText `CTFontCreateForString`) + `font_load_face_internal()` (file I/O, CoreText font creation) costs. Released in `font_context_destroy()`.

**Result**: md_test-emoji render **2,451ms → 223ms (11× faster)**. Remaining ~200ms is irreducible CoreText bitmap rasterization for 446 unique color emoji.

---

## Optimization Results Summary

| Optimization | Before | After | Speedup | Status |
|-------------|--------|-------|---------|--------|
| 1. Overflow clip redesign | 24,260ms | ~0ms | eliminated | ✅ DONE |
| 2. Cache font metrics | 3,062ms | 14ms | 219× | ✅ DONE |
| 3. Viewport cull | N/A | N/A | N/A | ✅ Superseded |
| 4. Emoji rendering | 2,451ms (md_test-emoji) | 223ms | 11× | ✅ DONE |
| **Total render (97 pages)** | **31,760ms** | **6,067ms** | **5.2×** | **All complete** |

---

## Implementation History

1. ~~**Optimization 1 (Overflow clip)**~~ — ✅ DONE (3.5× overall speedup)
2. ~~**Optimization 2 (Cache font metrics)**~~ — ✅ DONE (219× reduction in font_metrics, 33% overall render savings)
3. ~~**Optimization 3 (Viewport cull)**~~ — ✅ Superseded by Optimization 1
4. ~~**Optimization 4 (Emoji rendering)**~~ — ✅ DONE (11× for md_test-emoji: 2,451ms → 223ms)

**Remaining bottleneck**: Text rendering now dominates at 4,621ms (76% of 6,067ms total render). Layout time (10,039ms total) is now larger than render time, suggesting layout optimization may be the next frontier.

---

## Profiling Infrastructure

Timing counters in `render.cpp` and `layout.cpp`:
- `[LAYOUT_PROF] layout_html_root: XXms` — layout time (stderr)
- `[RENDER_PROF] render_block_view: XXms` — total render time (stderr)
- `[RENDER_PROF] font_metrics=N(XXms)` — font_get_rendering_ascender time
- `[RENDER_PROF] overflow_clip=N(XXms)` — clip time (now ~0ms)
- `[RENDER_PROF] text=N(XXms)` — text rendering time
- `[RENDER_PROF] bound=N(XXms)` — backgrounds, borders time
- `stderr_render_stats()` — outputs all counters to stderr (works with `--no-log`)

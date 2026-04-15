# Radiant Render Clipping Redesign

## Problem

The current clipping implementation uses a brute-force **pixel save/restore** approach:

1. `save_clip_region()` — copies **every pixel** in the clip bounding rect (O(w×h))
2. Children render freely
3. `apply_clip_mask()` — iterates **every pixel**, runs `point_in_rounded_rect()` per pixel, restores saved pixel if outside shape (O(w×h))

This is the #1 rendering bottleneck: **76% of total render time** across 97 test pages.
For a 500×300 region with 8px corner radii, it saves 150K pixels and tests 150K points, but only ~200 corner pixels actually need restoring.

The root issue: Radiant was designed with vector-shape clipping via ThorVG, but the pixel-level save/restore was added as a workaround because `draw_glyph()` and `fill_surface_rect()` bypass ThorVG and write directly to the surface pixel buffer.

## Design Principle

**Use vector shape clipping everywhere. No raster pixel save/restore.**

- All shaped clipping (rounded-rect, circle, ellipse, polygon) is done via vector clip paths
- Only CSS `mask-image` with an alpha bitmap requires raster masking (not in scope here)
- Each rendering operation is responsible for clipping itself to the active clip shape

## Current Rendering Operations Inventory

### Category 1: ThorVG Vector Calls (~40 call sites)

These go through `rdt_*` API → ThorVG rasterizer → surface pixels.

| Operation | Example Call Sites |
|---|---|
| Background color (with radius) | `rdt_fill_path` in render_background.cpp |
| Linear/radial gradients | `rdt_fill_linear_gradient`, `rdt_fill_radial_gradient` |
| Box shadows (inner/outer/drop) | `rdt_fill_path` in render_background.cpp |
| Borders (beveled, rounded, double) | `rdt_fill_path`, `rdt_stroke_path` in render_border.cpp |
| Outline | `rdt_stroke_path` in render_border.cpp |
| Text decoration (wavy) | `rdt_stroke_path` in render.cpp |
| List markers (disc, circle, disclosure) | `rdt_fill_path`, `rdt_stroke_path` in render.cpp |
| SVG inline rendering | `rdt_fill_path`, `rdt_stroke_path` in render_svg_inline.cpp |
| Images via ThorVG | `rdt_draw_image` in render.cpp |
| SVG pictures | `rdt_picture_draw` |
| Scrollbar handles | `rdt_fill_rounded_rect` in scroller.cpp |
| Form controls | `rdt_fill_path`, `rdt_stroke_path` in render_form.cpp |
| Caret, selection overlays | `rdt_fill_rect` in render.cpp |
| Column rules | `rdt_stroke_path`, `rdt_fill_rect` in render.cpp |

**Clipping strategy**: Use `rdt_push_clip(vec, clip_path)` / `rdt_pop_clip(vec)` — already implemented and working.

### Category 2: Direct Pixel Writes (~15 distinct operations)

These write directly to `surface->pixels`, bypassing ThorVG entirely.

| Operation | Location | Clipping Strategy |
|---|---|---|
| **draw_glyph** (grayscale/mono text) | render.cpp L629 | See §Text Glyph Clipping |
| **draw_color_glyph** (BGRA emoji) | render.cpp L555 | See §Text Glyph Clipping |
| **fill_surface_rect** (axis-aligned fills) | surface.cpp L336 | See §Rect Fill Clipping |
| **blit_surface_scaled** (raster bg images) | surface.cpp L442 | See §Image Clipping |
| **render_conic_gradient** | render_background.cpp | See §Gradient Clipping |
| **box_blur_region** (shadow blur) | render_background.cpp | Operates on already-clipped content |
| **CSS opacity blending** | render.cpp L2455 | Post-render composite, no clip needed |
| **mix-blend-mode compositing** | render.cpp L2488 | Post-render composite, no clip needed |
| **CSS filter pipeline** | render_filter.cpp | Post-render effect, no clip needed |

## Redesign: Per-Operation Vector Clipping

### Clip Context

Replace the current `ClipMask`/`ClipShape` pixel save/restore with a **clip shape stack** on `RenderContext`:

```
RenderContext.block.clip       — existing axis-aligned Bound (for rect clipping)
RenderContext.block.clip_shape — NEW: active non-rect clip shape (rounded-rect, circle, etc.)
```

The `clip_shape` is `nullptr` when only rect clipping is active (the common fast path). When border-radius overflow or CSS clip-path sets a non-rect shape, `clip_shape` is set before rendering children and cleared after.

### 1. ThorVG Vector Operations — `rdt_push_clip` / `rdt_pop_clip`

**Already supported.** When `clip_shape` is active, push it as a ThorVG clip path before rendering the block's children, pop after. All `rdt_*` calls within that subtree are automatically clipped by ThorVG's alpha mask.

At the overflow clip site (currently ~line 2344):
```
Before: save_clip_region → render_children → apply_clip_mask
After:  rdt_push_clip(vec, rounded_rect_path) → render_children → rdt_pop_clip(vec)
```

### 2. Rect Fills — `fill_surface_rect`

`fill_surface_rect` already clips to `rdcon->block.clip` (axis-aligned rect). When `clip_shape` is active:

**Fast path (common)**: If the fill rect is entirely inside the clip shape → proceed with existing rect clip. Check: all 4 corners of the fill rect pass `point_in_shape()`.

**Trivial reject**: If the fill rect is entirely outside the clip shape → skip. Check: bounding rect of shape doesn't intersect fill rect.

**Partial overlap (rare, only at rounded corners)**: For `CLIP_SHAPE_ROUNDED_RECT`, only the 4 corner regions can partially clip. For rows that intersect a corner arc, compute the scanline intersection (x-entry/x-exit from the arc equation) and fill only the inside portion. This is O(rows_in_corner × 1) — no per-pixel point-in-shape test.

### 3. Text Glyph Clipping — `draw_glyph` / `draw_color_glyph`

Text is the dominant rendering operation by pixel volume. Strategy:

**Fast path — glyph box entirely inside clip shape**: No additional clipping needed beyond the existing rect clip. This covers >99% of glyphs. Check: all 4 corners of the glyph bounding box pass `point_in_shape()`.

**Trivial reject — glyph box entirely outside clip shape**: Skip drawing entirely. Check: glyph bbox doesn't intersect clip shape bbox, or all 4 corners fail the shape test and the glyph is in a corner region.

**Partial intersection — glyph box straddles clip shape edge**: This only happens for glyphs near rounded corners. Two options:

- **Option A (simple)**: For each scanline (row) of the glyph bitmap, compute the clip shape's left/right boundary at that y-coordinate. Clamp the glyph's column range to `[clip_left, clip_right]` for that row. For rounded-rect, this is a simple circle-intersection formula per corner:
  ```
  For row y in corner(cx, cy, r):
    dx = sqrt(r² - (y - cy)²)
    clip_x = cx ± dx
  ```
  Cost: one sqrt per scanline row that intersects a corner arc (~8-16 rows for typical radii).

- **Option B (pixel mask)**: Pre-rasterize the clip shape into a 1-bit scanline table once per clip region (at push time). Each row stores `[x_start, x_end]`. Glyph rendering looks up the row to get clip bounds. Cost: O(height) setup, O(1) per row lookup during rendering.

**Recommendation**: Option A. The sqrt cost is negligible (8-16 calls per partially-clipped glyph), and partially-clipped glyphs are extremely rare — only glyphs whose bounding box straddles a corner arc.

### 4. Background Image Clipping — `blit_surface_scaled`

Similar to rect fills:

**Fast path**: Entire image rect inside clip shape → use existing rect clip.

**Partial overlap**: For rows intersecting the clip shape boundary, compute scanline x-bounds from the shape equation and clamp the blit range per row.

For `rdt_draw_image` (ThorVG image blits), clipping is automatic via `rdt_push_clip`.

### 5. Conic Gradient Clipping — `render_conic_gradient`

Per-pixel operation that already iterates over the fill region. Add clip shape test:

**Fast path**: If gradient rect is entirely inside clip shape → no change.

**Partial overlap**: For each pixel, check against scanline bounds (same as §3 Option A). Since conic gradient is already per-pixel, the marginal cost is minimal.

### 6. CSS clip-path Property

Same approach: instead of pixel save/restore, set `clip_shape` on the render context and let each rendering operation clip itself. Push ThorVG clip for vector ops. The only difference from overflow-clip is the shape can be polygon, circle, or ellipse (not just rounded-rect).

For polygon shapes, the scanline intersection uses the existing ray-casting approach but computed once per row (x-intersections sorted) rather than per pixel.

## Implementation Plan

### Phase 1: Overflow Rounded-Rect Clip (the 76% bottleneck) — ✅ DONE

1. ✅ Created `clip_shape.h` with `ClipShape` struct, `ClipShapeType` enum, inline helpers (`clip_point_in_rounded_rect`, `clip_scanline_rounded_rect`, `clip_shape_rect_inside`, `clip_shapes_rect_inside`, `clip_shapes_scanline_bounds`)
2. ✅ Added `ClipShape* clip_shapes[8]` + `int clip_shape_depth` stack on `RenderContext` (render.hpp)
3. ✅ Overflow clip site rewritten: `save_clip_region` → `rdt_push_clip` + clip shape stack push; `apply_clip_mask` → `rdt_pop_clip` + stack pop
4. ✅ `draw_glyph()` and `draw_color_glyph()`: added `need_shape_clip` fast path (bbox test), per-row scanline bounds via `clip_shapes_scanline_bounds`
5. ✅ `fill_surface_rect()`: 3-tier (no shapes → all inside → per-row scanline clipping). Updated 21 callers across render.cpp, render_border.cpp, render_background.cpp, render_form.cpp
6. ✅ `blit_surface_scaled()`: added clip shape params, per-row scanline clipping. Updated callers in render.cpp and render_background.cpp (via `blit_bg_tile`)
7. ✅ `view.hpp` declarations updated with optional `ClipShape**` + `int` default params

### Phase 2: CSS clip-path — ✅ DONE

1. ✅ Extended `clip_shape.h` with point-in-shape tests for all 5 types: `clip_point_in_circle`, `clip_point_in_ellipse`, `clip_point_in_inset`, `clip_point_in_polygon`, unified `clip_point_in_shape` dispatcher
2. ✅ Added scanline bounds for circle (`clip_scanline_circle`), ellipse (`clip_scanline_ellipse`), polygon (`clip_scanline_polygon`) — all analytical (sqrt for circle/ellipse, edge intersection for polygon)
3. ✅ Updated `clip_shapes_scanline_bounds` to dispatch across all shape types
4. ✅ Added `create_clip_shape_path()` — converts any `ClipShape` to a ThorVG `RdtPath*` for vector clip
5. ✅ CSS clip-path setup rewritten: `save_clip_region` → `rdt_push_clip` + clip shape stack push
6. ✅ CSS clip-path cleanup rewritten: `apply_clip_mask` → `rdt_pop_clip` + stack pop

### Phase 3: Clean Up — ✅ DONE

1. ✅ Removed `ClipMask` struct, `save_clip_region`, `apply_clip_mask`, `free_clip_mask`
2. ✅ Removed local `point_in_rounded_rect`, `point_in_polygon` (replaced by `clip_shape.h` equivalents)
3. ✅ Retained `free_clip_shape` (used for scratch-allocated CSS clip-path shapes)
4. ✅ Retained `parse_css_clip_shape` (parses CSS clip-path value string into ClipShape)

## Profiling Results (After Implementation)

Release build, macOS Apple Silicon, 1200×800 headless surface:

### Top 3 Benchmark Pages: Before vs After

| Page | Old Render | Old overflow_clip | Old clip% | New Render | New overflow_clip | Speedup |
|------|-----------|------------------|-----------|-----------|------------------|---------|
| md_axios-changelog | 2532ms | 2331ms | 92% | **160ms** | 0.1ms | **16×** |
| md_zod-readme | 2186ms | 1860ms | 85% | **290ms** | 0.0ms | **7.5×** |
| report | 803ms | 583ms | 73% | **166ms** | 0.0ms | **4.8×** |

### Detailed New Profile (md_axios-changelog)

```
overflow_clip=1089(0.1ms)    ← was 2331ms, now 0.1ms for same 1089 clip ops
font_metrics=7036(65.0ms)
text=669(79.8ms)
bound=1169(15.0ms)
blocks=1172(self=28.4ms)
Total render: 160.3ms
```

### Detailed New Profile (md_zod-readme)

```
overflow_clip=870(0.0ms)     ← was 1860ms, now 0.0ms for 870 clip ops
font_metrics=20047(180.5ms)  ← now the dominant cost (font cache lookup)
text=695(240.0ms)
bound=874(16.0ms)
blocks=885(self=24.6ms)
Total render: 290.2ms
```

### Tests

All 4876 radiant baseline tests pass:
- Layout Baseline: 4080 passed
- WPT CSS Text: 518 passed
- Layout Page Suite: 39 passed
- UI Automation: 47 passed
- View Page & Markdown: 97 passed
- Fuzzy Crash: 17 passed
- Pretext Corpus: 78 passed

## Performance Analysis

| Metric | Old | New | Improvement |
|---|---|---|---|
| Save cost | O(w×h) memcpy | 0 (no save) | **eliminated** |
| ThorVG ops clip cost | O(w×h) restore | O(1) per shape (ThorVG alpha mask) | **eliminated** |
| Glyph clip cost | O(w×h) pixel test | O(1) per glyph (bbox test, >99% fast path) | **~1000×** |
| Rect fill clip cost | O(w×h) pixel test | O(1) per fill (bbox test) | **~1000×** |
| Corner partial glyphs | O(w×h) pixel test | O(r) per glyph (scanline bounds) | **~100×** |
| Memory | w×h×4 bytes saved buffer | 0 | **eliminated** |
| Actual md_axios-changelog | 2331ms clip time | 0.1ms clip time | **23,310×** |

The overflow clip bottleneck (76% of render time) has been fully eliminated. The new dominant cost is `font_metrics` (font cache lookup per glyph) — a separate optimization target.

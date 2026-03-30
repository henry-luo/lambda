# Radiant Rendering Enhancement Proposal

## Overview

This proposal outlines structural enhancements to Radiant's HTML/CSS/SVG rendering pipeline.
The analysis is based on the current codebase (`radiant/render*.cpp`, `resolve_css_style.cpp`, `view.hpp`).

Radiant currently supports solid backgrounds, basic borders, gradients (linear/radial/conic), CSS transforms (2D), color-manipulation filters, and text rendering. However, many CSS visual properties are either absent, partially implemented, or only work in the rasterizer but not in SVG/PDF output.

This document organizes the gaps by priority tier and proposes implementation strategies for each.

---

## Implementation Progress

| Phase | Features | Status |
|-------|----------|--------|
| **Phase 1** | HSL colors, `visibility:hidden`, `outline` | ✅ Complete |
| **Phase 2** | Box-shadow blur (software kernel), text-shadow, `filter:blur()` | ✅ Complete |
| **Phase 3** | Border style variants (double/groove/ridge/inset/outset) | ✅ Complete |
| **Phase 4** | Background-image + position/size/repeat, `background-origin`, `background-clip`, `background-attachment` | ✅ Complete |
| **Phase 5** | `filter:drop-shadow()` | ✅ Complete |
| **Phase 6** | SVG inline: defs, gradients, `<use>`, `<clipPath>` | ✅ Complete |
| **Phase 7** | SVG output parity (gradients, shadows, transforms) | ✅ Complete |
| **Phase 8** | Render backend abstraction | ✅ Complete |
| **Phase 9** | PDF enhancements (fonts, gradients, images) | 🔲 Not started |

---

## Current Architecture Summary

**Rendering pipeline:**
```
HTML/CSS → DOM Parse → Style Resolution → Layout → View Tree → Rendering
```

**Output backends:**

| Backend | File | Quality |
|---------|------|---------|
| Raster (ThorVG canvas) | `render.cpp` | Primary, most complete |
| SVG export | `render_svg.cpp` | Partial — backgrounds/borders only, no shadows/filters/transforms |
| PDF export | `render_pdf.cpp` | Basic — text + rects, no gradients/shadows/borders |
| PNG/JPEG | `render_img.cpp` | Delegates to raster pipeline |

**Key data structures:**
- `BoundaryProp` — margin, padding, border, background, box-shadow
- `TransformProp` — CSS transform functions + origin
- `FilterProp` — filter function chain
- `InlineProp` — cursor, opacity, color, text-decoration
- `BlockProp` — text-align, white-space, sizing, list-style, etc.

---

## Tier 1 — High Priority (Core Visual Gaps)

### 1.1 `outline` Property ✅

**Status:** **Complete (Phase 1).** `OutlineProp` added to `view.hpp`, parsed in `resolve_css_style.cpp`, `render_outline()` implemented in `render_border.cpp` (ThorVG stroke, outside border-box, respects `outline-offset`), SVG `<rect stroke>` with `stroke-dasharray` emitted in `render_bound_svg()`.

**Spec:** CSS UI Level 3 — `outline-style`, `outline-width`, `outline-color`, `outline-offset`.

**Impact:** Outlines are used extensively for focus indicators and decorative borders that don't affect layout.

**Files modified:** `view.hpp`, `resolve_css_style.cpp`, `render_border.cpp`, `render_svg.cpp`

---

### 1.2 `box-shadow` Blur (Proper Gaussian Blur) ✅

**Status:** **Complete (Phase 2).** Real blur implemented via a 3-pass box-blur approximation of Gaussian. `box_blur_region()` exported from `render_background.hpp` and shared with `filter:blur()`. Inset shadows fully implemented via `render_box_shadow_inset()` (ThorVG path + clip + blur). SVG output uses `<filter>` with `feGaussianBlur`/`feOffset`/`feColorMatrix`/`feMerge`.

**Files modified:** `render_background.cpp`, `render_background.hpp`, `render.cpp`, `render_svg.cpp`
**Test:** `test/layout/data/box/box_shadow_01.html`, `box_shadow_02_text_filter.html`

---

### 1.3 `text-shadow` ✅

**Status:** **Complete (Phase 2).** `TextShadow` struct added to `view.hpp`, linked list stored in `FontProp.text_shadow`. Parsed in `resolve_css_style.cpp`. Raster rendering uses a blur pre-pass in `render.cpp`: each shadow is rendered to a temporary surface, `box_blur_region()` applied when `blur_radius > 0`, then composited under the glyph. SVG output references per-element `<feGaussianBlur>` filter defs.

**Files modified:** `view.hpp`, `resolve_css_style.cpp`, `render.cpp`, `render_svg.cpp`

---

### 1.4 HSL/HSLA Color Functions ✅

**Status:** **Complete (Phase 1).** `hsl_to_rgb()` conversion function implemented in `resolve_css_style.cpp` per CSS Color Level 4 §4.2.4. Wired into `resolve_color_function()` and `resolve_color_value()`; `hsl()` and `hsla()` now produce correct RGB output.

**Implementation plan:**
**Files modified:** `resolve_css_style.cpp`

---

### 1.5 Border Style Variants ✅

**Status:** **Complete (Phase 3).** All six decorative border styles now render correctly:
- **`double`** — two ThorVG trapezoid fills with a `width/3` gap, both raster and SVG
- **`groove`/`ridge`** — two half-width trapezoid fills, outer darker / inner lighter (groove) or reversed (ridge)
- **`inset`/`outset`** — two-polygon trapezoid approach (top+left vs bottom+right) so each side gets the correct 3-D shade; straight borders use filled polygons, rounded borders fall back to a single tinted stroke
- **Per-side non-uniform** — `render_per_side_borders()` handles each side independently with full style dispatch
- **SVG** — `svg_emit_border_side()` emits `<polygon>` with miter-cut corners for all styles

**Files modified:** `render_border.cpp`, `render_svg.cpp`
**Test:** `test/layout/data/box/box_border_styles_01.html`

---

## Tier 2 — Medium Priority (Background & Image Features)

### 2.1 `background-image: url()`

**Status:** URL parsed in `resolve_css_style.cpp` but image is never loaded or rendered as a background. Only the `<img>` tag path loads images currently.

**Implementation plan:**
1. Store the image URL in `BackgroundProp`.
2. During rendering, resolve the URL, load the image via the existing image loading path (`render_img.cpp`), and draw it to the background area.
3. Respect `background-size`, `background-position`, `background-repeat` (see below).

### 2.2 `background-size`, `background-position`, `background-repeat`

**Status:** Not stored in `BackgroundProp`. Multiple TODOs in `resolve_css_style.cpp` (lines 4271-4355).

**Implementation plan:**
1. **Data model** — Extend `BackgroundProp`:
   ```c
   typedef struct BackgroundProp {
       // ... existing fields ...
       // New fields:
       CssEnum bg_size_type;        // cover, contain, auto, or explicit
       float bg_size_width, bg_size_height;
       bool bg_size_width_percent, bg_size_height_percent;
       float bg_position_x, bg_position_y;
       bool bg_position_x_percent, bg_position_y_percent;
       CssEnum bg_repeat_x, bg_repeat_y;  // repeat, no-repeat, space, round
       CssEnum bg_attachment;       // scroll, fixed, local
       CssEnum bg_origin;           // padding-box, border-box, content-box
       CssEnum bg_clip;             // same values as origin
   } BackgroundProp;
   ```
2. **Style resolution** — Parse these properties (many are already partially parsed but discarded).
3. **Raster rendering** — In `render_background()`, after color/gradient, tile/position the background image according to the computed values.
4. **SVG output** — Emit `<pattern>` with `<image>` for repeated backgrounds, or direct `<image>` for non-repeating.

**Files to modify:** `view.hpp`, `resolve_css_style.cpp`, `render_background.cpp`, `render_svg.cpp`

---

### 2.3 `opacity`

**Status:** Stored in `InlineProp` and partially applied. Not consistently propagated to children or used in SVG output.

**Implementation plan:**
1. **Raster** — When `opacity < 1.0`, render the element and all children to an offscreen buffer, then composite with the specified alpha onto the main surface. This is the only correct approach per CSS spec (opacity creates a stacking context and applies to the entire subtree).
2. **SVG output** — Add `opacity="..."` attribute to the element's `<g>` wrapper.

**Files to modify:** `render.cpp`, `render_svg.cpp`

---

### 2.4 `visibility: hidden` ✅

**Status:** **Complete (Phase 1).** Visibility check added at the top of `render_block_view()` and `render_inline_view()`. Layout space is still reserved; children with `visibility:visible` are still rendered. SVG output also skips hidden elements.

**Files modified:** `render.cpp`, `render_svg.cpp`

---

## Tier 3 — Advanced Features

### 3.1 `filter: blur()` ✅

**Status:** **Complete (Phase 2).** Implemented as a post-processing pass in `render_filter.cpp` using the shared `box_blur_region()` from `render_background.hpp`. Operates directly on the raster surface — no ThorVG C++ API required.

**Files modified:** `render_filter.cpp`, `render_background.hpp`
**Test:** `test/layout/data/box/box_shadow_02_text_filter.html`

---

### 3.2 `filter: drop-shadow()`

**Status:** Logged as "not supported yet" (`render_filter.cpp:282`).

**Implementation plan:**
1. Extract the alpha channel of the rendered element region.
2. Apply Gaussian blur to the alpha channel.
3. Colorize with the shadow color.
4. Offset by (dx, dy) and composite under the original.

**Files to modify:** `render_filter.cpp`

---

### 3.3 SVG Inline Rendering Gaps ✅

**Status:** **Complete (Phase 6).** All major inline SVG gaps resolved:
- **`<defs>` processing** — `process_svg_defs()` walks `<defs>` children, builds `SvgDefTable` lookup (id → `SvgElemDef`/`SvgGradDef`).
- **Gradient resolution** — `apply_svg_gradient_fill()` creates ThorVG linear/radial gradients from `SvgGradDef`. `apply_svg_fill_stroke()` resolves `url(#id)` references with element bounding-box parameters.
- **`<use>` element** — Resolves `href`/`xlink:href`, renders referenced element with x/y offset translation.
- **`<clipPath>`** — Build ThorVG clip shape from path data, applied via `tvg_paint_set_mask_method()`.
- **`preserveAspectRatio`** — Full parsing of align (xMin/xMid/xMax × YMin/YMid/YMax) + meetOrSlice, computes correct viewBox→viewport transform matrix. Supports `none`, `meet`, and `slice`.

**Files modified:** `render_svg_inline.cpp`

---

### 3.4 `clip-path` and `mask`

**Status:** Not implemented for HTML elements.

**Implementation plan (future):**
1. `clip-path: inset()` / `circle()` / `ellipse()` / `polygon()` — Build ThorVG shape from the basic shape function, apply as mask.
2. `clip-path: url(#...)` — Reference SVG `<clipPath>` element (requires SVG defs support first).
3. `mask` — Advanced compositing, requires offscreen rendering.

---

### 3.5 `overflow: hidden` Clipping (Improvements)

**Status:** Basic rectangular clipping works via `rdcon->block.clip`. Rounded clipping exists but uses uniform radius as fallback.

**Implementation plan:**
1. Support per-corner radii in clip shapes (currently uses max radius for all corners).
2. Ensure `overflow: scroll` / `auto` renders scrollbar indicators in SVG output.

**Files to modify:** `render_background.cpp`, `render_border.cpp`, `render.cpp`

---

## Tier 4 — SVG/PDF Output Parity

The SVG and PDF output backends are significantly behind the raster backend. This section proposes a structural refactoring to close the gap.

### 4.1 SVG Output: Missing Visual Properties

Current `render_bound_svg()` only emits:
- Background color (solid rect)
- Border widths (as filled rects, not strokes)

**Current SVG output status (updated):**

| Feature | Raster | SVG |
|---------|--------|-----|
| Gradients (linear/radial) | ✅ | ✅ (`<linearGradient>`, `<radialGradient>`) |
| Box-shadow (blur + inset) | ✅ | ✅ (`<filter>` + `feGaussianBlur`) |
| Border-radius (correct per-corner) | ✅ | ⚠️ (uniform only) |
| Border styles (all variants) | ✅ | ✅ (`<polygon>` trapezoids) |
| Outline | ✅ | ✅ (`<rect stroke>`) |
| CSS transforms | ✅ | ✅ (`<g transform="matrix(...)">`) |
| CSS filters (`blur()`) | ✅ | ❌ |
| Opacity | ✅ | ✅ (`<g opacity>`) |
| Text decorations | ✅ | ❌ |
| Text-shadow | ✅ | ✅ (`<filter>` + `feGaussianBlur`) |
| Images | ✅ | ✅ (`<image>` href) |
| Inline SVG | ✅ | ✅ (passthrough) |

**Render Backend Abstraction (Phase 8) — Complete:**

A shared `RenderBackend` vtable and tree walker eliminates duplicated traversal logic across SVG and PDF backends. Implemented in `render_backend.h` and `render_walk.cpp`:

```c
struct RenderBackend {
    void* ctx;
    void (*render_bound)(void* ctx, ViewBlock* view, float abs_x, float abs_y);
    void (*render_text)(void* ctx, ViewText* text, float abs_x, float abs_y,
                        FontBox* font, Color color);
    void (*render_image)(void* ctx, ViewBlock* block, float abs_x, float abs_y);
    void (*render_inline_svg)(void* ctx, ViewBlock* block, float abs_x, float abs_y);
    void (*begin_block_children)(void* ctx, ViewBlock* block);
    void (*end_block_children)(void* ctx, ViewBlock* block);
    void (*begin_inline_children)(void* ctx, ViewSpan* span);
    void (*end_inline_children)(void* ctx, ViewSpan* span);
    void (*begin_opacity)(void* ctx, float opacity);
    void (*end_opacity)(void* ctx);
    void (*begin_transform)(void* ctx, ViewBlock* block, float abs_x, float abs_y);
    void (*end_transform)(void* ctx);
    void (*render_column_rules)(void* ctx, ViewBlock* block, float abs_x, float abs_y);
    void (*on_font_change)(void* ctx, FontProp* font_prop);
};
```

The shared walker (`render_walk_block`, `render_walk_inline`, `render_walk_children`) handles tree traversal, context save/restore, font/color propagation, opacity/transform wrapping, and view-type dispatch. Each backend provides only the drawing callbacks. SVG uses 13 callbacks; PDF uses 3 (bound, text, font-change). The raster backend (`render.cpp`) is not wired through this interface due to HiDPI scaling, pixel-level filters/opacity, scrollbars, and form controls.

New rendering features added to the walker are automatically available in both SVG and PDF output.

**Files added:** `render_backend.h`, `render_walk.cpp`
**Files modified:** `render_svg.cpp`, `render_pdf.cpp`

### 4.2 PDF Output Enhancements

Current PDF output (`render_pdf.cpp`) uses libharu with only:
- Basic text rendering (Helvetica/Times/Courier font mapping)
- Solid color rectangles
- No gradients, no shadows, no rounded borders, no images

**Incremental plan:**
1. **Font embedding** — Use libharu's TTF embedding for correct font rendering.
2. **Rounded rects** — Use PDF cubic Bezier path operators (c, l, m).
3. **Gradients** — Use PDF shading patterns (Type 2 axial, Type 3 radial).
4. **Images** — Embed PNG/JPEG via libharu's image functions.
5. **Box-shadow** — Approximate with semi-transparent filled shapes (PDF doesn't have native blur).

---

## Tier 5 — Minor Enhancements

### 5.1 `text-decoration-style` and `text-decoration-color`

**Status:** Basic underline/overline/line-through work. Style (solid/dashed/dotted/wavy/double) and color are not supported.

### 5.2 `text-overflow: ellipsis`

**Status:** Logged as "field not yet added to BlockProp" (`resolve_css_style.cpp:7341`).

### 5.3 `cursor` Property

**Status:** Stored in `InlineProp` but no rendering or platform cursor change.

### 5.4 `::first-line` / `::first-letter` Pseudo-elements

**Status:** Not supported in style resolution or rendering.

### 5.5 `min()`, `max()`, `clamp()` CSS Functions

**Status:** Logged as "not yet implemented" in `resolve_css_style.cpp:1448`. `calc()` works for single-level expressions.

---

## Implementation Order

Recommended implementation sequence, ordered by impact and dependency:

| Phase | Features | Status | Complexity |
|-------|----------|--------|------------|
| **Phase 1** | HSL colors, `visibility:hidden`, `outline` | ✅ Complete | Low |
| **Phase 2** | Box-shadow blur, text-shadow, `filter:blur()` | ✅ Complete | Medium |
| **Phase 3** | Border style variants (double/groove/ridge/inset/outset) | ✅ Complete | Medium |
| **Phase 4** | Background-image + position/size/repeat, `background-origin`, `background-clip`, `background-attachment` | ✅ Complete | Medium-High |
| **Phase 5** | `filter:drop-shadow()` | ✅ Complete | Medium |
| **Phase 6** | SVG inline: defs, gradients, `<use>`, `<clipPath>` | ✅ Complete | Medium |
| **Phase 7** | SVG output parity (gradients, transforms in SVG export) | ✅ Complete | High |
| **Phase 8** | Render backend abstraction | ✅ Complete | High (refactor) |
| **Phase 9** | PDF enhancements (fonts, gradients, images) | 🔲 Not started | High |

---

## Gaussian Blur Implementation Note

Both `box-shadow` blur and `filter: blur()` need a Gaussian blur kernel. Implement once and share:

```c
// Separable box blur (3-pass approximation of Gaussian)
// radius = ceil(blur_radius * 0.75) per CSS spec
void box_blur_rgba(uint32_t* pixels, int width, int height,
                   int stride, int radius, int passes);
```

**Algorithm:** For each pass, do a horizontal scan (accumulate sum in a sliding window of `2*radius+1` pixels), then a vertical scan. Three passes produce a result visually indistinguishable from true Gaussian blur.

**Performance:** O(width × height × passes) — independent of radius (sliding window). At 3 passes, a 1000×1000 region takes ~3M pixel operations. Acceptable for static rendering.

---

## Testing Strategy

1. **Visual regression tests** — Add HTML test files to `test/layout/` with the new CSS features. Compare rasterized output against browser reference screenshots.
2. **SVG output tests** — Verify SVG output contains correct elements (filter defs, gradient defs, opacity attributes, etc.) via text matching.
3. **Round-trip tests** — Render HTML → SVG → re-render SVG, compare pixel output.
4. **Edge cases** — Zero-width borders with radius, negative outline-offset, multiple overlapping shadows, nested opacity contexts.

---

## Summary

### Completed

- ✅ **`outline`** — `OutlineProp`, full raster + SVG rendering
- ✅ **`visibility:hidden`** — layout preserved, rendering skipped
- ✅ **HSL/HSLA colors** — `hsl_to_rgb()` per CSS Color Level 4
- ✅ **`box-shadow` blur** — 3-pass box blur, inset shadows, SVG `<filter>` output
- ✅ **`text-shadow`** — data model, blur pre-pass, SVG filter output
- ✅ **`filter:blur()`** — software kernel, shares `box_blur_region()`
- ✅ **Border style variants** — `double`/`groove`/`ridge`/`inset`/`outset`, per-side + SVG polygon output
- ✅ **`background-image`** — URL loading, `background-size`/`position`/`repeat`/`origin`/`clip`, raster + SVG (`<pattern>`/`<image>`)
- ✅ **`filter:drop-shadow()`** — alpha extraction + blur + colorize pipeline
- ✅ **SVG inline** — `<defs>`, gradient `url(#id)` resolution, `<use>`, `<clipPath>`, `preserveAspectRatio`
- ✅ **SVG output parity** — gradients (`<linearGradient>`/`<radialGradient>`), CSS transforms (`matrix()`), opacity (`<g opacity>`)
- ✅ **Render backend abstraction** — `RenderBackend` vtable + shared tree walker (`render_walk.cpp`), SVG (13 callbacks) and PDF (3 callbacks) migrated

### Remaining priorities

1. **PDF enhancements** — font embedding, gradients, rounded rects, images

The backend abstraction is now complete. New CSS features added to the shared walker are automatically available in both SVG and PDF output.

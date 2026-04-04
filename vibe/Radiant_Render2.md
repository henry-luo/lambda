# Radiant Rendering Enhancement Proposal — Phase 2

## Overview

This proposal identifies remaining CSS rendering gaps in Radiant after the Phase 1 enhancements (documented in `vibe/radiant/Radiant_Render.md`) and proposes implementation strategies organized by priority tier.

The analysis is based on the current codebase (`radiant/render*.cpp`, `resolve_css_style.cpp`, `view.hpp`) and covers gaps in CSS style resolution, raster rendering, and SVG output.

---

## Current State Summary

### Completed (Phase 1)

| Feature | Backend |
|---------|---------|
| HSL colors, `visibility:hidden`, `outline` | Raster + SVG |
| Box-shadow blur + inset, text-shadow, `filter:blur()` | Raster + SVG |
| Border style variants (double/groove/ridge/inset/outset) | Raster + SVG |
| Background-image + position/size/repeat/origin/clip | Raster + SVG |
| `filter:drop-shadow()` | Raster |
| SVG inline: defs, gradients, `<use>`, `<clipPath>` | Raster |
| SVG output parity (gradients, transforms, opacity) | SVG |
| Render backend abstraction (`RenderBackend` vtable) | SVG |

### Known Working Filters (Raster)

`blur()`, `brightness()`, `contrast()`, `grayscale()`, `hue-rotate()`, `invert()`, `opacity()`, `saturate()`, `sepia()`, `drop-shadow()` — all working in raster. None emit SVG `<filter>` equivalents yet.

---

## Tier 1 — High Priority (Core Visual Gaps)

### 1.1 Border-Radius: SVG Per-Corner + Elliptical Radii

**Status:** Raster backend (ThorVG) fully supports per-corner radii via `render_rounded_border()` with Bézier arcs. **SVG output is broken** — only `top_left` radius is used for both `rx` and `ry`, ignoring `top_right`, `bottom_right`, `bottom_left` (`render_svg.cpp:524-525`):

```c
float rx = view->bound->border->radius.top_left;
float ry = view->bound->border->radius.top_left;  // BUG: should differ per corner
```

**Elliptical radii** (e.g., `border-radius: 10px / 20px`) are not supported anywhere — neither parsed nor stored. CSS Backgrounds Level 3 §5.3 allows `border-radius: <h-radius> / <v-radius>` syntax.

**Implementation:**
1. **SVG per-corner fix** — Replace `<rect rx="..." ry="...">` (which only supports uniform radius) with an SVG `<path>` using arc commands (`A`) or cubic Bézier curves for each corner. This is the same approach the raster backend uses.
2. **Elliptical radii (CSS parse)** — Parse the `/` syntax in `border-radius` shorthand to store separate horizontal and vertical radii. Extend `Corner` in `view.hpp`:
   ```c
   typedef struct Corner {
       float top_left, top_right, bottom_right, bottom_left;           // horizontal radii
       float top_left_v, top_right_v, bottom_right_v, bottom_left_v;   // vertical radii (0 = same as horizontal)
   } Corner;
   ```
3. **Raster rendering** — Update `render_rounded_border()` to use per-corner vertical radii in Bézier control points when `*_v != 0`.

**Files to modify:** `view.hpp`, `resolve_css_style.cpp`, `render_svg.cpp`, `render_border.cpp`, `render_background.cpp`
**Complexity:** Medium

---

### 1.2 List Marker: `list-style-image` Rendering

**Status:** `list-style-image` URL is parsed (`resolve_css_style.cpp:4446`) and stored in `BlockProp.list_style_image`, but **never rendered**. `render_marker_view()` (`render.cpp:908-1057`) only handles keyword bullet types (disc, circle, square) and numbered markers — no code path loads or draws an image URL.

**Supported marker types** (currently rendering):
- Shapes: `disc` (filled circle), `circle` (stroked circle), `square` (filled square)
- Text: `decimal`, `decimal-leading-zero`, `lower-roman`, `upper-roman`, `lower-alpha`, `upper-alpha`, `lower-latin`, `upper-latin`, `lower-greek`, `armenian`, `georgian`
- Custom string markers via `::marker { content: "..." }`

**Missing marker types:**
- `none` (suppress marker)
- Image markers from `list-style-image: url(...)`
- `disclosure-open` / `disclosure-closed` (triangle markers for `<details>`)

**Implementation:**
1. In `render_marker_view()`, before the `switch(marker_type)`:
   - Check `BlockProp.list_style_image`. If set and non-null, load the image via the existing image loading path.
   - Scale to fit marker box (width × height) while preserving aspect ratio.
   - Fall back to the keyword bullet type if image load fails.
2. Add `disclosure-open`/`disclosure-closed` as triangle ThorVG shapes.

**Files to modify:** `render.cpp`, `view.hpp` (add image fields to `MarkerProp`)
**Complexity:** Medium

---

### 1.3 List Marker: `list-style-position` Rendering Quality

**Status:** `list-style-position` is parsed and stored as `MarkerProp.is_outside`. Layout creates outside markers with negative left offset. However:
- **Outside markers** don't clip correctly when the list item has `overflow:hidden`.
- **Inside markers** should participate in inline flow (rendered inline with text) but are currently treated as positioned blocks.

**Implementation:**
1. For `inside` markers, insert the marker as an inline child before the first text node so it flows with the text and wraps correctly.
2. For `outside` markers, ensure the marker is excluded from `overflow:hidden` clipping of its parent.

**Files to modify:** `layout_list.cpp`, `render.cpp`
**Complexity:** Low-Medium

---

### 1.4 `text-decoration-style`, `text-decoration-color`, `text-decoration-thickness`

**Status:** Only `text-decoration-line` is stored in `FontProp.text_deco` (underline/overline/line-through). No support for style (solid/dashed/dotted/wavy/double), color, or thickness.

**Current bug:** Text underline draws at incorrect position and doesn't skip descenders (`render.cpp:821`).

**Data model changes** — Extend `FontProp` in `view.hpp`:
```c
struct FontProp {
    // ... existing fields ...
    CssEnum text_deco;                // existing: underline, overline, line-through
    CssEnum text_deco_style;          // NEW: solid, dashed, dotted, wavy, double
    Color text_deco_color;            // NEW: decoration color (default: currentColor)
    float text_deco_thickness;        // NEW: line thickness in px (default: from font metrics)
    float text_underline_offset;      // NEW: underline-offset in px (CSS Text Decoration 3)
};
```

**Rendering changes** (`render.cpp:810-850`):
1. Use `text_deco_color` instead of inheriting text color.
2. Use `text_deco_thickness` for line height (fallback: font `underline_thickness` metric).
3. Position underline using font `underline_position` metric instead of `baseline + thickness`.
4. For `wavy` style: render a sine wave path via ThorVG shape.
5. For `dashed`/`dotted`: use ThorVG stroke dash pattern.
6. For `double`: render two parallel lines at 1/3 thickness.

**Files to modify:** `view.hpp`, `resolve_css_style.cpp`, `render.cpp`, `render_svg.cpp`
**Complexity:** Medium

---

### 1.5 `text-overflow: ellipsis`

**Status:** Parsed in `resolve_css_style.cpp:7753` with log_debug — "field not yet added to BlockProp".

**Data model changes** — Add to `BlockProp`:
```c
CssEnum text_overflow;  // CSS_VALUE_CLIP (default) | CSS_VALUE_ELLIPSIS
```

**Implementation:**
1. **Style resolution** — Store `text_overflow` in `BlockProp`.
2. **Inline layout** (`layout_inline.cpp`) — When line exceeds container width and `overflow:hidden` + `text-overflow:ellipsis`:
   - Measure the width of "…" (U+2026) in the current font.
   - Truncate the last text run to fit `container_width - ellipsis_width`.
   - Append an ellipsis `TextRect` at the end.
3. **Rendering** — No special code needed; the ellipsis renders as a normal glyph.

**Files to modify:** `view.hpp`, `resolve_css_style.cpp`, `layout_inline.cpp`
**Complexity:** Medium

---

### 1.6 `min()`, `max()`, `clamp()` CSS Functions

**Status:** Logged as "not yet implemented, treating as unset" at `resolve_css_style.cpp:1587`.

**Implementation:** In `resolve_css_value_length()`:
- `min(a, b, ...)` → evaluate all arguments, return the smallest.
- `max(a, b, ...)` → evaluate all arguments, return the largest.
- `clamp(min, val, max)` → `max(min, min(val, max))`.
- Each argument resolved recursively (handles nested `calc()`, lengths, percentages).

**Files to modify:** `resolve_css_style.cpp`
**Complexity:** Low-Medium

---

### 1.7 `mix-blend-mode`

**Status:** Not parsed or stored in Radiant. CSS property ID exists (`CSS_PROPERTY_MIX_BLEND_MODE`) but no handler in `resolve_css_style.cpp`.

**Implementation:**
1. **Style resolution** — Parse and store in `InlineProp` (new field `blend_mode`).
2. **Raster rendering** — Render the element subtree to an offscreen buffer, then composite onto the parent using the specified blend mode (multiply, screen, overlay, darken, lighten, etc.). Pixel-level blending math per compositing spec.
3. **SVG output** — Emit `style="mix-blend-mode: multiply"` on the element's `<g>` wrapper.

**Files to modify:** `view.hpp`, `resolve_css_style.cpp`, `render.cpp`, `render_svg.cpp`
**Complexity:** Medium-High

---

### 1.8 `background-blend-mode`

**Status:** Parsed in `resolve_css_style.cpp:4587` — TODO comment, value logged but never stored in `BackgroundProp`.

**Data model changes** — Add `CssEnum blend_mode;` to `BackgroundProp`.

**Rendering:** In `render_background()`, when drawing background-image over background-color with a blend mode, blend pixel-by-pixel using the specified mode before compositing onto the main surface.

**Files to modify:** `view.hpp`, `resolve_css_style.cpp`, `render_background.cpp`
**Complexity:** Medium

---

## Tier 2 — Medium Priority (Rendering Quality)

### 2.1 Opacity via Offscreen Compositing

**Status:** Opacity applied by modifying pixel alpha in-place after rendering (`render.cpp`). This doesn't correctly handle overlapping children — each child's alpha is modified independently.

**Correct behavior (CSS spec):** `opacity` creates a stacking context. The element and all descendants render to an offscreen buffer at full opacity, then the buffer composites onto the parent at the specified opacity.

**Implementation:**
1. Allocate a temp `ImageSurface` of the element's bounding box.
2. Render element + children into the temp surface.
3. Composite onto the main surface with the specified alpha (Porter-Duff source-over).

**Performance note:** Only needed when `opacity < 1.0` AND the element has children.

**Files to modify:** `render.cpp`
**Complexity:** Medium

---

### 2.2 Z-Index Stacking Context (Correct Ordering)

**Status:** `layout_positioned.cpp:202` — TODO: "add to chain of positioned elements for z-index stacking." Elements render in document order; z-index is tracked but not used for paint ordering.

**Implementation:**
1. During layout, collect positioned elements with z-index into a sorted list per stacking context.
2. A stacking context is created by: `position` + `z-index`, `opacity < 1`, `transform`, `filter`.
3. During rendering, traverse per CSS 2.1 Appendix E painting order:
   - Backgrounds/borders of stacking context root.
   - Children with negative z-index (sorted).
   - In-flow children (block, float, inline).
   - Children with positive z-index (sorted).

**Files to modify:** `layout_positioned.cpp`, `render.cpp`, `view.hpp`
**Complexity:** High

---

### 2.3 `clip-path` (Basic Shapes)

**Status:** `clip: rect()` parsing stub at `resolve_css_style.cpp:7608-7610`. `clip-path` not stored anywhere.

**Data model changes** — New struct in `view.hpp`:
```c
typedef enum ClipPathType {
    CLIP_PATH_NONE = 0,
    CLIP_PATH_INSET,      // inset(top right bottom left round radii)
    CLIP_PATH_CIRCLE,     // circle(radius at cx cy)
    CLIP_PATH_ELLIPSE,    // ellipse(rx ry at cx cy)
    CLIP_PATH_POLYGON,    // polygon(x1 y1, x2 y2, ...)
} ClipPathType;

typedef struct ClipPathProp {
    ClipPathType type;
    union {
        struct { float top, right, bottom, left; Corner radius; } inset;
        struct { float radius; float cx, cy; bool cx_percent, cy_percent, r_percent; } circle;
        struct { float rx, ry, cx, cy; } ellipse;
        struct { float* points; int count; } polygon;
    } shape;
} ClipPathProp;
```

**Rendering:**
1. Build a ThorVG shape from the clip-path definition.
2. Apply via `tvg_paint_set_mask_method()` with `TVG_MASK_METHOD_ALPHA`.
3. SVG output: emit `<clipPath>` def and reference via `clip-path="url(#...)"`.

**Files to modify:** `view.hpp`, `resolve_css_style.cpp`, `render.cpp`, `render_svg.cpp`
**Complexity:** Medium-High

---

### 2.4 `object-position`

**Status:** CSS property ID exists (`CSS_PROPERTY_OBJECT_POSITION`), default is `"50% 50%"`, but no handler in `resolve_css_style.cpp`. `object-fit` is fully implemented and centers by default, but explicit `object-position` values are ignored.

**Data model changes** — Add to `EmbedProp`:
```c
float object_position_x;       // NEW: 0.0–1.0 (default 0.5)
float object_position_y;       // NEW: 0.0–1.0 (default 0.5)
```

**Implementation:** Parse percentage/length/keyword values. Use in `render.cpp` when computing image offset for `object-fit: contain/cover/none/scale-down`.

**Files to modify:** `view.hpp`, `resolve_css_style.cpp`, `render.cpp`
**Complexity:** Low

---

### 2.5 `border-image`

**Status:** Not parsed or rendered. CSS grammar defines all sub-properties but no handler exists.

**Data model changes** — New struct:
```c
typedef struct BorderImageProp {
    char* source;                // URL of border image
    float slice[4];              // top, right, bottom, left slice offsets
    bool slice_fill;             // fill keyword
    float width[4];              // border-image-width
    float outset[4];             // border-image-outset
    CssEnum repeat_x, repeat_y; // stretch, repeat, round, space
} BorderImageProp;
```

**Rendering:** Slice source image into 9 regions (corners, edges, center). Draw corners at natural size, stretch/repeat/round edges, optionally fill center.

**Files to modify:** `view.hpp`, `resolve_css_style.cpp`, `render_border.cpp`
**Complexity:** High

---

### 2.6 SVG Output: CSS Filters

**Status:** SVG output does not emit `<filter>` elements for CSS filters applied to HTML elements.

**Implementation:** When a block has `FilterProp`, emit SVG `<filter>` defs:
- `blur()` → `<feGaussianBlur stdDeviation="...">`
- `grayscale()` → `<feColorMatrix type="saturate" values="...">`
- `sepia()` → `<feColorMatrix type="matrix" values="...">`
- `brightness()`/`contrast()` → `<feComponentTransfer>` with `<feFuncR>` etc.
- `hue-rotate()` → `<feColorMatrix type="hueRotate">`
- `drop-shadow()` → `<feGaussianBlur>` + `<feOffset>` + `<feFlood>` + `<feComposite>` + `<feMerge>`

**Files to modify:** `render_svg.cpp`, `render_walk.cpp`
**Complexity:** Medium

---

### 2.7 SVG Output: Text Decorations + Spacing

**Status:** SVG does not emit decoration elements for underline/overline/line-through. Also missing `word-spacing` and `letter-spacing` attributes on `<text>`.

**Implementation:**
1. After emitting `<text>`, emit `<line>` with appropriate y-position and stroke.
2. Add `word-spacing="Xpx"` and `letter-spacing="Xpx"` attributes on `<text>` elements.

**Files to modify:** `render_svg.cpp`
**Complexity:** Low

---

## Tier 3 — Text & Typography

### 3.1 `-webkit-line-clamp` / `line-clamp`

**Status:** Not parsed or stored. Common CSS property for truncating multi-line text with ellipsis.

**Implementation:**
1. Parse `-webkit-line-clamp: <integer>` (requires `-webkit-box-orient: vertical` and `display: -webkit-box`).
2. Store as `BlockProp.line_clamp`.
3. In inline layout, track line count. When exceeding the clamp value, stop laying out and append "…" to the last visible line.

**Files to modify:** `view.hpp`, `resolve_css_style.cpp`, `layout_inline.cpp`
**Complexity:** Medium

---

### 3.2 `hyphens`

**Status:** Not parsed or stored.

**Implementation:**
1. Parse `hyphens: none | manual | auto`.
2. For `manual`: break at `&shy;` (U+00AD) and render a hyphen.
3. For `auto`: requires a hyphenation dictionary (consider deferring).

**Files to modify:** `view.hpp`, `resolve_css_style.cpp`, `layout_text.cpp`
**Complexity:** Low (manual) / High (auto)

---

### 3.3 `font-feature-settings`

**Status:** Not parsed or stored. Enables OpenType features like ligatures (`liga`), small caps (`smcp`), old-style figures (`onum`).

**Implementation:**
1. Parse `font-feature-settings: "liga" on, "smcp"`.
2. Store as array of `{tag, value}` pairs on `FontProp`.
3. Pass to HarfBuzz shaping when loading glyphs.

**Files to modify:** `view.hpp`, `resolve_css_style.cpp`, `lib/font/font.c`
**Complexity:** Medium-High

---

### 3.4 `font-stretch`

**Status:** Not parsed. Selects condensed/expanded font variants.

**Implementation:**
1. Parse `font-stretch: condensed | expanded | 50%-200%`.
2. Store as numeric value in `FontProp`.
3. Include in font matching when selecting from available font faces.

**Files to modify:** `view.hpp`, `resolve_css_style.cpp`, `lib/font/font.c`
**Complexity:** Low-Medium

---

## Tier 4 — Advanced Features

### 4.1 CSS Content Functions (`counter()`, `counters()`, `attr()`)

**Status:** Content property partially implemented. String literals work. Functions have TODO stubs at `resolve_css_style.cpp:10366-10387`.

**Implementation:**
1. **`counter(name, style?)`** — Look up counter value from `counter_reset`/`counter_increment` chain.
2. **`counters(name, separator, style?)`** — Join all ancestor counter values.
3. **`attr(name)`** — Read HTML attribute value from the element's `DomElement*`.

**Files to modify:** `resolve_css_style.cpp`, `view.hpp`
**Complexity:** Medium

---

### 4.2 `backdrop-filter`

**Status:** Parsed but no rendering code or data structure.

**Implementation:**
1. Before rendering element, capture pixels behind it from parent surface.
2. Apply filter chain (most common: `blur()`).
3. Composite filtered backdrop, then render element on top.

**Files to modify:** `view.hpp`, `resolve_css_style.cpp`, `render.cpp`
**Complexity:** High

---

### 4.3 `filter: url()` (SVG Filter References)

**Status:** Returns early with log_debug at `render_filter.cpp:286`.

**Implementation:** Resolve URL fragment to SVG `<filter>` definition. Parse and execute filter primitive graph. Consider supporting only common primitives initially.

**Files to modify:** `render_filter.cpp`, `render_svg_inline.cpp`
**Complexity:** High

---

### 4.4 `overflow: hidden` with Per-Corner Radii

**Status:** Clip radius stored per-corner but `rounded_clip_shape()` may not handle asymmetric radii correctly when clip region is smaller than border box.

**Fix:** Ensure each corner uses its individual radius, clamped to half available width/height.

**Files to modify:** `render_background.cpp`
**Complexity:** Low

---

### 4.5 Complex `background` Shorthand

**Status:** `resolve_css_style.cpp:10959` — "Complex background shorthand not yet implemented" for multi-layer mixed-property backgrounds.

**Implementation:** Parse into constituent properties per CSS Backgrounds Level 3 §5.1.

**Files to modify:** `resolve_css_style.cpp`
**Complexity:** Medium

---

### 4.6 `image-rendering`

**Status:** Not parsed. CSS Images Level 3 `image-rendering: pixelated | crisp-edges` controls image scaling interpolation.

**Implementation:** Parse and store in `EmbedProp`. Set ThorVG image scaling mode: nearest-neighbor for `pixelated`, bilinear for `auto`.

**Files to modify:** `view.hpp`, `resolve_css_style.cpp`, `render.cpp`
**Complexity:** Low

---

### 4.7 `writing-mode` (Vertical Text)

**Status:** Parsed and stored for flex layout. Logical CSS properties mapped assuming horizontal-tb. Layout engine is fundamentally horizontal-only.

Full vertical text support requires swapping width/height semantics throughout layout — major architectural change. Consider supporting for flex direction only (already partial).

**Complexity:** Very High

---

### 4.8 CSS Animations and Transitions

**Status:** All animation properties parsed (`resolve_css_style.cpp:9604-9654`) but only log_debug. No keyframe storage, timing engine, or interpolation.

**Recommendation:** Defer to a future major version. Radiant is a static renderer. If needed, implement simplified `transition` for opacity/transform/color first.

**Complexity:** Very High (architectural change)

---

## Implementation Order

| Phase | Features | Complexity | Impact |
|-------|----------|------------|--------|
| **Phase 10** | SVG border-radius fix (per-corner `<path>`), `overflow:hidden` radii fix | Low-Medium | High — fixes visual bugs |
| **Phase 11** | `text-decoration-style/color/thickness`, underline position fix | Medium | High — fixes visible bug |
| **Phase 12** | `list-style-image` rendering, `disclosure-open/closed`, `list-style-position` quality | Medium | High — visible feature gap |
| **Phase 13** | `text-overflow:ellipsis`, `min()`/`max()`/`clamp()` | Medium | High — common CSS features |
| **Phase 14** | `mix-blend-mode`, `background-blend-mode` | Medium-High | Medium-High |
| **Phase 15** | Opacity offscreen compositing, z-index stacking | Medium-High | High — correctness |
| **Phase 16** | `clip-path` (basic shapes), `object-position` | Medium-High | Medium |
| **Phase 17** | SVG CSS filters + text-decoration + spacing output | Medium | Medium — output parity |
| **Phase 18** | `-webkit-line-clamp`, `image-rendering`, `font-stretch` | Low-Medium | Medium |
| **Phase 19** | CSS content functions, complex background shorthand | Medium | Medium |
| **Phase 20** | `border-image`, `font-feature-settings` | High | Medium |
| **Phase 21** | `backdrop-filter`, `filter:url()`, `hyphens:auto` | High | Low — rare usage |
| **Phase 22** | CSS animations/transitions, `writing-mode` vertical | Very High | Low — architectural |

---

## Gap Summary Table

| Feature | Style Parse | Data Model | Raster Render | SVG Output |
|---------|-------------|------------|---------------|------------|
| **Border-radius (SVG per-corner)** | ✅ | ✅ | ✅ | ❌ `top_left` only |
| **Border-radius (elliptical)** | ❌ | ❌ | ❌ | ❌ |
| **`list-style-image`** | ✅ | ✅ stored | ❌ not rendered | ❌ |
| **`list-style-position` quality** | ✅ | ✅ | ⚠️ basic | ⚠️ |
| **`disclosure-open/closed` markers** | ❌ | ❌ | ❌ | ❌ |
| **`text-decoration-style`** | ❌ | ❌ | ❌ | ❌ |
| **`text-decoration-color`** | ❌ | ❌ | ❌ | ❌ |
| **`text-decoration-thickness`** | ❌ | ❌ | ❌ | ❌ |
| **`text-underline-offset`** | ❌ | ❌ | ❌ | ❌ |
| **Underline position bug** | N/A | N/A | ❌ wrong position | N/A |
| **`text-overflow: ellipsis`** | ⚠️ log only | ❌ | ❌ | ❌ |
| **`min()`/`max()`/`clamp()`** | ⚠️ log only | N/A | N/A | N/A |
| **`mix-blend-mode`** | ❌ | ❌ | ❌ | ❌ |
| **`background-blend-mode`** | ⚠️ log only | ❌ | ❌ | ❌ |
| **`clip-path` (basic shapes)** | ❌ | ❌ | ❌ | ❌ |
| **`clip: rect()`** | ⚠️ stub | ❌ | ❌ | ❌ |
| **`object-position`** | ❌ | ❌ | ❌ | ❌ |
| **`border-image`** | ❌ | ❌ | ❌ | ❌ |
| **`backdrop-filter`** | ⚠️ parsed | ❌ | ❌ | ❌ |
| **`filter: url()`** | ✅ parsed | ✅ | ❌ | ❌ |
| **`-webkit-line-clamp`** | ❌ | ❌ | ❌ | ❌ |
| **`hyphens`** | ❌ | ❌ | ❌ | ❌ |
| **`font-feature-settings`** | ❌ | ❌ | ❌ | ❌ |
| **`font-stretch`** | ❌ | ❌ | ❌ | ❌ |
| **`image-rendering`** | ❌ | ❌ | ❌ | ❌ |
| **`counter()`/`counters()`/`attr()`** | ⚠️ stub | ⚠️ type only | ❌ | ❌ |
| **Opacity (group compositing)** | ✅ | ✅ | ⚠️ per-pixel | ⚠️ |
| **Z-index stacking order** | ⚠️ basic | ⚠️ basic | ⚠️ doc order | ⚠️ |
| **SVG: CSS filters** | N/A | N/A | N/A | ❌ |
| **SVG: text decorations** | N/A | N/A | N/A | ❌ |
| **SVG: word/letter-spacing** | N/A | N/A | N/A | ❌ |
| **CSS animations** | ⚠️ log only | ❌ | ❌ | ❌ |
| **CSS transitions** | ⚠️ log only | ❌ | ❌ | ❌ |
| **`writing-mode` (vertical)** | ⚠️ flex only | ⚠️ flex only | ❌ | ❌ |

Legend: ✅ Complete | ⚠️ Partial/Stub | ❌ Not implemented

---

## Testing Strategy

### Visual Regression Tests

New test HTML pages in `test/render/page/`. Each is a 100×100px page exercising a specific CSS property. Reference PNGs captured from a browser.

### Proposed Test Files

| Test File | Feature |
|-----------|---------|
| `border_radius_per_corner_01.html` | Different radii per corner |
| `border_radius_elliptical_01.html` | Elliptical `border-radius: 20px / 10px` |
| `list_style_image_01.html` | `list-style-image: url(...)` |
| `list_style_types_01.html` | All numbered list types (roman, alpha, greek) |
| `list_inside_outside_01.html` | `list-style-position: inside` vs `outside` |
| `text_deco_style_01.html` | `text-decoration-style: dashed/dotted/wavy/double` |
| `text_deco_color_01.html` | `text-decoration-color` with different colors |
| `text_overflow_ellipsis_01.html` | `text-overflow: ellipsis` with `overflow:hidden` |
| `css_clamp_01.html` | `width: clamp(50px, 80%, 90px)` |
| `clip_path_circle_01.html` | `clip-path: circle(50%)` |
| `clip_path_polygon_01.html` | `clip-path: polygon(...)` |
| `blend_mode_01.html` | `background-blend-mode: multiply` |
| `mix_blend_mode_01.html` | `mix-blend-mode: screen` |
| `opacity_stacking_01.html` | Nested opacity with overlapping children |
| `z_index_order_01.html` | Positioned elements with z-index ordering |
| `filter_drop_shadow_01.html` | `filter: drop-shadow(...)` on shaped element |
| `object_position_01.html` | `object-fit: cover` + `object-position: top left` |
| `line_clamp_01.html` | `-webkit-line-clamp: 2` multi-line truncation |
| `backdrop_filter_01.html` | `backdrop-filter: blur(5px)` |
| `image_rendering_01.html` | `image-rendering: pixelated` on small image |

### SVG Output Verification

String-based checks verifying SVG markup contains expected elements:
- `<path d="...">` for per-corner border-radius (not `<rect rx>`)
- `<filter>` / `<feGaussianBlur>` for CSS filters
- `<line>` for text decorations
- `word-spacing` / `letter-spacing` attributes on `<text>`

---

## Architecture Notes

### Render Backend Extension

The `RenderBackend` vtable (`render_backend.h`) should be extended:

```c
struct RenderBackend {
    // ... existing callbacks ...

    // NEW: Filter wrapper (for SVG <filter> emission)
    void (*begin_filter)(void* ctx, ViewBlock* block, float abs_x, float abs_y);
    void (*end_filter)(void* ctx);

    // NEW: Clip-path wrapper
    void (*begin_clip_path)(void* ctx, ViewBlock* block, float abs_x, float abs_y);
    void (*end_clip_path)(void* ctx);

    // NEW: Text decoration rendering
    void (*render_text_decoration)(void* ctx, ViewText* text, float abs_x, float abs_y,
                                    FontBox* font, Color color);
};
```

### Memory Allocation

All new property structs allocated via `alloc_prop()` / `pool_calloc()`. Variable-length data (gradient stops, polygon points) uses `arena_alloc()`.

### Specificity Tracking

New CSS properties participating in the cascade should include `int64_t *_specificity` fields, consistent with existing `BorderProp` patterns.
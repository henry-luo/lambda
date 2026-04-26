# Radiant–ThorVG Deep Integration Design

**Date:** April 2026
**Status:** Stage 1 complete

---

## Design Decision (Updated April 11, 2026)

**Keep ThorVG as the rendering backend on all three platforms for this phase.**

Extracting ThorVG's `sw_engine` or replacing it with platform-native backends (Core Graphics, Direct2D) is deferred to a future phase. The scope of that change is too large for a single step.

Stage 1 focuses on **isolation**: introduce `RdtVector` as Radiant's sole vector rendering interface. All ThorVG C API calls are moved out of Radiant rendering code and into a single backend file (`rdt_vector_tvg.cpp`). Radiant rendering files (`render.cpp`, `render_border.cpp`, `render_background.cpp`, `render_form.cpp`, `render_svg_inline.cpp`, `scroller.cpp`) call only `rdt_*` functions — never `tvg_*` directly.

This gives us:
1. **Clean separation** — ThorVG is an implementation detail behind a stable API.
2. **Future swapability** — replace `rdt_vector_tvg.cpp` with `rdt_vector_cg.mm` (macOS), `rdt_vector_d2d.cpp` (Windows), or an extracted `sw_engine` backend later.
3. **Immediate-mode semantics** — `RdtVector` is an immediate-mode API. The ThorVG backend handles the push/draw/remove dance internally, hidden from callers.
4. **Platform-native ready** — the API is designed for eventual platform-native backends (no ThorVG types leak into the header).

### What Changes

| Before | After |
|--------|-------|
| 30+ files call `tvg_*` directly | Only `rdt_vector_tvg.cpp` calls `tvg_*` |
| `Tvg_Canvas` in RenderContext | `RdtVector vec` in RenderContext |
| `Tvg_Paint` shapes in render code | `RdtPath*` + `rdt_fill_path`/`rdt_stroke_path` |
| Push/draw/remove on every shape | Single `rdt_fill_rect()`-style calls |
| ThorVG types in headers | Only `RdtVector`, `RdtPath`, `RdtMatrix` exposed |

### What Stays the Same

- ThorVG linked as external static library (`libthorvg.a`) on all platforms
- SVG text rendering uses ThorVG's `tvg_text_*` API (wrapped via `rdt_picture_take_tvg_paint`)
- SVG `<image>` loading uses ThorVG's `tvg_picture_*` API (wrapped via `rdt_picture_take_tvg_paint`)
- Build system unchanged (ThorVG in `mac-deps/`, `win-native-deps/`, `/usr/local/lib/`)
- Text/font pipeline entirely Radiant-owned (unchanged)

---

## 1. Motivation

Radiant currently uses ThorVG v1.0-pre34 as an external dependency, linked via the C API (`thorvg_capi.h`). This works, but the integration has three structural problems:

1. **Immediate-mode misuse of a retained-mode API.** Radiant creates a ThorVG shape, pushes it to the Canvas, draws, then immediately removes and destroys it — on every shape, every frame. ThorVG's Canvas/Scene layer is designed for retained-mode rendering (push once, update incrementally), but we use it as an immediate rasterizer, paying per-shape overhead for scene-graph bookkeeping we don't need.

2. **Text and font handling gap.** ThorVG's `Text` class converts TTF outlines into vector shapes, which gives no hinting, no subpixel AA, no complex shaping, and no font fallback. For HTML document text, Radiant already has a full font pipeline (CoreText on macOS, FreeType on Linux). On Linux, after FreeType is phased out, we need a glyph rasterization path — but not ThorVG's high-level `Text` API.

3. **Binary bloat from unused subsystems.** ThorVG ships ~189k lines of loaders (Lottie alone is 161k), GPU engines (GL 8.6k, WGPU 6.6k), animation framework, and a Saver module. Radiant uses only the software rasterizer (~9.5k lines) and SVG loader (~7.5k lines).

### What We Want from ThorVG

Only the **software vector rasterizer**: the ability to tessellate paths into RLE spans and composite them onto a pixel buffer with fills, strokes, gradients, transforms, clipping, and masking. This is concentrated in:

| Module | Lines | Purpose |
|--------|-------|---------|
| `sw_engine/` | ~9,500 | Software rasterizer (RLE, fill, stroke, image, post-effects) |
| `renderer/` core | ~7,000 | Paint, Shape, Scene, Fill, Render abstractions |
| `common/` | ~2,100 | Math, color, string, compression utilities |
| `loaders/svg/` | ~7,500 | SVG parser (optional — Radiant has its own SVG-to-scene converter) |
| **Total needed** | **~19,000–26,000** | Depending on SVG loader inclusion |

We do **not** need: GL/WGPU engines, Lottie/animation, TTF loader, Text class, Canvas scene-graph management, image loaders (PNG/JPG/WebP/GIF), Saver, C API bindings.

---

## 2. Current Architecture and Its Problems

### 2.1 Current Render Pattern

For every shape Radiant draws (border segments, bullets, wavy underlines, rounded backgrounds, SVG elements), the code follows this pattern:

```cpp
Tvg_Paint shape = tvg_shape_new();           // 1. allocate Paint
tvg_shape_append_rect(shape, ...);           // 2. build path
tvg_shape_set_fill_color(shape, r, g, b, a); // 3. set style
tvg_canvas_push(canvas, shape);              // 4. push to Canvas::Scene
tvg_canvas_reset_and_draw(rdcon, false);     // 5. update + render + sync
tvg_canvas_remove(canvas, NULL);             // 6. remove from Scene, destroy
```

What happens inside ThorVG on each cycle:

- **`push`**: Adds Paint to Canvas's internal `Scene` (linked-list insert)
- **`draw`** (which calls `update` internally): Allocates a `SwShapeTask`, computes transform, tessellates path into RLE, processes fill/stroke — all into renderer-allocated `RenderData`
- **`render`**: Rasterizes RLE spans to pixel buffer with compositing
- **`sync`**: Waits for task completion
- **`remove`**: Removes from Scene, calls `renderer->dispose()` to free `RenderData`, deletes the Paint

For a typical page render, this happens dozens of times per element. The Canvas/Scene overhead (status tracking, scene-graph insert/remove, dirty-region management) is pure waste for our use case.

### 2.2 The `reset_and_draw` Workaround

Radiant must call `tvg_swcanvas_set_target()` before every draw to reset ThorVG's dirty-region tracker. Without this, ThorVG clears previously-drawn content (because it thinks the "dirty" area needs clearing). This is a workaround for using ThorVG in immediate mode — its multi-shape incremental update system fights our one-shape-at-a-time pattern.

### 2.3 SVG Re-build Every Frame

For inline `<svg>` elements, `build_svg_scene()` walks the SVG DOM and constructs a full ThorVG scene tree (nested `Scene` nodes, `Shape`/`Text` paints). After one render, the entire tree is destroyed. On re-render (e.g., dirty-region update, scroll), it's rebuilt from scratch.

---

## 3. Approach: Isolation via RdtVector

Instead of extracting or forking ThorVG, we **isolate** it behind `RdtVector` — a thin immediate-mode vector rendering API owned by Radiant.

### Why Isolation First

1. We want only ~19k of ~240k total ThorVG lines, but extracting requires deep surgery into ThorVG's renderer/sw_engine coupling. The isolation layer gives us the clean API boundary first.
2. Once all Radiant code calls `rdt_*` instead of `tvg_*`, swapping the backend (to platform-native or extracted sw_engine) becomes a single-file change.
3. The ThorVG push/draw/remove overhead is hidden inside the backend — callers get clean immediate-mode semantics.
4. Risk is minimal: if the backend change causes regressions, only `rdt_vector_tvg.cpp` needs fixing.

### Architecture

```
┌───────────────────────────────────────────────────┐
│              Radiant View Tree                     │
│   DomElement → ViewBlock / ViewSpan / ViewText     │
├───────────────────────────────────────────────────┤
│         Radiant Render Dispatch                    │
│  render_block_view → render_inline_view → ...      │
│  render_border → render_background → scroller      │
│                                                    │
│  All rendering calls: rdt_fill_rect(),             │
│  rdt_stroke_path(), rdt_fill_linear_gradient()...  │
│                                                    │
│  NO tvg_* calls in any render file.                │
├───────────────────────────────────────────────────┤
│         rdt_vector.hpp (API header)                │
│  RdtVector, RdtPath, RdtMatrix, RdtPicture         │
│  Free functions, compile-time backend dispatch      │
├───────────────────────────────────────────────────┤
│    rdt_vector_tvg.cpp (ThorVG backend)             │
│    Wraps tvg_* C API calls internally              │
│    Manages push/draw/remove per shape              │
│    Owns Tvg_Canvas lifecycle                        │
├───────────────────────────────────────────────────┤
│         ThorVG (external, libthorvg.a)             │
│         Unchanged — linked as static library        │
└───────────────────────────────────────────────────┘
```

---

## 4. RdtVector API

### 4.1 API Header (`rdt_vector.hpp`)

The API is C-style free functions with opaque handles. No ThorVG types are exposed.

```cpp
// Opaque types
typedef struct RdtVector { RdtVectorImpl* impl; } RdtVector;
typedef struct RdtPath RdtPath;
typedef struct RdtPicture RdtPicture;
typedef struct { float e11,e12,e13, e21,e22,e23, e31,e32,e33; } RdtMatrix;

// Lifecycle
void rdt_vector_init(RdtVector* vec, uint32_t* pixels, int w, int h, int stride);
void rdt_vector_destroy(RdtVector* vec);
void rdt_vector_set_target(RdtVector* vec, uint32_t* pixels, int w, int h, int stride);

// Path construction
RdtPath* rdt_path_new(void);
void     rdt_path_move_to/line_to/cubic_to/close/add_rect/add_circle/free(...);

// Immediate-mode drawing
void rdt_fill_path(vec, path, color, fill_rule, transform);
void rdt_fill_rect(vec, x, y, w, h, color);
void rdt_fill_rounded_rect(vec, x, y, w, h, rx, ry, color);
void rdt_stroke_path(vec, path, color, width, cap, join, dash, dash_count, transform);
void rdt_fill_linear_gradient(vec, path, x1,y1,x2,y2, stops, count, rule, transform);
void rdt_fill_radial_gradient(vec, path, cx,cy,r, stops, count, rule, transform);

// Clipping
void rdt_push_clip(vec, clip_path, transform);
void rdt_pop_clip(vec);

// Image
void rdt_draw_image(vec, pixels, w, h, stride, dst_x, dst_y, dst_w, dst_h, opacity, transform);

// SVG picture (wraps ThorVG picture internally)
RdtPicture* rdt_picture_load(path);
RdtPicture* rdt_picture_load_data(data, size, mime_type);
void        rdt_picture_draw(vec, pic, opacity, transform);
void        rdt_picture_free(pic);
```

### 4.2 ThorVG Backend Pattern

Each `rdt_*` call in the ThorVG backend follows this internal pattern:

```cpp
void rdt_fill_rect(RdtVector* vec, float x, float y, float w, float h, Color color) {
    Tvg_Canvas canvas = vec->impl->canvas;
    Tvg_Paint shape = tvg_shape_new();
    tvg_shape_append_rect(shape, x, y, w, h, 0, 0, true);
    tvg_shape_set_fill_color(shape, color.r, color.g, color.b, color.a);
    rdt_tvg_push_draw_remove(vec->impl, shape);  // internal: push + draw + sync + remove
}
```

The push/draw/remove cycle is centralized in one helper, eliminating duplicated boilerplate across 30+ call sites.

---

## 5. File Organization

```
radiant/
├── rdt_vector.hpp          # API header — no ThorVG types exposed
├── rdt_vector_tvg.cpp      # ThorVG backend (wraps tvg_* C API)
├── render.hpp              # RenderContext has RdtVector vec (not Tvg_Canvas)
├── render.cpp              # calls rdt_* only
├── render_border.cpp       # calls rdt_* only
├── render_background.cpp   # calls rdt_* only
├── render_form.cpp         # calls rdt_* only
├── render_svg_inline.cpp   # calls rdt_* + tvg_text_*/tvg_picture_* (internal bridge)
├── scroller.cpp            # calls rdt_* only
├── surface.cpp             # calls rdt_picture_load/free for SVG, pixel-level for raster
├── cmd_layout.cpp          # calls rdt_picture_load/free for standalone SVG loading
├── ui_context.cpp          # calls rdt_engine_init/term, rdt_font_load
└── ...
```

Future backends (Stage 2):
```
├── rdt_vector_cg.mm        # Core Graphics (macOS) — Stage 2
├── rdt_vector_d2d.cpp      # Direct2D (Windows) — Stage 2
├── rdt_vector_sw.cpp       # Extracted ThorVG sw_engine — Stage 2
```

---

## 6. Migration Plan

### Stage 1: ThorVG Deep Integration (complete)

#### Phase 1: RdtVector API + ThorVG Backend + Migration ✅

1. ✅ Define `rdt_vector.hpp` — complete API header, no ThorVG types exposed
2. ✅ Implement `rdt_vector_tvg.cpp` — wraps ThorVG C API behind RdtVector
3. ✅ Migrate `render_form.cpp` — all tvg_ calls replaced with rdt_*
4. ✅ Migrate `scroller.cpp` — all tvg_ calls replaced
5. ✅ Migrate `render.cpp` — overlays, markers, wavy lines, column rules, debug rect, images
6. ✅ Migrate `render_border.cpp` — trapezoids, rounded borders, dashes, clipping
7. ✅ Migrate `render_background.cpp` — gradients, shadows, images, rounded-rect clipping
8. ✅ Migrate `render_svg_inline.cpp` — SVG shapes, transforms, scenes → rdt_* calls
9. ✅ Migrate `surface.cpp` — SVG picture creation
10. ✅ Remove `Tvg_Canvas` from RenderContext, remove all direct tvg_ includes from render files
11. ✅ `make test-radiant-baseline` — 4005/4005 pass

#### Phase 2: SVG Inline Rendering Rework ✅

Eliminated the ThorVG scene tree (`Tvg_Paint`/`Tvg_Scene`) from inline SVG rendering.
`build_svg_scene()` is replaced by `render_svg_to_vec()` which draws directly to `RdtVector`.

1. ✅ Added `rdt_matrix_multiply()` and `rdt_matrix_translate()` to `rdt_vector.hpp`
2. ✅ Updated `SvgRenderContext` — added `RdtVector* vec` and `RdtMatrix transform` for accumulated transforms
3. ✅ Replaced gradient/fill/stroke/transform helpers: `draw_gradient_fill()`, `draw_svg_fill_stroke()`, `compose_element_transform()`
4. ✅ Converted all shape renderers (rect, circle, ellipse, line, polyline, polygon, path) — build `RdtPath`, call `rdt_fill_path`/`rdt_stroke_path`/`rdt_fill_*_gradient` directly
5. ✅ Converted group/children rendering — save/restore `ctx->transform` instead of creating ThorVG scenes
6. ✅ Converted `render_svg_element()` dispatcher — void return, no scene push
7. ✅ Converted `<use>` element — compose offset into accumulated transform
8. ✅ Converted text rendering — `create_text_segment()` still uses `tvg_text_*` API internally, wraps result via `rdt_picture_take_tvg_paint()` + `rdt_picture_draw()`
9. ✅ Converted image rendering — `tvg_picture_*` loading wrapped via `rdt_picture_take_tvg_paint()`
10. ✅ Rewrote `build_svg_scene()` → `render_svg_to_vec()` — computes viewBox transform, composes with base transform, renders children directly to vec
11. ✅ Simplified `render_inline_svg()` — builds base transform (position + scale + document transform), calls `render_svg_to_vec()` directly
12. ✅ Build: 0 errors, 0 warnings. Tests: 4005/4005 baseline pass

**Remaining ThorVG usage in `render_svg_inline.cpp`** (26 `tvg_` calls, all properly wrapped):
- `create_text_segment()` — `tvg_text_*`/`tvg_font_load` for SVG `<text>` elements
- `render_svg_image()` — `tvg_picture_*` for SVG `<image>` elements
- Both bridge to rdt_ via `rdt_picture_take_tvg_paint()` before drawing

#### Phase 3: Full ThorVG Elimination Outside Isolation Files ✅

Removed all remaining `tvg_*` / `Tvg_*` / `thorvg_capi.h` usage from every file outside the two isolation files (`rdt_vector_tvg.cpp`, `render_svg_inline.cpp`). ThorVG is now fully encapsulated.

1. ✅ Changed `ImageSurface::pic` type from `Tvg_Paint` to `struct RdtPicture*` in `view.hpp`
2. ✅ Added `rdt_picture_dup()`, `rdt_engine_init()`, `rdt_engine_term()`, `rdt_font_load()` to rdt_ API
3. ✅ Removed `#include <thorvg_capi.h>` from `view.hpp` — no ThorVG types remain in the header
4. ✅ Migrated `surface.cpp` — `tvg_picture_new/load/load_data/get_size/paint_unref` → `rdt_picture_load/load_data/get_size/free`
5. ✅ Deleted dead code `create_tvg_picture_from_surface()` from `surface.cpp` (~40 lines, zero callers)
6. ✅ Updated `render.cpp` — `surface->pic` is `RdtPicture*`, used directly (no bridge wrapper needed)
7. ✅ Updated `render_background.cpp` — `rdt_picture_from_tvg_paint()` → `rdt_picture_dup()`
8. ✅ Migrated `cmd_layout.cpp` — all `tvg_picture_*` calls → `rdt_picture_load/get_size/free`
9. ✅ Migrated `ui_context.cpp` — `tvg_engine_init/term` → `rdt_engine_init/term`, `tvg_font_load` → `rdt_font_load`
10. ✅ Removed unused `#include <thorvg_capi.h>` from `pdf/pdf_to_view.cpp`
11. ✅ Removed bridge functions from `rdt_vector.hpp`: `rdt_vector_get_tvg_canvas`, `rdt_picture_from_tvg_paint`, `Tvg_Matrix` conversion utilities
12. ✅ Kept `rdt_picture_take_tvg_paint` — only bridge function remaining, used by `render_svg_inline.cpp` for text/image paint
13. ✅ Build: 0 errors. Tests: Lambda 566/566, Layout 4005/4005 baseline pass

**ThorVG is now isolated to exactly 3 files:**
- `rdt_vector_tvg.cpp` — backend implementation (all `tvg_*` calls)
- `render_svg_inline.cpp` — SVG text/image rendering (uses `tvg_text_*`/`tvg_picture_*` internally)
- `rdt_vector.hpp` — single `#include <thorvg_capi.h>` + one bridge declaration (`rdt_picture_take_tvg_paint`)

Zero `tvg_*` or `Tvg_*` references exist anywhere else in the codebase.

### Stage 1.5: SVG Loader Replacement (complete)

**Problem.**  ThorVG's bundled SVG loader (`tvg_picture_load`/`tvg_picture_load_data` for `mime_type="svg"`) matches `<text>` fonts by *family name only*.  When an SVG asset declares
`font-family="Times New Roman" font-weight="bold"`, ThorVG renders with the
registered "Times New Roman" face — never picking up "Times New Roman Bold"
even when both are loaded via `tvg_font_load`.  Browsers, by contrast,
resolve weighted family lookups to the appropriate face.  The bug surfaces
in any SVG with bold text (chalk readme logo, badge SVGs, chart
annotations).  Earlier mitigations — preloading bold variants and
preprocessing the SVG source to rewrite `font-family` values — were brittle
workarounds, not fixes.

**Fix.**  External SVGs go through the *same* loader and renderer as inline
`<svg>` in HTML body:

1. `rdt_picture_load(path)` reads the file → calls `parse_xml()` → walks
   the document wrapper to locate the root `<svg>` Element.
2. `rdt_picture_load_data(data, size, "svg")` does the same on an
   in-memory buffer (HTTP responses, data URIs).
3. Intrinsic size is computed via `calculate_svg_intrinsic_size()` (same
   helper used by inline SVG).
4. `rdt_picture_draw()` dispatches by kind:
   - `KIND_SVG_DOM`: calls `render_svg_to_vec(vec, svg_root, w, h, pool,
     1.0f, font_ctx, transform, NULL)` from `render_svg_inline.cpp`.
   - `KIND_TVG_PAINT`: legacy path retained for `<text>`/`<image>`
     primitives that the SVG renderer wraps via
     `rdt_picture_take_tvg_paint()`.

`render_svg_to_vec` already resolves font weight/style correctly: each
text segment calls `font_find_best_match()` → `resolve_svg_font_path()` →
`tvg_font_load(bold_path)` and `tvg_text_set_font(bold_name)` per text
node.  No preloaded fonts are needed.

**Font context propagation.**  Inline SVG inside HTML body uses
`RenderContext->ui_context->font_ctx`.  Pictures rendered off-screen
(into `ImageSurface`) have no render context, so `rdt_set_font_context()`
publishes a process-wide `FontContext*` pointer; `rdt_picture_draw()`
passes it to `render_svg_to_vec`.  Set once from `ui_context_init()`
right after the font context is created.

**Removed workarounds.**
- `radiant/surface.cpp::preprocess_svg_bold_fonts()` — ~170 lines of
  textual rewriting of `font-family` values inside elements with
  `font-weight: bold`.
- `radiant/ui_context.cpp` bold-variant preload loop for Times New Roman,
  Times, Georgia, Arial, Helvetica, Verdana, Geneva.

**Status.**  ThorVG's SVG loader is still linked into the binary but no
longer invoked from Radiant.  All file-, HTTP-, and data-URI SVG sources
flow through the unified Radiant pipeline.  Stage 2's platform-native
backends will not need an SVG loader at all.

### Stage 2: Platform-Native Backends (future)

1. Implement `rdt_vector_cg.mm` — Core Graphics backend for macOS
2. Implement `rdt_vector_d2d.cpp` — Direct2D backend for Windows
3. Compile-time backend selection via platform #ifdef
4. Pixel-level baseline comparison to validate rendering equivalence
5. Extract `sw_engine` from ThorVG for Linux-only use
6. Remove external ThorVG dependency
7. Implement `rdt_vector_sw.cpp` wrapping extracted sw_engine directly

---

## 7. Risks and Mitigations

| Risk | Impact | Mitigation |
|------|--------|------------|
| ThorVG backend adds indirection overhead | Low — one extra function call per shape, dominated by rasterization time | Profile if needed; the indirection is negligible vs. rasterization |
| Rendering regressions during migration | Medium | Run `make test-radiant-baseline` after each file migration |
| RdtVector API gaps discovered during migration | Low | API can be extended as needed; it's Radiant-owned |
| ThorVG push/draw/remove overhead not eliminated | Accepted for Stage 1 | Stage 2 platform-native backends eliminate it entirely |

---

## 8. Summary

| Aspect | Before | After (Stage 1) | Stage 2 |
|--------|--------|-----------------|--------|
| ThorVG dependency | External, C API | External, C API (isolated) | Extracted / replaced |
| ThorVG calls in render code | 30+ files | Only `rdt_vector_tvg.cpp` + `render_svg_inline.cpp` (3 files total) | Same |
| Rendering API | `tvg_*` C functions | `rdt_*` free functions | Same API, different backend |
| Scene management | Push/draw/remove inline, ThorVG scene tree for SVG | Push/draw/remove in backend, direct draw for SVG | Immediate-mode (native) |
| SVG rendering | `build_svg_scene()` → ThorVG scene tree | `render_svg_to_vec()` → direct `rdt_*` draw calls | Same |
| Text rendering | Radiant (all) | Radiant (all) | Same |
| Binary footprint | Full ThorVG | Full ThorVG | sw_engine only (~19k LoC) |

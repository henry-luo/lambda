# Radiant–ThorVG Deep Integration Design

**Date:** April 2026
**Status:** Active (Phase 1 in progress)

---

## Design Decision (Updated April 11, 2026)

**Keep ThorVG as the rendering backend on all three platforms for this phase.**

Extracting ThorVG's `sw_engine` or replacing it with platform-native backends (Core Graphics, Direct2D) is deferred to a future phase. The scope of that change is too large for a single step.

Instead, this phase focuses on **isolation**: introduce `RdtVector` as Radiant's sole vector rendering interface. All ThorVG C API calls are moved out of Radiant rendering code and into a single backend file (`rdt_vector_tvg.cpp`). Radiant rendering files (`render.cpp`, `render_border.cpp`, `render_background.cpp`, `render_form.cpp`, `render_svg_inline.cpp`, `scroller.cpp`) call only `rdt_*` functions — never `tvg_*` directly.

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
- SVG rasterization uses ThorVG's SVG parser and Canvas (wrapped in RdtPicture)
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
├── render_svg_inline.cpp   # calls rdt_* only
├── scroller.cpp            # calls rdt_* only
├── surface.cpp             # fill_surface_rect stays pixel-level
└── ...
```

Future backends (not in this phase):
```
├── rdt_vector_cg.mm        # Core Graphics (macOS) — future
├── rdt_vector_d2d.cpp      # Direct2D (Windows) — future
├── rdt_vector_sw.cpp       # Extracted ThorVG sw_engine — future
```

---

## 6. Migration Plan

### Phase 1: RdtVector API + ThorVG Backend + Migration (current)

1. ✅ Define `rdt_vector.hpp` — complete API header, no ThorVG types exposed
2. ✅ Implement `rdt_vector_tvg.cpp` — wraps ThorVG C API behind RdtVector
3. ✅ Migrate `render_form.cpp` — all tvg_ calls replaced with rdt_*
4. ✅ Migrate `scroller.cpp` — all tvg_ calls replaced
5. ✅ Migrate `render.cpp` — overlays, markers, wavy lines, column rules, debug rect, images
6. ✅ Migrate `render_border.cpp` — trapezoids, rounded borders, dashes, clipping
7. ✅ Migrate `render_background.cpp` — gradients, shadows, images, rounded-rect clipping
8. ☐ Migrate `render_svg_inline.cpp` — SVG shapes, transforms, scenes
9. ☐ Migrate `surface.cpp` — SVG picture creation
10. ☐ Remove `Tvg_Canvas` from RenderContext, remove all direct tvg_ includes from render files
11. ☐ Run `make test-radiant-baseline` — must pass 100%

### Phase 2: Platform-Native Backends (future)

1. Implement `rdt_vector_cg.mm` — Core Graphics backend for macOS
2. Implement `rdt_vector_d2d.cpp` — Direct2D backend for Windows
3. Compile-time backend selection via platform #ifdef
4. Pixel-level baseline comparison to validate rendering equivalence

### Phase 3: ThorVG Extraction (future)

1. Extract `sw_engine` from ThorVG for Linux-only use
2. Remove external ThorVG dependency
3. Implement `rdt_vector_sw.cpp` wrapping extracted sw_engine directly

---

## 7. Risks and Mitigations

| Risk | Impact | Mitigation |
|------|--------|------------|
| ThorVG backend adds indirection overhead | Low — one extra function call per shape, dominated by rasterization time | Profile if needed; the indirection is negligible vs. rasterization |
| Rendering regressions during migration | Medium | Run `make test-radiant-baseline` after each file migration |
| RdtVector API gaps discovered during migration | Low | API can be extended as needed; it's Radiant-owned |
| ThorVG push/draw/remove overhead not eliminated | Accepted for this phase | Future platform-native backends eliminate it entirely |

---

## 8. Summary

| Aspect | Before | After (Phase 1) | Future |
|--------|--------|-----------------|--------|
| ThorVG dependency | External, C API | External, C API (isolated) | Extracted / replaced |
| ThorVG calls in render code | 30+ files | Only `rdt_vector_tvg.cpp` | Same |
| Rendering API | `tvg_*` C functions | `rdt_*` free functions | Same API, different backend |
| Scene management | Push/draw/remove inline | Push/draw/remove in backend | Immediate-mode (native) |
| Text rendering | Radiant (all) | Radiant (all) | Same |
| Binary footprint | Full ThorVG | Full ThorVG | sw_engine only (~19k LoC) |

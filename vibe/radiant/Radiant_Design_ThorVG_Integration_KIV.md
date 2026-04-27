# Radiant Vector Rendering — Platform-Native Design (KIV)

**Date:** April 2026
**Status:** Proposal
**Supersedes:** Radiant_Design_ThorVG_Integration.md

---

## 1. Motivation

Radiant currently uses ThorVG v1.0-pre34 as an external dependency, linked via the C API (`thorvg_capi.h`). This integration has structural problems:

1. **Immediate-mode misuse of a retained-mode API.** Radiant creates a ThorVG shape, pushes it to the Canvas, draws, then immediately removes and destroys it — on every shape, every frame. ThorVG's Canvas/Scene layer is designed for retained-mode rendering (push once, update incrementally), but we use it as an immediate rasterizer, paying per-shape overhead for scene-graph bookkeeping we don't need.

2. **Text and font handling gap.** ThorVG's `Text` class converts TTF outlines into vector shapes — no hinting, no subpixel AA, no complex shaping, no font fallback. Radiant already has a full font pipeline (CoreText on macOS, FreeType on Linux). ThorVG's `Text` API is inadequate for HTML document rendering.

3. **Binary bloat from unused subsystems.** ThorVG ships ~189k lines of loaders (Lottie alone is 161k), GPU engines (GL 8.6k, WGPU 6.6k), animation framework, and Saver module. Radiant uses only the software rasterizer (~9.5k lines).

4. **Native OS APIs are superior.** Both macOS and Windows provide mature, hardware-tuned 2D vector rasterizers (Core Graphics and Direct2D respectively) that are already linked or readily available. Using ThorVG on these platforms is unnecessary overhead.

---

## 2. Platform-Native Strategy

Instead of one cross-platform rasterizer, use the best available API per platform:

| Platform | Vector Backend | Rationale |
|----------|---------------|-----------|
| **macOS** | Core Graphics (Quartz 2D) | Already linked via CoreText. Hardware-tuned, immediate-mode, subpixel AA. Used by Safari and Chrome internally. |
| **Windows** | Direct2D | Standard modern Windows 2D API. GPU-accelerated with software fallback. |
| **Linux** | ThorVG sw_engine (extracted) | No native OS vector rasterizer. ThorVG sw_engine is ~19k LoC, MIT licensed, proven in Radiant. |
| **WASM** | ThorVG sw_engine (shared with Linux) | Same software rasterizer for headless/embedded targets. |

### Why Platform-Native Over ThorVG Everywhere

| Concern | Platform-native | ThorVG everywhere |
|---------|----------------|-------------------|
| Performance | OS-optimized (SIMD, GPU) | Software-only rasterization |
| Anti-aliasing quality | Platform-native (matches system apps) | Generic AA |
| Binary size | No extra dependency on macOS/Windows | ~19k LoC on all platforms |
| Dependencies | macOS: already linked; Windows: COM (standard) | External lib everywhere |
| Maintenance | One backend per platform, clean separation | One code path, but carries workarounds |
| Pixel consistency | Minor differences across platforms | Identical output everywhere |

Pixel consistency across platforms is not a priority — Radiant is a document renderer, not a game engine. Platform-native AA and rendering quality is more important than cross-platform pixel matching.

---

## 3. Current Architecture and Its Problems

### 3.1 Current Render Pattern

For every shape Radiant draws (border segments, bullets, wavy underlines, rounded backgrounds, SVG elements):

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
- **`draw`** (calls `update` internally): Allocates `SwShapeTask`, computes transform, tessellates path into RLE, processes fill/stroke into renderer-allocated `RenderData`
- **`render`**: Rasterizes RLE spans to pixel buffer with compositing
- **`sync`**: Waits for task completion
- **`remove`**: Removes from Scene, calls `renderer->dispose()` to free `RenderData`, deletes the Paint

The Canvas/Scene overhead (status tracking, scene-graph insert/remove, dirty-region management) is pure waste for our immediate-mode use case.

### 3.2 The `reset_and_draw` Workaround

Radiant calls `tvg_swcanvas_set_target()` before every draw to reset ThorVG's dirty-region tracker. Without this, ThorVG clears previously-drawn content. This is a workaround for mismatched paradigms — ThorVG's incremental update system fights our one-shape-at-a-time pattern.

### 3.3 SVG Re-build Every Frame

For inline `<svg>` elements, `build_svg_scene()` constructs a full ThorVG scene tree. After one render, the entire tree is destroyed. On re-render, it's rebuilt from scratch.

---

## 4. Target Architecture

### 4.1 RdtVector — Platform-Abstracted Immediate-Mode API

```
┌────────────────────────────────────────────────────┐
│               Radiant View Tree                     │
│    DomElement → ViewBlock / ViewSpan / ViewText     │
├────────────────────────────────────────────────────┤
│          Radiant Render Dispatch                    │
│   render_block_view → render_inline_view → ...      │
│                                                     │
│   ┌──────────────┬────────────────┬───────────────┐ │
│   │ Text Glyphs  │ Vector Shapes  │  SVG Scenes   │ │
│   │ (bitmap blit)│ (rdt_vector)   │  (rdt_vector) │ │
│   └──────┬───────┴────────┬───────┴───────┬───────┘ │
├──────────┼────────────────┼───────────────┼────────┤
│          │        RdtVector API           │         │
│          │   fill_rect · fill_path        │         │
│          │   stroke_path · fill_gradient  │         │
│          │   push_clip · pop_clip         │         │
├──────────┼──────────┬─────────┬───────────┼────────┤
│          │  macOS   │ Windows │   Linux   │         │
│          │  Core    │ Direct  │   ThorVG  │         │
│          │  Graphics│ 2D      │ sw_engine │         │
├──────────┼──────────┴─────────┴───────────┼────────┤
│                   Pixel Buffer                      │
│            (ImageSurface, ABGR8888)                 │
└────────────────────────────────────────────────────┘
```

### 4.2 RdtVector API

A platform-agnostic immediate-mode vector rendering interface. Each platform backend implements this interface. No scene graph, no retained state.

```cpp
// radiant/rdt_vector.hpp

struct RdtVectorPath;  // platform-opaque path builder

struct RdtVector {
    // lifecycle — binds to a pixel buffer
    void init(uint32_t* pixels, int w, int h, int stride);
    void destroy();

    // path construction
    RdtVectorPath* path_new();
    void path_move_to(RdtVectorPath* p, float x, float y);
    void path_line_to(RdtVectorPath* p, float x, float y);
    void path_cubic_to(RdtVectorPath* p, float cx1, float cy1,
                       float cx2, float cy2, float x, float y);
    void path_close(RdtVectorPath* p);
    void path_add_rect(RdtVectorPath* p, float x, float y,
                       float w, float h, float rx, float ry);
    void path_add_circle(RdtVectorPath* p, float cx, float cy,
                         float rx, float ry);
    void path_free(RdtVectorPath* p);

    // fill
    void fill_path(RdtVectorPath* p, Color color,
                   const Matrix* transform = nullptr);
    void fill_rect(float x, float y, float w, float h, Color color,
                   float rx = 0, float ry = 0);

    // stroke
    void stroke_path(RdtVectorPath* p, Color color, float width,
                     StrokeJoin join, StrokeCap cap,
                     const float* dash_array = nullptr,
                     int dash_count = 0,
                     const Matrix* transform = nullptr);

    // gradient fill
    void fill_linear_gradient(RdtVectorPath* p,
                              float x1, float y1, float x2, float y2,
                              const GradientStop* stops, int stop_count,
                              const Matrix* transform = nullptr);
    void fill_radial_gradient(RdtVectorPath* p,
                              float cx, float cy, float r,
                              const GradientStop* stops, int stop_count,
                              const Matrix* transform = nullptr);

    // clipping
    void push_clip(RdtVectorPath* clip_path,
                   const Matrix* transform = nullptr);
    void pop_clip();

    // platform-private implementation data
    void* impl;
};
```

### 4.3 macOS Backend: Core Graphics

```cpp
// radiant/rdt_vector_cg.mm

struct RdtVectorCGImpl {
    CGContextRef ctx;           // bitmap context, owns no pixels
    CGColorSpaceRef colorspace;
};

struct RdtVectorPathCG : RdtVectorPath {
    CGMutablePathRef cg_path;
};

void RdtVector::init(uint32_t* pixels, int w, int h, int stride) {
    auto* cg = new RdtVectorCGImpl;
    cg->colorspace = CGColorSpaceCreateDeviceRGB();
    cg->ctx = CGBitmapContextCreate(pixels, w, h, 8, stride * 4,
        cg->colorspace, kCGImageAlphaPremultipliedLast);
    CGContextSetShouldAntialias(cg->ctx, true);
    impl = cg;
}

void RdtVector::fill_path(RdtVectorPath* p, Color color,
                           const Matrix* transform) {
    auto* cg = (RdtVectorCGImpl*)impl;
    auto* path = (RdtVectorPathCG*)p;
    CGContextSaveGState(cg->ctx);
    if (transform) {
        CGAffineTransform ct = { transform->e11, transform->e21,
                                 transform->e12, transform->e22,
                                 transform->e13, transform->e23 };
        CGContextConcatCTM(cg->ctx, ct);
    }
    CGContextSetRGBFillColor(cg->ctx,
        color.r / 255.0, color.g / 255.0,
        color.b / 255.0, color.a / 255.0);
    CGContextAddPath(cg->ctx, path->cg_path);
    CGContextFillPath(cg->ctx);
    CGContextRestoreGState(cg->ctx);
}
```

Key properties of the Core Graphics backend:
- `CGBitmapContextCreate` renders to Radiant's existing `ImageSurface` pixel buffer — no copy needed
- `CGContextSaveGState / RestoreGState` handles clip and transform stack
- Clipping via `CGContextClip()` after adding a clip path
- Gradients via `CGContextDrawLinearGradient` / `CGContextDrawRadialGradient`
- Already linked in the process (Radiant uses CoreText which is in the same framework)

### 4.4 Windows Backend: Direct2D

```cpp
// radiant/rdt_vector_d2d.cpp

struct RdtVectorD2DImpl {
    ID2D1Factory* factory;
    ID2D1RenderTarget* target;      // WIC bitmap render target
    IWICBitmap* wic_bitmap;         // wraps caller's pixel buffer
};

struct RdtVectorPathD2D : RdtVectorPath {
    ID2D1PathGeometry* geometry;
    ID2D1GeometrySink* sink;
    bool sink_closed;
};
```

Key properties:
- `IWICBitmap` wraps the existing pixel buffer; `ID2D1RenderTarget` renders into it
- GPU-accelerated when available, transparent software fallback
- `PushAxisAlignedClip` / `PushLayer` for clipping
- `ID2D1LinearGradientBrush` / `ID2D1RadialGradientBrush` for gradients

### 4.5 Linux Backend: ThorVG sw_engine (Extracted)

Copy ThorVG v1.0.3 sw_engine (~19k LoC) into `radiant/tvg/`. Use `SwRenderer` directly via C++ — no Canvas/Scene, no C API wrapper.

```cpp
// radiant/rdt_vector_tvg.cpp

struct RdtVectorTvgImpl {
    tvg::SwRenderer* renderer;
    // direct access to SwRenderer::prepare() and renderShape()
};
```

This is the only platform that carries ThorVG code. Details on extraction in Section 6.

### 4.6 Text and Font Ownership

All text and font handling remains entirely in Radiant across all platforms:

| Responsibility | Owner | Notes |
|----------------|-------|-------|
| Font discovery & config | `lib/font/font_config.c` | CoreText (macOS), fontconfig (Linux) |
| Glyph loading & caching | `lib/font/font_glyph.c` | Per-codepoint LoadedGlyph cache |
| Metrics (ascender, x-height, etc.) | `lib/font/font_tables.h` | Direct OpenType table parsing |
| Shaping & kerning | `lib/font/font_glyph.c` | kern/GPOS tables, CoreText advances |
| Bitmap rasterization (macOS) | CoreText `CTLineDraw` | Subpixel AA, hinting |
| Bitmap rasterization (Linux) | **via tvg SwRenderer** | Outline → RLE → grayscale bitmap |
| Bitmap rasterization (Windows) | DirectWrite | Replaces FreeType |
| Font fallback | `lib/font/font_fallback.c` | Codepoint-based chain |
| SVG `<text>` | `render_svg_inline.cpp` | Uses Radiant font system, not ThorVG Text |

### 4.7 SVG Inline Rendering

SVG inline rendering currently uses ThorVG's C API to build a scene tree from SVG DOM elements. Under the new architecture:

- **macOS/Windows**: `render_svg_inline.cpp` converts SVG elements directly to `RdtVector` calls (path construction + fill/stroke). No ThorVG scene tree needed.
- **Linux**: Same `RdtVector` API, backed by SwRenderer.
- **Cached rendering**: For complex SVGs, render to an offscreen bitmap and cache it on the `ViewBlock`. Invalidate on DOM mutation. This is simpler than caching a ThorVG scene tree and works across all backends.

```cpp
struct ViewBlock {
    // ... existing fields ...
    ImageSurface* cached_svg_surface;  // rendered bitmap cache
    bool svg_dirty;                     // invalidated on DOM mutation
};

void render_inline_svg(RenderContext* rdcon, ViewBlock* view) {
    if (!view->cached_svg_surface || view->svg_dirty) {
        // render SVG to offscreen surface via RdtVector
        view->cached_svg_surface = render_svg_to_surface(rdcon, view);
        view->svg_dirty = false;
    }
    // blit cached surface to main surface
    blit_surface(rdcon->ui_context->surface, view->cached_svg_surface,
                 rdcon->block.x + view->x, rdcon->block.y + view->y);
}
```

---

## 5. ThorVG Extraction (Linux Backend)

### 5.1 What to Extract from ThorVG v1.0.3

| Module | Lines | Include? | Notes |
|--------|-------|----------|-------|
| `common/` | ~2,100 | Yes | Math, color, string, array utilities |
| `renderer/` core | ~7,000 | Partial | Paint, Shape, Fill, Render — strip Canvas, Scene, Text, Saver, Animation |
| `renderer/sw_engine/` | ~9,500 | Yes | The software rasterizer — this is what we want |
| `renderer/gl_engine/` | ~8,600 | No | |
| `renderer/wg_engine/` | ~6,600 | No | |
| `loaders/svg/` | ~7,500 | No | Radiant has its own SVG-to-path converter |
| `loaders/lottie/` | ~161,000 | No | |
| `loaders/ttf/` | ~1,400 | No | |
| `loaders/png,jpg,...` | ~18,000 | No | |
| `bindings/capi/` | ~5,100 | No | |
| **Extracted total** | **~15,000** | | |

### 5.2 Why Copy Over Fork

1. We want ~15k of ~240k total lines. A fork carries 225k lines of unused code.
2. ThorVG is evolving toward Lottie/animation/WebGPU — irrelevant to Radiant.
3. The sw_engine is mature and stable. Core rasterization algorithms change rarely.
4. Only needed on Linux — not worth a full fork for one platform's backend.
5. MIT license permits copying, modification, and redistribution with attribution.

### 5.3 File Layout

```
radiant/
├── tvg/                             # extracted from ThorVG v1.0.3 (MIT)
│   ├── LICENSE                      # ThorVG MIT license notice
│   ├── common/                      # tvgMath, tvgColor, tvgStr, etc.
│   ├── renderer/                    # tvgPaint, tvgShape, tvgFill, tvgRender
│   │   └── sw_engine/               # SwRenderer, SwShape, SwRle, ...
│   └── README.md                    # provenance note
├── rdt_vector.hpp                   # platform-agnostic API (struct RdtVector)
├── rdt_vector_cg.mm                 # macOS backend (Core Graphics)
├── rdt_vector_d2d.cpp               # Windows backend (Direct2D)
├── rdt_vector_tvg.cpp               # Linux backend (extracted ThorVG)
├── render.cpp                       # uses RdtVector for all vector ops
├── render_border.cpp                # uses RdtVector
├── render_background.cpp            # uses RdtVector
├── render_svg_inline.cpp            # SVG → RdtVector calls + bitmap cache
└── ...
```

### 5.4 Build System

- `radiant/tvg/` sources compiled only on Linux (via `build_lambda_config.json` platform conditionals)
- macOS links `CoreGraphics.framework` (already linked via CoreText)
- Windows links `d2d1.lib`, `dwrite.lib`, `windowscodecs.lib`
- No separate library build — all compiled as part of Radiant

---

## 6. Migration Plan

### Phase 1: RdtVector API and macOS Backend

1. ✅ Define `RdtVector` interface in `rdt_vector.hpp`
2. ✅ Implement ThorVG backend in `rdt_vector_tvg.cpp`
3. ✅ Add `RdtVector` to `RenderContext` (removed `Tvg_Canvas`)
4. ✅ Migrate one call site (list bullet disc rendering) from ThorVG C API to `RdtVector`
5. ✅ Run radiant baseline tests — pixel-match confirmed
6. ✅ Progressively migrate remaining call sites:
   - ✅ `render_border.cpp` — border trapezoid shapes, rounded borders
   - ✅ `render_background.cpp` — rounded-rect fills, gradient backgrounds, clipped backgrounds
   - ✅ `render.cpp` — wavy underlines, column rules, markers, render_svg(), transforms (RdtMatrix)
   - ✅ `render_svg_inline.cpp` — `render_inline_svg()` uses rdt_ API (scene construction stays ThorVG internally)
   - ✅ `transform.hpp` — converted from `Tvg_Matrix` to `RdtMatrix`
7. ✅ `Tvg_Canvas` removed from `RenderContext`; all render files use `RdtVector`/`RdtMatrix`
8. ✅ Full baseline test pass (4005/4005 layout, 454/454 WPT)

### Phase 2: SVG Inline Rendering Rework

1. Replace `build_svg_scene()` (which builds ThorVG scene tree) with direct SVG-to-RdtVector path conversion
2. Implement SVG bitmap caching on `ViewBlock`
3. Add invalidation on DOM mutation
4. Test with inline SVG test cases

### Phase 3: Linux Backend

1. Extract ThorVG v1.0.3 sw_engine into `radiant/tvg/`
2. Strip unused modules, resolve `#include` dependencies
3. Implement `rdt_vector_tvg.cpp` wrapping `SwRenderer` directly (C++, no C API)
4. Run radiant baseline tests on Linux
5. Remove ThorVG external dependency from `setup-linux-deps.sh`

### Phase 4: Windows Backend

1. Implement `rdt_vector_d2d.cpp`
2. Run radiant baseline tests on Windows
3. Remove ThorVG external dependency from `setup-windows-deps.sh`

### Phase 5: Remove ThorVG External Dependency Entirely

1. Remove ThorVG from all `setup-*-deps.sh` scripts
2. Remove `thorvg_capi.h` includes
3. Remove ThorVG from `build_lambda_config.json` external libs
4. Clean up all `Tvg_Paint`, `Tvg_Canvas` type references
5. Full test pass on all platforms

### Phase 6: Linux Glyph Rasterization (Future)

1. Add OpenType `glyf`/`CFF` outline extraction to `lib/font/`
2. Convert outlines to `RdtVectorPath`
3. Rasterize via `RdtVector::fill_path()` to grayscale bitmap
4. Integrate into `font_load_glyph()` as the non-macOS path
5. Remove FreeType dependency

---

## 7. Risks and Mitigations

| Risk | Impact | Mitigation |
|------|--------|------------|
| Platform rendering differences | Low — document renderer, not pixel-exact requirement | Baseline tests per platform; visual review |
| Three backends to maintain | Medium | Shared API surface is small (~15 functions); backends are thin wrappers |
| Core Graphics pixel format mismatch | Low | `CGBitmapContextCreate` supports RGBA8888; may need swizzle to match ABGR8888 |
| Direct2D COM complexity on Windows | Low | Well-documented API; many reference implementations available |
| Upstream ThorVG sw_engine fixes missed (Linux) | Low — stable code | Periodic review of ThorVG releases |
| Linux glyph quality without FreeType hinting | Medium | ThorVG's AA rasterizer is adequate; can add grid-fitting later |

---

## 8. Summary

| Aspect | Current | Proposed |
|--------|---------|----------|
| Vector backend (macOS) | ThorVG v1.0-pre34 (external, C API) | Core Graphics (native, already linked) |
| Vector backend (Windows) | ThorVG v1.0-pre34 (external, C API) | Direct2D (native, GPU-accelerated) |
| Vector backend (Linux) | ThorVG v1.0-pre34 (external, C API) | ThorVG v1.0.3 sw_engine (extracted, C++ direct) |
| API layer | `tvg_canvas_push / draw / remove` (retained-mode misuse) | `RdtVector` (immediate-mode, platform-native) |
| Scene management | ThorVG Canvas/Scene | None (immediate-mode everywhere) |
| SVG inline | Build ThorVG scene tree per frame | `RdtVector` path calls + bitmap cache |
| Text rendering | Split (ThorVG Text for SVG, Radiant for HTML) | Radiant only (all text, all platforms) |
| Font handling | Split (ThorVG TTF + Radiant) | Radiant only |
| External dependency | Full ThorVG (~240k LoC, all platforms) | None on macOS/Windows; ~15k LoC extracted on Linux |

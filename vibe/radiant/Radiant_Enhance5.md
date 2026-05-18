# Radiant Rendering Enhancement Proposal

Date: 2026-05-18

## Executive Summary

Radiant rendering already has several strong foundations: `RenderContext` carries the current paint state, `RdtVector` abstracts vector backends, `DisplayList` separates recording from replay for the main raster path, and `render_walk.cpp` gives SVG/PDF a shared tree walker. The current problem is not lack of capability. The problem is that core rendering behavior is concentrated in a few large files, especially `render.cpp` and `render_svg_inline.cpp`, where traversal, state management, clipping, effects, text painting, display-list recording, profiling, and output tiling are interleaved.

This proposal restructures rendering around small, named subsystems:

- a painter API for drawing operations
- scoped render state helpers
- shared geometry, path, clip, and effect helpers
- a context-owned render profiler
- a cleaner display-list builder/replay split
- a shared paint walker usable by screen, SVG, and PDF backends
- output-target orchestration for screen, PNG, and tiled PNG rendering

The goal is an incremental extraction, not a rewrite. Each phase should preserve output first, then improve performance by giving dirty-region culling, effect bounds, display-list replay, and text painting clearer ownership.

## Implementation Update: 2026-05-18

The first rendering extraction passes have been completed for the direct raster and feature-module paint paths.

Completed:

- Added a shared raster paint context and raster API in `view.hpp` and `surface.cpp`:
  - `RasterPaintContext`
  - `raster_fill_rect()`
  - `raster_blit_surface_scaled()`
  - `raster_blit_pixels_scaled()`
- Moved the shared raster API into dedicated raster files:
  - `render_raster.hpp`
  - `render_raster.cpp`
- Kept legacy `fill_surface_rect()` and `blit_surface_scaled()` as compatibility wrappers that delegate to the shared raster API.
- Added shared painter helpers and moved them into dedicated painter files:
  - `render_painter.hpp`
  - `render_painter.cpp`
  - `render_painter_draw_picture_rect()`
  - `render_painter_draw_pixels_rect()`
  - `render_painter_fill_surface_rect()`
  - `render_painter_blit_surface_scaled()`
- Moved the `rc_*` display-list/direct-vector dispatch gateway out of `render.hpp` and into `render_painter.cpp`, while keeping all existing call signatures available through `render_painter.hpp`.
- Gave `RenderContext` a struct tag so painter declarations can forward-declare the context cleanly.
- Refactored `render_background.cpp` and `render_border.cpp` to use the shared painter helpers for direct surface fills, image tiles, and SVG picture tiles.
- Refactored replaced-content image rendering in `render.cpp` so SVG pictures, SVG raster fallbacks, normal raster images, webview layers, and surface clears use the shared vector/raster helpers.
- Refactored video frame blitting in `render_video.cpp` to use `raster_blit_pixels_scaled()` instead of a local scaling loop.
- Removed an unused duplicate poster-image scaler from `render_video.cpp`.
- Cleaned `surface.cpp` around the raster split by replacing local `fprintf()` diagnostics with `log_error()`, removing an unused temporary path variable, and adding a missing allocation-null check for `image_surface_create()`.
- Updated `DisplayList` direct-pixel commands so fill/blit replay receives the current rectangular clip and clip-shape stack.
- Updated tiled replay in `tile_pool.cpp` so direct-pixel fill/blit replay restores clip shapes and translates shape coordinates into tile-local space.
- Preserved blit opacity through display-list recording and replay.
- Added arena-owned polygon vertex copies for display-list raster clip stacks, plus per-tile scratch copies with tile-local coordinate translation during tiled replay.
- Added shared clip helper files:
  - `render_clip.hpp`
  - `render_clip.cpp`
- Moved CSS clip-shape parsing, clip-shape scratch cleanup, clip-shape-to-path conversion, and per-corner rounded-rect path construction out of `render.cpp`.
- Made `clip_shape.h` include its own `string.h` dependency for `memset()` instead of relying on transitive includes from larger render files.
- Added initial render-state helper files:
  - `render_state.hpp`
  - `render_state.cpp`
- Moved transform save/apply/restore and transform matrix concatenation out of `render_block_view()` into `render_state_push_transform()` and `render_state_pop_transform()`.

Validation:

- `git diff --check` passed.
- `make build` passed with 0 errors.
- `make layout suite=baseline` passed with 4149 successful tests and 6 skipped tests.

The earlier raster-facade gap has been closed: `surface.cpp` now keeps the surface ownership, image loading, and compatibility wrappers, while render-facing fill/blit/scaling behavior lives in `render_raster.cpp`.

This means the practical Phase 1 painter/raster consolidation is now largely complete, and the first shared clip/path plus render-state helper extractions have started. The next cleanup step is to continue extracting shared geometry and additional state scopes so feature modules stop carrying their own small variants of the same math and save/restore logic.

## Current Structure Assessment

### What Is Working Well

- `RenderContext` centralizes the active vector target, UI surface, scale, dirty region, transform, clip state, scratch arena, and optional `DisplayList`.
- The `rc_*` helpers in `render.hpp` already provide a narrow drawing gateway that can record into a display list or draw directly.
- `DisplayList` is a useful command buffer with record/replay support, tile replay hooks, clip commands, effect commands, and element begin/end markers.
- `RdtVector` keeps rendering backend details out of most feature code.
- `render_background.cpp`, `render_border.cpp`, `render_filter.cpp`, `render_img.cpp`, `render_form.cpp`, `render_pdf.cpp`, `render_svg.cpp`, and `render_video.cpp` show the right direction: feature-specific rendering modules exist.
- `render_walk.cpp` and `RenderBackend` already prove that a shared traversal layer can serve PDF and SVG output.
- Existing render profiling counters are useful enough to identify hotspots in glyph loading, text painting, block rendering, effects, clipping, opacity, blend, and display-list replay.

### Where The Structure Is Uneven

- `render.cpp` is doing too much. It owns profiling globals, thread-count setup, clip-shape parsing, rounded-rect path creation, glyph rasterization, text selection, text decorations, list markers, column rules, block traversal, transforms, clip-path, overflow clipping, opacity groups, mix-blend, filters, image dispatch, display-list recording, display-list replay, tiled PNG output, and UI overlays.
- `render_svg_inline.cpp` is another large mixed-responsibility module. SVG parsing helpers, inherited style state, transform handling, definitions, gradients, patterns, text, images, and painting all live together.
- The main screen renderer and the PDF/SVG renderers do not share the same paint walker. `render_walk.cpp` explicitly excludes the raster backend because the raster path has extra concerns, but those concerns can be modeled as painter/effect/output-target capabilities instead of requiring a separate traversal forever.
- Geometry helpers are duplicated or near-duplicated across rendering modules. Examples include transform lookup, per-corner rounded rect path creation, background/border/content paint rect adjustment, clip path construction, physical pixel conversion, and effect-region expansion.
- Paint state save/restore is mostly manual. `render_block_view()` has many local saved values and cleanup branches for transform, clip-path, overflow clip, filter backdrop, opacity backdrop, and mix-blend backdrop.
- Effects are not a first-class subsystem. Opacity, filter, blend, shadow, and backdrop save/composite operations are scattered across `render.cpp`, `render_background.cpp`, `render_filter.cpp`, and `display_list.cpp`.
- Text painting combines too many stages in one function: run walking, font setup, glyph loading, glyph drawing, selection background, composition bounds, text shadow, skip-ink gap collection, text decorations, and profiling.
- Display-list recording, storage, resource ownership, command bounds, and replay are tightly coupled. Some operations have precise bounds, while paths/effects often fall back to coarse bounds, limiting dirty-region and tiled replay efficiency.
- Direct-pixel operations and vector operations are mixed at call sites. Selection fills, glyph blits, image blits, opacity, blend, and filter paths need consistent clipping and transform behavior.
- `render_html_doc()` and `render_html_doc_tiled()` duplicate document render setup but differ in output strategy. This makes output modes harder to reason about and test.

## Proposed Module Boundaries

### 1. Painter API

Create:

- `radiant/render_painter.hpp`
- `radiant/render_painter.cpp`

Move the `rc_*` drawing gateway out of `render.hpp` into a named painter layer. The painter should be the only feature-facing API for paint commands.

Proposed responsibilities:

- route commands to `DisplayList` or direct `RdtVector`
- consistently apply active transform
- consistently pass surface clip and clip-shape stack to direct-pixel operations
- expose a compact API for feature modules
- hide display-list details from background, border, text, image, form, and SVG code

Sketch:

```cpp
typedef struct RenderPainter {
    RenderContext* context;
    RdtVector* vec;
    DisplayList* display_list;
} RenderPainter;

void render_painter_fill_rect(RenderPainter* painter, Rect rect, Color color);
void render_painter_fill_path(RenderPainter* painter, RdtPath* path, Color color, RdtFillRule rule);
void render_painter_stroke_path(RenderPainter* painter, RdtPath* path, Color color, float width,
                                RdtStrokeCap cap, RdtStrokeJoin join);
void render_painter_draw_image(RenderPainter* painter, const uint32_t* pixels, int src_w, int src_h,
                               int src_stride, Rect dst, uint8_t opacity);
void render_painter_fill_surface_rect(RenderPainter* painter, ImageSurface* surface, Rect rect,
                                      uint32_t color);
```

Initial migration can keep the old `rc_*` names as inline compatibility wrappers that delegate to the painter. Once callers are moved, `render.hpp` becomes a context declaration rather than a paint-command dumping ground.

### 2. Render State Scopes

Create:

- `radiant/render_state.hpp`
- `radiant/render_state.cpp`

Render state changes should be paired by construction. This avoids the fragile manual cleanup currently spread through `render_block_view()` and related paths.

Proposed scopes:

- font/color scope
- block coordinate scope
- transform scope
- vector clip scope
- clip-shape scope
- overflow clip scope
- effect group scope
- display-list element marker scope

Sketch:

```cpp
typedef struct RenderTransformScope {
    RenderContext* context;
    bool active;
    bool prev_has_transform;
    RdtMatrix prev_transform;
} RenderTransformScope;

RenderTransformScope render_push_transform(RenderContext* rdcon, ViewBlock* block,
                                           float abs_x, float abs_y);
void render_pop_transform(RenderTransformScope* scope);

typedef struct RenderClipScope {
    RenderContext* context;
    int saved_vector_depth;
    int saved_shape_depth;
    bool active;
} RenderClipScope;

RenderClipScope render_push_overflow_clip(RenderContext* rdcon, ViewBlock* block,
                                          float abs_x, float abs_y);
void render_pop_clip(RenderClipScope* scope);
```

This does not require exceptions or C++ destructors. Plain begin/end structs match the existing C+ style and make cleanup auditable.

### 3. Render Geometry Helpers

Create:

- `radiant/render_geometry.hpp`
- `radiant/render_geometry.cpp`

Consolidate geometry math that is currently repeated across background, border, block rendering, effects, and display-list replay.

Proposed helpers:

- CSS pixel to physical pixel conversion
- physical region clamp/intersection
- `Bound`/`Rect` conversion
- transform-origin resolution
- transformed bounds estimation
- background-origin/background-clip rect adjustment
- border-box/padding-box/content-box paint rects
- effect expansion for blur, drop-shadow, and filter chains

Sketch:

```cpp
Rect render_box_rect(ViewBlock* block, CssEnum box, float abs_x, float abs_y);
Rect render_background_origin_rect(ViewBlock* block, BackgroundLayer* layer,
                                   float abs_x, float abs_y);
Rect render_background_clip_rect(ViewBlock* block, BackgroundLayer* layer,
                                 float abs_x, float abs_y);
Bound render_rect_to_physical_bound(Rect rect, float scale);
Bound render_clamp_bound_to_surface(Bound bound, ImageSurface* surface);
Rect render_filter_effect_rect(ViewBlock* block, float abs_x, float abs_y);
```

This mirrors the layout-side push to centralize box math. Rendering needs the same discipline because background, border, clip, filter, and dirty-region math all depend on the same boxes.

### 4. Render Path Helpers

Create:

- `radiant/render_path.hpp`
- `radiant/render_path.cpp`

Move path construction out of feature modules.

Proposed ownership:

- per-corner rounded rect path creation
- rounded background clip path
- rounded border clip path
- CSS clip-shape path conversion
- border side trapezoid paths
- inline fragment background paths
- path clone/free wrappers for display-list use

Current candidates to move include:

- `create_per_corner_rounded_rect_path()`
- `create_clip_shape_path()`
- background clip path helpers
- border clip path helpers
- repeated top/right/bottom/left side path construction in border rendering

### 5. Render Clip Helpers

Create:

- `radiant/render_clip.hpp`
- `radiant/render_clip.cpp`

Clipping spans vector clips, pixel clip-shape stacks, overflow clips, CSS `clip-path`, and display-list clip save/restore. These should become one subsystem.

Proposed responsibilities:

- parse and allocate CSS clip shapes
- push vector path clip and matching pixel clip shape together
- restore clip depth consistently for direct and display-list paths
- convert rounded overflow clips into both vector paths and pixel clip shapes
- expose clip containment queries used by glyph/image/surface fast paths

Sketch:

```cpp
typedef struct RenderClipScope RenderClipScope;

RenderClipScope render_clip_push_css_clip_path(RenderContext* rdcon, ViewBlock* block,
                                               float abs_x, float abs_y);
RenderClipScope render_clip_push_overflow(RenderContext* rdcon, ViewBlock* block,
                                          float abs_x, float abs_y);
void render_clip_pop(RenderClipScope* scope);
```

### 6. Render Effects

Create:

- `radiant/render_effects.hpp`
- `radiant/render_effects.cpp`

Effects should be planned and applied as groups instead of being hand-coded inside block traversal.

Proposed effect group:

```cpp
typedef struct RenderEffectGroup {
    RenderContext* context;
    Bound region;
    bool has_filter_backdrop;
    bool has_opacity_group;
    bool has_mix_blend;
    float opacity;
    CssEnum blend_mode;
} RenderEffectGroup;

RenderEffectGroup render_effect_group_begin(RenderContext* rdcon, ViewBlock* block,
                                            float abs_x, float abs_y);
void render_effect_group_end(RenderEffectGroup* group, ViewBlock* block);
```

This module should own:

- opacity backdrop save/composite
- filter backdrop save/composite
- mix-blend backdrop save/apply
- effect region expansion and clamping
- display-list effect command emission
- direct-surface fallback implementation

`display_list.cpp` can keep replay implementations, but effect planning should not live in `render_block_view()`.

### 7. Text And Glyph Rendering

Create:

- `radiant/render_text.hpp`
- `radiant/render_text.cpp`
- `radiant/render_glyph.hpp`
- `radiant/render_glyph.cpp`

Split `render_text_view()` into stages.

Proposed stages:

- build paint runs from `ViewText`
- resolve font and color state
- paint selection/background fragments
- load/raster glyphs
- draw glyphs
- collect skip-ink gaps
- paint text decorations
- paint text shadows

Sketch:

```cpp
typedef struct TextPaintRun {
    ViewText* view;
    FontBox font;
    Color color;
    Rect visual_rect;
    int start_offset;
    int end_offset;
} TextPaintRun;

void render_text_view(RenderContext* rdcon, ViewText* text_view);
void render_text_paint_run(RenderContext* rdcon, TextPaintRun* run);
void render_text_decorations(RenderContext* rdcon, TextPaintRun* run);
```

Performance opportunities after this split:

- cache per-font setup and metrics used by rendering
- reuse glyph bitmaps across repeated display-list recording
- cache text paint run bounds for dirty-region rejection
- separate selection/caret overlays so caret changes do not force glyph display-list rebuilds

### 8. Render Profiler

Create:

- `radiant/render_profiler.hpp`
- `radiant/render_profiler.cpp`

Move global profiling counters out of `render.cpp` and into a context-owned profiler.

Proposed responsibilities:

- scoped timers for block, inline, text, glyph load, glyph draw, image, SVG, filter, clip, opacity, blend, dispatch, children, overflow, and display-list replay
- counter increments through named helpers
- log output through one reporting function
- optional event-state JSON emission
- thread-safe aggregation for tiled or parallel replay paths

Sketch:

```cpp
typedef enum {
    RENDER_PROFILE_BLOCK,
    RENDER_PROFILE_TEXT,
    RENDER_PROFILE_GLYPH_LOAD,
    RENDER_PROFILE_GLYPH_DRAW,
    RENDER_PROFILE_FILTER,
    RENDER_PROFILE_CLIP,
    RENDER_PROFILE_OPACITY,
    RENDER_PROFILE_BLEND,
    RENDER_PROFILE_DISPLAY_REPLAY,
} RenderProfileZone;

typedef struct RenderProfiler RenderProfiler;

void render_profiler_reset(RenderProfiler* profiler);
void render_profiler_add_time(RenderProfiler* profiler, RenderProfileZone zone, double ms);
void render_profiler_increment(RenderProfiler* profiler, RenderProfileZone zone);
void render_profiler_log(RenderProfiler* profiler);
void render_profiler_emit_event(RenderProfiler* profiler);
```

The current stderr render summary can remain during migration, but it should be emitted by the profiler rather than by ad hoc globals.

### 9. Display List Cleanup

Split display-list concerns:

- `radiant/display_list.h` stays the public command buffer API
- `radiant/display_list_builder.cpp` owns recording helpers
- `radiant/display_list_replay.cpp` owns replay and tiled replay
- `radiant/display_list_storage.cpp` owns allocation, cloning, and cleanup
- `radiant/display_list_bounds.cpp` owns command bounds

The current single `display_list.cpp` combines these responsibilities. Splitting it will make dirty-region and retained-subtree work easier.

Add or clarify:

- precise bounds for path, gradient, picture, clip, shadow, and filter commands
- explicit resource ownership for borrowed image/glyph buffers versus owned paths/pictures
- command validation in debug builds
- one helper for copying dash arrays and gradient stops
- retained subtree reuse around existing `DL_BEGIN_ELEMENT` and `DL_END_ELEMENT`

Sketch:

```cpp
typedef struct DisplayResourceRef {
    void* ptr;
    int kind;
    bool owned_by_display_list;
} DisplayResourceRef;

Bound dl_item_bounds(DisplayItem* item);
bool dl_item_intersects_dirty(DisplayItem* item, Bound dirty);
```

### 10. Output Target Orchestration

Create:

- `radiant/render_output.hpp`
- `radiant/render_output.cpp`

Unify the setup currently split across `render_html_doc()` and `render_html_doc_tiled()`.

Proposed concepts:

- `RenderOutputTarget`: screen surface, PNG file, tiled PNG, PDF, SVG
- `RenderRecordPlan`: direct draw or display-list record
- `RenderReplayPlan`: single-thread replay, tile replay, direct tile render
- common initialization and cleanup for `RenderContext`
- common dirty-region handling
- common profiler reporting

Sketch:

```cpp
typedef enum {
    RENDER_OUTPUT_SCREEN,
    RENDER_OUTPUT_PNG,
    RENDER_OUTPUT_TILED_PNG,
    RENDER_OUTPUT_PDF,
    RENDER_OUTPUT_SVG,
} RenderOutputKind;

typedef struct RenderOutputTarget {
    RenderOutputKind kind;
    ImageSurface* surface;
    const char* output_path;
    int tile_height;
} RenderOutputTarget;

void render_document_to_target(UiContext* uicon, ViewTree* view_tree,
                               RenderOutputTarget* target);
```

The first phase can preserve the existing code paths internally while moving shared setup into helpers.

### 11. Shared Paint Walker

Evolve:

- `radiant/render_walk.cpp`
- `radiant/render_backend.h`

Into:

- `radiant/render_tree_walk.hpp`
- `radiant/render_tree_walk.cpp`
- `radiant/render_backend.hpp`

The existing `RenderBackend` contract is a good start, but it is too sparse for the screen renderer because it lacks dirty culling, clip, effects, forms, video, webview, scrollbars, and display-list markers.

Proposal:

- keep PDF/SVG callbacks working
- add optional capability callbacks for effects, clipping, form controls, replaced content, and overlays
- let the raster backend use the same walker through `RenderPainter`
- keep backend-specific output details in callbacks

This gives one tree order for all output formats and makes rendering parity testable.

### 12. Inline SVG Refactor

Split `render_svg_inline.cpp` into focused modules:

- `radiant/svg/svg_parse.cpp`: length, color, transform, viewBox parsing
- `radiant/svg/svg_style.cpp`: inherited style, inline style, stylesheet rules
- `radiant/svg/svg_defs.cpp`: gradients, patterns, clip paths, markers, references
- `radiant/svg/svg_geometry.cpp`: viewport, preserveAspectRatio, transform composition
- `radiant/svg/svg_paint.cpp`: shape/text/image rendering
- `radiant/svg/svg_render.cpp`: public `render_svg_to_vec()` orchestration

Keep `render_svg_inline.hpp` as the public API during migration. The goal is to reduce the current single-file surface area and make SVG feature additions less likely to perturb unrelated behavior.

## ThorVG And Native API Reuse

Radiant should reuse ThorVG and OS native APIs where they give correct CSS semantics, but they should sit behind explicit backend capability checks. The current codebase already has the right abstraction seed: all normal vector drawing goes through `RdtVector`, and `rdt_vector_tvg.cpp` isolates ThorVG calls. There is also a macOS `rdt_vector_cg.mm` implementation, but the generated mac build currently uses `rdt_vector_tvg.cpp` and excludes `rdt_vector_cg.mm`, so Core Graphics should be treated as experimental until the backend contract and tests are completed.

### Current Backend Usage

- ThorVG is already used for path fill/stroke, rectangles, rounded rectangles, linear/radial gradients, vector clipping masks, raw image drawing, SVG picture drawing, inline SVG text/image bridging, SVG rasterization, and Lottie rendering.
- The ThorVG wrapper currently behaves like an immediate-mode renderer: each shape is pushed, drawn, synced, then removed. This keeps behavior simple, but it limits batching and makes clip masks expensive when many shapes are drawn inside the same clip.
- The raster renderer still uses software pixel paths for many operations: surface fills, raster image scaling, glyph blitting, selection/caret fills, CSS filters, box shadow blur, opacity compositing, mix-blend compositing, background-blend compositing, video frame blits, and webview layer blits.
- The Core Graphics backend implements basic paths, fills, strokes, gradients, clips, and image drawing, but it has unresolved semantic gaps: premultiplied-alpha drawing into Radiant's straight-alpha ABGR surface, missing or incomplete parity for tile offsets, clip depth save/restore, SVG DOM pictures, picture duplication, and the same display-list replay semantics as ThorVG.

### Backend Capability Layer

Create:

- `radiant/render_backend_caps.hpp`
- `radiant/render_backend_caps.cpp`

The renderer should ask what a backend can do instead of assuming ThorVG or Core Graphics behavior directly.

Sketch:

```cpp
typedef struct RenderBackendCaps {
    bool vector_paths;
    bool rounded_rects;
    bool gradients;
    bool nested_clips;
    bool image_scaling;
    bool picture_svg;
    bool opacity_group;
    bool blend_modes;
    bool gaussian_blur;
    bool color_matrix_filters;
    bool native_text_runs;
    bool premultiplied_surface;
    bool tile_offsets;
} RenderBackendCaps;

RenderBackendCaps render_backend_get_caps(RdtVector* vec);
```

This lets the painter choose a native path only when the backend supports the exact operation. Otherwise it falls back to the software reference implementation.

### ThorVG Reuse Opportunities

ThorVG should remain the default cross-platform vector backend. The best near-term reuse opportunities are:

- Batch vector replay into ThorVG scenes. Instead of push/draw/sync/remove per operation, display-list replay can build a ThorVG scene for consecutive vector-only commands, draw once, and flush before direct-pixel effects. This should reduce backend overhead without changing CSS semantics.
- Reuse ThorVG picture and scene opacity for simple vector-only opacity groups. This is safe only when the group has no direct-pixel commands, no CSS filter, no mix-blend-mode, and no video/webview placeholders.
- Route more raster image draws through `rdt_draw_image()` when the image does not need software-only behavior such as repeat wrapping, custom area-averaging, or clip-shape pixel masking. This gives ThorVG a chance to use its optimized image scaling path.
- Keep SVG and Lottie through ThorVG. These are already natural fits and should be preserved behind `RdtPicture` and the Lottie player.
- Add a retained ThorVG resource cache for static paths, repeated gradients, SVG pictures, and image paints, keyed by display-list resource identity. This should come after display-list ownership is clarified.

### Native API Reuse Opportunities

Native APIs should be optional backend implementations, not feature-specific calls scattered through rendering code.

macOS:

- Finish `rdt_vector_cg.mm` as a real `RdtVector` backend only after it implements the full `rdt_vector.hpp` contract.
- Use Core Graphics for paths, gradients, clipping, and image scaling behind `RdtVector`.
- Consider Accelerate/vImage or Core Image for blur and color-matrix filters behind `render_effects`, with the current software filter path kept as the reference implementation.
- Consider Core Text only behind a future `RenderTextBackend`. The current glyph cache and font backend are important for deterministic tests, so native text drawing should not be mixed into `render_text_view()` directly.

Windows:

- A future Direct2D backend can map naturally to paths, rounded rectangles, gradients, clips, image drawing, opacity layers, and many effects.
- DirectWrite can be considered for native text runs only through the same `RenderTextBackend` abstraction.
- WIC can remain an image decode/encode helper, but scaling should still be selected through painter/backend capabilities.

Linux:

- ThorVG should stay the portable default unless a well-scoped native backend is introduced.
- If a native path is added, prefer a complete backend implementation over one-off calls from feature modules.

### Software Paths To Keep As Reference

Some operations should remain software-first until backend parity is proven:

- CSS `mix-blend-mode` and `background-blend-mode`, because they depend on exact backdrop isolation and CSS blend formulas.
- CSS filter chains, especially `drop-shadow()`, because the current implementation depends on element alpha extraction, backdrop handling, and filter order.
- Box shadows and inset shadows with border radius, spread, clip-path interaction, and tile replay.
- Glyph blitting, selection, caret, and text decoration rendering until a text backend abstraction can reproduce current metrics and dirty-region behavior.
- Webview and video compositing, because they are post-composite layer operations.
- Software raster image scaling for repeat wrapping and browser-matching downscale behavior until native scaling is verified fixture-by-fixture.

### Backend Reuse Acceptance Criteria

- No rendering module calls ThorVG, Core Graphics, Direct2D, Core Image, vImage, or Core Text directly except through a backend-specific implementation file.
- `RdtVector` has one complete active backend per build target.
- Native backends pass the same rendering fixtures as ThorVG before they become default.
- Any native filter/effect path has a software reference fallback and a pixel-diff test.
- Backend choice is explicit in generated build configuration; do not select by accidentally compiling two `RdtVector` implementations or by editing generated `.lua` files.
- Premultiplied-alpha versus straight-alpha behavior is documented and tested before Core Graphics is enabled for general rendering.

## Performance Plan

### Phase 1: Behavior-Preserving Extraction

- Move `rc_*` helpers into `render_painter`.
- Move duplicated geometry/path helpers into `render_geometry` and `render_path`.
- Move profiling globals into `RenderProfiler`.
- Replace manual clip/transform save/restore in a few narrow call sites with explicit scope structs.
- Keep visual output unchanged.

Expected benefit: code becomes auditable, and future performance changes become safer.

### Phase 2: Better Dirty Bounds

- Add precise `DisplayItem` bounds for path, gradient, clip, filter, shadow, picture, and glyph commands.
- Use item bounds during replay instead of relying on broad page or subtree regions.
- Make effect-region expansion use `render_geometry` helpers so filters and shadows do not over-invalidate.
- Add tests for dirty caret/selection updates, transformed elements, and clipped elements.

Expected benefit: smaller replay regions and less full-surface work during interactive editing.

### Phase 3: Text Fast Path

- Separate stable glyph drawing from volatile overlays like caret and selection.
- Cache font setup and glyph metrics by `FontBox`.
- Cache text paint run bounds.
- Record glyph runs into display list independently from selection fills when possible.

Expected benefit: editing and caret movement avoid unnecessary glyph work.

### Phase 4: Effect Group Consolidation

- Use one effect group implementation for direct rendering and display-list recording.
- Clamp effect regions once.
- Reuse scratch allocation patterns for backdrop saves.
- Avoid repeated surface copies for nested opacity/filter/blend groups when their regions are empty or fully clipped.

Expected benefit: fewer temporary buffers and less duplicated pixel compositing code.

### Phase 5: Retained Display-List Subtrees

- Use existing `DL_BEGIN_ELEMENT` and `DL_END_ELEMENT` markers to cache unchanged element subtrees.
- Attach retained display-list fragments to stable view identity or dirty tracker keys.
- Re-record only dirty subtrees and volatile overlays.
- Keep borrowed resource lifetime explicit before enabling broad retained reuse.

Expected benefit: large static documents avoid full display-list rebuild on small UI changes.

### Phase 6: Backend Parity

- Move the raster path onto the shared paint walker.
- Keep PDF/SVG backend callbacks for output-specific differences.
- Add parity tests for tree order, opacity wrappers, transforms, images, text, and inline SVG.

Expected benefit: screen, PDF, SVG, and PNG output become easier to keep consistent.

## Proposed Regression Fixtures

Add focused rendering fixtures instead of relying only on broad visual smoke tests:

- opacity group with child background and text
- filter drop-shadow with transparent element
- mix-blend-mode over parent background
- overflow hidden with border-radius clipping text and image
- CSS `clip-path: inset()` clipping background, border, text, and image
- CSS `clip-path: circle()` with transformed child
- transformed background and border with dirty-region repaint
- inline background across wrapped text fragments with border radius
- text-shadow blur combined with underline skip-ink
- high-DPI raster image with `object-fit`
- embedded inline SVG with gradient, transform, and text
- dirty caret repaint inside a text editor block
- selection repaint does not erase parent background
- tiled PNG output matches non-tiled output for the same document
- PDF/SVG output preserve the same paint order for background, border, text, image, and inline SVG

Add display-list unit coverage for:

- command allocation and cleanup
- path ownership
- gradient stop copying
- dash array copying
- clip save/restore depth
- command bounds
- dirty-region intersection
- backdrop save and opacity composite
- filter command replay

Performance comparisons should use release builds, not debug builds.

## Acceptance Criteria

- `render.cpp` no longer owns profiling, painter dispatch, shared geometry, shared path creation, clip-shape parsing, effect-group planning, and output-target orchestration.
- `render_svg_inline.cpp` is split so parse/style/defs/geometry/paint are separately testable.
- Feature modules draw through `RenderPainter` or backend callbacks, not directly through scattered `rdt_*` calls.
- Clip, transform, opacity, filter, and blend state changes use named scope/helper functions.
- Background, border, filter, dirty-region, and tiled output code use the same geometry helpers for paint rects and effect bounds.
- Display-list items have documented ownership and bounds.
- The profiler is owned by `RenderContext` or an explicit render job, not global mutable counters.
- The first extraction phase produces no intentional visual output changes.
- New rendering fixtures cover the previously implicit critical cases.

## Suggested Implementation Order

1. Extract `render_profiler` from `render.cpp`.
2. Extract `render_geometry` and replace background/border/effect rect adjustment call sites.
3. Extract `render_path` and replace duplicated rounded-rect, clip-shape, and border path helpers.
4. Extract `render_painter`, keeping compatibility wrappers for existing `rc_*` calls.
5. Add backend capability reporting for the active `RdtVector` implementation.
6. Add `render_clip` scope helpers and migrate CSS clip-path plus overflow clipping.
7. Add `render_effects` and migrate opacity, filter, and mix-blend planning out of `render_block_view()`.
8. Split text painting into text/glyph/decorations helpers.
9. Split display-list storage, builder, replay, and bounds.
10. Unify `render_html_doc()` and `render_html_doc_tiled()` setup through `render_output`.
11. Expand `render_walk` into the shared paint walker and migrate raster rendering to it.
12. Split `render_svg_inline.cpp` into SVG parse/style/defs/geometry/paint modules.

This order keeps high-risk behavior changes late. The early phases make the code smaller and easier to inspect, while preserving the current rendering model.

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
- Added `RenderClipScope` helpers and moved CSS `clip-path` plus overflow rounded-rect clip push/pop logic out of `render_block_view()` into `render_clip.cpp`.
- Made `clip_shape.h` include its own `string.h` dependency for `memset()` instead of relying on transitive includes from larger render files.
- Added initial render-state helper files:
  - `render_state.hpp`
  - `render_state.cpp`
- Moved transform save/apply/restore and transform matrix concatenation out of `render_block_view()` into `render_state_push_transform()` and `render_state_pop_transform()`.
- Added initial render-geometry helper files:
  - `render_geometry.hpp`
  - `render_geometry.cpp`
- Moved background-origin/background-clip box adjustment, clip/rect intersection, and clipped pixel-bound conversion into the shared geometry layer.
- Added initial render-path helper files:
  - `render_path.hpp`
  - `render_path.cpp`
- Moved shared rounded-rect path construction and render-context clip path construction out of background/border feature modules.
- Added initial render-profiler helper files:
  - `render_profiler.hpp`
  - `render_profiler.cpp`
- Moved render profiling counter storage, reset, log-summary output, stderr profiling output, and `render.stats` event-log JSON emission out of `render.cpp`.
- Removed direct profiler `fprintf()` calls from `render.cpp`; stderr profiling output now goes through `render_profiler`.
- Added a `RenderProfiler*` to `RenderContext`, made each top-level render/tile render own its profiler instance, and removed the temporary process-global profiler getter.
- Replaced `render.cpp` compatibility counter macros with named profiler zones and helper calls:
  - `render_profiler_increment()`
  - `render_profiler_add_time()`
  - `render_profiler_add_sample()`
- Updated clip-shape rounded-rect path creation to use the shared path builder.
- Added initial render-composite helper files:
  - `render_composite.hpp`
  - `render_composite.cpp`
- Moved CSS blend-mode pixel compositing, backdrop copy/clear, premultiplied source-over compositing, and opacity group compositing into the shared composite layer.
- Updated background blend, block opacity/filter/mix-blend, display-list replay, and tiled replay to use the shared composite helpers.
- Added initial render-effects helper files:
  - `render_effects.hpp`
  - `render_effects.cpp`
- Moved shared effect backdrop save/clear and final opacity/source-over/blend application out of `render_block_view()` into the effects layer.
- Added `RenderEffectGroup` and moved opacity, filter, and mix-blend effect detection plus filter/backdrop region planning out of `render_block_view()`.
- Moved CSS filter application dispatch into `render_effect_group_apply_filter()`, so `render_block_view()` no longer directly calls display-list filter commands or software filter fallback.
- Added `render_effect_group_finish()` so filter, opacity, blend finishing and their profiler samples are owned by `render_effects` instead of `render_block_view()`.
- Added initial glyph rendering helper files:
  - `render_glyph.hpp`
  - `render_glyph.cpp`
- Moved glyph bitmap compositing, color-glyph scaling/blending, and glyph pixel sampling out of `render.cpp`.
- Added initial text rendering helper files:
  - `render_text.hpp`
  - `render_text.cpp`
- Moved text-decoration rendering and skip-ink gap collection out of `render_text_view()` into `render_text_decorations()`.
- Moved inline-fragment background painting and trailing soft-hyphen/ellipsis painting into `render_text`.
- Moved text-shadow painting, shadow-region blur dispatch, emoji-presentation detection, and profiled glyph-load dispatch into `render_text`.
- Added initial render-output helper files:
  - `render_output.hpp`
  - `render_output.cpp`
- Moved render-context initialization/cleanup, canvas background resolution, full/selective surface clear, root view-tree paint dispatch, and PNG/JPEG surface-save dispatch into `render_output`.
- Moved display-list replay planning and render-pool ownership out of `render.cpp` into `render_output_replay_display_list()`, including the single-thread versus tiled replay choice.
- Moved the normal HTML document render orchestration out of `render.cpp` into `render_output_render_html_doc()`, leaving `render_html_doc()` as a thin public wrapper.
- Moved large-output tiled PNG streaming out of `render.cpp` into `render_output_render_tiled_png()`, keeping `render_html_doc_tiled()` as a thin public entrypoint wrapper.
- Added initial UI overlay helper files:
  - `render_overlay.hpp`
  - `render_overlay.cpp`
- Moved focus outline, caret, canonical selection highlight, dropdown/context-menu overlay dispatch, and drag/drop overlay painting out of `render.cpp`.
- Added initial render-selection helper files:
  - `render_selection.hpp`
  - `render_selection.cpp`
- Moved cross-view selection ordering and image-selection membership checks out of `render.cpp`.
- Added initial column rendering helper files:
  - `render_columns.hpp`
  - `render_columns.cpp`
- Moved multi-column rule painting out of `render.cpp`.
- Added initial list/marker rendering helper files:
  - `render_list.hpp`
  - `render_list.cpp`
- Moved marker painting and list traversal helpers out of `render.cpp`.
- Removed obsolete commented-out list-number formatting code from `render.cpp`.
- Added initial vector-path rendering helper files:
  - `render_vector_path.hpp`
  - `render_vector_path.cpp`
- Moved PDF/vector path segment painting out of `render.cpp`.
- Added initial media rendering helper files:
  - `render_media.hpp`
  - `render_media.cpp`
- Moved SVG rasterization, replaced image content painting, image view wrapping, video placeholder recording, and webview layer placeholder/direct-blit painting out of `render.cpp`.
- Moved the public text rendering orchestration (`render_text_view()`) out of `render.cpp` and into `render_text.cpp`, keeping text run walking next to the text-specific glyph, shadow, decoration, and inline-background helpers.
- Restored saved font/color state before returning from the zero-font-size text path.
- Added initial display-list bounds helper file:
  - `display_list_bounds.hpp`
  - `display_list_bounds.cpp`
- Moved the tile replay item-intersection helper behind the public display-list bounds API.
- Added initial display-list storage helper file:
  - `display_list_storage.hpp`
  - `display_list_storage.cpp`
- Moved display-list item-array allocation/growth, arena-owned stop/dash/clip-shape copying, clip-shape restoration, initialization, cleanup, owned-resource release, and simple list queries out of `display_list.cpp`.
- Added initial display-list glyph replay helper files:
  - `display_list_replay_glyph.hpp`
  - `display_list_replay_glyph.cpp`
- Moved display-list glyph sampling, transformed glyph replay, grayscale glyph replay, and color-emoji replay out of `display_list.cpp`.
- Added initial display-list replay state helper files:
  - `display_list_replay_state.hpp`
  - `display_list_replay_state.cpp`
- Moved selective-repaint dirty clip union calculation, vector dirty clip push/pop, and replay-bound tightening helpers out of `display_list.cpp`.
- Added initial display-list backdrop replay helper files:
  - `display_list_replay_backdrop.hpp`
  - `display_list_replay_backdrop.cpp`
- Moved replay backdrop stack management for CSS opacity and blend-mode out of `display_list.cpp`.
- Added initial display-list shadow replay helper files:
  - `display_list_replay_shadow.hpp`
  - `display_list_replay_shadow.cpp`
- Moved replay shadow clip save/restore state out of `display_list.cpp`.
- Added initial display-list effect replay helper files:
  - `display_list_replay_effects.hpp`
  - `display_list_replay_effects.cpp`
- Moved replay opacity, CSS filter dispatch, box blur, inset blur, and outer-shadow replay out of `display_list.cpp`.
- Added initial display-list raster replay helper files:
  - `display_list_replay_raster.hpp`
  - `display_list_replay_raster.cpp`
- Moved replay of direct surface fills, scaled surface blits, and webview layer placeholders out of `display_list.cpp`.
- Added `display_list_replay.hpp` and `display_list_replay.cpp`.
- Moved the top-level display-list replay dispatcher out of `display_list.cpp`, leaving `display_list.cpp` as a compatibility placeholder while storage, bounds, recording, and replay now live in focused modules.
- Moved replay and bounds public declarations out of `display_list.h` into focused headers, and updated replay/tile/retained callers to include the narrower API.
- Added initial display-list raster recording helper file:
  - `display_list_record_raster.cpp`
- Moved direct surface fill/blit recording and external video/webview placeholder recording out of `display_list.cpp`.
- Added initial display-list effect recording helper file:
  - `display_list_record_effects.cpp`
- Moved opacity, backdrop, blend, filter, blur, shadow-clip, and outer-shadow command recording out of `display_list.cpp`.
- Added initial display-list vector recording helper file:
  - `display_list_record_vector.cpp`
- Moved vector draw command recording, glyph bounds calculation, image/picture recording, and clip-depth command recording out of `display_list.cpp`.
- Added `test_display_list_gtest.cpp` with focused coverage for the display-list refactor:
  - bounds/intersection behavior for replay-state commands
  - element marker layout bounds versus visual union bounds
  - vector path, stroke dash, gradient stop, image, glyph, and picture recording metadata
  - raster clip/clip-shape copies and external layer generations
  - effect-region bounds for filters, inset blur, and outer shadows
  - `dl_clear()` scratch rewind and list reuse
- Added initial backend capability helpers:
  - `render_backend_caps.hpp`
  - `render_backend_caps.cpp`
- Added immutable `RdtVectorCaps` reporting for the active vector backend, with ThorVG and experimental Core Graphics capability tables.
- Added retained ThorVG/Rdt picture resource caching in `rdt_vector_tvg.cpp` for parsed SVG file and SVG data pictures. Calls to `rdt_picture_load()` and `rdt_picture_load_data()` now return shallow duplicates of immutable cached SVG DOM pictures, avoiding repeated SVG parsing while preserving per-call size/transform mutation and caller-owned handles.
- Added an `RdtVector` batching API and ThorVG scene batching for display-list replay. Consecutive vector/image/picture replay commands now accumulate into one ThorVG scene and flush before software pixel operations such as glyph blits, filters, opacity/backdrop compositing, shadows, and webview layers.
- Extended the experimental Core Graphics backend to satisfy the expanded `RdtVector` entrypoint set with explicit unsupported fallbacks for SVG DOM pictures, engine/font lifecycle hooks, clip-depth save/restore, and batching.
- Added backend capability predicates for native blur and color-matrix filter chains, plus a shared `render_filter_apply_with_backend()` gateway. Direct effect rendering, display-list replay, and tiled replay now route CSS filters through one backend-aware entrypoint before falling back to the software reference path.
- Moved the repeated current-transform lookup into `render_state_current_transform()` and updated background/border drawing paths to use it.
- Removed stale local `RdtVector*` variables from background and border paths that now draw through the painter/context gateway.

Validation:

- `git diff --check` passed.
- `make build` passed with 0 errors.
- `make layout suite=baseline` passed with 4149 successful tests and 6 skipped tests.

The earlier raster-facade gap has been closed: `surface.cpp` now keeps the surface ownership, image loading, and compatibility wrappers, while render-facing fill/blit/scaling behavior lives in `render_raster.cpp`.

This means the practical Phase 1 painter/raster/effect consolidation is now largely complete, and the first shared clip/path, geometry, render-state, profiler, composite, effects, glyph, text, selection, column rendering, list/marker rendering, vector-path rendering, media rendering, output, display-list bounds/storage/recording/replay, and backend capability helper extractions are in place. The next cleanup step is to continue splitting text paint-run internals, display-list replay dispatch, and output-target orchestration.

## Implementation Update: 2026-05-19

The retained display-list work has moved from replay-only subtree skipping to a conservative cross-frame fragment cache.

Completed:

- Added `retained_display_list.hpp` and `retained_display_list.cpp`.
- Added a document-owned `RetainedDisplayListCache` in `DocState`.
- Capture now stores matched `DL_BEGIN_ELEMENT` / `DL_END_ELEMENT` ranges by stable `view_id` after each display-list recording pass.
- Cached fragments deep-copy display-list-owned resources:
  - paths
  - pictures
  - gradient stops
  - dash arrays
  - polygon clip-shape stacks
- Borrowed resources remain explicit borrowed references:
  - image pixel buffers
  - glyph bitmap buffers
  - filter/style pointers
  - video/webview surfaces
- `dl_clear()` now releases the display-list scratch arena, so repeated retained capture does not accumulate copied stops, dashes, or clip polygons.
- Added append rollback for retained fragments: failed resource cloning removes only the partially appended items and preserves the caller's existing display list.
- Dirty rects can now carry a `source_view_id`; merged dirty rects only keep that id when all merged sources match.
- `render_block_try_retained_fragment()` can reuse a cached fragment when:
  - the render is a selective dirty repaint
  - the cached marker bounds still match the current block marker bounds
  - every intersecting dirty rect has a known source view id
  - no intersecting dirty source is inside the reused subtree
  - transformed content keeps stable marker bounds and uses the cached visual union for dirty intersection
- Image, video, and webview layer paths attempt retained reuse before re-recording their block/payload marker range.
- Added resource-generation invalidation for borrowed retained payloads:
  - `ImageSurface::generation` now increments when image pixels are decoded, SVG fallback pixels are rasterized, GIF/Lottie animation frames swap or clear their pixel buffers, and webview layer snapshots refresh their surface.
  - Display-list image, scaled-surface blit, video placeholder, and webview placeholder commands record the generation observed at capture time.
  - Retained fragment reuse now rejects stale borrowed image/surface/video/webview payloads before appending the cached fragment.
  - Glyph bitmap buffers now record the font glyph-cache generation observed at capture time. Retained reuse accepts glyph-backed text fragments while the generation matches and rejects them after the glyph arena is reset.
- Transformed subtree reuse is less conservative:
  - Element markers now preserve the original layout marker bounds separately from the visual union bounds used for replay/dirty culling.
  - Retained reuse can compare stable layout marker bounds while using cached visual bounds for dirty-region intersection, so transformed fragments are no longer rejected solely because their recorded visual bounds differ from untransformed layout bounds.
- Added `test_retained_display_list_gtest.cpp` plus a small test-only stub file so retained fragment capture/append and dirty-source retained replay decisions can be tested without linking the full ThorVG/SVG stack.
- Added the retained display-list gtest to `build_lambda_config.json` and regenerated the generated Premake files through `make build-test`.
- Extended retained display-list tests to cover stale borrowed image pixels, scaled surface blits, video generations, webview surface generations, borrowed glyph buffers, marker-bounds mismatch, unknown dirty-source rejection, dirty-source-inside-subtree rejection, external dirty-source reuse, dirty misses, and transformed visual bounds with stable marker bounds.

Validation:

- `make build-test` passed.
- `./test/test_retained_display_list_gtest.exe` passed.

Remaining limits:

- Reuse is intentionally conservative and skips full repaints, unknown dirty sources, and any dirty source inside the subtree.
- The cache is currently keyed by view id and bounds, plus generation checks for borrowed image/surface/video/webview payloads.
- Volatile text fragments are now generation-checked: glyph-backed fragments can be reused while the font glyph cache generation matches, but caret/selection/preedit overlay integration still needs full UI repaint fixtures before broad text-subtree reuse should be relaxed further.

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

- `render.cpp` is still doing too much. It owns block traversal, transform/effect orchestration call sites, and parts of display-list recording, even though profiling, clip/path helpers, text rendering, selection helpers, column-rule painting, list/marker rendering, vector-path rendering, media rendering, output orchestration, display-list storage/replay slices, and UI overlays have been extracted.
- `render_svg_inline.cpp` is another large mixed-responsibility module. SVG parsing helpers, inherited style state, transform handling, definitions, gradients, patterns, text, images, and painting all live together.
- The main screen renderer and the PDF/SVG renderers do not share the same paint walker. `render_walk.cpp` explicitly excludes the raster backend because the raster path has extra concerns, but those concerns can be modeled as painter/effect/output-target capabilities instead of requiring a separate traversal forever.
- Geometry helpers are duplicated or near-duplicated across rendering modules. Examples include transform lookup, per-corner rounded rect path creation, background/border/content paint rect adjustment, clip path construction, physical pixel conversion, and effect-region expansion.
- Paint state save/restore is mostly manual. `render_block_view()` has many local saved values and cleanup branches for transform, clip-path, overflow clip, filter backdrop, opacity backdrop, and mix-blend backdrop.
- Effects are not a first-class subsystem. Opacity, filter, blend, shadow, and backdrop save/composite operations are scattered across `render.cpp`, `render_background.cpp`, `render_filter.cpp`, and `display_list.cpp`.
- Text painting now lives in `render_text.cpp`, but it still combines too many stages in one function: run walking, font setup, glyph loading, glyph drawing, selection background, composition bounds, text shadow, skip-ink gap collection, text decorations, and profiling.
- Display-list recording, storage, resource ownership, command bounds, and replay are still spread across several files, but the highest-risk pieces now have explicit homes: storage/lifecycle, bounds, record slices, replay slices, retained fragment capture/reuse, and borrowed resource-generation validation. Broad retained reuse still needs more validation around volatile overlays.
- Direct-pixel operations and vector operations are mixed at call sites. Selection fills, glyph blits, image blits, opacity, blend, and filter paths need consistent clipping and transform behavior.
- `render_html_doc()` and `render_html_doc_tiled()` are now thin public wrappers, but normal/tiled output behavior still needs a clearer target abstraction before adding PDF/SVG/screen-specific orchestration.

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

RenderClipScope render_clip_push_overflow_scope(RenderContext* rdcon);
void render_clip_pop_scope(RenderContext* rdcon, RenderClipScope* scope);
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

RenderClipScope render_clip_push_css_scope(RenderContext* rdcon, ViewBlock* block,
                                           float parent_x, float parent_y, float scale);
RenderClipScope render_clip_push_overflow_scope(RenderContext* rdcon);
void render_clip_pop_scope(RenderContext* rdcon, RenderClipScope* scope);
```

### 6. Render Effects

Create:

- `radiant/render_effects.hpp`
- `radiant/render_effects.cpp`

Initial helper extraction is now in place. `render_effects` owns bounded backdrop save/clear plus final source-over, opacity, and blend application for both display-list and direct-surface paths.

`RenderEffectGroup` now centralizes element effect detection and region planning, including:

- opacity group detection
- mix-blend detection
- filter rect expansion
- filter backdrop expansion and clamping
- filter command dispatch for display-list and direct-surface rendering

Current effect group:

```cpp
typedef struct RenderEffectGroup {
    RenderEffectBackdrop mix_blend_backdrop;
    RenderEffectBackdrop opacity_backdrop;
    RenderEffectBackdrop filter_backdrop;
    Rect filter_rect;
    CssEnum mix_blend_mode;
    float opacity;
    bool has_filter_backdrop;
    bool has_opacity_group;
    bool has_filter;
} RenderEffectGroup;

RenderEffectGroup render_effect_group_begin(RenderContext* rdcon, ViewBlock* block,
                                            const BlockBlot* parent_block);
bool render_effect_group_apply_filter(RenderEffectGroup* group,
                                      ViewBlock* block,
                                      Bound* clip);
```

The separate filter/opacity/blend finish calls have now been collapsed into `render_effect_group_finish()`, including profiler sampling for each effect kind.

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

Initial text/glyph extraction is now in place: `render_glyph` owns `draw_glyph()`,
color glyph scaling/blending, and glyph pixel sampling. `render_text` owns
inline-fragment backgrounds, trailing soft-hyphen/ellipsis marks,
text-decoration rendering, skip-ink gap collection, text-shadow painting, and
emoji-aware glyph loading. `render_text_view()` still owns run walking,
font/color setup, and selection logic.

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

Created:

- `radiant/render_profiler.hpp`
- `radiant/render_profiler.cpp`

Profiler ownership is now render-job scoped: top-level render paths create a
`RenderProfiler`, pass it through `RenderContext`, and use named profiler zones
instead of compatibility counter macros.

Proposed responsibilities:

- scoped timers for block, inline, text, glyph load, glyph draw, image, SVG, filter, clip, opacity, blend, dispatch, children, overflow, and display-list replay
- counter increments through named helpers
- log output through one reporting function
- optional event-state JSON emission
- thread-safe aggregation for tiled or parallel replay paths

Completed responsibilities:

- reset and log-summary output
- stderr counter and replay summaries for `--no-log` profiling
- `render.stats` event-log JSON emission
- render-job scoped profiler lifetime through `RenderContext`
- named increment/add-time/sample helpers for paint-phase counters

Remaining responsibilities:

- scoped timers for the main paint phases
- thread-safe aggregation for future tiled replay counters

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

The current stderr render summary remains available during migration and is now emitted by the profiler instead of ad hoc code in `render.cpp`.

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

Unify the setup currently split across `render_html_doc()`, `render_html_doc_tiled()`,
and the headless file exporters.

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
    RENDER_OUTPUT_JPEG,
    RENDER_OUTPUT_TILED_PNG,
    RENDER_OUTPUT_PDF,
    RENDER_OUTPUT_SVG,
} RenderOutputKind;

typedef struct RenderOutputTarget {
    RenderOutputKind kind;
    const char* output_file;
    ImageSurface* surface;
    int width;
    int height;
    int viewport_width;
    int viewport_height;
    int jpeg_quality;
    float scale;
    float pixel_ratio;
} RenderOutputTarget;

int render_output_render_view_tree_to_target(UiContext* uicon, ViewTree* view_tree,
                                             RenderOutputTarget* target);
int render_output_render_html_file_to_target(const char* html_file,
                                             RenderOutputTarget* target);
```

Implemented state:

- `RenderOutputTarget` now covers screen, PNG, JPEG, tiled PNG, PDF, and SVG.
- screen/PNG/JPEG/tiled PNG view-tree rendering routes through
  `render_output_render_view_tree_to_target()`.
- file-level PDF/SVG/PNG/JPEG orchestration is represented by
  `render_output_render_html_file_to_target()`, which delegates to the existing
  exporters while keeping target selection and output metadata centralized.
- the CLI render command now routes supported file outputs through the unified
  `render_html_to_output_target()` compatibility entrypoint.
- `render_html_doc()` remains a thin compatibility wrapper that creates a screen
  or raster-file target.

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
- Parsed SVG file/data pictures are now retained behind `RdtPicture`; repeated loads reuse immutable cached SVG DOM resources and return per-call shallow duplicates.
- The ThorVG wrapper still supports immediate-mode calls, but display-list replay now uses `rdt_vector_begin_batch()` / `rdt_vector_flush_batch()` / `rdt_vector_end_batch()` to submit consecutive vector paints as one ThorVG scene. Software pixel operations remain explicit flush points.
- CSS filters now enter through a backend-aware filter gateway. The active ThorVG and Core Graphics capability tables currently report no native blur/color-matrix support, so the gateway preserves the existing software reference implementation.
- The raster renderer still uses software pixel paths for many operations: surface fills, raster image scaling, glyph blitting, selection/caret fills, CSS filter fallback, box shadow blur, opacity compositing, mix-blend compositing, background-blend compositing, video frame blits, and webview layer blits.
- The Core Graphics backend implements basic paths, fills, strokes, gradients, clips, image drawing, raster picture duplication, transform metadata, and no-op/fallback lifecycle hooks for the full `RdtVector` entrypoint set. It remains experimental because premultiplied-alpha drawing into Radiant's straight-alpha ABGR surface, real tile offset replay, SVG DOM pictures, native batching, and display-list replay parity still need fixture coverage before it can be enabled.

### Backend Capability Layer

Created:

- `radiant/render_backend_caps.hpp`
- `radiant/render_backend_caps.cpp`

The renderer can now ask what a backend can do instead of assuming ThorVG or Core Graphics behavior directly. `RdtVector` exposes immutable backend capability metadata, and `render_backend_caps` provides the render-facing wrapper.

Filter dispatch now uses the same capability layer. `render_backend_supports_filter_chain()` checks whether a full CSS filter chain can be represented by native blur/color-matrix primitives, and `render_filter_apply_with_backend()` is the single entrypoint used by direct rendering, display-list replay, and tiled replay. Until a backend implements the native branch, it falls back to `apply_css_filters()`.

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
    bool vector_batching;
    bool premultiplied_surface;
    bool tile_offsets;
} RenderBackendCaps;

RenderBackendCaps render_backend_get_caps(RdtVector* vec);
```

This lets the painter choose a native path only when the backend supports the exact operation. Otherwise it falls back to the software reference implementation.

### ThorVG Reuse Opportunities

ThorVG should remain the default cross-platform vector backend. The best near-term reuse opportunities are:

- Broaden ThorVG scene batching beyond display-list replay where it is safe, especially direct render paths that emit long runs of vector-only commands before any software pixel readback.
- Reuse ThorVG picture and scene opacity for simple vector-only opacity groups. This is safe only when the group has no direct-pixel commands, no CSS filter, no mix-blend-mode, and no video/webview placeholders.
- Route more raster image draws through `rdt_draw_image()` when the image does not need software-only behavior such as repeat wrapping, custom area-averaging, or clip-shape pixel masking. This gives ThorVG a chance to use its optimized image scaling path.
- Keep SVG and Lottie through ThorVG. These are already natural fits and should be preserved behind `RdtPicture` and the Lottie player.
- Continue retained ThorVG resource caching beyond the completed SVG picture cache: static paths, repeated gradients, and image paints should be keyed by display-list resource identity after ownership and invalidation rules are explicit.

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
- Move profiling globals into `RenderProfiler`. Done: counters are owned by render-job scoped profiler instances passed through `RenderContext`.
- Replace manual clip/transform save/restore in a few narrow call sites with explicit scope structs. Done for transforms, CSS `clip-path`, and overflow rounded clips.
- Keep visual output unchanged.

Expected benefit: code becomes auditable, and future performance changes become safer.

### Phase 2: Better Dirty Bounds

- Add precise `DisplayItem` bounds for path, gradient, clip, filter, shadow, picture, and glyph commands. Done: path/gradient/clip bounds now come from `RdtPath` geometry, transformed images and pictures use transformed destination bounds, glyph and filter bounds are clipped to their recorded rectangular clip, and stateful zero-bound commands are preserved separately from drawable empty bounds.
- Use item bounds during replay instead of relying on broad page or subtree regions. Done for dirty replay and tile replay culling; skipped items still maintain clip/backdrop stack state.
- Make effect-region expansion use `render_geometry` helpers so filters and shadows do not over-invalidate. Done: filter group bounds and block dirty-marker bounds share `render_geometry` overflow helpers.
- Record matched `DL_BEGIN_ELEMENT` / `DL_END_ELEMENT` markers around block subtrees and compute marker bounds from the union of child display-list item bounds. Done: dirty replay and tile replay can now jump over a non-intersecting matched subtree.
- Keep replaced/layer payloads inside element marker bounds. Done for image, video, and webview-layer render paths by recording one outer marker around the block paint plus payload command.
- Add tests for dirty caret/selection updates, transformed elements, and clipped elements. Still needed as focused regression fixtures; current verification is build plus render smoke coverage.

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

- Use existing `DL_BEGIN_ELEMENT` and `DL_END_ELEMENT` markers to cache unchanged element subtrees. Done for conservative reuse: markers are recorded, matched, bounded by subtree command union, captured into `RetainedDisplayListCache`, and used for dirty/tile replay skipping plus cross-frame fragment append.
- Attach retained display-list fragments to stable view identity or dirty tracker keys. Done initially by stable view id, with dirty rects carrying optional `source_view_id` so reuse can reject dirty sources inside the subtree.
- Re-record only dirty subtrees and volatile overlays. Started: reuse is enabled only when intersecting dirty sources are known and outside the subtree; volatile/unknown/full-repaint cases fall back to normal recording.
- Keep borrowed resource lifetime explicit before enabling broad retained reuse. Started: owned display-list payloads are deep-copied, image/surface/video/webview payloads carry generation checks, and glyph/filter payloads remain documented borrowed references with conservative reuse gates.

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
- retained marker capture and append with matching begin/end indices. Started with `test_retained_display_list_gtest.cpp`.

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

1. Extract `render_profiler` from `render.cpp`. Done, including render-job scoped lifetime and named counter helpers.
2. Extract `render_geometry` and replace background/border/effect rect adjustment call sites. Done for background, border, and common clipped pixel bounds.
3. Extract `render_path` and replace duplicated rounded-rect, clip-shape, and border path helpers. Done for shared rounded-rect and clip-path creation.
4. Extract `render_painter`, keeping compatibility wrappers for existing `rc_*` calls. Done for the shared painter gateway and feature-module callers.
5. Add backend capability reporting for the active `RdtVector` implementation. Done with `RdtVectorCaps` and `render_backend_get_caps()`.
6. Add `render_clip` scope helpers and migrate CSS clip-path plus overflow clipping. Done.
7. Continue `render_effects` by moving profiling hooks out of `render_block_view()` and collapsing effect finish calls into a scoped end helper. Done.
8. Split text painting and smaller feature paint paths into named helpers. Started with glyph bitmap rendering in `render_glyph`, inline background, trailing mark, text-decoration, text-shadow, profiled glyph-load helpers, and `render_text_view()` ownership in `render_text`, cross-view selection predicates in `render_selection`, column-rule painting in `render_columns`, marker/list rendering in `render_list`, vector path painting in `render_vector_path`, and image/video/webview payload painting in `render_media`.
9. Split display-list storage, builder, replay, and bounds. Started with public display-list bounds helpers used by tile replay, a storage/lifecycle module, glyph replay helpers, replay dirty-clip state helpers, backdrop replay helpers, shadow clip replay helpers, effect replay helpers, direct raster replay helpers, top-level replay dispatch, direct raster recording helpers, effect recording helpers, and vector recording helpers.
   - Update: dirty replay now culls individual bounded commands, matched element markers carry subtree-union bounds, and dirty/tile replay can skip entire non-intersecting element subtrees.
   - Update: true cross-frame retained fragment reuse is enabled conservatively through `RetainedDisplayListCache`. Cached fragments deep-copy owned payloads and are reused only when bounds match and intersecting dirty sources are known to be outside the subtree.
10. Unify `render_html_doc()` and `render_html_doc_tiled()` setup through `render_output`. Done for the current raster pipeline: shared context lifecycle, background/clear handling, root paint dispatch, display-list replay planning, render-pool ownership, surface-save dispatch, normal document render orchestration, tiled PNG streaming, overlay dispatch, and screen/PNG/JPEG/tiled target dispatch are now outside `render.cpp`. File-level PDF/SVG targets are represented by the same `RenderOutputTarget` abstraction and currently delegate to the existing exporters; the CLI render command uses the unified file-target entrypoint.
11. Expand `render_walk` into the shared paint walker and migrate raster rendering to it.
12. Split `render_svg_inline.cpp` into SVG parse/style/defs/geometry/paint modules.

This order keeps high-risk behavior changes late. The early phases make the code smaller and easier to inspect, while preserving the current rendering model.

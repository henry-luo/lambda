# Radiant Render Paths

Date: 2026-05-22

## Purpose

Radiant rendering now has several overlapping paths: screen and PNG rendering record and replay a display list, large PNG export can render by strips, PDF and SVG export use a shared tree walker with backend callbacks, and inline SVG has its own SVG-to-vector bridge. This note documents those paths and proposes a more unified design.

The practical problem is familiar from recent bugs: a rendering behavior gets fixed in one path, but a parallel path still has the old behavior. Examples include SVG filter replay versus tiled replay, currentColor SVG icons, rounded clips, border-box SVG sizing, image/background clipping, and premultiplied alpha. The long-term fix is to make target differences live at a smaller backend boundary, not in separate paint implementations.

## Current Render Targets

`RenderOutputKind` currently defines six output target kinds:

- `RENDER_OUTPUT_SCREEN`
- `RENDER_OUTPUT_PNG`
- `RENDER_OUTPUT_JPEG`
- `RENDER_OUTPUT_TILED_PNG`
- `RENDER_OUTPUT_PDF`
- `RENDER_OUTPUT_SVG`

The targets do not map one-to-one to implementation paths.

### 1. Raster Screen, PNG, And JPEG

Main files:

- `render_output.cpp`
- `render_raster_walk.cpp`
- `render.cpp`
- `render_block.cpp`
- `display_list_*.cpp`
- `tile_pool.cpp`

Current flow:

1. `render_output_render_view_tree_to_target()` selects the raster target for screen, PNG, or JPEG.
2. `render_output_render_raster_target()` initializes `RenderContext`.
3. `render_output_clear_surface()` clears the full surface or dirty regions.
4. A `DisplayList` is created and assigned to `rdcon.dl`.
5. `render_output_render_view_tree()` calls `render_raster_view_tree()`.
6. `render_raster_walk.cpp` uses the shared `render_walk.cpp` traversal with raster override callbacks.
7. Raster callbacks call the rich raster helpers: block, text, background, border, form, image, video, webview, inline SVG, scroller, overlays.
8. Painter calls route through `rc_*` helpers. Because `rdcon.dl` is set, they record display-list commands instead of final pixels.
9. UI overlays are recorded after main content.
10. Retained display-list cache captures reusable fragments.
11. `render_output_replay_display_list()` replays the display list to the actual `ImageSurface`.
12. PNG/JPEG targets save the surface to file.

This is the most complete path and should be treated as the reference implementation for visual behavior.

### 2. Raster Display-List Replay

Main files:

- `display_list_replay.cpp`
- `display_list_replay_*.cpp`
- `tile_pool.cpp`

Replay has two variants:

- Single-surface replay through `dl_replay()`.
- Tiled parallel replay through `tile_grid_init()`, render pool jobs, and `tile_grid_composite()`.

The replay mode is selected in `render_output_replay_display_list()`.

The tiled replay path is used when:

- `RADIANT_RENDER_THREADS` enables worker threads.
- There is a non-empty display list.
- No dirty replay is active.
- The display list does not contain glyph commands.

Otherwise replay uses the single-surface path.

This split is useful for performance, but it is a recurring source of bugs. Any command added to the display list must have equivalent behavior in:

- item bounds calculation,
- single-surface replay,
- tiled replay,
- dirty replay,
- retained fragment validation.

### 3. Large Tiled PNG Export

Main files:

- `render_output.cpp`

`render_output_render_tiled_png()` streams a large output image as horizontal strips. It creates a tile surface, adjusts the render clip and vector tile offset, renders the view tree into that tile, and writes rows to libpng.

This is different from display-list tiled replay. It reruns view-tree rendering per strip and currently does not share the same record-once, replay-many pipeline used by normal raster output.

This path is memory-efficient, but because it is not just a target wrapper around the same display-list replay, it can diverge from screen/PNG behavior.

### 4. SVG Export

Main files:

- `render_svg.cpp`
- `render_walk.cpp`
- `render_backend.h`

Current flow:

1. `render_html_to_svg()` loads and lays out the document.
2. `render_view_tree_to_svg()` builds an SVG backend.
3. `render_walk.cpp` traverses the view tree.
4. `render_svg.cpp` backend callbacks emit SVG markup.

This path does not replay the raster display list. It has its own implementation of bounds, paint order, text, image, clipping, opacity, transform, and inline SVG passthrough.

### 5. PDF Export

Main files:

- `render_pdf.cpp`
- `render_walk.cpp`
- `render_backend.h`

Current flow is parallel to SVG export:

1. `render_html_to_pdf()` loads and lays out the document.
2. `render_view_tree_to_pdf()` builds a PDF backend.
3. `render_walk.cpp` traverses the view tree.
4. PDF backend callbacks emit PDF drawing commands.

The PDF path shares traversal with SVG, but not all paint semantics with raster display-list replay.

### 6. Inline SVG And SVG Pictures

Main files:

- `render_svg_inline.cpp`
- `rdt_vector_tvg.cpp`
- `rdt_vector_cg.mm`

Inline SVG is a subpath used by multiple targets.

Current flow:

1. HTML layout creates a view for `<svg>`.
2. Raster rendering calls `render_inline_svg()`.
3. `render_inline_svg()` computes the SVG content viewport, sets clipping, and calls `render_svg_to_vec()`.
4. `render_svg_to_vec()` walks the SVG element tree and emits `RdtVector` operations.

When called during raster display-list recording, the vector operations become display-list commands. When called from picture/native contexts, they may draw through `RdtVector` directly.

Because inline SVG has its own parser/style/filter/paint logic, it can diverge from both CSS painting and exported SVG/PDF behavior unless all final drawing goes through the same painter/display-list command model.

## Display-List Design

The display-list pipeline has several distinct phases.

### Phase 1: Record

During normal raster rendering, `rdcon.dl` points to an active `DisplayList`.

The normal render helpers still run, but painter gateway calls like `rc_fill_rect()`, `rc_draw_path()`, `rc_draw_image()`, `rc_push_clip()`, and effect helpers record `DL_*` commands instead of drawing final pixels.

Record-time responsibilities:

- traverse the view tree in paint order,
- resolve the current render state,
- emit display-list commands,
- copy borrowed command payloads that must outlive the immediate call,
- compute item-level metadata such as resource generations and element markers,
- mark retainable subtree boundaries.

### Phase 2: Overlay Record

After the main view-tree record pass, UI overlays are recorded:

- focus outline,
- caret,
- selection highlight,
- dropdown and context-menu overlay,
- drag/drop overlay.

These overlays are part of the frame output but conceptually sit above document content. They also have their own dirty behavior because caret and selection often change without document layout changes.

### Phase 3: Retained Fragment Capture

After recording, `retained_dl_cache_capture()` stores retainable fragments. The cache is keyed by element identity plus resource and style/layout validation data.

Capture-time responsibilities:

- find `DL_BEGIN_ELEMENT` and `DL_END_ELEMENT` fragments,
- store fragment command ranges,
- record visual bounds and resource generations,
- reject volatile or unsafe fragments.

### Phase 4: Retained Fragment Reuse

On later frames, `render_block_try_retained_fragment()` may inject a cached fragment rather than re-running all paint code for a subtree.

Reuse is a record-time optimization. The result is still a display list for the current frame.

Reuse must be invalidated by:

- layout changes,
- style changes,
- transform/opacity/filter changes,
- image/video/webview/glyph generation changes,
- text selection/caret overlay changes,
- scroll/clip changes that affect the fragment's local paint result.

### Phase 5: Replay

`render_output_replay_display_list()` clears `rdcon.dl` and replays the recorded list into the final surface.

Replay variants:

- single-surface `dl_replay()`,
- tiled parallel replay through `tile_pool.cpp`,
- dirty replay, where replay is clipped by the dirty tracker.

Replay-time responsibilities:

- honor command order,
- apply clips and transforms,
- paint vector/raster/text/effect commands,
- expand and clip effect regions correctly,
- use resource generations to avoid stale borrowed payloads,
- keep tiled replay pixel-equivalent to single replay.

## Initial Render Versus Later Replay

Initial render usually does this:

1. full layout,
2. full display-list record,
3. no retained subtree reuse,
4. full surface clear,
5. full replay.

Later frames may do this:

1. partial layout or no layout,
2. display-list record with retained subtree reuse,
3. dirty-region clear,
4. dirty-region replay,
5. overlay update,
6. video/webview surface update.

The display list itself is not a layout engine. It is a paint command stream for one frame. Retention makes it possible to avoid rebuilding parts of that stream, but the retained data must be invalidated conservatively.

## Animation, Interaction, Resize, And Scroll

Interactive rendering starts with state changes.

### Resize

Resize usually invalidates layout:

- viewport dimensions,
- percentage sizes,
- media queries,
- line wrapping,
- flex/grid/table sizing,
- scroll ranges.

Expected flow:

1. window size changes,
2. document/view tree is marked layout dirty,
3. layout recomputes geometry,
4. display list is recorded for the new geometry,
5. replay paints the updated frame.

Retained display-list fragments may still help for subtrees whose geometry and resources did not change, but resize should be assumed layout-affecting unless proven otherwise.

### Scroll

Scroll changes clip and offset state rather than document content.

Relevant state:

- root scroll offset,
- element scroller pane offset,
- iframe content clip,
- scrollbar thumb geometry,
- dirty regions for newly exposed areas.

Expected flow:

1. scroll state changes in `DocState` or element scroller state,
2. old and new visible regions are marked dirty,
3. render context applies clip/scroll offsets,
4. retained content can be reused if local content did not change,
5. display-list replay repaints the exposed/dirty regions,
6. scrollbar overlay is painted.

The ideal scroll path should not require repainting unchanged offscreen content. The current path has the right ingredients, but consistency depends on all commands having accurate bounds and dirty replay support.

### Animation

Animation updates computed style or animated state each frame.

Animation categories:

- paint-only: color, background color, text color,
- composite-like: opacity, transform,
- effect: filter, shadow, backdrop filter,
- layout-affecting: width, height, margin, font size, content.

Expected invalidation:

- paint-only should mark render dirty,
- opacity/transform should mark visual bounds dirty and prefer retained subtree reuse,
- filter/shadow/backdrop should expand dirty regions by effect bounds,
- layout-affecting animation should mark layout dirty.

Display-list retention is most valuable for transform/opacity/effect animations when child content is unchanged and only wrapper state changes.

### Editing, Selection, Caret, And Hover

Editing and selection mix content changes with overlay changes.

- Text insertion/deletion invalidates layout and glyph resources.
- Caret blink should be overlay-only.
- Selection movement invalidates old and new selection rects.
- Hover/focus can recascade styles and may invalidate paint or layout.

These should flow through the same dirty tracker used by display-list replay so the old overlay pixels are cleared and the new overlay pixels are painted.

## Current Consistency Risks

The current system has strong abstractions, but behavior can still diverge because equivalent paint logic exists in multiple places.

Main divergence points:

- raster record path versus single display-list replay,
- single display-list replay versus tiled replay,
- normal raster output versus strip-based large tiled PNG export,
- raster output versus SVG/PDF backend callbacks,
- inline SVG direct vector drawing versus inline SVG display-list recording,
- CSS effects in block rendering versus display-list effect replay,
- image/surface/video/webview paths with borrowed resources and resource generation checks,
- clip and transform handling in backend walkers versus raster state helpers.

Any feature implemented below the wrong boundary risks needing several parallel fixes.

## Proposed Unified Rendering Architecture

The target design is not one giant renderer. The target is one shared semantic paint model with small backend-specific lowering layers.

### 1. Make Display List The Canonical Paint IR

Use `DisplayList` as the canonical intermediate representation for page painting.

Target flow:

```text
ViewTree
  -> shared paint walker
  -> DisplayListBuilder
  -> DisplayList validation/bounds
  -> backend replay/lowering
  -> screen/png/jpeg/pdf/svg
```

In this model:

- CSS layout and paint-order decisions happen once during DL record.
- clipping, opacity, filters, transforms, shadows, images, glyphs, and SVG subscenes are represented as commands.
- targets differ mainly in how commands are replayed/lowered.

Raster replay lowers commands to `RdtVector` and `ImageSurface`.
SVG replay lowers commands to SVG markup.
PDF replay lowers commands to PDF commands.

This prevents SVG/PDF export from reimplementing the view-tree paint semantics independently.

### 2. Split DisplayList Into Stable Layers

Recommended layers:

- `DisplayListBuilder`: record commands from views.
- `DisplayListStorage`: command ownership, copied payloads, resource refs.
- `DisplayListBounds`: item and fragment bounds, effect expansion.
- `DisplayListValidator`: command invariants and resource generation checks.
- `DisplayListReplayRaster`: raster lowering.
- `DisplayListReplaySvg`: SVG lowering.
- `DisplayListReplayPdf`: PDF lowering.
- `DisplayListReplayTiled`: tile orchestration over the same raster command replay.

The important rule: a new paint feature adds one command or command family, then implements all required lowerings in named files. It should not add one-off view-walker code in each target.

### 3. Collapse Tiled PNG Export Onto Display-List Replay

Large PNG export should record the display list once, then stream tiles by replaying that list with tile clips.

Target flow:

```text
layout once
record display list once
for each output strip:
  clear tile
  replay display list into tile clip
  write rows
```

Benefits:

- strip output matches normal PNG output,
- no per-strip view-tree re-render divergence,
- retained fragments and command bounds remain useful,
- tile-only bugs are constrained to replay/lowering.

### 4. Route SVG And PDF Export Through Display-List Lowering

SVG/PDF should eventually stop walking the view tree directly for paint semantics.

Target flow:

```text
ViewTree -> DisplayList -> SVG/PDF replay backend
```

`render_walk.cpp` can remain as the shared builder traversal, but it should build `DisplayList` through the same paint builder used by raster. SVG/PDF target code should consume commands, not re-decide how a block paints.

Some commands may be unsupported in vector export. That should be explicit:

- native vector lowering when possible,
- isolated raster fallback picture when necessary,
- clear unsupported marker in logs/tests.

### 5. Make Inline SVG Produce A Sub-DisplayList Or RdtPicture Command

Inline SVG should not be a special branch that behaves differently depending on caller.

Preferred model:

- parse SVG DOM once,
- resolve SVG style/resources,
- record SVG paint into either:
  - a nested display-list fragment, or
  - an immutable `RdtPicture` command with resource generation,
- replay/lower that command consistently across raster/SVG/PDF targets.

This would reduce bugs where inline SVG direct vector drawing differs from display-list replay or export.

### 6. Centralize Effect Groups

Opacity, filter, blend, shadow, mask, backdrop, and clip should be one command model.

Recommended command structure:

- `BeginEffectGroup(bounds, clip, opacity, blend, filter, backdrop, isolation)`
- child paint commands
- `EndEffectGroup`

Backends decide whether to:

- lower to native vector constructs,
- use an offscreen raster group,
- flatten to a raster image fallback,
- reject/xfail if unsupported.

This avoids spreading effect behavior across block rendering, background rendering, filter code, display-list replay, and tiled replay.

### 7. Centralize Clip And Transform Stack Semantics

All targets should consume the same stack commands:

- `Save`
- `Restore`
- `ConcatTransform`
- `PushClipPath`
- `PushClipRect`
- `PopClip`

The stack model should be validated during recording and replay. Tiled replay should translate clips through one helper, not per-command local code.

### 8. Treat Resource Generation As Mandatory

Borrowed payloads should always carry a generation or immutable ownership marker:

- image pixels,
- SVG pictures,
- glyph buffers,
- video frames,
- webview surfaces,
- volatile text overlays.

Replay and retained-fragment reuse should reject stale generations. Commands with borrowed pointers and no generation should be marked non-retainable.

### 9. Add Cross-Path Parity Tests

For each new paint feature, require parity fixtures across relevant paths:

- normal raster PNG,
- display-list single replay,
- display-list tiled replay,
- large tiled PNG export,
- SVG export when supported,
- PDF export when supported,
- dirty replay if the feature can change interactively.

Useful test classes:

- clip + transform + image,
- border/background with border-radius,
- opacity + filter + text,
- inline SVG with currentColor and filters,
- retained subtree with changed image generation,
- scroll dirty repaint,
- resize relayout plus retained subtree rejection.

### 10. Make Path Selection Observable

Every render should log or expose:

- target kind,
- record mode,
- replay mode,
- tile count/thread count,
- dirty replay yes/no,
- retained fragment reused/captured/rejected counts,
- backend name and capability table.

This makes it obvious which path produced a bug.

## Suggested Incremental Plan

### Phase A: Guard Existing Paths

- Add a render-path trace struct emitted per frame.
- Add assertions for display-list stack balance and command validity.
- Add focused tests for single replay versus tiled replay equivalence.
- Add tests for normal PNG versus large tiled PNG export equivalence.

### Phase B: Unify Tiled PNG Export

- Record display list once for large PNG export.
- Replay strips from the same command list.
- Remove per-strip view-tree render except as a fallback.

### Phase C: Formalize DisplayListBuilder

- Move raster callback paint emission behind a builder-facing API.
- Make all `rc_*` calls explicit builder commands when recording.
- Document which helper is allowed to record commands and which helper may only lower commands.

### Phase D: Add SVG/PDF Display-List Replayers

- Start with simple commands: rects, paths, text, images, clips, transforms.
- Keep existing `render_svg.cpp` and `render_pdf.cpp` as fallback paths during migration.
- Flip individual command families to DL lowering as parity tests pass.

### Phase E: Consolidate Effects

- Replace ad hoc opacity/filter/blend command sequences with explicit effect group commands.
- Implement raster lowering first.
- Add SVG/PDF native lowering or raster fallback for unsupported effects.

### Phase F: Inline SVG As Nested Paint IR

- Record inline SVG into nested display-list fragments or cached pictures.
- Use the same resource-generation and bounds rules as normal display-list fragments.
- Lower nested SVG consistently for raster, SVG export, and PDF export.

## Target Rule

New rendering features should be implemented at the highest shared layer possible:

1. paint semantics in the display-list builder,
2. command ownership and bounds in display-list storage/bounds,
3. target-specific behavior only in replay/lowering,
4. fallback decisions behind backend capability checks,
5. tests that compare all enabled lowerings.

If a fix requires editing raster replay, tiled replay, SVG export, PDF export, and inline SVG separately, the feature is probably sitting below the right abstraction boundary.

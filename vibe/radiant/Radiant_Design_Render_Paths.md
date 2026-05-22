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

It diverges on *two* axes, not one. Besides re-walking the tree per strip, it
never sets `rdcon.dl`, so every `rc_*` call falls to the immediate `RdtVector`
branch. The strip path therefore also bypasses retained fragments, dirty replay,
and any effect implemented as a display-list post-op (opacity/blend/filter/shadow
groups). It is the *only* remaining consumer of immediate mode in normal output,
together with offscreen SVG-picture rasterisation.

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

Inline SVG is a subpath used by multiple targets. "SVG" actually refers to two
inputs that both end up as the same `Element*` SVG DOM:

- inline `<svg>` in HTML, parsed onto `DomElement::native_element`,
- external `.svg` files (`<img src>`, `background-image`, data URIs), parsed by
  `rdt_picture_load*` into an `RdtPicture` of kind `KIND_SVG_DOM` (`svg_root`).

#### Raster: inline and file SVG already share one painter

For screen/PNG/JPEG, both inputs converge on `render_svg_to_vec()`:

1. inline `<svg>` → `render_inline_svg()` → `render_svg_to_vec(vec, svg_root, …, dl, …)`,
2. `.svg` picture → `rdt_picture_draw` (KIND_SVG_DOM) → `render_svg_to_vec(vec, pic->svg_root, …)`.

This is deliberate (see the comment in `rdt_vector_tvg.cpp`): the file-SVG
picture goes through the same code path as inline `<svg>` so font and style
resolution stay uniform with HTML body text. Inline SVG passes `rdcon->dl`
through so it records into the display list; an offscreen picture passes
`dl == null` and draws immediately to `RdtVector`. This part of the system is
already unified and should be preserved.

`render_svg_to_vec()` carries its **own** copy of the `if (dl) dl_* else rdt_*`
dispatch (`render_svg_inline.cpp`), a third clone of the `rc_*` painter gateway
(alongside `render_painter.cpp` and the inline-effects code). The SVG *paint
semantics* are shared between inline and file SVG, but the painter dispatch is
duplicated.

#### Export: inline SVG diverges three ways

Across output targets the same inline `<svg>` has three different fates:

- **Raster** (`render_svg_to_vec`): full Radiant SVG paint/style/filter resolution.
- **SVG export** (`render_inline_svg_passthrough`): re-serialises the SVG DOM as
  text into a `<g transform>`; Radiant's SVG painter never runs. This is also a
  *correctness* gap, not just duplication — passthrough does not reflect the HTML
  cascade (`color`/`currentColor`, `fill`, `stroke`) that the painted path
  applies from `view->in_line`.
- **PDF export** (`render_inline_svg` callback is NULL): inline SVG is silently
  dropped.

So a fix to SVG rendering only lands in the raster path; SVG export passes the
buck to the consumer renderer and PDF shows nothing. This is the clearest single
example of "fixed in one path, broken in another," and the unified inline-SVG
design below (Proposal §5) targets it directly.

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

### Root cause: the per-element paint algorithm is duplicated

The deepest divergence is not "the same paint semantics live in two files." It is
that the algorithm for *how to paint one element* exists twice:

- `render_walk_block()` in `render_walk.cpp` runs the sequence
  font → position → transform → boundary → inline-SVG → image → opacity → children
  → column-rules, so export transforms now include the block's own paint as well
  as descendants.
  This is what SVG and PDF export use.
- The raster path never runs that body. `render_walk_children()` sees the raster
  backend has set the `render_block`/`render_inline` full-node overrides and calls
  them directly, so raster re-implements the same per-element sequence in
  `render_block_view()` (`render_block.cpp`) with its own transform scope,
  `render_bound`, opacity, and stacking handling.

So the "shared walker" is shared only as a *traversal skeleton*; for raster it is
a visitor that immediately hands control back to a parallel renderer. Two
implementations of the per-element paint algorithm is the structural reason the
same bug must be fixed in several places.

### Concrete duplication: boundary rendering is implemented three times, not at parity

Background + border + shadow + outline ("boundary") is implemented separately in
each target, and the three are not equivalent:

- raster: `render_border.cpp` / `render_background.cpp` — full (gradients,
  border-radius, 3D border styles, box-shadow),
- SVG: `render_bound_svg` (`render_svg.cpp`, ~400 lines re-deriving rounded-rect
  paths and 3D border darken/lighten),
- PDF: `pdf_cb_render_bound` (`render_pdf.cpp`) — four plain rects: **no
  border-radius, no gradients, no shadow, no border styles.**

A fix to border-radius or gradient backgrounds in raster cannot reach PDF because
the code is not there. This is the bug class this document exists to remove.

### Main divergence points

- raster record path versus single display-list replay,
- single display-list replay versus tiled replay,
- normal raster output versus strip-based large tiled PNG export (which also
  bypasses the display list entirely — see §3 above),
- raster output versus SVG/PDF backend callbacks (boundary, text, effects),
- inline SVG painted via `render_svg_to_vec` versus SVG-export text passthrough
  versus PDF-export drop (see §6 above),
- the `if (dl) dl_* else rdt_*` painter dispatch cloned in three places
  (`render_painter.cpp`, the SVG primitive helpers, the inline-SVG effect code),
- CSS effects in block rendering versus display-list effect replay,
- image/surface/video/webview paths with borrowed resources and resource generation checks,
- clip and transform handling in backend walkers versus raster state helpers.

Any feature implemented below the wrong boundary risks needing several parallel fixes.

## Proposed Unified Rendering Architecture

The target design is not one giant renderer. The target is one shared semantic paint model with small backend-specific lowering layers.

### 1. Two IR levels: a semantic paint IR above the raster display list

The natural temptation is "make `DisplayList` the canonical IR for all targets."
That is a trap, because **today's `DisplayList` is already a raster lowering, not a
target-neutral paint IR.** Its op set is pixel-domain:

- `DL_FILL_SURFACE_RECT`, `DL_BLIT_SURFACE_SCALED` (direct surface pixels),
- `DL_COMPOSITE_OPACITY` / `DL_SAVE_BACKDROP` / `DL_APPLY_BLEND_MODE` carrying
  *physical pixel* `x0,y0,w,h` and `premultiplied_source`,
- `DL_BOX_BLUR_REGION` / `DL_BOX_BLUR_INSET` / `DL_OUTER_SHADOW` (multi-pass pixel
  box blur),
- `DL_DRAW_GLYPH` carrying a **rasterised `GlyphBitmap`**, not a glyph id + run.

If SVG/PDF consume *this* list, text becomes images of glyphs (no selectable
`<text>`, huge files) and every pixel-only op has no vector meaning, so each
vector backend must reinterpret or skip it. The divergence does not disappear; it
relocates into the lowering layer. "N parallel renderers" becomes "1 raster IR +
N backends each special-casing raster-only ops."

The fix is to recognise **two levels** and make the shared layer the higher one:

- **Semantic paint IR** (target-neutral, the canonical shared layer): glyph *runs*
  (font + positions + text + color), `BeginEffectGroup(opacity, blend, filter,
  backdrop, clip, transform, isolation)` / `EndEffectGroup`, fills/strokes,
  paths, gradients, images, the clip/transform stack, and nested SVG subscenes.
  No physical pixels, no premultiplied compositing, no rasterised glyphs.
- **Raster display list** (today's `DisplayList`): one *lowering* of the semantic
  IR — glyph run → bitmap, effect group → backdrop-save + blur + composite, etc.
  Tiled replay re-replays this list.

Target flow:

```text
ViewTree
  -> shared paint walker
  -> PaintBuilder           (emits the semantic paint IR)
  -> PaintIR validation/bounds
  -> per-target lowering:
       raster lowering  -> DisplayList -> RdtVector / ImageSurface (single + tiled)
       svg   lowering   -> SVG markup (native <text>, gradients, filters)
       pdf   lowering   -> PDF drawing commands
  -> screen/png/jpeg/pdf/svg
```

In this model:

- CSS layout and paint-order decisions happen once during the paint-IR record.
- clipping, opacity, filters, transforms, shadows, images, glyphs, and SVG
  subscenes are represented as semantic commands.
- targets differ only in lowering; the raster display list is just the most
  detailed lowering, not the contract every backend must understand.

This prevents SVG/PDF export from reimplementing the view-tree paint semantics
independently, *and* avoids forcing vector backends to special-case raster-only
ops.

> Pragmatic migration note: the semantic IR can start as a thin layer *above*
> today's `DisplayList`. Raster keeps lowering to `DisplayList` unchanged; the
> first win is making raster's `render_block_view` and the SVG/PDF
> `render_walk_block` both *emit the same paint IR*, collapsing the duplicated
> per-element algorithm before any backend is rewritten.

### 2. Split The Pipeline Into Stable Layers

Recommended layers, top (semantic) to bottom (raster):

- `PaintBuilder`: record the semantic paint IR from views (the single per-element
  paint algorithm; replaces both `render_walk_block` and `render_block_view`).
- `PaintIRStorage`: command ownership, copied payloads, resource refs.
- `PaintIRBounds`: item and fragment bounds, effect expansion.
- `PaintIRValidator`: command invariants, stack balance, resource generation checks.
- `LowerRaster`: semantic IR → `DisplayList` (the existing raster command set).
- `LowerSvg`: semantic IR → SVG markup (native `<text>`, gradients, filters).
- `LowerPdf`: semantic IR → PDF drawing commands.
- `DisplayListReplayRaster` / `DisplayListReplayTiled`: replay the raster
  `DisplayList` to a surface, single or per-tile. Tiled replay is replay-only — it
  never re-walks views.

The important rule: a new paint feature adds one *semantic* command or command
family, then implements its lowering in each `Lower*` file. It should not add
one-off view-walker code in each target.

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

This should be paired with an explicit deliverable: **delete immediate mode.**
Once tiled PNG and offscreen SVG-picture rasterisation both record into a display
list, nothing sets `rdcon.dl == null`, so the `else rdt_*` branch in every `rc_*`
helper (and its two clones in the SVG primitive/effect code) can be removed. That
collapses the painter to a single recording path and eliminates a whole class of
"record vs immediate" divergence at its source.

### 4. Route SVG And PDF Export Through Paint-IR Lowering

SVG/PDF should stop walking the view tree directly for paint semantics and lower
the semantic paint IR instead.

Target flow:

```text
ViewTree -> PaintBuilder -> PaintIR -> LowerSvg / LowerPdf
```

`render_walk.cpp` can remain as the shared traversal, but it should drive the same
`PaintBuilder` used by raster. SVG/PDF target code consumes semantic commands, not
re-decides how a block paints (this is what removes the triple boundary
implementation called out above).

Some commands may be unsupported in a vector target. That must be explicit, and
backed by a **per-target capability table** — note that the existing
`render_backend_caps.hpp` describes only the raster `RdtVector` backend (ThorVG /
CoreGraphics) and must be extended to SVG/PDF export. Each `Lower*` consults its
capabilities and chooses:

- native vector lowering when supported,
- isolated raster fallback picture (render the subtree to an image, embed it) when
  not,
- a clear unsupported marker in logs/tests so the fallback is observable, never
  silent (today PDF silently drops inline SVG — exactly what this prevents).

### 5. Unify Inline And File SVG As A Nested Paint-IR Subscene

Start from what already works: inline `<svg>` and external `.svg` files both reach
`render_svg_to_vec()` for raster, so their *paint semantics are already shared*.
The job is not to unify those two inputs — it is to make `render_svg_to_vec`
produce the shared semantic paint IR (one nested **SVG subscene** command) instead
of driving `RdtVector`/`DisplayList` directly, so that the one painter feeds every
target. Concretely this replaces three behaviours with one:

- raster's direct `render_svg_to_vec` → `RdtVector` drawing,
- SVG export's `render_inline_svg_passthrough` (DOM-to-text re-serialisation),
- PDF export's NULL inline-SVG callback (drop).

Preferred model:

- parse the SVG DOM once (inline from `native_element`, file via
  `rdt_picture_load*`); cache file SVGs as today,
- resolve SVG style/resources once,
- have `render_svg_to_vec` emit a **`SvgSubscene` paint-IR command**: a nested
  paint-IR fragment plus the data needed to place and inherit it, then lower that
  one command per target:
  - raster lowering → today's `DisplayList` ops (unchanged behaviour),
  - SVG lowering → native SVG markup (or, when the subscene uses unsupported
    features, the existing passthrough re-serialisation as an explicit fallback),
  - PDF lowering → PDF commands, or a raster-fallback picture when unsupported —
    never a silent drop.

**Inheritance and geometry that must survive lowering.** The reason passthrough is
not just duplication but a correctness bug is that it ignores state the painted
path applies. The `SvgSubscene` command must carry, so every lowering reproduces
it:

- the inherited `currentColor` and cascaded `fill` / `stroke` / `stroke-width`
  taken from `view->in_line` (`has_color`, `has_svg_fill`, `svg_stroke_*`) — see
  `render_inline_svg`,
- the content viewport, `viewBox`, and `preserveAspectRatio`-derived transform
  (the scale/translate composition in `render_svg_to_vec`),
- the content clip established for the SVG box,
- `source_path` for resolving nested SVG refs and the recursion guard,
- a resource generation, so the subscene is retainable and stale-reuse is rejected
  (it is an immutable parsed DOM — a natural fit for generation tagging).

This makes inline SVG behave identically across raster, SVG export, and PDF export,
and removes the third clone of the painter dispatch (the SVG primitive/effect
helpers fold into the same `PaintBuilder` gateway).

### 6. Centralize Effect Groups

Opacity, filter, blend, shadow, mask, backdrop, and clip should be one command
model. These are **semantic-IR** commands (§1, §2). The pixel-domain ops in
today's `DisplayList` — `DL_COMPOSITE_OPACITY`, `DL_SAVE_BACKDROP`,
`DL_APPLY_BLEND_MODE`, `DL_BOX_BLUR_REGION`/`_INSET`, `DL_OUTER_SHADOW` — become
the *raster lowering* of one effect group, not the contract vector backends must
read.

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

This is mostly an *enforcement* gap, not new design: the fields already exist
(`resource_generation` on `DlDrawImage` / `DlDrawGlyph`, `src_generation` on
`DlBlitSurfaceScaled`, etc.). What is missing is the `PaintIRValidator` rule that
marks any borrowed-pointer command lacking a generation as non-retainable, so the
guarantee holds by construction rather than by convention.

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
- backend name and capability table,
- per-command-family lowering taken (native vs raster-fallback vs unsupported).

This makes it obvious which path produced a bug. Much of this already exists —
`render_profiler.cpp` emits record/replay timings, tile/thread counts, and
selective/tiled flags — so this is an *extension* of the profiler (add backend
capability, lowering choice, and fallback markers), not a new facility.

## Suggested Incremental Plan

The ordering matters. The risky step is pointing vector export at a shared IR;
everything before it should de-risk that without changing pixels. In particular,
**do not** rewrite SVG/PDF to consume the IR until raster already emits and
re-consumes it at parity — otherwise the raster bias in today's `DisplayList`
(§1) leaks into the vector backends.

### Phase A: Guard Existing Paths

- Add a render-path trace struct emitted per frame (extend `render_profiler.cpp`).
- Add assertions for display-list stack balance and command validity.
- Add focused tests for single replay versus tiled replay equivalence.
- Add tests for normal PNG versus large tiled PNG export equivalence.
- Hygiene: delete checked-in `*.bak` files (`render_svg.cpp.bak`,
  `render_img.cpp.bak`, `layout_table.cpp.bak`) so nobody edits a dead copy.

### Phase B: Unify Tiled PNG Export And Delete Immediate Mode

- Record the display list once for large PNG export; replay strips from it.
- Remove per-strip view-tree render except as a fallback.
- Route offscreen SVG-picture rasterisation through a display list too.
- With no remaining `rdcon.dl == null` callers, delete the `else rdt_*` branch in
  every `rc_*` helper and its two clones (SVG primitive/effect dispatch). One
  recording path remains.

### Phase C: Introduce The Semantic Paint IR Above The Display List

- Define the semantic paint IR (glyph runs, effect groups, paths, gradients,
  images, clip/transform stack, SVG subscene).
- Make a single `PaintBuilder` the one per-element paint algorithm, replacing both
  `render_walk_block` (export) and `render_block_view` (raster).
- Keep raster lowering to today's `DisplayList` so pixels do not change; verify
  with the baseline raster tests.
- This collapses the duplicated per-element algorithm — the root cause — before
  any backend is rewritten.

### Phase D: Route SVG/PDF Export Through Paint-IR Lowering

- Build per-target capability tables (extend `render_backend_caps.hpp` to SVG/PDF).
- Start with simple command families: rects, paths, text, images, clips, transforms.
- Keep existing `render_svg.cpp` / `render_pdf.cpp` paths as fallback during migration.
- Flip families to IR lowering as parity fixtures pass; replace PDF's minimal
  boundary and SVG's re-derived boundary with the shared lowering.

### Phase E: Consolidate Effects

- Replace ad hoc opacity/filter/blend sequences with explicit effect-group IR commands.
- Implement raster lowering first (the existing pixel ops become that lowering).
- Add SVG/PDF native lowering or raster-fallback picture for unsupported effects;
  log the fallback (never silent).

### Phase F: Inline SVG As A Nested Paint-IR Subscene

- Make `render_svg_to_vec` emit one `SvgSubscene` IR command instead of driving
  `RdtVector`/`DisplayList` directly.
- Carry inherited `currentColor`/`fill`/`stroke`, the viewBox/`preserveAspectRatio`
  transform, the content clip, `source_path`, and a resource generation (§5).
- Lower the subscene consistently for raster, SVG export (replacing passthrough),
  and PDF export (replacing the dropped callback).
- Use the same resource-generation and bounds rules as normal IR fragments.

## Target Rule

New rendering features should be implemented at the highest shared layer possible:

1. paint semantics in the `PaintBuilder` (semantic paint IR),
2. command ownership and bounds in paint-IR storage/bounds,
3. target-specific behavior only in per-target lowering (raster `DisplayList`,
   SVG, PDF),
4. fallback decisions behind per-target capability checks,
5. tests that compare all enabled lowerings.

If a fix requires editing raster replay, tiled replay, SVG export, PDF export, and inline SVG separately, the feature is probably sitting below the right abstraction boundary.

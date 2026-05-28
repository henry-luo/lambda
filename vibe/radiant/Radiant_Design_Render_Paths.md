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

1. inline `<svg>` → `render_inline_svg()` → `render_svg_to_vec(vec, svg_root, …, dl, paint_list)`,
2. `.svg` picture → `rdt_picture_draw` (KIND_SVG_DOM) → `render_svg_to_vec_via_display_list(vec, pic->svg_root, …)`.

This is deliberate (see the comment in `rdt_vector_tvg.cpp`): the file-SVG
picture goes through the same code path as inline `<svg>` so font and style
resolution stay uniform with HTML body text. Inline SVG passes `rdcon->dl` and
`rdcon->paint_list` through so it records into the shared PaintIR/display-list
pipeline; an offscreen picture uses `render_svg_to_vec_via_display_list()` to
create temporary recording targets and replay them into the picture surface.
This part of the system is already unified and should be preserved.

`render_svg_to_vec()` now records SVG primitives through the shared
PaintIR/display-list gateway. The SVG *paint semantics* are shared between
inline and file SVG, and the primitive painter dispatch is no longer a private
SVG clone.

#### Export: inline SVG now uses the semantic subscene path

Across output targets the same inline `<svg>` now enters PaintIR as a semantic
`PAINT_SVG_SUBSCENE` command:

- **Raster** (`render_svg_to_vec`): full Radiant SVG paint/style/filter resolution.
- **SVG export**: lowers the subscene through `paint_ir_lower_svg_stream()` and
  the shared subscene serializer, carrying inherited `currentColor`, `fill`,
  `stroke`, `stroke-width`, opacity, clip, transform, and source path.
- **PDF export**: records the same subscene command; the PDF PaintIR lowerer owns
  the explicit `[PDF_PAINT_IR]` raster fallback image.

This removes the old `render_inline_svg_passthrough` split and the
callback-owned PDF inline-SVG fallback. Native PDF vector lowering for nested SVG
can still improve inspectability and file size, but the ownership boundary is now
shared.

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
  versus PDF-export raster fallback (see §6 above),
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
  silent (the PDF inline-SVG raster fallback is the first slice of this behavior).

### 5. Unify Inline And File SVG As A Nested Paint-IR Subscene

Start from what already works: inline `<svg>` and external `.svg` files both reach
`render_svg_to_vec()` for raster, so their *paint semantics are already shared*.
The job is not to unify those two inputs — it is to make `render_svg_to_vec`
produce the shared semantic paint IR (one nested **SVG subscene** command) instead
of driving `RdtVector`/`DisplayList` directly, so that the one painter feeds every
target. Concretely this replaces three behaviours with one:

- raster's direct `render_svg_to_vec` → `RdtVector` drawing,
- SVG export's `render_inline_svg_passthrough` (DOM-to-text re-serialisation),
- PDF export's target-specific inline-SVG raster fallback.

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

### Execution Update: 2026-05-28

The remaining work is now grouped into four larger implementation phases:

1. **Guardrails and observability**: make path selection, backend capability,
   display-list validation, retained-fragment decisions, and export fallbacks
   visible in logs/events before changing semantics.
2. **Record-once raster convergence**: keep normal PNG/JPEG/screen and large
   tiled PNG on the same display-list record/replay model, then remove immediate
   raster painter branches once offscreen SVG-picture rendering no longer needs
   them.
3. **Semantic PaintIR ownership**: move the per-element paint algorithm into one
   PaintBuilder that emits semantic commands, with raster lowering preserving
   today's display-list behavior.
4. **Vector export lowering**: route SVG/PDF export through PaintIR lowerers with
   capability-driven native/fallback decisions, including nested inline/file SVG
   subscenes.

Phase 1 is now implemented in code:

- `RenderPathTrace` records target, replay mode, backend name/capabilities,
  display-list state, tiled/strip mode, surface size, and retained-cache counters.
- Raster screen/PNG/JPEG, large tiled PNG, and file-level SVG/PDF exports all emit
  an observable `[RENDER_PATH]` line; event logs also receive the expanded
  `render.path` payload when enabled.
- Retained display-list capture/reuse now reports per-frame candidates, captures,
  non-retainable skips, copy failures, reuse hits, misses, stale-resource
  rejections, and dirty-source rejections.
- Borrowed-resource retainability is centralized in the display-list layer via
  `dl_item_is_retainable_for_fragment()`, so retained-fragment capture no longer
  owns a private duplicate of that rule.
- Focused retained-display-list tests cover stats reset, non-retainable borrowed
  image rejection, and reuse outcome counters.

Phase 2 is now implemented with the shared recording gateway:

- Added `render_paint_gateway.hpp`, a common PaintIR/display-list recording
  boundary used by both the raster `rc_*` painter API and the inline/file SVG
  painter helpers.
- `render_painter.cpp` no longer owns its own PaintIR-vs-display-list branching
  logic per primitive; it builds a `PaintRecordTarget` and delegates to the
  shared gateway.
- `render_svg_inline.cpp` no longer owns a private duplicate of that primitive
  recording logic for paths, gradients, pictures, clips, opacity/backdrop, and
  SVG blur effects.
- The low-level `render_svg_to_vec()` API now requires explicit `DisplayList`
  and `PaintList` targets; offscreen picture callers use
  `render_svg_to_vec_via_display_list()` as the convenience wrapper.
- The shared gateway no longer has a display-list-only compatibility branch:
  missing `PaintList`/`DisplayList` targets now log and record nothing.
- This keeps the current record-then-lower behavior intact while removing one of
  the cloned painter dispatch layers called out in this document.
- Existing render-output parity fixtures cover normal PNG versus forced tiled
  PNG, single replay versus threaded tiled replay, and SVG-picture single replay
  versus threaded tiled replay.

Phase 3 / Phase C is now implemented for block paint ownership:

- Added `render_paint_block_run()` and `RenderPaintBlockOps`, a shared block
  paint lifecycle driver for begin → self paint → children → finish.
- Raster `render_block_view()` now uses that driver while keeping the existing
  raster phase bodies, retained-fragment checks, clips, effects, scrollers, and
  display-list lowering unchanged.
- SVG/PDF export `render_walk_block()` now uses the same driver lifecycle with
  export callbacks as the backend-specific operations.
- Focused tests cover the lifecycle contract, including the inline-SVG/export
  case where self-paint intentionally skips child traversal but still runs
  finish/restore.
- Raster output parity and the baseline layout suite verify the refactor does
  not change rendered output.

Phase 4 / Phase D is now implemented for the simple export command families:

- Inline SVG is no longer owned as a Phase D callback fallback; Phase F records it
  as a semantic `PAINT_SVG_SUBSCENE` and the PDF lowerer owns its explicit
  `[PDF_PAINT_IR]` raster fallback.
- `RenderOutputParity.PdfInlineSvgUsesRasterFallbackImage` verifies that PDF
  export emits an inline image for an inline SVG subscene.
- SVG/PDF export have target capability tables for rects, rounded rects, paths,
  strokes, gradients, images, glyph runs, clips, transforms, and
  target-specific opacity support.
- SVG export lowers these command families through `paint_ir_lower_svg_stream()`.
- PDF export lowers rects, rounded rects, paths, strokes, images, glyph runs,
  clips, transforms, and gradient backgrounds through the PDF PaintIR lowering
  path. PDF gradients use an explicit `[PDF_PAINT_IR]` raster fallback image.
- `RenderOutputParity.PdfGradientBackgroundUsesRasterFallbackImage` verifies that
  PDF gradient backgrounds are no longer silently omitted.
- Inline SVG subscene ownership is complete in Phase F.

### Phase A: Guard Existing Paths

- Done: render-path trace struct emitted per frame (extend `render_profiler.cpp`).
- Done: assertions for display-list stack balance and command validity.
- Done: focused tests for single replay versus tiled replay equivalence.
- Done: tests for normal PNG versus large tiled PNG export equivalence.
- Done: checked-in `*.bak` files (`render_svg.cpp.bak`, `render_img.cpp.bak`,
  `layout_table.cpp.bak`) are no longer present.

### Phase B: Unify Tiled PNG Export And Delete Immediate Mode

- Done: record the display list once for large PNG export; replay strips from it.
- Done: remove per-strip view-tree render.
- Done: route offscreen SVG-picture rasterisation through a display list too.
- Done: with no remaining direct SVG picture render and no gateway
  display-list-only branch, one PaintIR/display-list recording path remains for
  raster painter primitives.

### Phase C: Introduce The Semantic Paint IR Above The Display List

Status: **Complete for the Phase C ownership milestone as of 2026-05-28.**

- Done: define the semantic paint IR (glyph runs, effect groups, paths, gradients,
  images, clip/transform stack, SVG subscene).
- Done: route primitive painter commands through `PaintList` and raster lowering
  before they become `DisplayList` commands.
- Done: introduce the shared block paint lifecycle driver used by both
  `render_walk_block` (export) and `render_block_view` (raster), so the
  per-element phase order is no longer maintained as two unrelated functions.
- Done: keep raster lowering to today's `DisplayList` so pixels do not change;
  verified with focused display-list parity, render-output parity, retained
  display-list tests, and the baseline layout suite.

Implemented pieces:

- `render_paint_block_run()` / `RenderPaintBlockOps` own the common block paint
  lifecycle: begin, self-paint, optional child traversal, finish/restore.
- Raster adapts its existing phase bodies into that lifecycle, preserving
  retained fragments, dirty/viewport skips, transforms, clipping, effects,
  scrollers, column rules, and `DisplayList` lowering.
- SVG/PDF export adapts its callback-driven block walker into the same lifecycle,
  preserving current export behavior while removing the second standalone block
  phase algorithm.
- `PaintIrParityTest.SharedBlockPaintDriverSkipsChildrenButFinishes` and
  `PaintIrParityTest.SharedBlockPaintDriverRecordsChildrenTime` lock the new
  lifecycle contract.

Verification completed:

- `make build`
- `make build-test`
- `test_display_list_gtest` PaintIR/display-list parity and driver tests
- `test_retained_display_list_gtest`
- `test_pdf_render_visual_gtest --gtest_filter='RenderOutputParity.*'`
- `make layout suite=baseline` (4175 passed, 6 skipped)
- `make check-int-cast`
- `git diff --check`

Moved out of Phase C:

- Full SVG/PDF consumption of simple semantic PaintIR command families is handled
  by Phase D.
- Consolidating opacity/filter/blend semantics into effect-group lowering is now
  handled by Phase E.
- Replacing inline-SVG passthrough/fallback with a true `SvgSubscene` command
  is handled by Phase F.

### Phase D: Route SVG/PDF Export Through Paint-IR Lowering

Status: **Complete for simple SVG/PDF PaintIR export lowering as of 2026-05-28.**

- Done: build per-target capability tables in `render_backend_caps.hpp` for SVG
  and PDF export.
- Done: route simple command families through export lowering:
  rects, rounded rects, paths, strokes, text/glyph runs, images, clips,
  transforms, and gradients.
- Done: keep the existing `render_svg.cpp` / `render_pdf.cpp` callback paths as
  migration fallbacks for complex boundary/effect cases while simple primitives
  pass through `PaintList`.
- Done: SVG export streams PaintIR through `paint_ir_lower_svg_stream()`,
  including native paths, gradients, images, text runs, clips, transforms, and
  opacity groups.
- Done: PDF export lowers supported PaintIR directly to PDF drawing commands and
  uses explicit raster fallback images for command families PDF cannot represent
  natively, currently CSS gradient backgrounds and SVG subscenes.
- Done: fallback paths log `[PDF_PAINT_IR]` and are covered by
  `RenderOutputParity.PdfInlineSvgUsesRasterFallbackImage` and
  `RenderOutputParity.PdfGradientBackgroundUsesRasterFallbackImage`.

Not part of Phase D anymore:

- Effect-group ownership for opacity/filter/blend/shadow is handled by Phase E;
  deeper native vector quality remains optional follow-up work.
- Replacing SVG export inline-SVG passthrough and PDF inline-SVG callback-owned
  raster fallback with a semantic `SvgSubscene` command is handled by Phase F.
- Further native PDF support for gradients/effects can improve output quality,
  but explicit raster fallback satisfies Phase D's no-silent-drop contract.

### Phase E: Consolidate Effects

Status: **Complete for the Phase E effect-group ownership milestone as of
2026-05-28.**

- Done: `PAINT_BEGIN_EFFECT_GROUP` / `PAINT_END_EFFECT_GROUP` now have raster
  lowering. A complete PaintIR effect group expands to the existing trusted
  display-list pixel ops: save backdrop, apply filter, composite opacity, and
  apply blend mode.
- Done: SVG export continues to lower opacity-only effect groups to native
  `<g opacity="...">` groups, and unsupported effect groups now log
  `[SVG_PAINT_IR]` instead of failing silently.
- Done: PDF export now receives block opacity through the shared PaintIR
  effect-group callbacks and lowers opacity groups to native PDF ExtGState
  resources (`/ca` and `/CA`) applied through the content stream.
- Done: block-level export opacity now wraps the whole block lifecycle rather
  than only child traversal, so backgrounds/borders participate in SVG/PDF
  effect groups.
- Done: focused tests cover semantic effect-group raster lowering, filter /
  backdrop expansion, SVG opacity-group streaming, PDF ExtGState serialization,
  and PDF opacity effect groups.

Verification completed:

- `make build-test`
- `test_pdf_writer_gtest`
- `test_display_list_gtest --gtest_filter='PaintIrParityTest.*:DisplayListTest.*'`
- `test_pdf_render_visual_gtest --gtest_filter='RenderOutputParity.*'`
- `test_retained_display_list_gtest`
- `make check-int-cast`
- `git diff --check`

Future quality work, not Phase E blockers:

- Native SVG/PDF filters, blend modes, and shadows can improve output quality.
  The shared semantic effect-group command, raster lowering, SVG native opacity,
  PDF native opacity, and unsupported-effect logging are now in place.

### Phase F: Inline SVG As A Nested Paint-IR Subscene

Status: **Complete for the Phase F semantic SVG subscene ownership milestone as
of 2026-05-28.**

- Done: `PaintSvgSubscene` carries the inline SVG DOM root, owning pool,
  font context, viewport size, pixel ratio, inherited `currentColor`,
  `fill`/`stroke`/`stroke-width`, `fill:none` / `stroke:none`, opacity, base
  transform, content clip, `source_path`, and a resource generation.
- Done: `render_svg_to_vec()` now records one `PAINT_SVG_SUBSCENE` command and
  lowers it through PaintIR for raster output instead of directly driving both
  `RdtVector` and `DisplayList`.
- Done: raster lowering expands the subscene through the existing inline SVG
  primitive renderer, preserving the trusted current pixel path while making
  the ownership boundary semantic.
- Done: SVG export lowers `PAINT_SVG_SUBSCENE` through
  `paint_ir_lower_svg_stream()` and the shared subscene serializer; the old
  inline-SVG passthrough serializer in `render_svg.cpp` is removed.
- Done: PDF export records inline SVG as `PAINT_SVG_SUBSCENE`; the PDF PaintIR
  lowerer owns the explicit raster fallback image and logs it as
  `[PDF_PAINT_IR]`.
- Done: PaintIR registers the inline-SVG lowerers as optional handlers, so small
  PaintIR-only test binaries do not link the full inline SVG renderer but full
  raster/SVG/PDF render paths still lower subscenes consistently.

Verification completed:

- `make build-test`
- `test_display_list_gtest --gtest_filter='PaintIrParityTest.*:DisplayListTest.*'`
- `test_pdf_render_visual_gtest --gtest_filter='RenderOutputParity.*'`
- `test_pdf_writer_gtest`
- `test_retained_display_list_gtest`
- `make check-int-cast`
- `git diff --check`

Future quality work, not Phase F blockers:

- Native PDF vector lowering for nested SVG can improve PDF inspectability and
  file size later. Phase F's ownership target is complete because inline SVG is
  now represented as a semantic PaintIR subscene with shared raster, SVG export,
  and PDF export lowering.

### Phase G: Quality And Observability Hardening

Status: **Quality hardening slices in progress as of 2026-05-28.**

- Goal: keep Phase A-F ownership intact while making lowerer decisions more
  inspectable and avoiding silent or content-dropping behavior for unsupported
  vector effects.
- Done: structured PaintIR lowering counters for commands, emitted
  commands, explicit fallbacks, and unsupported commands in render path traces.
- Done: unsupported SVG effect groups can now be captured as cropped raster
  fallback images. SVG embeds the captured pixels as a PNG data URI marked with
  `data-radiant-fallback="effect-raster"` and records fallback counters.
- Done: unsupported PDF effect groups can now be captured as cropped raster
  fallback images. Opaque captures still use compact inline PDF images; captures
  with transparency are emitted as PDF image XObjects with grayscale soft masks
  so fallback alpha is preserved.
- Done: SVG/PDF export walker now surfaces CSS effect groups through a semantic
  `begin_effect_group` / `end_effect_group` backend callback. CSS filters and
  mix-blend modes reach PaintIR lowerers, so unsupported export effects are
  counted and marked by the shared fallback path.
- Done: text inside rasterized export effect groups now lowers through a
  registered glyph-run raster lowerer, preserving text content in exact
  fallback images without coupling core PaintIR to the font stack.
- Done: outer `box-shadow` is routed into semantic effect groups for SVG/PDF
  export, recorded as PaintIR shadow commands, and captured by the shared
  raster fallback image path.
- Done: `backdrop-filter` is now parsed into the resolved style/view model as a
  `backdrop_filter` payload, routed through block/inline semantic effect groups,
  counted by the shared fallback path, and covered by SVG/PDF export fallback
  parity.
- Done: SVG/PDF effect fallback capture now carries a page-backdrop display-list
  mirror. Backdrop-sensitive fallbacks replay prior page pixels into the cropped
  fallback surface before applying blend/backdrop-filter semantics, so PDF can
  emit opaque fallback images for backdrop-filter over opaque page content
  instead of transparent images with soft masks.
- Done: stale Phase D/F prose was updated so the document no longer describes
  the removed SVG passthrough and callback-owned PDF inline-SVG fallback as
  current behavior.

Verification completed:

- `make build-test`
- `test_display_list_gtest --gtest_filter='PaintIrParityTest.*:DisplayListTest.*'`
- `test_pdf_render_visual_gtest --gtest_filter='RenderOutputParity.*'`
- `test_pdf_writer_gtest`
- `test_retained_display_list_gtest`
- `make check-int-cast`
- `git diff --check`

Future quality work:

- Native vector `backdrop-filter` remains target-limited: PDF has no direct CSS
  backdrop-filter operator, and SVG viewer support is not yet used as a trusted
  export path. The current contract is exact cropped raster fallback over the
  tracked prior page backdrop for backdrop-sensitive groups.
- More visual fixtures can be added for mixed text, shadow, blend, and filter
  combinations once the fallback matrix stabilizes.

## Target Rule

New rendering features should be implemented at the highest shared layer possible:

1. paint semantics in the `PaintBuilder` (semantic paint IR),
2. command ownership and bounds in paint-IR storage/bounds,
3. target-specific behavior only in per-target lowering (raster `DisplayList`,
   SVG, PDF),
4. fallback decisions behind per-target capability checks,
5. tests that compare all enabled lowerings.

If a fix requires editing raster replay, tiled replay, SVG export, PDF export, and inline SVG separately, the feature is probably sitting below the right abstraction boundary.

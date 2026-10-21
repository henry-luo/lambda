# Radiant — Repaint for Presentation Animations

**Status:** Proposal, 2026-10-21. Stages A and B are implemented. A2 covers
paint-only commits only, per Q5. See the
[stage A checkpoint](#stage-a-checkpoint--2026-10-21) and the
[stage B checkpoint](#stage-b-checkpoint--2026-10-21).

**Scope:** frames whose only DOM change is a presentation write of `transform`
or `opacity`, the "paint-only commit" (RAD_16). This proposal covers how much
those frames re-rasterise, and how retained rasters can stand in for unchanged
or merely moved content. Layout, cascade and the paint-only commit's
eligibility are unchanged.

**Governing rulings:**
- **D4.5.3v2:** incremental presentation embraces retention; "restyle, reflow
  and repaint reuse every retained structure that is still valid".
- **D4.5.1v4** and **D4.2.4:** retained storage is arena-owned, released in
  batches, and reference-counted across threads.

This proposal adds no ruling. It revives the row "Transform/opacity fast path —
Retained display sub-trees", which [Radiant_Render_Threading_and_Animation.md](Radiant_Render_Threading_and_Animation.md)
lists as *Deferred*. It also meets the "old/new damage bounds" requirement in
[Radiant_Impl_Rendering_Optimization.md](../impl/Radiant_Impl_Rendering_Optimization.md).

---

## 1. The problem, measured

The photo album's page turn writes one leaf's `rotateY` and two shade
opacities per frame through `dom.presentation_style_set_property`. After the
demo-side tuning of 2026-10-21, every frame takes the paint-only commit, which
costs 0.6 ms. Even so, a frame costs a 55–61 ms median. That is from a release
host, full corpus, 1440×900 at 1×, using `test/demo/photo-album/benchmark.py
--book`. The time is almost all raster:

| Phase per turn frame | Cost | Source |
|---|---|---|
| Commit (cascade, layout) | 0.6 ms | `RADIANT_FRAME_PROFILE=1` |
| Record (view tree → DisplayList, ~1,855 items) | ~2 ms (debug) | `[TIMING] render_block_view (record)` |
| Replay over the whole surface | 30–38 ms (debug), the bulk of the release frame | `[TIMING] dl_replay` |

The release-host sample of the replay breaks down as:
- projective image sampling for the turning leaf
  (`render_image_project_pixels`, `render_pixel_bilinear_mix`);
- ThorVG path rasterisation and scaled images for the static pages;
- the photo drop-shadow blur (`box_blur_region`);
- gradients and the full-window radial backdrop.

Only one subtree changed, yet every pixel of the window was redrawn.

**Damage is small.** The leaf is 535×696 CSS px, turning about the spine under
`perspective: 2400px`. The union of its projected bounds over two consecutive
frames is 29–35% of the window (median 0.30), at both 17 ms and 55 ms frame
intervals: about one page. The other ~70% of the window (the opposite page,
the bars, the spread strip and the backdrop) is unchanged but repainted.

A Retina window rasterises four times the pixels, so it is slower than these 1×
figures. The window present then uploads the whole surface as a new GL texture
(§2.4).

## 2. How a paint-only frame repaints today

### 2.1 Pipeline

```
presentation_style_set_property (dom.cpp:12123)
  → dom_mutation_notify: mutation_epoch++; style_content_epoch++ only for an inherited property
  → post_html_handler_paint_only_commit (event.cpp:7957): resolve the property; full_repaint = true (7994);
    doc_state_request_repaint → is_dirty = true (state_store.cpp:4317)
  → render (window.cpp:1144) → render_html_doc → render_output_render_raster_target (render_output.cpp:1430)
      render_output_clear_surface (979): force_full = is_dirty → clear everything
      RenderFrameCache: misses (its key includes mutation_epoch)
      record the whole tree (render_raster_view_tree); capture retained fragments
      replay (1355): replay_dirty = null → every item over the full surface; any glyph forces serial replay
  → repaint_window (window.cpp:1100): new texture, glTexImage2D of the full surface, swap, glFinish
```

### 2.2 Where a full repaint is forced

1. **The paint-only commit marks `full_repaint` unconditionally**
   (`event.cpp:7994`). It never computes the written element's old or new
   bounds.
2. **Any repaint request sets `is_dirty`, and `is_dirty` forces a full clear.**
   `doc_state_request_repaint` and `doc_state_mark_dirty` set it
   (`state_store.cpp:4317-4324`); `render_output_clear_surface` takes
   `force_full = state->is_dirty` (`render_output.cpp:986`).
   - So selective rects marked by the incremental commit
     (`dom_js_mark_selective_dirty`, `event.cpp:7849-7859`), hover, and
     animation ticks are all repainted in full, because each of them also
     requests a repaint.
   - Only `dirty_mark_element`, which sets `needs_repaint` alone, reaches the
     selective clear. RAD_13 §7 describes the selective path, but it is
     practically unreachable from a commit.
3. **The incremental commit falls back to full** when selective marking fails
   (`event.cpp:7856`).
4. **Headless rendering is always full.** `event_sim_render_surface` calls
   `doc_state_mark_dirty` before every render (`event_sim.cpp:3203`).

### 2.3 What already exists to build on

- **Element markers with post-transform bounds.** `dl_end_element`
  (`display_list_storage.cpp:1128`) rewrites each `DL_BEGIN/END_ELEMENT` with
  the union of its recorded items' device-space bounds. The previous frame's
  visual bounds of every element are therefore known after recording.
  Retained fragments keep them (`bounds`, `marker_bounds`).
- **Dirty-clip replay.** `dl_replay_push_dirty_clip` clips to the dirty union,
  and `dl_replay_skip_clean_item` jumps over element subtrees outside it
  (RAD_12 §5).
- **Retained display-list fragments**, keyed by `view_id` (RAD_12 §6). They are
  reused only on selective frames, and never inside a `css3d_context`.
- **The inline-SVG raster layer** (RAD_14 §4.4), the model for §4:
  - a document-resource registry;
  - a content stamp of generations;
  - capture on the second identical paint;
  - two-pass black/white capture for exact straight alpha;
  - limits of 48 MB per layer and 256 MB per document;
  - `RADIANT_SVG_LAYER=off|eager`.

  It blits only under a uniform scale plus translation; rotation, skew and 3D
  disqualify it.
- **The old+new damage pattern** of CSS animations (`animation.cpp:545-572`).

### 2.4 How 3D content is painted and presented

- **No offscreen pass.** A transformed subtree is drawn primitive by primitive
  under the accumulated projective `rdcon->transform`:
  - paths through `path_replay_projective`;
  - images by inverse sampling (`render_image_project_pixels`).
- **`preserve-3d` planes.** `Css3dPaintContext` records runs per plane, projects
  and BSP-orders the planes, and rewrites the DisplayList tail as
  polygon-clipped copies (`render_css3d.cpp:304-441`).
- **Present.** `repaint_window` creates a texture per frame and uploads the full
  surface with `glTexImage2D`, then calls `glfwSwapBuffers` and `glFinish`. At
  2× that is about 20 MB per frame.

### 2.5 A related gap

`getBoundingClientRect` on the turning leaf returned its untransformed layout
box (720, 72, 535 × 696) on every frame of the turn. CSSOM View defines it to
include transforms. A1's projected bounds would give it a correct source, but
fixing it is a separate conformance item, not part of this proposal.

## 3. Stage A — damage-limited paint-only frames

**Goal:** a paint-only frame repaints only the union of each written element's
visual bounds before and after the write.

### A1. Damage from presentation writes

The paint-only commit stops setting `full_repaint` and marks damage per record
instead:

- **Old bounds.** Take the element's visual bounds from the last recorded
  frame: the element marker's bounds, kept in a per-view `last_visual_bounds`
  map that recording refreshes. These are device-space and include descendants
  and effects (shadows, overflow).
- **New bounds.**
  - For a transform write, project the element's local visual-overflow rect
    through the new accumulated transform, including ancestor
    perspective/`preserve-3d`, with the same projection `css3d_project_quad`
    uses.
  - For an opacity write, the new bounds equal the old ones.
- **Mark the union.**
  - Mark it with a source hint (Radiant_Design_Rendering.md §7, element damage)
    and request a repaint *without* forcing a full clear (A2).
  - Fall back to `full_repaint` when either bound is unknown: a first paint, an
    element never recorded, a non-invertible or behind-the-eye projection, or a
    `preserve-3d` subtree whose plane bounds were not recorded.

### A2. Let a request coexist with selective regions

- **Split the two meanings of `is_dirty`.** "The surface must be fully redrawn"
  becomes `dirty_tracker.full_repaint`, already the dedicated flag. `is_dirty`
  keeps "something needs a render" for scheduling.
- **Clear.** `render_output_clear_surface` chooses selective clearing whenever
  regions exist and `full_repaint` is false.
- **Audit every caller that marks regions and also requests a repaint**:
  - the incremental commit;
  - hover;
  - animation ticks.

  This stage makes their rects effective for the first time. Each one gets a
  parity test (§6) before it is admitted. A caller whose rects are not trusted
  sets `full_repaint` explicitly.
- **Headless.** Keep the event simulator's forced full render as the default,
  so goldens are unchanged. Add `RADIANT_SELECTIVE_SIM=1` to exercise the
  selective path in replays.

### A3. Present only what changed (live window)

- Keep one persistent texture, recreated only on resize.
- Upload the dirty union with `glTexSubImage2D`, or the full surface on a full
  repaint.
- Drop `glFinish` from the steady state, or justify it with a measurement.

**Expected effect:** replay touches ~30% of the window for the book. Element
subtrees outside the union are skipped, and pixel work outside it is clipped.
The estimate is 55 → ~20–25 ms at 1×. The upload shrinks in proportion.

**Risk:** stale pixels if a bound is too small. Projected bounds and the
fallbacks in A1 err toward repainting more. The §6 parity checks compare a
damage-limited frame with a full frame pixel for pixel.

## 4. Stage B — raster layers for subtrees under presentation transforms

**Goal:** a subtree whose only change is its own transform or opacity is drawn
from a retained raster, one projected blit, instead of replaying its hundreds
of items under a 3D matrix. In the book, the turning leaf's photos, prints,
captions and shades become one bitmap per face.

### B1. The layer unit and eligibility

- **Layer root.** A layer root is an element that receives presentation writes
  of `transform` or `opacity` and is eligible for the paint-only commit. Inside
  a `preserve-3d` context, the unit is the plane: each `backface-visibility`
  face, because faces are what `Css3dPaintContext` projects and sorts.
- **Ineligible:**
  - subtrees containing video, canvas, scene3d, webview or an iframe;
  - running CSS or web animations inside the subtree;
  - editable or focused text with a caret;
  - content larger than the layer limit.

### B2. Change signal (content stamp)

The SVG layer has its key; this generalises it. Add a per-element
`paint_layer_generation`, bumped by `dom_mutation_notify` for every ancestor of
a mutated node up to the nearest layer root, mirroring
`dom_svg_layer_note_mutation`. Two exceptions:

- A presentation write of a non-inherited property **on the layer root itself**
  does not bump its own layer. It moves or fades the layer.
- A write on a descendant does bump it.

The stamp also includes:
- `style_content_epoch` (already skips non-inherited presentation writes);
- the selector/content version;
- font, glyph and image generations;
- `doc_state_content_version`;
- the interaction-state version for the subtree (hover, focus);
- raster scale.

### B3. Capture

- **Pass.** Record the root's subtree with the root's own transform *excluded*
  (layer-local space, origin at the root's border box), at `raster_scale ×
  min(max projected scale, 2)`. Rasterise with the SVG layer's two-pass
  black/white scheme (`svg_layer_render_pass`, `svg_layer_resolve_alpha`), so
  the result is exact straight RGBA.
- **When.** Capture on the second consecutive identical paint, as SVG layers
  do. An eager hint captures on the first paint of a presentation-written root
  (Q2).
- **Ownership.** Captured layers live in a document-resource registry
  (`services.paint_layer_registry`), modelled on `svg_layer_registry`:
  - arena-owned (D4.5.1v4);
  - reference-counted while a frame's DisplayList points at them (D4.2.4);
  - released at document teardown or on memory pressure;
  - recapture builds a new layer and retires the old one at frame end (no
    in-place reset while referenced).

### B4. Paint

- **Item.** On a hit, recording emits one `DL_DRAW_LAYER` item: the layer
  surface, the current accumulated transform (projective allowed) and the
  root's opacity, in place of the root's subtree. Its marker bounds are the
  projected layer quad.
- **Replay.**
  - The replay uses the existing projective image path
    (`render_image_project_pixels`), plus a fast path for identity or
    translation (`raster_blit_surface_scaled`'s 1:1 path).
  - Under `preserve-3d`, the plane's layer replaces the plane's runs before the
    planes are BSP-ordered, so ordering and backface culling are unchanged.
- **Hit testing** already reads the retained transform and is unaffected.

### B5. Quality

A projected layer is a resampled bitmap. Text and edges on the moving leaf are
bilinear-filtered, as in browser compositors. They are sharp at rest, because
an identity or translation blit is 1:1. Capturing at up to 2× the device scale
bounds the softness during the turn. A layer whose current scale exceeds its
capture scale recaptures when the motion settles.

**Expected effect (book):** inside the damaged ~30%, the frame paints:
- the static page under the leaf, itself a layer at rest (identity
  transform: a 1:1 blit);
- the gutter;
- one projected leaf bitmap.

The estimate is ~20 ms (after A) → 6–10 ms at 1×. That meets the 16 ms target
at 1× and approaches it at 2×. To be confirmed by measurement (§6).

## 5. Out of scope (stage C, deferred)

These remain deferred until A and B are measured:
- a compositor thread;
- GPU compositing of layers;
- `will-change` layer promotion beyond Q2;
- tiled replay with glyphs (RAD_12 §9.5).

## 6. Verification plan

- **Parity.**
  - For A, render the same frame damage-limited and full; the surfaces must be
    identical.
  - For B, a layer paint against a direct paint:
    - identical at whole-pixel identity or translate placement;
    - within one level per channel and 0.1% of ink under projection, as for
      SVG layers.
  - Fixtures:
    - a 2D translate;
    - a 3D `rotateY` under perspective;
    - `preserve-3d` with backface faces;
    - opacity;
    - a mutation inside a layer (must recapture);
    - hover inside a layer;
    - a font or image arriving after capture.
- **Switches.**
  - `RADIANT_PAINT_LAYER=off|eager`, mirroring `RADIANT_SVG_LAYER`;
  - `RADIANT_SELECTIVE_SIM=1` for UI replays on the selective path;
  - one env knob to force full repaint, for A/B timing.
- **Gates.**
  - `make test-radiant-baseline` must pass in full;
  - the slide UI suite (presentation writes are its main path);
  - the photo-album replays;
  - `test_retained_display_list_gtest`, extended for layer items.
- **Performance.** Use `benchmark.py --book` with a release host at 1× and 2×.
  Record per-frame record, replay, upload and total time before and after each
  stage, plus layer memory.
- **Path traces.** Extend `[RENDER_PATH]` with the damage share and layer
  hits, misses and captures, so the path taken can be confirmed after the fact
  (RAD_13 §9).

## 7. Staging and effort

| Step | Change | Size | Depends on |
|---|---|---|---|
| A1 | Old/new visual bounds for presentation writes; per-view last bounds | Medium (event.cpp, recording bookkeeping) | — |
| A2 | Split `is_dirty`/`full_repaint`; audit region-marking callers | Medium, wide reach | A1 for the paint-only caller |
| A3 | Persistent texture plus sub-rect upload | Small (window.cpp) | A2 |
| B1–B4 | Paint-layer registry, stamp, capture, `DL_DRAW_LAYER`, css3d plane integration | Large | A (layers pay off most when damage is bounded) |
| Docs | RAD_12 §5–6, RAD_13 §7, RAD_16 §3; this doc's checkpoints | Small | each step |

A is useful on its own for every presentation animation: slides, CSS-driven
transforms written by packages, and the album. B builds on A.

## 8. Open questions

| ID | Question | Recommendation |
|---|---|---|
| Q1 | Order: A then B, or A only for now? | A first, measured, then B. A is lower risk and benefits all presentation animation. |
| Q2 | Layer eligibility: automatic for presentation-written roots, opt-in by `will-change: transform`/`opacity`, or both? | Both. A presentation-written root qualifies after two identical paints; `will-change` captures eagerly. Radiant does not parse `will-change` today, so it would join the support matrix. |
| Q3 | Is bilinear softness on moving layers acceptable (sharp at rest)? | Yes. It matches browser compositors; capture up to 2× device scale. |
| Q4 | Memory: a separate budget for HTML layers, or one shared with SVG layers (48 MB per layer, 256 MB per document)? | One shared raster-layer budget per document, with the SVG limits. |
| Q5 | A2 makes existing region-marking callers (incremental commit, hover, animation ticks) repaint selectively for the first time. Admit them all in A2, or only the paint-only commit first? | Paint-only commit first. Each other caller is admitted after its own parity test. |
| Q6 | Headless replays: keep forced full renders by default? | Yes, to keep goldens stable. Use the opt-in env knob for selective replays. |

---

## Stage A checkpoint — 2026-10-21

Stage A is implemented. A1, A2 (paint-only commits only, per Q5) and A3 are
done. Stage B is held until A is reviewed. Nothing is committed.

### What was built

- **Damage registry.** `radiant/presentation_damage.cpp` tracks the elements a
  paint-only commit writes. It keeps a `DomNodeRef` and the bounds each element
  was last painted at, in `DocState::presentation_damage` (32 entries,
  least-recently-written replacement, idle entries pruned after 600 frames).
- **Bounds from recording.**
  - `render_element_marker_end` notes a tracked element's bounds as the union
    of its recorded items, not its marker. The marker starts from the
    untransformed layout box, which a turned leaf no longer covers.
  - Painting ops are all `DIRTY_SKIPPABLE`, with device-space projected
    bounds. An unbounded item (a plane crossing the viewer) makes the frame
    full.
  - Markers inside a `preserve-3d` context are recorded before composition, so
    each leaf gets its own bounds. No compose hook was needed.
- **Commit (A1).**
  - The paint-only commit marks each target's old bounds, rounded out to whole
    device pixels plus one, and requests a region repaint
    (`doc_state_request_region_repaint`), which leaves `is_dirty` alone.
  - A target never painted, or a full repaint already pending, falls back to
    the old full path.
- **Render (A1/A2).**
  - A presentation frame records the whole tree first, then adds each target's
    new bounds, then clears selectively.
  - Video and unbounded planes, an export target, or any `is_dirty` request
    make it full.
- **Present (A3).** One GL texture persists. A frame whose writes all lay in
  the damage union uploads only that rectangle (`glTexSubImage2D`); anything
  else uploads everything.
  - `UiContext::present_partial`/`present_rect` accumulate across renders
    between presents.
  - The cached video blit resets them to a full upload.
- **Switches.**
  - `RADIANT_PRESENTATION_DAMAGE=off` disables the path.
  - `RADIANT_SELECTIVE_SIM=1` lets headless replays repaint selectively; they
    still render in full by default, so goldens are unchanged (Q6).
  - `RADIANT_DAMAGE_VERIFY=1` re-renders each selective frame in full and
    compares.
  - The `assert_damage_parity` sim event checks the totals.

### Where implementation differed from §3, and why

1. **`is_dirty` came from the presentation write itself.**
   `dom_presentation_style_set_property` → `dom_mutation_notify` →
   `doc_state_request_reflow` sets `is_dirty`, and the paint-only commit only
   withdrew the reflow.
   - The batch now records `dirty_before_batch` and
     `full_repaint_requests_before_batch` at its first mutation, next to
     `reflow_pending_before_batch`.
   - The commit withdraws `is_dirty` only when the document was clean before
     the batch and no full repaint was requested since.
   - Every other way of setting `is_dirty` now bumps
     `DocState::full_repaint_requests`: `doc_state_mark_dirty`,
     `doc_state_request_repaint`, and the 22 direct writes in
     `state_store.cpp` and `resource_loaders.cpp`.
2. **The frame tick forced a full repaint.** `radiant_tick_document_animations`
   requested a repaint after every frame-callback tick. It now requests a
   region repaint when presentation damage is pending and no full request was
   made during the tick. Canvas, scene3d, CSS animations and non-paint-only
   commits all request full repaints themselves, so they still get one.
3. **The dirty clip could not give parity.**
   - `dl_replay_push_dirty_clip` pushes a rectangle path, which ThorVG applies
     as a clip mask.
   - Blending through the mask shifted repeating-gradient stripes by up to 96
     levels inside the region (none outside).
   - The damage union is instead replayed as **one tile**
     (`render_output_replay_region`): the region plus a 4 px margin, on a tile
     worker (`dl_replay_tile`), copied back into the surface.
   - The tile edges clip, so no mask is involved.
   - The old clip-mask replay remains the fallback when no tile worker exists
     (`RADIANT_RENDER_THREADS=1`).
4. **Parity is exact outside the union, perceptual inside.**
   - The scaled-image rasteriser steps its samples from the surface origin, so
     a tile at another origin samples isolated pixels at 32- and 128-pixel
     intervals slightly differently: up to 15 levels in the book, 31 in the
     fixture.
   - Two full renders of the same frame are identical, so this is
     origin-dependent rasterisation, not stale content.
   - The check is: no pixel outside the repainted union may differ, and none
     inside may exceed the YIQ threshold `assert_snapshot` uses (pixelmatch
     0.1).
5. **Coordinate spaces.** Presentation damage is marked in surface CSS px
   (`dirty_mark_surface_rect`, clipped to the surface band), the space
   selective clear and replay consume. `dirty_mark_element` marks document
   coordinates clipped against the scrolled viewport, which disagrees with the
   clear when the root scrolls. That caller is not admitted (Q5), and its
   mismatch is recorded here for its own audit.

### Verification

- **Parity replays** (`RADIANT_DAMAGE_VERIFY=1`):
  - `test/ui/presentation_damage.json`: translate over gradients and text, an
    opacity fade, a `preserve-3d` card with backface-hidden faces, a box
    entering from off-screen, and an inline-style write every eighth frame
    that falls back to full. 47 of 47 presentation frames were selective,
    median damage share 0.51.
  - `test/ui/photo_album/book_damage.json`: three page turns, 90 of 90 frames
    selective, damage share median 0.30 (0.09–0.40).
  - Both: zero pixels differ outside the union, none perceptibly inside.
- **Existing suites.** The album, slide, Finder and restyle replays pass with
  the default full sim renders.

### Measured (release host, full corpus, 1440×900 at 1×, 8 page turns)

| Frame gap within a turn | Median | Max |
|---|---|---|
| Before (`RADIANT_PRESENTATION_DAMAGE=off`) | 88.9 ms | 248 ms |
| After (`RADIANT_SELECTIVE_SIM=1`) | 62.3 ms | 196 ms |

That is a 30% reduction, not the ~65% the 30% damage share suggested. A sample
of the selective frames shows why:
- recording the whole tree is ~18% of a render;
- ~80% is the tile replay of the damaged region.

That region holds nearly all the expensive pixels: the turning leaf's
projected photos and the page beneath. Stage A removes the static chrome and
the far page from each frame, but the costly content sits inside the damage.
That is stage B's target, so A's measurement supports B.

**Not measured:** the live-window present (A3). Headless replays have no
present step. At 1× a full upload is ~5 MB per frame; at 2× it is ~20 MB, and
A3 uploads ~30% of that.

### Measured in a live window (Retina 2×, 1440×900, release host)

A live window was driven by an event file with real-time `wait` events, and a
temporary trace logged each main-loop iteration. Before the fixes below, a turn
painted **4–8 frames**, 130–280 ms apart. The 550 ms turn clock was used up
before the region path ever ran:
- ~120 ms of click handling;
- a full repaint of ~250 ms, because the turn mounted the next leaf and wrote
  inline `z-index`;
- a second full repaint, because a shade layer was written for the first time
  and had no recorded bounds yet.

Fixes:
- **Engine:** `presentation_damage_note_candidate` (called at an element's
  display-list marker) tracks any element that carries a presentation
  `transform` or `opacity` from its first recording. Its old bounds are then
  known on its first write, which no longer forces a full repaint. Only real
  writes evict entries; a full table skips candidates.
- **Demo:** the turn's first frame now follows the click by 3 ms instead of
  56–66 ms:
  - the book pre-mounts one leaf ahead;
  - each stack's top leaf rests at its lifted `z-index`, so starting a turn
    writes no inline style;
  - the turn clock advances at most 50 ms per frame, so a slow frame slows the
    turn instead of skipping it.

After the fixes, a turn paints **14–17 frames**, all on the region path except
the landing. The median frame gap is 47 ms on a quiet machine (55–65 ms under
load). Remaining per-frame cost:
- region tile replay on one worker: paper gradients and the leaf's projected
  photos;
- recording, of which about two-thirds is re-blurring each photo's outer
  `box-shadow` over its whole rectangle, even though an outer shadow never
  paints inside the border box.

The region replay cannot fan out across workers while the list has glyphs
(RAD_12 §9 item 5). Stage B (a retained raster of the static spread) and an
edge-band, cached outer-shadow blur are the next steps.

## Stage B checkpoint — 2026-10-21

Stage B is implemented as designed in §4, with the deviations below. It is
switched by `RADIANT_PAINT_LAYER=off|eager`.

### What was built

**B1: layer unit.** `radiant/render_paint_layer.cpp`, called from
`render_block_view_content`.
- A layer root is a flat element moved by a presentation transform: either its
  own, or, inside a `preserve-3d` context, an ancestor's up to the context root.
  So each plane, a face or a shade, is a unit.
- Ineligible roots:
  - roots with a presentation `filter`;
  - subtrees holding video, audio, canvas, an iframe, embed, object, scene3d or
    webview;
  - subtrees with CSS or web animations or transitions;
  - subtrees using `mix-blend-mode` or `backdrop-filter`.
- Ineligibility is decided once per stamp.

**B2: change signal.**
- The existing `DomElement::svg_layer_generation` became `layer_generation`,
  shared with SVG layers. A new field would have broken the `DomElement` size
  ratchet (376 bytes).
- `dom_layer_note_mutation` bumps it on every element at or above a mutation,
  except the target of a non-inherited presentation write.
- SVG layers keep their meaning: their bitmap never holds the root's own
  transform, opacity or filter.
- The stamp adds:
  - `style_content_epoch`;
  - `doc_state_content_version`;
  - the document layout count;
  - glyph, font-resource and image-resource generations;
  - raster scale;
  - the plane's painted extent (`css3d_block_paint_rect`, factored out of
    `Css3dPaintContext::enter`).

**B3: capture.**
- The root's subtree is recorded through its ordinary paint pipeline into an
  offscreen list. The root's own transform is cancelled by starting from its
  inverse (`rdt_matrix4_invert`, new), so every descendant composes exactly as
  on screen.
- The list is rasterised by `render_layer_rasterize`. That function was
  extracted from the SVG layer and is shared with it: one premultiplied pass, or
  the black and white passes.
- Capture happens on the second identical paint, at most two per frame.
- Surfaces are document-owned with a 48 MB per-layer and 256 MB per-document
  budget (Q4: same limits, separate registry). Idle surfaces are freed when
  room is needed.

**B4: paint.**
- One image item is drawn under the root's own pushed transform, inside the
  root's marker and the plane's run, so css3d ordering and backface culling are
  unchanged.
- The root's own opacity is applied when the image is drawn, not captured
  (`RasterRenderContext::paint_layer_root` skips its opacity group). A shade's
  per-frame opacity therefore leaves its layer valid; a group composites as one
  image either way.

**B5: rest and motion.**
- A translation placement at rest is captured with its sub-pixel fraction
  folded into the capture origin, and drawn 1:1 with nearest sampling.
- While the translation changes, and under any projection, the existing capture
  is drawn resampled (bilinear).
- A resting layer whose alignment no longer holds recaptures.

**Damage.**
- Switching a root between its live paint and its layer, or a new capture,
  damages the root's projected extent. The two representations differ by
  resampling and alpha rounding, so a selective frame must not keep the other
  one's pixels.

**Stage A fix found by B's parity work.**
- A presentation write to a plane of a `preserve-3d` context now damages the
  whole context: `damage_context_root`, with context roots tracked as
  candidates.
- Composition orders and splits every plane against the others, so a moving
  plane can change another plane's clip seams. Making the shades separate
  planes exposed one-column seams on the static page.

**Demo.**
- Each leaf's shades are sibling planes of its faces (`mod_book_ui.ls`,
  `photo-album.css`), so their per-frame opacity no longer changes the face
  layers.

**Tests.**
- `event_sim` adds `RADIANT_LAYER_VERIFY=1` and `assert_layer_parity`
  (`min_frames`, `min_exact`, `max_mean`).
- Damage verification holds captures (`PAINT_LAYER_HOLD_CAPTURES`), so it
  compares repaint scope only. The frame cache keys on the layer override.
- `test/ui/paint_layer.json` reuses `presentation_damage.ls`, which gained a
  text mutation inside the slider every tenth frame and a slider hover.
- `test/ui/photo_album/book_layers.json` covers the book.

### Where implementation differed from §4, and why

- **Parity bar (§6).** "Identical at whole-pixel placement; within one level
  and 0.1% of ink under projection" did not hold, for three reasons found by
  measurement:
  1. The plane clip antialiases the layer's already antialiased edge, so even a
     1:1 face differs on its edge column.
  2. Fine textures (the cover's 2 px cloth stripes) land at another sub-pixel
     phase when resampled: up to 7% of pixels perceptibly different, yet
     visually identical.
  3. The live 3D path itself misplaces a mirrored repeating gradient. A back
     face under a −180° leaf draws its stripes one pixel off a flat paint of
     the same geometry. Repro: a 240×160 face with 45° 4 px stripes as the back
     face of a `preserve-3d` card at `rotateY(-180deg)`, against a flat box at
     the landed position. This is not a layer issue: the layer, captured
     unmirrored, matches the flat paint.

  So the assertion bounds the frame's mean channel difference (2.0 for the
  synthetic fixture, 4.0 for the book) and reports per-pixel figures, with 1:1
  frames separately.
- **No `DL_DRAW_LAYER` op.** The existing image item carries the layer, with
  the surface as its resource owner. A replay that outlives a recapture reads
  through the handle; destroy defers while read.
- **No `will-change` (Q2).** Presentation-written roots only.
- **Capture scale** is the device scale, not up to 2× (B3). That bounds layer
  memory: a page face is about 1.5 MB at 1× and 6 MB at 2×.

### Verification

- Fixtures pass: `paint_layer`, `photo_album_book_layers`,
  `presentation_damage` and `photo_album_book_damage`, the last with layers
  both on and off.
- Book layer parity: 116 frames, 24 at 1:1, worst mean 2.85, worst perceptible
  share 0.13%.
- Synthetic layer parity: 49 frames, worst mean 1.34.

### Measured (release host, full corpus, 1440×900, 8 turns; machine under load from other sessions)

| Frame gap within a turn | Stage A only (`RADIANT_PAINT_LAYER=off`) | A + B |
|---|---|---|
| Headless 1× (`RADIANT_SELECTIVE_SIM=1`) | 72–75 ms | 45–46 ms (−38%) |
| Live window, Retina 2× | 89–92 ms | 86 ms (−5%) |

Stage A alone is now slower than at its checkpoint (62 ms headless): it
repaints the whole 3D context. The B estimate of 6–10 ms (§4) is not met. A
live-window sample with layers shows where the time remains:
- **Layer draws are not plain copies.** At rest a face is a 1:1 image, but it
  goes through ThorVG's clipped scaled-image path (`_rasterScaledRleImage`),
  because each plane is drawn inside its css3d polygon clip.
- **The turning leaf** is one projected image (`render_image_project_pixels`)
  at 2×, which costs about as much as its live content did.
- **Shade planes** are full-page projective gradient fills.
- **Region replay** still runs on one tile worker (the glyph gate,
  RAD_12 §9 item 5), and the whole spread is now in the region.
- **Captures** (about three per turn) land on the frames after a turn starts.

Candidate next steps:
- a 1:1 blit fast path for an exact layer under an axis-aligned plane clip (done,
  below);
- skipping the polygon clip for an unsplit plane (done, below);
- multithreaded region replay;
- shades drawn as layers' tint parameters rather than gradient planes.

### Fast path for layers at rest (2026-10-21)

- **`rdt_draw_image`.** A 1:1 image at a whole-pixel translation
  (`rdt_image_is_pixel_copy`, shared in `view.hpp`) is composited directly from
  its source under rectangular clips. Before, every pixel was first resampled
  into a projection buffer.
- **`Css3dPaintContext::compose`.** The polygon clip is dropped for a fragment
  that is its whole plane rectangle, axis-aligned (neither split nor cut by the
  viewport), when the run's only paint is such 1:1 copies. Live content keeps
  its clip. A layer's edge pixels now get one antialias instead of two, so the
  1:1 frames differ from live paint by up to 27 levels on the plane's edge
  column.
- Parity is unchanged otherwise: book worst mean 2.85, synthetic 1.34. All four
  damage and layer fixtures pass.

| Frame gap within a turn (release, same binary, under load) | Layers off (stage A) | Layers on, before fast path | Layers on, with fast path |
|---|---|---|---|
| Headless 1× | 70–75 ms | 45 ms | 35 ms (−50%) |
| Live window, Retina 2× | 89–92 ms | 86 ms | 71 ms (−21%) |

The live sample now shows 1:1 layer copies at about 1% of a frame (they were
about 25%). The remaining costs are:
- the shade planes' projective gradient fills (~30%);
- the turning leaf's projected image, sampled and then composited through
  ThorVG under its polygon clip (~40%);
- the radial desk backdrop inside the region (~8%);
- captures after a turn starts (~10%).

### Remaining frame costs, and what removed them (2026-10-21)

A per-draw trace of a live frame, made after the fast path, found that most of
the steady cost was neither shades nor layers.

| Finding | Change | Window 2× median |
|---|---|---|
| About 14 library thumbnails (scaled, under rounded clips) were repainted every frame under the opaque full-window book. | Demo: `.album[data-book="true"] > .sidebar, > .workspace { visibility: hidden }`. The library keeps its layout and scroll position. | 77 → 42 ms |
| A shade plane as a layer is a full-page projected bitmap at 2×. Live, it is one gradient. | A capture recording at most two painting items is kept live (`PAINT_LAYER_TRIVIAL_ITEMS`). | 42 → 34 ms (layers now beat no layers at 2×: 34 against 36 ms) |
| The projected turning leaf was composited through ThorVG under its polygon clip. | `SCALE_MODE_LINEAR_EDGE`: the projection sampler antialiases the quad's own edges. Compose drops the clip for a whole plane whose run holds only layer images (the whole-plane test now compares the vertices' plane coordinates with the rectangle's corners, under any transform). | neutral at 2× (the composite was not the bottleneck once the library was hidden); kept for its cheaper path |
| Every landing changes `style_content_epoch`, `doc_state_content_version` and the layout count, and every photo decode changes the image generation. Each one re-arms every layer. | **Revalidation.** On a stamp change a layer re-records its subtree and keeps its surface when the recording's content fingerprint (`dl_content_fingerprint`) is unchanged. A glyph-cache change still recaptures. Frame-local gradient images are folded by pixel content. | — |
| Captures landed on the first frames of the next turn. | **Idle capture frames.** After interaction stops, the window gives pending layers (armed, or resting misaligned) a damage-free presentation frame (`render_paint_layers_idle_frame`), which records and captures but replays and uploads nothing unchanged. Realignment releases a surface only when that frame can recapture it. | turn-start frames 140–190 → 49–75 ms; mid-turn peaks 139 → 80 ms |

**Measured after all of the above** (release, full corpus, 1440×900, under
background load):
- Live Retina window: median frame gap 33.5 ms (about 30 fps), 16–22 frames per
  550 ms turn.
- Headless at 1×: 18 ms with layers, 51 ms without.
- The first turn after the book opens still has one frame of about 290 ms: the
  first captures plus the first decodes of that spread's photos.

**Open:**
- The first-turn spike.
- The remaining 2× per-frame cost: the projected turning face, region replay on
  one worker (RAD_12 §9 item 5) and the radial desk backdrop.
- The live 3D path's mirrored-gradient offset (above).

### The two open issues, fixed (2026-10-21)

**Mirrored repeating-gradient offset.** This was not a layer defect.
- The analytic gradient path accepted only matrices whose `e31`, `e32` and
  `e33` were exactly affine. The vector backend calls a matrix projective only
  above a 1e-6 tolerance (`matrix_is_projective`).
- A back face under a −180° leaf composes to a translation with about 1e-18 of
  perspective residue. That fell back to ThorVG's gradient, which places a 4 px
  stripe period one pixel off.
- `rdt_fill_linear_gradient_analytic` now uses the backend's tolerance and drops
  the residue. The mirrored face matches a flat paint except its plane-clip edge
  column.
- Book layer parity improved: worst mean 2.85 → 0.77 while the endpaper was
  live, and 1:1 frames 27 → 81.

**First-turn spike** (first two frames 473 + 293 ms → about 245 + 49 ms; later
turn starts unchanged at 60–85 ms):
- **Fingerprint stability.** Revalidation kept reporting the cover face as
  changed, because text-shadow rasters are new surfaces on every recording. The
  fingerprint compared their handles. It now folds every image by the pixels it
  draws (`dl_draw_image_resolve`, inside an image read scope), so identical
  content keeps its layer.
- **Trivial planes judged by cost.** A plane with at most two painting items
  stays live only when they are cheap: no image, no gradient with more than 4
  stops. The endpaper face (a background plus a 363-stop repeating texture) was
  being kept live and re-rasterised analytically on every frame.
- **Transformed box-shadow cache.** Shadows under a transform are rasterised at
  record time (blur plus per-pixel border-box exclusion). A document-owned LRU
  (`services.shadow_image_cache`: 64 entries, 64 MB) keys them by geometry
  relative to their pixel origin and records them by handle, so re-recording a
  face no longer re-blurs every photo shadow.

**Left in the first turn:** about 245 ms of first-time work:
- a relayout from the button's interaction state;
- the first paint of the uncovered leaf, with its photo decodes;
- the window replay.

Idle frames could paint and capture the next leaf ahead of the first click;
that is not done.

**Note on measurements:** `origin/master` was merged on top of the stage A+B
commit (d208dba92). Afterwards, frames in the same live-window test ran slower
(median about 55 ms against 33 ms before, under similar load). In the merged
tree, `photo_album_collage` fails its undo step in both debug and release, with
or without these changes, so that failure belongs to the merge.


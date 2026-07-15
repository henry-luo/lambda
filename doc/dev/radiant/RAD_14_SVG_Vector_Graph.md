# Radiant — SVG, Vector Graphics & Diagram Layout

> **Part of the [Radiant detailed-design set](RAD_00_Overview.md).** This document covers three cohesive sub-areas that share one paint pipeline: the `RdtVector` immediate-mode vector API and active ThorVG backend, with an excluded CoreGraphics implementation retained for future exploration; the inline-SVG renderer that walks a *Radiant-parsed* SVG element tree and records it into that API (plus the easily-confused opposite-direction view-tree→SVG-text serializer); and Lambda graph layout whose routed edges enter Radiant as generated SVG paint layers.
>
> **Primary sources:** `radiant/render.hpp`, `radiant/rdt_vector_tvg.cpp`, `radiant/rdt_vector_cg.mm`, `radiant/render_svg_inline.cpp`, `radiant/render_svg.cpp`, `radiant/render_vector_path.cpp`, `radiant/render_path.cpp`, `lambda/package/graph/layout.ls`, `lambda/package/graph/dagre.ls`, `lambda/package/graph/transform.ls`, and `radiant/graph_bridge.cpp`.
> **Audience:** engine developers. **Convention:** `file:line` references drift; confirm against the symbol name.

---

## 1. What this area is

Everything Radiant paints as a vector — CSS backgrounds and borders, block clip paths, inline `<svg>`, standalone `.svg` files, and auto-laid-out `.mmd`/`.d2`/`.dot` diagrams — funnels through one narrow immediate-mode surface, `RdtVector` (`render.hpp`). That surface is the whole point of the design: Radiant code calls `rdt_path_*`/`rdt_fill_*`/`rdt_stroke_*`/`rdt_push_clip`/`rdt_picture_*` and never touches ThorVG or CoreGraphics directly. The header states the rule literally (`render.hpp`): "All Radiant rendering code calls rdt_* functions — never tvg_* directly." The one licensed exception is `render_svg_inline.cpp`, which may bridge a `Tvg_Paint` into an `RdtPicture` for text/image glyph runs (`rdt_picture_take_tvg_paint`, `render.hpp`, guarded by `#ifndef LAMBDA_HEADLESS`).

Three sub-areas share this surface and are documented in turn:

1. **The `RdtVector` abstraction** (§2–§3) — the API, active ThorVG implementation, retained CoreGraphics exploration source, and capability table that lets callers degrade gracefully.
2. **SVG** (§4–§5) — the inline-SVG renderer (SVG *input*) and, separately, the view-tree→SVG-text serializer (SVG *output*); these run in opposite directions and are easy to confuse.
3. **Graph / diagram layout** (§6–§8) — a Dagre-inspired Lambda layout over measured rich HTML nodes, with routed links lowered through generated SVG subscenes.

The recording substrate they share — PaintIR and the DisplayList — is owned by [RAD_12 — Paint IR & Display List](RAD_12_Paint_IR_Display_List.md); the render walk that drives it is [RAD_13 — Render Walk & Painters](RAD_13_Render_Walk_Painters.md). This doc describes the seams into those, not their internals.

---

## 2. The `RdtVector` abstraction and backend sources

<img alt="RdtVector API, active ThorVG backend, and retained CoreGraphics source" src="diagram/rad14_rdtvector_backends.svg" width="720">

### 2.1 The API surface

`struct RdtVector` (`render.hpp`) is a one-field wrapper around an opaque `RdtVectorImpl*` bound at `rdt_vector_init` to a **caller-owned ABGR8888 pixel buffer** (stride in pixels, not bytes — `render.hpp`). Around it the header declares the full immediate-mode vocabulary: path construction (`rdt_path_new`/`rdt_path_move_to`/`rdt_path_cubic_to`/`rdt_path_add_rect`/`rdt_path_add_circle`, `render.hpp`), fill/stroke (`rdt_fill_path`/`rdt_fill_rect`/`rdt_stroke_path`, `:171-189`), gradients (`rdt_fill_linear_gradient`/`rdt_fill_radial_gradient`, `:195-207`), nested alpha-mask clipping (`rdt_push_clip`/`rdt_pop_clip` plus save/restore depth, `:215-222`), image blit (`rdt_draw_image`, `:230`), and SVG-picture loading/drawing (`rdt_picture_*`, `:243-258`). A portable path can also be inspected without knowing the backend via `rdt_path_visit` over the `RdtPathCommand` stream (`render.hpp,165`).

`RdtPath` and `RdtVectorImpl` are opaque (`render.hpp,66`); each backend defines the concrete struct. `RdtMatrix` (`render.hpp`) is a 3×3 affine deliberately laid out identically to `Tvg_Matrix`, with inline `rdt_matrix_identity`/`rdt_matrix_multiply`/`rdt_matrix_translate` helpers (`render.hpp`) so transform composition needs no backend call.

### 2.2 ThorVG is active; CoreGraphics is retained but excluded

There is **no runtime dispatch**. Both translation units define the complete `rdt_*` symbol set, so only one could be linked into a build. The current build configuration always compiles `rdt_vector_tvg.cpp` and lists `rdt_vector_cg.mm` in `exclude_source_files`, including the Jube overlay. CoreGraphics is intentionally not compiled or tested; its source is retained only for future backend exploration. ThorVG is therefore the sole active vector backend on every platform.

Callers still must not branch on backend identity. The active implementation publishes an immutable `RdtVectorCaps` table (`render.hpp`), returned by `rdt_vector_get_caps`, and optional-feature code gates on the flag. The retained CoreGraphics source follows the same contract so it can be evaluated later without changing callers. The table advertises `vector_paths`, `rounded_rects`, `gradients`, `nested_clips`, `image_scaling`, `picture_svg`, `picture_duplication`, `svg_dom_pictures`, `opacity_group`, `blend_modes`, `gaussian_blur`, `color_matrix_filters`, `native_text_runs`, `vector_batching`, `premultiplied_surface`, `tile_offsets`, and `clip_depth_save_restore`.

In the active ThorVG backend, native `gaussian_blur` is advertised only under `#ifdef __APPLE__`; the native CSS-filter blur path therefore degrades on Linux/Windows. This does **not** mean SVG blur is gone on those platforms — see the important clarification in §5.4. Any capability claims in the excluded CoreGraphics source are exploratory until that backend is deliberately reactivated and tested.

### 2.3 ThorVG backend internals

`rdt_vector_tvg.cpp` wraps the ThorVG C API (`thorvg_capi.h`). `rdt_vector_init` creates a software canvas targeting the caller's ABGR8888 buffer (`rdt_vector_tvg.cpp:800`). Two performance structures matter. First, a **content-hash paint cache** dedupes repeated fills/strokes/gradients: `rdt_paint_hash_path`/`rdt_paint_hash_common` (`rdt_vector_tvg.cpp:190,202`) key a mutex-guarded cache (`g_paint_cache_mutex`) so an identical path re-issued across frames need not be rebuilt. Second, clipping is emulated: ThorVG has no clip stack, so the backend maintains a **thread-local mask stack** of fixed depth (`RDT_MAX_CLIP_DEPTH 8`, `rdt_vector_tvg.cpp:1461`; `struct ClipEntry`, `:1463`; `rdt_push_clip`/`rdt_pop_clip`/`rdt_clip_save_depth`/`rdt_clip_restore_depth`, `:1474-1516`) and applies each active clip as a per-shape alpha mask at draw time. The header comment (`:1441-1458`) documents this design and the tight push→draw→pop bracketing it assumes.

### 2.4 Retained CoreGraphics exploration source

`rdt_vector_cg.mm` is excluded from compilation but retained for future exploration. It targets a `CGContextRef`, keeps a private premultiplied backing surface while preserving Radiant's straight-alpha ABGR public contract, defers flushes through `batch_depth`, and flips CoreGraphics' y-up CTM to Radiant's y-down coordinates. These internals are design notes, not a maintained capability guarantee, until the source is intentionally brought back under a compile/test target.

---

## 3. Paths and pictures on top of the abstraction

Two small files build geometry through the `RdtVector` API. `render_path.cpp` constructs clip/border geometry: `render_path_create_rounded_rect` (`render_path.cpp:7`) emits a per-corner rounded rectangle using the circle-approximation constant `RENDER_PATH_KAPPA` (`render_path.cpp:5`), and `render_path_create_clip_path` (`:65`) derives the current block's clip rectangle (honoring `has_clip_radius`). `render_vector_path.cpp` renders a CSS `VectorPathProp` — a block whose `vpath->segments` linked list carries `VPATH_MOVETO`/`LINETO`/`CURVETO`/`CLOSE` — into an `RdtPath` and then strokes/fills it through the render context (`render_vector_path`, `render_vector_path.cpp:6`).

SVG *pictures* — standalone `.svg` files and offscreen scenes — are loaded through `rdt_picture_load`/`rdt_picture_load_data` (`render.hpp`). Critically, **Radiant parses the SVG itself**, not ThorVG's loader: `svg_picture_create` (`rdt_vector_tvg.cpp:1646`) calls `html5_parse_svg_document` (`:1667`) to produce a `KIND_SVG_DOM` picture holding a Radiant-parsed `Element` root plus an owned `Pool` (`RdtPicture::Kind`, `rdt_vector_tvg.cpp:60`; the file comment at `:1710` states "the SVG path is parsed by Radiant (not ThorVG)"). Loaded pictures are cached by path under `g_picture_cache_mutex` (`rdt_vector_tvg.cpp:101,286`). When drawn (`rdt_picture_draw`), a `KIND_SVG_DOM` picture is replayed through the same DisplayList path as inline SVG (`:1894`), so file-SVG, inline-SVG, and PDF export (`render_pdf.cpp`) all share one renderer. `rdt_picture_get_svg_root`/`rdt_picture_find_svg_element_by_id` (`render.hpp`) expose that parsed tree for scripting.

---

## 4. Inline SVG — walking a Radiant-parsed element tree

`render_svg_inline.cpp` (5633 lines) is the real SVG feature implementation. It does not use ThorVG's SVG loader; it walks the `Element*` tree Radiant already parsed and records paint operations. The render walk reaches it through the backend seam: `render_inline_svg` (`render_svg_inline.cpp:5519`) is called from `render.cpp:161`, `render_raster_walk.cpp:53`, and via the backend function pointer `backend->render_inline_svg` in `render_walk.cpp:176`. Layout consults `calculate_svg_intrinsic_size` (`render_svg_inline.cpp:924`) for CSS Images-Level-3 sizing (the `SvgIntrinsicSize` struct, `render.hpp`) from `layout_block.cpp`.

### 4.1 Traversal state and dispatch

All traversal state lives in `SvgInlineRenderContext` (`render.hpp`): the `svg_root` and `Pool`, an optional `FontContext`, the required `DisplayList* dl` and `PaintList* paint_list` record targets, the accumulated `RdtMatrix transform` (viewBox × group × element), viewBox transform fields, inherited paint/text style (fill/stroke/opacity/`current_color`/font), the `defs` `HashMap` of id→definition, an embedded-`<style>` rule cache, and the `suppress_masks` recursion guard.

The core recursion is `render_svg_element` (`render_svg_inline.cpp:4812`), a tag dispatcher over per-tag handlers: `render_svg_rect` (`:1602`), `render_svg_circle` (`:1621`), `render_svg_ellipse` (`:1637`), `render_svg_line` (`:1654`), `render_svg_polyline`/polygon (`:1718`), `render_svg_path` (`:2539`), `render_svg_text` (`:3353`), `render_svg_image` (`:3844`), `render_svg_group` (`:4384`), `render_svg_children` (`:4518`), and `<use>` resolution `render_svg_use_target`/`render_svg_external_use` (`:4570,4632`). `<defs>` is registered by `process_svg_defs` (`:4532`) → `register_svg_def_element` (`:1033`), which stores gradients, clip paths, masks, symbols, patterns, and markers in a `SvgDefTable`.

### 4.2 Geometry, paint, filters, clips, text

Path data is parsed by `parse_svg_path_d` (`render_svg_inline.cpp:2001`), which handles M/L/C/S/Q/T/A/Z and converts elliptical arcs to cubics via `arc_to_beziers` (`:1888`); an unrecognized command is logged, not fatal (`:2263`, "unsupported command"). Transform lists are parsed by `parse_svg_transform` (`:798`) into a 6-float matrix; `points` attributes by `parse_points_to_path` (`:1680`).

Paint dispatch is `draw_svg_fill_stroke` (`render_svg_inline.cpp:1445`), routing to solid fill, `draw_gradient_fill` (`:1329`), `draw_pattern_fill` (`:1377`), or stroke. Filters: `resolve_svg_solid_filter_tint` for `<feFlood>` (`:1182`) and `resolve_svg_gaussian_blur_filter` for `<feGaussianBlur>` (`:1219`), the latter bracketed by `svg_begin_gaussian_blur_filter`/`svg_finish_gaussian_blur_filter` (`:1309,1315`). Clip/mask handling: `resolve_svg_clip_path` (`:4350`) and `build_clip_path_from_def` (`:4276`), with a masked-source repaint path (`render_svg_masked_source_*`, `:4153-4214`) guarded against recursion by `suppress_masks`. Embedded CSS is collected by `collect_svg_style_rules` (`:3063`) + `parse_svg_style_text` (`:461`) and applied with `svg_apply_inherited_paint_attrs` (`:756`). Text uses a dual path: Radiant glyph rasterization (`render_svg_text_with_radiant_glyphs` at `:3212`, `draw_glyph_affine` at `:3189`, with a cmap check `font_file_has_unicode_cmap` at `:2729`) and, when needed, a ThorVG `tvg_text` run bridged into an `RdtPicture` via `rdt_picture_take_tvg_paint` (`:3510` etc.).

### 4.3 Record then replay

Handlers do not blit. They append to a `PaintList`/`DisplayList` through inline wrappers (`svg_fill_path`/`svg_stroke_path`/`svg_fill_linear_gradient`/`svg_draw_picture`, `render_svg_inline.cpp:180-213`) that call the shared `paint_record_*` API ([RAD_12](RAD_12_Paint_IR_Display_List.md)). `render_svg_inline_register_paint_ir_lowerers` (`:5409`) registers the SVG subscene lowerer so a deferred `PaintSvgSubscene` (`render.hpp`) can be expanded later by raster, PDF, and SVG-output backends. `render_svg_to_vec_via_display_list` (`:5446`) is the offscreen entry: it builds the PaintList/DisplayList and replays it into an existing `RdtVector` (with tiling support), so pictures follow the exact same replay path as page raster output rather than emitting immediate `rdt_*` calls.

---

## 5. SVG output — the opposite-direction serializer (do not confuse with §4)

`render_svg.cpp` is a different thing entirely and a frequent source of confusion: it is a `RenderBackend` that serializes the **already-laid-out view tree back out to an SVG *text* document**. It consumes views and produces SVG markup; §4 consumes SVG and produces pixels.

### 5.1 Entry and driver

The entry point is `render_view_tree_to_svg` (`render_svg.cpp:1717`, declared in `render.hpp`), called from `render_img.cpp:501`. It builds an `SvgRenderContext` (`render_svg.cpp:44-66`, a `StrBuf* svg_content` plus font/block/effect state) and wires a standard render-walk backend via `svg_make_backend` (`:1622`). The walk's callbacks emit markup: `svg_cb_render_bound` → `render_bound_svg` (`:788`) for rects/borders/backgrounds, `svg_cb_render_text` → `render_text_view_svg` (`:290`) for `<text>`, borders via `svg_emit_border_side` (`:696`), and inline SVG via `svg_cb_render_inline_svg` (`:1337`), which itself builds a `PaintSvgSubscene` (`:1387`) so nested inline SVG re-uses §4's machinery.

### 5.2 Raster fallback for inexpressible effects

Effects SVG text cannot express (Gaussian blur, blend modes, color-matrix filters) are rasterized to an embedded `<image>`: `svg_begin_effect_raster_fallback`/`svg_finish_effect_raster_fallback` (`render_svg.cpp:232,248`) capture the effect group and `svg_emit_raster_fallback_image` (`:214`) encodes it as a base64 PNG via `svg_encode_surface_png` (`:161`). This is the output analog of the caps-driven degradation in §2.2 — the serializer honors the same "SVG can't do this" boundary by falling back to pixels.

### 5.3 Both SVG directions share the subscene builder

`PaintSvgSubscene` (`render.hpp`) is the shared unit: raster (`render_raster_walk.cpp:53`), PDF (`render_pdf.cpp`), and this SVG-output backend (`render_svg.cpp:1387`) all defer inline-SVG through the same builder, which keeps one code path for SVG regardless of the final target.

### 5.4 Important: SVG `<feGaussianBlur>` is not gated by the backend blur cap

The `gaussian_blur` capability flag (§2.2) describes the **vector backend's native filter blur**, consumed by the CSS filter/backdrop path (`render_filter.cpp:208`, `render_backend_caps.cpp:10`). Inline SVG's `<feGaussianBlur>` takes a *different* route: `svg_finish_gaussian_blur_filter` records a `box_blur_region` DisplayList op (`svg_box_blur_region`, `render_svg_inline.cpp:239,1317`) that is replayed in **software** by `dl_replay_box_blur_region` (`display_list_replay_effects.cpp:28`), with no `__APPLE__` guard. So inline-SVG blur works on all platforms; only the backend-native CSS-filter blur is `__APPLE__`-limited for ThorVG. The scan digest's blanket claim ("no Gaussian blur on Linux/Windows") over-generalizes and should be read as this narrower statement.

---

## 6. Graph / diagram layout — semantic HTML and Velmt

<img alt="Inline SVG in, SVG out, and graph to SVG data flows" src="diagram/rad14_svg_dataflows.svg" width="720">

The C syntax parsers produce a Lambda `<graph>` element. `graph.transform.to_html()` normalizes it to a semantic `<graph data-radiant-layout="lambda-graph">` containing measured `<node>` children and zero-size `<edge>` metadata children. Nodes may contain arbitrary block, inline, flex, grid, text, image, or whole-SVG content.

Radiant first lays out every direct child with its normal layout engine. The retained Lambda callback receives ephemeral Velmt handles containing each child's border-box dimensions and attributes. `graph.layout.from_velmts()` builds the canonical map model, calls `graph.layout.compute()`, and returns border-box top-left placements plus routed edge geometry. The callback is pure; registration is the explicit orchestration step.

The resulting document retains its Lambda runtime, heap, JIT code, callback, and generated layer roots. Callback lookup is scoped by the parent document's retained heap plus layout name, so simultaneous documents cannot overwrite one another's `lambda-graph` registration.

---

## 7. The Dagre-inspired hierarchical algorithm — and what it is not

<img alt="Dagre-inspired five phase graph layout" src="diagram/rad14_dagre_phases.svg" width="720">

`lambda/package/graph/dagre.ls` is **Dagre-inspired, not a faithful port of the JS Dagre library**. It normalizes nodes and edges, assigns longest-path ranks, creates layers, performs a barycenter ordering sweep, assigns centered coordinates, transforms those coordinates for TB/BT/LR/RL, clips endpoints to node rectangles, and emits orthogonal paths.

Network-simplex ranking, dummy nodes for long edges, Brandes-Kopf coordinate assignment, shape-specific clipping, parallel-edge separation, self-loop routing, clusters, and edge-label placement are not yet implemented. Long or cyclic graphs can therefore produce weaker ordering and routes than Graphviz or JS Dagre.

---

## 8. Generated SVG paint and CLI flow

`graph.transform.paint` converts routed edge points into immutable `<svg>` elements returned as custom-layout `paint_layers`. Radiant roots those elements for the document lifetime and merges generated layers with normal node child views using one stable signed-z sequence. SVG, PDF, and raster backends consume the same sequence; hit testing consumes it in reverse while skipping generated layers, which are initially non-interactive.

`radiant/graph_bridge.cpp` builds a small Lambda document that imports the transform package, parses the source with the appropriate graph flavor, installs `lambda-graph`, and returns `to_html()`. `render`, `view`, `layout`, and the generic document loader all use this bridge. Final SVG/PDF/PNG/JPEG output is produced by normal Radiant rendering; there is no direct C graph-to-SVG path.

---

## 9. Known Issues & Future Improvements

1. **`render_svg_inline.cpp` is a 5633-line monolith.** It mixes SVG parsing, CSS style resolution, path/arc geometry, gradients/patterns, filters, clips/masks, and text glyph rendering in one file. *Improvement:* split along the natural seams — path parsing, filters/masks, gradients/patterns, and text — into separate TUs sharing `SvgInlineRenderContext`.
2. **Graph layout is Dagre-inspired, not a faithful port** (`lambda/package/graph/dagre.ls`). It still lacks network-simplex ranking, dummy nodes, Brandes-Kopf x-assignment, clusters, parallel-edge separation, and self-loop routing. *Improvement:* normalize long edges through dummy ranks before repeated crossing-reduction sweeps.
3. **Backend caps parity gaps** (`rdt_vector_tvg.cpp:774` vs `rdt_vector_cg.mm:273`). Both leave `opacity_group`, `blend_modes`, `color_matrix_filters`, and `native_text_runs` = false, so those effects silently no-op or fall back to raster (§5.2). The ThorVG native `gaussian_blur` cap is `__APPLE__`-only (`rdt_vector_tvg.cpp:787-791`) — the CSS-filter blur path degrades on Linux/Windows ThorVG builds (but inline-SVG `<feGaussianBlur>` does not; see §5.4).
4. **Fixed clip-stack depth in the ThorVG backend.** `RDT_MAX_CLIP_DEPTH` is hard-coded to 8 (`rdt_vector_tvg.cpp:1461`); overflow is logged and the clip dropped (`:1476`). Deeply nested SVG clip paths beyond depth 8 silently lose clipping.
5. **Two directions named "SVG" are easy to confuse.** `render_svg_inline.cpp` is SVG *input* (element tree → pixels); `render_svg.cpp` is SVG *output* (view tree → SVG text). They share only the `PaintSvgSubscene` builder. *Improvement:* rename `render_svg.cpp` to something like `render_svg_output.cpp` to make the direction unmistakable.
6. **Sparse explicit debt markers.** The only "unsupported" log in the inline renderer is the SVG path parser's unknown-command branch (`render_svg_inline.cpp:2263`); `rdt_picture_load` logs unsupported formats rather than falling back. Most debt here is structural (file size, algorithm scope) rather than tagged with TODO/FIXME.
7. **Manual lifetime bookkeeping around `RdtPicture`.** The path-keyed picture cache plus its mutex (`rdt_vector_tvg.cpp:101,286`) pair with hand-managed `Pool`/`Element` ownership on `KIND_SVG_DOM` pictures — a place to watch for leaks or races under concurrent load.

---

## Appendix A — Source map

| File | Responsibility (this doc) |
|---|---|
| `radiant/render.hpp` | The immediate-mode `rdt_*` API, `RdtMatrix`/`RdtPath`/`RdtVector`, the `RdtVectorCaps` table, and the "never call `tvg_*`" rule. |
| `radiant/rdt_vector_tvg.cpp` | ThorVG backend: SW canvas, content-hash paint cache, emulated clip mask stack, `svg_picture_create` (Radiant-parsed SVG-DOM pictures), `g_tvg_caps`. |
| `radiant/rdt_vector_cg.mm` | CoreGraphics backend: premultiplied backing surface, straight-alpha conversion at flush, batch-depth flush deferral, y-down CTM flip, `g_cg_caps`. |
| `radiant/render.hpp` / `render_svg_inline.cpp` | Inline-SVG declarations and implementation: element dispatch, path/arc/transform parsing, gradients/patterns/filters/clips/masks, dual-path text, and record/replay into `RdtVector`. |
| `radiant/render.hpp` / `render_svg.cpp` | View-tree → SVG-text declarations and serializer (`render_view_tree_to_svg`, `svg_make_backend`) with raster fallback for inexpressible effects. |
| `radiant/render_path.cpp`, `radiant/render_vector_path.cpp` | Rounded-rect/clip path construction and CSS `VectorPathProp` rendering through `rdt_*`. |
| `lambda/package/graph/layout.ls`, `dagre.ls` | Pure canonical graph geometry, ranking, coordinates, and edge routing. |
| `lambda/package/graph/transform.ls`, `transform/*` | Semantic HTML, custom-layout installation, themes, and generated SVG edge layers. |
| `radiant/graph_bridge.cpp` | Shared graph-file to Lambda-document bridge for render, view, layout, and generic loading. |
| `radiant/stacking_order.cpp` | Stable signed-z merge of generated layers and measured node views. |

## Appendix B — Related documents

- [RAD_00 — Overview](RAD_00_Overview.md) — the set index and architecture.
- [RAD_01 — View & DOM Model](RAD_01_View_and_DOM_Model.md) — the parsed `Element`/view tree that both inline SVG and the SVG-output serializer traverse.
- [RAD_12 — Paint IR & Display List](RAD_12_Paint_IR_Display_List.md) — the `paint_record_*` / DisplayList substrate the inline-SVG renderer records into and replays.
- [RAD_13 — Render Walk & Painters](RAD_13_Render_Walk_Painters.md) — the render walk and `RenderBackend` seam that dispatch `render_inline_svg` and drive the SVG-output backend.
- [RAD_07 — Fonts](RAD_07_Fonts.md) — the `FontContext` and glyph pipeline used by SVG `<text>` rendering.

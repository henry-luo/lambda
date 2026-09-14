# Radiant Canvas 2D API

**Status:** partial implementation — C0 and a bounded C1 drawing subset are
implemented. The remaining C1–C4 scope remains a proposal; this document now
records both the accepted placement and the deliberately unsupported surface.

**Date:** 2026-09-13

**Audience:** Radiant, DOM-bridge, and LambdaJS developers

**Scope:** a bounded, portable `HTMLCanvasElement` /
`CanvasRenderingContext2D` implementation. This proposal deliberately does
not propose WebGL, WebGPU, browser Worker integration, or browser-complete
Canvas conformance.

**Formal linkage:** **D7.4.1v2** requires native resources to cross the script
boundary as VMap projections rather than raw pointers; **D7.4.4** requires
declared interfaces and record-owned hooks for every host object; **D7.4.2**
owns async completion delivery; and **D7.5.3** fixes the Lambda↔Radiant
boundary. The per-page ordering assumption is Radiant's existing **RC1**:
script, style, layout, and display-list construction share one page thread.

## 1. Decision and current implementation

Promote Canvas from KIV to a bounded **Canvas 2D, raster-first** milestone.
The canvas bitmap is a retained, document-owned `ImageSurface`; it is the
authoritative Canvas result. The current 2D subset applies completed native
draws to that bitmap under Radiant ownership and paints the bitmap as the
`<canvas>` element's replaced content. Later APIs may use a command buffer
when synchronous local rasterization is no longer sufficient.

Canvas must not call ThorVG directly from a JS binding and must not keep its
state in JavaScript expandos. Both approaches bypass Radiant's retained render
and ownership model, make readback/lifetime ambiguous, and risk corrupting a
frame while a page callback is active.

This is intentionally not a promise of full browser Canvas. It is a useful,
testable 2D surface for diagrams, charts, editors, and libraries that need more
than the present `measureText()` compatibility path.

The implemented C0/C1 subset is intentionally smaller than the proposed C1
row below. It provides a document-owned `ImageSurface`, `getContext("2d")`
identity, HTML-canvas width/height reset semantics, solid fill/stroke state,
`save`/`restore`, transforms, clipped `clearRect`, fill/stroke rectangles, and
paths (`beginPath`, `moveTo`, `lineTo`, `quadraticCurveTo`, `bezierCurveTo`,
`rect`, `arc`, `closePath`, `fill`, `stroke`, and nonzero `clip`). Stroke caps
and joins, `rgb()`/`rgba()` colors, `textAlign`, and transformed, clipped
`fillText()` are real bitmap operations. `font` / `measureText()` retain the
existing font service. The context restores both native state and its
JS-visible projection after `restore()`, so later calls cannot observe stale
style properties. The implemented surface excludes `strokeText`, even-odd
clipping, gradients, dashes, images, readback, export, composition modes, and
all OffscreenCanvas drawing.

## 2. Current position

The current bridge creates `HTMLCanvasElement`, `OffscreenCanvas`, and a
`CanvasRenderingContext2D`-branded object. It supplies font-aware
`measureText()` and replaces the former no-op HTML-canvas geometry methods
with native operations. Unsupported methods are absent rather than
false-success compatibility hooks. `<canvas>` is already a replaced element,
with a 300 × 150 default bitmap coordinate space when its attributes are
absent.

Radiant already has the necessary rendering substrates:

- `ImageSurface` supplies owned RGBA pixel storage, generation tracking, image
  blitting, and PNG encoding.
- PaintIR expresses paths, fills, strokes, gradients, images, clips, and
  transforms; DisplayList lowers that semantic form to the raster backend.
- The normal painter can composite an `ImageSurface` into the CSS box with
  clipping, transforms, opacity, and the existing raster/vector output paths.

The existing compatibility methods must not survive as false-success APIs once
a method is declared supported. A method is either native and correct for its
advertised subset or absent from the published surface.

## 3. Model and ownership

```
Canvas JS API
    │ declared native interfaces (D7.4.1v2, D7.4.4)
    ▼
CanvasRegistry ─── CanvasEntry ─── CanvasState + current path
    │                     │
    │                     ▼
    │              authoritative ImageSurface
    │                     │
    ▼                     ▼
future readback/export   <canvas> painter → DisplayList → raster output
```

### 3.1 Canvas registry

`DomDocumentServices` gains one opaque `CanvasRegistry*`. A registry owns a
`CanvasEntry` for each `HTMLCanvasElement`, keyed by its document-owned native
element address. The entry owns:

- bitmap width and height, and the authoritative `ImageSurface`;
- the current `CanvasState`, state stack, and current path;
- a bitmap generation; and
- per-canvas dimension and pixel limits.

Future command-buffer milestones add pending `CanvasOp` records, copied
paths/styles, dirty rectangles, flush state, and document/realm accounting.

The registry, not the JS wrapper, owns canvas state. A removed canvas remains
available through its wrapper until the document's normal detached-node
lifetime rules release it; document teardown destroys every remaining entry.
This preserves wrapper identity and follows the non-owning DOM-wrapper model.

`OffscreenCanvas` will use the same `CanvasEntry` representation but must be
registered in the realm's Canvas capsule rather than a DOM-node registry row.
That is not implemented yet: its compatibility object remains limited to
`getContext("2d")`, font assignment, and `measureText()`. Its drawing methods
are not published as no-ops, and `transferControlToOffscreen()` and Worker
ownership are excluded.

### 3.2 Script-visible objects

`HTMLCanvasElement` and `CanvasRenderingContext2D` use declared native
interfaces with native state resolved only through the bridge. Future
`OffscreenCanvas`, `CanvasGradient`, `CanvasPattern`, `Path2D`, `ImageData`,
and `ImageBitmap` use opaque resource IDs. Native pointers never become values
observable to script, as required by **D7.4.1v2**. Member lookup, property
writes, and finalization use the one record-owned host-object protocol of
**D7.4.4**.

A context resolves its owning HTML canvas to the registry entry instead of
copying bitmap or state. Repeated `getContext("2d")` returns the same context
object for an HTML canvas. A non-canvas receiver throws; an unsupported context
name returns `null`.

### 3.3 Size and reset semantics

The `width` and `height` IDL properties bind to the real canvas attributes.
Setting either property, including setting it to its prior value, allocates a
new cleared bitmap and resets the context state, state stack, and path. Later
clip and pending-command state shares this reset rule. It then marks the canvas
visually dirty.

CSS width and height are independent: they change only the destination box in
which the bitmap is painted. Radiant must not silently multiply the backing
store by device-pixel ratio; scripts choose a high-density backing size in the
ordinary Canvas way. CSS scaling uses the existing image-surface painter.

## 4. Drawing and flush contract

### 4.1 C0/C1 local raster application

The current subset applies each completed draw synchronously inside
`radiant/canvas_2d.cpp` to the entry's authoritative `ImageSurface`. It uses a
short-lived Radiant `RdtVector` targeting only that surface; it does not draw
to the page `RenderContext`, enter layout, or expose renderer pointers through
the DOM binding. This follows the existing local SVG-rasterization containment
pattern while preserving **D7.5.3**'s explicit bridge boundary.

Every completed draw bumps the surface generation and publishes a
paint-only DOM mutation. `width`/`height`, including `setAttribute` and
`removeAttribute`, replace the surface and reset the native state and the
cached context's observable default properties.

### 4.2 Future command recording

A 2D method validates and normalizes its arguments, snapshots the applicable
state, and appends a native `CanvasOp`. `save()`/`restore()` manipulate native
state frames; path construction updates the native current path. A draw op
therefore carries the resolved transform, clip, style, line settings, alpha,
and composite mode in effect at that call, rather than referring back to
mutable JS properties.

This is a bounded command buffer, not a retained JS replay log. A successful
flush applies pending operations to the authoritative bitmap and releases their
temporary path/style payloads. The bitmap holds the historical result; resize
or context reset starts a new history.

### 4.3 Future flush points

Canvas flush is local raster work. It is never a page style/layout flush and
never calls `layout_html_doc()` from a property getter.

- Before a render walk paints a dirty canvas, flush its pending operations.
- Before `getImageData`, `toDataURL`, `toBlob`, `convertToBlob`,
  `transferToImageBitmap`, or `drawImage` reads a source canvas, flush that
  source canvas first.
- Before a canvas is used as the source of another Canvas operation, preserve
  source pixels if source and destination alias.

Within a page, all recording and flush work runs on the RC1 page thread. A
flush must have a per-entry re-entrancy guard: API calls made while flushing
are rejected or queued for the next flush, never recursively rendered into the
same surface.

### 4.4 Future rasterization

`radiant/canvas_2d_renderer.cpp` should own conversion of supported `CanvasOp`s
to local PaintIR and DisplayList operations, replayed into the entry's
`ImageSurface` with a canvas-local clip. It will reuse Radiant paths, font
shaping, gradients, source-over compositing, and image blits; DOM bindings only
validate Web API arguments and enqueue operations.

Canvas-local composition happens against the canvas bitmap, not the page
backdrop. Page-level effect operations must not be repurposed as Canvas
`globalCompositeOperation`: they have different source/destination and
isolation semantics. The current and initial supported composite set is only
source-over; other modes are not advertised.

After flushing, the entry bumps the `ImageSurface` generation and marks the
canvas's paint bounds dirty. This lets retained display-list fragments reject a
stale canvas source by the same generation discipline used for images and
webview surfaces.

## 5. Painting and export

`render_canvas_content()` is the current page-paint entry for an HTML canvas.
It obtains the entry's bitmap and sends it through the standard image-surface
painter at the canvas's laid-out CSS box. CSS overflow clipping, transforms,
opacity, filters, and image rendering apply outside that bitmap exactly as
they do to other replaced content.

The implemented output policy is deliberately raster:

- raster page output blits the canvas `ImageSurface`; and
- SVG/PDF embedding is deferred; those output walkers do not yet dispatch
  Canvas content.

The intended completed raster policy is:

- screen, PNG, and JPEG output blit the canvas `ImageSurface`;
- SVG and PDF output embed a raster representation of the surface; and
- no SVG/PDF vector-Canvas promise is made, because pixel reads, pixel writes,
  compositing, and Canvas raster semantics make an arbitrary vector replay
  unsound.

A later optimization may emit vector commands for a proven vector-only subset,
but it is never observable API behavior and must fall back to the authoritative
bitmap whenever equivalence is not exact.

## 6. API milestones

| Milestone | Required surface | Deliberate boundary |
|---|---|---|
| **C0 — ownership** | real `getContext("2d")` identity; width/height attributes and reset; bitmap allocation; canvas painter; dirty/generation integration | **implemented for HTMLCanvasElement** |
| **C1a — core geometry** | `save`/`restore`, transforms, `clearRect`, `fillRect`, `strokeRect`, paths, `fill`, `stroke`, solid colors, line width, `globalAlpha`, `font`, and `measureText` | **implemented**, source-over only; no readback or general image source |
| **C1b — core completeness** | clipping, cap/join, text drawing, full Canvas color parsing, and deterministic canvas-only pixel goldens | **partially implemented:** nonzero clips, cap/join, `fillText`, and named/hex/`rgb()`/`rgba()` colors; even-odd clips, `strokeText`, wider CSS Color syntax, and pixel goldens remain deferred |
| **C2 — common library slice** | linear/radial gradients, dash arrays, `drawImage` from decoded images and canvas sources, supported `globalCompositeOperation` values | no patterns, shadows, ImageData, or arbitrary image codecs |
| **C3 — pixel and export slice** | `ImageData`, `createImageData`, `getImageData`, `putImageData`, PNG `toDataURL`, `toBlob`, and `OffscreenCanvas.convertToBlob` | encoding stays in memory; no filesystem capability is implied |
| **C4 — breadth only with a consumer** | patterns, shadows, `Path2D`, richer text metrics, `ImageBitmap`, broader composition modes | add only with a focused consumer and conformance test |

The planned C1 set is the minimum useful static-chart/editor surface. It provides
actual drawing rather than a broad nominal API. `createLinearGradient()` must
not be published until `addColorStop()` affects drawing.

## 7. Explicit non-goals

- WebGL, WebGL2, WebGPU, shader compilation, GPU resource APIs, and GPU/CPU
  synchronization.
- Worker, `postMessage`, structured clone, and
  `transferControlToOffscreen()`. They remain outside the Radiant Web Worker
  contract.
- Browser-complete CORS, tainted-canvas behavior, video-frame sources, and
  every image decode/encode format. Resource loading follows the existing
  document policy; no readback policy is broadened accidentally.
- Browser-exact color-management, font fallback, text shaping, and every
  `TextMetrics` field before Radiant has the matching render primitive.
- A JavaScript polyfill or command log that only makes feature detection pass.

## 8. Limits, errors, and security

Canvas allocation validates bounded integer dimensions and arithmetic overflow
before allocating pixels. The current implementation caps a canvas at 4096 on
either axis and 4,194,304 pixels, and leaves the existing bitmap intact when an
IDL resize is rejected with `RangeError`. Document-wide and queued-command caps
belong to the later command-buffer milestone.

The future `putImageData` converts between Web-facing unpremultiplied RGBA
bytes and the internal `ImageSurface` alpha convention at the API boundary.
Internal canvas rendering and page compositing must never perform accidental
repeated conversion.

Future `toBlob` and `OffscreenCanvas.convertToBlob` deliver their result
through the existing event-loop completion path, not a module-owned loop pump,
per **D7.4.2**. The intended first export slice supports PNG only and performs
no filesystem I/O.

## 9. Alternatives rejected

### Direct ThorVG calls from `dom_canvas.cpp`

Rejected. The bridge would mutate a renderer-owned object during script
execution, bypass dirty tracking and retained display-list invalidation, and
provide no sound pixel/readback ownership model.

### A JS command polyfill

Rejected. It duplicates state, cannot safely use Radiant's paths, fonts, image
surfaces, or render lifecycle, and recreates the no-op compatibility problem in
a more elaborate form.

### Vector-only Canvas

Rejected. Canvas is fundamentally bitmap-authoritative once readback,
`putImageData`, or destination composition occurs. Vector export can be a
transparent optimization later, not the resource model.

### Full browser Canvas at once

Rejected. Canvas breadth includes independent products—GPU APIs, Workers,
security/tainting, codecs, color management, and advanced text. The C0–C3
slices make each supported claim testable and useful.

## 10. Acceptance gates

Each milestone requires both focused DOM tests and end-to-end output evidence:

- API identity and errors: context identity, illegal receivers, unsupported
  contexts, width/height reflection, and resize reset behavior.
- Deterministic pixels: paths, transforms, clips, alpha, lines, gradients,
  text, image draws, and composition each have compact canvas-only goldens.
- Lifecycle: drawing invalidates a retained page repaint without forcing layout;
  a readback sees pending operations; nested render/event callbacks do not
  re-enter a flush.
- CSS integration: default intrinsic size, CSS scaling, clipping, opacity,
  transforms, and a resized canvas in normal document layout.
- Output: the same fixture is checked in raster output and as the expected
  raster fallback in SVG/PDF.
- Ownership: forced-GC, repeated create/remove, resize, and OffscreenCanvas
  teardown tests show no leaked surface, stale wrapper, or stale
  display-list-resource generation.
- Compatibility: selected WPT Canvas 2D tests are added only when their
  asserted methods belong to the current milestone. A library smoke test is
  supplementary, never the only conformance evidence.

## Appendix A — intended implementation placement

| Area | Proposed responsibility |
|---|---|
| `lambda/dom/dom_canvas.*` | WebIDL argument handling, host-object publication, opaque IDs, and property/method dispatch only |
| `radiant/canvas_2d.cpp` | implemented `CanvasRegistry`, entry/state/path ownership, reset/caps, local raster application, and page bitmap composition |
| `radiant/canvas_2d_renderer.cpp` | future Canvas-local PaintIR/DisplayList recording and queued rasterization/composition |
| `radiant/render_canvas.cpp` | optional future extraction of the current `render_canvas_content()` page-paint entry |
| `radiant/render.hpp` / `radiant/layout.hpp` | narrow shared declarations; no Canvas API state in generic DOM bridge code |
| DOM/page lifecycle | create/remove/teardown hooks, visual dirty notification, retained generation validation |

`dom_canvas.cpp` remains deliberately thin. This maintains the DOM architecture
in which JS objects are wrappers over Radiant-owned state, and preserves the
single rendering authority in Radiant.

## Appendix B — ratification and follow-up

The implemented C0/C1a application of **D7.5.3** does not revise a formal
ruling. Expanding the formal Lambda↔Radiant contract still requires the
following before the later milestones are accepted:

1. recording the C0/C1a bounded support contract in
   `Radiant_Design_DOM_API.md`;
2. recording any expanded Lambda↔Radiant Canvas placement against **D7.5.3**;
   if that expands the ruling rather than merely applying it, revise the formal
   ruling in place and bump the formal-design document version; and
3. moving the remaining C1b–C4 implementation sequence and progress evidence
   into a `vibe/impl/` plan before that work begins.

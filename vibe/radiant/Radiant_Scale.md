# Radiant Coordinate Spaces and Scale

**Status:** accepted working design; scale ownership, input, interaction,
raster-cache, and host-native boundaries are implemented; the broader semantic
Paint IR migration and real-monitor automation remain partial
**Revised:** 2026-08-29
**Scope:** Radiant layout, input, interaction state, paint geometry, raster
output, vector export, native overlays, nested documents, and HiDPI changes
**Formal-spec linkage:** no current `S#` or `D#` ruling covers Radiant
coordinate domains; this document is the working design authority for the
topic
**Decision series:** RSC1–RSC12
**Detailed implementation descriptions:**
[RAD_12](../../doc/dev/radiant/RAD_12_Paint_IR_Display_List.md),
[RAD_13](../../doc/dev/radiant/RAD_13_Render_Walk_Painters.md),
[RAD_15](../../doc/dev/radiant/RAD_15_Events_Input.md),
[RAD_17](../../doc/dev/radiant/RAD_17_Interaction_State.md),
[RAD_19](../../doc/dev/radiant/RAD_19_Form_Controls.md), and
[RAD_20](../../doc/dev/radiant/RAD_20_Application_Shell_Browsing.md)

---

## 1. Purpose

Radiant has two unavoidable coordinate domains:

- documents are styled, laid out, queried, and interacted with in CSS logical
  pixels;
- raster surfaces and framebuffer-backed output are allocated and addressed in
  physical device pixels.

The architectural requirement is not merely that both domains exist. It is
that each subsystem has exactly one domain and conversions occur only at named
boundaries. A physical coordinate stored in document state, or a logical
pointer multiplied opportunistically inside an event handler, makes behavior
depend on device scale and eventually fails on HiDPI displays.

The `<select>` dropdown failure exposed that class directly: GLFW supplied a
logical pointer, while rendering cached the dropdown rectangle in physical
surface pixels. Multiplying the pointer in dropdown handling repairs that one
comparison, but preserves the invalid state contract. The durable fix is to
keep both pointer and popup geometry logical and convert only while painting.

## 2. Coordinate domains

| Domain | Unit and origin | Owners | Must not own |
|---|---|---|---|
| Document-local logical | CSS pixels relative to a view, containing block, or nested document viewport | style resolution, layout, transforms, scroll state, range geometry | framebuffer dimensions |
| Top-level viewport logical | CSS pixels relative to the interactive top-level viewport | platform-normalized pointer events, hit-testing, document overlays, popup state | raster addresses |
| Device surface | physical pixels relative to an `ImageSurface` or framebuffer | raster allocation, tile bounds, pixel clips, pixel reads/writes | DOM, event, or interaction policy |
| Host-native | the unit and origin required by the platform API | native child windows, IME candidate windows, OS menus | document state |
| Vector target | PDF points or SVG user units | final vector backend | device-pixel assumptions inherited from the current monitor |

“Logical” describes the unit; “local,” “document,” and “top-level viewport”
describe the origin. Two values being in CSS pixels does not make them directly
comparable if they have different origins. Nested document, scroll, and CSS
transform mapping must therefore remain explicit even after device-pixel leaks
are removed.

Rendering produces a physical raster result, but semantic paint geometry should
remain logical until the raster lowering boundary. This preserves one geometry
model for screen, PNG, PDF, and SVG and avoids repeated per-feature scaling and
rounding.

## 3. Decisions

### RSC1 — CSS logical pixels are the canonical document space

Style resolution, layout, intrinsic sizing, view-tree geometry, scroll
positions, CSS transforms, DOM geometry APIs, hit-testing, editing geometry,
and persistent interaction state use CSS logical pixels represented as
`float`.

No layout result varies merely because the same page is displayed on a monitor
with a different device scale. A layout result can vary when the logical
viewport, CSS, font selection, content, or semantic page zoom changes.

### RSC2 — Platform input is normalized once, before `RdtEvent`

`RdtEvent` is the logical-input boundary. Every absolute pointer coordinate in
an `RdtEvent` is a top-level viewport logical coordinate.

- GLFW already reports logical window coordinates, so its callbacks pass them
  through unchanged.
- A backend that reports device coordinates divides by its device scale in its
  platform adapter before constructing the event.
- Event dispatch, hit-testing, default actions, drag thresholds, and form
  behavior never inspect the backend's physical coordinate convention.

Wheel or gesture deltas require their own normalization contract. They are not
absolute pointer positions and must not be multiplied or divided merely because
pointer coordinates are.

### RSC3 — Pointer precision is preserved

Pointer coordinates remain fractional logical values through dispatch and
hit-testing. `float` is the Radiant storage type. Integer conversion is allowed
only where an API or raster operation requires a discrete pixel/index, and the
rounding rule is explicit at that point.

This rules out the current `MousePositionEvent::x/y` integer payload as the
long-term contract: GLFW provides `double`, and truncating it at event creation
loses information before hit-testing.

### RSC4 — Interaction state never stores physical geometry

`DocState` geometry is logical. This includes dropdown and context-menu
rectangles, pending popup anchors, drag anchors, selection/caret presentation,
autoscroll regions, and any future tooltip or dialog placement state.

Document-level overlays store top-level viewport logical rectangles. Rendering
converts those rectangles for the active output target. Event handling compares
logical pointers directly with those logical rectangles.

The renderer may read interaction state. It must not rewrite canonical state
with a rectangle that has already been scaled for a particular surface.

### RSC5 — Nested documents map logical space to logical space

Iframe targeting and popup placement use a shared logical transform from a
nested document viewport to the top-level viewport. The transform accounts for:

- iframe border/content origins;
- ancestor positions;
- scroll offsets;
- supported CSS transforms;
- nested iframe depth.

Device scale is not part of this transform. It is applied only after geometry
has reached the top-level logical viewport and is being rasterized or handed to
a host-native API.

### RSC6 — Paint semantics are logical; raster lowering is physical

The render walk and backend-neutral Paint IR consume logical geometry. Raster
lowering applies one CSS-to-surface transform and emits physical display-list or
raster coordinates. Surface dimensions, tile bounds, pixel clips, image buffers,
glyph bitmaps, and pixel effects are physical.

SVG and PDF consume the same logical paint semantics and map them to their own
target units. They do not inherit the current screen's device scale unless the
caller explicitly requests a scaled export.

Pixel snapping is a raster policy, not layout policy. Borders, glyph origins,
and clips may snap after transformation without changing the logical view tree.

### RSC7 — Device scale, output density, and page zoom are distinct

The scale vocabulary is:

| Quantity | Meaning | Affects layout? |
|---|---|---|
| `device_scale` | physical framebuffer pixels per logical window pixel | no, unless the logical viewport also changes |
| `output_scale` | caller-requested raster/export density | no |
| `page_zoom` | semantic page zoom or viewport scaling | potentially; its viewport and CSS behavior must be defined separately |
| `raster_scale` | logical paint units to destination surface pixels | derived, normally `device_scale × output_scale` |

Radiant owns these quantities explicitly as `UiContext::device_scale`,
`ViewportMeta::output_scale`, `ViewportMeta::page_zoom`, and the derived
`ViewportMeta::raster_scale` / `RenderContext::raster_scale`. Viewport
`initial-scale` initializes `page_zoom`; it does not silently become CLI output
density. The legacy CLI spelling `--pixel-ratio` and the font library's
`FontContextConfig::pixel_ratio` remain adapter vocabulary at their public
boundaries, not document/layout state.

### RSC8 — Conversion functions are centralized and typed

Conversions belong to narrow APIs, conceptually:

```cpp
CssPoint platform_pointer_to_css(...);
DevicePoint css_to_device(CssPoint point, RasterScale scale);
DeviceRect css_to_device(CssRect rect, RasterScale scale);
HostRect css_to_host(CssRect rect, HostWindowMetrics metrics);
```

The C+ implementation may use small structs or strict `_css` / `_device`
suffixes. It must not rely on an unqualified `x`, `width`, or `scale` where two
domains are plausible. Conversion helpers are not general conveniences:
platform adapters call the input conversion, raster/host adapters call output
conversion, and document policy calls neither.

Device scale should retain independent X/Y measurements at the platform seam.
Code may expose one scalar only after validating that the axes agree within a
defined tolerance.

The C+ boundary types and conversions live in `radiant/scale.hpp`:
`RdtLogicalPoint/Rect`, `RdtDevicePoint/Rect`, discrete device-pixel point/size
types, and logical-to-host conversion with an explicit Y-origin policy.
`UiContext` wrappers add the document's output scale and preserve the
platform's independent X/Y device scales.

### RSC9 — Host-native overlays have an explicit second boundary

A child webview, IME candidate window, or genuine OS menu may require host
screen coordinates rather than Radiant surface coordinates. The owner computes
that mapping at the platform call from a logical top-level rectangle plus
window position, content insets, origin convention, and platform scale.

Host coordinates are ephemeral arguments. They are never cached as the
canonical popup or caret rectangle in `DocState`.

### RSC10 — Scale changes do not automatically imply reflow

Moving a window between monitors can change framebuffer size and device scale
without changing the logical viewport. In that case Radiant recreates the
surface, refreshes scale-dependent font/raster resources, invalidates retained
physical render caches, and repaints without relaying out the document.

Radiant reflows only when the logical viewport or another layout input changes.
If a platform reports framebuffer and content-scale notifications separately,
the shell settles both into one coherent metrics update before rendering.

### RSC11 — Physical caches declare their scale key

Any cache containing physical output—glyph bitmaps, rasterized SVG subscenes,
image surfaces, retained physical display-list fragments, or webview snapshots—
is keyed or invalidated by every scale factor that affects its pixels. Logical
layout caches do not include device scale in their identity.

### RSC12 — Coordinate invariance is a tested property

The same logical event scenario must resolve the same targets, selected values,
hover indices, caret boundaries, scroll positions, and logical state snapshots
at device scales `1.0`, `1.25`, `1.5`, and `2.0`. Raster dimensions and sampled
pixel locations may differ according to scale.

Required coverage includes:

- `<select>` and context-menu overlays;
- fractional edge hits;
- scrolled containers;
- nested iframes;
- translated/scaled CSS transforms as supported by hit-testing;
- caret and selection geometry;
- moving a live window between device scales;
- equal logical output from layout and DOM geometry queries across scales.

## 4. End-to-end contract

```text
platform pointer
    │  normalize once if the backend reports device coordinates
    ▼
RdtEvent in top-level logical CSS pixels
    ▼
hit-test / dispatch / default actions / DocState
    │  all geometry remains logical
    ▼
render walk and semantic Paint IR
    │  apply target transform once
    ├── raster lowering ──► physical display list / surface / framebuffer
    ├── SVG backend ──────► SVG user units
    └── PDF backend ──────► PDF points
```

Nested document mapping occurs between `RdtEvent` and hit-testing and remains a
logical-to-logical transform. Native child-window mapping branches from a
top-level logical rectangle through the host adapter.

## 5. Data ownership contract

| Data | Canonical domain |
|---|---|
| `UiContext::viewport_width/height` | logical CSS pixels |
| `UiContext::window_width/height` | physical framebuffer pixels |
| `UiContext::device_scale_x/y` | platform device scale on each axis |
| `UiContext::device_scale` | validated isotropic scale used by the current raster backend |
| `ViewportMeta::output_scale` | explicit raster/export density |
| `ViewportMeta::page_zoom` | semantic viewport zoom metadata; excluded from raster density |
| `ViewportMeta::raster_scale` | derived logical-paint to destination-pixel scale |
| view `x/y/width/height`, bounds, scroll positions | logical CSS pixels |
| absolute pointer coordinates in `RdtEvent` | top-level viewport logical pixels |
| dropdown/context-menu/caret/selection state | logical pixels with an explicit origin |
| `RenderWalkState` and backend-neutral paint geometry | logical CSS pixels |
| raster surface, tile dimensions, pixel clips | physical device pixels |
| native overlay bounds passed to an OS API | host-native units, ephemeral |

Font APIs straddle the boundary: CSS font size and layout advances are logical;
glyph bitmap dimensions and bearings are physical. A font metric returned in
physical units must be divided exactly once before layout consumes it.

## 6. Invariants and review rules

1. A field carrying geometry states its domain and origin in its declaration.
2. `pixel_ratio` does not appear in layout or event policy merely to make two
   values comparable.
3. No renderer writes scaled geometry back into `DocState` or the view tree.
4. No platform callback emits physical coordinates into `RdtEvent`.
5. No vector export depends implicitly on the monitor hosting the process.
6. Every logical-to-device conversion is grep-able through a small conversion
   API or an explicitly named raster scale.
7. Every float-to-integer conversion is at a discrete boundary and follows the
   Radiant `INT_CAST_OK` rule.

Not every compatibility use of `pixel_ratio` is a defect. The font library and
legacy CLI/fixture inputs expose that established spelling at their adapter
boundaries. Radiant immediately maps it to `device_scale` or `raster_scale` and
does not carry it into layout or event policy.

## 7. Current conformance and known gaps

The 2026-08-28 through 2026-08-29 audit confirmed these implemented
boundaries:

- style, layout, intrinsic form sizing, initial containing blocks, multicol
  geometry, replaced-image sizing, and scrollbar interaction use CSS logical
  pixels (RSC1);
- GLFW input enters `RdtEvent` unchanged as logical coordinates, while
  `MousePositionEvent`, synthetic input, raw-event replay, drag interpolation,
  native Lambda event maps, and JS Mouse/Pointer/Wheel/Drag events preserve
  fractional coordinates (RSC2, RSC3);
- dropdown and context-menu state is top-level viewport logical geometry;
  popup painters scale local copies and no longer write raster geometry back to
  `DocState` (RSC4);
- a shared nested-document viewport-offset walk accounts for iframe ancestors
  and scroll before popup placement (RSC5);
- `ViewportMeta`, `UiContext`, render/export targets, tile jobs, SVG subscenes,
  and webview state use the RSC7 vocabulary. `ViewportMeta` deliberately
  retains its 32-byte footprint under a `static_assert`, because later
  `DomDocument` members have native/JIT-sensitive offsets (RSC7);
- `radiant/scale.hpp` centralizes typed logical-to-device and logical-to-host
  conversions. GLFW's already-logical input passes through the platform-input
  adapter, while popup painting, surface sampling, webview bounds, and IME
  placement use explicit destination adapters (RSC2, RSC8, RSC9);
- `UiContext` retains independent X/Y device scales, recreates physical
  surfaces and font raster caches on scale changes, recursively invalidates
  embedded-document paint state, and avoids reflow when the logical viewport
  is unchanged (RSC10, RSC11);
- retained display-list fragments are invalidated by a full dirty frame on a
  raster-scale change; webview child bounds are keyed by device scale and layer
  snapshots by raster scale (RSC11);
- headless event fixtures can declare a device scale, allocate a proportionate
  surface, and keep their viewport and event coordinates logical. Four-scale
  matrices cover select/default-action commit, context-menu targeting and
  geometry, scrollbar dragging, translated-transform hit-testing, and
  fractional caret/selection geometry. An injected `1.0` to `2.0` transition
  proves logical select geometry is unchanged while the surface and physical
  resources are rebuilt (RSC10, RSC12).

Two gaps remain:

- the thin PaintIR primitive gateway still receives destination-space geometry
  from several painters to preserve byte-identical DisplayList output. Moving
  every primitive to logical semantic geometry and applying the complete target
  transform during lowering remains the RSC6/RSC8 render-path migration; the
  device-scale ownership and conversion boundaries no longer depend on that
  migration;
- the native window path settles framebuffer and content-scale callbacks and
  repaints without reflow, but CI has only the injected transition. A real
  cross-monitor transition remains a platform integration/manual gate.

The 2026-08-29 HiDPI visual audit also corrected the
`svg_text_hidpi_01` reference setup: Radiant used a 360×140 logical viewport at
2×, while Chromium had been captured at a 720×280 logical viewport at 1×.
Capturing both at 360×140 logical and 2× physical reduced the comparison from
3.20% to 0.55%; changing Radiant to match the old reference would have violated
RSC1 and RSC7.

## 8. Non-goals

- This design does not define the full CSS transform inverse used by
  hit-testing; it requires that transform mapping remain logical.
- It does not define browser zoom UI or mobile viewport semantics beyond
  separating them from output density.
- It does not require raster and vector backends to share storage formats; they
  share logical paint semantics.
- It does not require all physical operations to use floating point. Pixel
  buffers and tile indices remain discrete after the conversion boundary.

---

## Appendix A — Implementation history

This appendix preserves the implementation chronology from the original
“Radiant Scale Refactoring Proposal.” Status here is historical evidence, not a
claim of current conformance. The original completion labels predated the
dropdown HiDPI failure and were too broad.

### A.1 Original campaign summary

| Historical phase | Work recorded by the original document | Revised interpretation |
|---|---|---|
| Phase 0 | View-tree JSON changed to CSS logical pixels and added `coordinate_system: css_logical_pixels` | landed |
| Phase 1 | Added document `given_scale` and combined `scale` fields | landed historically; replaced by the RSC7 ownership vocabulary in the 2026-08-29 follow-up |
| Phase 2 | Removed broad `pixel_ratio` multiplication from CSS/HTML style resolution | substantially landed; residual uses require classification |
| Phase 3 | Converted layout geometry and intrinsic sizes to logical pixels | substantially landed, not globally proven |
| Phase 4 | Changed default CSS font sizing to logical units while retaining scale-aware glyph resources | landed |
| Phase 5 | Added render-time scaling for screen, PNG/JPEG, SVG, and PDF paths | landed in several paths; conversion remains distributed |
| Phase 6 | Declared input handling complete because GLFW coordinates were already logical | platform half landed; state/hit-test consumers were not fully audited |
| Phase 7 | Added CLI output `--scale` support | landed historically |
| Phase 8 | Updated iframe loading and viewport calculations | logical iframe sizing landed; popup/overlay coordinate state remained physical |
| Phase 9 | Parsed viewport scale metadata and body transform scale | extraction landed; semantic separation from output scale remains open |

The campaign recorded a then-current baseline of 73/73 Lambda runtime tests and
1704/1704 Radiant baseline tests (1777 total). Those figures are an old snapshot
and must not be used as a current acceptance result.

### A.2 Historical changes by layer

The original refactor made the following broad changes:

- `view_pool.cpp` stopped dividing serialized geometry by `pixel_ratio` and
  emitted logical values directly.
- CSS absolute units were resolved against 96 CSS pixels per inch without
  multiplying by device scale.
- HTML presentational dimensions, intrinsic image dimensions, table widths,
  and positioned-element sizes moved toward logical units.
- the default CSS font size became 16 logical pixels; font resources remained
  scale-aware for crisp glyph rasterization.
- render entry points allocated scaled raster surfaces and multiplied logical
  geometry during painting.
- window callbacks stopped multiplying GLFW cursor coordinates.
- iframe viewport dimensions stopped dividing already-logical block dimensions
  by device scale.
- the CLI exposed caller-requested output scaling.

The proposal listed modifications across `dom_element.hpp`, style resolution,
layout modules, `ui_context.cpp`, render backends, `window.cpp`, and
`lambda/main.cpp`. Those file lists and line estimates were planning aids, not
part of the lasting design contract.

### A.3 Historical rationale retained

The campaign was motivated by six valid problems:

1. layout tied to monitor density was non-portable;
2. headless rendering needed explicit output density;
3. callers needed high-resolution raster export without relayout;
4. SVG/PDF should not inherit display-dependent geometry;
5. scattered scaling created double- and missing-conversion risks;
6. platform-dependent layout made baselines unstable.

Those reasons remain valid. RSC1–RSC12 sharpen the old “centralize scaling in
rendering” phrase into enforceable ownership and conversion boundaries.

### A.4 Historical migration and compatibility notes

The original proposal warned that view-tree coordinates and `FontProp` sizes
would change from physical to logical units and that external consumers might
need an explicit logical-to-device helper. It estimated negligible rendering
cost from coordinate multiplication and expected fewer scale operations during
style resolution.

It also proposed these verification classes:

- unchanged layout output across device ratios;
- 1× and 2× raster outputs with proportional surface dimensions;
- crisp Retina window rendering;
- cross-platform equality for explicit-scale CLI output.

The direction remains correct, but RSC12 expands the gate to interaction state,
fractional ratios, nested documents, and monitor-scale changes.

### A.5 2026-08-28 conformance audit

The first audit against RSC1–RSC12 found and repaired these concrete leaks:

- integer truncation in native, synthetic, replayed, and JS-visible pointer
  coordinates;
- physical dropdown/context-menu geometry stored in interaction state and a
  tactical pointer-to-surface conversion in dropdown handling;
- nested dropdown placement that omitted ordinary ancestor scrolling;
- renderer-owned popup state and renderer-owned physical scrollbar-handle
  geometry;
- device-scaled form intrinsic sizes, initial-containing-block dimensions,
  multicol viewport dimensions, and flex replaced-image dimensions;
- headless event surfaces that could not represent a non-1× device scale;
- scale changes that did not refresh font handles/caches or recursively dirty
  embedded documents;
- an unused document-loader `pixel_ratio` parameter that suggested layout was
  scale-dependent even though the value was ignored.

The audit added RSC12 select fixtures at four scales and corrected the scrolled
iframe dropdown fixture to assert the canonical top-level logical anchor. This
entry records implementation history; the decisions in the main body remain
the authority.

### A.6 2026-08-29 ownership, host, and matrix completion

The follow-up slice:

- separated device scale, output density, page zoom, and derived raster scale
  throughout document, UI, render-target, SVG, tiling, and webview ownership;
- preserved the ABI-sensitive `ViewportMeta` footprint after an initial added
  field exposed native/JIT offset coupling;
- introduced typed logical/device/host coordinate helpers;
- corrected AppKit top-left/bottom-left mapping, Windows IME DPI conversion,
  Linux child-webview bounds, and raster-keyed webview layer snapshots;
- added four-scale context-menu, scrollbar, transformed-hit, and editing
  geometry fixtures; and
- repaired the HiDPI browser reference so both engines use the same logical
  viewport and device scale.

This entry records implementation history, not a replacement for RSC1–RSC12.

## Appendix B — Remaining migration

Completed in the 2026-08-28 audit:

1. pointer coordinates are fractional logical values end to end;
2. dropdown/context-menu and scrollbar interaction geometry is logical;
3. nested dropdown anchors map into the top-level logical viewport;
4. the known layout-side scale leaks were removed and every remaining explicit
   layout/event `pixel_ratio` use was classified;
5. device-scale changes refresh raster resources without reflow;
6. the select interaction matrix runs at all four RSC12 scales and includes an
   injected live scale transition.

Completed in the 2026-08-29 follow-up:

1. the RSC7 factors have explicit names and owners, with page zoom excluded
   from raster density;
2. typed logical/device/host conversion helpers cover the platform, popup,
   pixel-sampling, webview, and IME boundaries;
3. native overlays use logical-to-host conversion and do not cache host-native
   geometry as document state;
4. retained physical caches are invalidated or scale-keyed; and
5. the four-scale interaction matrix covers context menus, scrollbars,
   translated transforms, and editing geometry in addition to selects.

Remaining sequence:

1. migrate the remaining thin PaintIR primitive commands from
   destination-space payloads to logical semantic geometry and apply target
   transforms in each lowering (RSC6, RSC8);
2. add a real cross-monitor device-scale transition integration/manual gate;
3. keep the ordinary Radiant baseline as the acceptance gate for each slice.

This appendix is staging guidance, not a competing source of truth. If staging
and RSC1–RSC12 disagree, the decisions win.

## References

- [CSS Values and Units Level 3](https://www.w3.org/TR/css-values-3/) — CSS
  reference pixel and absolute-unit definitions
- [CSSOM View Module](https://www.w3.org/TR/cssom-view-1/) — viewport and DOM
  geometry coordinate conventions
- [Pointer Events](https://www.w3.org/TR/pointerevents/) — pointer-coordinate
  event model
- [HTML viewport meta extension](https://html.spec.whatwg.org/multipage/semantics.html#the-meta-element) — viewport metadata context

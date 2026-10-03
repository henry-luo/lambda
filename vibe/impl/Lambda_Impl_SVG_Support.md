# Lambda SVG Support — Implementation Plan

**Date:** 2026-10-02

**Status:** paused at user request on 2026-10-03; P1-P6 implemented with focused and aggregate Radiant validation; P7 oracle review remains open; P8-P9 have focused validation; P10 filter graphs have F1-F4 focused gates; P11 content/input gates pass with export deferred to P13; P12 animation is in progress; final filter audits and P13 portable/vector export gates remain outstanding

**Scope:** the missing behavior in §15 of [HTML, CSS and SVG Support](../../doc/HTML_CSS_SVG_Support.md#15-svg), including the limitations inside partial and supported rows; SVG-specific export limitations in §17 are a separate final phase.

**Source audit:** initial working tree at `c886322fd`; original support-matrix evidence is dated 2026-09-28. Implementation and fresh validation on 2026-10-02 are recorded in §7.4.

## 1. Objective and authority

Close the listed SVG gaps through the existing Radiant SVG renderer. The first
milestone makes ordinary icons, diagrams and labels reliable; subsequent phases
add complete resource painting, effects, embedded HTML and animation. Every
matrix limitation below has an implementation phase and an acceptance test.

The [documentation convention](../../doc/Doc_Convention.md#5-implementation-plans-vibeimpl)
classifies this file as an informative implementation plan. It introduces no
Lambda semantics or design ruling. Applicable formal requirements are:

| Ruling | Requirement applied here |
|---|---|
| **D4.1.3** | “Input/format allocations are pool/arena-owned and outside GC rooting entirely.” Parsed external SVG retains its owning Input/pool; it is not treated as a GC object. |
| **D4.2.2v2–D4.2.4** | Resources and allocator lifetimes belong to document contexts; published shared arenas/pools require retained ownership and exclusive reset. Deferred SVG commands cannot borrow expired render scratch. |
| **D4.2.5v3** | Application allocations use `mem_*` or the owning Arena/Pool/GC API; no new direct libc allocation paths. |
| **D4.2.6** | Document/pool cleanup owns decoded surfaces, references and other external resources. |
| **D4.5.1v3** | Radiant is “never GC'd”; the seam contract is “pin, gen-check, copy-as-value.” DOM removal, asynchronous completion and retained paint must honor generation checks. |
| **D1.5v2, D5.3.3** | Runtime-generated SVG and script callbacks crossing the seam use precise `RootFrame`/`Rooted`/persistent ownership; conservative stack scanning remains retired. |

These IDs refer to [Lambda Formal Design](../../doc/Lambda_Formal_Design.md).
No current `S#`/`D#` ruling defines SVG painting or Radiant coordinate semantics.
For those points, retain **RSC1** (float CSS logical coordinates), **RSC6**
(logical paint, physical raster lowering), and **RSC11–RSC12** (scale-aware
caches and invariance tests) in [Radiant Scale](../radiant/Radiant_Scale.md),
and the ownership boundaries in [Radiant Geometry](../radiant/Radiant_Design_Geometry.md).
The embedding/style decisions in [Radiant SVG2](../radiant/Radiant_SVG2.md#design-decisions)
remain the working design: inline SVG participates in its host document CSS;
external SVG image documents retain isolated styles.

Use the W3C specifications linked in the phases for SVG behavior. Implementing
these gaps does not require a new Lambda ruling. If work uncovers a conflict
with an existing ruling, resolve it before implementation and update both the
formal spec and its working design record according to rule 17.

## 2. Current implementation and reusable seams

SVG input already reaches Radiant-owned parsing and rendering through inline
HTML, standalone documents, pictures, backgrounds, data URIs, nested images
and local external `<use>`. Preserve that shared path. ThorVG remains the active
vector backend; its SVG loader is not the replacement implementation.

| Area | Current source/symbols | Implication for the work |
|---|---|---|
| SVG traversal | `radiant/render_svg_inline.cpp`: `render_svg_element`, `render_svg_children`, `render_svg_to_display_list_primitives` | Add missing element semantics to one traversal; do not implement separate inline/image renderers. |
| Paint contract | `radiant/render.hpp`: `SvgInlineRenderContext`, `SvgInitialPaint`, `PaintSvgSubscene`; `radiant/paint_ir.cpp` | Carry computed styles, references and effect state through record/lower/replay. Extend each payload's copy/retain/destroy behavior together. |
| CSS | `lambda/input/css/`, `radiant/resolve_css_style.cpp`: `resolve_inline_color_property`, `resolve_color_value`; SVG-local `get_svg_attr_or_style` | Replace the lightweight SVG cascade with an adapter over the shared CSS engine. Current host fill/stroke storage only carries solid colors. |
| Geometry | `svg_append_basic_shape_path`, `svg_parse_path_d`, `svg_parse_transform`; `rdt_path_get_bounds`, `rdt_path_visit` | Reuse path construction and inspection. Audit bounds precision before using it for object-bounding-box resources. |
| Paint resources | `SvgDefTable`, `register_svg_def_element`, `draw_gradient_fill`, `draw_pattern_fill`, `draw_svg_fill_stroke` | Resolve resources and inheritance into reusable typed paint values instead of adding more string-special-case branches. |
| Text | `SvgTextLayout`, `svg_text_collect`, `svg_text_measure`, `svg_text_place`, `svg_text_draw`; `lib/font/` | Extend the current measured-run model and Radiant fonts. Reuse glyph outlines/coverage through a shared font adapter. |
| Images/references | `radiant/surface.cpp`, `rdt_picture_load`/`rdt_picture_load_data` in `radiant/rdt_vector_tvg.cpp`; `render_svg_image`, `render_svg_external_use` | Share resource resolution and decoders, retaining document-relative URLs and existing recursion guards. |
| Clips/effects | `resolve_svg_clip_path`, `build_clip_path_from_def`, `svg_group_enter`/`leave`, `render_svg_element_with_simple_mask`, `resolve_svg_gaussian_blur_filter`; `radiant/render_filter.cpp` | Replace approximations behind the common element/effect boundary. Existing software SVG blur is independent of the native CSS blur capability. |
| Interaction | SVG presentation/CTM/paint-hit helpers in `lambda/dom/dom.cpp`; SVG target refinement in `radiant/event.cpp`; `radiant/view_geometry.cpp` | Keep painted geometry, `elementFromPoint()` and actual pointer targets aligned. Affine SVG unprojection already uses `rdt_matrix_unproject_affine_point`. |
| Invalidation | `dom_svg_layer_note_mutation` in `lambda/dom/dom.cpp`; `svg_layer_*` in `render_svg_inline.cpp` | Extend dependencies beyond mutation under the SVG root: host styles, fonts and referenced definitions also affect cached paint. |
| Export | `render_svg_subscene_to_svg` and `svg_subscene_serialize_element`; `radiant/render_svg.cpp`, `radiant/render_pdf.cpp` | SVG output currently serializes source elements; PDF rasterizes SVG subscenes onto white. Raster improvements alone do not close export gaps. |

The initial source inspection identified concrete causes: polygon/polyline
geometry left its bounds at zero, and group-opacity fallback used viewBox
dimensions even when those dimensions were absent. Those causes are repaired
in §7.4. Inherited paint still stores colors rather than resource references;
text styles only carry a solid fill; the dispatcher has
no `switch`/`foreignObject` handler; and the SVG-local stylesheet matcher is
narrower than the shared CSS engine.

Do not treat every matrix observation as confirmed-current. For example, the
file-image path now resolves URLs and translates a ThorVG picture, while the
matrix reports wrong placement/base resolution; the simple mask path also has
an exclusion special case. Reproduce these cases through final paint and inspect
transform/ownership handoffs before deciding whether they remain broken.

## 3. Coverage map

The IDs below identify plan work packages, not new normative rulings or a
second issue ledger. “Listed gap” records the original support matrix. G3–G5,
C4, G2 and A1 have implementation evidence in §7.4; the other packages remain
**planned / awaiting reproduction**.

| ID | Listed gap | Phase | Required acceptance case |
|---|---|---|---|
| C1 | Page stylesheets do not reach inline SVG descendants | P2 | Host selectors alter nested SVG paint; external SVG image styles remain isolated. |
| C2 | Embedded `<style>` lacks selector lists, combinators and `!important` | P2 | Shared cascade agrees for comma lists, descendant/child selectors and importance conflicts. |
| C3 | Font CSS on `<g>` does not inherit | P2, P5 | Group `style` changes descendant family/size/weight/style and measured text placement. |
| C4 | `hsl()`, percentage `rgb()`, `rebeccapurple` paint black | P2 | Equivalent SVG and HTML color declarations produce matching pixels. |
| C5 | `visibility` is ignored | P2 | Hidden parent with a visible descendant, inherited visibility and pointer targeting. |
| C6 | CSS transforms on SVG children are ignored | P3 | CSS/attribute transforms, origins and ancestor composition agree with painted hits. |
| G1 | Root/nested viewport overflow is always clipped | P3 | Visible overflow paints outside the viewport; hidden overflow clips under transforms. |
| G2 | Group opacity depends on root `viewBox` | P3 | Overlapping children composite once with and without a viewBox. |
| G3 | `<rect ry>` without `rx` is square | P1 | Both one-radius forms and the half-size radius clamp. |
| G4 | An unstroked `<line>` becomes black | P1 | Default/explicit `stroke=none` is blank; inherited/CSS stroke remains visible. |
| G5 | Invalid path tokens discard valid preceding segments | P1 | Valid prefix paints; incomplete commands stop at the specification's error boundary. |
| G6 | Percentages, `em` and `ex` lack context | P3 | Nested viewports, font-relative lengths and diagonal-based stroke/dash percentages. |
| R1 | Raster `<image>` loses x/y, base URL or aspect alignment/`none` | P4 | Offset, nonzero viewBox and relative asset from a different working directory; all fit modes. |
| R2 | GIF/WebP cannot load inside SVG `<image>` | P4 | GIF through the existing decoder; WebP through the shared image decoder after its codec prerequisite. |
| R3 | `<a>` ignores its own transform/attributes | P3 | Paint inheritance and transforms match equivalent `<g>`; actual link targeting remains correct. |
| A1 | Default-unit gradients disappear on paths/polygons/polylines | P6 | Curved/nonrectangular geometry receives the expected gradient and bounds. |
| A2 | Inherited gradients become black; gradient strokes become black | P6 | One typed paint representation works on inherited fill and stroke. |
| A3 | `gradientTransform`, repeat/reflect, href templates, `fx`/`fy`/`fr` missing | P6 | Affine gradient, repeated/reflected stops, template chain and focal radial cases. |
| A4 | Styled `stop-color` is lost | P2, P6 | Stop style, opacity and `currentColor` go through the shared cascade. |
| A5 | `context-fill`/`context-stroke` become black | P6, P7 | Markers and use instances receive solid/resource context paint with the source coordinate frame. |
| A6 | Pattern `viewBox`, `patternContentUnits`, href missing | P6 | Tile/content spaces, inherited template attributes/children and transformed tiles. |
| A7 | Dash zeros disappear; dash percentages use plain units | P3, P7 | `0 n` round-cap dots, odd lists, all-zero list, negative invalid list and percentage offsets. |
| A8 | `stroke-miterlimit`, `vector-effect`, `paint-order` missing | P7 | Acute miter cutoff, non-scaling stroke under affine transforms and reordered fill/stroke/markers. |
| K1 | clip children/transforms/units/style and target transform ignored | P8 | Multiple paths/basic shapes, child and target transforms, object-bounding-box units and CSS clip reference. |
| K2 | `clip-rule` missing | P8 | Even-odd holes differ from nonzero clipping. |
| K3 | Gradient masks ignored; black shapes cannot reliably cut holes | P8 | White-to-black gradient, overlapping black cutout and alpha/luminance mask composition. |
| M1 | Only marker-end on paths; missing start/mid/shape markers/viewBox | P7 | Start/mid/end on paths, lines, polylines and polygons with correct tangents and marker viewport. |
| T1 | Text uses only the first x/y/dx/dy; `rotate` missing | P5 | Addressable-character lists across nested tspans, Unicode and repeated rotation values. |
| T2 | Text stroke, opacity/fill-opacity and gradient fill missing | P5, P6 | Glyph outlines/coverage use shared paint and compositing, including `fill=none`. |
| T3 | Spacing, dominant-baseline and decoration missing | P5 | Measured gaps, inherited baselines and visible underline/overline/strike across runs. |
| T4 | `<textPath>` content skipped | P9 | Curved labels, offsets, anchoring and transformed reference paths. |
| E1 | Filter handles only the first blur on shapes/paths | P10 | Multi-node filter chain on group, text, image and use sources. |
| E2 | `feDropShadow`, `feOffset`, `feMerge`, `feColorMatrix`, `feFlood` ignored | P10 | Named intermediate results and a complete shadow chain. |
| E3 | `feBlend`, `feComposite`, `feMorphology` absent | P10 | Blend/composite operators, arithmetic coefficients and dilate/erode. |
| E4 | `feTurbulence`, `feDisplacementMap`, `feImage`, `feTile`, lighting absent | P10 | Deterministic texture/displacement, resource image, tile and diffuse/specular light fixtures. |
| S1 | `<switch>` paints every child | P11 | First eligible child only, including no match and language changes. |
| S2 | `<foreignObject>` paints nothing | P11 | HTML layout/paint and real input inside a transformed/clipped SVG rectangle. |
| N1 | `<animate>`, `<animateTransform>`, `<set>` remain static | P12 | Deterministic document-time samples, repeats, freeze/remove and invalidation. |
| X1 | SVG export loses page sizing and emits undeclared xlink prefixes | P13 | XML-valid standalone output with resolved viewport/style/resources and matching pixels. |
| X2 | SVG pictures are linked/dropped; PDF inline SVG is opaque and 1× | P13 | Portable embedded SVG images and transparent, density-aware PDF fallback. |

R2 depends on shared WebP decoding, which §16 also lists as missing. Adding a
SVG-only decoder would duplicate the resource system. General HTML/CSS gaps
and all non-SVG export deficiencies in §17 remain outside this plan; P13 owns
the image/style/scale behavior necessary for SVG subscenes and SVG pictures.
Tooltips, general accessibility expansion and additional SVG DOM interfaces
are separate work unless a listed feature requires them.

## 4. Shared implementation contract

### 4.1 Computed state and resources

Introduce an internal computed SVG style adapter using the existing CSS
selector/parser/cascade machinery. It may resolve an inline DOM node or an
isolated external SVG document; it must not manufacture ordinary CSS boxes
for every SVG shape. Represent paint as none, solid color, resource reference
plus fallback, or context paint. Preserve URLs and `currentColor` until their
resolution context is known. Separate inherited properties from element
effects: opacity, clips, masks and filters apply to rendered results.

Extend the existing render context/subscene with an owned or retained style
snapshot, viewport/font metrics, document base and resource-generation facts.
The exact struct layout follows a call-site audit in P0. Persistent caches use
document resources under D4.2/D4.5; per-walk resolution uses render scratch.
Retained commands copy values or retain the owning document/pool, including
all strings, paths, stops, surfaces and animation samples they later consume.

An ID/resource resolver serves use, gradient/pattern templates, markers, clips,
masks, filter images and text paths. Reuse existing lookup/load guards and add
typed target validation and cycle detection as required. Dependencies include
host stylesheet changes, referenced nodes outside the SVG subtree, font loads,
viewport/font-relative units, external resources and animation time. Extend
`svg_layer_generation`/layer keys so cached and direct rendering agree.

### 4.2 Geometry and compositing

Keep separate viewport, local geometry, object-bounding-box and target spaces.
One resolver chooses the percentage basis for each property; one transform
composition path serves shapes, resources and hit testing. Reuse
`rdt_path_get_bounds` and `rdt_path_visit`. The shared bounds helper now uses
cubic derivative extrema rather than the control-point hull, restores the
closed-subpath current point and excludes isolated moves. Keep this geometry
box separate from conservative bounds expanded for strokes/effects; do not
create a private inaccurate bounding-box approximation in every SVG subsystem.

A common element rendering boundary renders source content, applies its filter,
then clipping/masking and final opacity according to the effect specifications.
It handles containers, text, images and use instances as well as basic shapes.
Reuse `PaintEffectGroup`, surface/fallback helpers and DisplayList replay;
native backend limitations select a tested fallback rather than dropping paint.

### 4.3 Code organization

Extract shared shapes before adding another per-kind variant. Promote required
existing static helpers through coherent module headers rather than copying
them. Keep public rendering APIs in `radiant/render.hpp`, geometry/view contracts
in `view.hpp`/`layout.hpp`, and font APIs in `lib/font/`. If the SVG monolith is
split, use cohesive style/resource, geometry, text and effect translation units
with the existing module header; avoid a parallel set of per-file public headers.
Register new sources through `build_lambda_config.json` and `make`.

All work is Lambda-owned C++17/C+ code using `Str`/`StrBuf`, `ArrayList`, `HashMap`
and owned memory APIs. Radiant geometry remains `float`; integer pixel/index
conversions require the documented cast exception. Use distinct `log_*` prefixes
and concise root-cause comments. Do not edit vendored ThorVG or other dependencies.
If a reproduced defect genuinely needs an upstream change, isolate it, seek
approval, and record an approved patch under `patches/`.

## 5. Implementation phases

### P0 — Reproduce and establish the shared contracts

**Dependencies:** none. **Size:** medium; mandatory before behavior changes.

1. Add minimal fixtures for the coverage map, split compound rows into distinct
   assertions, and capture browser references with the bundled headless shell.
   Record source revision, viewport, fonts, pixel ratio and selected input path.
2. Compare historical observations to current pixels. A passing case becomes
   a retained regression with evidence; a failure receives its actual root cause.
3. Audit DOM association, shared cascade entry points, deferred payload ownership,
   tight path bounds, source URLs and cache dependency tracking. Define the
   minimal style/resource/geometry interfaces from these call sites.
4. Place new pixel/event fixtures in `test/ui/` and render comparisons in the
   existing `test/render/` infrastructure. Register each UI JSON with exactly
   one owner in `test/ui/ui_test_manifest.json`. Reuse existing fixture helpers.

**Exit:** every coverage ID has a reproducible test/oracle or an explicit
environment blocker, and the shared contract has reviewed owners/lifetimes.
A blocked oracle is not a completed feature.

### P1 — Correct basic geometry and defaults

**Dependencies:** P0. **Size:** small. **Coverage:** G3–G5.

Fix radius fallback in `svg_append_basic_shape_path`, removing the one-way
`ry = rx` assumption. Remove the unconditional black-line draw from
`render_svg_basic_shape` and use normal stroke resolution. Adjust
`parse_svg_path_d` to return completed geometry up to a malformed command,
with bounded token progress and no fabricated coordinates. Preserve move-only,
closepath, arc and quadratic-reflection behavior.

**Exit:** corner/default-stroke/error-prefix pixels agree with the browser;
existing path, marker-end and SVG paint-hit fixtures remain green. Use
[SVG paths](https://www.w3.org/TR/SVG2/paths.html) for error recovery.

### P2 — Unify SVG cascade, inheritance and colors

**Dependencies:** P0. **Size:** large. **Coverage:** C1–C5, A4.

1. Adapt the shared CSS engine to inline SVG descendants and isolated external
   SVG trees. Presentation attributes enter the cascade with their specified
   precedence; inline styles do not automatically defeat stylesheet `!important`.
2. Replace SVG-local selector/style parsing after parity coverage. Resolve group
   fonts, stops, visibility and inherited paints through computed state.
3. Reuse `css_parse_color`/`resolve_color_value` and their token parsing. Extend
   missing common color forms in the shared parser where necessary, removing
   the duplicate SVG named-color/function behavior. Invalid declarations must
   not silently overwrite an earlier valid color with black.
4. Tie restyle, ancestor class changes, style-node edits and font resolution to
   SVG layer invalidation. Implement `visibility` without pruning a descendant
   that can override it; preserve `display:none` subtree suppression.

**Exit:** host and embedded selector/importance tests, inherited font/style tests,
color equivalence and visibility pointer tests pass, including stylesheet edits
after a layer was cached. Follow [SVG styling](https://www.w3.org/TR/SVG2/styling.html).
Unrelated unsupported CSS selectors remain reported as shared CSS limitations.

### P3 — Resolve units, viewports, transforms and opacity

**Dependencies:** P1–P2. **Size:** large. **Coverage:** C6, G1–G2, G6, R3, part of A7.

Replace context-free `parse_svg_length` consumption with property-aware used
lengths: horizontal/vertical viewport bases, normalized diagonal where required,
font metrics for `em`/`ex`, and explicit object-bounding-box fractions. Preserve
typed numeric attributes generated by Lambda/PDF documents.

Compose CSS and attribute transforms with the correct precedence and SVG
reference box/origin. Apply `<a>` state through the shared container scope.
Make root/nested overflow clipping conditional on computed overflow, and include
visible overflow in effect bounds, layer captures and targeting. Correct
group-opacity bounds without a viewBox and composite overlapping children once.

**Exit:** nested/offset viewports, font-relative units, affine CSS child transforms,
visible/hidden overflow and group opacity pass both rendered and pointer tests
at 1×/2×. Keep intrinsic sizing and user-space viewport mapping distinct.
Follow [SVG coordinates](https://www.w3.org/TR/SVG2/coords.html).

### P4 — Fix SVG image placement and shared resource loading

**Dependencies:** P2–P3. **Size:** medium; WebP codec work is a separate prerequisite.
**Coverage:** R1–R2.

Consolidate file/data URI/SVG/raster `<image>` placement using one source-to-
destination transform and fit calculation. Audit the `rdt_picture_take_tvg_paint`
handoff so an earlier translate/resize is not replaced by the final matrix.
Resolve relative paths against the owning document/resource URL throughout
nested image/use chains. Retain the correct base and SVG processing mode for
data-URI resources; do not invent a working-directory base or permit subresources
that the image processing mode disallows.

Reuse `surface.cpp` and `lib/image.c` decoding for GIF and other supported raster
formats. Add WebP once to the shared decoder/build configuration, then expose
it through SVG; use an upstream library without editing vendor sources. Retain
existing SVG parsing, PDF registered-image resolvers, local use and recursion
guards. Share resource completion/invalidation rather than adding blocking
network reads inside element paint.

**Exit:** file and data sources match for x/y, meet/slice/alignment/none, transforms
and source-relative URLs; nested image lifetimes survive reload/removal. GIF is
covered, and WebP remains visibly pending until its shared decoder gate passes.
Follow [SVG embedded content](https://www.w3.org/TR/SVG2/embedded.html).

### P5 — Complete positioned text runs and glyph painting

**Dependencies:** P2–P3; P6 supplies paint servers. **Size:** large.
**Coverage:** T1–T3, C3; T2 finishes after P6.

Extend `SvgTextLayout` with addressable-character positioning across nested runs:
full x/y/dx/dy/rotate lists, spacing and baseline facts, rather than indexing
UTF-8 bytes. Preserve whitespace, text chunks, anchoring and textLength behavior.
Resolve decoration and font metrics through the current font system.

Expose reusable glyph outline/coverage data from `lib/font/` (existing
`glyf_get_outline` and font rasterization are starting points). Paint glyph fill,
stroke and decoration through the same SVG paint/effect path as other geometry.
Define fallback for glyph formats without outlines; do not silently lose stroke
or revert `fill=none` to black. Keep document and picture text placement equal.

**Exit:** list positioning, rotation, inherited fonts, spacing/baselines,
decorations and translucent stroked text pass measured and pixel assertions.
Font fixtures are bundled and deterministic. General Arabic/Indic shaping debt
in §11 remains a separate prerequisite, not a claim of completion here.
Follow [SVG text](https://www.w3.org/TR/SVG2/text.html).

### P6 — Resolve complete gradient and pattern paints

**Dependencies:** P2–P3; integrates with P5. **Size:** large. **Coverage:** A1–A6, T2.

1. Resolve typed fill/stroke paints, inheritance, URL fallback and context paint
   with a retained source element/coordinate frame. Compute geometry bounds
   for every supported shape and glyph run through the shared exact-bounds API.
2. Resolve gradient href chains with attribute/stop inheritance, cycle handling,
   units, affine `gradientTransform`, spread modes and radial focus/radius facts.
3. Extend gradient data through PaintIR, DisplayList and `RdtVector` for gradient
   stroke and additional radial/spread parameters. Audit native capability;
   supply Lambda-side lowering for unavailable operations.
4. Add pattern template/content-space/viewBox behavior to the same resolver and
   tile renderer. Clip tiles correctly and bound repeated work for tiny/singular
   tiles without substituting the viewport for an invalid geometry box.

**Exit:** nonrectangular/curved fills, inherited paints, strokes, transformed and
templated resources pass; cyclic/missing references have specified fallback.
Pattern and gradient dependencies invalidate cached consumers.
Follow [SVG paint servers](https://www.w3.org/TR/SVG2/pservers.html).

### P7 — Complete stroke geometry, markers and paint order

**Dependencies:** P1, P3, P6. **Size:** large. **Coverage:** A5, A7–A8, M1.

Preserve zero dash entries, validate negatives/all-zero lists, repeat odd lists
and resolve percentage lengths consistently with DOM paint-hit helpers. Carry
`stroke-miterlimit` through paint/replay/backend data. Implement non-scaling
stroke in target geometry for general affine transforms rather than dividing
width by a guessed scalar scale.

Extend the existing path-emission tangent facts into reusable per-subpath vertex
metrics. Use them for start/mid/end markers on every listed shape, including
closed and degenerate segments. Resolve marker viewBox/aspect ratio, reference
point, orientation and units; context paint comes from P6. Honor `paint-order`
at the common fill/stroke/marker dispatch.

**Exit:** dotted/miter/non-scaling-stroke pixels and hit tests agree; marker
tangents/viewports and every paint-order permutation pass. Initially advertise
`vector-effect:non-scaling-stroke` explicitly. Inventory the remaining SVG2
vector-effect values in P0 and implement/test them as later P7 increments, or
keep the broader matrix row partial with the exact remaining values named.
Follow [SVG painting](https://www.w3.org/TR/SVG2/painting.html).

### P8 — Replace clip and mask approximations

**Dependencies:** P2–P3, P6–P7. **Size:** large. **Coverage:** K1–K3.

Build clips from all eligible children with their own transforms and clip rules;
combine children with the specified union semantics. An alpha-coverage clip is
available when one concatenated path cannot express that union. Apply the target
element transform and `clipPathUnits`, resolve CSS references, and preserve nested
clip lifetime/order/depth through replay.

Replace solid-shape source repaint/exclusion shortcuts with a mask surface
rendered through the ordinary SVG traversal. Handle mask region/content units,
gradient/text/image content, alpha versus luminance and overlapping black/white
content. Reuse shared effect surfaces with explicit premultiplication/color-space
conversion and document ownership; masks must compose, not repaint the source
once for each child.

**Exit:** multi-path unions, even-odd holes, transformed object-bounding-box clips,
gradient masks and general black cutouts pass, including nested masked groups.
Follow [CSS Masking](https://www.w3.org/TR/css-masking-1/).

### P9 — Add text-on-path placement

**Dependencies:** P5–P7. **Size:** medium/large. **Coverage:** T4.

Extend the shared path metrics used by markers with bounded arc-length sampling
and tangent queries; do not introduce a second SVG path parser. Resolve textPath
href, startOffset/pathLength calibration, anchoring, spacing and placement
under referenced-path transforms. Feed placed/rotated glyphs into P5 painting.

**Exit:** straight/curved/reversed paths, nested tspans, percentage offsets and
textLength have browser references. Referenced-path edits update cached text.

### P10 — Evaluate SVG filter graphs

**Dependencies:** P4, P6, P8 and the common effect boundary. **Size:** very large.
**Coverage:** E1–E4.

Build a document-owned filter program and per-render execution scratch. Resolve
`in`/`in2`/`result`, SourceGraphic/SourceAlpha, omitted-input defaults, primitive
subregions, filter/primitive units and color-interpolation rules. Capture any
renderable element's source through the same boundary; do not search only for
the first blur and ignore the rest of its chain.

Deliver primitives in dependency order:

| Step | Primitives | Oracle |
|---|---|---|
| F1 | Gaussian blur, offset, flood, merge, color matrix, drop shadow | Transparent shadow chain with named intermediate results and anisotropic blur. |
| F2 | Blend, composite, morphology | Two-input operators, arithmetic coefficients, erosion/dilation and edge cases. |
| F3 | Image, tile, turbulence, displacement map | Resource completion, repeated subregions, fixed-seed output and selected channels. |
| F4 | Diffuse/specular lighting and distant/point/spot light children | Normal/lighting calculations with explicit parameters and numerical tolerances. |

Reuse `render_filter.cpp` and replay surface operations where their contracts
match; extend them through Lambda-owned kernels where they do not. Memory and
work limits flow through the existing document memory coordinator. An unavailable
primitive must remain an explicit incomplete feature; a partial shadow rendered
as a blur is not accepted. Follow [Filter Effects](https://www.w3.org/TR/filter-effects-1/).

**Exit:** each F step passes independently on group/text/image/use and shape
sources, with nested transform/clip/mask/opacity tests. Report primitive coverage
individually; the filter row closes only after all four steps pass.

### P11 — Implement conditional content and embedded HTML

**Dependencies:** P2–P4, P8. **Size:** small for switch; very large for foreignObject.
**Coverage:** S1–S2.

For `<switch>`, evaluate the applicable conditional-processing attributes and
render only the first eligible child. Confirm SVG2 language conditions and the
compatibility treatment of legacy SVG1.1 attributes in browser fixtures; do not
invent a fixed feature list. Language/settings changes trigger invalidation.

For `<foreignObject>`, connect its parsed HTML descendants to the existing Radiant
layout/view tree under the SVG containing rectangle. Layout consumes logical
width/height; paint and event targeting compose its SVG CTM and clip. Reuse block,
inline, flex/grid, forms and document scripts without a second document engine.
Provide an external-image rendering context that follows SVG image restrictions;
it cannot inherit host selectors or acquire interactive page behavior by accident.

**Exit:** switch first/no-match cases pass. Embedded HTML renders through actual
layout, and pointer/focus/scroll/mutation tests work under translation, scale and
rotation. Transparent/offscreen and SVG/PDF fallback remain covered. Follow
[SVG embedded content](https://www.w3.org/TR/SVG2/embedded.html) and
[SVG structure](https://www.w3.org/TR/SVG2/struct.html).

### P12 — Add SVG animation on the document timeline

**Dependencies:** P2–P3, P6–P8; P9/P11 where animated content uses them.
**Size:** very large. **Coverage:** N1.

Reuse document scheduling in `radiant/animation.cpp`/`css_animation.cpp` and the
existing event loop. Add an SVG timing/evaluation adapter for animate,
animateTransform and set. Keep animated values separate from DOM base attributes
and compose them with the cascade and transform/resource resolver.

Stage simple duration/value sampling first, then begin/end/restart, repeats,
fill freeze/remove, keyTimes/keySplines/calcMode, additive/accumulate and event/
syncbase timing. Implement supported attribute value classes explicitly, including
colors, lengths and transforms. Inventory the animatable classes used by the
listed elements in P0, and add per-class interpolation increments; incompatible
values need the specified discrete/error behavior. Mark unfinished classes
individually rather than declaring the entire element supported.
Add SVG timeline controls where needed by the automated oracle. Stop scheduling
on detach/teardown and invalidate every affected cached dependency.

**Exit:** controlled-clock pixel samples at boundaries and repeats agree with the
browser; animated resource users and detached documents do not retain stale paint
or timers. Static exports use a documented deterministic time (initial document
time by default), with an explicit sample-time option only if added and tested.
The SMIL row remains partial until its published value/timing limits are closed.
Follow [SVG animation](https://www.w3.org/TR/SVG11/animate.html).

### P13 — Preserve completed SVG behavior in exports

**Dependencies:** P2–P12 for feature-complete export; add smoke coverage earlier.
**Size:** large. **Coverage:** X1–X2.

Replace source-only inline serialization with a resolved snapshot or equivalent
PaintIR lowering that carries CSS sizing, computed SVG style and the sampled
animation state. Emit required namespaces, remap IDs/references when embedding
multiple subscenes, and package external assets so output remains usable away
from the original working directory. SVG pictures must reach SVG/PDF through
the same subscene contract rather than being dropped by image-only lowering.

For PDF, preserve vector operations where available and use the shared SVG
renderer for fallback with real transparency and caller-selected density. Avoid
the current opaque-white, one-pixel-per-logical-unit surface. Carry content clips,
visible overflow, effects and target transforms through both lowerers.

**Exit:** exported SVG parses as XML and rasterizes to the resolved reference;
PDF tests cover colored backdrops, alpha edges, image SVGs and 1×/2× density.
General PDF text/font/page issues from §17 remain outside this SVG plan.

## 6. Order and milestones

| Milestone | Phases | User-visible result | Completion requirement |
|---|---|---|---|
| M0: reproducible baseline | P0 | Current gaps and source drift are evidenced. | Coverage map has actual reproducers and recorded oracles. |
| M1: icons and ordinary labels | P1–P6 | Correct defaults, page CSS, viewports, images, text and paint servers. | All mapped IDs pass, including cached repaint and both inline/external paths. |
| M2: diagram/effect geometry | P7–P9 | Full listed markers, stroke details, clips/masks and text paths. | Rendered and real-pointer gates pass, including deep/nested cases. |
| M3: advanced content | P10–P12 | Listed filter primitives, switch, embedded HTML and SMIL. | Each primitive/timing/content contract has a reproducible oracle. |
| M4: portable output | P13 plus aggregate validation | Completed behavior survives SVG/PDF export. | Input and export evidence are separately green; support matrix is refreshed. |

Within each milestone, finish a narrow change and its regression tests before
expanding the next subsystem. P2/P3 and exact bounds/retained ownership are the
critical foundations; advanced filters and foreignObject should not precede them.
Sizes are relative planning estimates, not calendar commitments.

## 7. Validation and closeout

### 7.1 Test matrix

| Dimension | Required coverage |
|---|---|
| Input route | Inline HTML, standalone SVG, img, CSS background, data URI, SVG image and external use; select relevant routes per feature. |
| Style/context | Presentation attribute, inline style, embedded stylesheet and host stylesheet; host CSS must remain isolated from external SVG images. |
| Geometry | With/without viewBox, nonzero origins, nested viewports, 1×/2×, nonuniform scale, rotation and fractional dimensions. |
| Rendering | Interactive headless paint, PNG export, cached layer eager/off/normal; SVG/PDF output for each exportable feature. |
| Mutation/lifetime | Edit paint/geometry/text/style/defs, remove/reinsert, change a referenced node outside the SVG root, load fonts/images and close/reload documents. |
| Pointer | Coordinate-based click/move assertions, DOM elementFromPoint and transformed shape/foreignObject targets. Do not accept synthesized fallback activation as evidence. |
| Invalid input | Path errors, unresolved/cyclic references, invalid lengths/colors/dashes, singular transforms and failed resources; deterministic progress and cleanup. |
| Platforms | macOS plus Linux/Windows smoke for raster/effects; use explicit capabilities, not OS assumptions about SVG software blur. |

Extend `test/test_rdt_vector_gtest.cpp` for actual geometry/replay contracts and
the existing DOM/render tests for integration. Pixel tests must assert both
positive ink and negative space/holes, with tight documented tolerances and
bundled fonts. Browser layout rectangles alone do not prove SVG paint correctness.
When adding a Lambda `*.ls` regression, add its expected `*.txt` result too.

### 7.2 Focused commands

Run from the repository root; all new scratch and captures live under `./temp/`.
Examples below use existing targets/CLI options; fixture names are selected as
each phase lands, and a zero-match run is a failure to validate.

```bash
make build-test
./test/test_rdt_vector_gtest.exe
./test/test_ui_automation_gtest.exe --suite baseline --test 'svg_*' --jobs 1
./test/test_ui_automation_gtest.exe --suite hit-test --test '*svg*' --jobs 1
./test/test_ui_automation_gtest.exe --suite editor --test svg-dom-contract --jobs 1
make dom-ui-run test='dom_svg_*' ARGS='--jobs 1'
make test-render pattern=svg
make lint ARGS='--rule ^no-int-cast-radiant$'
git diff --check
```

Use `RADIANT_SVG_LAYER=off` and `RADIANT_SVG_LAYER=eager` for paired pixel
comparisons, plus repeated normal-policy paint for cache invalidation assertions.
Capture Chromium with `CHROME_HEADLESS_SHELL` as documented in `AGENTS.md`.
Run real document/editor interaction fixtures serially when they share resources.
For performance measurements only, build with `make release`, record the exact
binary/source/fixtures and compare interleaved runs at matched backend/scale.
Do not infer performance from debug-build functional runs.

### 7.3 Required completion gates

1. Every completed coverage ID has a browser/WPT-derived semantic oracle, a
   focused regression and retained evidence under the phase's implementation
   record. Document exact unsupported residue for broad rows such as filters,
   vector-effect and SMIL; narrower subsets do not earn a full support label.
2. Run `make test-radiant-baseline` after engine changes and at milestone closeout.
   Run `make test-lambda-baseline` and `make test262-baseline` when shared runtime,
   rooting or JS/DOM adapters are changed. Node baseline is not part of this plan.
3. Report focused counts separately from aggregate counts, including failures,
   partials, skips, timeouts, environment blockers and missing oracles. A focused
   pass does not waive a required failing aggregate gate or authorize baseline
   changes that mask a defect.
4. Refresh §15 of `HTML_CSS_SVG_Support.md` only after the corresponding cases pass;
   refresh the SVG-related §17 rows from export evidence. Update
   [RAD_14](../../doc/dev/radiant/RAD_14_SVG_Vector_Graph.md) and the SVG2 progress
   record for the final architecture. Keep remaining issues in the central
   [Lambda Issue Ledger](../Lambda_Issue_Ledger.md), linking this plan.
5. Record each phase as completed only with its source changes, validation and
   remaining limits. The whole plan is done when every coverage package and
   required gate is satisfied, not merely when M1 is usable.

### 7.4 Progress record

| Phase | Status | Evidence |
|---|---|---|
| P0 | Partial | Live reproductions and Chromium oracles for six packages; source/ownership/cascade audit completed. Remaining packages still need fixtures and their own oracles. |
| P1 | Implemented | G3–G5: radius auto fallback/clamping, normal default line paint, atomic path parameter sets and valid-prefix recovery. Decimal/exponent/arc-flag grammar, zero-radius arcs, closepath termination and cross-kind smooth-curve reflection have focused unit coverage. The thin-rectangle optimization also stops at malformed command separators. |
| P2 | Implemented | C1–C5 and A4: host/isolated CSS adapter over the shared selector and cascade engine, selector lists/combinators/importance, inherited group font CSS, visibility with visible descendants, shared colors and styled gradient stops. Invalid paint declarations preserve earlier valid declarations. Retained SVG layers track host document and font-resource generations. |
| P3 | Implemented foundations | C6, G1, G6, R3 and A7: shared viewport/font length resolver and shape contours, CSS/attribute transform precedence with view/fill reference boxes and origins, shared viewBox/PAR mapping for paint/hits, conditional root/nested overflow, link container state and preserved zero/percentage dashes. Text/resource-specific unit consumers and complete stroke boxes remain in their later phases. G2 also includes visible-overflow opacity bounds. |
| P4 | Implemented | Shared PNG/JPEG/GIF/static-WebP images, placement/aspect fitting, isolated SVG resources, relative URIs, retained ownership/promotion and GIF-frame invalidation. |
| P5 | Implemented; T2 paint servers finish in P6 | UTF-16 positioning lists, repeated rotation, logical glyph metrics, inherited spacing/baselines, nested textLength, outline/bitmap coverage paint, decoration and shared character-cell targeting. Font/image documents own isolated font descriptors. |
| P6 | Implemented; aggregate Radiant validation passed | Typed inherited fill/stroke/currentColor/resource/context paints; gradient transforms, spread, focal circles, dynamic styled stops and local/external templates; resource-style font metrics; clipped pattern tiles with units/viewBox/templates and bounded sampling. Recursive use context paint preserves source bounds/frame/document. Marker context paint finishes in P7. |
| P7 | Implementation present; oracle review open | Affine non-scaling strokes, complete dash/miter/order data, authored marker topology and instance paint/hit geometry. Two original raster fixtures retain pending reviewed oracle corrections. |
| P8 | Implemented; focused validation passed | Common premultiplied effect capture for shapes, containers, text/tspan, image, use, symbol and SVG viewports; clip unions/rules/units/transforms; alpha/luminance/linear masks; exact clipped pointer targeting and mutation invalidation. Final aggregate gates remain required. |
| P9 | Implemented with focused validation | Shared path metrics, local/external path references, SVG2 basic shapes/inline paths, calibrated offsets, anchoring, reversed and closed paths, nested positioning, textLength, warped outlines/color glyphs and live pointer/mutation behavior. |
| P10 | In progress | F1-F4 graph kernels and focused browser/specification gates are implemented. Additional filter audits and final aggregate validation remain outstanding. |
| P11 | Implemented; export gate pending P13 | Switch selection and inline/isolated foreignObject layout, paint, input, mutation and image restrictions have focused gates. |
| P12 | In progress | Basic controlled-time SMIL passes 41/41 in Chromium and native UI; advanced value/timing gates remain open. |
| P13 | In progress | Resolved styles, SVG pictures, embedded HTML and transparent PDF capture pass the initial snapshot gate; portable/vector and platform gates remain outstanding. |

**Implementation record, 2026-10-02.** Radiant continues to emit PaintIR and
DisplayList commands through the existing ThorVG adapter. No vendor source
was edited. Viewport data remains float logical geometry (**RSC1/RSC6**);
per-render tables retain their existing scratch lifetime, and no deferred
payload borrows new temporary strings (**D4.2.2v2–D4.2.4**).

| Retained regression | Before the fix | Chromium oracle | Result after the fix |
|---|---|---|---|
| `test/ui/svg_geometry_defaults.{html,json}` | 9/20 pixels passed on a rebuilt pre-change release runtime | 20/20 | 20/20 |
| `test/ui/svg_paint_bounds.{html,json}` | 3/16 pixels passed | 16/16 | 16/16 |
| `test/ui/svg_css_colors.{html,json}` | 4/13 pixels passed; the four HTML controls already painted correctly | 13/13 | 13/13 |

The UI manifest's existing `baseline` glob owns all three fixtures; each has
one suite owner. Chromium oracle: `HeadlessChrome/143.0.7499.169` at 1×.
CLI raster checks repeat all 49 assertions at 1×/2× with
`RADIANT_SVG_LAYER=off` and `eager`: **196/196** samples passed. Results,
reference PNGs, binary hashes and build logs are under `temp/svg-support/`.

Focused validation: `test_rdt_vector_gtest.exe` **9/9**, including path
grammar, zero-radius arcs, extrema/degenerate derivatives, closed subpaths and continued
primitive geometry; `test_css_system.exe` **36/36**, including valid/invalid
HSL numeric syntax and separators; `test_color_gtest.exe` **9/9**.
Existing SVG clip/tiled-image/text/use paint fixtures pass. The seven SVG
hit-test fixtures pass **71/71** assertions, and `svg-dom-contract` passes
**9/9**. `make lint ARGS='--rule ^no-int-cast-radiant$'` passes.
The CoreGraphics bounds call was updated to its geometry-bound API; runtime
validation uses the active ThorVG backend on macOS. Linux/Windows and the
excluded CoreGraphics backend have not been runtime-tested in this batch.

**Aggregate gate, final debug profile:** `make test-radiant-baseline` passes:
**4,143 counted tests, 3,786 passed, 357 partially passing, 0 failed**.
The 193 baseline UI fixtures, 125 DOM fixtures, 66 view-command tests and 51
page snapshot fixtures pass. Render visual checks report 203/212 passing,
8 expected failures, 1 skipped and 1 new pass; WPT CSS syntax has 38 passed
and 5 skipped. The checkout's original release executable/profile is restored
after validation; its SHA-256 is retained with the 196/196 raster results.

The first debug-profile `make test-radiant-baseline` reported 3,775 passed, 357 partially passing and
10 failed. The failures included missing ignored PDF fixtures, a cached
ThorVG archive without its Lottie loader, a sandbox-blocked HTTP server, and
the page-layout snapshot. PDF fixtures were restored via the repository's
shared-test-directory convention; the sibling Lambda-opus checkout supplied a
Lottie-enabled archive, with both vendor trees clean at the same source revision
(`54a98e42813a84d3ac2314b818725f9eea8b1658`). The local cached Meson metadata
used an obsolete libc++ assertions flag rejected by the current SDK; reusing
the validated archive avoided a vendor source edit.
The six affected editor fixtures, both view fixtures and HTTP test then passed
focused rechecks. The debug-profile rerun then had only the page-layout snapshot failure
(3,785 passed, 357 partially passing, 1 failed). The checkout's original
release profile passes the page snapshot; all 51 page fidelity metrics match
the pre-change release exactly. The detailed runner reports 29 successful and
22 below individual fidelity thresholds, with 0 errors, on both binaries; the
snapshot gate compares the saved suite averages and passes. The pre-change reference was relinked from
527 retained release objects, all built before the first implementation edit;
its object manifest and binary hash are retained under `temp/svg-support/`.
A release-started aggregate run finished with 3,781 passed, 357 partially
passing and 4 failed. All four failures assert INFO messages compiled out by
`NDEBUG` (`lib/log.h`); the same four assertions fail on the pre-change release
in an isolated workspace fixture. After the host/test/module artifacts were
rebuilt, the final debug page snapshot passes at 91% elements and 91% text.
No layout snapshot or test expectation was changed to remove a failure.

**P2 implementation record, 2026-10-02.** Inline SVG resolves its live DOM
against host stylesheets; external images/use references create isolated,
per-walk document/style metadata. The old SVG stylesheet parser/matcher is
removed. Declaration priority is shared with DOM CSSOM/range queries; parsed
author rules carry author origin, and selector lists retain their strongest
matching specificity. Visibility retains geometry for visible descendants and
text measurement. Stops resolve style/currentColor/percentage opacity.
Document epochs invalidate host selectors and cross-root `<use>` resources;
font descriptor/source and glyph-cache generations invalidate retained paint.
These snapshots obey **D4.2.2v2–D4.2.4/D4.5.1v3**.

`svg_cascade` passes **21/21** browser-derived pixels, including group font
family/size/weight/slant and isolated external image styles. The real-click
`svg_cascade_mutation` passes **14/14** pixel/hit/attribute assertions after
host class changes, `<style>.textContent` replacement, visibility overrides
and definition mutation outside the painted SVG root. It exposed and repaired
missing stylesheet-owner reparsing for STYLE-classified text changes and raw
source lookup missing cross-root definitions. Both fixtures pass with layers
`off` and `eager`; static P1/P2 cases pass **280/280** raster samples across
1×/2× and both cache policies. Vector/cascade/font resource tests pass **12/12**.
The P2 Lambda baseline passes **2,104/2,104** input and **4,065/4,065** runtime
tests. The Test262 release-build prerequisite encountered the subsequent P3
helper refactor before it was complete; Test262 and a fresh Radiant aggregate
gate remain pending for the current combined batch.

P0's full exit gate and P3–P13's remaining packages are not complete. P3 now
has Chromium coordinate and overflow/dash fixtures (**46/46** pixel/hit
assertions); implementation is in progress. Later phases still require their
own reproductions and acceptance evidence.


**P3 implementation record, 2026-10-02.** `svg_resolve_length` supplies axis and
normalized-diagonal percentages, absolute units and actual font x-height.
`svg_append_basic_shape_path` supplies contours to paint, bounds and interaction;
the duplicate DOM shape builders are removed. Typed CSS transform decoding is
shared with box transforms, and viewBox/PAR mapping is shared with DOM CTMs.
CSS transforms override attributes; SVG origins use view/fill reference boxes.
`<a>` uses the common group state and its transformed child receives real clicks.
Inline SVG user space follows its committed CSS viewport when no viewBox exists.
Root/nested overflow is conditional; visible roots use direct recording because
a viewport-sized raster layer cannot contain their ink. Opacity groups bound
visible overflow by the ancestor paint clip. All dimensions remain float
(**RSC1/RSC6**); per-walk decoding and copied layer keys retain the existing
ownership rules (**D4.2.2v2–D4.2.4/D4.5.1v3**).

Chromium passes `svg_coordinates` **25/25** (including link click bubbling),
`svg_context_units` **21/21** and `svg_overflow_dashes` **23/23**. Radiant passes
all **201/201** assertions in 12 SVG paint/cascade/mutation fixtures with eager
layers, the existing SVG hit suite **71/71**, and editor contract **9/9**.
Static P1–P3 pixel assertions pass **476/476** across 1×/2× and layer `off`/`eager`.
Further phase-specific consumers will use these length/coordinate foundations;
the resolver/cascade/font/vector unit suite passes **14/14**. The P3 aggregate run exposed image/PDF failures repaired in P4 and was interrupted
by subsequent source changes; a frozen-source aggregate gate and Test262 remain pending.


**P4 implementation record, 2026-10-02.** SVG images now share `load_image` and
the PNG/JPEG/GIF/WebP decoders; codec setup/link dependencies are declared for
macOS, Linux and Windows. One source-to-image-rectangle transform implements
x/y, every aspect alignment, meet/slice/none and transformed overflow clipping.
Nested SVG images retain their intrinsic frame and isolated style adapter.
Chrome retains inner root letterboxing when explicit SVG intrinsic dimensions
differ from its viewBox; this compatibility behavior is tested separately from
the image rectangle's aspect fitting. Referenced images use secure image mode;
standalone SVG loads its own DOM and permits document-relative resources. SVG
fragment parsing preserves its context namespace, so `innerHTML` retains image
elements instead of rewriting them as HTML img. External use keeps its own
resource/style scope and uses the document cache and ready network resources.

Deferred fallback images transfer ownership to PaintIR and copy pixels into
DisplayList storage; document-cache images retain owner/generation references
(**D4.2.2v2-D4.2.4, D4.5.1v3**). A later shared decode promotion refreshes earlier
recordings before replay, and full-size decodes are not needlessly replaced.
Raw raster placement uses independent axis scaling instead of ThorVG picture
size fitting. File and data GIF animation updates image generation and invalidates
SVG layers. The common default visibility field now uses `VIS_VISIBLE`, matching
its readers rather than mixing the CSS keyword and Visibility enum domains.

Chrome references pass images **49/49**, image mutation **16/16**, standalone
relative resources **5/5**, GIF frame samples **6/6**, external use **8/8** and
shared cascade priority mutation **5/5**. Image and standalone static pixels
pass **212/212** at 1x/2x with layers off/eager. Eager SVG fixtures pass
**294/294** before adding the five priority assertions; four document-editor/viewer
image/PDF fixtures also pass. Vector/decoder/resource tests pass **17/17**,
DisplayList tests **77/77**, and the old SVG image payload render fixture returns
to its **0.63%** baseline. The exact selector-list audit separates three footer
copyright links from eleven other footer links in Bootstrap: **250 -> 251**
canonical recipes and **3302 -> 3309** shared payload bindings, with zero
recascade growth. Only those structural ceilings change; prior byte budgets
remain intact. Temporary audit instrumentation is removed.

P6-P13 and the full P0 coverage gate remain open. Required aggregate Radiant,
Lambda and Test262 gates must run against the final frozen source tree.


**P5 implementation record, 2026-10-02.** Text collects addressable Unicode
characters, consuming ancestor positioning lists in UTF-16 units after whitespace
collapse. Nearest descendant x/y/dx/dy entries override individual addresses;
rotation repeats its last entry. Font family lists, weight/slant and logical
advances use the shared font resolver rather than a ThorVG file-name metric
proxy. Letter/word spacing, baseline alignment/shift and textLength calibration
feed the same positions into glyph paint, DOM bounds and pointer character cells.
Positioned gaps and length-adjustment gaps remain untargeted; ordinary tracking
belongs to the character cell. Descendant textLength values calibrate first and
remain protected from ancestor glyph scaling, following SVG 2 §11.2.1/§11.9.

The font module exposes baseline-relative outline visitors; TrueType quadratic
conversion is shared with its rasterizer, and CoreText supplies supported native
outlines. Bitmap-only glyphs expose outer coverage and hole contours, preserving
stroke and fill=none. Empty outline stubs cannot suppress this fallback. Emoji
measurement and painting reuse the HTML emoji selector; retained color pixels
are copied out of the font cache. Resource walks and geometry-only documents
register their own copied font descriptors and release their temporary metadata
(**D4.2.2v2-D4.2.4, D4.5.1v3**); secure SVG image mode permits embedded font bytes.
Glyph paint shares inherited fill/stroke opacity and text/tspan compositing
scopes. Underline/overline paint before glyphs, strike-through afterward;
decoration geometry supports solid/dashed/dotted/double/wavy and shares the
HTML decoration helper. Ancestor decorations propagate through a descendant's
none value; explicit descendant paint remains local.

Eight static text fixtures pass **756/756** pixels at 1x/2x with layers off/eager.
Nine text fixtures, including two real mutation clicks, pass **210/210** with
layers both off and eager. The combined existing/new SVG and four document
image/PDF preview fixtures pass **535/535** assertions in 32 fixtures; existing
SVG hit tests remain **71/71**. Vector/font/codec/resource units pass **20/20**;
Radiant float-cast lint and git diff checks pass. Chromium references cover
positioning **27/27**, paint **20/20**, spacing/baselines **29/29**, textLength
**61/61**, embedded-resource fonts **18/18** and mutation **10/10**.

Three separately identified normative cases are not advertised as Chromium
agreement: Chromium 143 consumes x-list entries by code point for supplementary
characters, adjusts internal spacing in descendant textLength nodes and shifts
following text twice, and paints SVG decorations with its legacy solid fill/
stroke behavior instead of SVG 2 style/color extensions. The generated
rectangular font and specification fixtures make those assertions deterministic;
platform emoji coverage has a separate native test with an explicit absence skip.
General shaping remains the existing prerequisite described in P5. Gradient and
pattern text paint completes in P6; complete clip/mask/filter boundaries complete
in P8/P10. Aggregate milestone/final gates remain required.


### P6 paint-server increment (2026-10-02)

Typed paint retains declaration document/base, URL fallback, currentColor and
context references until consumer geometry is known. Linear/radial stroke,
spread, focal-circle facts, miter and owned dash arrays pass through PaintIR,
DisplayList, retained copies, SVG serialization and the ThorVG cache key. The
shared declaration adapter uses the existing CSS variable resolver through a
lookup callback; resolved values serialize substituted tokens rather than raw
var() source. CSS-wide paint/stop values respect inherited versus non-inherited
properties. Existing shared CSS variable limitations in the support matrix
remain outside this proposal.

One walk-owned resource registry shares parsed external documents, isolated
selectors/fonts, relative bases and cycle identity across paints/templates/use.
It releases parsed Input/picture/font owners after recording (**D4.1.3,
D4.2.2v2-D4.2.4, D4.2.6**); retained paint copies paths/stops/dashes/images rather
than borrowing those owners. Recursive use context paint applies the source
geometry box and source coordinate frame (**RSC1/RSC6**); absent context paints
nothing. All text glyphs and decoration share the complete text geometry domain.
Font-size ex and paint-resource em/ex use resolved font metrics for paint and DOM
geometry, completing those P3 consumers.

Pattern content renders once to an isolated tile and samples destination
coverage. Tile/content units, viewBox/PAR, transformed tiles, inherited content,
stroke coverage and fractional/tiny periods share this path. Suspended backend
clips now keep their saved entries separate from nested offscreen clips, even
when the clip stack grows. Surface allocation uses checked pixel products and
allocates one uint32_t per pixel. No vendor code was edited.

The current ThorVG software radial renderer clamps focal circles and imposes a
minimum radial radius. Lambda-side two-circle sampling preserves SVG 2 cone
coverage, focal-circle radii and degenerate/boundary cases using the same coverage
painter as patterns. See [SVG 2 radial-gradient notes](https://www.w3.org/TR/SVG2/pservers.html#RadialGradientNotes).

Six paint fixtures pass **652/652** pixels at both scales/cache policies; the
three text regressions raise the combined check to **936/936**. The actual
external-use mutation fixture remains **8/8**. Vector tests pass **24/24**,
including focal/spread cache separation and replay after the recording arena
dies; DisplayList tests pass **77/77**, retained storage tests **24/24**, and
Radiant float-cast lint passes. The aggregate Radiant milestone is running.
A broader focused UI attempt overlapped its prerequisite rebuild and encountered
a temporarily missing executable; its skips/failures are excluded and require
a serial rerun after the aggregate gate.

Chromium references agree for gradient/pattern/use context paint and CSS/font
metrics. Normative fixtures explicitly identify Chromium 143 disagreements:
coincident radial circles, boundary-focus repeat averaging and external paint
server template hrefs. Direct external paint/use URLs were captured over a local
HTTP origin; file-origin security failures are not used as pixel expectations.
P7-P13, platform smoke and final aggregate/export gates remain outstanding.

P6 aggregate closeout: `make test-radiant-baseline` passed all 4,185 recorded checks
(3,828 fully passing and 357 partially passing layout checks, with no failed required
checks). The serial focused rerun passed 39/39 GTests (37 fixtures plus two scheduler
checks). Evidence: `temp/svg-support/p6-radiant-baseline.log` and
`temp/svg-support/p6-final-ui-serial.log`. The earlier overlapping-build UI log is
excluded from acceptance evidence.


### 7.7 P8 clipping, masking and common effect boundaries — 2026-10-03

The approximation paths have been removed. The shared boundary captures source
content once, filters it, applies clip and mask coverage, then composites final
opacity, following [CSS Masking 1 sections 2, 5–7 and 9](https://www.w3.org/TR/css-masking-1/).
Root and nested SVG viewports and symbol instances establish their user-space
frame before evaluating effects; text/tspan captures reuse already positioned
glyphs. Child clip contours contribute a union with their own `clip-rule`;
object-bounding-box units retain nonzero origins. Text/image/stroke/gradient
masks use ordinary SVG painting, with explicit alpha/luminance and linear-RGB
conversion. Missing or circular masks produce transparent black. Invalid clip
references apply no clipping; an empty valid clip removes all paint.

Decoded images explicitly carry straight alpha and renderer surfaces carry
premultiplied alpha. ThorVG uploads convert an owned copy and include the alpha
mode in the cache key. Generated effect images copy their pixels and metadata
into deferred paint before the source owner is destroyed, honoring
**D4.2.2v2–D4.2.4/D4.2.6**. Shared analytical contour tests restrict clipped pointer
regions without turning masks into pointer exclusions (CSS Masking section 5).
No ThorVG source was changed.

Focused evidence under `temp/svg-support/`:

- `p8-effect-final-ui.log`: eight clip/effect fixtures passed, including 51
  assertions with actual point clicks, text/image masks, source validation and
  two cross-root mutation fixtures (27 assertions each).
- `p8-effect-hit-legacy-final.log`: all seven original SVG hit fixtures passed
  (73 assertions). Two previous expected hits were outside their strokes in
  both Chromium and the exact renderer. Expectations now target the root and
  each fixture adds a verified point inside the stroke; original files and
  browser captures remain under `temp/svg-support/`. This is independent of
  the pending P7 raster-oracle review.
- `p8-effect-scopes-after.log`: all 384 pixel assertions passed at 1×/2× with
  layers off/eager; the new scope fixture improved from 13/29 to 29/29 in every
  mode, matching Chromium. `p8-clip-sources-native.log`: another 52/52 assertions.
- `p8-paint-regression-raster.log`: all 872 gradient/pattern/context/radial/
  external-paint/font-metric/text pixel assertions passed across those modes.
- `p8-effect-ui.log`: 16 selected fixtures passed; `p8-effect-vector.log`: all
  27 vector tests passed, including owner release and alpha-mode cache reuse.

Fixture `oracle` metadata distinguishes normative exceptions: Chromium ignores
resources inside a display:none SVG, retains stale luminance coverage after
computed mask-type becomes alpha, renders nonexistent/circular masks unmasked,
and clips away a target for an invalid indirect clipPath use. CSS Masking
sections 6.1, 7.1, 9.1–9.2 and SVG2 linking section 16.1.2 supply those expected
results. Native checks preserve the specified behavior and raw browser evidence.
Full filter graph execution is P10; export, platform smoke and final aggregate
validation remain P13. P7's reviewed raster oracle corrections remain pending.


### 7.8 P9 — text-on-path implementation and focused validation

SVG2 [§11.8.2–11.8.3](https://www.w3.org/TR/SVG2/text.html#TextPathElement)
defines the reference geometry and placement used here. D4.2 ownership applies:
path metrics own their segments, text-layout scratch owns placement facts, and
font/image owners outlive every retained paint command. RSC logical/physical
coordinate separation is preserved in glyph outlines and image sampling.

`render_path_flatten_cubic` is shared by DOM stroke queries and arc-length
metrics; SVG uses its existing parser and canonical basic-shape decomposition.
Metrics omit move gaps, preserve contour closure, bound subdivision, and provide
sampling, endpoint extrapolation and restricted normal-field projection.
`textPath` resolves local/external references, the referenced element's own
transform, `pathLength` calibration and percentage `startOffset`. It handles
SVG2 `side=right`, inline `path`, anchoring, nested tspan positioning, textLength,
open-path clipping by glyph midpoint and one circuit of a closed contour.
Following ordinary text resumes at the path endpoint, adjusted for closed-loop
offsets. Invalid references omit their content. The inherited exact spacing is
retained; SVG2 permits the UA to use exact spacing for `spacing=auto`.

Align uses the glyph-endpoint chord. Stretch projects outlines, character cells,
decoration and color-glyph bitmaps through the same path-normal field, with
bounded adaptive edge subdivision. Pointer targeting and real clicks use these
placed cells. A CoreText discrepancy found by the bitmap reference was fixed:
emoji fallback now retains the selected platform face, whose baseline adjustment
is lost by raw CGFont reconstruction. The face-swap helper retains aliases before
releasing old references.

Focused gates:

- Seven portable UI fixtures: 150 assertions, all passing, including 40 real
  pointer/click assertions and two 19-assertion live reference-edit cases.
- Main/geometry/stretch/continuation pixels: 288 assertions across 1x/2x and
  cache off/eager. The separate macOS color-font smoke has 48 assertions;
  its 12 coordinates and +/-8 tolerance are unchanged across independently
  captured density-specific oracles. `p9-accepted-raster-results.json` records
  all 336 accepted assertions and the binary hash.
- Native vector 29/29, display-list 77/77, retained-display-list 24/24;
  Radiant float lint and `git diff --check` pass.

Chromium 143 agrees with all 33 primary pixels and all 40 interaction assertions.
It does not implement SVG2 basic-shape textPath references, `side=right`, inline
`path` or method=stretch. Equivalent explicit paths, independently positioned
continuation text, and an independently projected Ahem outline supply the geometry
oracles. An independent Chromium canvas bitmap and path-normal projection supplies
the color-glyph oracle, passing all 12 pixels at each density. Original captures
are preserved. Chromium also leaves hidden/zero-size referenced-path mutations
stale; the native mutation fixtures follow live SVG2 reference geometry, with the
browser differences recorded rather than loosening assertions.

Final aggregate and export/platform gates remain required in P13. P7's separately
rejected oracle corrections remain pending; this phase does not change them.

### 7.9 P10 — filter graph progress and focused F1–F4 gates

**Status: in progress.** F1–F4 kernels have focused validation; remaining
filter/export audits are not complete. No formal SVG painting ruling changed. Document resource ownership,
retained values and teardown follow **D4.2.2v2–D4.2.6/D4.5.1v3**; logical/physical
coordinates and density checks follow **RSC1/RSC6/RSC11–RSC12**.

The common effect boundary now captures SourceGraphic for any renderable target,
executes a named acyclic graph, then applies clip, mask and final opacity.
Programs own their strings, arrays and turbulence tables in document resource
arenas, key on mutation epochs, pin during execution, and register with the
existing memory coordinator. Unpinned stale programs are reclaimable. Execution
visits only the final primitive's dependency tree and retires each intermediate
surface after its last consumer. Raster captures, paint-server surfaces and
filter surfaces share checked allocation/reclamation; kernels charge a work
budget derived from the configured memory policy. The retained output owns its
premultiplied surface independently of execution scratch.

[Filter Effects 1 §§7–10](https://www.w3.org/TR/filter-effects-1/) supplies filter
and primitive units/regions, default and repeated result names, input defaults,
hard clipping and color-space contracts. Declared regions remain available for
image fitting and tile periods even when stored pixels are clipped by the filter
region. Unknown/forward result names use the implicit input. Malformed matrix
arity passes through; disabled blur/morphology radii preserve their input.

Implemented kernels and focused coverage:

- F1: anisotropic separable Gaussian blur, bilinear offset, flood, merge, 4×5
  matrix/saturation/hue/luminance operations, and drop shadow. Named chains,
  SourceAlpha, flood-only output, transformed sources and filter/clip/mask/opacity
  ordering are covered.
- F2: all 16 blend modes, mixing without compositing, all seven listed composite
  operators including arithmetic/lighter, and separable component-wise
  erosion/dilation. Nonseparable blend formulas use the shared CSS compositing
  kernel, following [Compositing 1 §10](https://www.w3.org/TR/compositing-1/#blending).
- F3: ordinary image/use resource traversal for feImage; tile repetition using
  the declared source subregion; selected-channel displacement preserving the
  source color space; and deterministic fractal/turbulence noise with seed
  truncation, unit-disk gradient rejection, octaves and stitched frequencies.
  Primitive bounding-box coordinates, cropped image placement, cropped tile
  periods and live href/reference/period/radius changes are covered.

Focused gates recorded under `temp/svg-support/`:

- `p10-f3-accepted-raster-results.json`: **824/824** pixels across 1×/2× and
  cache off/eager, with binary hash. This includes the filter fixtures and
  existing clip/mask/effect-scope regression pixels.
- Nine filter UI fixtures: **208 assertions** with cache off and again with
  cache eager, including real button clicks, source targeting and mutations.
- Native vector **36/36**, display-list **77/77**, retained-display-list **24/24**;
  memory-pressure, pin/reclaim/teardown, unused-tree work, seed regeneration,
  premultiplied extrema and source ownership are checked. Radiant float lint and
  `git diff --check` pass.

Chromium 143 matches the ordinary image/tile/operator/source cases. Independent
canvas noise follows the exact published §9.21 algorithm and passes all 16 pixels
at both densities. Chromium's Skia implementation retains outside-unit-disk
vectors and consequently passes only the four zero-frequency pixels of that
reference; original captures and the primary source inspection are preserved.
Chromium ignores `no-composite`, so an independently colored rectangle verifies
the specified mixing-only result. Section 9.11 leaves displacement interpolation
to the renderer: Lambda uses bilinear sampling, whereas Chromium uses nearest
sampling in the advanced-source fixture. An equivalent fractional-offset browser
reference verifies the single affected 1× edge; all coordinates and the ±8
channel tolerance remain unchanged, with independently checked 2× expectations.
The reusable independent references live in `test/svg/oracles/`.

F4 adds diffuse and specular Phong lighting with distant, point and spot children,
alpha-height surfaces, density-aware sampling kernels and cone-edge coverage. A
shared weighted endpoint calculation produces all nine specified Sobel boundary
kernels; affine-height numerical tests cover corners, edges and interiors.
Lighting colors resolve their CSS initial values and interpolation space;
diffuse alpha is one and specular alpha is the maximum premultiplied RGB channel
([Filter Effects 1 §§9.10, 9.19, 11](https://www.w3.org/TR/filter-effects-1/)).
Named graphs remain under the same D4 ownership and work-limit contracts.

Four lighting fixtures cover all six light/effect combinations, sloped alpha,
linear color, missing light, zero constants, rear lights, group/text/image/use,
bounding-box point coordinates, transformed clip/mask/opacity, and live
color/constant/exponent/position/child replacement. The F4 checkpoint records
**1,000/1,000 raster assertions** across 1×/2× and cache off/eager in
`p10-f4-accepted-raster-results.json` (with binary hash), **272 UI assertions**
in each cache mode, and native vector **37/37**, display-list **77/77**,
retained-display-list **24/24**, with float lint and diff checks. Chromium 143
returns opaque black for a zero specular constant; §9.19 yields zero RGB and
zero alpha. The original failure remains recorded; an equivalent zero-opacity
flood reference verifies the specified transparency at both densities without
changing fixture coordinates or tolerance.

The blur audit now implements `none`, `duplicate` and `wrap` using the declared
input subregion. Its six pixels pass all four density/cache combinations and a
numerical sigma-one test distinguishes transparent, duplicated and opposite-edge
samples. Chromium 143 ignores the latter two attributes. The independent
`svg_filter_blur_edges_reference.html` canvas computes normalized double-precision
Gaussian weights and input extension; six pixels pass at each density. Original
browser/native failures are retained, and separate 2× expectations select the
physical sample center with the same channel tolerance.

The standard-input audit adds lazy FillPaint, StrokePaint and backdrop capture,
with SourceAlpha/BackgroundAlpha sharing their corresponding image slots. Target
paint inputs use their original geometry/viewport basis over the filter region,
including solid/currentColor, gradients and patterns. Shared compositing now
uses the existing helper for two premultiplied pixels: merge and drop-shadow
previously called the straight-destination helper. A four-input numerical test
checks one capture per standard image and the correct semi-transparent result;
new rendered merge/shadow/mask pixels agree with Chromium at both densities.
The input checkpoint `p10-inputs-accepted-raster-results.json` records **1,116/1,116**
pixels (binary hash), **301 UI assertions** in each cache mode, native vector
**39/39**, display-list **77/77**, retained-display-list **24/24**, and float lint.

Backdrop capture replays preceding commands from the SVG isolation boundary,
then shares SourceGraphic's resampling into filter axes. SVG roots, nested
viewports and private effect/resource captures establish their own boundaries;
explicit SVG isolation composes through the common effect capture. A focused
17-pixel fixture passes at both densities and cache modes on root, isolated-group,
nested-viewport and rotated sources. Chromium 143 does not implement standard
paint/backdrop inputs; independently painted plain SVG reference rectangles and
offset copies verify all 17 pixels of each reference at both densities. Their
original browser failures and the main fixture assertions remain preserved.
The live input fixture passes **17/17 assertions** in each cache mode and in an
independently painted Chromium event reference. Real clicks change preceding
paint, offset, gradient stops, currentColor, stroke-none and ignored fill-opacity.

Fractional `feTile` periods now interpolate between opposite input borders
without rounding the declared repeat distance. The numerical regression first
reproduced an opaque constant tile fading to half-alpha in each axis; the fixed
kernel and 16-pixel rendered fixture pass, with an ordinary rectangle reference
at both densities. SourceGraphic/SourceAlpha capture is lazy through the existing
traversal callback. Nested capture, backdrop replay, resampling, final color
conversion and coverage composition share the graph's work counter. Compilation
admits arena growth, tokens, node and merge storage through the memory coordinator;
allocation failures are retried on a later acquisition rather than cached as a
permanent semantic failure (D4.2.2v2–D4.2.6, D4.5.1v3).

`p10-audit-accepted-raster-results.json` records **996/996 filter pixels** and
**1,364/1,364 including clip/mask/effect boundary regressions**, across 1×/2× and
both cache modes (binary SHA-256 recorded). All **351 UI assertions** in 19
filter fixtures pass in each cache mode. Native vector **41/41**, display-list
**77/77**, retained-display-list **24/24**, float lint and diff checks pass. The
first complete Radiant baseline found three failures: the pending P7 marker
oracle, an SVG font-shorthand regression, and missing PDF page images during
scrolling. The latter two have root-cause fixes and focused validation; the
aggregate gate has not been rerun and is not green.

Remaining before closing P10: aggregate gates and the final coordinate/resource
audit; P13 export/platform validation remains a later gate. P7's separately rejected
oracle corrections remain pending and have not been applied.

### 7.10 — Conditional content and embedded HTML (P11, in progress)

The shared conditional evaluator follows SVG2 §§5.7.1–5.7.5: the first eligible
direct child of `switch` is selected independently of display or visibility,
`requiredFeatures` is obsolete, unsupported `requiredExtensions` fails, and
`systemLanguage` uses case-insensitive preference-prefix matching. Empty authored
attributes retain presence semantics even when the HTML parser stores their
value as Lambda null. Never-rendered definitions remain referenceable; conditional
text spans use the same evaluator during glyph collection. Paint, bounds and
pointer traversal select the same branch. Preferred languages are document-owned
with platform discovery and a host setter that invalidates document generations
(D4.2.2v2–D4.2.6); no fixed language or feature table substitutes for user settings.

`svg_switch` passes **16/16 pixels** at 1×/2× with both cache modes, and Chromium
143 passes all 16 static assertions with explicit accept-language preferences.
`svg_switch_mutation` passes **25/25 assertions** in each native cache mode,
including text collection, selected bounds and coordinate-based rotated clicks.
Chromium leaves conditional branches stale after attribute mutations; the original
**13/25** failure report is retained. An ordinary SVG `display` reference validates
all **25/25** assertions independently. Native vector tests pass **43/43**.

The aggregate SVG text-path failure came from querying individual SVG font
properties without projecting the winning `font` shorthand. The cascade now
projects it after variable resolution, including reset values and family fallback
groups; `font:var(...)` survives parser validation. `svg_font_shorthands` passes
**18/18 pixels** at both densities/cache modes and in Chromium. The unchanged
`enhance5_spec_svg_text_path_01` visual check passes at **1.80%** (baseline 1.84%).

The unchanged Prince scroll fixture passes **5/5** after fixing PDF authoring:
each page previously emitted colliding `clip0`/pattern IDs into a single HTML
document. The shared PDF interpreter accepts a resource prefix and PDF assembly
supplies page namespaces; callers can also set `id_prefix` for multiple PDFs.
The dedicated Lambda regression returns seven true assertions, and browser
captures reproduce the missing images before the fix and show restored images
after it. SVG fragment lookup remains document-wide as required by SVG2 §5.6.

Inline `foreignObject` uses Radiant's existing block/inline/flex/grid/forms layout
under the SVG2 §12.2 logical containing rectangle and §12.5 HTML integration point.
Its HTML child paint is captured in containing-block coordinates before applying
the SVG CTM, preserving nested CSS clips, rounded overflow, stacking and opacity
(RSC1/RSC6/RSC11–RSC12). The original **5/25** basic and **13/18** CSS failures are
retained. Both fixtures now pass **25/25** and **18/18** pixels respectively at
1×/2× with caching off and eager, and Chromium passes the same assertions.

The interaction fixture passes **38/38** assertions in both cache modes and in
Chromium, covering actual translated/scaled/rotated clicks, unchanged screen
`clientX`/`clientY`, input focus and typing, scrolling, geometry mutation, CSS clips,
and removal/reattachment. The same SVG root hit walker delegates HTML targeting
after affine unprojection; HTML bounds project through the containing SVG frame.
Cached SVG layers include the document interaction generation, so wheel scrolling
invalidates their captured pixels without requiring a DOM attribute mutation.

The acceptance work exposed three ownership/compositing defects. Replaced SVG
roots now release their HTML descendants' external layout payloads. Form values
survive temporary removal of pass-local form properties while their registered
DOM control owner remains live (D4.5.1v3); the StateStore suite passes **10/10**
including the new detach regression. Opacity replay preserves premultiplied
channels in transparent captures. SVG vector tests pass **44/44**, and existing
switch/stroke/clip/text-path pointer fixtures still pass with both cache modes.

Isolated file/data SVG images use a private document/style/font context and the
same HTML layout/paint functions. The host's selectors do not cross that boundary.
SVG image processing blocks subordinate external images/backgrounds, scripts,
frames and active media while allowing embedded data resources (SVG2 §§2.2.4,
2.2.6, 12.5). The image fixture passes **16/16**, its restrictions fixture
**12/12**, and standalone SVG **4/4**, at 1×/2× with both cache modes. Chromium
passes the same fixtures; the image/restriction checks also have 2× captures.

Nested SVG inside transformed HTML integration points now composes CSS placement
with its enclosing SVG CTM for bounds and actual pointer targets: **12/12** in
both native cache modes and Chromium. Immutable viewport pointer coordinates
prevent nested SVG targeting from consuming the outer HTML's local coordinates.
CSS `var()` and font-shorthand projection are shared between geometry queries and
painting; SVG `calc`/`min`/`max`/`clamp` uses Radiant's existing evaluator with
viewport/font-relative leaves. The geometry fixture passes **7/7** in all four
native modes and browser densities, and its click/mutation fixture **8/8** in
native eager mode and Chromium. Local HTML opacity groups now clip against their
recording bounds rather than the host surface extent, including negative local
coordinates: **4/4** in all native modes and browser densities.

XML vocabulary gates exclude SVG/unknown namespaced content from HTML layout;
prefixed XHTML retains its local layout/type-selector identity. Namespace and
prefix fixtures each pass **4/4** in all native modes and Chromium. External SVG
uses `parse_svg_document()` and shared XML parsing, preserving case, CDATA,
namespace declarations and self-closing XHTML boundaries. The preceding HTML
wrapper incorrectly absorbed following SVG siblings. Existing external paint and
text resources pass **17/17** and **18/18** in all native modes; external-use live
mutation passes **7/7** in native eager mode. All original fixture expectations
remain intact. The float-cast lint passes. P13 still owns the SVG/PDF export exit
gate; aggregate validation has not been repeated since these changes.

### 7.11 — SVG animation (P12, in progress)

The first controlled-clock fixture freezes the SVG fragment's document clock,
then samples 0, 0.5, 1, 2, 2.5 and 4 seconds through real button clicks. It checks
numeric geometry, color interpolation, set visibility, transform repetition,
freeze/remove, and unchanged DOM base attributes. Chromium 143 passes **41/41**;
the native pre-implementation reproduction is **9/41**. The initial adapter now
passes **41/41** in native UI and **45/45** vector unit tests. Sample values are
document-owned and separate from base DOM attributes, and retained SVG paint
includes the sample generation. `svg_smil_values` adds a **63/63** Chromium gate
for keyTimes, discrete/paced/spline interpolation, from/by/to, additive and
accumulate; its native reproduction is **17/63** before that evaluator stage,
and the value evaluator passes **63/63** natively.
`svg_smil_timing` adds a **90/90** Chromium gate for interval/restart/repetition
boundaries: native reproduction was **64/90**, followed by **90/90** after shared
interval evaluation. The event/control fixture has **66/66** Chromium assertions
for clicks, begin/end syncbases, and DOM begin/end methods with offsets. Its
initial qualified-repeat case incorrectly assumed paused seeks would raise a
repeat event; the preserved capture is **58/66**. That case now uses the equivalent
scheduled `A.begin+2s` syncbase. The corrected event/control fixture passes
**66/66** natively. The running-clock fixture has a separate **5/5** Chromium
gate for begin/repeat/end callback order and qualified repeat activation. Its
native reproduction was **1/5**; a decimal-duration boundary fix restores frozen
final geometry (**3/5**). Shared timing-event delivery then passes **5/5** natively,
including qualified repeat activation. The preceding basic/value/timing/control
fixtures remain green after that change: **265/265** combined native assertions
and **45/45** vector units. The new `svg_smil_complex` fixture has **25/25**
Chromium assertions for point/path lists, object-bounding-box percentages and
animated root viewBox; native reproduction is **8/25** before that class stage.
Point/path vectors and sampled viewport setup then pass **25/25**, including
DOM path bounds after replacing their raw `d` read. The additional value-class
fixture passes **24/24** in Chromium and native UI for inherited percentage
gradients, filter vectors, dash/text lists and marker angles. Its preserved
mixed-unit input has **23/24** Chromium assertions; same-unit percentages form
the accepted reference without changing assertion count, positions or tolerances.
The sandwich/color fixture reproduces **51/72** natively and passes **72/72**
after activation-time priority, class-specific additive zero, `currentColor`
and linear RGB interpolation. Chromium passes **61/72**: its SMIL interpolation
ignores computed `linearRGB`, and its by-only color introduces the target's
current color. The normative assertions retain
[SVG 1.1 §19.2.12](https://www.w3.org/TR/SVG11/animate.html#AnimateElement)
and [SMIL §3.2.2](https://www.w3.org/TR/2001/REC-smil-animation-20010904/#AnimFuncValues)
for these two behaviors; the original captures and a computed-style probe are
preserved. All eight native fixtures pass with both disabled and eager SVG
layers at 1×/2×: **1,544/1,544** assertions. Vector units pass **46/46**, state
units pass **10/10**, and the float-cast lint passes. Sample storage has a
document-owned 64 MiB work limit and allocation failures do not publish a
completed cache generation (D4.2.2v2–D4.2.6).
The clock-only layout fixture now passes **30/30** in Chromium and native UI.
It covers animated outer SVG dimensions, `foreignObject` HTML reflow, input
geometry and unchanged base attributes. Box reads occur after the sampled frame
commits, following Radiant's existing host-turn geometry contract. The original
same-turn fixture/captures remain under `temp/svg-support`. Clock changes mark
the affected DOM layout subtree and ancestors dirty; the EventSim forced-render
helper now consumes pending reflow before painting and clearing dirty flags.
The value-error fixture reproduces **7/40** natively and now passes **40/40**.
The evaluator distinguishes numbers, integers, lengths, angles, colors, paint,
number/length lists, paths and discrete values rather than accepting a color in
any numeric attribute. It trims enum tokens, accepts a terminal value-list
separator, applies absent geometry defaults, normalizes optional numeric pairs,
resolves length-list units and rounds integer interpolation. Chromium passes
**29/40**: discrete to-only and nonadditive from/by-list assertions retain the
normative [SMIL value rules](https://www.w3.org/TR/2001/REC-smil-animation-20010904/#AnimFuncValues)
and [SVG list-type rules](https://www.w3.org/TR/SVG11/animate.html#Animatable).
The syncbase-cycle fixture passes **84/84** in Chromium, reproduces **71/84**
natively and now passes **84/84**. Timing queries iterate anchored cycles to a
fixed point, leave unanchored cycles unresolved, include future intervals needed
by negative offsets and retain every matching event offset. Queries have explicit
depth, instance, graph-size and work limits with a visible error on exhaustion.
The partial frozen-to fixture reproduces **9/17** in both Chromium and native UI.
Native now passes **17/17**, following [SMIL's frozen to-animation rule](https://www.w3.org/TR/2001/REC-smil-animation-20010904/#AdditiveAnim):
the underlying sandwich is sampled at the active end and its resulting value is
retained across later samples and base mutations. Rewind and animation-definition
changes invalidate that capture. Owned frozen strings have an 8 MiB document
limit separate from the 64 MiB sample limit (D4.2.2v2–D4.2.6).
The image-clock fixture passes **18/18** in Chromium and native UI for HTML
images, SVG images and CSS backgrounds, including frozen state and suppressed
image scripts. Each picture duplicate owns its elapsed time; the parsed cache
owner remains at zero. Image scheduler teardown cancels before releasing the
surface (D4.2.6). Its `MEMTRACK_DEBUG` run ends with **zero live allocations**.
The background shorthand now accepts the parser's canonical URL value through
the existing background-image resolver, including the function form.

Resource/filter to-animations now use marker/focal/filter initial values, and
invalid `attributeType` and mismatched path structures leave base values intact.
The 11-assertion priority fixture distinguishes the SVG 1.1 XML presentation
layer from the CSS animation layer and tests CSS importance on geometry.
Chromium's newer SVG animation model ignores `attributeType`; its 7/11 capture
is retained alongside the legacy SVG 1.1/SMIL §3.5 assertions. Geometry now
resolves sampled values through the shared cascade before paint and bounds;
`animateTransform` retains SVG transform syntax at that boundary.

The negative-offset fixture passes **64/64** in Chromium and native UI, covering
anchored self/mutual cycles, future syncbase lookahead and negative initial begins.
Accepted begin times remain stable when a restart changes its own syncbase end
([SMIL §3.6.8](https://www.w3.org/TR/2001/REC-smil-animation-20010904/#Timing-EvaluationOfBeginEndTimeLists)).
Timing work is shared across a sample/event walk: 1,048,576 operations overall,
16,384 per query, 512 graph nodes, 256 intervals/passes and 32 reference levels.
The 65,536-node/256-level sample traversal, 4,096 timelines, 65,536 controls,
65,536 document instance times and 256 instance times per control are explicit
bounds. Limit exhaustion reports an error; failed samples do not publish a
completed generation and a later smaller sample can recover. Owned state and
image clocks register memory statistics; retired controls/timelines are pruned
without discarding detached, pinned DOM state (D4.2/D4.5.1v3).

All fifteen native fixtures pass across disabled/eager SVG layers and 1×/2×:
**2,600/2,600** assertions (`p12-after32-ui-matrix.json`). Vector units pass
**53/53** after the resource-mode increment; state units pass **10/10** and float
lint passes. These supersede the earlier focused counts above.
External use instances now sample their host timeline while directly referenced
paint resources retain secure static processing, following
[SVG 2 §§2.3, 5.6.5](https://www.w3.org/TR/SVG2/conform.html#processing-modes).
The six pixels pass natively and in an equivalent inline Chromium reference.
Chromium leaves the external use animation static; its original captures and
the earlier over-broad resource-animation fixture remain preserved.
The original Chromium running capture was **4/5** because it did not expose
`TimeEvent.detail`; the callback-count oracle preserves event-order/count and
qualified-repeat assertions without depending on that missing public field.
No complete animation support claim is made from these gates alone.
Qualified attribute names, remaining value/timing audits, instance/cache scope
coverage and deterministic exports remain required (D4.2.6/D4.5.1v3).


The QName and type/default audit extends this gate to **18 fixtures and
2,660/2,660 assertions** across disabled/eager layers and 1×/2×
(`p12-after36-ui-matrix.json`). SVG 1.1 §§19.2.4–19.2.6 require the target to
remain in the current SVG fragment and a qualified attribute name to resolve
against the animation element's namespace bindings. Alternate XLink prefixes
now share DOM namespace lookup; the namespace fixture passes **4/4** natively
and in Chromium. Non-animatable filter `result`, inapplicable geometry and
invalid enumeration samples leave their base value intact. Filter/mask and
primitive percentage initial values now provide the XML underlying value for
to-animations. Vector units pass **56/56** after this stage. The five mixed
font-unit assertions agree with Chromium; the original percentage expectation
and captures are retained in `p12-font-units-initial.*`. This does not close
remaining timing/default, instance/cache or resource-lifetime audits.

### 7.12 — SVG export (P13, in progress)

The eight-assertion export fixture covers host CSS sizing/paint, gradient stop
CSS, an XLink use, embedded HTML, a sampled set, alpha over a colored backdrop
and an isolated SVG image. Native input and Chromium pass **8/8**. The original
SVG output failed XML parsing on an undeclared prefix; its PDF lost page CSS,
foreignObject, transparency and the SVG image. The initial resolved SVG
snapshot parses as XML and passes **8/8** in Chromium. The shared capture now
passes **8/8** in the PDF rendered by Poppler at one pixel per logical unit
(`p13-snapshot-after6-pdf-oracle.json`). The input explicitly sizes its colored
backdrop; the earlier zero-height input remains preserved.

SVG images now share `PaintSvgSubscene` with inline SVG, including the private
image clock and source path. Native and export image placement share one
object-fit/object-position helper. The PDF capture uses the host document's
CSS/font/embedded-HTML context, caller density and transparent straight-alpha
payload. The density/alpha unit passes at 1× and 2×; all **57/57** vector units,
**10/10** state units and float-cast lint pass after this stage. These are focused
gates: portable external assets, vector retention, overflow/effect clips,
namespace/platform smoke and final aggregate validation remain open.

The next export increment records the shared SVG renderer's final paint rather
than retaining source styles or timing definitions. Representable operations
can retain vector paths/gradients; unsupported pixel operations use the same
transparent capture. Recording owners remain alive through synchronous
lowering under **D4.2.2v2–D4.2.6**. This increment is still awaiting its build
and validation and is not counted as an accepted gate above.


**Wrap-up checkpoint (2026-10-03, user requested pause):** The final-paint
increment is accepted for the focused snapshot gate. The shared renderer now
records resolved geometry, CSS, sampled animation and referenced resources;
SVG lowers representable paint to vector operations and embeds transparent PNG
payloads for unsupported pixel operations. PDF retains its exact opaque path
operations and uses shared transparent replay for the other subscenes. Paint
clip paths retain a clone through deferred lowering (**D4.2.2v2/D4.2.6**).
The temporary source-DOM serializer and animation-value visitor were removed;
exports have one final-paint source of truth.

SVG output-scale reproduction passed **0/8** at doubled coordinates. The root
now scales its physical width/height while retaining the logical viewBox, and
passes **8/8** at 2×. The 1× snapshot also passes **8/8** in both SVG and PDF.
The 2× PDF passes **8/8**; Poppler reports the inline fallback at **320×160**
for the **160×80** logical scene, with a matching soft mask. The SVG artifact's
image links contain only embedded data URIs and its relocated copy passes
**8/8** in Chromium. Final focused units pass **58/58** vector and **10/10**
state; float-cast lint and `git diff --check` pass. Evidence is in
`temp/svg-support/p13-*-after8*` and `p13-snapshot-relocated-*`.

The proposal is **not complete**. Resume with the prepared fourteen-fixture
export matrix (`p13-export-fixtures.json`), expanded density/alpha-edge and
visible-overflow/effect/resource/font checks, namespace and platform smoke,
remaining P12 timing/default/instance/cache audits, P7's pending oracle approval,
then fresh Radiant/Lambda/Test262 aggregate gates and support-matrix updates.
No final aggregate gate was run after these export changes. The user's pause
supersedes the earlier instruction to continue until the proposal is complete.

# Lambda SVG Support — Implementation Plan

**Date:** 2026-10-02

**Status:** in progress; G3–G5, C4, G2 and A1 implemented; remaining packages planned

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
| P2 | Partial | C4: SVG uses `css_parse_color`; common HSL parsing and RGBA conversion are shared with CSS. The duplicate SVG named-color table and RGB parser are removed. C1–C3, C5 and A4 remain planned, including invalid declaration cascade fallback. |
| P3 | Partial | G2: save/composite bounds use the target viewport before viewBox/group transforms. Missing/nonzero viewBox and nested overlapping groups are covered. Units, overflow, CSS transforms and `<a>` remain planned. |
| P4–P5 | Planned | Image and positioned/stroked text work has not started. |
| P6 | Partial | A1: paths, polygons and polylines supply geometry bounds; shared cubic bounds use derivative extrema. Paint inheritance/strokes/templates, focal/spread transforms and pattern semantics remain planned. |
| P7–P13 | Planned | Stroke/markers, clipping/masks, textPath, filters, conditional content, animation and export remain outstanding. |

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

P0's full exit gate and P2–P13's remaining packages are not complete. The next
implementation milestone is the shared DOM/style adapter and its cache
invalidation tests, followed by viewport/length and typed-paint foundations.

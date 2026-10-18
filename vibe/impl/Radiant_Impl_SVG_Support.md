# Radiant SVG support enhancement plan

**Date:** 2026-10-18. **Status:** implementation active; the user-ratified
shared-parser repair is implemented, with qualification limits recorded in §12.
**Source reviewed:** `f29404b31`.

Deliver CSS-driven icons and diagrams, then close the missing component-transfer
filter primitive. Follow with a separate, bounded CSS geometry increment. The
user confirmed interactive-first (2026-10-18): inline icons, interactive charts
and diagrams define the leading acceptance workloads. Network sprites are
deferred by the user (2026-10-18); specialist illustration effects have separate
decisions below.

This follows the finite delivery model in the latest
[CSS implementation plan](Radiant_Impl_CSS_Support.md#1-delivery-scope-and-priorities)
and [Radiant workflow](../../radiant/README.md): finish named behaviors, retain
qualification limits, and reassess before extending a feature family. The
[support matrix §15](../../doc/HTML_CSS_SVG_Support.md#15-svg) supplies the current
advertised scope; [§17](../../doc/HTML_CSS_SVG_Support.md#17-output-targets) separates
SVG content from general HTML export. The
[previous SVG implementation record](Lambda_Impl_SVG_Support.md#716-p12-remaining-timing-instance-dom-and-final-closeout)
retains its completed inventory and outstanding platform gate.

## 1 Priorities and delivery boundary

| Order | Milestone | Priority and outcome | Fixed scope | Relative effort |
|---|---|---|---|---|
| 0 | Qualification intake | P0: distinguish current regressions from missing behavior | Six existing controls and one failing probe per selected gap | Small |
| 1 | Individual SVG transforms | P1: modern CSS can position and rotate icons and diagram groups | Static 2D `translate`, `rotate`, `scale` on shapes, `g` and local `use`; view/fill reference boxes | Medium |
| 2 | CSS paint and motion | P1: hover states, progress rings and chart updates reach SVG paint | Six SVG paint/stroke properties through transitions and keyframes; existing opacity/2D-transform controls | Large |
| 3 | Component transfer | P1: SVG art using channel/alpha remapping renders its filter graph | `feComponentTransfer`, four channel children, five transfer types | Medium |
| Shared | SVG DOM export and virtual-frame snapshots | P1: edited SVG saves as SVG and selected frames export deterministically | E0 current-DOM serialization and fixed virtual-frame acceptance across Milestones 1–3 | Medium |
| 4 | CSS shape geometry | P2: styles and variables control simple chart geometry | `rect`, `circle`, `ellipse`; nine applicable geometry properties | Medium |

**Core delivery ends after Milestones 0–3 and shared E0 export acceptance.**
Implement and qualify E0 alongside the selected feature milestones. Milestone 4
is a subsequent extension. Milestones 1, 3 and 4 require intake;
Milestone 2 also requires Milestone 1. This schedule records the user's
interactive-first priority; the feature milestones remain proposed.

CSS motion precedes new SMIL features because existing `animate`,
`animateTransform` and `set` already have a substantial delivered inventory,
while the matrix explicitly leaves CSS sampling open. Component transfer comes
before convolution because it supplies reusable channel/alpha remapping within
the existing filter executor. CSS geometry follows: attribute-based shapes
already work, so this adds a useful authoring surface without repairing an absent
basic-shape renderer.

A reproduced crash, stale owner or serious shared paint defect may preempt this
order. Give it an explicit blocker with a failing case and completion condition.
An isolated pixel fringe or a large synthetic failure count does not acquire P0
status by itself.

## 2 Existing support and remaining causes

Keep the previous plan's geometry, cascade, fonts/textPath, paint servers,
stroke/markers, clips/masks, fifteen filter primitives, foreignObject and SMIL
inventory as regression coverage. Do not schedule those families again merely
because their broad matrix rows remain partial. General text shaping remains a
font-engine dependency; it is not a new SVG text implementation task.

| Selected gap | Source evidence and owning stage | Implementation direction |
|---|---|---|
| Individual transforms | `radiant/render_svg_inline.cpp:2293`, `compose_element_transform`, reads only `transform` and returns early when it is absent. `lambda/dom/dom.cpp:12707`, `dom_svg_transform_from_element`, has the corresponding early return. | Extend one shared local-matrix computation and its DOM provider; reuse the existing individual-transform parsers and matrix composition. |
| CSS paint sampling | `radiant/css_prop_table.cpp:61`, `kCssPropertyRuntimeMetadata`, lacks SVG paint/stroke sampling entries. `radiant/render_svg_inline.cpp:1066`, `svg_dom_resolve_property`, reads authored declarations and SMIL samples. | Trace sample production, presentation reads and invalidation together. Adding registration alone cannot deliver animated paint. |
| Component transfer | `radiant/render_svg_inline.cpp:6846`, `svg_filter_kind`, recognizes fifteen primitives. `svg_filter_compile` invalidates a graph containing an unrecognized primitive. | Add a typed node to the current compiler/executor; preserve graph validation and ownership. |
| CSS geometry | `radiant/render_svg_inline.cpp:3017`, `svg_append_basic_shape_path`, uses `read_svg_number_attr`, which reads numeric Items/attributes and SMIL samples. | Resolve applicable geometry through the shared cascade before building the existing contour. Preserve numeric Lambda-built attributes. |

These are source diagnoses to verify with Milestone 0's visible probes. If a
probe already passes, retain the evidence and remove that task; do not invent
an adjacent failure to keep the milestone open.

The active [CSS qualification register](Radiant_CSS_Qualification.md#active-repair-request--2026-10-15)
already owns the seven remaining exporter groups, additional layout failures,
legacy paged migration and teardown qualification. Relevant shared failures
remain visible, but this plan does not duplicate that repair schedule. Historical
macOS SVG passes do not establish current Linux/Windows execution or a green
current aggregate.

## 3 Shared implementation constraints

| Authority | Constraint |
|---|---|
| **D2.11.1–D2.11.2** | Inline HTML SVG follows browser-standard foreign-content parsing; external SVG reuses the same HTML5 parser and Mark result model. XML/XHTML hosts keep their syntax under **D2.10.1**. |
| **D2.11.3–D2.11.4** | SVG source export serializes the current edited DOM. Animation snapshots capture the selected virtual frame, keep that clock fixed and remain at that frame when reopened. |
| **D4.1.3 / D4.2.2v2–D4.2.6** | Parsed SVG, compiled filters, resources and sampled payloads retain their actual document/pool owners. Copy or retain every value that survives the current walk. |
| **D4.5.1v4 / D4.1.4v5** | Preserve document and selected-view ownership and independent scratch arenas: “pin, gen-check, copy-as-value.” Cache retirement cannot invalidate retained paint. |
| **D4.6.1v4** | Extend canonical property identity and generated accessors; preserve existing IDs. |
| **D5.3.3 / D5.4.1** | Callback and DOM bridges use precise roots and the owning evaluator lifetime. |
| **D7.3.3–D7.3.4** | Reuse declared engine/DOM providers; do not introduce new upward host imports. |

These rulings are in the [formal design](../../doc/Lambda_Formal_Design.md).
For coordinate policy, where no formal ruling supplies the SVG-specific rule,
retain **RSC1 / RSC6 / RSC7 / RSC11–RSC12** in
[Radiant Scale](../radiant/Radiant_Scale.md): float logical geometry, distinct
device/output scale, and declared cache scale dependencies. No formal ruling is
changed by this schedule. Task labels below are acceptance IDs, not design IDs.

Use one computed value and geometry path for paint, DOM geometry, hit testing
and export. SVG shapes do not need ordinary HTML layout boxes. Reuse
`svg_get_dom_presentation_property`, `SvgLengthContext`,
`resolve_individual_transform_value`, `svg_resolve_local_transform`,
`svg_append_basic_shape_path`, and the existing animation/filter infrastructure.
Promote shared helpers through the coherent module header/provider instead of
copying them. Extract shared cases before adding another per-property variant.

Keep inline SVG in host CSS and external SVG images in their isolated style and
resource mode. SVG source export uses the current DOM, including edits, under
**D2.11.3**. A selected virtual frame supplies the settled state for a stable
snapshot under **D2.11.4**; capture preserves its time. Reuse the existing virtual
timer controls and DOM serialization through the declared provider boundary.
Rendered PNG/PDF and general HTML-page SVG output retain their resolved-paint
contract: vectors where supported, transparent fallbacks at the requested
density otherwise. ThorVG remains a backend; all changes belong in Lambda-owned
code.

## 4 Milestone 0 Qualification intake

**Bound:** run the existing `svg_cascade_mutation`, `cssom_svg_paint`,
`svg_geometry_defaults`, `svg_stroke_interaction`, `svg_filter_graph` and
`svg_export_snapshot` controls. Add one small failing probe for each of the four
selected gaps, with an independent static or browser reference. Capture the
reference once unless its input changes.

Record the source revision, exact binary/profile, fonts, viewport, density,
cache mode and resource base. Inspect the first disagreement along
`parse → cascade → sample/computation → geometry → paint/hit → capture`.
Inventory local tools and corpus links before scheduling broad verification.

Classify a failure as a selected-feature cause, a delivery blocker, existing
qualification debt or an environment problem. Reuse the central issue ledger
for new independent defects. Do not require every historical SVG fixture or
every CSS debt item to pass before starting a feature.

**Done:** every selected task has a visible reproducer or passing evidence;
the owning stage is identified; platform/tool limitations have an explicit
disposition. No support percentage is inferred from these controls.

## 5 Milestone 1 Individual SVG transforms

**Outcome:** a CSS-positioned icon or diagram group paints and receives input at
the same transformed location, including when `transform` itself is absent.

Include 2D individual translation, rotation and scale on basic shapes, `g` and
local `use`; numeric/px/percentage translation, angle units, `none`, variables,
and the existing `view-box`/`fill-box` origins. Reuse the common composition
order defined by [CSS Transforms 2 §5](https://www.w3.org/TR/css-transforms-2/#individual-transforms).
Keep attribute-versus-CSS `transform` precedence intact.

| Acceptance | Observable result |
|---|---|
| T1.1 | Individual-only transforms match equivalent transform-list references; a combined translate/rotate/scale/list case proves order. |
| T1.2 | A nonzero viewBox and `fill-box` with a centered origin produce the expected bounds. With a 20 × 10 fill box, `translate: 50% 25%` moves the shape by 10 and 2.5 units. |
| T1.3 | A class/variable edit moves paint, DOM matrix/rectangle queries and actual pointer targeting together; the old location stops hitting. |
| T1.4 | Local `use` and a group with two children retain instance/ancestor composition. Cache off/eager and PNG/SVG/PDF agree at 1×/2×. |

**Excluded:** 3D/perspective, new stroke-box accuracy, animated individual
properties, resource-transform expansion and exhaustive use-instance contexts.
Required changes to the DOM provider follow **D7.3.3–D7.3.4**.

**Verification budget:** one focused transform fixture during edits, neighboring
SVG transform/use hits once stable, then the required area gates. Keep one
export case per distinct composition/reference-box cause; avoid a Cartesian
product across every SVG element.

## 6 Milestone 2 CSS paint and motion

**Outcome:** CSS hover transitions and keyframes visibly animate an inline icon,
chart mark or progress ring, with coherent computed reads and cache updates.
Shared E0 acceptance saves DOM edits and captures a stable selected virtual
frame for the implemented animation profile.

The six new sampling properties are `fill`, `stroke`, `fill-opacity`,
`stroke-opacity`, `stroke-width` and `stroke-dashoffset`. Include solid sRGB
colors, resolved `currentColor`, scalar opacity and compatible numeric/px
stroke lengths. Verify existing `opacity` and 2D `transform` consumers on SVG
descendants. Reuse the existing document scheduler, timing/easing, interpolation
and animation state; add no SVG-specific CSS clock.

Apply sampled values through the shared presentation adapter. Account for both
CSS sample changes and SMIL generation in any affected style/paint cache key.
Keep base attributes intact and honor the distinct animation/transition cascade
origins, including their precedence against important declarations. Behavior follows
[CSS Animations](https://www.w3.org/TR/css-animations-1/) and
[CSS Transitions](https://www.w3.org/TR/css-transitions-1/).

| Acceptance | Observable result |
|---|---|
| A2.1 | At controlled start/mid/end times, a fill/stroke transition has the expected computed color and visible pixels; retargeting starts from the current sample. A linear width transition from 2 to 6 reaches 4 halfway through. |
| A2.2 | A keyframed progress ring changes dash offset and width; opacity controls change actual coverage. Remove restores the underlying value, authored important values override keyframes, and active transitions retain their specified cascade precedence. |
| A2.3 | An inherited group color/stroke sample reaches an unstyled child. Pausing and resuming through existing controls produces deterministic samples. |
| A2.4 | A transformed moving shape has matching paint/hit geometry at the midpoint. Detach/reinsert does not retain a stale animation owner or cached sample. |
| A2.5 | Cache off/eager agrees. Existing SMIL controls retain their behavior; initial and selected virtual-frame paint match independently authored static references. E0 verifies that the serialized SVG frame remains stable after reopening. |

**Excluded:** gradient/pattern/URL-paint interpolation, `none` interpolation,
dash-list interpolation, geometry-property animation, animation of isolated
external SVG image CSS, CSS animation inside use-instance trees, new Web
Animations interfaces, and additive CSS/SMIL composition on the same property.
The latter remains a separate composition task; do not change its precedence as
a shortcut for this milestone.

**Verification budget:** two causal fixtures, one for transitions and one for
keyframes, plus a focused detach/owner control. Use the existing controlled clock
instead of sleeps. Exercise AST/MIR when script/DOM bridges change, and scoped
ASan for retained samples/teardown. Run the full SMIL matrix once after stable
changes to shared sampling or generation logic, not after every property edit.

## 7 Milestone 3 Component transfer

**Outcome:** a graph containing `feComponentTransfer` can adjust channel colors
and alpha without losing the filtered source.

Implement `feFuncR`, `feFuncG`, `feFuncB`, `feFuncA` with `identity`, `table`,
`discrete`, `linear` and `gamma`. Extend `RdtSvgFilterNode` and the existing
compiler/executor with owned typed channel data and bounded table storage. Use
one parameterized transfer evaluator. Reuse graph inputs/results, regions,
color-space conversion, surface budgets and retained-program retirement.

Compute on unpremultiplied channels, then clamp and restore premultiplication;
respect `color-interpolation-filters`. These semantics and transfer definitions
come from [Filter Effects §9.7](https://www.w3.org/TR/filter-effects-1/#feComponentTransferElement).

| Acceptance | Observable result |
|---|---|
| F3.1 | Each transfer type matches analytic channel values, including endpoints, missing-channel defaults and one alpha remap. A linear channel with slope 0.5/intercept 0.25 maps 0.4 to 0.45 before final byte quantization. |
| F3.2 | A translucent source and a transparent pixel match a reference after sRGB and linearRGB processing; identity preserves the source. |
| F3.3 | A named-result chain with existing color-matrix/blur/composite nodes produces visible expected output on a group containing text and an image. |
| F3.4 | Editing a transfer parameter invalidates the program; a pinned old recording remains valid until release. Oversized tables/allocation failure follow existing admission and cleanup paths. |
| F3.5 | One isolated SVG image and one inline graph produce correct 1×/2× PNG/SVG/PDF output, including transparent fallback and relocated SVG resources. |

**Excluded:** convolution, new filter DOM wrappers, animated transfer parameters,
rewriting CSS filter functions, native backend specialization and filter-wide
conformance expansion. Unknown primitives retain explicit failure diagnostics;
do not make arbitrary unsupported graphs pass by dropping nodes.

**Verification budget:** focused channel/executor units, one graph/mutation
fixture and one portable export fixture. Run neighboring filter graph/resource
tests and scoped ASan once the owned node format stabilizes. Preserve existing
memory budgets; no new silent truncation or independent allocation ladder.

## 8 Milestone 4 CSS shape geometry extension

**Outcome:** stylesheet rules and custom properties resize/reposition simple
chart shapes, and every geometry consumer observes the same result.

Include `x`, `y`, `width`, `height`, `rx`, `ry` on `rect`; `cx`, `cy`, `r` on
`circle`; and `cx`, `cy`, `rx`, `ry` on `ellipse`. This is nine distinct
properties, with applicability checked per element. Support numeric/px values,
percentages, `em`/`rem`, existing linear length-percentage math, CSS-wide values
and variables through shared validation/computation. Reuse registered width and
height identities rather than adding SVG duplicates (**D4.6.1v4**).

| Acceptance | Observable result |
|---|---|
| G4.1 | Stylesheet/inline CSS wins over applicable presentation attributes; removing the winning declaration restores the attribute or initial value. Geometry properties do not inherit by default. |
| G4.2 | Nested viewport percentages and a font/variable edit change geometry correctly. In a 200 × 100 viewport, a rect width of `calc(50% - 10px)` uses 90 units. Computed values preserve unresolved percentage dependencies until used-value resolution. |
| G4.3 | Auto/missing radii, zero/negative dimensions and invalid substitutions follow explicit specification-derived cases; existing numeric Lambda-built attributes still render. |
| G4.4 | One changed rounded rectangle updates paint, geometry bounds, object-bounding-box gradient/clip and pointer targeting together; a changed circle and ellipse prove the other shape cases. |
| G4.5 | Literal-attribute reference shapes match initial/mutated PNG/SVG/PDF output at 1×/2× with cache off/eager. |

Use [SVG geometry properties](https://www.w3.org/TR/SVG2/geometry.html) for
applicability and bases, retaining raw browser differences separately if the
reference implements a different revision. Geometry for `image`, `foreignObject`,
nested/root SVG sizing and use/symbol viewports is excluded, as are CSS `d`,
path morphing, geometry animation and exhaustive newer-unit combinations. Keep
the matrix partial for those contexts.

**Verification budget:** one applicability/cascade fixture and one causal
geometry/paint/hit fixture; neighboring shared contour and attribute tests;
then required CSS/DOM/Radiant gates. Add a unit only for a shared computation
that the visible fixture cannot adequately distinguish.

### 8.1 Shared SVG DOM export and virtual-frame acceptance

**Outcome, USER 2026-10-18; D2.11.3–D2.11.4:** export the current SVG DOM,
including edits, and use the frame selected by the virtual timer for stable
animation snapshots. E0 is shared core acceptance across Milestones 1–3.

The namespace-aware DOM walk is now exposed as `dom_serialize_xml` in
`lambda/dom/dom.cpp`, behind `dom_xml_serializer_serialize_to_string`. Reuse or
expose that shared walk through the DOM/provider boundary; never copy it into
the painter. Preserve SVG elements, text, attributes, styles, child order and
resource references. A DOM `<text>` remains text and a filter graph remains SVG
elements. General HTML-page SVG/PDF rendering retains its separate paint route.

Reuse `js_event_loop_set_virtual_clock`, `js_event_loop_advance_virtual_time`,
the document animation scheduler and the existing SVG timeline controls. Let
the selected frame's due work, sampled animation values, style and layout settle,
then capture without advancing or resetting virtual time. The existing virtual
frame controls define advancement; export adds no independent clock or sleep.

CSS/SMIL sampled values can live outside DOM base attributes. For a frozen
snapshot, project those effective values onto an export copy of the current SVG
DOM and make animation drivers inert in that copy. Preserve the live DOM's base
values and animation declarations. Reopening the snapshot must display the
selected frame. The export-copy step reuses the current animation/presentation
adapters and namespace-aware serializer under **D7.3.3–D7.3.4**.

| Acceptance | Observable result |
|---|---|
| E0.1 | Insert/remove a shape, edit its attributes/style and change SVG text; export and reload preserve the edited tree, text, IDs, namespace identity and local references. |
| E0.2 | Select a nonzero virtual frame and settle its work. Repeated exports at that fixed frame preserve the same content and visible state despite elapsed wall-clock time; capture leaves the virtual timestamp unchanged. |
| E0.3 | A CSS paint/transform sample and an existing SMIL sample match independent static references after the exported SVG is reopened. Cache off/eager and 1×/2× captures agree; the exported animation stays frozen. |
| E0.4 | Edit the SVG at the selected frame and export again; the edit appears. Capture preserves live base attributes and animation declarations. Advancing the existing virtual timer then produces the next sampled state normally. |

**Scope:** current-DOM SVG save and deterministic virtual-frame snapshots for
the selected feature inventory. Wall-clock animation capture, animated/video
export, authoring-tool metadata and new PDF vector/text capabilities stay
outside this delivery. Network sprites retain their existing deferral.

**Verification budget:** two fixtures, an edited static SVG and a virtual-frame
animated SVG. Reuse the current event/virtual-clock harness, DOM serializer
checks and export runner. Assert the reloaded tree as well as visible pixels;
earlier paint-only SVG/PDF comparisons do not qualify DOM serialization. Run
the required area gates after the shared implementation stabilizes.

## 9 Qualification and completion

During implementation, run the focused cases listed per milestone. After stable
changes, run `make test-radiant-baseline` and the additional required gates from
the [test map](../../test/README.md#1-which-gate-do-i-run): CSS input checks for
registry/parser changes, Lambda baseline for DOM/runtime changes, and Test262
when JS adapter source changes. A multi-area merge follows the map's aggregate
requirement. A failed required gate remains a qualification failure; do not call
it green because the new fixture passed.

Run `make test-svg-export` after stable shared paint/transform/filter changes;
use named fixtures in the existing `test/svg/test_svg_export.cjs` runner during
edits. Use `make test-svg-paint`/`make test-svg-smil` for their affected inventories.
Register new acceptance fixtures with their existing UI/export owners; do not
build a second harness. Check nonempty paint, causal mutation and real pointer
targets. E0 additionally checks current-DOM serialization and fixed-frame
reopening. Native-to-native agreement alone cannot establish correctness.

Run layout float-cast lint when geometry changes, the CSS inventory check when
wiring changes, and boundary validation when provider placement/imports change.
Use scoped ASan for lifetime-sensitive work. Performance claims require release
binaries and a finite representative workload; a performance project or
monolith split is not a prerequisite for this feature schedule. Keep all scratch
and capture artifacts under `./temp/`.

The remaining Linux/Windows P13 gate is a separate qualification track. On each
available platform, run the portable export inventory and a focused native
control covering transforms, component transfer, text and image isolation at
both densities. A build-only pass does not close runtime smoke. Unavailable
platform execution stays pending; it need not reopen a delivered macOS feature.

Close a milestone when its named cases pass, required gate results and failures
are recorded against the final source/binary, and excluded contexts are stated.
Update the support matrix only to the demonstrated scope. Keep a short receipt
of delivered behavior, remaining behavior, verification and next action; link
raw evidence instead of appending a second historical test-count narrative.
Documentation-only edits require link/source/diff checks, not engine builds.

## 10 Ordered deferred work

Promote one row only for a named workload, a finite input/context list and a
verification budget. Existing common-page blockers can change this order.

| Order | Candidate | Why deferred and proposed bound |
|---|---|---|
| 1 | Remote external sprites/resources | Deferred by the user, 2026-10-18; outside this delivery. A future milestone requires a settled loader/readiness/origin policy. Its proposed first bound is same-origin HTTP(S) external `use` through the existing loader, retaining image-mode restrictions. No wholesale `SvgResourceManager` replacement. |
| 2 | `feConvolveMatrix` | Useful for imported illustration effects; lower reuse than component transfer. Start with a bounded kernel profile, specified edge/alpha behavior and explicit rejection outside its budget. |
| 3 | `animateMotion` / `mpath` | Adds animated diagram paths; existing transform animation already covers common motion. Define path-distance, rotation and clock scope before implementation. |
| 4 | Further SVG CSS | Geometry on images/viewports/foreignObject, CSS animation in image/use documents, stroke-box accuracy, sampled CSSOM and remaining paint interpolation. Each needs its own named context. |
| 5 | Text shaping and rendered-page output | Complex shaping belongs to the shared font plan; searchable/selectable text in rendered-page export and wider native PDF paint need separate output requirements. SVG DOM save preserves its existing text nodes under **D2.11.3**. |
| 6 | Architecture and conformance tail | Full resource/cache API redesign, persistent ID indexes, cache eviction/rotated-layer acceleration, broad SVG DOM reflection, discard and at-risk vector effects require demonstrated need. |

Minor SVG/PDF edge differences retain their existing qualification owner.
No full-SVG2 claim, blanket WPT import, universal native-vector export or complete
cross-product of elements/effects/units is part of this delivery.

## 11 Confirmed scope decisions

**Confirmed scope (2026-10-18):** defer network sprites. No new remote-resource
loading milestone is required for this delivery.

**Confirmed workload priority (USER, 2026-10-18):** interactive-first. After
qualification intake, deliver individual SVG transforms, then CSS paint and
motion, then component transfer. Acceptance centers on CSS-styled icons,
progress rings and interactive charts/diagrams, including coherent live paint,
geometry and pointer targeting. CSS shape geometry remains the subsequent P2
extension.

**Confirmed parser rulings (USER, 2026-10-18):** **D2.11.1** requires browser-
standard parsing for SVG embedded in HTML. **D2.11.2** requires external SVG to
reuse that same HTML5 SVG parsing path and Mark result model, reducing parser
duplication. This supersedes the earlier recommendation to retain separate XML
intake for SVG pictures. Explicit XML/XHTML hosts retain their syntax under
**D2.10.1**. The working SVG2 design is reconciled to these rulings.

**Confirmed export contract (USER, 2026-10-18):** **D2.11.3** saves the current
SVG DOM, including edits. **D2.11.4** uses the frame selected by the virtual
timer for stable snapshots; capture preserves that frame and virtual time.
Wall-clock animation capture is outside the requirement. Shared E0 acceptance
defines the finite implementation and verification scope. Rendered-page
SVG/PDF keeps its existing paint contract.

The workload, parsing and export decisions are settled. No open design question
blocks the proposed milestones.

## 12 Shared SVG parser implementation record

**2026-10-18, D2.11.1–D2.11.2:** standalone loading already embeds its SVG source
in an HTML document. SVG pictures instead called `parse_svg_document` in the XML
parser, producing different results for the same markup. The external intake
now calls the existing HTML5 document parser and selects its SVG subtree using
`ElementReader`; `Input::root` retains the common `#document` owner. The XML
parser no longer owns an SVG-specific entry point or permissive SVG root branch.
The picture loader, local external references and image resources reuse this
entry without introducing a second tokenizer or tree builder.

Two parser regressions fail on the predecessor and pass with the shared path.
They compare complete external/inline serialized subtrees for case adjustment,
duplicate/unquoted attributes, recovery, namespace spellings, CDATA and HTML
integration points; the owner case releases source bytes before reading the
published tree. A third regression covers a malformed self-closing prefixed
HTML child and ordinary foreignObject closing tags. The shared current-node
namespace helper replaces the tokenizer's private predicate and also controls
end-tag case matching; HTML descendants cannot close camel-cased SVG ancestors.
All 68 HTML parser checks pass, including unchanged strict XHTML admission.

Isolated SVG style documents now explicitly use the HTML page kind. Leaving
that kind unknown had reapplied XML namespace-prefix lookup after HTML parsing.
The shared namespace lookup also distinguishes actual XMLNS attributes from
literal `xmlns:a` attributes in HTML; a new animation unit preserves real
namespace declarations while rejecting those literal aliases. All 166 vector
and animation units pass, and the complete 128-run controlled SMIL matrix passes
all 3,864 assertions at both densities and cache policies.
FO's already-parsed XML graphics emit local SVG/XHTML names and canonical
XLink/XML attributes through the existing unwrapped HTML formatter, preserving
their original pixel controls without a second SVG parser.

Chromium 152.0.7977.42 independently captures both inline-HTML and original XML
interpretations of the two existing foreignObject fixtures and the SMIL QName
fixture. Their twelve pixel expectations now follow the HTML interpretation in
**D2.11.2**, with every sample and original channel tolerance retained. Six
focused native UI fixtures pass all 81 assertions, including ordinary SVG images,
resource isolation and the combined export snapshot. That snapshot's reference
to the same SMIL asset uses the independently verified HTML color.

External image/use/paint-server export controls now use canonical HTML SVG and
XLink names, retaining unknown prefixed-element controls and exact PNG/SVG/PDF
comparisons; the group passes. The FO comparison's external SVG asset likewise
uses unprefixed SVG names while its XML-hosted instream graphics remain qualified.
The graphics comparisons pass with their geometry and paint controls intact;
the wider FO group still fails page-count controls in `flow_traits`,
`policy_bindings` and `compound_flow_queries`, and preview/PDF pixels in
`sheet_keeps`.

Portable export covers 18 SVG fixtures at both densities in PNG, relocated SVG
and PDF: **108/108 format comparisons and 2,394/2,394 pixel assertions pass**.
This combines the full matrix with the corrected snapshot replay on the same
binary SHA-256; it is not a second full-matrix run. The matrix's three additional
CSS controls retain two failing `css_containment_boxes` PDF comparisons (four
pixels); they are not changed here. Module-boundary validation, the Radiant
float-cast lint, formal-index consistency and diff checks pass.

The required aggregate receipts remain qualification limits:

- **Radiant baseline:** 5,803 passed, 10 failed, 160 partial, one skipped.
  This run predates the final HTML XMLNS lookup repair; its SMIL namespace
  failure is subsequently covered by passing focused UI and complete SMIL
  checks. The other failures are seven paged-media controls, a kitchen-sink
  page-load crash and an input-events runner idle timeout. An isolated crash
  trace reaches background MIR satellite compilation; no predecessor A/B
  establishes whether it predates this work.
- **Input baseline:** 4,335 passed, 47 failed. This run precedes the external
  SVG export-source migrations described above; replay confirms the SVG export
  group and FO graphics comparisons. The raw failures also include Wiki,
  script-format round trips and broader PDF/layout groups. The full aggregate
  is not claimed green and has not been rerun after fixture migration.

Evidence and exact failed cases: `temp/svg_parser_unification/validation.json`,
the two `*_gate/test_summary.json` files, `pdf_focus_results.json`, the export
reports and browser captures in the same directory. No Linux/Windows execution
or full HTML/SVG standards-conformance claim is made by this repair.

## 13 Current implementation checkpoint

**2026-10-18, base `06f4d14f9`:** implements the confirmed interactive-first
proposal and the P2 shape extension. E0 follows **D2.11.3–D2.11.4**. Network
sprites remain deferred. Evidence lives in `temp/svg_enhancement/`; the intake
binary is retained there for causal comparisons. The final aggregate gate completed with failures outside the focused SVG
inventory; this checkpoint does not claim a green required gate.

- **Intake:** six existing controls pass 147 assertions on a fresh build. New
  failing probes isolate individual transforms, CSS paint samples, component
  transfer and CSS geometry before the corresponding fixes.
- **Transforms:** SVG paint and DOM matrices share the existing individual
  transform decoder and composition. The shape/group/local-use mutation and
  pointer fixture passes; the native matrix unit checks CSS and attribute lists
  plus percentage reference boxes.
- **CSS paint/motion:** effect-owned typed samples feed paint and DOM queries;
  base captures cannot read their own animation samples. SVG leaves receive
  cascade/motion preparation, sample changes expire presentation caches, and
  interrupted transitions retarget from the visible sample. Focused keyframe,
  inheritance, importance, virtual transition and cancellation checks pass;
  the neighboring native CSS motion suite passes 147 tests.
- **Component transfer:** all five function types execute on straight channels
  before repremultiplication with the remapped alpha. Tables belong to compiled
  program arenas, have a 4,096-value per-channel cap and use existing graph/work
  budgets. The last function per channel wins. Native arithmetic/alpha/color
  tests and visible inline/image/filter-chain/mutation controls pass. The new
  compiler ownership/admission case passes, including pinned-table mutation,
  last-child selection, invalid input and over-limit rejection.
- **CSS geometry:** seven appended standard names plus existing width/height
  keys reach rect/circle/ellipse through the shared contour builder. Typed math,
  percentages, owner fonts, variables, CSS-wide defaults, two-way auto radii and
  invalid-substitution defaulting share computation with CSSOM. The geometry
  and cascade/applicability probes pass. Numeric Mark attributes remain valid.
- **DOM/frame export:** `render_svg_dom_to_file` reuses `dom_serialize_xml` with
  an optional projection. It writes selected CSS/SMIL values and inert animation
  drivers without cloning a live DOM or mutating its attributes. Standalone SVG
  source saves use this route; ordinary page SVG/PDF retains paint export. The
  existing render event can select an SVG root with `target.selector`. Edited
  static and virtual-frame reload probes pass 24 assertions, including unchanged
  live source/time, subsequent timer advancement and edited frame re-export.
  Consecutive fixed-frame exports are byte-identical.
- **Independent evidence:** Chromium 152 agrees with 20 initial transform/motion,
  13 transfer/geometry and 20 geometry/cancellation checks. The final expanded
  geometry/resource/font fixtures also pass 32 browser checks. DOM/frame snapshots
  pass reopened-file qualification at both densities/cache modes and 14 independent
  Chromium pixel probes; standalone source save retains the filter graph and
  its four channel elements, with no raster substitution.
- **Focused qualification:** 16 UI fixtures pass 276 native checks, including
  the final ownership, resource-bounds and live-font controls. The nine new
  fixtures pass 36 density/cache runs and 464 assertions on each of AST and MIR.
  Pause/resume, detach/reinsert and underlying-value restoration are covered.
  A CSS geometry edit updates object-bounding-box gradient/clip paint and hits;
  owner/root font edits update em/rem geometry and restore it. The existing SMIL
  inventory passes 128 runs / 3,864 assertions. Native vector and CSS animation
  suites pass 169 and 147 tests. Portable export passes 154/156 format checks
  and all SVG fixtures; the two PDF containment failures reproduce with the
  saved intake binary at both densities. The initial three SVG export regressions
  were caused by rejecting unitless CSS geometry; Chromium accepts these values,
  and the corrected shared validator restores all 18 affected format checks.
  SVG reload at 2× exposed missing navigation density propagation; the event
  navigation path now uses the shared document raster-scale synchronizer.
- **Sanitizer:** six full focused CLI fixtures pass with ASan and leak detection
  disabled. The frame fixture passes its 15 assertions then reports a shutdown
  use-after-free at `lib/mem_context.c:203`; an existing HTML animation/navigation
  control with no SVG export reproduces the same retained-fragment parent-node
  failure. Separate ASan save and fresh-process reopen pass all 15 frame checks.
  This records the failure; it does not claim a green single-process lifetime
  gate. A stale decoder object initially broke the ASan link; rebuilding that
  profile's `codec_aom.o` from current configuration restored the supported build.
- **Required aggregate:** `make test-all-baseline` completed (192 runners):
  **17,795 passed, 359 failed, 160 partial, one skipped**. The core Lambda
  executable suite passes 1,620 tests; the UI batch passes 640 tests; native
  Radiant passes 208. Library and validator lanes pass. Failed areas are paged
  composition, clipboard/paste WPT cases, PDF/fidelity and script-format checks,
  wiki formatting, concurrency, Node preliminaries and the retired Bash/Ruby
  inventories. Radiant's lane has 12 failures (seven paged, five clipboard/paste);
  the PDF visual runner has 24 failures. These remain failures; the scope does
  not include repairing their other subsystem owners or weakening expectations.
  The additional owner fixture and extended G controls were qualified separately
  after the aggregate UI batch; no production changes followed the aggregate
  build. Preserve `aggregate_summary.json` and `all_baseline_final.log` with the
  focused receipts. Broader failure attribution is not asserted merely from a
  passing SVG probe; only the two portable PDF cases have intake-binary A/B
  evidence here.
- **Document/tool checks:** generated name checks, layout float-cast lint, formal
  index regeneration, new local links, diff checks and the module boundary pass.
  CSS inventory regeneration fails on existing missing PDF-writer evidence
  (`test/lib/test_pdf_writer_gtest.cpp#TransparencyGroupsBalanceAndRetainNestedVectorResources`);
  that receipt is not removed or replaced with unrelated SVG evidence.

### SVG save and selected-frame use

`lambda render drawing.svg -o saved.svg` saves the standalone current SVG DOM;
SVG source dimensions and animation declarations remain authored. Raster output,
PDF and HTML-page SVG continue to use render/export sizing. Native interactive
callers use `render_svg_dom_to_file(root, path, freeze)` after their existing
frame flush; `freeze=true` captures the already-selected virtual frame. The
existing event/virtual-timer control route exposes the same behavior:

```json
[
  {"type":"advance_time","ms":1000,"steps":10},
  {"type":"render","target":{"selector":"#drawing"},"file":"temp/frame.svg"}
]
```

Standard `XMLSerializer.serializeToString(svg)` keeps its ordinary current-DOM
serialization behavior; it does not freeze animation. Capturing a frame never
adds a timer, resets the clock or requires a wall-clock sleep. Wider animated
external-resource/use-instance snapshots remain unqualified under **D2.11.4**;
network-sprite delivery and wall-clock animation export remain deferred.

## 14 Outstanding work follow-up

**2026-10-18, base `c3505c090`:** follow-up to §13. The selected feature work is
implemented; required aggregate qualification and unavailable platform execution
remain separately reportable. Evidence: `temp/svg_outstanding/`.

| Item | Delivered behavior and verification |
|---|---|
| Lifetime blocker, **D4.2.1v3** | `MemContext` follows explicit backing-child edges across contexts and reparenting, rather than assuming reverse registration/context order. Subtree unregistration uses the same allocation-free traversal. Seventeen context and seven factory tests pass with ASan; the full frame-export/reopen and independent HTML navigation reproducers exit without the former use-after-free. |
| **F3.3** | A named color-matrix → blur → composite → component-transfer chain filters a group containing SVG text and an image. Both source types paint correctly and change after a channel edit; native and Chromium controls pass. PNG/SVG/PDF pass at 1×/2×. |
| **G4.5** | Initial and live edited rect/circle/ellipse geometry matches independently authored literal-attribute references byte-for-byte in PNG/SVG/PDF at 1×/2× with caches off/eager: 24 actual/reference format pairs. The existing export runner now supports live event sequences and reference documents. Comparing exports at the same origin avoids unrelated translated-curve rasterization differences. |
| Live capture | PDF render events now borrow the settled current edition through the existing Lambda PDF exporter (**S2.6.6v3**). HTML-page SVG capture carries raster density into its outer dimensions, retaining CSS-unit viewBox coordinates. Source saves retain **D2.11.3**. |
| Broader **D2.11.4** inventory | Rendered local/file-backed `use` instances serialize separate sampled definitions with rewritten local IDs/references and sibling paint definitions. Generated prefixes avoid authored-ID collisions. Local SVG image imports embed a frozen SVG sampled at the existing picture time. Export/reopen preserves independent click-triggered instance states, the image frame and filter resources; later timer advancement still works. Repeated fixed-frame files are byte-identical. Chromium passes 20 reopened pixel checks plus unique-ID/reference/driver checks at both densities. |
| CSS inventory | Reconciled four references to the removed native PDF writer with the current **S2.6.6v3** PDF implementation. Removed stale native-Form/ink-tracking/ASan qualification claims, retaining the old visual cases for requalification. The current isolated-alpha PDF case and opacity UI controls pass. Added bounded SVG geometry qualification; generation/check now pass. TSV trailing empty fields retain their delimiters. |

**Focused receipt:** six fixtures pass 24 density/cache runs and 224 assertions
on each of AST and MIR. Five full CLI fixtures pass under the supported ASan
profile, including the imported-resource frame and live edited PDF capture;
leak detection is disabled. Memory-tracker leak reports remain and are not a
leak-free qualification. The separate memory unit runners are ASan-instrumented.
Layout float-cast lint, module-boundary validation, CSS inventory and diff checks
pass. No production source changes followed the final qualification build.

**Required qualification:** the restricted aggregate completed 172 runners
before repeated macOS graphics/XPC denials made the viewer case pool unproductive.
Its results remain in `aggregate.log` and the original `test_output/`; the
unfinished 20 runners then completed with native graphics/loopback access and a
separate `remaining_results/` directory. This is a failed aggregate qualification,
not a green continuous run. Both batches use the same final binary
`b74a5fcf8c669c2e9decc34679adeab9d9e1034137b4b69ecc30239e7d760afe`.

The remaining batch reports 7,410 passed, 171 failed, 160 partial and one skipped.
Core Lambda passes 1,620/1,620; viewer cases 208/208; UI 644/644; DOM UI 160/160;
paged media 491/491; vector 169/169; CSS motion 147/147; forced-GC 313/313.
Layout meets its recorded baseline with partial support retained. Its failed
owners are Node preliminaries (158), script-format round trips (12) and one
15-second kitchen-sink page-load timeout. That page subsequently passes three
isolated replays with the same limit (4.54/3.51/3.48 seconds); its original timeout
is not removed. Five local database-server failures and the platform-trust-store
failure pass all seven scoped controls with required access. The restricted
video runner stops at its first decoded frame; all 14 video cases pass when
replayed with native media access.

Other recorded owners remain wiki formatting, retired Bash/Ruby inventories,
PDF schema validation and runtime context/cache/concurrency. The 306 export-parity
cases execute before the restricted PDF runner stalls on WebGL; all 22 failed
parity case names occur in the earlier receipt. A scoped WebGL rerun completes
with graphics access but retains its existing PDF pixel failures. The two PDF
input cases prevented by that stall are replayed separately: one passes and the
Poppler page comparison fails. Case identity alone is not broad causal attribution;
no assertions, goldens, skip lists or support floors were weakened.

**Portable gate:** 178/180 format checks pass, including **all 162 SVG-fixture
checks**. Only `css_containment_boxes` PDF at 1×/2× remains failed, matching the
two intake-binary comparisons in §13. The 24 live geometry/reference format pairs
pass against the final binary. The independent imported-frame browser check,
AST/MIR density/cache matrices and scoped ASan receipts also use that source.
`qualification_summary.json` links the separate run results without rewriting
the failed gate.

**Remaining scope:** Linux/Windows runtime/export smoke is pending; macOS cannot
establish it. Nested resource closure, SVG images inside use templates, complete
use-host inheritance and other resource combinations remain unqualified under
**D2.11.4**. These require named, finite workloads before further expansion.
Network sprites retain the user’s deferral; wall-clock capture remains outside
the requirement. No new design ruling is needed for the delivered follow-up.

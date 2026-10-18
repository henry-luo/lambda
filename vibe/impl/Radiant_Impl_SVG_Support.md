# Radiant SVG support enhancement plan

**Date:** 2026-10-18. **Status:** feature milestones proposed; the user-ratified
shared-parser repair is implemented, with qualification limits recorded in §12.
**Source reviewed:** `f29404b31`.

Deliver CSS-driven icons and diagrams, then close the missing component-transfer
filter primitive. Follow with a separate, bounded CSS geometry increment. This
order assumes inline icons, interactive charts and locally supplied document art
are the main workloads. Network sprites are deferred by the user (2026-10-18);
specialist illustration effects have separate decisions below.

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
| 4 | CSS shape geometry | P2: styles and variables control simple chart geometry | `rect`, `circle`, `ellipse`; nine applicable geometry properties | Medium |

**Core delivery ends after Milestones 0–3.** Milestone 4 is a subsequent extension,
not a condition for shipping the core. Milestones 1, 3 and 4 require intake;
Milestone 2 also requires Milestone 1. The order is a delivery recommendation,
not a normative design ruling or a measured frequency ranking.

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
resource mode. Preserve the existing resolved-paint export contract: vectors
where supported, transparent fallbacks at the requested density otherwise.
Current CLI exports sample initial document time; this scope adds no new clock
option. ThorVG remains a backend; all changes belong in Lambda-owned code.

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
| A2.5 | Cache off/eager agrees. Existing SMIL controls retain their behavior; initial-time PNG/SVG/PDF matches an independently authored static reference. |

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
targets. Native-to-native agreement alone cannot establish correctness.

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
| 5 | Text and vector-output expansion | Complex shaping belongs to the shared font plan; searchable/selectable export text and wider native PDF paint need separate output requirements. |
| 6 | Architecture and conformance tail | Full resource/cache API redesign, persistent ID indexes, cache eviction/rotated-layer acceleration, broad SVG DOM reflection, discard and at-risk vector effects require demonstrated need. |

Minor SVG/PDF edge differences retain their existing qualification owner.
No full-SVG2 claim, blanket WPT import, universal native-vector export or complete
cross-product of elements/effects/units is part of this delivery.

## 11 Scope decisions and open design questions

**Confirmed scope (2026-10-18):** defer network sprites. No new remote-resource
loading milestone is required for this delivery.

**Confirmed parser rulings (USER, 2026-10-18):** **D2.11.1** requires browser-
standard parsing for SVG embedded in HTML. **D2.11.2** requires external SVG to
reuse that same HTML5 SVG parsing path and Mark result model, reducing parser
duplication. This supersedes the earlier recommendation to retain separate XML
intake for SVG pictures. Explicit XML/XHTML hosts retain their syntax under
**D2.10.1**. The working SVG2 design is reconciled to these rulings.

1. **Which workload should determine the order?** The proposed core favors
   inline icons and interactive diagrams. If static illustration import/export
   is the priority, move component transfer ahead of CSS motion.
2. **Does enhanced export require editable vectors or sampled animation time?**
   The recommendation is to retain current resolved vectors plus density-aware
   transparent fallbacks and initial-time snapshots. Requiring searchable text,
   all-vector filters/PDF or a CLI sample-time option materially changes scope
   and needs a separate bounded output milestone.

These questions do not prevent review of the proposed phases. New export
requirements need a separate bounded milestone; the parser question is resolved.

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

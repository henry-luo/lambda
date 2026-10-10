# Radiant CSS Support — Remaining Implementation Plan

**Status:** Paused at the user's request on 2026-10-10, at the validated generated-content style-containment checkpoint (§9). P0 inventory/shared-value fixes, size-container queries and containment increments are delivered; P0–P2 acceptance remains open.

**Predecessor:** [CSS selector/property implementation record](<Radiant_Impl_CSS_Selector_Property_Support (retired).md>), closed and retired with unfinished scope transferred here.

**Baseline:** [HTML, CSS and SVG Support](../../doc/HTML_CSS_SVG_Support.md), audited 2026-10-10; source checkpoint `eb75fe63b` plus the support-document update.

**Scope:** Remaining CSS selector, cascade, value, layout, paint, motion and interaction consumers, including their CSSOM and export surfaces. This is the active CSS implementation backlog.

**Authority:** An implementation plan, not a new semantics/design ruling. Ownership follows **D4.5.1v4 / D4.1.4v5**, property identity **D4.6.1v4**, native roots/context lifetime **D5.3.3 / D5.4.1**, and module boundaries **D7.3.3–D7.3.4** in [Lambda_Formal_Design.md](../../doc/Lambda_Formal_Design.md). No formal ruling defines this CSS priority order. New unresolved architectural choices require a working design record before implementation; this draft changes no ruling.

## 1. Starting point and delivery contract

The predecessor delivered substantial shared machinery: selector specificity and validation, namespace/relative/state selectors, cascade layers and nesting, custom-property substitution and bounded expansion, live CSSOM interfaces, typed math and grid lengths, logical borders/radii, animation/transition consumers, SVG presentation properties, scrolling, markers, selection and file-button paint. These are the starting implementation, not tasks to implement again.

The current registry has **409 unique property definitions**. The older **342 / 244 effective / 43 partial / 55 parsed-only** classification is historical; it does not classify the current registry. HTML responsive images, PDF Unicode glyph outlines and the explicit paged compositor also now exist. Remaining work must be based on a current reproducer, not an earlier progress paragraph saying a feature was absent.

Starting focused evidence was 523/523 native view/pagination tests, 112/112 vector tests, and 38 CSS Syntax files passing with six skips. The starting export audit recorded 171 passes, 29 failures requiring unavailable Poppler tools, and one Poppler-dependent skip; refreshed delivery results appear in §9. These checks do **not** establish full CSS conformance or a fresh all-green aggregate. See the support report for dated baseline inventories and output-specific limits.

For each delivery:

1. Reproduce a bounded missing behavior on the current source and identify its owning stage. If it already works, retain evidence and remove it from the active slice.
2. Fix the shared cause, keeping parser validation, cascade, computation and consumer responsibilities distinct. Reuse existing grammar, substitution, logical mapping, style ownership and paint lowering.
3. Demonstrate invalid-input/defaulting behavior, the visible or interactive effect, and live mutation where applicable. Compare browsing, raster, SVG, PDF and paged views only where the feature has a relevant consumer; keep their statuses separate.
4. Record the remaining limits and update the support matrix. A registered name, a computed string, or a produced file alone does not close a support item.

A priority is a delivery order, not a requirement to finish an entire tier before touching the next. Only the stated dependencies block a task. A reproduced crash, lifetime error or wrong cascade winner in any tier takes P0 priority. Do not turn an unlimited grammar/API audit into a prerequisite for every feature.

## 2. Reprioritized order

The order favors incorrect existing behavior and features used by common responsive pages, then text/paint/motion fidelity, then compatibility breadth that needs larger new models. Rare selector extensions and browser API reflection no longer drive the entire CSS schedule.

| Priority | Work | Why this position | Dependencies |
|---|---|---|---|
| P0.1 | Current evidence and bounded support inventory | Prevents repeating completed work or treating historical failures as current | None |
| P0.2 | Shared cascade, substitution and computed-value correctness | A bad winner or wrong unit base affects many families | Current reproducer from P0.1 |
| P0.3 | Live condition and mutation invalidation | Initial paint passing while edits/resizes are stale is a user-visible failure | Relevant P0.2 computation |
| P1.1 | Containment, size container queries and container units | Largest missing responsive CSS consumer | P0.2–P0.3; container dependency design |
| P1.2 | Grid/intrinsic sizing and logical geometry residue | Existing layout support still loses common sizing forms | Relevant P0.2–P0.3 paths |
| P1.3 | Everyday text layout and decoration | Truncation, wrapping, ruby and underline defects affect ordinary documents | Existing inline/font pipeline |
| P1.4 | Multilayer backgrounds and CSS image selection | Common authored backgrounds remain narrower than HTML images | P0.2; existing image selector/paint contracts |
| P1.5 | Scroll/overflow and control-part fidelity | Required for usable panes, forms and editors | P0.3; existing input/scroll state |
| P2.1 | Shaping, bidi, vertical text and font features | High visual impact, but requires a coherent text pipeline | Font/text design; P1.3 interfaces |
| P2.2 | Existing CSS paint and export parity | Recovers effects already advertised in raster output | Relevant P1 work; shared paint IR |
| P2.3 | Animation/transition and individual-transform completion | Completes the large existing motion investment | P0.2–P0.3; P2.1/P2.5 where needed |
| P2.4 | Selector, language/direction and shadow-tree residue | Finish real invalidation/identity gaps before rare new selectors | P0.3; shared DOM identity/slot state |
| P2.5 | CSSOM, registered values and SVG CSS computation | Finish observable semantics around existing consumers | Relevant P0.2, P1 and P2 consumers |
| P2.6 | Remaining generated/control/top-layer parts | Each needs a separate layout/paint destination | P1.5; P2.4 for shadow parts |
| P3.1 | Broader color, image and value functions | Adds compatibility breadth after common consumers are sound | P0.2; P2.2/P2.5 |
| P3.2 | New exclusion, mask and positioning/layout models | Requires substantial geometry/state rather than registration | Focused design; P1.2/P2.2 |
| P3.3 | Motion paths, scroll timelines and discrete transitions | Builds on a complete basic motion/scroll contract | P1.5/P2.3 |
| P3.4 | Platform/state-dependent compatibility | Depends on browser/platform services or policy decisions | Explicit owner/model per feature |

### First delivery slices

Start with a small P0.1 inventory for the next slice, then reproduce the remaining fixed-length computed getter and owner-unit/defaulting cases in P0.2. Close the reproduced shared defect with a real consumer and memory check; do not attempt complete CSSOM reflection first.

Next, define the size-container dependency contract and deliver named/unnamed size queries plus container-relative units as bounded P1.1 increments. In parallel in the schedule, P1.3 ellipsis/clamping and P1.4 multilayer background/image-set work can proceed once their specific shared-value dependencies pass. Each increment gets its own acceptance record; none requires complete shaping, every selector edge case, or a new animation model.

<a id="p0"></a>
## 3. P0 — Make existing behavior trustworthy

<a id="p0-1"></a>
### P0.1 — Evidence, inventory and qualification

- Build a reproducible inventory joining property definitions, generated names, validators, computed storage and actual consumers. Track aliases/shorthands without double-counting effective properties. Attach bounded evidence to partial/parsed-only/unrecognized families.
- Start with the P1 families; extend the inventory with each delivery rather than postponing all implementation until every value is audited. Before final closeout, reconcile the complete 409-name checkpoint plus any additions.
- Refresh the required Lambda/input and Radiant gates against a named source/binary configuration. Recheck old UI, view, math, table and memory failures before carrying them as defects; a dated predecessor failure is not a current failing test.
- Make Poppler/reference-tool availability explicit for export qualification. Separate missing tools, corpus/platform gaps and known partial baselines from engine failures without masking raw runner failures.
- Carry the predecessor's module-boundary debt as an explicit integration issue under **D7.3.3–D7.3.4**. The last archived checkpoint was 273 deferred imports against a 165 baseline, not a fresh measurement. Use the existing boundary audit and fix ownership/API placement; do not raise the allowlist to hide it. A focused unchanged-import result is not boundary closure.

**Acceptance:** Versioned inventory and current gate results, with exact source/build provenance and open failures assigned. Historical totals are labeled; passing-file floors cannot hide failed cases. CSS deliveries may proceed with proven pre-existing qualification debt, but the final plan cannot claim clean qualification while that debt remains.

<a id="p0-2"></a>
### P0.2 — Shared values, cascade and computed reads

- Finish the property-by-property residue for CSS-wide keywords, `all`, shorthand/longhand/logical competition, origin/layer rollback and keywords reached through substitution. Preserve the distinction between parse-time rejection and invalid-at-computed-value defaulting.
- Complete owner-relative unit/math computation where demonstrated gaps remain: inherited values, percentages, fonts/leading, alternate views, SVG instances, non-length domains, nonfinite values and dimensionally invalid math. Validate quirks-mode lengths only in the proper document/property context.
- Bound custom-property **dependency depth** as well as the existing byte/component and memoization limits. Audit lazy fallbacks, allocation failure and large/deep/shared graphs without changing established token-spelling behavior or copying the resolver.
- Remove remaining fixed-length computed-value adapters where they truncate supported values; share full-length serialization with layout computation. Verify reads during dirty callbacks, recascade, adoption, teardown and independent view editions under **D4.5.1v4 / D4.1.4v5**.

**Acceptance:** Each reproduced case has parser/cascade or native computation coverage and an actual consumer/CSSOM fixture. Earlier declarations, computed fallback, inherited bases and repeated reads behave correctly; no retained pool growth or lifetime regression. Full grammar/reflection breadth continues in P2.5/P3.1.

<a id="p0-3"></a>
### P0.3 — Mutation and condition dependencies

- Audit live invalidation for relative/sibling/table selectors, attributes and control state, rule/declaration changes, adoption and shadow/slot changes. Preserve the existing conservative full-recascade fallback until a narrower dependency is proven correct.
- Verify media conditions after viewport/DPR changes and linked/style/import media changes; update cached condition results and styles together. Add missing common media features, including hover/pointer, only with real environment state and invalidation.
- Give container-dependent rules an explicit condition key and relayout dependency rather than reusing viewport-only condition caching. Keep print/secondary-view environments independent of browsing geometry.

**Acceptance:** Mutating only the causal input updates matched sets, computed values, geometry and paint without a fabricated extra DOM mutation. Repeated resize/recascade does not accumulate style recipes. Broad invalidation optimization is separate from correctness.

<a id="p1"></a>
## 4. P1 — Common responsive pages and documents

<a id="p1-1"></a>
### P1.1 — Containment and container-dependent styling

- Complete the tested containment modes and intrinsic-size interactions; extend `content-visibility` beyond `hidden` with explicit visibility, measurement and invalidation semantics.
- Implement `container`/`container-name` and the required `container-type` behavior, then size `@container` conditions and named-container selection, including nested rules.
- Resolve `cqw`/`cqh`/`cqi`/`cqb`/`cqmin`/`cqmax` against the correct eligible container and writing-mode axes. Specify no-container fallback and cycle handling in a focused design record before coding.
- Keep style/scroll-state queries and broader container features out of the first size-query slice; retain them explicitly as later extensions.

**Acceptance:** Named/unnamed/nested containers, axis changes, missing containers, container resize and font/content changes have minimal WPT/native cases plus live layout/paint fixtures. Query-induced relayout converges without stale results or silent cycles.

<a id="p1-2"></a>
### P1.2 — Existing grid and logical layout residue

- Complete existing grid math/track grammar, variable substitution, repeat/line-name forms and auto-fit gutter behavior; finish computed serialization and mutation coverage. Preserve the delivered full inner-box percentage base and computed inherited lengths.
- Audit intrinsic measurement and flex/grid alignment approximations against current failures. Replace silent fixed-cap loss with an explicit checked policy or growable storage where appropriate; do not claim full grid support from a few math fixtures.
- Finish vertical logical float/clear placement and remaining physical/logical geometry interactions. Coordinate new layout models with P3.2 rather than silently approximating them.
- Paged fragmentation, page-float/table producers and independent columns stay owned by the [paged-media plan](Radiant_Impl_Paged_Media.md); CSS grammar/computation changes must retain its selected-view controls.

**Acceptance:** Current failing track/alignment/axis cases improve against browser references; resize/font/variable changes preserve the result. Cap behavior is tested explicitly. Browsing success does not imply paged support.

<a id="p1-3"></a>
### P1.3 — Text visible on everyday pages

- Paint ellipsis correctly for `text-overflow` and clamping, including fitting the final line, grapheme boundaries and RTL. Finish `text-wrap-style`, justification/inter-character placement and text-transform gaps that have current reproducers.
- Finish multi-pair ruby layout, `ruby-align`, `hanging-punctuation`, `alignment-baseline`, `baseline-shift` and other unconsumed inline traits. Keep shaping/vertical prerequisites explicit where needed.
- Complete decoration thickness/list/shorthand forms, mixed-unit underline offsets, skip-ink on non-solid strokes, multi-run uniformity and emphasis placement. Preserve delivered `tab-size`, text-indent flags and text-align-all behavior as regressions.

**Acceptance:** Geometry and raster fixtures cover clipping versus visible ellipsis, multiple inline runs, inheritance and live changes. SVG/PDF status remains partial until their actual paint checks pass; vertical/shaping-dependent cases transfer to P2.1/P2.2 explicitly.

<a id="p1-4"></a>
### P1.4 — Background and image families

- Complete multilayer `background`/`background-image`, per-layer position/size/repeat/origin/clip/attachment and blend behavior. Resolve full positions, radial size keywords/ellipse geometry, length stops, repeating gradients and color hints through shared descriptors.
- Extend CSS `image-set()` to real density/type selection in image consumers instead of first-candidate generated content. Reuse the existing image/resource selector where its contract fits; do not reimplement delivered HTML `srcset`/`sizes`/`picture` selection.
- Finish transformed/background-attachment cases and `image-rendering` for transformed/display-list images, markers and border images. Track rendered lazy `sizes="auto"` as an adjacent replaced-image dependency, not as absent responsive-image support.

**Acceptance:** At least one two-layer URL/gradient page, mixed attachment/scroll case, DPR/source-selection case and transformed sampling case reach real pixels and survive mutations. Cross-fade/element image functions continue in P3.1; vector parity is P2.2.

<a id="p1-5"></a>
### P1.5 — Scrolling, overflow and usable control parts

- Finish logical/axis-specific overflow clipping and computed serialization; verify overflow-clip-margin boxes, rounded corners and paint/hit agreement.
- Complete scroll-snap paired/oversized regions and resnapping, remaining scroll producers, scroll-margin/padding reads during dirty callbacks, keyboard/touch overscroll chaining and smooth-scroll/reduced-motion behavior.
- Complete the existing file-button intrinsic sizing/style subset and other `appearance` gaps, keeping control-part declarations off their hosts. Add practical `resize` and missing cursor behavior through the existing event model; keep legacy spatial-navigation properties in P3.4.

**Acceptance:** Nested panes, focus/selection scroll, programmatic and native input, relayout and control restyle have live tests. PNG/SVG/PDF limitations are recorded separately. No OS picker or full browser editing parity is implied by CSS control styling.

<a id="p2"></a>
## 5. P2 — Text, paint, motion and API fidelity

<a id="p2-1"></a>
### P2.1 — Shaping and font-dependent layout

- Establish a coherent shaping/bidi/grapheme contract using the existing [font/text design](../radiant/Radiant_Design_Font_Text.md); preserve glyph clusters through measurement, layout, paint, hit testing and selection. Do not patch vendored font/raster dependencies.
- Complete complex-script shaping, ligatures, mirrored/reordered bidi text, vertical/sideways glyph orientation and text-combine behavior. Expand language-sensitive line breaking and hyphenation only with corresponding language data and tests.
- Connect `font-stretch`, `font-size-adjust`, GSUB/OpenType features, `font-variant-*`, `font-feature-settings`, `font-language-override`, `font-variation-settings` and `font-optical-sizing` to selection/shaping/metrics. Audit `font-display`, asynchronous font readiness and descriptor/weight-range handling rather than treating a loaded face as a complete loader contract.
- Coordinate font caches, fallback, color-font/emoji sequences and cross-platform behavior with their existing owners. Advanced synthesis/palette/system-color work remains P3.4.

**Acceptance:** Arabic/Indic, ligature, CJK/vertical, mixed bidi and emoji-cluster fixtures retain consistent advances, glyph placement and caret boundaries. Platform-specific implementations remain labeled until exercised; text export consumes the same glyph results.

<a id="p2-2"></a>
### P2.2 — Paint/compositing and export parity

- Complete existing HTML SVG/PDF consumers for overflow/visibility, backgrounds, dashed/dotted borders, border-image slices, repeating/conic gradients and text decoration/emphasis. PDF font outlines already exist; do not reopen the obsolete built-in-font-only issue.
- Finish filter/backdrop/blend/group-opacity/isolation behavior with correct stacking and backdrop inputs. Keep fallback density and alpha semantics explicit; an embedded image marker alone is insufficient pixel evidence.
- Extend clip shapes beyond raw px/percent, geometry boxes and supported URL resources; finish existing mask-image approximations before adding the entire mask family. Share SVG resource semantics where appropriate without leaking external-image styles into the host page.
- Finish 3D flattening/grouping, depth ordering, backface/SVG descendant scenes and coordinate-dependent projective export or an explicit fallback. Complete raster-image fitting/cropping and SVG resource embedding for relocatable output.
- SVG CSS residue includes use-instance inheritance/effects, keyword/math/URL contexts and gradient/compositing color interpolation. New SVG geometry, filter primitives and SMIL timing remain owned by the [SVG implementation record](Lambda_Impl_SVG_Support.md).

**Acceptance:** Raster references and independently rasterized SVG/PDF pages cover the actual effect/clip/composite result at 1×/2×. Run required reference tools; do not credit skipped/failed comparisons. Preserve shared paint IR and retained payload ownership under **D4.5.1v4**.

<a id="p2-3"></a>
### P2.3 — Complete existing motion consumers

- Finish calculated time/count grammar, `linear()` easing, CSS keyframe selector-list/cap handling, conditional/layered named-rule lookup and complete phase/event behavior, including display-none cancellation.
- Finish sparse/neutral endpoints, base restoration and concurrent replace/add/accumulate composition. Audit transition interruption/reversal, shorthand targets and broader property writers without redoing the delivered dynamic lists and physical-side writers.
- Finish individual `translate`/`rotate`/`scale` math, sampling, dirty-callback serialization and SVG descendants. Carry planar shear/reflection, near-identity serialization and numerical conditioning into the shared transform path.
- Complete supported Web Animation keyframe forms: shorthand/logical/custom properties, effect defaults, timing dictionary, autoplay/playback/lifecycle and live underlying refresh. Basic explicit multi-property sampling is already implemented; advanced timeline/range work is P3.3.
- Connect registered-property interpolation and SVG CSS animation/transition sampling only after their computation and presentation contexts are shared with P2.5/P2.2.

**Acceptance:** Midpoint and boundary samples verify computed values, geometry and paint; interruption, cancellation, teardown and repeated effects verify events and lifetime. Existing known transition-event failures must be reclassified by current reproducers, not skipped into a green claim.

<a id="p2-4"></a>
### P2.4 — Selectors, language/direction and shadow contexts

- Finish full language-tag registry/extension validity, remaining language loader/mutation paths, manual slot assignment and slot API/event integration. Named-slot/host direction and document-language defaults are delivered regressions.
- Complete shadow stylesheet scope/relational invalidation and query-versus-stylesheet context tests. Prioritize wrong matches/restyles; optimize broad fallback recascade only after correctness.
- Audit complete QName/namespace validation, foreign-content identity after mutation/reparenting, expanded-attribute MutationObserver/SVG consumers and live-control state routes.
- Coordinate remaining XML normalization, document/doctype/CDATA/PI/template serialization, raw-text escaping and void-element variants with [DOM implementation work](Radiant_Impl_DOM3.md). These are carried DOM dependencies where they change CSS identity or observability, not a second full DOM/serializer project.
- Retain HTML table column matching as regression coverage; new non-HTML column associations stay in P3.4 until their host relationship is defined.

**Acceptance:** Matching and specificity agree for equivalent stylesheet/query contexts; language/direction/namespace and assignment mutations update consumers. Native factory/serialization controls and real shadow fixtures verify identity across adoption and teardown.

<a id="p2-5"></a>
### P2.5 — CSSOM and registered-value completion

- Complete shorthand expansion/compression, pending-value serialization and logical intermixing across inline, rule and computed declarations. Preserve delivered DOMString names, authored token spelling, wrapper identity and readonly computed objects.
- Finish descriptor applicability/validation/recovery, canonical condition text/MediaList, import/keyframe/page-margin interfaces and cross-realm/extensibility/prototype semantics. Prioritize cases required by existing consumers before exhaustive reflective API breadth.
- Finish `@property` relative-unit, color/image/URL/transform computation, nonlinear percentage/nonfinite edges, shadow/import contexts, registry interfaces and `CSS.registerProperty`. Share owner-context computation and interpolation with P0.2/P2.3.
- Complete SVG computed getters for pseudo-elements, presentation/resource contexts, shorthand pending values and effect samples. Additional SVG CSS names require grammar, inherited presentation semantics and actual consumers, not registration alone.

**Acceptance:** Native and JS/Lambda clients observe the same values/defaults and mutation effects. Forced-GC, cross-realm/detached/adopted ownership and repeated-read memory controls follow **D5.3.3 / D5.4.1 / D4.5.1v4**. Browser differences retain their standards rationale and explicit fixtures.

<a id="p2-6"></a>
### P2.6 — Generated/control/top-layer destinations

- Finish allowed `::marker` and `::selection` properties and output paths; marker color/font and selection foreground/background already work. Finish file-button properties/vector consumers after P1.5 sizing.
- Implement `::backdrop` through a real top-layer layout/paint destination and state transitions. Coordinate dialog/popover behavior through existing DOM ownership rather than styling the host as a substitute.
- Complete `::slotted()` live assignment behavior and `::part()` exported-name traversal after shadow prerequisites. Keep part styles distinct through restyle, mutation and teardown.
- Named highlights/media cues and new state pseudo-classes retain their model prerequisites in P3.4.

**Acceptance:** Each pseudo-element has an independently observable target; styling/removing it never contaminates its originating host. Layering, hit behavior where applicable, live state and relevant export paths have fixtures.

<a id="p3"></a>
## 6. P3 — Broader CSS compatibility and new models

These remain in scope as backlog, but are not prerequisites for closing the common-page P1 slices. Each family needs its own bounded delivery definition before coding.

<a id="p3-1"></a>
### P3.1 — Broader values and image/color computation

Carry Lab/LCH/Oklab/Oklch and remaining `color()` spaces, relative colors, `color-mix()`, `light-dark()`/system-color contexts, missing-component interpolation, complete math/angle/non-length domains and infinity handling. Carry `env()` fallback semantics, typed `attr()` outside generated content, `cross-fade()` and CSS `element()` images. Container units belong to P1.1, existing typed math defects to P0.2. Every promoted function must reach its real property and output consumers, not just serialize successfully.

<a id="p3-2"></a>
### P3.2 — New layout, exclusion and mask families

Carry `margin-trim`, `float-offset`, `wrap-flow`/`wrap-through`, `marker-offset`, `shape-outside`/`shape-*`, `mask`/remaining `mask-*`/`mask-type`, legacy `clip`, subgrid and anchor positioning. Masonry is a separately scoped extension, not assumed by grid support. Define exclusion geometry, resource ownership, dependency/cycle behavior and fallback/unsupported policy before registration. Reuse existing SVG mask/clip machinery where compatible; coordinate actual page producers with the paged plan.

<a id="p3-3"></a>
### P3.3 — Advanced motion and scheduling

Carry `offset-*` motion paths, scroll-driven timelines/range offsets and `transition-behavior`. Build on the existing animation mixer and scroll state after P2.3/P1.5. `will-change` is deferred until there is a defined, measurable optimization effect; accepting its name alone is not support. Define invalidation, resource budgeting and lifecycle before advertising these families.

<a id="p3-4"></a>
### P3.4 — State/platform-dependent compatibility

| Carried family | Required owner/contract before implementation |
|---|---|
| `:visited` | Real history source and privacy-safe exposed styles/queries; no unconditional matching. |
| `:fullscreen`, `:autofill`, `:playing`, `:paused` | Actual platform/media/control state, event transitions and invalidation. |
| `::highlight()`, `::cue` | Highlight registry or media cue model and a distinct paint destination. |
| `color-scheme`, `touch-action` | UA control/system-color environment or gesture dispatch and cancellation semantics. |
| `font-synthesis`, `font-palette` and broader color-font control | Font selection/synthesis and palette-capable glyph paint, coordinated with P2.1. |
| `nav-index`, `nav-up`, `nav-right`, `nav-down`, `nav-left` | An explicitly supported spatial-navigation model; keep parsed-only status until one exists. |
| Additional media features, container style/scroll-state queries and non-HTML column relations | Real environment/container/host state plus dependency invalidation; no parser-only promotion. |
| Broad `::-webkit-*` compatibility aliases | Named aliases to implemented parts only; no wildcard acceptance claim. |

The deferred model requirement is an explicit disposition of unfinished work, not a support claim. Runtime picker chrome, full browser editing policy, Canvas/WebGL expansion, complete PDF semantics, XSLT, a full TeX frontend and unrelated core-runtime defects are outside this CSS plan.

## 7. Carry-forward coverage from the retired plan

This mapping supersedes the old P0–P5 schedule. Historical progress remains in the retired file; only the outstanding residue below is active. A later fix in that history takes precedence over an earlier missing-feature note.

| Predecessor scope | Remaining disposition |
|---|---|
| P0 selector/cascade failures, keyword modes, `all`, invalid declarations | P0.2 for demonstrated residue; delivered specificity/validation/layer/nesting fixes remain regression coverage. |
| P0 custom substitution, defaults, cycles, shared shorthands | P0.2 dependency depth, broader grammar/computation and failure edges; P2.5 CSSOM/shorthand/descriptor completion. Delivered expansion bounds/memoization and raw-token serialization are not restarted. |
| P0 adjacent at-rules | P0.3 media/import/supports invalidation; P1.1 container conditions; P2.4 scope/shadow contexts; P2.5 registered values and interfaces. |
| P1 structural, relative, state, namespace and column selectors | P0.3 wrong live invalidation; P2.4 remaining language/direction/identity/shadow/state routes; P3.4 new state sources and non-HTML associations. Delivered filtered-nth/has/default/validity/namespace/table cases stay covered. |
| P2 marker, selection, file button, backdrop, slots/parts | P1.5 practical control sizing; P2.6 remaining part/paint consumers; P2.4 slot/shadow dependencies. |
| P2 highlight/cue, visited/fullscreen/autofill/media and vendor aliases | P3.4, explicitly dependent on real models/policies. |
| P3 backgrounds, border-image, gradients and image functions | P1.4 common backgrounds/selection; P2.2 border/effect/vector parity; P3.1 remaining image functions. |
| P3 text and layout | P1.2 grid/logical/intrinsic residue; P1.3 common text/decoration; P2.1 shaping/vertical/font dependencies. Pagination is handed to its existing plan. |
| P3 animation/containment | P1.1 containment/container/content visibility; P2.3 basic motion completion; P3.3 new motion/timeline families. |
| P3 effects/UI/font variants | P1.5 control/scroll behavior; P2.1 font features; P2.2 effect/clip/mask/export completion; P3.2 full mask model. |
| P4 box/overflow/scroll and UI/replaced inventory | P1.5 existing scroll/overflow/resize/cursor residue; P1.4 remaining sampling; P3.2 margin-trim; P3.4 spatial navigation. Already-consumed properties are not relabeled parsed-only. |
| P4 floats/exclusions/marker geometry | P1.2 logical side floats; P3.2 float-offset/wrap/marker-offset; page float-reference/defer stay coordinated with the implemented paged profile. |
| P4 text/ruby/font shaping and font-display | P1.3 inline/ruby traits; P2.1 shaping/features/loader; P2.2 export placement. |
| P4 background attachment/border/isolation/clip/mask/backface | P1.4 attachment/layers; P2.2 existing effects/3D/export; P3.2 legacy clip/full masks. |
| P4 animation/container shorthands | P1.1 container shorthand; P2.3 residual animation grammar/composition. Existing animation shorthand is implemented. |
| P5 individual transforms and logical borders/radii | P2.3 individual transform residue; P0.2/P2.5 computed serialization; P2.2 output/3D gaps. Delivered logical mappings and corner math are regressions. |
| P5 masks/shapes/anchors/motion/timelines/optimization | P3.2/P3.3; no name-only registration. |
| P5 font/UI additions and SVG styling | P3.4 platform/font controls; P2.2/P2.3/P2.5 SVG presentation, effects, sampling and computed values. Root-only SVG styling is an obsolete description. |
| Follow-up CSSOM/registered-property work | P0.2 critical computation/read defects; P2.5 broad interfaces/serialization/registration; P2.3 interpolation. |
| Follow-up language, attribute identity and serialization work | P2.4, coordinated with the DOM owner; resolved named-slot/expanded-attribute/factory/adoption cases remain regressions. |
| Consumer recount, lifecycle, platforms, strict boundary and aggregate qualification | P0.1 plus the delivery/final gates below; old red checkpoints do not automatically become current defects. |

## 8. Implementation seams and validation

| Area | Existing implementation/test starting points |
|---|---|
| Grammar/values/registry | `lambda/input/css/css_parser.cpp`, `css_value_parser.cpp`, `css_properties.cpp`, generated name catalogs; `test/css/`. |
| Selectors and dependency planning | `lambda/input/css/selector_matcher.cpp`, `radiant/css_cascade.cpp`; [cascade design §3.10](../radiant/Radiant_CSS_Cascade.md#310-use-one-style-invalidation-planner), `test/ui/` and native DOM integration controls. |
| Computed/style ownership | `radiant/resolve_css_style.cpp`, `css_prop_table.cpp`, `view_tree_css.cpp`; `css_prop_serialize_computed()` and selected-view tests. |
| Layout/text/fonts | `radiant/layout_inline.cpp`, grid/flex/block modules and `lib/font/`; current WPT layout baselines and font tests. |
| Motion | `radiant/css_animation.cpp`, `animation_value.cpp`, `animation_mixer.hpp`; native CSS-animation and frame/UI fixtures. |
| Paint/exports | `radiant/paint_ir.cpp`, `render_clip.cpp`, `render_svg.cpp`, `render_pdf.cpp`; `test/test_pdf_render_visual_gtest.cpp`, SVG export corpus. |
| DOM/CSSOM/state | `lambda/dom/`, shared CSS rule/declaration metadata, JS adapter and document interaction state; native lifetime tests and `test/ui/cssom_*` fixtures. |
| Cross-view/page/resource interaction | `test/test_view_tree_model_gtest.cpp` within `test_view_reuse_gtest.exe`; [paged-media implementation](Radiant_Impl_Paged_Media.md). |

Use [Developer_Guide §7](../../doc/dev/Developer_Guide.md#7-worktrees-and-agent-gotchas) before builds: match owner archives, test executables and host configurations, and keep external corpora/font/reference-tool prerequisites visible. Keep all logs/artifacts under `temp/`. Never weaken existing assertions, baselines, memory ceilings or skips to close a work item.

Per slice, run focused native grammar/computation tests and end-to-end layout/UI/paint checks relevant to its consumer. For lifecycle changes, run instrumented ASan and forced-GC/repeated read/recascade/adoption controls with no retained-growth regression. For raster/vector changes, compare actual pixels with reference tools; for performance claims use a release host and record the workload/configuration. Use existing suites rather than adding tests that merely mirror implementation.

Before marking a slice qualified, run the required applicable gates: `make test-radiant-baseline`, the unchanged `make test-css-cascade-memory` workloads, CSS Syntax, and `make lint ARGS='--rule ^no-int-cast-radiant$'` for layout changes. Run `make test-lambda-baseline` for shared runtime/input/DOM changes, `make test-svg-export` for affected SVG consumers, and `make check-module-boundary` for boundary-affecting work. Existing failures need a matching-source control and remain visible; unrun or unavailable gates remain unqualified.

### Final closeout criteria

- The current registry and support matrix reconcile syntax, computation and consumer status, with backend/live/paged differences retained.
- Every carried item has a completed acceptance record, an explicit remaining task, or a named deferred model/owner. Finishing P1 alone closes a bounded milestone, not the complete CSS backlog.
- Required regression, ownership/memory, export/reference and strict-boundary qualification is recorded against the delivered source. Known debt is resolved or the plan remains partial.
- Only measured support changes are promoted in the user report. Keep this plan as the active backlog and append brief delivery evidence here; preserve the retired predecessor as history.

## 9. Delivery evidence

### 2026-10-10 — P0 inventory and bounded shared-value delivery

Source base: `f40fe6cb4`; debug/native macOS host and owner archives rebuilt from the working tree. This is a bounded P0 delivery, not P0–P2 completion.

- **P0.1 inventory:** [Generated inventory](../../test/css/support_inventory.tsv), reproduced with [generator](../../utils/generate_css_support_inventory.py) (`--check` checks freshness), reconciles all 409 registered names/IDs with generated names, registration validators/shorthands, computed accessors/storage and static resolver/consumer/test references. References establish wiring only. The versioned [qualification manifest](../../test/css/support_qualification.json) attaches bounded scope, executable evidence and remaining limits to four partial container/containment properties; 405 rows remain unqualified pending family acceptance evidence. The generator rejects unknown/duplicate qualification identities and missing evidence paths/cases. Aliases/shorthands are not counted as independently effective consumers.
- **P0.2 computed reads:** A native regression reproduced the 511-character truncation before the fix. Accessor callbacks now write growable text; full-length CSSOM reads and bounded native projections share the same computation. A bounded projection fails with an empty output when the complete value does not fit. Temporary formatter allocations are scratch-owned under **D4.5.1v4 / D4.1.4v5**. One hundred repeated native reads leave document-pool bytes/allocation counts unchanged.
- **P0.2 substitution:** Document-configurable dependency depth defaults to 128 named custom-property dependencies. Memoization includes remaining depth so a shallow cached path cannot bypass a deeper path's budget. Typed/owned-token paths retain lazy fallbacks, registered defaults and live recovery. Inheritance walks ancestors iteratively and does not consume dependency depth; a 600-ancestor control retains declaration-owner values and dependency tracking. A separate traversal guard bounds native value/token recursion.
- **Focused checks:** 95/95 animation/computation tests; 114/114 vector/font/substitution tests; 27/27 shared StringBuf tests. All 22 CSSOM UI fixtures passed after the writer migration. The depth/long-value/budget fixture group passes 34/34 assertions, including committed fallback geometry and recovery. Isolated ASan probes instrument the affected owners and both regression translation units (remaining archives are ordinary debug); both full runners pass 95/95 and 114/114 with no sanitizer report. Required layout lint passes.
- **Aggregate qualification:** The initial Radiant baseline passed (4,182 full, 352 recorded partial); the browsing-container refresh passed **4,186 full / 352 partial**, including 423 UI fixtures, 149 DOM UI fixtures, 95 view-command tests, 12 view fixtures and the unchanged cascade-memory gate. The latest Lambda/input gate remains red at **6,574/6,577**, with all **2,112 input cases passing**. Pristine `f40fe6cb4` and current hosts reproduce byte-identical failure reports for `math_test_math_html_output`, `edit_view_only` and `latex_test_latex_phase3_corpus` using the unchanged golden harness. Math output differs only in the platform fallback family spelling (`.PingFang UI SC` versus `.PingFang UI Text SC`); edit output differs in boolean-attribute serialization and a paragraph normal-form roundtrip; LaTeX returns the harness timeout status 124 on both hosts. These remain assigned baseline defects, without changing goldens or suppressing raw failures. Logs, compile/link commands and hashes are under `temp/css_support_p0/` and `temp/css_support_p1/`.
- **Boundary debt:** Fresh base and current `make check-module-boundary` rebuilds both fail at **274 deferred imports against 165**, with the exact same 137 unapproved symbols and no additions/removals. The pre-rebuild image's 273 count was stale. No allowlist was changed. Matching-source comparison is complete; ownership/API remediation remains open under **D7.3.3–D7.3.4**.

Still open in P0: property-by-property cascade/unit/defaulting residue, condition/mutation audit, complete consumer qualification, strict boundary closure, aggregate gates and instrumented lifetime controls. No P1 or P2 acceptance item is marked complete by these checks.

### 2026-10-10 — P0.3/P1.1 browsing size-container increment

The reproduced 300px named-container case now selects its 100px child rule and resolves `50cqw` to 150px. This increment implements browsing consumers under the [focused contract](../radiant/Radiant_CSS_Cascade.md#313-size-container-delivery-contract); it does not close P1.1.

- `container`/`container-name`/`container-type` share parser validation, shorthand projection, cascade winners and owned computed names. Size/inline-size establish the tested size containment and independent formatting context. Named, unnamed, nested and comma-list size queries retain target-dependent groups; width/height/logical-size/ratio/orientation, boolean/range/math/negation and unknown-feature selection have native/WPT-derived controls.
- Query values compute in the selected container's completed font/custom-property environment. The shared rule program now finishes all ancestor stylesheets before visiting descendants; its reference fallback has the same ordering. A cross-stylesheet 10px-to-20px font reproducer and live `var()`/math/fallback/invalid-dimension controls pass. Relative container units choose eligible axes independently and fall back to the small viewport when absent.
- Browsing layout stages geometry and recascade before publication, reports nonconvergence, and samples existing motion without committing provisional transition snapshots or advancing retained clocks. Viewport changes refresh the engine environment and cascade together. Native flat-tree controls exercise shadow hosts, named assignment and suppressed fallback children; full shadow rendering remains open.
- Focused evidence: **100/100** native animation/computation tests; **83/83** assertions across four container UI fixtures, including visible pixels, nested/font/variable/viewport changes and exact transition midpoints. The 38 boolean-condition cases derive from pinned WPT `4ea2132b9a14ad4baf33677ad1723789dd183d6b`. Required layout lint and inventory freshness pass. Scoped ASan instruments the changed CSS/container owners plus both regression translation units: **100/100** and **114/114**, no address-sanitizer finding; remaining archives are ordinary debug and leak detection is disabled.
- Chromium `152.0.7977.42` agrees on **67/70** style comparisons. Three query-`var()` results differ in both default and experimental browser configurations; the retained expectations follow [CSS Conditional Rules §5.4](https://www.w3.org/TR/css-conditional-5/#size-container) and pinned WPT `var-evaluation.html`, rather than changing assertions to match that browser. Raw comparisons, screenshots, compile/link commands and provenance are under `temp/css_support_p1/`.

The refreshed browsing Radiant gate passed **4,186 full / 352 partial**; Lambda and boundary controls are classified above. Broader containment/content-visibility, complete query grammar/pseudo/shadow contexts, conditional-motion lifecycle and resource-pressure controls remain unqualified. No P0–P2 tier is marked complete.

### 2026-10-10 — P1.1 independent-view container increment

A native reproducer initially selected the base 20px rule in every edition and resolved container units against the viewport. Independent views now install their own provider, preserve the selected element during length computation and retain validated previous-pass content measurements outside replaceable style/layout scratch. The [delivery contract](../radiant/Radiant_CSS_Cascade.md#313-size-container-delivery-contract) records ownership, checkpoint rollback and stabilization under **D4.5.1v4 / D4.1.4v5**.

Four native controls pass: different wide/narrow/print geometry; query math using edition-local owner fonts/custom properties; content extents across physical fragments with padding/borders; and budget failure before commit, reset invalidation and leased-generation retention. Query callbacks rebind to the retained shell when a generation is leased. The complete native view/pagination runner passes **527/527**, including a scoped ASan build of CSS/container/lifecycle owners and both view regression translation units (remaining archives are ordinary debug; leak detection disabled). Flat PNG/SVG/PDF and fragmented preview/physical-PDF output match independently specified CSS geometry. With Poppler 26.10.0 installed, the complete export parity runner passes **202/202**, resolving the earlier missing-tool failures and skip. Raw artifacts/provenance are under `temp/css_support_p1/`; broader containment and consumer coverage remain open.

### 2026-10-10 — P1.1/P1.5 containment layout, clipping and hit increment

The float-height, overflowing paint/hit, invalid keyword-set and overlapping static-context reproducers now pass under the [containment contract](../radiant/Radiant_CSS_Cascade.md#314-containment-consumers). Computed `contain` modes remain distinct from implicit container-type containment; inheritance/defaulting and live removal rebuild the used flags. Parser and post-substitution validation reject mixed standalone modes, duplicate/conflicting modes and unsupported tokens. Computed sets use canonical keyword case/order while container names remain case-sensitive.

Layout/paint containment establishes the shared formatting context. Removing the obsolete multicolumn margin repair prevents double accounting; column fragmentation now recognizes final margin/end-gap breaks, drops margin-only continuations and retains fractional balance minima. Fixed logical sizes, padding, atomic size containment, scroll containers and avoid-break behavior have separate controls. The new column fixture compares 30 geometry assertions with Chromium `152.0.7977.42`, including live one/three-column changes.

Raster, SVG and PDF share rounded descendant clips and uniform solid-border paths. Paint containment clips without allocating a scroll pane or changing computed overflow keywords. PDF retains clip depth across streamed batches. Static containment contexts and ordinary overlapping siblings use matching paint/hit order; rounded border corners exclude the same pointer region. Reverse traversal exposed an unbounded editor margin fallback and an SVG attribute read through anonymous layout wrappers; both shared roots are fixed. SVG DOM fixtures now carry authored backing, while an explicit native cache/late-wrapper case retains source-less layout identity under **D4.5.1v4 / D4.1.4v5**.

Focused validation passes **103/103** computation tests, **117/117** vector/font tests, **527/527** independent-view tests and **72/72** containment UI assertions. Scoped ASan passes the same **747 native tests**, plus **102 UI assertions** covering containment and the original selection/radio/math regressions; the affected owners and native regression units are instrumented, remaining objects/archives are ordinary debug, and leak detection is disabled. The complete export parity runner passes **202/202**. Independent PNG/relocated-SVG/Poppler-PDF comparisons pass **72/72 pixel assertions at 1×/2×**. Layout lint and inventory freshness checks pass. Logs, reference captures, compile/link commands and source/binary hashes are under `temp/css_support_p1/`.

The first integration run exposed eight failures; focused repairs retain their raw logs. The final-source aggregate passed **4,193 full / 352 partial** (4,545 required cases), including the restored nested size-containment control. Two adjacent editor margin-typing checks reproduce the same failures on pristine `f40fe6cb4` and current hosts and remain assigned separately. Style counter/quote scoping, non-principal/table/ruby applicability, complete intrinsic/visibility interactions and independent-view containment remain open. This increment closes no P0–P2 tier.

### 2026-10-10 — P1.1 generated-content style containment

`contain: style` now bounds counter increment/set mutations while preserving
outer reads and the containing element's own counter operations. Nested
counter instances and quote depth stay within the subtree; query-container
style containment uses the same owner. Browsing and independent compositions
share counter scopes under [the focused contract](../radiant/Radiant_CSS_Cascade.md#315-style-containment-and-generated-content-scopes),
with lifetime ownership governed by **D4.5.1v4 / D4.1.4v5**.

The new browsing fixture passes **36/36** assertions, covering nested and
implicit containment, inline/block pseudos, all four quote keywords, live
removal/restoration, flex/grid/float/column flows, CSS order and stretched
items. The pinned Chromium 152.0.7977.42 host matches **28/28** applicable
isolated generated-glyph captures against explicit text, with zero differing
pixels. Fresh native tests pass **530/530**. The required Radiant aggregate passes
**4,194 full / 352 partial** (4,546 cases, no required failures). Scoped
AddressSanitizer passes **750 native tests** and **108 containment UI assertions**
with leak detection disabled; changed owners are instrumented and remaining
objects use `debug_native`. Raw results and source/binary hashes are retained
under `temp/css_support_p1/style_containment_asan`. All **203/203** export parity tests pass, including a new literal-pseudo
reference that compares flat PNG/SVG/PDF and three independently rasterized
paged PDF pages. No priority tier is closed by this increment.

Work paused at this checkpoint at the user's request. The unfinished
`content-visibility: auto` implementation was removed from the delivery;
its proposed contract remains pending in the cascade design. Resume with
an initially offscreen intrinsic-size placeholder and scroll activation
reproducer before extending relevance to focus or independent views.

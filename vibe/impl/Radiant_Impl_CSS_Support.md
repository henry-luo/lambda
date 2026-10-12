# Radiant CSS Support — Remaining Implementation Plan

**Status:** Stopped at the user’s wrap-up request; P0–P2 implementation on merged `9e0d913f6` (2026-10-12). Protected bounded text-shadow/filter-density and earlier clip/focus/provider/ownership checkpoints pass required gates, 1,149 matching sanitizer cases and 634 live assertions per runtime. The reviewed full exporter passes 249/253 groups; nineteen gradient-range comparisons, two ordered PDF edges and four blurred-shadow PDF edges remain. The SVG gate passes 126 format checks /2,466 assertions. The boundary remains 185 deferred /48 unapproved imports, with 45 removed and no additions. Broader P0–P2 criteria stay open. See §9 for evidence and limits.

**Predecessor:** [CSS selector/property implementation record](<Radiant_Impl_CSS_Selector_Property_Support (retired).md>), closed and retired with unfinished scope transferred here.

**Baseline:** [HTML, CSS and SVG Support](../../doc/HTML_CSS_SVG_Support.md), audited 2026-10-10; source checkpoint `eb75fe63b` plus the support-document update.

**Scope:** Remaining CSS selector, cascade, value, layout, paint, motion and interaction consumers, including their CSSOM and export surfaces. This is the active CSS implementation backlog.

**Authority:** An implementation plan, not a new semantics/design ruling. Ownership follows **D4.5.1v4 / D4.1.4v5**, property identity **D4.6.1v4**, native roots/context lifetime **D5.3.3 / D5.4.1**, and module boundaries **D7.1.3 / D7.5.3 / D7.3.3–D7.3.4** in [Lambda_Formal_Design.md](../../doc/Lambda_Formal_Design.md). No formal ruling defines this CSS priority order. New unresolved architectural choices require a working design record before implementation; this draft changes no ruling.

## 1. Starting point and delivery contract

The predecessor delivered substantial shared machinery: selector specificity and validation, namespace/relative/state selectors, cascade layers and nesting, custom-property substitution and bounded expansion, live CSSOM interfaces, typed math and grid lengths, logical borders/radii, animation/transition consumers, SVG presentation properties, scrolling, markers, selection and file-button paint. These are the starting implementation, not tasks to implement again.

The current registry has **410 unique property definitions**, including the appended legacy orientation entry with existing NameIds preserved. The older **342 / 244 effective / 43 partial / 55 parsed-only** classification is historical; it does not classify the current registry. HTML responsive images, PDF Unicode glyph outlines and the explicit paged compositor also now exist. Remaining work must be based on a current reproducer, not an earlier progress paragraph saying a feature was absent.

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
- Carry the predecessor's module-boundary debt as an explicit integration issue under **D7.1.3 / D7.5.3 / D7.3.3–D7.3.4**. The last archived checkpoint was 273 deferred imports against a 165 baseline, not a fresh measurement. Use the existing boundary audit and fix ownership/API placement; do not raise the allowlist to hide it. A focused unchanged-import result is not boundary closure.

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
- Complete CSS `image-set()` in broader border/list and independent/paged image consumers. Single background/generated URL images, URL markers and raster/linear-gradient borders now use shared density/type selection with native, live and export controls (§9). Retained marker mutation and border-payload ownership are qualified; broader source classes and geometry remain open. Reuse the shared selector and image/resource contracts; delivered HTML `srcset`/`sizes`/`picture` selection remains regression coverage.
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

### 2026-10-10 — P1.1 browsing content visibility increment

Computed `visible`/`hidden`/`auto` modes now remain distinct from the used
skipped state under [the visibility contract](../radiant/Radiant_CSS_Cascade.md#316-content-visibility-delivery-contract-partially-implemented).
Viewport/nested-pane proximity, focus and selection invalidate relevance;
layout settles it before publishing paint. Auto descendants retain geometry
for user-agent access. Intrinsic `auto` memory belongs to the selected edition
under **D4.5.1v4 / D4.1.4v5**, survives load-time geometry allocator replacement,
and excludes measurement trials and skipped descendants.

Browsing fixtures exercise remembered-size scroll activation, hidden paint,
hits/focus/scroll, selection, deferred CSS motion, resize/intersection observers
and rendered text. Initial observer registration waits for committed geometry;
skipped descendants do not replace last delivered sizes or intersect even an
internal root. `innerText` shares the range text pipeline, excludes skipped and
generated text, and preserves `user-select: none` text; its broader HTML text
collection fidelity remains unqualified.

At this historical checkpoint, focused checks passed **106/106** CSS computation/motion tests,
**531/531** native view tests, **73/73** DOM Range tests and **51/51** new UI
assertions. The first aggregate exposed seven regressions: ordinary sibling
counter-reset propagation outranked an ancestor's same-name instance, and the
loader discarded committed size history. The counter category now meets all
**60** recorded requirements and sizing **776/776**, without changing baselines.
The focused DOM runner also now links without unrelated layout callbacks;
its synthetic fixtures use lifecycle-registered factories instead of raw,
unregistered nodes. The refreshed aggregate passes **4,198 full / 352 partial**
(4,550 required cases), with no required failures. Scoped AddressSanitizer
passes **827 native tests** and **51 UI assertions**; changed CSS/DOM/visibility
owners and five regression translation units are instrumented, remaining
objects/archives use `debug_native`, and leak detection is disabled. All
**203/203** export parity tests pass. Chromium 152.0.7977.42 agrees on **49/51**
assertions; its activation transition differs on `transitionrun` and opacity
from the retained [CSS Containment 2 draft §4.5](https://drafts.csswg.org/css-contain-2/#restrictions)
expectation. Float-dimension lint and inventory freshness pass. Raw results and
source/binary hashes are in
`temp/css_support_p1/content_visibility_checkpoint_provenance.json`. This
checkpoint precedes the completed refinement recorded below.

At that checkpoint, independent/paged visibility and explicit hidden-style
sampling were open. Their bounded delivery is recorded below; remaining
applicability, event, relevance and lifetime coverage stays open.


### 2026-10-10 — P1.1 intrinsic sizing and independent visibility refinement

The intrinsic-size grammar now shares strict axis decomposition, including
`auto none`, and rejects extra axes, invalid units, percentages and negative
literal sizes. Physical/logical component winners use the shared cascade,
including important declarations and rollback. Math resolves in the owner's
computed font environment; explicit inheritance copies parent computed sizes.
The final flex content-height refinement preserves contained cross sizes.
These fixes have native computation and an eight-assertion live fixture.

Independent compositions preserve hidden principal boxes and their intrinsic
fallbacks while omitting descendants and generated content. Print compositions
render auto contents. A screen/print native control and a two-page output
reference exercise edition-local fonts, scoped counters and pagination under
**D4.5.1v4 / D4.1.4v5**. Explicit skipped-style reads refresh existing opacity
effects without advancing their retained clocks or firing deferred events;
font/line-height and SVG presentation contexts remain unqualified.

Fresh checks pass **112/112** CSS computation/motion tests, **532/532** native
view tests, **73/73** DOM Range tests and **61/61** assertions across five live
fixtures. Scoped AddressSanitizer passes **834 native tests** and the same
**61 UI assertions**; changed owners and five regression units are instrumented,
remaining archives use `debug_native`, and leak detection is disabled. All
**204/204** export parity tests pass, including independent SVG rasterization
and Poppler PDF references. The final-source Radiant gate passes **4,199 full /
352 partial** (4,551 required cases, no required failures). Layout lint and
inventory freshness pass; the manifest now qualifies ten bounded partial
properties among 409 registered rows.

Chromium 152.0.7977.42 matches **58/61** assertions. The dirty hidden-opacity
query and two activation-transition checks differ from the retained draft
expectations; raw values remain recorded. Source/binary hashes, commands,
results and a patch snapshot are retained in
`temp/css_support_p1/content_visibility_refinement_provenance.json`.
Independent remembered-size history, broader fragmentation/applicability,
state-change events, top-layer relevance, forced-geometry access and broader
motion contexts remain open. No P0–P2 tier is closed.


### 2026-10-10 — P1.1 queued visibility events and post-scroll activation

The typed `ContentVisibilityAutoStateChangeEvent` constructor now performs
DOMString/dictionary conversion, exposes a branded readonly `skipped` accessor
and preserves subclass prototypes. Notifications queue after the final
browsing layout state, share the traced native timer owner, and validate the
target again before dispatch. Initial observation, scrolling, inline/IDL
handlers, removal/reinsertion and duplicate suppression have **17 live
assertions**. A JS golden covers defaulting, descriptors, illegal receivers,
required `new`/type arguments, Symbol conversion, getter exceptions, conversion
order and subclassing. Both fixtures pass with forced GC and freed-memory
poisoning (**D5.3.3 / D4.3.3**).

A separate reproducer exposed a late scroll consumer: `scrollIntoView()` applied
viewport/pane positions after content relevance had settled, publishing a
placeholder and blank descendants. Scroll application now participates in the
final convergence loop; post-scroll relevance settles before remembered sizes,
motion state, geometry and paint publish. Container/descendant scroll controls
pass **8/8** geometry/scroll/pixel assertions without an extra author mutation.

Native CSS/view/Range checks pass **112/532/73** and both the new and existing
transition-event JS goldens pass. The seven visibility/intrinsic UI fixtures
pass **86 assertions** under scoped AddressSanitizer; all **834 native sanitizer
cases** also pass. The refreshed Radiant gate passes **4,201 full / 352 partial**
(**4,553 required**), export comparisons pass **204/204**, and the release
Test262 baseline passes **40,261/40,261** (2,652 cases outside that baseline are
skipped). The standalone Lambda/input gate passes **6,607/6,609**: the unchanged
`edit_view_only` boolean-attribute spelling and `latex_test_latex_phase3_corpus`
60-second timeout remain red. The earlier concurrent/sandboxed run had 230
additional subprocess-launch failures; all those cases pass in the standalone
rerun. Exact source hashes, archived fixtures and build/test commands are in
`temp/css_support_p1/content_visibility_event_scroll_checkpoint/provenance.json`.
These results precede the visibility-history and HTML-formatter refinement
below. Chromium matches **75/86**
checks: in addition to the three retained motion differences, initial/re-enable
state-change notifications, native event flags and one immediate post-scroll
deactivation differ. Exact comparisons remain recorded; complete event/task
ordering and cross-browser interoperability stay unqualified.

### 2026-10-10 — P1.1 proximity history and browsing size-memory lifetime

Connected elements retain auto proximity history across disabling auto and
display-none changes. Actual disconnection resets proximity immediately,
including a synchronous remove/reinsert. Sizing memory has a separate lifetime:
brief reinsertion preserves it; a rendering update while still disconnected
removes it. This uses validated view-owned DOM identities under
**D4.5.1v4 / D4.1.4v5**, with queued callbacks retaining the existing
**D5.3.3 / D4.3.3** owner.

Final browsing layout now clears remembered sizes independently when an axis
loses `auto`, preserves memory through display-none ancestors, and prevents
ordinary size containment or provisional non-auto layout from overwriting
remembered content dimensions. The **17-assertion** memory fixture follows
CSS Sizing 4 §5.2.1 and pinned `auto-006.html` / `auto-009.html`; the event fixture
now passes **19/19**. The complete eight-fixture set passes **105/105** under
scoped AddressSanitizer, with **834/834** native sanitizer cases. Native
CSS/view/Range checks pass **112/532/73**. Chromium matches **90/105** assertions;
three motion, nine event, one scroll-deactivation and two ordinary
size-containment memory comparisons differ. The latter two follow the current
draft rather than Chromium's fallback-memory overwrite. Full event/task-order
interoperability remains open.

The Lambda gate's `edit_view_only` failure was traced to HTML serialization:
the parser correctly keeps an empty boolean attribute as a string, but the
formatter emitted its quoted empty spelling instead of the existing bare
presence convention. The formatter now shares that convention while retaining
ordinary empty strings and nonempty boolean attribute values. Its **45/45**
native tests and the original editor fixture pass. The full Lambda/input gate
has not been rerun for this refinement; the independent LaTeX corpus timeout
and module-boundary debt remain open. Fresh focused source/build evidence is
archived in
`temp/css_support_p1/content_visibility_history_memory_checkpoint/provenance.json`.
The earlier required aggregate/export/Test262 results precede these changes.
Independent-view size memory and all other unfinished P0–P2 scope remain open.

### 2026-10-10 — P1.1 committed independent-view size memory

Independent hidden placeholders now consult their selected edition's committed
inner dimensions for automatic axes. Computed fallback values remain separate,
so inheritance and CSSOM do not acquire a used placeholder size. Final model
commit stages valid weak-identity history, updates completed principal boxes,
and publishes only after every allocation succeeds. Reference/container trials,
failed composition and model checkpoints cannot publish provisional sizes.
Active edition history transfers across retained-page arena replacement;
immutable previews keep their previous geometry/fonts (**D4.5.1v4 / D4.1.4v5**).

A native regression covers distinct browsing/print media sizes, a fresh
edition's fallback, hidden-content mutations, auto removal/reintroduction,
failed composition, and a live preview lease across reset. At **1× and 2×**, the
old preview retains its green content and the current hidden edition paints
white content space. Native view tests pass **533/533**, CSS and Range tests
pass **112/73**, and scoped sanitizer checks pass **835 native cases and 105
UI assertions**. The debug Radiant gate passes **4,202 full / 352 partial**
(**4,554 required**). After the package string-joining correction below, the
Lambda/input gate passes **6,609/6,609**. Source/binary evidence is archived in
`temp/css_support_p1/content_visibility_edition_memory_checkpoint/provenance.json`.
Export/Test262 qualification is still being refreshed; broader independent fragmentation, memory lifecycle,
top-layer relevance, event/task ordering and remaining P0–P2 scope stay open.


### 2026-10-10 — P0.1 required Lambda/input gate restored

The remaining LaTeX corpus timeout had a shared package cause: HTML serialization
and plain/rich/filtered text extraction repeatedly retained and copied every
prefix of large embedded SVG font resources. Both now reuse the existing
balanced string join. Text extraction keeps element attributes out of content
(**S2.2.3**); HTML escaping, raw CSS, void tags, attribute presence and filtered
text retain their behavior. Portable per-SVG font embedding remains unchanged.

The three affected package tests pass with their goldens, and the complete
Lambda/input gate passes **6,609/6,609**. The corpus completes within its unchanged
60-second limit (**46 seconds** in the aggregate). A separate release-only run
matches the corpus result golden in **41.2 seconds**, versus **81.1 seconds**
before the fix; the earlier 17.8-second candidate probe covered algebra alone
and is not the comparable full-corpus timing. Raw failed runs remain archived.
This closes both previously recorded Lambda gate failures, not the unfinished
module boundary or complete P0–P2 qualification.

### 2026-10-10 — P1.2 auto-fit placement and collapsed gutters

Automatic repetition now expands before placement without an item-count cap.
Only empty repeated tracks collapse, with explicit-template flags shifted past
preceding implicit tracks. Shared span/gutter helpers keep one overlapping
gutter between surviving tracks and suppress leading/trailing gutters, through
intrinsic sizing, free-space calculation, offsets and content distribution
([CSS Grid 2 §7.2.3.2 / §11.1](https://www.w3.org/TR/css-grid-2/#auto-repeat)).
Zero-size items remain intrinsic contributions; empty intrinsic maxima acquire
finite growth limits before maximization. Automatic tracks stretch only for
`normal`/`stretch`, with the grid initial alignment restored to `normal`.
Pass-local track state retains the existing **D4.5.1v4 / D4.1.4v5** ownership.

The new fixture passes **21/21** native and Chromium checks, including explicit
and negative placement, spanning and multi-track patterns, row collapse,
negative implicit tracks, percentage gutters, `normal`/distributed alignment,
raster pixels, and resize/font/variable/insertion/removal changes. Mutation
checks observe the next committed frame, following the non-forcing geometry
contract in [DOM3 Phase 1](Radiant_Impl_DOM3.md#phase-1--make-non-forcing-layout-snapshots-one-native-contract).
The required Radiant gate passes **4,203 full / 352 partial** (**4,555 required**).
Scoped AddressSanitizer passes **835 native tests and 126 UI assertions**, including
the new grid fixture. Nine independent absolute-box reference pairs pass PNG
and independently rasterized SVG/PDF comparisons at **1×/2×**. The complete
export gate passes **205/205**. Test262 passes **40,261/40,261** on the archived
release host (2,652 cases outside that baseline remain excluded). The fresh
module-boundary audit fails at **274 imports / 137 unapproved**, against the
165 baseline. The unapproved count matches the base control, but the exact set
replaces the old viewport-origin helper with `view_get_layout_position`; that
new DOM-to-engine import is being repaired through the native provider seam
under **D7.3.3–D7.3.4**, without an allowlist change. Complete repeated line names,
track CSSOM serialization, explicit cap policy and other P1.2/P0–P2 work remain
open.

### 2026-10-10 — P0.1 committed geometry stays behind the DOM engine provider

The grid checkpoint's fresh boundary audit identified one changed import:
`view_get_layout_position` replaced the former viewport-origin helper. DOM SVG
screen transforms and shadow hit tests now request committed origin coordinates
through `dom_engine_element_viewport_origin`; its strong implementation reuses
Radiant's containing-block/scroll helper, and its runtime-only weak half returns
absence and zero outputs. Reads remain non-forcing. This repairs API ownership
under **D7.3.3–D7.3.4**, without a new coordinate walker or allowlist adjustment.

The fresh audit still fails on existing debt, but improves to **273 imports /
136 unapproved** against 165. Its exact unapproved set removes only the newly
introduced layout-position import from the 274/137 grid checkpoint. Five
existing SVG geometry/CSSOM/hit/backface fixtures pass **101 assertions**;
a separately linked runtime-only provider probe also passes. Raw evidence is
under `temp/css_support_p1/geometry_seam_*`; broader boundary remediation and
P0–P2 qualification remain open.

### 2026-10-10 — P1.2 retained aliases and repeated named grid lines

Track lists and automatic-repeat patterns now retain a set of decoded names
at each line. Fixed and automatic expansion merge repeat boundaries and retain
outer aliases. Retained clones own their names across source-pool retirement;
pass projections use the grid scratch mark under **D4.5.1v4 / D4.1.4v5**.
Placement resolves positive/negative occurrences, implicit-name fallback,
anchored named spans and area-edge aliases without rewriting authored item
references ([CSS Grid 2 §7.2.2–§7.2.3 / §8.3](https://www.w3.org/TR/css-grid-2/#line-placement)).
Numeric and named edges share span/conflict handling, including unanchored
start spans, end-only placement and reversed edges. Single repeat() values
and surrounding track lists share parsing and boundary merging.

The fixture passes **39/39** native and Chromium assertions, including escaped
and case-sensitive identities, both axes, owner-relative tracks, declaration
retargeting and restoration. Nine new absolute-box pairs pass PNG and
independent SVG/PDF raster comparisons at **1×/2×**. The first full export
refresh exposed a numeric unanchored-span regression; the shared placement
fix passes that focused test, and the corrected `RenderOutputParity.*` refresh passes
**206/206** (not the separate PDF-input visual corpus).
The required Radiant gate passed **4,204 full / 352 partial** (**4,556 required**)
before that final numeric refinement and provider-owner relocation. The required
Lambda/input gate passed **6,609/6,609**. The final scoped ASan refresh passed **835 native tests /
165 UI assertions**, including the numeric refinement, provider relocation and
source-pool retirement control.
These dated results do not qualify later changes automatically. Complete track
CSSOM reads, checked cap policy, broader placement/applicability and P0–P2
remained open at that checkpoint; the four cap reproducers then failed all
native checks and are addressed in the following record.


### 2026-10-10 — P1.2 coherent grid storage and extent policy

The four original reproducers failed at the 64-track / 256-item fixed arrays.
Tracks, placement items, intrinsic contributions and sizing indices now use
scoped growable storage. Computed track arrays release their backing storage
before their grid scratch mark retires under **D4.5.1v4 / D4.1.4v5**. Index and
allocation failures propagate to the sizing/placement owner; partial arrays
cannot silently publish as a complete grid.

The dense occupancy matrix also had an independent one-million-cell clamp
that could shrink existing dimensions during expansion. Sparse origin-zero
rectangles now preserve occupancy through negative expansion without allocating
all empty cells. Later marks and clears retain their overwrite semantics.
One explicit policy applies before narrow placement indices: at most 10,000
explicit tracks and origin-zero lines **[-10000,10000]**. Crossing areas clip;
wholly outside areas move to the last track on that edge with span one, per
[CSS Grid 2 §5.4](https://www.w3.org/TR/css-grid-2/#overlarge-grids).
Authored declarations remain intact. Repeat counts are checked before numeric
conversion, and truncated outer/repeat names do not move onto a retained line.

Area metadata now retains full names and dimensions, including unnamed cells;
it no longer truncates at 16 rows/columns, 32 names or 31 bytes. Its streaming
bounds/count validation avoids a cell matrix, and generated area-edge aliases
use actual name lengths. Area-defined explicit tracks without a supplied size
use the correct `grid-auto-*` pattern. Complete area grammar rejection and
CSSOM serialization remain separate outstanding work.

The capacity fixture passes **24/24 native and Chromium assertions**, including
300 items/contributions, 1,500 fixed repeats, automatic tracks, sparse expansion,
negative implicit tracks, long area names, both flows and mutation/restoration.
Eight native UI checks exercise Radiant's declared extent policy, including
extreme indices/repeats and retained-versus-dropped boundary names; these are
UA-policy controls, not Chromium equality claims. Six focused native controls
pass, including allocation failure, sparse negative expansion, source-name
retirement and clipping. Scoped ASan passes **840 native tests / 197 UI
assertions**. The required Radiant gate passes **4,206 full / 352 partial** (**4,558 required**).
Six independent absolute-reference PNG/SVG/PDF pairs pass at **1×/2×**.
All **207/207 `RenderOutputParity` tests** pass. The broader PDF runner reports
**208/209**, with `PdfRenderVisual.CompareLambdaPagesAgainstPopplerReference`
failing on nine corpus-page visual baselines. The archived named-line binary
reproduces the identical nine measurements; this existing PDF-input/render
debt remains open under P2.2. Logs and the matching-source comparison are in
`temp/css_support_p1/grid_capacity_pdf_control_comparison.json`. The boundary rebuild remains
**273 deferred imports / 136 unapproved** against 165, with no symbol-set
changes or allowlist edits; that P0 ownership debt remains open.
P0–P2 and the broader grid grammar/CSSOM/logical/paged scope remain open.


### 2026-10-10 — P1.2 grid resolved values and placement projection

The computed-property table previously had no grid accessors: all eleven
initial fixture reads failed while Chromium passed. Displayed grids now retain
individual used pixel tracks, implicit tracks and collapsed auto-fit tracks,
with adjacent decoded name sets merged. Browsing snapshots belong to the view
property pool; independent-edition snapshots belong to their rollback-aware
model arena under **D4.5.1v4 / D4.1.4v5**. Layout scratch never escapes into a
computed getter. Non-grid reads retain computed track syntax and resolve lengths
at their declaration/font owner, including inherited values and repeat/math forms.
This follows [CSS Grid 2 §7.2.6](https://www.w3.org/TR/css-grid-2/#resolved-track-list).

Grid placement now has a shared CSS-owned grammar/projection/serializer used by
validation, layout and CSSOM. It rejects adjacent authored bracket groups,
malformed/empty placement segments, decimal integer spellings and invalid span
forms before cascade. Integer math rounds through the typed evaluator; positive
span math clamps to its grammar minimum before placement. Keyword-shaped custom
line names keep their case-sensitive spelling.

CSSOM member writes/removal expand an overlapping grid shorthand while retaining
its siblings, so removing an overriding member cannot revive that member from
an earlier shorthand. Pending projections keep their originating grammar through
owned declaration clones; substitution then projects the requested longhand.
Observable pending longhands serialize empty. The shared declaration mutation
path covers inline and rule declarations; these checks exercise inline consumers.
See [CSSOM value serialization](https://drafts.csswg.org/cssom/#serializing-css-values)
and [CSS Values pending substitution](https://drafts.csswg.org/css-values-5/).

The expanded fixture passes **50/50 native and Chromium assertions**, including
999-byte track text, both kinds of computed track reads, shorthand defaults,
math integers, declaration rejection, inherited owners, pending values and causal
width/font/variable/member changes/restoration. Four focused native controls pass:
parser rejection, pending source-pool retirement, repeated-read document-pool
stability and independent-edition rollback/commit/reset retention. All **844**
ordinary native controls and the same **844 scoped ASan controls** pass; the
ASan UI lane passes **247 assertions across 13 fixtures**. The initial required
Radiant run crashed in animation-root teardown; isolated checks and the exact
gate recheck pass (**4,207 full passes / 352 partial cases**), but that recheck
does not erase the lifetime failure. Its diagnosis and correction follow below.
The boundary audit still fails with **273 deferred imports / 136 unapproved**.
The immutable `temp/css_support_p1/grid_computed_checkpoint/` preserves 111
source files, the debug binary, patch, sanitizer provenance and crash report.
Lambda/input, Test262 and export were not refreshed at that checkpoint.
Grid-definition/area shorthand and pseudo-element CSSOM, broader grammar/unit
contexts, intrinsic/alignment/logical geometry and independent paged grid remain
open, as do P0–P2. Existing PDF-input visual and strict module-boundary debt is
recorded in the preceding checkpoint.

### 2026-10-10 — P0 native animation resources and standalone JS teardown

The macOS crash report for the grid CSSOM gate identifies
`animation_host_destroy → root_vector_destroy → heap_generation_for` during
document destruction. `free_document` destroys a standalone JS evaluator before
the document resource table: animation roots therefore retained a freed
`EvalContext`. This is a context-lifetime defect under **D5.3.3 / D5.4.1**,
independent of whether the allocator happens to leave readable freed memory.

Document resources can now name their evaluator owner. Standalone JS cleanup
releases that owner's resources before retiring code, the heap or context;
resources borrowing the still-retained Lambda evaluator keep their existing
document lifetime. Native animation hosts declare their root owner. Explicit
release, owner retirement and complete document destruction share the resource
unlink/destruction helper, with unlinking before callbacks.

Extending the sanitizer lane to the runtime allocator exposed the same ownership
defect in history's ambient roots. History state and input file values now use
owner-bound external `RootVector` slots. A resource retirement callback clears
those heap values while retaining native history URLs/index, scroll restoration
and sanitized input text. Existing resources can acquire this binding when their
first JS value is stored. External-root reads observe heap replacement without
registering roots; unregistered spans do not expose zeroed internal slots as
language values. Repeated destruction of an unregistered vector does not inspect
its retired context.

The expanded Scene3D lane also reproduced a cached `StateStore::semantic_context`
rebinding after standalone JS cleanup: the next document's setup then read the
freed evaluator. Runtime retirement now detaches that cache, template/render map
borrows, script model values/callbacks and editor roots before context release.
Native view state and the animation scheduler remain document-owned. The
integration control asserts the thread is unbound both immediately after JS
cleanup and after later document destruction.

The new integration control loads real SVG/native timelines, roots a payload,
checks release while the original heap generation remains live, then ticks and
destroys the surviving document with repeated cleanup. Both focused cases and
all **60 ordinary Scene3D cases** pass. An isolated ASan control changes only
`script_runner.cpp` back to the old cleanup order: it aborts with a confirmed
**heap-use-after-free in `heap_generation_for`** (exit 134). The raw control and
its source/command hashes are retained under
`temp/css_support_p1/script_resource_retirement_control/`. The corrected scoped
ASan lane passes **987 native cases** (60 Scene3D, 83 GC, 112 CSS, 542 view,
73 range and 117 vector) and **247 UI assertions across 13 fixtures**. The raw
runner log-decoding failure is retained separately; its native log passed, and
the view suite was rerun with its actual exit status recorded. The required Lambda/input gate passes **6,609/6,609** and Radiant passes
**4,207 full / 352 partial cases** (plus 60 Scene3D controls before the summary).
The standalone non-collecting StateStore target passes **15/15** after refreshing
its layout/root-release fixture seams. Cast lint passes. Strict boundary debt
remains **273 deferred imports / 136 unapproved**. The immutable
`temp/css_support_p1/script_resource_retirement_checkpoint/` retains source and
debug-binary hashes, sanitizer provenance and raw gate logs. Full Test262 and
export qualification were not refreshed here; P0–P2 remain open.


### 2026-10-10 — P1.2 decoded grid-area grammar and CSSOM

The minimal area fixture passed **66 Chromium checks**, while the unchanged
native build passed 23 and failed 43. Area strings were accepted by a generic
string validator, then validated late in layout; malformed rectangles could
therefore win the cascade or retain stale area geometry after substitution.

The existing growable area parser now belongs to the CSS value layer. Parser
acceptance, substituted-value validation, specified/computed serialization and
layout projection share decoded cell tokens and rectangle validation. Empty,
unequal, disconnected, nonrectangular and trash-token rows are rejected;
dot runs become single null cells, while Unicode/digit-leading names and case
remain intact. Canonical rows use single spaces/dots. The layout visitor runs
only after complete validation and copies borrowed names into its own pool
under **D4.5.1v4 / D4.1.4v5**; the scratch quote-joining adapter is removed.

The repository fixture passes **66 native/Chromium assertions**, including inline
and rule mutation and valid → invalid-variable → valid restoration with actual
track widths. The focused parser/storage controls pass; all **543 view / 112 CSS
native tests** and **845 scoped ASan native tests** pass. The sanitizer UI lane
passes **313 assertions across 14 fixtures**. New invalid/defaulted/decoded-area
controls pass six 1×/2× absolute-reference pairs across PNG/SVG/PDF; existing
named-line and capacity export controls also pass. A wrapper UTF-8 decoding
failure is retained separately; byte-safe parsing and actual native exit codes
are recorded in the completed result file.

The required full aggregates passed at the immediately preceding teardown
checkpoint, before this area change; they are not counted as fresh area results.
Grid-definition shorthand, broader math/intrinsic/alignment/logical geometry,
pseudo-element and independent paged contexts, strict module-boundary debt and
P0–P2 closure remain open.

### 2026-10-11 — P1.2 grid-definition shorthand and P0 document-owner qualification

`grid-template` and `grid` now use shared definition grammar, owned longhand
projection and recomposition. Explicit tracks, decoded area rows and merged
line-name boundaries share the existing validators. `grid` resets the auto
components; `grid-template` preserves them. Pending substitution retains the
original shorthand grammar across partial overrides/removal, inheritance and
source-pool retirement (**D4.5.1v4 / D4.1.4v5**). Layout resolves the winning
components after font computation through the existing cascade waist, including
priority, layer rollback and invalid-at-computed-value defaults. Specified and
computed track serialization share one structural formatter; owner-specific
unit computation remains in its existing consumer. Computed `grid` retains the
browser's legacy six-component slash order; computed `grid-template` retains
explicit functions and uses resolved tracks for an entirely implicit axis.
Dense row flow canonically serializes as `dense`.

Two repository fixtures pass **145 native/Chromium assertions** (53 grammar,
CSSOM and mutation checks; 92 settled computed/geometry checks). The unchanged
pre-definition control failed 45 of its original 48 grammar checks and 79 of 92
geometry checks. Parser/projection lifetime tests and a 256-read no-pool-growth
control pass; the full native view/CSS lanes pass **544/112**. Seven new export
cases pass at 1×/2× across PNG, SVG and PDF, including area geometry, line-name
merging, cyclic auto tracks, rejected syntax and the two reset behaviors.

The required Radiant aggregate first exposed a separate history lifetime defect:
initializing native history for a script-less document borrowed another
document's ambient evaluator. After iframe replacement, its roots could outlive
that evaluator. LLDB identifies initialization during document-host binding;
ASan confirms the invalid owner access at history teardown. The shared owner
lookup now accepts the canonical document runtime or a bootstrap evaluator whose
runtime explicitly names that same document (**D5.3.3 / D5.4.1**). Animation
construction also requires that explicit owner. A new foreign-document control
checks unbound initial history, matching bootstrap and reads after the foreign
evaluator retires; the existing standalone retirement control still passes.

The original iframe sequence now passes all four assertions and teardown under
ASan. The expanded sanitizer lane passes **990 native cases** (61 Scene3D,
83 GC, 112 CSS, 544 view, 73 range and 117 vector) plus **458 UI assertions across
16 fixtures**. The refreshed Radiant gate passes **4,210 full / 352 partial
cases**, including **95/95** view commands and **12/12** view UI fixtures. The
grid-definition Lambda/input gate passed **6,609/6,609** immediately before the
Radiant-only owner correction; it is not counted as a fresh post-correction
Lambda run. Cast lint and the 409-name inventory check pass. Strict module-boundary
debt remains **273 deferred / 136 unapproved imports**; its raw failure is retained.

`temp/css_support_p1/grid_definition_checkpoint/` retains the original source,
binary and failing aggregate. `temp/css_support_p1/iframe_resource_owner_checkpoint/`
retains the corrected 132-file source snapshot, debug binary, sanitizer
provenance and current raw evidence. Calculated repeat counts, pseudo-element
CSSOM, intrinsic/alignment/logical geometry, independent paged contexts and
the remaining P0–P2 families remain open. The calculated-repeat reference fixture
already reproduces **71 failures out of 95 assertions** on this frozen build;
its implementation is the next increment.

### 2026-10-11 — P1.2 calculated repeat counts and empty explicit grids

Fixed repeat counts now share typed integer grammar and computation with grid
placement. Literal decimal/exponent counts reject before cascade; math keeps its
specified unrounded value, then rounds to the nearest integer and clamps to the
permitted range during computation. Typed dimensional cancellation, nested
precedence, numeric functions and nonfinite values use the existing math engine.
Owner-font computation, inherited computed counts and custom-property invalidation
reach real placement. Specified math grouping and computed repeat-boundary names
share structural formatting; only actual grid boxes receive the legacy boundary
projection. Owned math/projection scratch retires with the caller's pool
(**D4.5.1v4 / D4.1.4v5**).

The mutation control exposed an independent placement defect: an empty explicit
grid invented a column before a negative-only item. Placement now establishes
definite primary extents before automatic spans and preserves an explicit count
of zero. Both-flow native controls and invalid-substitution/recovery geometry
verify that `none` creates only the required implicit track.

Three repository fixtures pass **287 native UI assertions**: 95 integer/math and
invalid-input controls, 132 repeated-name/computed/precedence checks, and 60 live
font/custom-property/inheritance/defaulting checks. The original 95-check fixture
failed **71 assertions** on the frozen document-owner checkpoint. Chromium 152
passes **284/287**, with three raw computed-string differences retained:
`repeat(auto-fill,10px[b])` loses `[b]` when there is no leading named line. The
normative fixture preserves the final name set per
[CSS Grid 2 §7.2.5](https://www.w3.org/TR/css-grid-2/#computed-tracks). The current
[Chromium auto-repeat serializer](https://raw.githubusercontent.com/chromium/chromium/main/third_party/blink/renderer/core/css/properties/computed_style_utils.cc)
uses entry count for its trailing-index bound, consistent with the measured
omission; this is an inferred explanation, not a vendor patch or suppressed check.

The full native view lane passes **546 cases**, including parser/computation and
256 repeated computed reads for literal, relative-unit and grouped-math counts.
Nine new independent absolute-reference export cases pass at 1×/2× across PNG,
SVG and PDF, and all seven existing grid-definition export cases still pass.
ASan passes **992 native cases** (61 Scene3D, 83 GC, 112 CSS, 546 view, 73 range,
117 vector) and **745 UI assertions across 19 fixtures**, including the final
shared fixture script. Lambda/input passes **6,609/6,609**. Radiant passes **4,213 full / 352 partial cases** (4,565 total), including
**447/447** UI fixtures, **95/95** view commands and **12/12** view UI fixtures.
Cast lint and the 409-name inventory check pass. The fresh strict boundary audit
still fails at **273 deferred / 136 unapproved imports**; its allowance is unchanged.

`temp/css_support_p1/grid_repeat_checkpoint/` retains the source/build hashes,
debug binary, sanitizer provenance, raw native/browser/UI/gate results and export
artifacts. Broader P0–P2 implementation remains open. The next both-flow placement
fixture reproduces **20 failures out of 148 assertions** here, while Chromium
passes all 148; sparse locked-axis ordering/cursor behavior is the next increment.

### 2026-10-11 — P1.2 flow-selected placement and sparse ordering

The shared grid-placement driver now selects its fixed/search axes from
`grid-auto-flow` throughout the locked-item and remaining-item steps. Sparse
locked items start after earlier locked items with overlapping secondary spans;
dense packing may fill earlier gaps. Definite-primary and fully automatic items
advance the same cursor, preserving its start-line position between items.
The previous physical-row/column wrappers are removed in favor of the existing
axis-parameterized search. This implements the reproduced
[CSS Grid 2 §8.5](https://www.w3.org/TR/css-grid-2/#auto-placement-algo) cases;
ownership/module boundaries remain **D4.5.1v4 / D4.1.4v5 / D7.3.3–D7.3.4**.

`css_grid_flow` passes **592 native/Chromium assertions** covering row/column,
sparse/dense, locked gaps, shared cursor progression, negative implicit tracks,
automatic spans and order-modified placement. Density and flow direction change
without another DOM mutation, then restore. The frozen calculated-repeat binary
fails **74/592** on the same fixture; the original initial-only control failed
**20/148**. The full native view lane passes **547 cases**, including transposed
native locking/cursor controls.

Four new three-item absolute-reference cases pass at 1×/2× in PNG, SVG and PDF.
All eight grid export test groups pass after extending the existing helper to
iterate over item geometry. Initial export failures exposed missing implicit-track
sizes in the new fixture and an existing reference-height specificity defect:
the base child rule overrode the requested reference height. The corrected
fixtures preserve the browser's authored fixed auto-track pattern, and reference
selectors now make the independent heights effective. The earlier repeat
invalid-row case now also explicitly overrides the base child height, verifying
the intended seven-pixel implicit row. Chromium independently matches all four
flow geometry pairs and that corrected row-height pair. Original failures and
corrected sources/results are retained.

ASan passes **993 native cases** (61 Scene3D, 83 GC, 112 CSS, 547 view, 73 range,
117 vector) and **1,337 UI assertions across 20 fixtures**. Cast lint and the
409-name inventory check pass. The fresh strict module-boundary gate still fails
at **273 deferred / 136 unapproved imports**, with no allowance increase.
Radiant passes **4,214 full / 352 partial cases** (4,566 total), including
**448/448** UI fixtures, **95/95** view commands and **12/12** view UI fixtures. The preceding
calculated-repeat Lambda/input gate passed **6,609/6,609**; no Lambda engine code
changed in this flow increment, and that result is not presented as a fresh run.

`temp/css_support_p1/grid_flow_checkpoint/` retains source/build hashes, the
debug binary, sanitizer provenance, native/browser/UI/export evidence and raw
gate results. Broader P0–P2 work remains active. The next focused grid-line probe
reproduces **10/48 failures** for relative-unit math admission/computation and
specified strings; its expanded shorthand/inheritance fixture fails **26/66**; its reference keeps specified expressions and changes the
computed line after the item's font changes.

### 2026-10-11 — P1.2 grid-line math, component cascade and defaults

Grid-line validation now retains calculated expressions until an owner font is
available. Specified serialization preserves unrounded expressions and grouping;
shorthand projection owns the complete syntax tree. Layout and CSSOM use their
existing font/math adapters, and native inheritance reads the parent's computed
component even when the parent has no applicable grid-item storage. Deferred
zero clamps to a permitted line, while CSS-wide defaults cannot become implicit
line names. The shared axis adapter also reduces unanchored named spans and
discards competing end spans. Ownership remains **D4.5.1v4 / D4.1.4v5** and
property identity remains **D4.6.1v4**; no formal ruling changes.

The grid component pass now covers placement and definition properties. It
uses the shared CSS-owned member mask for noncontiguous placement components,
preventing traversal order from allowing a lower-priority longhand to overwrite
a shorthand. Shorthand-only declarations are included even without a longhand
node. The initial broad export run exposed that applicability omission; its raw
failures are retained alongside the corrected run.

`css_grid_line_math` passes **342 native assertions** across specified values,
computed values and geometry, causal font/custom-value changes, invalid
substitution, inheritance, CSS-wide resets, span conflicts and restoration.
The frozen flow checkpoint fails **193/342** on the identical final fixture.
Chromium 152 agrees on **336/342** comparisons; six infinity comparisons remain
explicit implementation-limit differences. Radiant keeps its documented
`INT_MAX` computed integer and origin-zero `[-10000,10000]` used-placement limits;
Chromium uses a larger grid/coordinate range. These differences are preserved in
the fixture and raw reference comparison, not suppressed into browser passes.
Numeric range handling follows [CSS Values 4 §10.12](https://www.w3.org/TR/css-values-4/#calc-range);
placement follows [CSS Grid 2 §8.3](https://www.w3.org/TR/css-grid-2/#line-placement).

The native view lane passes **548 cases**, including parser-pool retirement of
relative-unit projections and stable document-pool usage across 256 repeated
caller-pool reads for each repeat-count variant. All **12 grid export groups**
pass. Eight new independent absolute-reference cases pass at 1×/2× in PNG, SVG
and PDF, and Chromium independently matches all eight geometries. ASan passes
**994 native cases** (61 Scene3D, 83 GC, 112 CSS, 548 view, 73 range, 117 vector)
and **1,679 UI assertions across 21 fixtures**, with matching source provenance.

The fresh Lambda/input gate passes **6,609/6,609**. Radiant passes
**4,215 full / 352 partial** cases (4,567 total), including **449/449** UI
fixtures, **95/95** view commands and **12/12** view UI fixtures. The page
snapshot gate passes; a separate raw 51-page comparison returns **30 passes /
21 failures** on both current and frozen flow binaries, with identical per-page
element/span/text scores. These existing partial-page failures remain explicit.
The render baseline retains five expected failures and one skip; the independent
grid pixel tests above pass separately. Cast lint, diff checks and the refreshed
409-name inventory pass; eight bounded partial families now qualify 25 names.
The strict module-boundary gate still fails at **273 deferred / 136 unapproved
imports** against the 165 baseline, without allowance changes.

`temp/css_support_p1/grid_line_math_checkpoint/` retains source/build hashes,
the debug binary, sanitizer provenance and raw native/browser/UI/export/gate
results. Broader owner-unit/track grammar, pseudo CSSOM, intrinsic/alignment and
logical axes, independent paged grid and the other P0–P2 families remain open.

### 2026-10-11 — P1.3 horizontal overflow paint and P0 glyph-run ownership

Horizontal LTR text now shares a paint-only overflow plan across raster, SVG and
PDF. It fits whole clusters across inline runs, applies the block's marker
font/color and baseline, clips the first cluster when required, and suppresses
clamp markers when nothing was discarded. Original source rectangles retain
their byte ranges and geometry, as required by
[CSS Overflow 3 §5.1](https://www.w3.org/TR/css-overflow-3/#text-overflow).
Export extents intersect ancestor overflow clips. Inline style resolution also
preserves its containing block's definite dimensions; clearing that shared
context had expanded authored absolute-box widths during post-flow layout.

Native ownership tests exposed a separate deferred-paint defect: a recorded
20px glyph run observed the walker's later 30px font. `PaintGlyphRun` now captures
the selected `FontBox` by value, with an explicit presence bit, and shared
lowerers read that snapshot. Its style remains borrowed from the live owner for
the paint pass under **D4.5.1v4 / D4.1.4v5**. Native controls verify 256 repeated
scopes, unchanged source rectangles, nested state restoration and command-buffer
growth. The presence bit also preserves an empty captured box through semantic
validation. Text scopes live in concrete children painters; the generic block
sequencer stays independent of font services. Shared border stroke geometry now
lives in the existing path module, fixing the DisplayList link dependency without
stubs or duplicate implementations (**D7.3.3–D7.3.4**).

Fourteen explicit-text reference pairs pass independently in Chromium 152 and at
1×/2× across PNG, SVG and PDF. They cover clip/fitting text, combining marks,
case expansion, mixed colors/fonts, one/two-line clamps, absolute descendants,
opacity and filter fallback. The live fixture passes **20 assertions** across
inherited color/font changes, disabling and stylesheet restoration; Chromium
matches all ten computed/geometry attributes. All **213 export parity groups**,
**81 DisplayList**, **119 vector** and **548 view** native cases pass. An export
run that overlapped a gate-triggered executable relink failed and skipped cases;
that raw result is retained separately. The serialized final run passes all 213
with no skips.

ASan passes **996 native cases** (61 Scene3D, 83 GC, 112 CSS, 548 view, 73 range,
119 vector) and **1,699 UI assertions across 22 fixtures**, with matching hashes
for all 110 instrumented source/header inputs. Fresh Lambda/input passes
**6,609/6,609**. Radiant passes **4,221 full / 349 partial** cases (4,570 total),
including **450/450** UI fixtures, **95/95** view commands and **12/12** view UI
fixtures. The page snapshot gate passes; the existing render baseline still has
five expected failures and one skip. Cast lint, diff and the 409-name inventory
checks pass. Nine bounded partial families now qualify 28 names. The fresh strict
module-boundary audit still fails at **273 deferred / 136 unapproved imports**
against the 165 baseline, without an allowance increase.

`temp/css_support_p1/text_overflow_checkpoint/` retains the source/build hashes,
debug binary, sanitizer provenance and raw controls, browser/UI/export/gate
results. This is bounded horizontal text support: RTL/bidi, atomic/vertical/scroll
and transformed/decorated inline contexts, broader clamp grammar and the rest of
P0–P2 remain open. The next distinct-declaration clamp grammar probe fails
**14/21 assertions** here; its Chromium control passes all 21.

### 2026-10-11 — P0 clamp integer computation and P1.3 counter scope

The numeric clamp subset now separates integer-token parsing from computed
integer math. Literal zero/negative counts, decimal/exponent spellings and
length/percentage tokens are rejected before cascade. The existing typed integer
evaluator computes calculated counts after the owner's font, rounds them and
applies the positive range. Both clamp names reuse that evaluator and integer
serializer; modern shorthand/longhands and alias component semantics remain open.
The contract is recorded in [Font/Text §12](../radiant/Radiant_Design_Font_Text.md#12-clamp-integer-computation-contract-2026-10-11)
and [CSS Overflow 4 §5.1.1](https://drafts.csswg.org/css-overflow-4/#webkit-line-clamp).

CSSOM uses the existing owner-font adapter during dirty reads. Explicit inheritance
copies the parent's computed scalar; invalid substitutions and non-inherited
CSS-wide resets compute to `none`. Serialization reads declarations rather than
transient used counters. Both ancestor propagation sites share one helper and
stop at independent formatting contexts, per [CSS Overflow 4 §5.2](https://drafts.csswg.org/css-overflow-4/#max-lines).
Repeated reads release their scratch pools under **D4.5.1v4 / D4.1.4v5**, without
changing a formal ruling.

The two native regressions fail on the prior source and pass here, including 256
caller-pool reads for each ownership/defaulting case without retained pool growth.
The final live fixture passes **320 assertions**; all **317 specified/computed/
geometry attributes** match Chromium 152, and three marker checks verify mutation
and restoration. It includes same-context and independent-context descendants.
The frozen horizontal-text implementation fails **169/320** assertions on that
same final fixture. Sixteen additional count references match Chromium pixels and
pass PNG/SVG/PDF comparisons at **1×/2×**. All **214 export parity groups** pass
with no skips, as do **550 view**, **81 DisplayList** and **119 vector** cases.

ASan passes **998 native cases** and **2,019 UI assertions across 23 fixtures**;
all **110 instrumented source/header hashes** match. Fresh Lambda/input passes
**6,609/6,609**. Radiant passes **4,222 full / 349 partial** cases (4,571 total),
including **451 UI**, **95 view commands** and **12 view UI fixtures**. The page
snapshot gate passes; the render baseline retains five expected failures and one
skip. Cast lint, diff and the 409-name inventory checks pass. The nine partial
families still qualify 28 names; no whole family or P0–P2 tier is closed.

The fresh strict boundary audit remains **273 deferred / 136 unapproved imports**
against the unchanged 165 baseline (**D7.3.3–D7.3.4**).
`temp/css_support_p1/clamp_math_checkpoint/` retains exact source/build provenance,
the debug binary, sanitizer inputs, raw prior/current native/UI controls, browser
references and aggregate/export results. A separate current legacy applicability
probe fails **23/40** browser-derived checks: display/orientation gating and the
missing `-webkit-box-orient` surface are the next demonstrated defects. Modern
clamp components, broader unit/pseudo contexts, RTL/bidi/atomic/vertical/scroll,
shaping and the rest of P0–P2 remain open.


### 2026-10-11 — P0/P1.3 legacy clamp applicability and intrinsic wrapping

`-webkit-box-orient` now uses the ordinary property/name/IDL catalogs and strict
four-keyword grammar. Existing 607 markup NameIds retain their values
(**D4.6.1v4**); Python and Lambda catalog generators agree. Computed
inline-axis/block-axis values canonicalize to horizontal/vertical. Shared display
computation gates legacy clamping on a vertical legacy box, converts its computed
display to flow-root/inline-block, and preserves computed inheritance/defaults
and dirty variable reads. Other boxes retain the computed count without using it
to clamp. Unclamped legacy boxes route orientation through the flex consumer
without overwriting computed flex-direction.

Flex/grid spans exposed a missing break-all min-content contribution. Intrinsic
measurement now retains letter/grapheme and punctuation boundaries, while its
unwrapped max-content pass preserves whole-run shaping. A new native Arabic
joining control caught and corrected a regression during this implementation;
it does not qualify Arabic paint. Flex-item flow shares block clamp counter
initialization/result storage and refreshes its overflow viewport after final
height refinement. This prevents a two-line item from retaining a one-line clip.
The contract is in [Font/Text §13](../radiant/Radiant_Design_Font_Text.md#13-legacy-box-clamp-applicability-contract-2026-10-11),
with scratch ownership under **D4.5.1v4 / D4.1.4v5**.

The two legacy native regressions fail before the change and pass here, including
256 repeated computed reads per style with no retained pool growth. All four
new native controls pass. The final fixture passes **380 assertions**, including
**371 Chromium 152 attributes** and nine marker pixels. The preceding qualified
binary fails **196/380** on that same fixture. **22 independent visible references**
match Chromium and pass PNG/SVG/PDF at **1×/2×**. All **215 export parity groups**
pass with no skips, as do **554 view**, **81 DisplayList** and **119 vector** cases.

ASan passes **1,002 native cases** and **2,399 UI assertions across 24 fixtures**;
all **117 instrumented source/header hashes** match. Lambda/input passes
**6,609/6,609**. The final Radiant gate passes **4,223 full / 349 partial** cases
(4,572 total), including **452 UI**, **95 view commands** and **12 view UI
fixtures**. Page snapshots pass; the render baseline retains five expected
failures and one skip. Catalog, cast lint, diff and **410-name inventory** checks
pass. Nine partial families qualify 29 names; no whole P0–P2 tier is closed.

The strict boundary audit remains **273 deferred / 136 unapproved imports**
against the unchanged 165 baseline (**D7.3.3–D7.3.4**).
`temp/css_support_p1/clamp_legacy_checkpoint/` retains exact source/build
provenance, the binary, sanitizer inputs, native/UI source controls, browser
references and aggregate/export results. Modern clamp components, alias cascade,
broader legacy flex mappings and writing modes, RTL/bidi/atomic/scroll/decoration,
shaping and the rest of P0–P2 remain open. A retained RTL direction-change probe
passes its LTR/reference controls but fails both RTL marker checks; that is the
next demonstrated text-paint defect.


### 2026-10-11 — P0/P1.3 RTL overflow, text-transform reads and portable SVG text

Horizontal overflow fitting now uses the block's visual inline end. RTL runs
retain the visible suffix's original byte offset and physical position, with an
independent marker using the block's font/color. Source rectangles remain
unchanged; nested paint scopes restore their selected fonts and plans. The
contract is [Font/Text §14](../radiant/Radiant_Design_Font_Text.md#14-visual-edge-overflow-projection-contract-2026-10-11)
(**FT-14**, [CSS Overflow 3 §6.1](https://www.w3.org/TR/css-overflow-3/#text-overflow)),
with scoped ownership under **D4.5.1v4 / D4.1.4v5**.

Capitalized slices retain source word boundaries across fitting, raster/shadow
paint and export. Computed text-transform reads use the winning declaration
instead of a lazily allocated block enum. One shared keyword-set validator now
serves transform, orientation and containment grammar, distinguishing rejected
literal declarations from invalid computed substitutions. Combined transform
keywords serialize correctly; their additional paint operations remain open.
Sixteen computed styles survive 256 repeated reads each without retained
pool growth.

Independent SVG rasterization exposed font substitution changing the visible
suffix bounds. Plain native HTML text now exports the selected face's outlines
through the existing PDF glyph-path helper, with escaped accessible titles
through shared semantic paint groups. Effect fallback keeps its owned text runs;
decorated/shadowed and unavailable-outline text still has viewer-font limits.
The contract is [Font/Text §15](../radiant/Radiant_Design_Font_Text.md#15-selected-font-svg-text-contract-2026-10-11)
(**FT-15**). SVG text remains non-selectable/non-searchable when outlined. Three
Mermaid integration tests initially lost their labels; all pass with the original
goldens after accessible titles were added. Six export tests now compare explicit
computed-style output references instead of depending on native SVG text syntax.

The new live fixture passes **114 assertions**, including **98 Chromium 152
attributes** and 16 real-paint checks across direction, inherited font/color,
disable and restoration changes. The preceding qualified binary fails **112/114**
on that final fixture. **14 independent visible suffix references** match Chromium
and pass PNG/SVG/PDF at **1×/2×**. All **217 export parity groups**, **555 view**,
**120 vector** and **81 DisplayList** native cases pass. The relocated SVG/PDF
matrix also passes all **126 format comparisons** across 21 fixtures at both
densities, without external resource requests. A final export run overlapped a
gate-triggered executable relink and failed ten groups with missing-executable
errors; its raw stderr is retained. The serialized rerun qualifies the fixed
debug binary without overlapping make targets.

ASan passes **1,004 native cases**, **2,513 UI assertions across 25 fixtures** and
**168 RTL export invocations**. All **117 instrumented source/header hashes**
match. Lambda/input passes **6,609/6,609**. Radiant passes **4,225 full / 349 partial** cases (4,574 total), including **453 UI**, **95 view commands** and **12 view UI fixtures**.
The page snapshot gate passes; the render baseline retains five expected failures
and one skip. Catalog, cast lint, diff and **410-name inventory** checks pass.
Ten bounded partial families qualify 30 names; no whole P0–P2 tier is closed.

The strict boundary audit remains **273 deferred / 136 unapproved imports**
against the unchanged 165 baseline (**D7.3.3–D7.3.4**).
`temp/css_support_p1/rtl_ellipsis_checkpoint/` retains exact source/build hashes,
the debug binary, sanitizer provenance and raw source/browser/native/UI/export/
aggregate controls. Mixed bidi, RTL clamps, atomic/vertical/scroll/decorated
truncation, complete transforms/shaping and the other P0–P2 families remain open.
The next spacing reproducer fails all eight raster-reference pairs on this
checkpoint, while Chromium matches all eight; it covers positive/negative
word spacing, collapsed/preserved separators, effects and both overflow edges.

### 2026-10-11 — P0 computed spacing reads and P1.3/P2.2 paint advances

The preceding qualified RTL binary fails all eight spacing raster-reference
pairs and **19/139 assertions** on the final live fixture. Layout included authored
word spacing while fitting, direct text, shadows and recorded/export glyph runs
used independent advances. Export also treated every source separator as a
preserved space. Shared character/space helpers now carry signed computed word
and letter spacing through those consumers, retaining the existing cursive
tracking suppression. Collapsed separators advance once; preserved separators
retain each advance. Fitted slices keep their original source byte offsets and
whitespace context. Recorded run spacing remains extra justification, while
authored spacing comes from the captured font style. Live SVG attributes retain
negative word/letter spacing. Ownership follows **D4.5.1v4 / D4.1.4v5**; the
working contract is [FT-16](../radiant/Radiant_Design_Font_Text.md#16-computed-spacing-in-paint-2026-10-11).

`word-spacing: normal` and initial/default reads compute to `0px`, including
dirty reads before a font snapshot exists. The native computed-read control now
covers **25 transform/spacing styles × 256 reads** with no retained pool growth.
The live fixture passes **139 assertions**: 120 Chromium-derived computed-style
and settled-geometry attributes, plus 19 paint checks. Causal inherited spacing
changes repaint immediately; geometry checks follow the explicit non-forcing
frame contract in [DOM API §3.4](../radiant/Radiant_Design_DOM_API.md#34-cssom-layout-and-geometry).
Browser-style same-task reflow remains outside that contract.

Eight independent spacing references match Chromium 152 and pass PNG/SVG/PDF
at **1×/2×**. Reference gaps use exact font-relative advances. Independent SVG
rasterization also exposed a one-level antialias difference between a single
outline path and identical glyphs split across two paths: a diagnostic regrouping
produces byte-identical output. The final export controls preserve corresponding
visible glyph groups; whole-run spacing remains covered by native and live paint
checks. No comparison tolerance or vendor renderer changed.

All **218 export groups**, **555 view**, **122 vector** and **81 DisplayList** cases
pass. The relocated SVG/PDF matrix passes **126 format comparisons** without
external requests. ASan passes **1,006 native cases**, **2,652 UI assertions across
26 fixtures** and **96 spacing export invocations**; all **117 instrumented
source/header hashes** match. Lambda/input passes **6,609/6,609**. Radiant passes
**4,228 full / 349 partial** cases (4,577 total), including **454 UI**, **95 view
commands**, **149 DOM UI** and **12 view UI fixtures**. The render baseline retains
five expected failures and one skip. Catalog, cast lint and **410-name inventory**
checks pass; source diff checks exclude the generated TSV's required empty-column
tabs. Eleven bounded partial families now qualify 31 names.

The strict boundary audit remains **273 deferred / 136 unapproved imports**
against the unchanged 165 baseline (**D7.3.3–D7.3.4**).
`temp/css_support_p1/text_spacing_checkpoint/` retains source/build hashes,
the qualified debug binary, raw before/current/browser/native/UI/export controls
and sanitizer evidence. Tabs, complete Unicode separators, cross-node/shaping/
vertical spacing, decoration placement and relative-unit/pseudo computation
remain open. The next P1.4 reproducer covers CSS `image-set()` density/type
selection; Chromium matches 20 raster-image references, while generated content
currently chooses the first candidate and backgrounds have no selection consumer.
P0–P2 remain active; no whole tier or text family is closed.


### 2026-10-11 — P1.4 selected CSS images and P2.2 background export

CSS `image-set()` now selects a supported density/type candidate for a single
background image and generated URL replacement. Optional/default resolution,
dpi/dpcm/dppx/x, typed resolution math, descriptor order/uniqueness and forbidden
nesting validate before cascade. Supported MIME filtering precedes equal-density
source order. The selected view supplies the device environment; HTML and CSS
share density preference. Raster natural size respects selected resolution,
while SVG natural dimensions follow their image format.

Generated replacements revalidate source/admission policy before warm-cache
reuse and retain their selected source. Automatic image-set content no longer
gets forced to zero dimensions. Temporary resolved URLs release after admission.
Explicit background inheritance clones owned gradients, stop arrays and layer
arrays; replacement cannot retire a parent's payload (**D4.5.1v4 / D4.1.4v5**).
The working contract is [CI-17](../radiant/Radiant_CSS_Cascade.md#317-css-image-selection-and-retained-paint-payloads-partially-implemented).

SVG/PDF background images now use the raster painter's tile plan, positioning
and clip rectangles, selected natural size and owned rounded clips. URL-less
raster surfaces embed their pixels in SVG. Explicit export device scale reaches
asset selection independently of output density ([RSC6–RSC7](../radiant/Radiant_Scale.md#rsc6--paint-semantics-are-logical-raster-lowering-is-physical)).
That exposed physical glyph-cache advances/bitmaps in logical effect paint;
shared conversion now fixes effect placement and SVG/PDF width/emphasis reads.
The previously failing filtered RTL 2× reference passes without changing a
comparison tolerance or renderer dependency.

Four native image controls cover fifteen candidate selections × 256 repeated
reads, eleven invalid declarations, independent-view density, 256 inherited
payload replacement cycles and source/policy changes with warm images. The live
fixture improves from **18/44 to 44/44**, matching **44 Chromium assertions**.
Twenty initial image references match Chromium at both densities. Ten export
cases × two device densities pass **60 PNG/SVG/PDF comparisons**. Chromium agrees
on **18/20 export references**; its two pseudo-image references retain a two-CSS-
pixel inline baseline offset. Radiant follows the single-image replacement rule
in [CSS Content 3](https://drafts.csswg.org/css-content-3/#content-property), whose
compatibility note links [CSSWG issue 2889](https://github.com/w3c/csswg-drafts/issues/2889).
This difference remains explicit rather than counting as browser parity.

The CSS Images gate exposed a stale `image-set-resolution-001` browser oracle:
it recorded a 0×0 replacement after missing `/images/green.png`. The existing
resource-aware extractor now captures the specified **200×100** replacement for
the **100×50 image at 0.5x**. Three prior/current comparisons show the prior
runtime's zero-size defect and current browser agreement. The new generated
nested reference is in the linked `lambda-test` corpus at
`layout/reference/wpt/image-set__image-set-resolution-001.json`; the fixture,
baseline entry and assertions are unchanged. Old/new reference hashes and raw
comparisons are retained in the checkpoint. All **56 CSS Images cases** pass.

All **219 export groups**, **559 native view** and **123 vector** cases pass.
The required SVG gate passes **126 format comparisons** with relocated artifacts
and no external requests. ASan passes **1,011 native cases**, **2,696 assertions
across 27 UI fixtures**, and **384 export invocations** (120 image-set, 168 RTL,
96 spacing); all **119 instrumented source/header hashes** match. Leak detection
is disabled; native repeated-read/replacement controls cover bounded pool growth.
Lambda/input passes **6,609/6,609**. Radiant passes **4,230 full / 349 partial**
cases (4,579 total), including **455 UI**, **95 view commands**, **149 DOM UI** and
**12 view UI fixtures**. Render keeps five expected failures and one skip.
Catalog, cast lint, source diff and the **410-name inventory** checks pass; the
TSV diff exception preserves required empty columns. Twelve bounded partial
families now qualify **34 names**, leaving **376 unqualified**.

The strict boundary gate still fails at **273 deferred / 136 unapproved imports**
against the unchanged 165 allowance (**D7.3.3–D7.3.4**); the unapproved symbol set
is unchanged from the spacing checkpoint. The final plan cannot close with this
debt. `temp/css_support_p1/css_image_set_checkpoint/` preserves **185 source
hashes**, the matched debug runtime, instrumented provenance, browser/reference
controls, raw failed/current gates and the generated external reference.

Complete image-function/gradient-body grammar, ordered multilayers and per-layer
geometry/blending, transformed sampling, broader generated content and CSSOM,
and independent/paged CSS-image producers remain open. The next controlled
probe has **ten Chromium-matching marker/border references** with visible image
pixels; all ten native selection pairs fail. It also exposes half-sized raster
marker images at device scale 2 and `list-style-type:none` applicability. Shared
image recognition and the remaining consumers are the next P0/P1.4/P2.2 work.
P0–P2 remain active; no whole image family or tier is closed.

### 2026-10-11 — P1.4 list/border image selection and P0 retained marker ownership

Image-kind recognition now has one shared classifier for registered image
values, list shorthands, border shorthands and the selected-image resolver.
URL list/border images retain the declaration's stylesheet base and selected
resolution. Marker natural dimensions and automatic border-image widths use
that density. A valid image precedes a `none` marker string; explicit image
`none` terminates inheritance. Retained markers refresh their borrowed image
and suppression after mutation, and raster marker dimensions convert from
logical to physical once. These are bounded additions to
[CI-17](../radiant/Radiant_CSS_Cascade.md#317-css-image-selection-and-retained-paint-payloads-partially-implemented),
not complete image grammar or layout conformance.

Border inheritance clones its owned URL/linear-gradient payload and stops;
replacement retires the old payload after constructing the new one. One
nine-slice producer now supplies raster, SVG and PDF geometry, source crops,
region clips and tiling. Deferred gradient tiles retain their paths and stops.
The solid boundary fast path yields to image sources. URL-marker export uses
the common image-resource producer. Ownership follows **D4.5.1v4 / D4.1.4v5**.
Native controls cover 256 repeated pre-parsed list/border resolutions and 256
independent border inheritance/replacement cycles with flat live pool usage.

The live fixture improves from **17/42 to 42/42**, matching Chromium. All ten
raster selection pairs now pass at 1×/2×. Seven URL/linear-gradient export
controls at both densities give **14 references, 42 format comparisons and 84
export invocations**. Exact comparison stays within each format; independent
selected-color coverage excludes matching omissions and fallback bullets or
ordinary blue borders. PDF color profiles/edge rasterization and output canvas
padding remain separate limits; no comparison tolerance was changed.

ASan exposed an uninitialized transient font owner when a retained marker
already had a font. The reuse branch skipped allocation, then read that
uninitialized owner as a pointer. Explicit empty initialization fixes the
reproducible crash without changing signal recovery or fixture assertions
(**D4.1.4v5**). Preserved IPS/LLDB traces and failed/current UI results document
the cause. The fixed source passes **1,013 native ASan cases**, **2,738 assertions
across 28 UI fixtures**, and **468 export invocations** (204 selected images,
168 RTL, 96 spacing); all **120 instrumented source/header hashes** match.
Leak detection remains disabled; the repeated-resolution/replacement controls
separately check bounded pool growth.

The fixed debug runtime passes **561 native view**, **123 vector** and all
**220 export groups**. The required SVG matrix passes **126 format checks /
2,466 pixel assertions**. Radiant passes **4,231 full / 349 partial** cases
(4,580 total), including **456 UI**, **95 view commands**, **149 DOM UI** and
**12 view UI fixtures**. Render retains five expected failures and one skip.
Lambda/input passes **6,609/6,609** on the immediately preceding runtime; that
gate precedes only the retained-marker transient-font initialization fix, and
its exact runtime hash remains recorded. Catalog, cast lint, source diff and
inventory checks pass. Thirteen bounded partial families qualify **38 of 410
names**, leaving **372 unqualified**.

The strict boundary gate still fails at **273 deferred / 136 unapproved
imports**, against the unchanged 165 allowance; its symbol set is unchanged
from the previous selected-image checkpoint (**D7.3.3–D7.3.4**). P0 cannot close
with this debt. `temp/css_support_p1/css_image_set_parts_checkpoint/` preserves
**189 source hashes**, the matched debug runtime, sanitizer provenance, raw
failed/current controls, browser captures and the prior external reference
correction.

Outside URL-marker placement differs from Chromium despite matching selected
natural dimensions. Gradient-marker export, radial/conic or SVG URL borders,
broader image grammar/defaulting/CSSOM, multilayer geometry/blending and
independent/paged producers remain open. Pure device-scale repaint may retain
its previous image selection under CSS Images UA policy and RSC10; causal
declaration changes after the transition are qualified.

The next translation probe has ten Chromium-matching references but ten native
failures. Its live fixture passes **54 Chromium assertions** and only **49/54**
before correction: URL markers omit the active paint transform, while border
image tiles move independently of their untransformed slice clips. These
reproduced P1.4/P2.2 defects are the next slice. P0–P2 remain active.

### 2026-10-11 — P1.4/P2.2 translated markers, border clips and export extents; wrap-up

URL markers now consume the active raster paint transform. Border slice clips
use the same transform as their image/gradient tiles; inherited viewport clips
already occupy projected coordinates. The shared export-bounds walk composes
paint matrices for boxes and overflow clips before integral surface allocation.
Previously a translated 40px list still allocated a 90px canvas, versus 94px for
the independent positioned reference; the translated bottom edge now agrees.
This extends [CI-17](../radiant/Radiant_CSS_Cascade.md#317-css-image-selection-and-retained-paint-payloads-partially-implemented)
and RSC6 without changing ownership (**D4.5.1v4 / D4.1.4v5**).

The live fixture improves from **49/54 to 54/54**, matching Chromium. Ten raster
selection pairs pass. Seven export controls, both untranslated and translated,
at two densities give **28 references / 84 format comparisons / 168 export
invocations**, including retained filtered gradients. Independent positioned
references and selected-color coverage exclude shared transform mistakes and
matching omissions. No tolerance, fixture golden or boundary allowance changed.

All **220 export parity groups** pass on the final debug runtime. The SVG gate
passes **126 formats / 2,466 pixel assertions**. ASan passes **1,013 native cases**, **2,750 assertions
across 28 UI fixtures** and **288 selected-image export invocations**; all
**120 instrumented source/header hashes** match. Leak detection is disabled.
An initial concurrent Scene3D run stalled at its Khronos control and was
terminated after more than two minutes. The standalone full Scene3D retry
passes all 61 cases; the view retry passes all 561 under explicit timeouts.
The initial stall and retry logs remain preserved, without changing signal
handling or test assertions. The preceding full Lambda/Radiant gates remain attached to their recorded
checkpoint runtimes; they were not rerun or relabelled for this wrap-up.
The unfiltered export executable reports **221 passed / 1 failed**: its separate
PDF-input baseline test has nine page regressions. The frozen pre-transform
runtime reproduces the same nine regression metrics; this failure remains
outstanding. The initial isolated control lacked its package link and compared
no pages; that invalid control is retained separately and excluded from evidence.
Cast lint, inventory and source diff checks pass. The matched debug runtime,
189 source hashes, sanitizer provenance and failed/current controls are saved
in `temp/css_support_p1/css_image_set_parts_transform_checkpoint/`.

Outside URL-marker browser placement, gradient marker export, broader
transformed sampling, projective rectangles crossing `w=0`, broader image
grammar/defaulting/CSSOM, radial/conic or SVG borders, multilayer
geometry/blending and independent/paged producers remain open. The most recent
strict boundary gate still fails at **273 deferred / 136 unapproved imports**
against the unchanged 165 allowance (**D7.3.3–D7.3.4**). Thirteen bounded partial
families qualify **38 of 410 names**; no whole family or tier is complete.

Execution is paused at the user's explicit wrap-up request. Resume from these
recorded limits and the P0–P2 acceptance criteria; do not treat this bounded
transform correction as closure of P0, P1 or P2.

### 2026-10-11 — P1.4/P2.2 gradient markers, decoded images and effect snapshots

Gradient markers now share an owned image producer across raster, SVG and PDF.
Linear/radial commands own copied paths and stops; conic/repeating-radial images
own their generated pixels through deferred lowering. The shared sampler fixes
hard-stop boundary selection and uses premultiplied sRGB interpolation. Zero
repeat periods use the specified equally spaced premultiplied average; unresolved
linear periods use their distance-weighted average. Resolved repetitions retain
checked heap-owned stop expansion. This extends [CI-17](../radiant/Radiant_CSS_Cascade.md#317-css-image-selection-and-retained-paint-payloads-partially-implemented) under **D4.5.1v4 / D4.1.4v5**.

Lazy file raster images decode before painting; SVG embeds their actual decoded
pixels for relocatable output. Shared effect snapshots now own a real scratch
arena, allowing saved-backdrop and filter scratch allocations. Export captures use
physical page-density bounds and logical embedded destinations. Isolated groups
composite premultiplied foregrounds; PDF soft-mask images store straight channels.
The independent foreground/backdrop replay passes retain their original order.

Twenty-five gradient sources, four paint/effect modes and two densities give
200 independent PNG references, 600 comparisons and 1,200 export invocations.
Visible-color checks reject matching omissions. Native controls include 1,536
source-release/deferred-replay cycles, checked repetition capacity, average-color
and premultiplied backdrop checks. The live fixture passes 84 assertions over 91
events. ASan passes 1,062 native cases, the live fixture and all 600 export
comparisons; all 113 instrumented source/header hashes match. Leak detection is
disabled; remaining host objects/archives use ordinary debug_native.

Four StyleEpoch selector fixtures now use authored Input/MarkBuilder elements.
Their former layout-only nodes correctly excluded selectors, so the original
mutating assertions could not test authored restyles. This test-factory correction
retains every assertion and retires the Input through its ordinary owner under
**D4.2.6**. The full view runner passes **603/603** ordinarily and under ASan.

The final debug/native macOS host (source base `8fbe9aab8`, SHA-256
`1729c640cc6fadef67396d66b7d098c80fff4dd11723806ff23ebaece98b7c10`)
passes all **225 export parity groups**, **6,647 Lambda/input cases** (including
all **2,112 input cases**), and the Radiant gate's **4,289 full / 349 partial**
cases. The SVG gate passes **126 formats / 2,466 pixel assertions** with Poppler
26.10.0 and librsvg 2.62.3 available. The unchanged cascade-memory gate, CSS
Syntax and required cast lint pass. The page-snapshot checker accepts improved
suite averages while three per-file scores decline: `bootstrap-5-kitchen-sink_`,
`examples_multi-step-form_` and `examples_pagination_`. Preserve these score
limits; aggregate floors do not establish per-file conformance.

The separate PDF-input visual test still reports **nine page regressions**.
The frozen base reproduces all **44 page metrics** exactly, including those nine;
this source does not introduce them. The initial overlapping Lambda gate reports
**6,646/6,647** with a 60-second LaTeX corpus timeout. Its unchanged serial rerun
passes all **6,647**, with that case finishing in 49.862 seconds; the isolated
base also passes. The raw timeout remains a recorded load-related instability.
The strict boundary gate still fails at **273 deferred / 136 unapproved imports**
against the unchanged 165 allowance (**D7.1.3 / D7.5.3 / D7.3.3–D7.3.4**);
the unapproved symbol set is unchanged. Cast lint, inventory and source diff
checks pass; the TSV retains its generated nineteen-column empty fields.
The matched runtime, **127 source hashes**, sanitizer provenance and failed/current
controls are frozen in `temp/css_support_p2/gradient_markers_checkpoint/`.

Raw failures remain attached to their own candidates. The initial zero-period
oracle incorrectly used the final color; the corrected oracle reproduces the
failure before the shared fix. Chromium 152 still uses the final stop in the
recorded degenerate controls and differs on default marker size/baseline; those
are explicit browser differences, not credited parity. One instrumented render
and one ordinary grid reference render were killed during concurrent sweeps;
their isolated retries pass, and the standalone instrumented sweep passes all
600 comparisons. The interrupted and raw failing sweeps remain preserved.

The partial inventory still qualifies 38 of 410 names in 13 families. Complete
gradient grammar/color interpolation, subpixel radial/conic repetition, accumulated
ancestor magnification, non-identity effect sampling, SVG/radial/conic border
sources, multilayer geometry/blending and independent/paged producers remain
open. P0–P2 remain active. The next P0 slice is strict provider/API ownership
under **D7.1.3 / D7.5.3**, starting with the 41 unapproved native Canvas/WebGL imports;
no allowlist or symbol-classification adjustment is authorized by this delivery.


### 2026-10-11 — P0 graphics provider ownership and P2 Canvas/image compositing

Under **D7.1.3 / D7.5.3 / D7.3.3–D7.3.4**, native Canvas/WebGL operations now
route through runtime-owned, complete immutable tables. Strong lower registration
and acquire/release publication replace direct upward calls; no renamed weak
forwarders or allowance changes hide the imports. `radiant-dom` publishes its
callbacks during static catalog registration, before per-context initialization.
The plain state/command records and byte-identical WebGL method/constant catalogs
move below Radiant. Arguments remain synchronous borrows; encoded Canvas bytes
remain caller-owned. The native DOM runner links without a graphics provider and
passes **74/74**, including absent-provider outputs, every incomplete table,
conflict/idempotence checks and eight concurrent publishers and 8,000 successful void callback invocations.

The rebuilt boundary images remove **41 unapproved imports**, from
**273 deferred / 136 unapproved** to **232 deferred / 95 unapproved**. No new
unapproved import appears. Lib/core/io/radiant DSOs link strictly; the runtime
boundary gate still fails. The checker, 165-symbol allowance and moved catalogs
are unchanged. Overall Class-F ownership/API debt remains P0 work; this slice
closes only the direct native graphics imports. See the bounded implementation
record in [Lambda_Design_Static_Modules.md](../Lambda_Design_Static_Modules.md).

The real consumers exposed additional P2.2 defects. Canvas backing pixels were
omitted from vector walkers, drawing surfaces advertised straight channels while
Rdt stored premultiplied pixels, and CPU blits ignored source/destination alpha
contracts. Canvas and images now share fitting, clipping and a semantic image
producer. Decode precedes the owned PaintIR snapshot under **D4.5.1v4 / D4.1.4v5**.
CPU blits filter premultiplied texels before composition; affine/projective vector
replay uses the same pixel-center sampler. The native blend control passes **216
cases**, including downsampling, both alpha modes, opacity and transparent targets.

The first full Scene3D run reproduced double-applied Canvas CSS opacity: image
paint applied it inside an existing group. Reusing the shared block scope for
images and Canvas applies opacity once to decorations and replaced pixels. The
original mixed-page assertions recover from **61/62 to 62/62**. The frozen
pre-group runtime also fails the independent baked-alpha controls, with red 64
where 128 is expected. A later full export run reports **225/226** because the
new image producer added a coincident content clip. Preserved PDF streams show
the same 24x24 source and 12x12 placement, with only the extra clip differing.
The raster containment policy avoids redundant edge clipping while retaining
clips for fitted overflow; the original image-set controls then pass unchanged.

The final Canvas fixture covers two source alpha values, six effect modes and
two densities: **24 pairs / 72 exact format comparisons / 144 exports**. Visible
red/blue coverage rejects matching omissions. Opaque group controls use independent
baked-alpha PNGs. Poppler combines a translucent soft mask and constant alpha
with a one-level blue-channel difference from a precombined mask byte; that raw
failed oracle and its pixels remain preserved. Translucent exact controls use
the same alpha representation. Initial missing-Pillow and vector-viewport
assumptions are retained separately and excluded from evidence; vector output's
existing content padding is not changed to make that probe pass.

The final macOS debug/native runtime is SHA-256
`8035b14297c77c86d888babde00454977c355763304a3c181b3119d34b0369aa`
on source base `8fbe9aab8`. It passes **226/226 export parity groups**, **127/127
vector tests**, **62 Scene/WebGL**, **20 map**, **489 selected-view** and **74
provider/range** cases. Radiant passes **4,290 full / 349 partial**, including
**506 UI automation**, **101 view commands**, CSS Syntax and the unchanged
cascade-memory gate. Render visual records **207/212**, with five newly passing
expected failures, four remaining expected failures and one skip. The SVG matrix
passes **126 format checks / 2,466 pixel assertions**; its native paint control
passes another **680 assertions**. Required cast lint and inventory checks pass.
The last full Lambda/input run passes **6,647/6,647**, including **2,112 input
cases**, on the separately preserved runtime
`4e7a2588b39d587db5dca335a4566de2058882df0fa6e974c569185658c3e283`;
only a duplicate EOF newline cleanup precedes the final native refresh.

ASan passes **1,064 native cases**, the Canvas JS golden, **84 live gradient UI
assertions**, and all **732 format comparisons / 1,464 export invocations** across
Canvas, gradient markers and selected natural image sizes. All **124 instrumented
source/header hashes** match. Remaining host objects/archives and vendors use
ordinary debug_native; leak detection is disabled. Repeated native teardown,
retained source-release and bounded-allocation controls remain enabled.

The separate PDF-input visual test remains failed, improving from **nine to six
baseline regressions** across the same 44 pages. Three previously failing pages
recover (`PrinceCatalogue` pages 3–4 and `flyer` page 1); no new baseline failure
appears. Seventeen page mismatch scores improve. The still-passing Ghent
conformance page 2 worsens from **10.368726% to 10.852424%**; one other page changes
only its mean delta. Raw metrics assign that worsening and the six failures to
P2 qualification debt. The page snapshot likewise retains three per-file score
declines despite accepted improved averages. No golden, tolerance, floor or
allowlist changes conceal these limits.

The partial CSS inventory remains **38 qualified names of 410 in 13 families**.
WebGL Canvas SVG/PDF snapshot preparation, inactive platform backends, broader
transformed sampling/paint and every remaining P0–P2 consumer remain open. The
next P0 work is the remaining provider/API ownership; the newly worsened PDF
page and WebGL vector preparation are bounded P2 reproducers. Matching source,
binaries, sanitizer provenance and failed/current controls are frozen in
`temp/css_support_p2/graphics_provider_checkpoint/`. The long-running goal remains
active; no whole priority tier is complete.

### 2026-10-11 — P2 WebGL Canvas snapshots reach vector paint

The frozen graphics-provider runtime reproduces visible red/blue WebGL pixels
in PNG while omitting them from SVG/PDF. Raster context setup prepared the GPU
snapshot; vector paint acquired the still-empty Canvas surface. The default
windowless probe first reports unavailable context creation and blank output in
all formats. That availability failure remains preserved separately. A positive
PNG control uses the full host's existing `LAMBDA_HEADLESS_GLFW_WINDOW=1` option;
no exporter silently enables native graphics.

Surface acquisition and document preparation now share the dirty-snapshot
helper. Every semantic image producer receives current GPU pixels before owning
its PaintIR copy. Unchanged reads reuse the publication. Replacement retires its
generation while retained leases preserve old pixels under **D4.5.1v4 /
D4.1.4v5**; raster preparation still runs before retained ancestors can bypass
media traversal. The native control exports first, checks stable surface identity
and generation, then verifies eight redraws and retained old colors.

The shared Canvas fixture preserves the previous 2D controls and adds **96 WebGL
pairs / 288 exact format comparisons / 576 export invocations**: two source
alphas, premultiplied/straight and preserved/discarded buffers, six effect modes
and two densities. Independent red/blue PNG references and visible channel
coverage reject matching omissions. All **226 existing export groups plus the
WebGL group pass (227/227)**. The full native Scene/WebGL suite passes **63/63**.
Required Radiant qualification remains **4,290 full / 349 partial**, including
**506 UI**, **101 view commands** and the unchanged memory gate. Render visual
retains **207/212**, five new passes, four expected failures and one skip.
The page snapshot retains its three recorded per-file declines; raw accepted
averages are 91.8% elements and 92.6% text.
The rebuilt strict lib/core/io/radiant DSOs pass their link checks; the unchanged
boundary checker still fails on **232 deferred / 95 unapproved imports**, with
an identical unapproved set and 165-symbol allowance. Cast lint and inventory
freshness pass.

The qualified macOS debug/native runtime is SHA-256
`ed40d289f910c016b7fa19e50a0e15357a1dfb973bfa100aab502dfb8b9ae0ac`
on source base `8fbe9aab8`. ASan passes **1,065 native cases**, the Canvas JS
golden, and **420 exact comparisons / 840 export invocations** across 2D Canvas,
WebGL and selected natural image sizes. All **124 instrumented source/header
hashes** match; the instrumented runtime is
`4744e238b88a45fdecb8885c6dee6d0887f258b0f18372df865ebd4c2641c07f`.
Remaining host objects/archives and vendors use debug_native; leak detection
is disabled. Lower-runtime production sources are unchanged from the preceding
full **6,647/6,647 Lambda/input** gate.

The CSS inventory remains **38 qualified names of 410 in 13 families**. This
delivery qualifies bounded native-host Canvas export, not default null-host
graphics, independent/paged media producers or inactive platform backends. The
six PDF-input baseline failures and previously worsened Ghent page 2 remain
P2 debt. A read-only pixel comparison localizes that worsening to embedded
artwork: 30,775 pixels change, while body text stays identical. It preserves
before/current/reference pixels and their hashes for the next paint reproducer.
No golden, tolerance, floor or boundary allowance changes. Evidence and matching
source/binaries are frozen in `temp/css_support_p2/webgl_exports_checkpoint/`.
P0–P2 and the long-running goal remain active; remaining native CSS/provider
ownership is the next P0 slice.

### 2026-10-11 — P0 native CSS provider ownership and consumer qualification

Under **D7.1.3 / D7.5.3**, six audited CSS calls now use the lower runtime's
complete immutable table. The shared generic helpers serve Canvas, WebGL and
CSS; the CSSOM bridge drops its Radiant view-header include. Serialized strings
use the caller's pool, while computed custom values may borrow document storage
and are consumed synchronously under **D4.5.1v4 / D4.1.4v5**. The synchronization
hook's behavior is unchanged. The bounded contract is recorded in
[Lambda_Design_Static_Modules.md §13](../Lambda_Design_Static_Modules.md#13-sm9--normalize-the-runtimeradiant-reverse-edges).

Rebuilt strict DSOs remove all **six** audited imports: **226 deferred / 89
unapproved**, with zero additions and unchanged checker/165-symbol allowance.
The overall boundary gate remains failed. Native provider/range controls pass
**75/75**, including six incomplete CSS tables, absence, conflict/idempotence
and eight concurrent publishers with **16,000 successful void callbacks**.
Vector controls pass **127/127**; 16 computed/CSSOM export groups and two view
interface/mutation controls pass. The full Lambda/input gate passes
**6,647/6,647**, including **2,112 input cases**, on runtime SHA-256
`5a3650aaa8085327dadeb91f48126a40cff9fdcff7a900fb0deab0af6a617e36`.
The forced native refresh produces the same binary. Full native Scene/WebGL
passes **63/63**. The refreshed Radiant gate passes **4,290 full / 349 partial**
(4,639 required cases); the normal export gate passes **227/227**. Existing page
snapshot score declines and PDF-input regressions remain recorded in the preceding
qualified increments; this provider change does not claim their repair.

The isolated ASan build covers **126 source inputs**, with zero source mismatches,
and passes **1,066 native cases**: animation 115, provider/range 75, GC 83, vector
127, Scene3D 63 and view reuse/model 603. The Canvas JS golden passes. All **19
instrumented export groups** pass, including the 16 computed/CSSOM groups and the
2D/WebGL/image-selection controls with **420 comparisons / 840 exports**. ASan
runtime SHA-256 is `4367d1b40fdee5591aa3a9b66a11fdb494925079017f3253b77440e7f2bd10cc`;
the ordinary debug runtime stays unchanged throughout qualification. Cast lint,
inventory generation/freshness and diff checks pass. Immutable sources, binaries,
raw controls and provenance are in `temp/css_support_p0/style_provider_checkpoint/`;
working evidence is in `temp/css_support_p0/style_provider/`.

The next P1.4/P2.2 reproducer is preserved separately in
`temp/css_support_p1/background_layers/before/`, on the frozen previous WebGL
runtime. Of 18 two-color comparisons, **14 fail**: both shorthand and longhand
paint the radial blue layer above an authored first linear red layer (1,536 wrong
raster pixels), and all 12 SVG/PDF comparisons omit the mixed layers. Reversing
the linear/radial order provides passing raster controls. Resolution's separate
gradient-kind arrays discard authored cross-kind order; vector walkers consume
only the scalar gradient. The next implementation needs an ordered shared layer
descriptor and the existing owned gradient-image producer, under **D4.5.1v4 /
D4.1.4v5** and **CI-17**, shared across the raster and vector consumers.
The remaining 89 imports, broader CSS/API ownership and all other unfinished
P0–P2 acceptance criteria stay active.

### 2026-10-11 — P1.4/P2.2 next ordered-background delivery contract

The frozen CSS-provider checkpoint contains the earlier matching matrix/design
revision. The working documents now record the separately reproduced mixed-layer
defects and [CI-18](../radiant/Radiant_CSS_Cascade.md#318-ordered-background-layers-delivery-contract-implementation-pending),
with ownership under **D4.5.1v4 / D4.1.4v5**. This next contract requires authored
ordering, empty layer indexes, repeated per-layer component lists, independent
inherited payloads and the existing common gradient/image paint producer.
Source implementation and its acceptance controls remain the next active work;
no additional property is qualified by this contract or its failed reproducer.

### 2026-10-12 — P1.4/P2.2 ordered-background implementation under qualification

The new authored-order `BackgroundLayer` owner replaces gradient-kind arrays.
Empty/unavailable images retain their indexes. Computed component lists keep
their original periods and surplus entries; explicit inheritance clones gradient
stops and component storage before the originating owner retires, under
**D4.5.1v4 / D4.1.4v5** and **CI-18**. Raster/SVG/PDF share the ordered tile walk
and the existing owned gradient producer. Uniform gradients use a solid path;
varying-gradient controls compare immediate pixels with deferred pixels after
both the source and recording retire. Blended backgrounds isolate their own
layers/color from the page and use the existing shared replay for export.

The immutable candidate `temp/css_support_p1/background_layers/ordered_03.exe`
and its 14-source anchor preserve this provisional implementation. The original
six-case probe improves from **4/18 to 18/18 exact matches** across PNG/SVG/PDF.
Six focused image/inheritance controls, full vector **127/127**, and full view
reuse/model **604/604** pass. The new export group exercises **24 pairs / 72
comparisons** at 1×/2×: all raster/SVG comparisons pass, but **two 2× PDF blend
comparisons fail at the image boundary**. Existing PDF gradient/filter/blend/
backdrop structural controls pass unchanged. Padding/mask experiments did not
resolve the pixel defect; raw failures are preserved and the padding change was
reverted. Cast lint and inventory generation/freshness pass; source diff checks
pass excluding the generated TSV's intentional trailing empty columns.

The candidate's required Radiant aggregate passed **4,290 full / 349 partial**
(4,639 required cases). The subsequent owner-publication ordering cleanup rebuilt
and passed all **604/604** view controls again; its 14-source/runtime anchor is
`ordered_04_anchor.json`. Its isolated ASan build covers **127 source inputs**
with matching hashes and passes **1,067 native cases** (115 animation, 75
provider/range, 83 GC, 127 vector, 63 Scene3D and 604 view controls). The ordinary
runtime stays unchanged; the instrumented source snapshot is `asan_sources_04/`.
Full export, URL/gradient, attachment/transform and live controls are pending;
full position/math and motion/computed serialization remain open. This is not a
qualified background-family delivery and closes no P0–P2 tier. Working logs,
candidate source/binary anchors and failures are in
`temp/css_support_p1/background_layers/`; the previous qualified CSS-provider
checkpoint remains unchanged.

The subsequent axis-period reproducer fails when a child changes the image
count while inheriting different X/Y list lengths. Chromium confirms that a
two-entry X list and three-entry Y list cycle independently, including
`background-position:inherit`. Resolution previously applied axis overrides to
active layers after storing the combined position period, losing the inherited
values. The replacement stores and cascades each axis independently before
projecting the image list (**CI-18**, ownership under **D4.5.1v4 / D4.1.4v5**).
The retained child survives parent replacement and adopts the new values on its
next restyle. Shorthand competition, importance, substitution and computed
defaulting controls pass. Candidate `ordered_05.exe` and its 14-source anchor
preserve this increment. Its independent colored-tile control changes from
failure to **12/12 exact PNG/SVG/PDF comparisons** (four 1×/2× pairs / 24 exports),
and native suites pass **606/606** views and **127/127** vector cases. Its
matching 127-input isolated ASan build passes **1,069 native cases**, the Canvas
JS golden and all **12 inherited-axis pixel comparisons**. Instrumented ordered
background controls retain exactly the two known PDF boundary failures without
an ASan report. Sources and provenance are frozen in `asan_sources_05/`; the
ordinary runtime remains unchanged. Full qualification remains provisional.

The required Radiant aggregate also passes on `ordered_05.exe`: **4,290 full /
349 partial** (4,639 required cases). The three recorded snapshot declines
remain; the gate's passing average does not claim their repair.

A separate direct PDF-writer probe reproduces the opaque-image boundary
difference for both inline and XObject images at 2×. Rectangle clips reduce but
do not remove it. These controls retain the two blend-export failures; neither
the writer nor the test tolerance/runner has been changed to hide them.

The next full-position probe fails **21/24** comparisons on `ordered_05.exe`;
the numeric-position control passes. Computation previously read only two
components and discarded mixed math. A shared physical-position descriptor now
handles reordered keywords, three-/four-value edge offsets and axis longhands,
and validates position declarations before they replace earlier winners.
Linear length/percentage math uses the existing scalar terms; nonlinear math
owns computed expression trees under **D4.5.1v4 / D4.1.4v5**. Inheritance clones
the already computed lengths, and paint evaluates percentages against image
free space using the existing computed-math resolver (**CI-18**).

Candidate `ordered_06.exe` and its 18-source anchor preserve this provisional
increment. Its extended 12-case probe passes **36/36** PNG/SVG/PDF comparisons.
Full native view controls pass **609/609**, including parent font-unit
computation, owner retirement, changing percentage bases and 256 restyles with
flat live pool bytes. Durable position exports pass **72/72** comparisons at
1×/2× (24 pairs / 144 exports), and inherited axes retain their **12/12**.
The subsequent document-scoped quirky-length refinement is building; the new
source revision still needs fresh ASan and required aggregate qualification.
This does not close the background family, motion/serialization or P0–P2.

The subsequent `ordered_07.exe` candidate has a 20-source anchor and passes
**610/610** native views, **127/127** vector cases and **84/84** durable
position/axis export comparisons. Its background-position quirky-length control
accepts bare numbers only in the listed document/property context, retaining
standards-mode and function rejection. The shared DOM mode helper is promoted
rather than copied. This follows **CI-18** and
[CSS Values 4 appendix C](https://www.w3.org/TR/css-values-4/#deprecated-quirky-length).
Its required Lambda/input gate passes **6,647/6,647**, including **2,112 input
cases**; required Radiant passes **4,290 full / 349 partial** (4,639 required
cases). The ordinary runtime remains unchanged. The recorded snapshot declines
remain open. Its fresh isolated ASan build covers **131 matching source inputs**
and passes **1,073 native cases** (115 animation, 75 provider/range, 83 GC,
127 vector, 63 Scene3D and 610 view cases), the Canvas JS golden and all **84
position/axis export comparisons**. Its ordered-background group retains the
two known PDF boundary failures without an ASan report. The ordinary runtime
remains unchanged; source inputs and provenance are frozen in `asan_sources_07/`.

The PDF writer experiment now also compares Poppler's Cairo renderer with
`pdftoppm`: all four image-vs-rectangle controls match exactly with Cairo at
144 dpi, as do both actual background blend PDFs against their references.
This narrows the remaining discrepancy to rasterizer edge coverage, without
asserting an upstream defect or replacing the existing raw failing controls.
Artifacts are under `background_layers/pdf_sampling/`; the two `pdftoppm`
failures remain open in qualification.

The real headless UI controls each execute 17 events and 11 exact pixel
assertions: move positions, clear/restore image layers and change device scale
from 1× to 2×. Both edge-offset and nonlinear `min()`/`max()` forms pass in normal
and ASan runs (**44 assertions** total). The nonlinear case is now the durable
`test/ui/css_background_layers.{html,json}` fixture. A fresh required Radiant
recount passes **4,291 full / 349 partial** (4,640 required cases) on the unchanged
`ordered_07.exe` runtime. The boundary rebuild passes all four strict DSOs and
retains **226 deferred / 89 unapproved imports**, with no symbol added or removed
relative to the qualified P0 style-provider checkpoint. Its raw gate still fails
against the unchanged 165 allowance. No family/property qualification status or
allowance has been raised.


The next size-math probe fails **15/18** PNG/SVG/PDF comparisons on
`ordered_07.exe`; its numeric-size control passes. The component reader dropped
math values, and the shorthand size reader misclassified math after `/` as a
position. Position and size now share computed-math storage/ownership under
**D4.5.1v4 / D4.1.4v5**, while paint supplies their distinct percentage bases
(**CI-18**). Size parsing validates the complete per-layer one-/two-component
shape, `auto`/`cover`/`contain` and nonnegative literals. Math clamps its final
used result to the nonnegative range. Explicit zero size suppresses the image;
a gradient's `auto` dimension uses the positioning area instead of a fabricated
intrinsic aspect ratio. This follows
[CSS Backgrounds 3 §2.9](https://www.w3.org/TR/css-backgrounds-3/#the-background-size)
and [CSS Values 4 §10.12](https://www.w3.org/TR/css-values-4/#calc-range).

Candidate `ordered_09.exe` freezes 22 source/fixture inputs. Its independent
size probe passes **18/18**, native views pass **613/613**, and durable size
exports pass **72/72** at 1×/2× (24 pairs / 144 exports). Position/axis exports
retain **84/84**. The live fixture now passes **22 exact pixel assertions over
35 events**, adding nonlinear/linear size changes at both scales and image
replacement while the larger tile remains selected. The first export-reference
revision omitted padding from the reference area and drew zero-area PDF patches;
those raw failures remain in `sizes08_exports.log`. The corrected independent
references preserve positioning geometry and represent a zero-sized image by an
absent patch. Its required Lambda/input aggregate passes **6,647 cases**
(including 2,112 input cases), and required Radiant passes **4,291 full /
349 partial** (4,640 required cases), retaining the same three snapshot
declines. The fresh isolated ASan build freezes **131 matching source inputs** and passes
**1,076 native cases** (115 animation, 75 provider/range, 83 GC, 127 vector,
63 Scene3D, 613 view), the Canvas JS golden, all **156 size/position/axis export
comparisons** and all **22 live pixel assertions**. Its ordered group retains
the two PDF edge failures without an ASan report. The ordinary runtime remains
unchanged; proof is frozen in `asan_sources_09/` and `sizes09_asan_summary.json`.

The full normal export gate is **225/231**, with six raw failed groups. In addition
to the two ordered-background PDF comparisons, it reports three alpha-opacity
assertions, three SVG rectangle-output groups (six assertions) and gradient
markers (36 raster / 24 PDF comparisons). All five additional groups fail with
exactly the same assertions on `ordered_07.exe` and pass on the qualified P0
style-provider runtime using the current runner. They therefore predate the size
increment and remain open regressions in the ordered-background delivery.
The generic-path substitution for solid backgrounds and uniform gradient images
is the next paint root being addressed. The fresh boundary rebuild passes all
four strict DSOs and retains the identical **226 deferred / 89 unapproved**
symbols against the unchanged 165 allowance; its raw gate still fails. This
remains a provisional increment, without family/property promotion.


The subsequent paint revision restores canonical solid-background rectangles and
retains the standalone gradient-image producer's qualified sampling/coverage.
Scale/translation lowering preserves rectangle primitives. Integral intersections
of rectangular clips use the existing exact premultiplied source-over compositor,
after flushing pending vector draws. Native controls reproduce alpha 254 before
the fix and verify alpha 255 with nested clips and 1×/2× density. This remains
within **CI-18** and the existing **D4.5.1v4 / D4.1.4v5** ownership contract.

Candidate `ordered_12.exe` freezes 23 source/fixture inputs and passes **128 vector
/ 613 view cases**, both required aggregates (**6,647 Lambda/input; 4,292 full /
349 partial Radiant, 4,641 required cases**) and all **22 live pixels**. The three
snapshot declines remain recorded. Its full export gate improves to **230/231**:
the five earlier alpha/SVG/marker regression groups pass; the ordered-background
group retains exactly two PDF edge assertions. Its fresh **131-input ASan** build
passes **1,077 native cases**, the Canvas JS golden, **156 size/position/axis and
600 marker comparisons**, the five repaired groups and all **22 live pixels**,
with the same two PDF failures and no ASan report. Sources and provenance are
frozen in `asan_sources_12/` and `paint12_asan_summary.json`. The strict boundary
rebuild passes four DSOs and retains the identical **226 deferred / 89 unapproved
imports**; the raw gate still fails against the unchanged 165 allowance.

The next shorthand probe reproduces **14/14 incorrect literal replacements** on
this frozen runtime. Chromium rejects every case. The prepared 32-event live
fixture has **15 failures among 29 assertions** on the native predecessor and
passes all 29 in Chromium, including computed invalid-variable defaulting and
recovery. Shared lower validation and component decoding now enforce complete layer
cardinality and contiguous position/size/repeat terms, with final-layer color
validation (**CI-18**). Computation and computed-color extraction consume the
same borrowed descriptor; scratch lists retain the existing **D4.5.1v4 /
D4.1.4v5** ownership contract. Candidate `ordered_13.exe` freezes 26 inputs and
passes **615 native view cases**, all **14 literal preservation controls**, all
**29 live assertions** and **30 PNG/SVG/PDF comparisons** at 1×/2×. The independent
paint controls cover literal rejection, defined-but-invalid variables without
using their fallback, and a valid missing-variable fallback with two layers.
Its matching-source qualification passes both required aggregates (**6,647
Lambda/input; 4,293 full / 349 partial Radiant, 4,642 required cases**) and
**231/232 full export groups**, retaining the same two ordered PDF edge
assertions. The fresh **131-input ASan** build passes **1,079 native cases**, the
Canvas JS golden, both live fixtures (**22 + 29 assertions**) and nine focused
export groups (**186 background and 600 marker comparisons**), with no ASan
report; the two PDF controls still fail. Four strict DSOs rebuild successfully;
the boundary remains **226 deferred / 89 unapproved imports**, with the raw gate
failing against the unchanged 165 allowance. Sources and raw results are frozen
in `ordered_13_sources/`, `asan_sources_13/`, `grammar13_asan_summary.json` and
`grammar13q_qualification_results.json`.

The next longhand probe reproduces **17 invalid literal replacements** and a
valid three-value `background-attachment` list being rejected. Chromium preserves
all 18 invalid controls and accepts all six valid comma lists. The pending
shared layer validator covers images, repeat, attachment, origin, clip and blend
under **CI-18**. No family or P0–P2 tier is closed.


The shared longhand validator now validates complete comma-separated layers for
images, position, size, repeat, attachment, origin, clip and blend. Repeat
computation and shorthand parsing consume the same lower axis descriptor; image
recognition shares the registered-value and border-image predicate (**CI-18**).
All **18 invalid literal controls** preserve their prior winner and all **six
valid lists** are accepted. The longhand live fixture improves from **11/30 to
30/30 assertions**, including defined-invalid repeat defaulting and live
recovery; Chromium passes the same 30. Native component inheritance checks
retain distinct periods and storage and stable pool bytes over 256 recascades
(**D4.5.1v4 / D4.1.4v5**).

A separate mixed URL/gradient page passes **26 live assertions** across causal
1×/2× density changes: local moves with pane content, scroll stays with the pane,
and fixed stays with the viewport. Candidate `ordered_14.exe` freezes 30
source/fixture inputs and passes **617 native view cases**. Its new export group
passes 35/36 comparisons, retaining one 2× PDF boundary assertion for repeated
uniform vector tiles. The two older bitmap/PDF blend-boundary assertions remain
separate. Matching-source qualification passes both required aggregates
(**6,647 Lambda/input; 4,295 full / 349 partial Radiant, 4,644 required cases**).
Full exports pass **231/233 groups**, retaining exactly those three PDF
assertions. Fresh **131-input ASan** passes **1,081 native cases**, the Canvas JS
golden and all three live fixtures (**22 + 30 + 26 assertions**), with no ASan
report. Nine of ten focused export groups pass; the additional vector-tile PDF
assertion remains, together with the two separately run bitmap assertions.
Four strict DSOs pass; the boundary still has **226 deferred / 89 unapproved
imports** and its raw gate fails against the unchanged 165 allowance. Frozen
sources and results are in `ordered_14_sources/`, `asan_sources_14/`,
`grammar14_asan_summary.json` and `grammar14q_qualification_results.json`. Raw
failures and predecessor/browser controls remain under
`temp/css_support_p1/background_layers/`. No family or tier is closed.


The next coverage probe fails **21/36 comparisons** on `ordered_14.exe`:
fractional uniform linear/radial/conic repeats expose tile seams in PNG/SVG,
and clipped vector tiles add PDF edge coverage. All **24 independent Chromium
pairs** at 1×/2× agree with the rectangle references. A separate PDF-writer
control isolates clipping: adjacent tiles under a rectangular clip differ at
32/64 pixels at 72/144 dpi, while geometrically intersected rectangles match
exactly. The live phase/alpha/space fixture fails **18/22 assertions** on the
predecessor and passes all 22 in Chromium. A staged shared producer change
merges touching uniform tiles along each axis, retains actual `space` gaps,
and geometrically intersects rectangular clips (**CI-18**). The repair is now applied after the matching longhand ASan/boundary run finished;
its own qualification remains pending.

A bounded P2.5 computed-read probe also preserves **13 empty results** for
background shorthand components, lists, owner-unit math, substitution and
inheritance. The accessor table has no rows for these longhands. Chromium's
reference output is frozen separately in `background_reads16_results.json`;
implementation and lifetime qualification remain pending.


The coverage repair now passes all **45 uniform/space probe comparisons**;
its separate single-space predecessor fails all nine, while Chromium confirms
all six single-space pairs at 1×/2×. The shared tile walk uses the authored
position when fewer than two `space` tiles fit on an axis. The expanded live
fixture improves from **4/26 to 26/26 assertions**, matching Chromium over phase,
alpha, gap, single-space and causal density changes. Full native vector/view
checks pass **128/617**. The durable export group adds 15 independent geometries
at both densities; full source-matched qualification is next. Ownership remains
within **D4.5.1v4 / D4.1.4v5**, with no new retained payload type (**CI-18**).


Candidate `ordered_15.exe` freezes 32 inputs. Its 15-case coverage export group
passes all **90 PNG/SVG/PDF comparisons** at 1×/2×, and the longhand group now
passes all **36**, repairing the third PDF assertion without changing its
reference or checker. Both source-matched normal probes and the 26-assertion
live fixture pass; the complete Radiant/export/ASan/boundary run is active.
The last required Lambda/input result is the unchanged-engine candidate 14;
this paint-only revision does not claim a fresh Lambda aggregate.

The candidate 15 run finished: **4,296 full / 349 partial** Radiant cases,
**1,081 ASan native cases**, the Canvas golden and **133 live assertions** in
each runtime pass. Full exports pass **232/234 groups**, retaining the two
ordered blend PDF assertions and exposing one negative-position PDF clip-edge
assertion; the focused instrumented run reproduces that third assertion without
an ASan report. The boundary remains **226 deferred / 89 unapproved**, with the
same import set and four strict DSOs. Matching source and binary evidence is
frozen in `solid15q_*` and `asan_sources_15`; this candidate remains provisional.
An independent writer control confirms that intersecting a rectangle before
native clipping restores exact 72/144 dpi coverage, including when the native
clip is retained. The next repair shares sharp-rectangle recognition between
raster and PDF and intersects PDF rectangle fills with their active rectangular
clips, preserving native curved/rotated clipping (**CI-18**, **D4.5.1v4**).

The prepared computed-background UI fixture confirms the accessor gap in the
live host: **14 empty-read failures among 18 assertions**, while all four
inherited-size paint controls pass. Chromium passes all 18, including the
causal owner-font resize. It remains staged in `reads16_ui.*`; no computed-read
implementation or qualification is claimed yet (**CI-18**, P2.5).

### 2026-10-12 — P2 rectangle clipping and P0/P2.5 computed background components

The rectangle repair now passes **234 affected comparisons**: 36 independent
overflow-intersection, 72 position, 36 longhand and 90 uniform-tile PNG/SVG/PDF
checks at 1×/2×. Overflow paths with all-zero corners now retain the rectangle
primitive. PDF lowering tracks owned rectangular clip bounds across batches,
intersects rectangle fills geometrically, and releases the stack on pop, page
reset and teardown. Curved/rotated clips retain native path clipping. The frozen
33-input `ordered_16b.exe` still reproduces the two ordered bitmap PDF assertions;
full qualification is combined with the following accessor increment.

Nine computed background component accessors now share layout's cascade winner,
substitution validation, shorthand decoder and axis origins. Length computation
shares the existing font/SVG/grid normalizer and uses each declaration owner's
prospective font context. Inherited original periods are projected over the
receiving image count; queries borrow no retained paint payload and serialize
through caller-owned growable strings (**CI-18**, **D4.5.1v4 / D4.1.4v5**).
Generated before/after getters retain their originating font and inherited
computed lengths. Image/shorthand getters and effect sampling remain open.

The expanded live fixture improves from **4/20 to 20/20**, with all 20 assertions
matching Chromium 152. Three native controls cover defaults/competition,
independent inherited periods, before/after font contexts, 256 repeated reads,
strings beyond 511 bytes and retained caller strings after declaration
replacement; all **620 native view cases** pass. Four computed-read-to-paint
references add **24 passing export comparisons** at 1×/2×. One initial native
test crash came from reusing a transferred `StringBuf`; its raw backtrace is
retained and the corrected control retains the returned string once.

`ordered_17.exe` freezes 37 inputs with runtime SHA-256
`38aa95b8390e41422630c0d19755ee5002e7cc6091f929fcaaa3e2024266cdf9`.
Matching Lambda/input, Radiant, full exports, ASan, six live fixtures and boundary
qualification is running in `reads17q_*`; no matching aggregate/ASan result or
family promotion is claimed before it completes. The inventory still contains
410 registered names, and its freshness check and Radiant cast lint pass.

That first combined run stopped at a real required-gate failure: **6,646/6,647**
Lambda/input cases passed; `map_export` reached a stack overflow. Changing
zero-radius contour storage to a compact rectangle made the affine expander
recreate the same primitive recursively. Its correction then exposed an
existing fractional-alpha raster control whose coverage changed with the new
storage. The final repair preserves the original contour storage and recognizes
sharp rectangular contours in the shared bounds query. PDF geometry can then
intersect the same rectangle without changing raster path coverage. The initial
gate, direct map reproducer and vector failure remain separate raw artifacts;
they are not overwritten or treated as qualified results (**CI-18**).

The final contour query passes **130/130 vector cases**, including sharp affine
expansion and the existing fractional-alpha snapshot control. `map_export`
passes both directly and through its unchanged batch harness. The next frozen
candidate repeats the full required gates; no checker, golden or failure
inventory was changed. The next P1.4 radius reproducer is independent: all
**21 explicit radial-size comparisons** fail on `ordered_16b.exe`, while
Chromium agrees on all 14 reference pairs at 1×/2×. Literal px/%/em and math
radii are absent from the retained radial descriptor; its implementation is
next after the combined candidate's source qualification.

The final `ordered_17c.exe` freezes the same 37 inputs with runtime SHA-256
`7835da0b749f0143bdcf8bd31b98a462a2b7d09fca620b71be2ac2c7fe85e1d6`.
Its fresh complete qualification runs in `reads17cq_*`, superseding the stopped
`reads17q_*` attempt while retaining its failed gate. The repaired vector suite
and exact batch `map_export` control pass; complete aggregate/ASan/boundary
results remain pending. Inventory freshness, cast lint and scoped whitespace
checks pass for this source state.

The completed combined checkpoint is `ordered_17d.exe`, runtime SHA-256
`195f5bba51f6b3b844b22ad6539a86e43cb13d2db7548d5cb1de756be34830d2`.
The passing Lambda gate rebuilt the executable without changing its 37 source
inputs; the actual tested binary was frozen under this alias and its passing
record reused with exact runtime/source verification. The stopped 17c guard
assertion remains alongside the original results. Required gates pass **6,647
Lambda/input** and **4,299 full / 349 partial Radiant** (4,648 required cases).
Full exports pass **235/236 groups**; only the two original 2× ordered bitmap
PDF blend-edge assertions remain. The negative-position and fractional-alpha
controls pass with the original contour storage (**CI-18**).

Matching 131-input ASan runtime
`351371635191c952cddc4764684ac712478777dccf3c4bc752a1a4b21b5950d7`
passes all **1,086 native cases**, the Canvas JS golden, all six live fixtures
(**153 assertions per runtime**) and all 13 focused export groups (**372
background/overflow/read comparisons and 600 marker comparisons**), with no
ASan report. The separate ordered group retains the same two PDF assertions.
All four strict DSOs link; the raw boundary gate still fails with **226 deferred
/ 89 unapproved imports**, exactly the prior set against baseline 165. Source
snapshots, binary hashes, raw results and protected artifact hashes are retained
in `ordered_17d_sources/`, `asan_sources_17d/` and `reads17dq_*`. Cast lint,
inventory freshness and scoped whitespace checks pass. Image/shorthand reads,
effect sampling, broader image acceptance and all other P0–P2 criteria remain
open; no whole family or tier is promoted by this checkpoint.

### 2026-10-12 — P1.4 explicit radial-radius computation under qualification

The frozen predecessor ignores all seven px/%/em/math radius forms (**21/21
wrong PNG/SVG/PDF comparisons**) and accepts all 14 malformed radial preludes;
Chromium preserves those 14 earlier declarations. The live predecessor passes
only **15/26** assertions. A shared lower decoder now validates radial shape,
size/radius arity, nonnegative literals, typed circle radii and generic center
syntax, including selected `image-set()` candidates. Omitted shapes infer a
circle for one length and an ellipse for two length/percentage components,
following [CSS Images 3 §3.2.1](https://www.w3.org/TR/css-images-3/#radial-gradients).

The computed descriptor owns radius syntax normalized in its declaration
owner's font context; the shared radial geometry resolves independent percentage
axes/nonlinear math against each painted tile. Inherited descriptors deep-clone
that payload with their stops, and shared descriptor retirement also reclaims
mask replacements (**CI-18**, **D4.5.1v4 / D4.1.4v5**). Four native controls cover
literal grammar, selected invalid winners, inferred shape, tile resizing, owner
font inheritance/retirement and 256 replacement cycles with stable live pool
bytes/allocations. All **624 native view cases** pass.

The applied repair passes **36/36** expanded PNG/SVG/PDF probe comparisons,
**84 durable output comparisons** at 1×/2× and **26/26 live assertions** through
font/width/variable changes and density changes. Chromium matches all 26 live
assertions and 24 expanded reference pairs; its reference uses homogeneous
nonlinear percentage radii. Mixed length/percentage `min()`/`max()` radii remain
native/spec controls: Chromium 152 declines those values, which is retained in
the browser grammar record. Inventory freshness (410 names), cast lint and
scoped whitespace checks pass. A fresh required Lambda/input gate followed by
complete Radiant/export/ASan/boundary qualification is active in `radial18q_*`;
no matching aggregate or family promotion is claimed before it completes.
Full gradient centers, complete stop/hint/interpolation grammar, image/shorthand
getters, effect sampling and remaining P0–P2 work stay open.

The frozen 40-input `ordered_18.exe`, runtime SHA-256
`c1791162e031240f3b348171229e79baa7bf90115d2bca566ee7a44afeceff24`,
passes fresh required gates (**6,647 Lambda/input; 4,300 full / 349 partial
Radiant**) and **236/237 export groups**, retaining only the two original
ordered bitmap PDF assertions. Matching 131-input ASan runtime
`dfe5dd6356b81c75b632e4e69fd0138046310fc5576212b33968c2c4cf1aa82a`
passes **1,090 native cases**, all seven live fixtures (**179 assertions per
runtime**), the Canvas JS golden and all 14 focused groups (**456
background/radius/overflow/read comparisons plus 600 marker comparisons**),
with no ASan report. All four strict DSOs link; the raw boundary gate remains
226 deferred / 89 unapproved, with no import-set change. Frozen source snapshots,
raw results and protected artifact hashes are retained in `ordered_18_sources/`,
`asan_sources_18/` and `radial18q_*`. Twelve further mask/marker output probes
match their independently equivalent radius references and contain real
red/blue/masked pixels, excluding matching omissions.

A follow-up case probe exposes a new validator regression: three uppercase
radial preludes are rejected while their lowercase control is accepted.
Consequently this is a bounded lowercase computation checkpoint; case grammar
remains P0.2 work for the next candidate. Full radial/conic center computation
also fails **30/30** off-center output comparisons while Chromium agrees on
all 20 reference pairs. Its prepared live fixture fails **16/27** assertions
on the predecessor and passes all 27 in Chromium. The staged next repair shares
the generic position decoder, owner-normalized geometry and whole-box center
basis across radial/conic paint, and uses the existing caseless identifier and
image-function metadata matchers. No staged-code qualification is claimed yet.

### 2026-10-12 — P0 case grammar and P1.4 full gradient centers under qualification

The applied center repair improves the strengthened off-center probe from
**0/30 to 30/30 PNG/SVG/PDF comparisons** and the causal live fixture from
**11/27 to 27/27**, matching Chromium's 20 reference pairs and all 27 live
assertions. Radial and conic computation now share the generic one/two/four-value
position decoder. Center offsets use the entire gradient box (the point has
zero object size), and owner-normalized lengths/nonlinear percentage expressions
remain in the descriptor's independently cloned geometry payload. Whole-box
resizing, signed offsets, inherited fonts and parent retirement have native
coverage (**CI-18**, **D4.5.1v4 / D4.1.4v5**).

The radius validator's uppercase regression is corrected through the existing
caseless identifier matcher. Gradient kind and repetition also use one lower
function metadata table across linear/radial/conic resolution. The four literal
case controls are accepted again; six uppercase function/prelude reference
pairs match Chromium at both densities. Three new native controls pass, and
all **627 view cases** pass, including 256 inherited-geometry recascades with
stable live pool bytes/allocations. Two durable groups add **96 passing output
comparisons** at 1×/2×. Inventory freshness (410 names), cast lint and scoped
whitespace checks pass. Fresh matching-source Lambda/Radiant, full export,
ASan, eight live-fixture and strict-boundary qualification is running in
`centers19q_*`. No aggregate/family promotion is claimed until it completes.
Broader conic/body grammar, stop hints/interpolation, computed image/shorthand
reads, effect sampling and all unfinished P0–P2 work remain open.

### 2026-10-12 — Full-center/case checkpoint qualification complete

The 42-input normal runtime is
`122fe5c3167b564c56dfc3951cf409e620436c77ef9ab96ca7ee33cb5dbf991a`;
the matching 131-input ASan runtime is
`64b6fd7787cd2d2995ce2f11b381ba396fc1bdcb92716361596031469dc3461b`.
Fresh Lambda/input passes **6,647** cases; Radiant passes **4,301 full /
349 partial (4,650 required)**. Full exports pass **238/239 groups**, retaining
only the two recorded ordered-background 2× PDF edge assertions. All **1,093
ASan native cases**, **206 live assertions per runtime**, the Canvas JS golden
and 16 focused export groups (**552 background/radius/center/case/overflow/read
and 600 marker comparisons**) pass without sanitizer reports. All four strict
DSOs link; the raw boundary gate retains **226 deferred / 89 unapproved**
imports with no additions/removals relative to the radius checkpoint. Proofs
are immutable in `ordered_19_sources/`, `asan_sources_19/`, `centers19q_*` and
`centers19q_protected_artifacts.json`. The uppercase radial validator regression
is now qualified as corrected; broader family acceptance remains open.

The next stop-math probe reproduces **26/27** output mismatches while Chromium
passes all 18 equivalent reference pairs at 1×/2×. Function-valued stops are
currently treated as omitted positions, and omitted stops distribute before
their used line basis is known. The next repair retains independently owned
computed stop syntax, reuses the existing math evaluator and shares used-position
fixup across background, border, text and radial-mask consumers (**CI-18**,
**D4.5.1v4 / D4.1.4v5**). A separate PDF control shows that replacing the bitmap
fallback with a native isolated transparency form retains the same Poppler edge
differences; Cairo matches the form and rectangle exactly. No checker/tolerance
change, vendor patch or unproven PDF group fix is applied.

### 2026-10-12 — P0.2/P1.4 qualified gradient-stop math and used-line fixup

Function-valued stop positions now retain owned computed syntax after font and
viewport leaves resolve in their declaration owner. Pure conic angle math
computes in canonical degrees; percentages/nonlinear length-percentage math
receive the used gradient line before endpoint, monotonic and omitted-run
fixup. Background, border-image, clipped raster text and radial-mask consumers
reuse the shared conversion. Masks prepare that list once per composite region.
The lower stop decoder validates typed positions and body/hint ordering for
ordinary and selected images. Invalid dimensional stops preserve earlier
literal image winners. Full prelude/angle-percentage breadth and actual hint
interpolation remain open.

The common clone/release path deep-clones each stop's expression and retires
partial targets without aliasing their source (**CI-18**, **D4.5.1v4 /
D4.1.4v5**). The descriptor remains POD for existing temporary-buffer contracts.
Four new view controls cover used-line ordering, owner fonts, parent retirement,
256 stable-pool recascades/mask replacements and invalid winners. All **631
view/style** and **130 vector** cases pass; the existing retained-paint control
now also destroys computed-expression owners before replay.

The strengthened output probe improves from **64/69 mismatches to 69/69 paired
comparisons**, with **46 exact Chromium reference pairs**. The causal live
fixture improves from **10/34 to 34/34 assertions** at 1×/2×, matching Chromium.
Positive coverage detects existing SVG/PDF omissions: clipped gradient text
is absent, and radial masking is absent from the red rectangle. These are P2.2
work; matching omissions are excluded from the durable all-output group.
Its **21 cases × 2 densities × 3 formats (126 comparisons)** require real red
coverage; a separate raster-only group adds **4 comparisons**, blue/red glyph
coverage and a white masked-corner control. Both groups pass. Inventory
freshness (410 names), cast lint and scoped whitespace checks pass.

Fresh matching-source Lambda/Radiant, full export, 131-input ASan, nine live
fixtures, retained ownership and strict-boundary qualification will be recorded
in `stops20q_*`. No completed aggregate or family promotion is claimed yet.
The two preserved PDF edge assertions and the full outstanding P0–P2 goal
remain open.

### 2026-10-12 — Gradient stop-math checkpoint qualification complete

The 47-input normal runtime is
`ae0f48e3e96eade3a9e2ddedbbe98766630c2e45f0af88c57ed03bafa96dd228`;
the matching 131-input ASan runtime is
`b8ab0c7170f20b4188268fc46f31641b895837e9983f50df0a5d2217a4bf1b2e`.
Fresh Lambda/input passes **6,647** cases; Radiant passes **4,302 full /
349 partial (4,651 required)**. Full exports pass **240/241 groups**, retaining
only the two recorded ordered-background 2× PDF edge assertions. All **1,097
ASan native cases**, **240 live assertions per runtime**, the Canvas JS golden
and 18 focused export groups (**678 background/geometry/read + four raster
mask/text + 600 marker comparisons**) pass without sanitizer reports. All four
strict DSOs link; the raw boundary gate retains **226 deferred / 89 unapproved**
imports with no additions/removals. Immutable proofs are `ordered_20_sources/`,
`asan_sources_20/`, `stops20q_*` and `stops20q_protected_artifacts.json`.
No broader family promotion is claimed.

The next P2.2 mask control compares a fully transparent red/blue subtree with
a genuinely empty box: raster matches, while SVG and PDF each differ by
**1,536 pixels**. The initial hidden-subtree reference also exposed an existing
visibility omission in those exports; its raw matching-omission evidence is
preserved separately. The corrected five-case controls match all ten Chromium
reference pairs; a 42-event /30-assertion raster mutation fixture passes on
frozen20 and Chromium. The root is the missing mask in the shared effect
descriptor/walk. A staged repair carries the existing frame-local radial-mask
payload through density/transform-aware effect capture and applies mask/opacity
once; retained display lists keep their existing borrowed-mask rejection
(**D4.5.1v4**). It is not applied or qualified at this checkpoint. Clipped-text
and visibility export paths remain separate P2.2 work.

### 2026-10-12 — P2.2 qualified radial-mask export effect groups

The corrected transparent-subtree control differs by 1,536 pixels in each
predecessor SVG/PDF output while PNG passes. The shared export walker omitted
`BoundaryProp::mask` from its effect eligibility and `PaintEffectGroup` omitted
the mask descriptor. The walker now passes the existing `RadialMaskPaint`
through shared PaintIR and effect capture. Masks composite the flattened
subtree once with opacity, after filtering and before blending; transform and
density inverses retain the same whole-box sampling context. Native opacity-only
export paths correctly fall back when a mask is present. No vendor/PDF-writer
change or comparison tolerance change is involved. This is an existing mask
approximation repair under **CI-18 / P2.2**, not the full mask family.

The 16 native density/translation, opacity/blend and nested-opacity controls
pass with child clipping and retained-list rejection. The display list still
rejects borrowed mask style payloads; the final captured image owns its pixels
(**D4.5.1v4 / D4.1.4v5**). The new export group passes all 30 PNG/SVG/PDF
comparisons at both densities, including positive red/blue coverage and cleared
content corners. All 30 causal raster live assertions pass. Chromium passes
all ten image pairs and all 30 live assertions. Matching omitted paint receives
no credit: the earlier visibility-hidden reference is retained as raw evidence
of the separate visibility omission.

Frozen candidate `ordered_21.exe` has SHA-256
`8834ee8694a31eb00b0a35188f408655d5d272cab5bd4cc2ee7301a9770f03a8`
and 51 anchored inputs. Fresh gates pass **6,647 Lambda/input** and
**4,304 full /349 partial Radiant cases (4,653 required)**. Full exports pass
**241/242 groups**; the only failures remain the two ordered 2× PDF edge
assertions. Matching 131-input ASan runtime SHA-256
`c7b9f0d3956e2c502979cafdc22894dbfef1a2df5ac666698dd8957450f581a7`
passes **1,098 native cases**, **270 live assertions per runtime**, the Canvas
JS golden and all **19 focused export groups** (708 background/geometry/read/mask,
four raster-only mask/text and 600 marker comparisons). There are no sanitizer
reports. All four strict DSOs pass their ownership checks; the raw boundary
gate remains at **226 deferred /89 unapproved** imports with no additions or
removals. Source snapshots and 69 protected artifacts are in
`temp/css_support_p1/background_layers/mask21q_*`. No family or P0–P2 tier is
closed. The reconciled inventory now carries **47 bounded partial names in
14 families** of the 410-name registry, including the delivered background
components and existing radial-mask approximation.

The next visibility probe reproduces 18 failures among 24 PNG/SVG/PDF pairs:
exports omit hidden boxes/text and visible-descendant rules; raster also paints
non-table collapse, deferred outlines and image markers. A separate replaced
image adds two export failures. The corrected quoted inline-SVG control already
passes all three formats and remains regression coverage. The prepared repair
shares computed self-visibility while continuing descendant traversal, and
keeps hidden boxes out of hit testing. It was applied after candidate21 finished
and its evidence was protected. Clipped gradient text remains a distinct paint
producer root.


### 2026-10-12 — P2.2 qualified visibility paint and hit repair

A shared computed-self visibility predicate now suppresses ordinary hidden and
non-table collapsed box, text, replaced-image, marker, outline, column-rule and
scrollbar paint while retaining visible descendant traversal in raster and
export walkers. The existing inline-SVG renderer retains its own descendant
visibility handling. Hit testing excludes the invisible box itself and keeps
its visible descendants. The hidden-scrollport control exposed a second root:
its stored scroll offset was gated by scrollbar presence. Hit offsets now
include programmatically scrolled hidden overflow independently on each axis,
while visible/clip axes retain their unscrolled hit space.

The ten browser image pairs at both densities and all 72 browser live
assertions pass. Frozen20 fails 30 of those live assertions. The first repair
passes70/72; correcting the hidden-scrollport hit coordinates passes **72/72**.
The new PNG/SVG/PDF group passes **60 comparisons** at both densities, including
positive visible-descendant pixels and cleared own-paint corners. Raw earlier
fixture revisions and failures remain separate. Full matching-source qualification is complete; this is not family closure.
State and paint payload owners retain **D4.5.1v4 / D4.1.4v5**. Table collapse,
focusability, link annotations, broad independent/paged interactions and
clipped gradient text remain open under **CI-18 / P2.2**.


Frozen visibility candidate `ordered_22.exe` has SHA-256
`014f13f760232055dc4768963c01101fff97550b1fa415cc1fc14aa859bf11c2`
and 58 anchored inputs. Fresh gates pass **6,647 Lambda/input** and
**4,305 full /349 partial Radiant (4,654 required)**. Full exports pass
**242/243 groups**; the remaining group preserves the same two ordered 2x PDF
edge assertions. Matching 132-input ASan runtime SHA-256
`2d540b2f9c2dafee507407cc98289cc1b43648f4964d241bb2188422116f4a8b`
passes **1,098 native cases**, **342 live assertions per runtime**, the Canvas
JS golden and all **20 focused export groups** (768 background/geometry/read/
mask/visibility, four raster-only mask/text and 600 marker comparisons). There
are no sanitizer reports. Four strict DSOs pass; the raw boundary gate remains
at **226 deferred /89 unapproved** with no additions or removals. Source
snapshots and 73 protected artifacts are under
`temp/css_support_p1/background_layers/visibility22q_*`. The inventory adds a
bounded visibility record: **48 partial names /15 families /410 registry**.
No family or P0–P2 tier is closed.

### 2026-10-12 — P1.4/P2.2 applied gradient-range repair; wrap-up handoff

Valid CSS stops outside 0–100% reached the native 0–1 stop contract unchanged.
The expanded predecessor control observed an allocation-sensitive crash and
heap corruption for stops at 150%/200%. Read-only backend inspection traced
the write beyond the fixed color table to an unbounded first-color prefix
when the first native offset exceeds one. The repair stays in Lambda's CSS
producer; no vendor source was changed.

Linear producers now extend the gradient line and normalize offsets. Radial
producers preserve interpolation at zero radius before dropping negative
stops, and extend both radii for overshoot. Border-image projection reuses the
normalized source line. The existing stop-color sampler is shared through the
paint-boundary module. Constant painted intervals reuse the owned solid-path
command, avoiding unnecessary gradient rasterization. Recording/source
retirement controls include outside stops for all three gradient kinds and
repetition modes. Owners retain **D4.5.1v4 / D4.1.4v5**; this is bounded
**CI-18** producer work, not a new ruling or family closure.

The 19 independent cases pass **38/38 exact Chromium image comparisons** at
both densities, and all **20 browser live assertions** pass. The latest
debug build (`range23_*_build_c.log`) succeeds. Current focused checks pass
both ownership/replay cases and **20/20 live assertions**; the final native
vector sweep passes **131/131**. The export group passes **94/114 comparisons**
and fails 20. All SVG pairs pass. Remaining failures include one-byte PNG/PDF
color differences, transformed native overflow clipping and 2× PDF hard-stop
boundary pixels. The solid-interval change reduced the earlier 24 failures
to 20. Raw earlier crashes, failed builds and failed comparisons are retained;
no comparator, tolerance, golden or boundary allowance was changed.

Final no-int-cast lint and the 410-name generated inventory check pass.
Inventory regeneration updates native test line references only, retaining
**48 partial names /15 families**. This applied repair has **no refreshed
required baselines or matching sanitizer qualification** and is not promoted
in the support matrix. The qualified visibility source/binary snapshots and
73 protected artifacts remain the last accepted checkpoint. Current focused
evidence is under `temp/css_support_p1/background_layers/range23_*`, especially
`range23_exports_focus_b_results.json`, `range23_live_current_b_results.json`
and `range23_wrap_vector_results.json`. Resume from the current source; the
earlier `range23_stage` draft is stale. Complete the remaining range failures
and full qualification before acceptance, then continue the open P0–P2 work.

### 2026-10-12 — P0/P1.5/P2.2 resumed clip, export and focus consumers

Fresh reproduction on merged `9e0d913f6` retains all 20 range export failures
and both ordered-background PDF edges. Raster overflow now retains the owning
box's clip geometry and original forward transform before scrolling; its
coarse rejection bound uses projected destination coordinates. Rounded and
transformed boxes receive exact scoped clips. Plain untransformed rectangles
retain their existing block-bound constraint, avoiding redundant image masks.
The shared SVG path
writer preserves native float control points and exact short numeric spellings;
paint attributes retain their command separator. The duplicated paint path
writer is removed. PDF gradient snapshots intersect rectangular ancestor clips
in owner coordinates, and image output constrains its destination extent.

Computed-self visibility also gates native sequential/programmatic focus and
DOM `focus()` through the existing immutable style-provider table. An explicitly
visible descendant of a hidden box remains eligible. Provider absence,
mandatory entries and concurrent publication retain **D7.1.3 / D7.5.3**;
borrowed state and paint payloads retain **D4.5.1v4 / D4.1.4v5**. This extends
bounded **CI-18** consumers without changing a formal ruling.

The new overflow fixture passes **40/40 live assertions** in Chromium and
Radiant, including transform, reflection, radius, scroll, width and density
changes. Its three independent output cases pass **18 PNG/SVG/PDF comparisons**.
The focus fixture passes **32/32 Chromium and native assertions**, improving
from 14/32. Native vector tests pass 132/132 and lower provider tests pass 2/2.
Gradient exports improve to **106/114**; eight one-byte marker/border PNG/PDF
differences remain. Both old PDF edge assertions remain, with reduced error.
No comparator, tolerance, golden or boundary allowance was changed.

Candidate24's first required Radiant gate exposed six SVG separator assertions
and an SVG image visual regression. Both are repaired: the display-list suite
passes 83/83, and image coverage returns to the protected predecessor's 133
differing pixels. The failed 74-input source/binary checkpoint and raw results
remain protected. Candidate25 exposed recursive compact-rectangle expansion
in map SVG export. A shared explicit-contour expander repairs the runtime path;
a native affine regression retains the compact primitive as its input. The
zero-radius compact optimization was removed after candidate26 exposed changed
fractional-edge alpha. All 15 map scripts and 132 native vector tests now pass.
Failed/interrupted candidate25/26 artifacts remain recorded. Candidate27's fresh
combined gate passed 6,764/6,765. Its import-cache scheduling assertion passes
in three separate isolated processes; the full fresh rerun passes 6,765/6,765.
The failed source/binary checkpoint is preserved, with no cache source/test
change. Matching required Radiant/native/live/export sanitizer qualification
continues (`consumer27r_*`). No new support promotion or family closure is
claimed until that work finishes.

The next rendered-focus probe is staged separately: Chromium passes all 48
script/Tab/Shift+Tab assertions at 1×/2×, while the current native runtime passes
22/48. It reproduces `display:none` targets/ancestors, CSS overrides of HTML
`hidden`, and visibility mutations before the next frame. The staged shared
predicate reads live computed visibility and display along flat-tree ancestry,
retaining the existing provider's immutable-table contract (**D7.1.3 / D7.5.3**).
The [HTML focus-area model](https://html.spec.whatwg.org/multipage/interaction.html#focusable-area)
also admits rendering delegated to children, so `display:contents` is retained
in a native control despite the recorded Chromium difference. CSS overrides
of `hidden` follow the [HTML hidden-attribute model](https://html.spec.whatwg.org/multipage/interaction.html#the-hidden-attribute).
This is a bounded **CI-18** contract; no staged implementation is accepted yet.

### 2026-10-12 — Resumed-consumer checkpoint and newly reproduced export residue

The completed `consumer27r_*` run protects 74 normal /131 instrumented inputs,
89 result artifacts and separate required-gate/failing-image manifests. Normal
runtime `e43bb50efed53ad3c3a6acae7fe95c87009d595698aa103f804adab02d597020`
and ASan runtime
`1968c9d3345711b5a23bb443b545458cd50280b1332c882e3eacd28d58b2f5c8`
match their snapshots. Fresh Lambda/input passes 6,765; required Radiant passes
with 5,366 full /353 partial /one skipped case. All 1,103 native sanitizer tests,
434 live assertions per runtime and the Canvas JS golden pass without a
sanitizer report. Instrumentation covers the named CSS/DOM/lifecycle/paint owner
units; other host objects/archives retain `debug_native`, with leak detection
disabled. The no-int-cast lint and 410-name inventory freshness check pass.

Full exports pass 241/245 groups. In addition to two ordered PDF edges and eight
range differences, the sweep retains 20 constant-alpha marker comparisons and
one inherited selected-image PDF edge. The applied range optimization replaced
constant gradients with solid-path commands, changing alpha lowering. The new
PDF destination mask also changes overlapping image-edge coverage. Both repairs
are staged separately; no tolerance, golden, allowlist or support promotion is
changed (**CI-18**, **D4.5.1v4 /D4.1.4v5**).

The fresh strict module audit fails with 230 deferred /93 unapproved imports
against the unchanged 165 baseline. Four imports are new relative to the older
visibility checkpoint: history traversal, media command/info and window watch.
This is current **D7.1.3 /D7.5.3 /D7.3.3–D7.3.4** ownership/API debt, not closure.
The full P0–P2 goal remains active.

### 2026-10-12 — Applied rendered-focus and constant-gradient alpha repair

The shared focus predicate now reads live computed visibility and resolves
display along flat-tree ancestry. DOM script focus trusts the immutable native
provider's rendering result; provider absence retains its hidden-attribute
fallback (**D7.1.3 /D7.5.3**). The new fixture passes all 48 native and Chromium
script/Tab/Shift+Tab assertions at 1×/2×, improving from 22/48. A native 16-case
control, including variable display, hidden overrides and delegated contents,
passes 4,096 repeated reads without retained pool-byte growth
(**D4.5.1v4 /D4.1.4v5**). The existing 132-case vector suite also passes.

Constant image intervals retain gradient commands and alpha lowering; a shared
two-stop constructor replaces three variants. Removing the redundant global
PDF image mask restores all 600 gradient-marker and 60 selected-image export
comparisons, including the inherited-image edge. Transformed overflow retains
all 18 comparisons. Gradient ranges still fail 19/114: eight one-byte
marker/border differences and eleven 2× PDF boundary comparisons. The trial
opaque-gradient mask restored range boundaries but changed 14 marker edges;
a common image-object experiment did not improve the boundary results. Both
trials and the earlier failed test compile remain recorded. Those trials are
removed; no comparator, golden or allowance changes are made (**CI-18**).

Fresh aggregate and matching sanitizer qualification of the combined applied
source is next. No new support promotion or whole P0–P2 closure is claimed.

### 2026-10-12 — Focus lifecycle settlement and lower-owned host catalog calls

Candidate28's required Lambda/input gate passes 6,765; its required Radiant gate
exposes seven integration failures, with the 76-input source/binary snapshot
and raw gate results preserved. Six failing controls retain focus after a
handler hides its connected anchor. Post-handler mutation commits now clear
ineligible focus through the existing focus transition/event path. Native
document focus remains authoritative for `activeElement`, rather than a stale
script mirror restoring the cleared target. The invariant check is unchanged.
A new fixture passes 24 Chromium/native assertions at 1×/2×, including ordered
blur/focusout delivery, body fallback and explicitly visible descendant retention
(**CI-18**, **D4.5.1v4 /D4.1.4v5**).

The remaining offcanvas fixture requests focus during `showing`, while its
simplified stylesheet makes that state `display:none`. Chromium reproduces the
failed focus acquisition. The fixture now supplies the missing `showing`/`hiding`
display states; the original input and browser/native failures remain recorded.
All five failed DOM controls and both photo-editor integration cases now pass.
Neither expected assertions nor vendor Bootstrap code is changed.

Seven timing, viewport, window-watch, history and media catalog wrappers now
belong to the lower runtime and dispatch through a complete immutable native
host table. Radiant publishes the existing implementations at registration;
provider absence returns `ItemNull`. Shared publication helpers retain atomic
validation and conflicting-publication rejection (**D7.1.3 /D7.5.3**). Lower
tests verify absence, each missing field, operation identity, argument order,
8,000 concurrent publication rounds and 56,000 real catalog calls. The third
worker-test variant shares the existing spawn/join shape rather than copying it.

The fresh focused boundary measure improves from 230 deferred /93 unapproved
to **223 deferred /86 unapproved**, with exactly these seven imports removed
and no additions. The 165 allowance is unchanged; strict boundary closure
remains open under **D7.3.3–D7.3.4**. The combined source is undergoing fresh
required/native/live/export sanitizer qualification (`consumer29q_*`). Gradient
range/ordered PDF residue and all other unfinished P0–P2 criteria remain active.

### 2026-10-12 — Owned-root focus qualification and collection metadata registry

Candidate29 passes the fresh required gates (6,765 Lambda/input; 5,368 full /
353 partial /one skipped Radiant) and 243/245 full export groups. Its required
sanitizer view runner fails the existing owned-root focus regression; the other
five runners pass and no address-sanitizer error is reported. Qualification
stops before live/export sanitizer and boundary checks. The failed source,
binaries, raw assertions and 150 required-gate artifacts remain protected in
`consumer29q_failed_checkpoint.json`; this is an incomplete checkpoint.

The focus helper now stops at the document-owned root. A native debugger probe
also identifies a successful but empty pre-render visibility projection: that
absence was being treated as hidden. Empty reads now preserve the existing
native default. The unchanged ownership regression and all 636 native view
cases pass, including 4,096 live-style eligibility reads without retained pool
growth (**D4.5.1v4 /D4.1.4v5**, **CI-18**). The first insufficient owned-root
repair and its failed native result are preserved separately.

Ten collection metadata getters now use a lower-owned immutable type registry.
The existing Radiant Jube identities and public module getters stay with their
owner; DOM and CSSOM consumers borrow the same addresses through the lower
registry (**D7.1.3 /D7.5.3**). Four provider tests verify absence, every mandatory
field, distinct metadata identities, concurrent publication and conflicting
registration rejection. All 503 fresh JS cases pass with the applied registry.
The 165-symbol boundary allowance is unchanged; a fresh boundary count is
pending, so no reduction is claimed yet (**D7.3.3–D7.3.4**).

Candidate30 has 81 primary normal inputs plus a separately captured DOM CSSOM
consumer source, all unchanged during its fresh gates. Required Lambda/input
and Radiant results remain 6,765 and 5,368 full /353 partial /one skipped.
Full exports and matching sanitizer/native/live/boundary qualification are
running in `consumer30q_*`. The normal binary is
`dd1aa516df263c4eebec5f8453c970f5e56bf7df101b67c16a0e37d78e094ca6`.
The property inventory still contains 410 names; no support promotion is made
before the complete checkpoint. P0–P2 and the long-running goal remain active.

### 2026-10-12 — Qualified clip/focus consumers and seventeen removed imports

Candidate30 completes qualification with unchanged source and binaries:
6,765 fresh Lambda/input cases; 5,368 full /353 partial /one skipped required
Radiant cases; 1,106 matching sanitizer cases; the Canvas JS golden; and
506 live assertions on each of the normal and sanitizer runtimes. The repaired
owned-root focus regression passes. Normal exports pass 243/245 groups;
sanitizer focused exports pass 22/23, retaining the same range group, and the
separate ordered control retains its two PDF failures. The 19 range comparisons
remain eight marker/border channel differences and eleven 2× PDF boundaries.
No sanitizer report, golden/tolerance change or boundary allowance change occurs.

The boundary measures **213 deferred /76 unapproved**, removing exactly the
ten collection-type imports from the 223/86 host-call checkpoint, with no new
imports. Together the applied provider slices remove seventeen imports from
230/93 (**D7.1.3 /D7.5.3 /D7.3.3–D7.3.4**). The strict boundary gate still fails;
P0 ownership closure is not claimed. Four strict DSOs build successfully.

The normal primary snapshot contains 81 inputs; the additional DOM CSSOM
consumer was captured during the gate and matches the 133-input sanitizer
snapshot, giving 82 distinct normal source inputs. Normal runtime:
`dd1aa516df263c4eebec5f8453c970f5e56bf7df101b67c16a0e37d78e094ca6`;
sanitizer runtime:
`d7a20549e862f0679326c97e3b81d12549e003b6ee1a8e382238315b5c02e56b`.
`consumer30q_protected_artifacts.json` protects 96 records, with separate
150-file required-gate and 1,344-file export-namespace copies; those namespace
copies also retain historical controls. `consumer30q_supplemental_protection.json`
checks the additional source against both current and sanitizer snapshots.

The inventory and support matrix now record bounded transformed-overflow and
rendered/settled-focus evidence (**CI-18**, **D4.5.1v4 /D4.1.4v5**). Properties
remain partial where the documented broader consumers are unfinished. A staged
PDF link fix suppresses hidden/collapsed own annotations while preserving
existing visible-descendant links; its durable test fails before and passes
after at both densities. Chromium likewise omits hidden links, while its print
output omits the visible descendant's link even though browsing activates it.
Two remaining unapproved Item host wrappers are staged through the existing
host provider and pass all four provider tests. Neither staged change is part
of this accepted source. A separate clipped-gradient single-glyph probe shows
1,537 red native pixels and zero blue, versus Chromium's 864 red /673 blue;
SVG emits a transparent outline. These are the next concrete P0/P2 slices.
P0–P2 and the long-running goal remain active.

### 2026-10-12 — Applied PDF annotation and remaining Item host ownership fixes

The accepted candidate30 source is protected before applying this slice.
Hidden/collapsed own anchor boxes now omit PDF link annotations through the
existing shared visibility predicate; semantic traversal and explicitly visible
descendant links remain intact (**CI-18**). The new two-density native test fails
six assertions on frozen30 and passes all ten URI-presence checks after the fix.
The isolated candidate and applied main build both pass. Chromium browsing
activates the visible descendant's link, although its print output drops that
annotation; the browser difference is retained as evidence, not a conformance
claim for all PDF semantics.

The two remaining unapproved Item host wrappers, editing-focus clearing and
edit-target selection, now use the established lower-owned host provider. Their
implementations, arguments and return values are unchanged (**D7.1.3 /D7.5.3**).
All four provider tests pass, including nine-operation argument/identity checks
and 8,000 publication rounds. Required Lambda/input passes 6,765 on candidate31;
fresh Radiant and matching sanitizer/live/export/boundary qualification are
running in `consumer31q_*`. Its 82-input normal snapshot includes DOM CSSOM
without a supplemental source record, and normal runtime is
`60ce766fa627d6badc7056f9180ef94716807ca73cabf5d8a87006ef60db1c74`.
No further boundary reduction or link-family promotion is claimed yet.

### 2026-10-12 — Qualified PDF visibility annotations and nineteen removed imports

Candidate31 completes the same fresh required gates: 6,765 Lambda/input and
5,368 full /353 partial /one skipped Radiant. All 1,106 matching sanitizer
cases, the Canvas golden and 506 live assertions per runtime pass, with no
sanitizer report. Full exports improve to 244/246 groups because the new PDF
annotation group passes; the existing 19 range comparisons and two ordered PDF
edges remain. Focused sanitizer exports pass 23/24, including the annotation
group, and the separate ordered control retains its previous failures.

The first boundary attempt links stale Radiant/RT objects containing the moved
host wrappers. Source/header timestamps predate those objects because staged
copies retained their mtimes. The raw linker failure and both object files are
preserved. Rebuilding those exact objects, without changing source or allowance,
builds the four strict DSOs and measures 211 deferred /74 unapproved imports,
removing exactly editing-focus clearing and edit-target selection from 213/76
with no additions (**D7.1.3 /D7.5.3 /D7.3.3–D7.3.4**). The strict gate still
fails on the remaining debt. Developer Guide §7.3 documents this source-swap
hazard; subsequent staged application must give changed files fresh mtimes.

The 82-input normal runtime is
`60ce766fa627d6badc7056f9180ef94716807ca73cabf5d8a87006ef60db1c74`;
the 133-input sanitizer runtime is
`e589ed51f37f911643d727b9cfd4d856b43519326bb661bf572bf9ddaa133ae3`.
`consumer31q_final_protected_artifacts.json` protects 98 final records and the
preserved stale-object provenance, plus separate 150 required-gate and 1,352
export-namespace files. The original failed boundary attempt remains intact;
`consumer31q_boundary_retry_gate.log` is the current measurement.

The visibility inventory now records ten flat PDF annotation controls at 1x/2x:
hidden, inherited-hidden and ordinary collapsed own anchors are omitted, while
visible and explicitly visible descendant links stay present (**CI-18**,
**D4.5.1v4 /D4.1.4v5**). Broader fragment/effect/backface link semantics and the
Chromium print difference remain explicit limits. Nineteen unapproved imports
are removed from the resumed 230/93 checkpoint. An eighteen-operation event
provider is staged separately; five lower tests, all 503 JS cases and 43 live
focus/visibility-event assertions pass. Its source/header overlay preserves
public module spellings as aliases for the lower-owned key schema. A missing
logging-config symlink caused one initial fixture-environment failure, retained
before the 503-pass retry. Full staged ownership/sanitizer qualification is next.
P0–P2 and the long-running goal remain active.

### 2026-10-12 — Applied lower-owned event provider under qualification

Eighteen DOM event host calls now dispatch through a lower-owned immutable
provider rather than importing Radiant directly. Five DOM/JS consumers use the
owned API; the public Radiant event functions remain available to their module
clients. The event target-key schema belongs to the lower interface, with the
existing module names retained as aliases (**D7.1.3 /D7.5.3**). Native event
payload, prototype, path and dispatch implementations are unchanged; their
existing traced backing/pin ownership remains **D4.5.1v4 /D4.5.2**.

Five provider tests cover absence, all eighteen mandatory fields, argument and
operation routing, all eight creation-flag combinations, 8,000 publication
rounds and 200,000 event calls, including conflicting registration rejection.
The existing provider variants now share one round/worker loop. Staged native
integration passes all 503 JS cases and 43 live focus/visibility-event assertions.
One logging test initially lacks its isolated working directory's config; its
raw failure remains, and a read-only link to the unchanged repository config
makes the full 503-case retry pass. Main builds and all five provider tests pass.

Changed files receive fresh mtimes during application, preventing the source
swap hazard observed in the predecessor boundary check. Candidate32 freezes 90
normal inputs and passes fresh required Lambda/input and Radiant gates; full
export, matching sanitizer/native/live and boundary checks are running in
`events32q_*`. The normal runtime is
`f350e4c3d9245bbc83132a1c63bcea1e82d1dc2d44fa3ee3405621edbc9a4ac4`.
The sanitizer recipe also instruments the new DOM core/clipboard consumers.
No further import reduction is claimed before the current audit.

A separate unapplied clipped-text draft paints per-pixel glyph coverage in
native output and uses the shared background positioning box instead of
measured text extent. Its single-glyph split-color counts match Chromium's
864 red /673 blue pixels; the original native output had 1,537 red /zero blue.
Selected-font vector masks reach actual SVG/PDF pixels, but the first independent
split-color references retain eight vector edge differences while all four
native references pass. Direct glyph-contour gradient filling is being tested
through the existing image/gradient machinery (**CI-18**). These drafts remain
unqualified and separate from candidate32. No tolerance or golden changes occur.
P0–P2 and the long-running goal remain active.

### 2026-10-12 — Qualified event provider and thirty-seven removed imports

Candidate32 passes 6,765 fresh Lambda/input and 5,368 full /353 partial /one
skipped required Radiant cases. All 1,107 matching sanitizer cases pass, including
78 DOM/provider and 636 native view cases. The Canvas golden and 525 live
assertions on each runtime pass across 17 fixtures. There are no sanitizer
reports. Full exports remain 244/246; focused sanitizer exports pass 23/24 and
the ordered control retains its previous two PDF assertions. All 19 range
comparisons remain recorded rather than masked.

The four strict DSOs build on freshly applied source. The raw boundary gate
measures 193 deferred /56 unapproved imports, removing exactly 18 event host
imports from 211/74 with no additions. Together the accepted slices remove 37
imports from the resumed 230/93 checkpoint. The 165 allowance stays unchanged;
strict closure remains open (**D7.1.3 /D7.5.3 /D7.3.3–D7.3.4**).

Normal 90-input runtime:
`f350e4c3d9245bbc83132a1c63bcea1e82d1dc2d44fa3ee3405621edbc9a4ac4`;
sanitizer 136-input runtime:
`9336359b88bdc8feb7c34e936ab8f6b5fd6d2553cfb63ef0b565363cb7d69b8a`.

`events32q_protected_artifacts.json` protects 99 records; separate 150 required-gate
and 1,352 export-namespace copies preserve controls and failures. Instrumentation
covers the owned CSS/DOM/lifecycle/paint subset and native regression translation
units, including the new DOM core/clipboard consumers; remaining host objects
use debug_native and execution disables leak detection. Publication, traced
payloads and dispatch identity preserve **D4.5.1v4 /D4.5.2**.

The clipped-text draft remains unapplied. Direct-contour fills did not remove
SVG channel differences and worsened PDF edges; physical-grid snapshot alignment
reduces the severe PDF displacement but retains vector differences. Exact draft
sources, failed link/paint trials and independent references are preserved in
`temp/css_support_p1/background_layers/text_clip32/`. Native glyph coverage and
owner-positioning fixes pass four PNG references, while eight vector comparisons
remain failures (**CI-18**). No golden/tolerance or allowance changes occur.
P0–P2 and the long-running goal remain active.

### 2026-10-12 — Applied event workflow provider continuation

Four remaining synthetic/author-template dispatch imports now use the existing
lower-owned event provider. Native implementation and public owner entry points
stay in Radiant, with declarations promoted to its existing event header rather
than copied into a new seam (**D7.1.3 /D7.5.3**). The record-operation list remains
shared with the module, while workflow callbacks retain their distinct owner
names. Existing dispatch/pin/traced ownership remains **D4.5.1v4 /D4.5.2**.
The provider has 22 operations and the shared worker test verifies 29 calls per
publication round, including all creation flags, for 232,000 event calls. Main
builds and all five native provider checks pass in `workflow33_*`; no import
reduction or new acceptance is claimed before aggregate qualification. The clipped-text draft is unapplied.
P0–P2 and the long-running goal remain active.

### 2026-10-12 — Qualified event workflow and forty-one removed imports

Candidate33 passes fresh required gates (6,765 Lambda/input; 5,368 full /
353 partial /one skipped Radiant), all 1,107 matching sanitizer cases, the
Canvas golden and 525 live assertions per runtime. Full exports remain 244/246;
focused sanitizer exports remain 23/24 and the ordered control retains its two
PDF failures. No sanitizer report, tolerance/golden or allowance change occurs.
The four strict DSOs build and the raw audit removes exactly four synthetic/
author-template imports: 193/56 becomes 189 deferred /52 unapproved, with no
additions (**D7.1.3 /D7.5.3**). Forty-one imports are removed from 230/93; the
165 allowance is unchanged and closure stays open (**D7.3.3–D7.3.4**).
Normal 91-input runtime:
`e71876606f48aca48ebc9de3f66d8612a711e040d30c35df27c5ef639a91aee8`;
sanitizer 136-input runtime:
`22c492e75074771606d3ac51e25494fa97db1d4e0c2acf586007c966220ce8b5`.
`workflow33q_protected_artifacts.json` protects 99 records with separate 150
required-gate and 1,352 export-namespace copies. The exact sources are protected
before the next ownership application.

### 2026-10-12 — Applied editing, fragment-factory and evaluator ownership fixes

Command-query and designMode transports now use the established lower host
provider; the package retains all editing policy under **D7.2.5**. Mixed result
and absence fields preserve nine existing Item operations and add the unchanged
Item-query and Boolean-designMode mechanisms (**D7.1.3 /D7.5.3**). Staged native
integration passes all 503 JS cases and nine editing command assertions.

The unbacked document-fragment allocator moves from the renderer's Range file
to the existing lower document-node factory. Three duplicated creation sites
reuse it; the existing Range test now verifies null rejection, document identity
and detachment. Backed fragment construction retains its existing input owner
(**D4.5.1v4 /D7.1.3**). All 78 lower range/provider cases pass.

Evaluator-thread transfer moves unchanged into runtime-state.cpp. The runner
calls that owned API; Radiant's public entry remains a thin delegate. A new
regression verifies null/no-op behavior, active-owner rejection, quiescent
transfer, stack-limit refresh and JS rebind under **D5.4.1**. All 37 runtime and
503 JS staged cases pass. No execution/GC ownership ruling changes.

After protecting 33, sixteen verified files are applied with fresh mtimes.
Main debug/native builds and 78 range/provider plus 37 runtime cases pass.
Candidate 34 will add the runtime test runner to matching ASan execution and
summary, instrument its new owning runtime/runner sources, and run the editing
fixture on both binaries. Source/header snapshots and raw failures remain
required before acceptance; no further import reduction is claimed yet.
Clipped-text and the broader P0–P2 criteria remain open.

### 2026-10-12 — Qualified editing, fragment and evaluator ownership

Protected `ownership34q_*` qualifies the applied editing host operations, shared
lower fragment factory and runtime context-transfer owner. The existing editing
policy and public C APIs stay unchanged (**D7.2.5 / D7.1.3 / D7.5.3**); quiescent
thread transfer preserves side-stack and JS TLS ownership (**D5.4.1**).
Fresh gates pass 6,766 Lambda/input and 5,368 full /353 partial /one skipped
Radiant cases. Seven source-matched sanitizer runners pass 1,144 cases, eighteen
live fixtures pass 534 assertions per runtime, and the Canvas golden passes.
The instrumented owner set includes runtime-state and runner; nine regression
translation units are instrumented, with remaining host objects/archives from
`debug_native` and leak detection disabled. This is bounded ASan qualification.

Full exports remain 244/246; focused exports remain 23/24, and ordered exports
retain the two PDF boundaries. Nineteen range comparisons remain unchanged.
The fresh boundary measures 185 deferred /48 unapproved imports, removing the
two editing calls, fragment factory and evaluator switch, with no additions or
change to the 165 allowance (**D7.3.3–D7.3.4**). Forty-five imports are removed
since 230/93. All four strict DSOs build; the strict gate still fails.
Ninety-nine normal and 140 sanitizer inputs match protected runtimes. The record
protects 105 evidence artifacts, 150 required-result copies and 1,352 export
namespace copies. `ownership34q_asan_summary.json`, `ownership34q_boundary_summary.json`
and `ownership34q_protected_artifacts.json` are the measured evidence. No formal
ruling or CSS-family qualification changes.

### 2026-10-12 — Applied linear-gradient text consumer for qualification

The native glyph consumer now samples shared gradient stops at each covered
pixel center in the background owner's positioning box, preserving the original
foreground and selected text paint. Generated images transfer to deferred paint
ownership at submission, including allocation failure (**D4.5.1v4 / CI-18**).
SVG/PDF record selected-font contours against the same shared image consumer.
Opaque constant intervals use solid contour paint with explicit owner/stripe
clips. Exact cardinal angles preserve axis alignment; owner coordinate paint
order keeps shared PDF clip edges stable under reversed gradient directions.
The existing five-argument gradient-image API remains intact.

The final isolated source passes 42 exact PNG/SVG/PDF comparisons at both
densities: owner-sized/indented glyphs, both axes, reversed directions, repeats
and two-glyph runs. All 134 vector cases pass, including payload rewind cleanup
and exact cardinal owner boundaries. Ten verified files are applied with fresh
mtimes. `text_clip35_staged_proof.json` and `text_clip35_verified_stage/` preserve
source, runtime and pre-application hashes. Full matching-source qualification
is next; no whole `background-clip:text` support claim is made.

Earlier text trials preserve raw failed output comparisons and binaries. The
native axial PDF writer experiment remains isolated and unapplied; it first
exposed exponent number encoding, then retained edge differences. The unrelated
PDF snapshot alignment trial is excluded from the applied increment. Broader
background layers/tile geometry, continuous/transparent vector edge parity,
fragment/shaping/ancestor ownership and color-font coverage remain open.

The applied runtime passes all 40 live pixel assertions in
`test/ui/css_gradient_text.{html,json}` at both densities. Width/height,
direction, indentation, clip mode, foreground and text-content changes cause
real repaint checks. The protected preceding runtime fails 16/40 against the
same fixture (`text_clip35_preceding_live_*`), preserving the original defect.
Main build, inventory check and no-int lint pass. Fresh qualification is active
in `text_clip35q_*` with the live fixture on both matching binaries, the two new
export groups and both payload/cardinal tests. No matching aggregate acceptance
is claimed until it finishes.

### 2026-10-12 — Qualified bounded linear text and corrected export provenance

Candidate35 passes 6,766 fresh Lambda/input cases and the host-enabled Radiant
gate (5,369 full /353 partial /one skipped). Seven matching ASan runners pass
1,146 cases, nineteen live fixtures pass 574 assertions per runtime, and the
Canvas golden passes. The two new text groups pass all 42 exact PNG/SVG/PDF
comparisons on both matching runtimes. The recorded snapshots
contain **103 normal and 141 sanitizer inputs**; the counts are measured in
`text_clip35rq_final_asan_summary.json`. No sanitizer report is present.

The first sandboxed Radiant run preserved local-server failures and macOS
host-service stalls; a local bind probe returned EPERM. The unchanged source
and binary passed with the required host access. The initial export runner was
stale and omitted the two new groups. Its raw results remain preserved. An
explicit rebuild and registration check ran the fresh executable: full exports
**246/248**, focused ASan exports **25/26**, ordered controls one failing group.
The same nineteen range comparisons and two ordered PDF edges remain. Future
qualification rebuilds the export runner and checks every requested group.

The strict boundary remains 185/48 with no additions and unchanged allowance165;
all four strict DSOs build, while strict closure remains open
(**D7.3.3–D7.3.4**). The final record protects 121 artifacts plus 150 required-result
copies and 3,376 native/ASan export files. The accepted evidence is
`text_clip35rq_final_asan_summary.json`, `text_clip35v_runner_provenance.json`
and `text_clip35rq_final_protected_artifacts.json`; the first `text_clip35rq_*`
export summary is superseded. Shared deferred payload ownership remains
**D4.5.1v4**. The inventory records bounded text coverage and explicit limits,
with no whole background/text-family promotion or formal ruling change.

### 2026-10-12 — Isolated text tile/layer investigation

The same tile-size/position/repetition/color fixture fails eight of sixteen
checks on the accepted runtime and passes all sixteen in Chromium and the
isolated shared tile-producer draft. The single clipped-color fast boundary
incorrectly filled the whole box in SVG/PDF; its predicate must reject other
clip destinations. Per-layer text coverage precedes composition: the
[CSS Backgrounds 4 clipping model](https://drafts.csswg.org/css-backgrounds-4/#background-clip)
and independent Chromium captures expose mixed-color antialias edges under
stacked clipped images (**CI-18**). A draft that combined all layers before
coverage was rejected and preserved, despite passing its original simplified
references. Transparent gaps and opaque same-color seams required reference
correction; the corrected multilayer reference paints each clipped layer.

The latest `text_tiles36/stage/` stays unapplied. It shares the existing authored
background/tile walk and applies bitmap coverage to each layer, with direct
retention in the display list. Glyph and image source-over use different
opaque rounding; an isolated shared-blend trial passes 136 vector cases,
including all coverage-byte combinations and thirty-two replays after glyph
cache/style changes, but four native multilayer edge comparisons still differ
by one byte. Other staged tile/vector comparisons pass. The raw Chromium and
native results remain separate; no layer-family qualification is claimed.

### 2026-10-12 — Applied text tiles/layers and native coverage replay

The shared authored background walk now has an explicit text destination.
Color and each image layer receive glyph coverage before composition, using the
same tile-size/position/repeat and clip geometry as ordinary backgrounds.
Native generated layers use the existing CPU blit eligibility and captured
clip data; transform/3D/vector-clip contexts retain their image lowering.
Glyph/image opaque source-over share rounding in the pixel module. SVG/PDF use
selected-font contours per layer. The simple boundary shortcut rejects other
clip boxes, preventing clipped color from filling the whole border box.

Immutable image publication is shared by Canvas, Scene3D and generated glyph
layers rather than copying a third protocol variant (**D4.5.1v4**). Temporary
owners release publication references after display-list leases are acquired.
A trial that forcibly destroyed a leased source is preserved as a failed
snapshot-lifetime experiment; it is fixed at the owner release. Layer count and
selection use shared helpers. No vendored source or formal ruling changes.

Twelve verified code/header/test files and four live fixture files are applied
with fresh mtimes. All 84 staged PNG/SVG/PDF comparisons pass at both densities,
covering size/position/repeat/no-repeat, gaps, clipped color and two opaque
layers, alongside earlier glyph controls. All 136 native vector cases pass:
image/glyph source-over agrees for all 256 coverage values over fifteen color
pairs, and a leased snapshot survives thirty-two replays after bitmap/style
changes. Sixteen tile/color and eight rounded ancestor-clip live checks pass.
`text_tiles36_staged_proof.json` and `text_tiles36_verified_stage/` preserve the
source/runtime and prior main hashes. Full qualification is next; broader
owner/fragment/shaping, transformed layer AA, URL/radial/conic contexts and
browser matte-profile differences remain open (**CI-18**).

### 2026-10-12 — Qualified text tile/layer and immutable publication checkpoint

Protected `text_tiles36q_*` passes fresh core 6,766 and Radiant 5,371 full /
353 partial /one skipped cases. Seven matching sanitizer runners pass 1,148
cases; twenty-one live fixtures pass 598 assertions per runtime, and the Canvas
golden passes. The exporter is explicitly rebuilt and all requested groups are
registered. Full exports pass 247/249 groups, focused exports 26/27; the same
nineteen gradient-range comparisons and two ordered PDF boundaries remain.
The new text-tile group and earlier text controls pass all 84 comparisons on
both native and sanitizer runtimes. No tolerance, golden or allowance changes.

111 normal and 141 sanitizer inputs match protected runtimes. The artifact
record protects 120 records, 150 required-result copies, 1,352 preceding export
namespace files and 1,344 text export files. The measured boundary remains
185 deferred /48 unapproved with no additions; all four strict DSOs build,
while closure remains open (**D7.3.3–D7.3.4**). Shared Canvas/Scene3D/glyph image
publication and owner release follow **D4.5.1v4**. The scoped inventory now
records tile/color/layer consumers and their remaining limits (**CI-18**).
The main source is available for the next verified increment; no whole-family
or P0–P2 closure is claimed.

### 2026-10-12 — Isolated text-shadow routing and visible paint order

`text_effects37/` reproduces six SVG failures: fully transparent shadow styling
selected a branch that omitted background paint. Moving existing run setup and
background recording before the branch preserves text lifetime and passes
18 exact comparisons. Visible sharp shadows were absent from SVG/PDF; native
background images overlaid sharp shadows and source-order traversal put the
last shadow on top. Selected-font contour consumers, a shared reverse-shadow
visitor and native background/shadow/foreground order pass eighteen additional
comparisons, including independent overlapping glyph references for shadow
lists. The helper reuses the module's owned solid-shape recorder (**CI-18 /
D4.5.1v4**). The [CSS Text Decoration paint order](https://drafts.csswg.org/css-text-decor-3/#text-shadow-property)
places the shadow between background and text; no formal ruling changes.

Blurred shadows remain under investigation. An isolated borrowed filter is
consumed synchronously before its output image is deferred. Selected-font
raster lowering fixes the outline-versus-filter coverage discrepancy, and a
convolution-support helper accounts for all three passes. Raw trials still
preserve native and one export byte-edge failure; none is applied or promoted
as full blur support. Sharp verified sources are frozen separately.

### 2026-10-12 — Applied text shadows and filter device-length capture

The applied `text_effects37` increment keeps clipped backgrounds before shadows
and foreground text, visits each authored shadow in reverse order, and captures
each native shadow into a separate immutable glyph image. Unrelated backdrop
pixels are never blurred; different offsets/radii retain their own layers. SVG
and PDF consume selected-font contours for sharp shadows and selected-font
raster captures for blur. Capture bounds follow emitted hinted glyph bitmaps,
which can exceed outline bounds; this repairs a clipped SVG blur edge (**CI-18 /
D4.5.1v4**).

The new references exposed a shared density defect: ordinary native and export
CSS filter lengths were still interpreted as device pixels at 2×. Display-list
arena copies now convert blur/drop-shadow lengths without mutating computed
CSS. The capture helper belongs to display-list recording, preserving paint-only
test linkage. Full evaluator transfer belongs to the runner, keeping its JS and
native-stack dependencies out of standalone core fixtures while preserving the
existing handoff behavior (**D5.4.1 / D7.1.3**). The full test build and directory
fixture pass; no test stubs or assertions are weakened.

Focused main references pass 56/60 comparisons: all twenty PNG and twenty SVG
checks, with four exact 2× PDF byte edges retained as failures. Aligned extracted
PDF alpha masks for the basic blur case are identical; its reader comparison
still fails. Thirty-six live mutation/DPR assertions and 137 native vector cases
pass. The new ownership test replays after font/context, source text and PaintIR
retirement at both densities and checks temporary capture arenas are released.
No tolerance, golden or boundary allowance changes. Broader shaping, decoration,
color-font, fragment/ancestor geometry and transformed blur semantics remain open.

`ordered_37_anchor.json` protects 116 inputs and runtime
`e21b4ed26a27a16bc42fdc4a4650b427006924aa73ee9c72ae98dbef6d60ea7a`.
Fresh required gates and matching sanitizers are running as `text_effects37q_*`;
this is an applied increment awaiting qualification, not a new accepted aggregate
or P0–P2 closure. The protected `text_tiles36q_*` remains the accepted aggregate.

### 2026-10-12 — Qualified bounded shadows, density and stronger SVG checks

Protected `text_effects37q_*` passes fresh core 6,766 and Radiant 5,377 full /
353 partial /one skipped cases. Seven matching sanitizer runners pass 1,149
cases; twenty-two live fixtures pass 634 assertions per runtime and the Canvas
golden passes. The raw full sweep also exposed two representation-specific SVG
assertions: serialized shadow CSS and a CJK `dx` string. They are superseded by
independent default-font shadow references at both densities and actual CJK
foreground/shadow region checks in PNG and rasterized SVG. Existing native CJK
assertions are preserved through a shared helper; neither test weakens its
behavioral contract (**CI-18**).

The rebuilt `text_effects37r_*` full sweep passes 249/253 groups. The same
nineteen gradient-range comparisons and two ordered PDF edges remain, together
with four exact 2× blurred-shadow PDF comparisons. The stronger default-font/CJK
checks also pass against the sanitizer runtime. `make test-svg-export` passes
all 126 format checks and 2,466 pixel assertions, including relocated resources.
No tolerance, golden, skip or boundary allowance changes.

An additional Cairo probe passes 17/21 failed PDF pairs exactly, including all
four new blur pairs and both ordered-background pairs. Four marker/border range
pairs still differ by one byte. The original Splash failures are preserved;
the probe narrows the diagnosis without replacing their required comparisons.
All twenty native and twenty SVG shadow references pass at both densities;
selected-font default-font references and CJK pixel checks add real consumers
rather than crediting serialized CSS (**CI-18 / D4.5.1v4**).

116 reviewed normal inputs and 144 sanitizer inputs match protected binaries.
The reviewed change touches only the exporter test source; production code,
all sanitizer inputs, native tests and live fixtures are unchanged from the
fresh required gates. `text_effects37r_reused_runtime_gates.json` records this
reuse explicitly. The earlier checkpoint protects 123 records, 150 required
result copies, 1,352 preceding namespace exports and 724 shadow files; the
reviewed record additionally protects fourteen records and 1,260 export/probe
files. The 410-name inventory and no-int-cast lint pass. Boundary closure remains
185/48, with no additions or removals relative to checkpoint 36
(**D7.1.3 / D7.3.3–D7.3.4**). This qualifies a bounded consumer, not a text-family
or P0–P2 closeout; broader text/paint models and the remaining API debt stay open.

### 2026-10-12 — Gradient precision investigation and requested wrap-up

The accepted `text_effects37r_*` source/runtime checkpoint remains unchanged.
`range_color38/` is isolated and unapplied. Read-only inspection identifies
native lookup-table interpolation as the marker/border range byte discrepancy.
An exact-endpoint canonicalization trial repairs those aliases but introduces
cropped-background differences; it is rejected. An analytical native fill trial
removes the marker/border byte differences but retains the eleven range PDF
reader failures and introduces text coverage regressions. Its full-surface
capture also needs bounded allocation and performance work before promotion
(**CI-18 / D4.5.1v4**).

Both trials preserve source/runtime hashes, raw failures and logs. The analytical
trial ends with two passing /three failing groups; no tolerance, reference reader,
vendor source or main implementation is changed. `range_color38/unqualified_handoff.json`
and frozen sources record the remaining batch/clip/coverage investigation.
The user requested wrap-up; implementation work stops with the complete P0–P2
scope outstanding, not marked achieved. Resume from the accepted checkpoint
and this root-cause evidence rather than applying either draft.

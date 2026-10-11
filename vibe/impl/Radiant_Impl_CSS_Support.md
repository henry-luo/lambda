# Radiant CSS Support — Remaining Implementation Plan

**Status:** Execution paused at the user's wrap-up request. The latest bounded selected list/border image transform checkpoint is recorded in §9 (2026-10-11). The long-running scope covers every P0–P2 acceptance criterion; remaining implementation and qualification stay open.

**Predecessor:** [CSS selector/property implementation record](<Radiant_Impl_CSS_Selector_Property_Support (retired).md>), closed and retired with unfinished scope transferred here.

**Baseline:** [HTML, CSS and SVG Support](../../doc/HTML_CSS_SVG_Support.md), audited 2026-10-10; source checkpoint `eb75fe63b` plus the support-document update.

**Scope:** Remaining CSS selector, cascade, value, layout, paint, motion and interaction consumers, including their CSSOM and export surfaces. This is the active CSS implementation backlog.

**Authority:** An implementation plan, not a new semantics/design ruling. Ownership follows **D4.5.1v4 / D4.1.4v5**, property identity **D4.6.1v4**, native roots/context lifetime **D5.3.3 / D5.4.1**, and module boundaries **D7.3.3–D7.3.4** in [Lambda_Formal_Design.md](../../doc/Lambda_Formal_Design.md). No formal ruling defines this CSS priority order. New unresolved architectural choices require a working design record before implementation; this draft changes no ruling.

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

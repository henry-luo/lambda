# Retained computed style, sizing and layout

**Status:** milestones 1–3a, the independent milestone-3b consumers and shared
update/publication implemented and regression-qualified; general 3b and LLVM
ancestor-height fidelity remain open, 2026-10-21.  
**Design:** [CSS Cascade §3.47](../radiant/Radiant_CSS_Cascade.md#347-resolve-and-lay-out-once-for-unchanged-inputs),
[View Reuse VR15](../radiant/Radiant_Design_View_Reuse.md#vr15--computation-identity-follows-inputs-and-values-not-execution).  
**Ownership:** D4.5.1v4 / D4.1.4v5 / D4.5.2; no formal ruling changes.

## Milestones

| Milestone | Outcome and acceptance | State |
|---|---|---|
| 1. Stable style inputs | Repeating a parent's resolution for layout storage/context does not invalidate descendants when computed inputs are unchanged. Font, variables, real style edits and unsupported effects still invalidate correctly. | implemented and qualified |
| 2. Retained sizing queries | Repeated height requests and A→B→A constraints reuse results; changed width, percent bases, fonts, content/style versions and measurement policy miss correctly. | implemented and qualified for supported horizontal flow subtrees, including flex-item flow |
| 3a. Computed box boundary | Provisional inline/block roles and new prop allocation do not rerun the cascade. Clean computed geometry is immutable; dependent used values have separate storage. | implemented and qualified for eligible horizontal boxes |
| 3b. Retained flow results | Measurement consumes clean computed records; exact height requests retain layout results for final flow. | demand-driven row preparation, independent block/inline/nested-flex flow sharing and height-key normalization qualified; general query-owned results pending |
| 4. Shared update/publication | Correct retained anonymous-table/list mutations; reuse current script-produced geometry at presentation and skip unchanged geometry reads. | implemented; regression gates pass, LLVM ancestor-height fidelity open |

Milestone 1 uses stable input identity as a conservative migration step, not
complete computed-value equality. Milestone 2 must not claim reuse of line
fragments while the legacy estimate path still returns only a scalar height.
Keep such limitations explicit until the relevant consumer is migrated.

## Milestone 3a: computed box boundary

Implemented and qualified for eligible horizontal selected-view boxes.

- Eligible horizontal selected-view elements compute box/text properties before
  provisional view typing. Frozen typed geometry and normalized percentage
  expressions survive prop allocation; a separate used-box record is allocated
  only when dependent lengths need another context (**VR15**).
- Committed reuse validates computed inputs and containing constraints before
  preserving algorithm-assigned sizes. A mismatch restores clean CSS. Keeping
  a resolved bit alone formerly reused stale percentage samples.
- Supported intrinsic-height queries read clean preferred sizes. Percentage
  definiteness belongs to the actual flow/flex/page constraint: auto flex
  sizing differs from definite stretch/main sizing, ratio transfer and detached
  page-area sizing. Query keys include that ownership and policy even when
  only a descendant uses percentages.
- Cyclic intrinsic widths read computed parent storage before view typing.
  Ratio transfer converts border-box height once; functional fit-content maxima
  and stretch do not replace definite intrinsic contributions with used limits.
  Replaced-element height policy remains on its existing path.

Pool ownership remains **D4.5.1v4 / D4.1.4v5 / D4.5.2**. Secondary views,
table/replaced/form/SVG roles and unsupported effects retain their adapters.
Vertical flow still stores assigned inline constraints in mutable props; migrate
that consumer with retained flow results before extending the computed-box
boundary. Immutable font sharing, stable boxes/line fragments and loader
publication are not implemented by this phase.

Focused release checks pass: 168 core, 168 native text/layout, 501 paged-media
and all 776 sizing baseline cases; the layout integer-cast lint also passes.
The final release Radiant baseline passes with native macOS access: 6,036
passed, 143 existing partial outcomes and 2 skipped, zero unexpected failures
(6,181 total). It includes all 15 layout categories, the view/UI suites and WPT
input events. The native geometry fixture covers ten cases on initial layout
and retained reflow, independently confirmed with Chromium 152. Earlier failing experiments
remain under `temp/retained-style-phase3/`; no references or floors changed.

LLVM retains four document layouts. The full serialized comparison has 269
differences, all confined to the sidebar (255 geometry, 14 metadata); outside
that sidebar the trees are identical. Only one link changes height:
`Reference`, 2,160→32 px, matching Chromium. Relative to the qualified
milestone-2 release:

| Work across the page load | Before | After |
|---|---:|---:|
| Full CSS resolutions | 595,226 | 325,474 |
| Parent-key misses | 940 | 15 |
| Height requests | 1,819,286 | 1,900,731 |
| Height computations | 1,763,940 | 1,847,772 |
| Height cache hits | 55,346 | 52,959 |

Height computation increases by 4.8%; this phase does not solve repeated flow
or intrinsic estimation. Milestone 3b must replace those mutable estimate paths
with explicit constraints and retained completed layout results.

Evidence: `temp/retained-style-phase3/{core-final,sizing-final,lint-final}.log`,
`baseline-final-output/`, `geometry-{report,validation}.json`, and
`browser-check.json`.

Three interleaved, fresh-process release pairs use the same local LLVM assets.
Load times are 40.043/39.610/40.298 s before and 39.663/39.374/41.161 s after:
median **40.043→39.663 s (0.9%)**, with overlapping ranges. This establishes no
meaningful end-to-end speedup despite 45.3% fewer full CSS resolutions. Median
peak RSS is 795.6→794.1 MiB. Earlier profile captures used different host
conditions; only these paired trials support the timing comparison. Binary,
input and trial provenance: `temp/retained-style-phase3/report.json` and
`trials.json`. The remaining 1.85 million height computations and four document
layouts remain the priority for milestones 3b and 4.

## Milestone 3b, first consumer: demand-driven flex sizing and completed text flow

**VR15**: row-flex preparation must request width without a speculative subtree
height at max-content width. Height validity must be explicit so a subsequent
column query upgrades the entry instead of interpreting zero as a measurement.
At the assigned row-flex width, start sharing completed flow with final layout
for horizontal, auto-height items with one ordinary text child. Keep lines and baselines in their
selected-view storage (**D4.5.1v4 / D4.1.4v5 / D4.5.2**), and invalidate that
result on another trial layout or changed constraints/inputs. Complex flow and
percentage-dependent descendants remain on the existing path until their
complete query/result contract is migrated.

Acceptance: width-only preparation performs no height query; a later height
request upgrades the same cache entry; eligible exact cross-size flow is consumed
by final flow without rebuilding text rectangles; changed width, style, text,
font or measurement invalidates it. Verify initial and retained layout, wrapping,
padding, baseline alignment, stretched height and neighboring unsupported items.
Use core/native text checks during implementation, then sizing/flex baseline,
integer-cast lint and the Radiant aggregate once stable. Compare the complete
LLVM tree and three interleaved release timing pairs with milestone 3a.

Implemented: partial measurement entries upgrade in place, including retained
flex measurement memos. Row preparation no longer visits the subtree for height
at max-content width. A completed text-flow memo carries explicit content/style,
font resource, viewport and assigned-width inputs; final flow shares the view's
existing rectangles and baselines. The final stretched height is preserved.
Natural line contributions come from completed content, independent of stretch
in a definite-height container. Any actual content pass invalidates both flex
retention paths before rewriting geometry, including an intervening final pass
at another width; trial-layout scopes also invalidate their ancestors. An
A→B→A regression ensures an old A memo cannot point to B's live rectangles.
Incomplete budget-limited traversal cannot
produce a reusable result. Reset/retirement releases the pool-owned memo.

This consumer retains one live fragment result per item. General mixed/block
flow, dependent descendants and multiple independently retained fragment queries
still require the general `LayoutResult` migration. It does not remove the four
LLVM document layouts or the remaining scalar estimator.

The initial sizing run exposed `stretch/flex-line-001`: taking the trial's used
box height as its natural contribution collapsed wrapped stretched lines.
The repair measures completed content plus decoration, with initial/retained
native coverage and an independent Chromium 152 fixture check. Earlier failed
results remain in `temp/retained-flow-phase3b/`; no baseline/reference changed.

The aggregate UI gate also caught generated flex boxes entering the text-only
consumer: probing/replaying `::before`/`::after` consumed quotes outside source
order (`A1Z` became `AAZ`). Ordinary elements owning pseudos and generated boxes
are distinct roles. The shared `layout_generated_pseudo` helper excludes the
latter until a result can carry ordered counter/quote effects. Initial/retained
native coverage checks the contained quote, alongside the existing 36-assertion
style-containment UI fixture. HTML list containers also have implicit counter
operations without authored counter props; the existing list-container helper
keeps those boxes on the same ordered path. The failed aggregate is preserved as
`baseline-generated-failure*`; earlier timing provenance is under
`pre-generated-benchmark/`.

Focused release verification passes 169 core tests, 169 native text/layout tests,
all 776 sizing cases and the layout integer-cast lint. The seven fixture cases
match Chromium 152. The final full serialized LLVM tree is identical to the
qualified milestone-3a tree. Work across its four document layouts is:

| Work | Milestone 3a | First 3b consumer |
|---|---:|---:|
| Height requests | 1,900,731 | 1,257,526 |
| Height computations | 1,847,772 | 1,208,921 |
| Height cache hits | 52,959 | 48,605 |
| Full CSS resolutions | 325,474 | 323,306 |
| Parent-key misses | 15 | 7 |

Height computations decrease **34.6%**, primarily by removing the unused
preparation query. A hit alone does not describe the removed subtree work.
The exact text-flow path is exercised by LLVM; general mixed subtree flow
continues to use the estimator. LLVM measures and consumes 12 completed text
flows across the four layouts. Geometry/profile captures overlapped builds and
are work/correctness evidence only; isolated paired trials establish load time.
Evidence: `temp/retained-flow-phase3b/{core-final,text-final,sizing-final,lint-final}.log`,
`geometry-report.json` and `browser-check.json`.

Three interleaved fresh-process release pairs use the same local assets,
profiling disabled and no concurrent builds/tests:

| Metric | Milestone 3a | First 3b consumer |
|---|---:|---:|
| Load time, trials (s) | 40.312 / 39.622 / 39.663 | 36.520 / 35.744 / 35.299 |
| Median load time (s) | 39.663 | 35.744 |
| Median peak RSS (MiB) | 789.1 | 767.0 |

Median load time decreases **9.9%** and median peak RSS decreases **22.1 MiB**.
All six loads render successfully. These paired trials, rather than timings
from earlier host conditions, support the comparison. Provenance and raw
phases: `temp/retained-flow-phase3b/{report,trials}.json` and the benchmark logs.
Browser-like loading speed and general once-only subtree flow remain pending.

The final release Radiant aggregate passes with native macOS access: **6,037
passed, 143 existing partial outcomes, 2 skipped, zero unexpected failures**
(6,182 total). It includes all 15 layout categories, all 659 UI automation
cases, the 36-assertion containment regression, view/DOM interaction and all
32 WPT input-event files. The integer-cast lint also passes. Evidence:
`temp/retained-flow-phase3b/baseline-final.log` and `baseline-final-output/`.
No test references, assertions or baseline floors were weakened.

This closes the bounded first consumer by behavior: row preparation avoids
unused height traversal, exact ordinary text flow supplies final fragments,
and changed constraints or context effects cannot consume stale geometry.
Next: general mixed/block-flow results with independently owned fragments for
distinct queries, then milestone 4's retained mutation and shared publication.

## Qualified 3b extension: ordinary block/inline subtrees

**VR15:** extend completed row-flex flow from one text child to ordinary
horizontal block/inline descendants. Reuse the existing context-independence
predicate; validate every descendant's computed inputs and reject ordered
generated effects, dependent lengths, resources and other formatting contexts.
The selected view still owns one live result (**D4.5.1v4 / D4.1.4v5 / D4.5.2**);
independent fragment storage for A→B→A queries remains a subsequent milestone.

Acceptance: nested inline wrapping and block stacking share their completed
geometry with final stretch; initial and retained layouts agree; descendant
style/text edits and intervening measurement invalidate the result. Percentage,
pseudo/counter and resource descendants stay on their existing path. Use focused
native cases and sizing/flex checks first, then the Radiant baseline and layout
lint. Compare the complete local LLVM tree and three isolated release pairs
against the qualified first 3b consumer.

Implemented: the row-flex cross-size consumer uses the same completed-flow pass
for ordinary block/inline descendants. The shared context-independence predicate
is promoted from intrinsic sizing, and preflight validates each descendant's
computed inputs, effects and remaining traversal budget. Final flow retains
child placements, margin state, line fragments and baselines in place; it does
not rerun those children. Revalidate after flow before publishing a complete
memo. The function is now `flex_measure_completed_flow`.

The height-query key also drops the caller's available-space label when the
existing whole-subtree proof establishes context independence. The estimator's
ordinary-flow path consumes the explicit wrapping width; a min/max-content
label or unrelated definite space does not add a new input. Actual width,
versions, font and content policy remain keyed. A percentage-dependent
descendant propagates dependence and preserves the full policy key. Completed
flow still keys available space explicitly because atomic inline shrink-to-fit
reads it; scalar estimation and completed layout have different consumers.

The new text-mutation regression exposed a pre-existing cycle in
`dom_text_set_content`: Mark child relinking preserved the old text wrapper
beside the new wrapper; the subsequent identity repair made its sibling link
point to itself. Character edits now reuse `dom_text_replace_backed_string`
and document-owned payloads, preserving node links (**D4.5.1v4 / D4.5.2**).
Text and style changes share ancestor intrinsic-input invalidation; text edits
mark containing geometry dirty without forcing CSS resolution. Initial/retained
native coverage checks wrapping, margin collapse, final stretch, descendant
style/text edits, fragment identity, trial invalidation and conservative fallbacks.
Trial scopes invalidate contained flow memos as well as enclosing ones: their
scalar snapshots do not restore text fragments in a nested item. Native cases
exercise both directions. The stalled process sample and failed experiment remain under
`temp/retained-flow-subtrees/`.

Independent Chromium 152 checking also exposed an existing adjacent atomic-inline
break defect: a 40px atomic box followed by 40px text in a 60px parent overflows
instead of moving the text to the next line (Radiant height 40px, Chromium
60px). Both frozen releases produce identical output. Keep this line-breaking
repair outside the retained-flow milestone; the native atomic-inline case
checks unchanged fragments/placements across replay, without blessing the
incorrect height as a reference. The padded flex-basis width difference
(Radiant 60px, Chromium 70px) is also pre-existing. Browser parity for these
cases remains partial. Evidence: `atomic-{before,after}.json`, `browser-check.json`.

Focused release verification passes 170 core, 170 native text/layout, all 917
CSS/input cases and 40 DOM-node cases; the layout integer-cast lint passes.
The full LLVM tree is identical to the qualified first-3b consumer. Across the
same four document layouts:

| Work | First 3b consumer | Subtree extension |
|---|---:|---:|
| Height requests | 1,257,526 | 1,137,105 |
| Height computations | 1,208,921 | 1,085,928 |
| Height hits | 48,605 | 51,177 |
| Full CSS resolutions | 323,306 | 323,306 |
| Parent-key misses | 7 | 7 |

Computations fall **10.2%**. The first subtree-only prototype saved only eight
height computations on LLVM; removing irrelevant caller-space distinctions
accounts for nearly all the work reduction. General effects/table/resource
flow and the four loader layouts remain. Profile captures overlap builds and
provide correctness/work counts, not load-time evidence. Evidence:
`temp/retained-flow-subtrees/{geometry-report,core-final,text-final,css-final,dom-node-final,lint-final}.*`.

The full release Radiant baseline passes: **6,038 passed, 143 existing partial
outcomes, 2 skipped, zero unexpected failures** (6,183 total). This includes all
3,294 layout cases, 659 UI automation cases and 32 WPT input-event files. No
baseline references or floors changed. Evidence: `baseline-final.log` and
`baseline-final-output/` in the same directory.

Three fresh-process, interleaved release pairs run without concurrent builds or
tests, using the same local LLVM assets and profiling disabled:

| Metric | First 3b consumer | Subtree extension |
|---|---:|---:|
| Load time, trials (s) | 35.411 / 34.445 / 34.575 | 34.781 / 34.389 / 33.465 |
| Median load time (s) | 34.575 | 34.389 |
| Median peak RSS (MiB) | 770.8 | 763.3 |

Load time is effectively flat: **0.5%** lower median with overlapping ranges.
Median peak RSS falls **7.5 MiB**. All six loads render successfully; hashes,
raw resource data and stage timings are in `report.json`, `trials.json` and
the benchmark logs. Reduced height-call counts do not establish browser-like
loading speed. The next performance milestone is eliminating repeated full
document flow, beginning with retained anonymous-table/list normalization and
shared update/publication. General query-owned fragments and ordered generated
effects remain pending; one live subtree memo cannot satisfy multiple active
fragment queries or make scalar probes preserve used boundary state.

## Milestone 4: shared update/publication

**VR15:** one continuous-document checkpoint validates published geometry for
CSSOM reads, task checkpoints and presentation. The selected ViewTree owns the
snapshot (**D4.5.1v4 / D4.1.4v5 / D4.5.2**). Loader root publication must not
discard an already current script-produced tree. A real reset restores authored
table runs and first-letter source text before resetting their generated props;
list/pseudo boxes must be regenerated rather than inheriting reset used state.

Acceptance: repeated unchanged reads and final presentation add no document
flow and preserve text/box identity; text/style mutations, viewport/font changes
and explicit reflow requests invalidate; table/list/pseudo changes remain correct
after multiple resets. Publish consumed inputs before observer delivery so later
writes cannot be mistaken for completed geometry. Keep paged/secondary editions
and explicit custom-layout lifecycle passes on their existing flow entry points.

Implemented: `layout_update_document` is shared by CSSOM,
task/load checkpoints, window/event reflow and continuous presentation/export.
A selected-tree publication compares exact input/environment fields; a general
state-version counter was rejected because lifecycle/selection bookkeeping does
not establish changed CSS. The loader no longer destroys script geometry.
Actual full reset restores authored table runs/first-letter text before resetting
props, and detaches materialized generated content for refresh by retained
owners. Completed flow consumes queued reflow work before publishing and sampling
observers; later invalidations remain pending. Paint-only/detached commits may
advance consumed versions only when the environment still matches.

Retaining the tree exposed two mutable sizing defects. Column contribution
queries initialized child boxes in place while leaving their committed fragments
alive; the existing measurement scope now restores those boxes. Flex item
`has_intrinsic_*` fields were pass-local work reused after final layout had
refined their values. Item initialization now expires that work; measurement
scopes preserve the work they temporarily replace. Existing computed/used
validation supplies styles, and incremental updates retain clean sibling results.
Legacy scalar cache entries expire at the measurement generation, independently
of retained computed-style ownership (**VR15**).

Ordinary invalidation retains the existing view-pool epoch; an actual replacement
still normalizes generated structure before resetting props. Removing a separator
merges all anonymous table runs, including row groups and block parents. Generated
list-item sizing reads resolved display; outside marker bounds constrain an
anonymous cell by a maximum instead of an inline advance. Scroll writes use
published geometry availability rather than loading lifecycle bookkeeping;
presentation settles scroll-dependent sticky offsets without repeating flow.
An intrinsic query can see a native control's provisional UA box before its
cascade. Its winning `box-sizing:content-box` declaration must override that
provisional border-box when deriving the min/max contribution.

String `append`/`prepend` previously linked text directly and issued a target-less
notification, leaving its containing flow clean. Both now use the existing
backing-aware text insertion helper and its targeted mutation notification;
appending after a geometry read invalidates retained fragments (**VR15**).

Focused release checks pass: 176 native text/layout and 170 core cases. New
coverage verifies unchanged read/presentation identity, text/style/viewport/font
invalidation, explicit and scheduled reflow, repeated table/list/pseudo reset,
SVG pixels, contribution-query isolation, scalar-cache expiration, merged
cell/row-group runs, script scroll/sticky placement and native control sizing
after behavior initialization, plus text insertion after a geometry read. The
initial EPUB transform assertion now expects
the committed CSSOM `matrix(...)` instead of raw `scale(...)` declaration syntax;
all six viewport/resize assertions pass. Chromium 148 independently confirms the
same matrix and visual width in `browser-mutations.json`.

The final release Radiant baseline passes: **6,044 passed, 143 existing partial
outcomes, 2 skipped, zero unexpected failures** (6,189 total). All 3,294 layout
cases clear their recorded floors; all 659 UI and 32 WPT input-event cases pass.
The layout integer-cast lint and `git diff --check` pass. Earlier failed gates
remain preserved; no layout reference, comparator or pass floor was weakened.
Evidence: `baseline-text-fixed.log`, `baseline-text-fixed-output/`,
`text-insertion-{native,core}.log` and `lint-text-fixed.log`.

LLVM's full comparison remains **nonidentical**: 42,951 `normal`→`400` font-weight
aliases, five computed `given_height` metadata changes and five ancestor height
changes, 312,511.7→315,655.7 px. Eight more geometry fields belong to the header
edit button: its width changes 20.2→16 px, matching Chromium; surrounding icon
placement changes with it. All remaining fields and text fragments match.
Chromium 148 gives those ancestors 443,138.75 px at the
same viewport; both Radiant versions underestimate the completed content.
The exact cause of the remaining ancestor-height delta is not isolated, and
general flex flow/estimator migration remains open. Keep this qualification
limit separate from passing native tests; no comparator, reference or floor
was weakened. Evidence: `geometry-{report,classification}.json` and
`browser-llvm.json` under `temp/shared-publication/`.

| Work across the LLVM load | Previous qualified release | Shared publication |
|---|---:|---:|
| Document layouts | 4 | 3 |
| Full CSS resolutions | 323,306 | 239,073 |
| Parent-key misses | 7 | 4 |
| Height requests | 1,137,105 | 855,309 |
| Height computations | 1,085,928 | 817,930 |
| Height cache hits | 51,177 | 37,379 |

Height computations decrease **24.7%**. Profile capture times are work evidence,
not an isolated load-time comparison. The failed aggregates and rejected
experiments remain under `temp/shared-publication/`.

Three isolated, interleaved fresh-process release pairs use the same local LLVM
assets. Load times are 34.541/33.686/34.263 s before and
25.495/25.881/26.408 s after: median **34.263→25.881 s (24.5% faster)**.
Median peak RSS falls **770.9→761.9 MiB**. All six processes exit successfully
and complete rendering. The comparator still reports the geometry differences
above; these timing gains do not establish browser-equivalent ancestor sizing.
Binary/input/source provenance and per-trial timing/RSS are in `report.json`,
`trials.json`, `benchmark-source.patch` and `benchmark-*.timing.tsv`.

Verification budget: focused native checkpoint/mutation/reset cases and existing
text/core tests, then the Radiant baseline and layout integer-cast lint. Compare
LLVM's complete serialized geometry, work counts and three isolated interleaved
release timing pairs with the previous qualified binary. Artifacts live in
`temp/shared-publication/`; do not compare overlapping build/profile wall times.

## Milestone 3b continuation: natural-size queries for nested flex

**VR15:** measure an auto-height nested flex item by real flow at its assigned
inline width, before final cross-axis stretch. A natural-size query owns an
explicit root constraint; horizontal flex growth and provisional cross stretch
cannot turn that root's estimated height into a fixed input. Preserve the normal
final constraint policy, including cyclic percentages and definite stretch.
Selected-view results keep their existing ownership (**D4.5.1v4 / D4.5.2**).

The minimal reproducer has two 20px line boxes containing 10px glyphs. Chromium
gives its column and row 40px; the scalar estimate leaves both at 20px while
children extend to 40px. A nested page/main/content version likewise leaves its
outer ancestors at 40px while content reaches 60px. LLVM has the same structural
mismatch: a 443,059.5px article container inside 315,655.7px flex ancestors.

Scope: independent horizontal flex/block/inline subtrees without dependent box
lengths, ordered generated effects, resources, percentage gaps/bases, stretch
bases or other unmigrated formatting contexts. Retain the natural result for final flow only when the nested flex
block constraint also matches; stretch is a distinct query when it changes
descendant flex placement. General query-owned fragments and ordered effects
remain separate work.

Acceptance: correct 40px/60px nested heights on initial and retained layout;
definite stretch keeps its allocated box and relays out dependent nested flex;
matching constraints preserve fragments; width/content edits invalidate;
existing cyclic percentage controls remain 36px/28px. Verification: focused
native text/core cases and Chromium references, then Radiant baseline and layout
lint. Refresh the LLVM full-tree/work comparison; use three isolated release
pairs for any load-time claim. Artifacts: `temp/retained-flow-columns/`.

Implemented: shared box-length dependency checks admit nested flex while keeping
each formatting algorithm's additional constraints explicit. Natural flow owns
its root constraint; final nested-flex reuse validates the measured block size.
The native regression verifies fragment identity, redistribution after stretch,
retained layout and width/text invalidation, plus dependent-gap/basis fallbacks.
Chromium 148 independently confirms the fixture and cyclic-percentage controls.
All 178 native text/layout and 170 core cases, layout lint and diff checks pass.
The full release Radiant baseline passes: **6,086 passed, 143 existing partial
outcomes, 2 skipped, zero unexpected failures** (6,231 total), including all
3,294 layout floors, 662 UI cases and 32 WPT input-event files. No baseline
reference, comparator or pass floor changed. Evidence: `baseline.log` and
`baseline-output/` in the same artifact directory.

LLVM's complete serialized layout tree is identical to the predecessor. Document
layouts remain three, full CSS resolutions 239,073 and parent-key misses four.
Height requests change 855,309→855,303 and computations 817,930→817,928. This
does not materially reduce LLVM work; no load-time improvement is claimed and
no timing sweep was run. Its generated/effectful and dependent formatting paths,
including the known ancestor-height mismatch, remain open. Full comparison and
binary/input/source provenance: `geometry-report.json`, `manifest.json` and
`source.patch`; native/lint evidence: `*-qualified.log`.

## Verification budget

Use `test_radiant_core_gtest` for focused style/sizing invariants, then relevant
text/layout and mutation cases. Run Radiant baseline at the integrated milestone
boundary and the required layout integer-cast lint. Preserve failures and
goldens; classify environmental failures separately from engine regressions.

Use the existing local LLVM assets and release profiling. Compare query counts
with actual computations and misses; separate duplicate work from genuine new
constraints/script updates. Performance claims require three comparable trials
per variant and binary/input provenance. Do not overwrite a running consumer's
binary.

## Qualified milestone 1–2 checkpoint

Style input checks are shared by reuse and revision recording. Layout storage
reconstruction keeps a valid input revision; parent identity is also keyed so
reparenting cannot confuse equal revision numbers. Unsupported styles retain
conservative invalidation.

Height queries retain distinct constraints in selected-view pool entries.
The key includes a direct flex parent's actual percentage basis, which can
change independently of the ambient context during sizing.
Global mutation/style/font versions expire stale entries; explicit dirty
notifications advance a subtree input revision up the ancestor chain, including
descendant changes before a DOM commit. A document-wide dirty counter was
rejected after LLVM profiling: anonymous node initialization discarded unrelated
subtree queries. The same notification clears ancestor intrinsic-width caches.
Unsupported descendants propagate non-cacheability.
Pool reset/retirement releases the retained entries. The legacy scalar estimate
remains: this milestone does not retain final line fragments or remove flow.
Direct text queries remain uncached; containing flow queries can reuse their
combined result. Flex containers, grid, tables, replaced resources, multicolumn
and unsupported computed styles keep the conservative path. In particular,
legacy flex measurement reads descendant used widths: caching its ancestors
would require tracking those mutable inputs. Propagate that restriction until
milestone 3 replaces those reads with explicit constraints.

`LAYOUT_PROFILE` separates height requests, hits, computations and stores.
Version misses count discarded nonempty query sets. Key-input counters count
fields that differ from every retained candidate; they overlap and are not an
exhaustive partition of misses.

Focused verification passes 160 core and 917 CSS/input cases, plus the layout
integer-cast lint. The final Radiant baseline passes with native macOS access:
6,035 passed, 143 partial, 2 skipped, zero failures; this includes all 167
text/layout cases. Native access is necessary here: the sandboxed run stalled
in graphics initialization and the video decoder crashed in Apple's XPC thread,
while both controls and the full native-access baseline pass.
Evidence: `temp/retained-layout/{core-final,css-input-final,lint-final,radiant-baseline-final}.log`.
The full serialized LLVM layout tree is identical before and after the combined
change, using the same local assets and release binaries. Diagnostic counts:

| Work across the page load | Before | After |
|---|---:|---:|
| Document layouts | 4 | 4 |
| Full CSS resolutions | 1,178,513 | 595,226 |
| Parent-key misses | 828,676 | 940 |
| Height requests | 2,573,534 | 1,819,286 |
| Actual height computations | 2,573,534 | 1,763,940 |
| Retained height hits | 0 | 55,346 |

A hit avoids descending into the subtree, so the reduction in computations
exceeds the direct hit count. Evidence:
`temp/retained-layout/geometry-report.json` and `geometry-{before,after}.lambda.log`.
Three interleaved release trials (`view --headless`, profiling disabled, native
macOS access, identical offline assets) measure:

| Metric | Before | After |
|---|---:|---:|
| Total load time, trials (s) | 132.028 / 131.097 / 129.603 | 111.221 / 112.597 / 111.436 |
| Median load time (s) | 131.097 | 111.436 |
| Median peak resident memory (MiB) | 733.4 | 797.8 |

Median load time decreases **15.0%**; retained queries cost **64.4 MiB** of
additional median peak resident memory on this fixture. All six loads render
successfully. Binary/input hashes, trial phases and raw resource measurements:
`temp/retained-layout/{report,trials}.json` and `benchmark-{before,after}-*.output.log`.

The remaining full CSS work includes 264,614 storage misses; reason counts are
first-failing checks, so resolving the parent-key defect exposes storage misses
that were already present. Different sizing constraints and repeated full
document updates still cause substantial measurement work. The next architectural
step is milestone 3: computed records independent of mutable view props, followed
by a completed flow result that both size-only and final-flow consumers can use.
Milestone 4 must repair retained anonymous table/list normalization before
removing the loader's geometry rebuild. Browser-like loading speed and the
complete once-only contract are not achieved by milestones 1–2.

The previously measured release median was 47.319 s with four document layouts,
2,573,534 recursive height calls and 1,178,513 full CSS resolutions, including
828,676 parent-key misses. These are historical measurements, not a baseline
measurement of the current binary.

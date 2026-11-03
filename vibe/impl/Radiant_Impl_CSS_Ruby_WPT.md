# CSS Ruby WPT implementation

Status: active. Started 2026-11-02 on `codex/css-ruby-wpt`, source base
`c5e40c521`. The imported suite contains 212 documents. The initial worktree
run reports 19 full matches, 193 geometry mismatches, and no execution errors.
Computed roles, HTML construction, shared columns and annotation hiding are
qualified with existing aggregate failures documented below. Annotation-break
normalization, inline font boxes, Ruby spacing, nested envelopes and non-inline
Ruby formatting and isolated-inline margin advances are also qualified. General flow
integration and the remaining raw suite failures are still open. Coordinated
horizontal flow is qualified for source-text fragments, complete shared columns
and neighboring whole atomic units. Spanning/missing columns and vertical/RTL
source fragmentation remain open. Completed-line trailing spaces and bounded
Ruby column reconciliation are qualified; hidden-annotation CSSOM exposure and
float exclusion for internally split Ruby remain open. Whole unbroken Ruby
float bands, relative-offset composition and block-axis annotation margin
envelopes are qualified. Inline margin contributions and source border-box
publication are also qualified. The objective is not complete.
Semantic line-over/under mapping and internal block-start padding are qualified
for ordinary Ruby across horizontal and both vertical writing modes.
First-line position inheritance is qualified for selected horizontal source
fragments, including retained line membership changes.
Ruby column completion also retains newly introduced leading base-float
exclusions; annotation-emitted float ownership remains partial.

## Objective and contracts

Repair the failures exposed by `make layout suite=wpt-css-ruby` through shared
CSS implementations. Preserve upstream fixtures, browser references,
comparison tolerances, and failure reporting. Scores are evidence, not an
accepted baseline.

[CSS Ruby 1](https://drafts.csswg.org/css-ruby-1/) governs box normalization,
pairing, placement, whitespace, line breaking, and formatting properties.
[CSS Display 3](https://www.w3.org/TR/css-display-3/) governs computed display,
blockification, and inlinification. Existing Lambda contracts remain unchanged:
**D2.10.1v2** for XHTML admission, **D4.1.4v5 / D4.5.1v4** for document and
layout-scratch ownership, **D4.5.2** for retained value ownership, and
**D5.3.3 / D5.4.1** for script roots and evaluator lifetime.

## Initial source findings

`layout_inline.cpp` currently identifies annotations by the `rt` tag, lays out
all bases as one run, and centers each annotation over that whole run. The
intrinsic-width probe repeats the tag classification. This loses computed
display roles and the segment/column pairing specified in CSS Ruby 1
sections 2.1–2.3. The current simple-pair geometry is a bounded implementation,
not a model that can be extended with per-fixture conditions.

The raw Chromium references also expose browser limitations: in
`rbc-rtc-basic-001`, authored `ruby-base-container` and `ruby-text-container`
compute to `inline`. In `ruby-annotation-pairing-001`, the source explicitly
records a Chromium pairing failure. Audit these independently against the
specification; an engine must not copy an incorrect browser result to improve
the raw score.

## Delivery sequence

1. **Execution and inventory (P0 prerequisite).** Build and run the suite in
   this worktree. Classify load/crash/readiness failures separately from
   computed-value, intrinsic-size, and placement disagreements. Isolate one
   document before assigning the cause of a batch failure.
2. **Computed roles and line metrics (P1).** Correct inlinification and
   blockification before layout, classify annotations by computed display, and
   keep annotation line-height out of the base metrics. Verify direct/nested
   markup and retained display changes independently of browser limitations.
3. **HTML ruby tree construction (P1 prerequisite).** Apply the HTML in-body
   start-tag rules for omitted `rb`/`rt`/`rp`/`rtc` end tags. Validate sibling
   order, annotation-container preservation, and scope boundaries before
   extending layout pairing.
4. **Ruby structure and horizontal columns (P1).** Use computed display roles
   and a shared normalized segment/level/column model for measurement and
   placement. Represent anonymous structure in layout scratch without changing
   source DOM parent/sibling links. Cover successive base/annotation pairs,
   annotation-only columns, nested inline bases, and arbitrary elements with
   ruby display roles. Reuse existing flow, text metrics, and view-tree shifts.
5. **Formatting and whitespace (P1).** Apply inherited ruby alignment and
   positioning, annotation hiding, and the normalization whitespace rules to
   the same model. Keep source text/DOM identity and paint geometry coherent.
6. **Flow integration (P1).** Address intrinsic contributions, line breaking,
   overhang, non-inline containers, positioning, bidi, and vertical axes in
   finite groups selected from the observed failures.
7. **Qualification.** Run the complete Ruby suite and required Radiant gates
   at stable milestone boundaries. Record spec/browser disagreements and
   unresolved feature families explicitly.

## Verification budget

Use a focused original WPT case and adjacent variants during an edit loop.
Add native assertions when they independently test pairing, geometry, or
retained reflow that raw Chromium cannot establish. At a layout milestone
boundary, run the dimension-cast lint and the Radiant baseline once; repeat
only for relevant new changes or unresolved failures. Keep builds separate
from running consumers. Use the worktree's own executable and source, and
store diagnostics under its `temp/` directory.

## M1: computed roles and annotation metrics

Confirmed causes:

- The base line merges an annotation's independent line metrics, then adds its
  inherited line-height to the base line a second time. A `64px/5` WPT base
  becomes 480px high instead of retaining its 320px line. CSS Ruby 1 §3.3
  excludes annotation line-height; §3.6 uses available leading. Replace the
  independent-line merge with the actual annotation-box contribution on its
  annotated side. Use a stable base baseline across every annotation so a
  preceding annotation cannot be counted again.
- Display fix-up checks only a directly-parenting internal ruby box, omitting
  the ruby container and recursive inline descendants. CSS Display 4
  [§2.7](https://drafts.csswg.org/css-display-4/#inlinify) passes inlinification
  through inline flow boxes and stops at atomic/independent contexts.
  Preserve inner flex/grid/table/ruby roles and list-item identity. Floats and
  positioned internal ruby boxes blockify to ordinary block flow.
- Layout and intrinsic lookahead identify annotations by the `rt` tag.
  Centralize computed-display classification and use it for placement,
  bounding-box exclusion, final alignment, and lookahead. Author changes from
  ruby-text to inline must remove annotation behavior on retained reflow;
  arbitrary elements with ruby-text must acquire it.

Acceptance checks:

- `RubyAnnotationsUseExistingLeadingWithoutTheirOwnLineHeight`: direct and
  nested bases, over/under annotations, inherited/zero/large annotation
  line-height, an ordinary-inline control, and retained reflow. Passes after
  the line-metric repair.
- `RubyDisplayFixupRecursesThroughInlineBoxesAndStopsAtFormattingContexts`:
  direct/recursive/contents/list-item cases, atomic/flex/grid boundaries,
  hidden/content boxes, out-of-flow internal roles, and live ancestor changes.
  Passes, including the final computed-role extension.
- `RubyFormattingFollowsComputedRolesAcrossRetainedDisplayChanges`: native
  versus arbitrary-element markup, an `rt` explicitly made inline, and live
  annotation-role removal/restoration. Passes.

The raw suite remains 19/212 after the metric-only change and reaches 20/212
after computed-role fixes. `ruby-justification-001` becomes a full match, with
no previously full cases lost. Recursive inlinification improves from 0% to
44.4% element matches, while separate normalization disagreements remain. Logs:
`temp/ruby-initial.log`, `temp/ruby-after-leading.log`,
`temp/ruby-after-computed-roles.log`, `temp/ruby-native-roles.log`.

The first Radiant gate is interrupted after denied macOS window-service
connections hang native graphics tests. Its layout component reports four
related regressions: three initial-letter/ruby cases and
`multicol-fill-balance-021`. Focused diagnosis finds that the initial's measured
annotation displacement must also affect its origin-line advance, subtracting
the raised-origin displacement already applied. Atomic annotation children
also retain their actual extent even when overflowing the annotation's font
box; their extent is independent of annotation line-height. The four focused
cases pass after these repairs. The gate also records a browser-global-profile
UI failure whose attribution is pending. Rerun the aggregate with native
graphics access after the parser prerequisite and focused checks are stable.
No test tolerance, upstream fixture, capture, or accepted score was changed.

The native-graphics rerun completes all 29 components. Every layout baseline
category passes with no recorded regressions (4973 full matches and 301
accepted partials), and all 441 text-query tests pass. The aggregate remains
red: `dom_browser_global_profile` fails two assertions, and seven slide UI
fixtures abort after their assertions pass. A separately rebuilt, unchanged
`c5e40c521` host reproduces all eight failures from this checkout's cwd. Six
slide failures reproduce in the initial seven-case control; reorder reproduces
on an isolated rerun with both hosts. Logs: `temp/ruby-m1m2-radiant-baseline.log`,
`temp/ruby-head-dom-control.log`, `temp/ruby-head-slide-control.log`,
`temp/ruby-head-reorder-control.log`, and `temp/ruby-current-reorder-control.log`.
These unrelated existing failures remain visible; no baseline exemption is
added. All edited sources are restored and the current host rebuilt after the
control experiment.

## M2: HTML ruby start-tag construction

`ruby-align-002a` exposes an earlier disagreement: `<rt>` remains inside an
implicitly closed `<rb>`, so styling/layout receive the wrong parent and child
sequence. The HTML tree builder already has ruby tags in its implied-end-tag
class, but has no dedicated in-body start-tag branch to use that class.

Follow the HTML Standard's
[in-body rules](https://html.spec.whatwg.org/multipage/parsing.html#parsing-main-inbody):
`rb`/`rtc` generate all implied end tags when ruby is in scope; `rp`/`rt` do so
except for `rtc`. Unexpected non-implied current nodes produce a parse error
without an extra stack pop. All four tags share the insertion path and existing
scope/closing helpers. XHTML admission remains governed by **D2.10.1v2** and
does not use these HTML recovery rules.

Acceptance: omitted-end-tag sibling ordering, sequential annotation containers,
uppercase HTML tokens, preservation of a non-implied formatting parent,
ordinary tags outside ruby scope, and an object scope boundary. Both new HTML
parser tests pass. The original Ruby suite remains 20 full matches and 192
mismatches, with no execution errors. Qualification also includes input parser
gates and the Radiant gate.

The input baseline passes all 2112 checks: HTML5 WPT parser 364, CommonMark
1343, YAML 391, ASCII Math 3, and LaTeX Math 11. The pinned YAML submodule is
initialized in the worktree, and tracked corpus links resolve through the
worktree's companion-corpus link. Log: `temp/ruby-m2-input-baseline.log`.

The full input run executes 4621 checks but is not green. Its sandboxed PDF
component times out in four native WebGL cases, also marking unexecuted cases
in those batches failed. A bounded rerun of that component with native graphics
access completes all 470 cases: 465 pass and five fail; all four WebGL cases
pass. The remaining five concern inside-image list markers, a background image,
RTL line clamping, Ghent PDF import, and a tide-table PDF round trip. The ten
wiki-formatting failures and twelve script-format prose failures also remain.

Separately rebuilt unchanged `c5e40c521` native runners reproduce the same five
PDF failures and ten wiki failures; the unchanged host reproduces all twelve
prose failures. The combined component evidence therefore leaves 27 existing
input failures, with no new failure attributed to these Ruby repairs. This is
not a claim that the aggregate gate is green. Evidence:
`temp/ruby-m2-input-full.log`, `temp/ruby-m2-input-summary.json`,
`temp/ruby-m2-pdf-native-graphics.log`, `temp/ruby-m2-pdf-native-summary.json`,
`temp/ruby-head-pdf-control.log`, `temp/ruby-head-wiki-format.log`,
`temp/ruby-head-wiki-modular.log`, and `temp/ruby-head-script-formats.json`.
The edited sources are restored, and the current host/native runners rebuilt
after the control experiment.

## M3: normalized segment and column model

Priority: P1. Ordinary multi-pair ruby currently centers each annotation over
all bases. This breaks the common `<ruby>base<rt>reading</rt>base<rt>reading</rt>`
case, not just complex markup.

Build transient virtual structure from computed display roles, following CSS
Ruby 1 §2.2–§2.3. A segment owns one base level and its following annotation
levels. Each level owns explicit boxes or anonymous source-node ranges; no
source node is reparented. Consecutive annotations form one level, consecutive
annotation containers form separate levels, and a new base level starts the
next segment. Missing bases or annotations become empty virtual boxes. A
single anonymous annotation within an explicit annotation container spans its
segment; a single explicit annotation pairs only with the first base.

Use this structure for both intrinsic measurement and used placement. Size
each column from all participating boxes; distribute spanning excess across
its columns per §3.1.1. Retain the established base inline formatting context
and separate annotation contexts. Reuse the existing view-tree shifting and
inline bounding-box functions. Normalization lifetime is bounded by layout
scratch (**D4.5.1v4**); retained paint/query data belongs to the selected view
pool (**D4.1.4v5**).

Acceptance: successive simple pairs, consecutive explicit base/annotation
boxes, annotation-only and missing-annotation columns, two annotation levels,
one spanning anonymous annotation, computed roles on arbitrary elements, and
unchanged source sibling/parent links after retained reflow. Alignment,
auto-hiding, whitespace gap pairing, vertical-axis mapping, and fragmentation
remain subsequent finite deliveries against this shared structure.

A bundled Chromium 152 probe with Ahem records the column/query contract in
`temp/ruby-column-browser-probe.json`: a 20px base with a 40px annotation has a
40px base text range; a 40px base with a 10px annotation gives the annotation
text range 40px; successive 20/30px and 40/10px pairs occupy adjacent 30px and
40px columns, totaling 70px. Text ranges include allocated alignment space.
Preserve that geometry while arranging actual glyph advances; shifting only
the range rectangle to the glyph origin is insufficient. Center/start and
space distribution therefore need separate measured-content offsets when
column boxes expand. Reuse the existing text-justification machinery, including
its paint and source-range consumers, instead of adding a second glyph path.

The shared scratch model now supplies both intrinsic column measurement and
used placement. `RubyColumnsPairSegmentsLevelsAndAnonymousSpanningContent`
passes all eight cases, including retained reflow and preservation of source
parent/sibling links. The successive-pair case now measures and uses 70px;
changing its first annotation font from 10px to 20px and back changes both
used and max-content width from 70px to 100px and back. Unbroken Latin bases
retain their combined min-content width, using the existing inline boundary
and min-run helpers. Annotation-only ruby contributes its virtual base font
strut rather than a zero-height line.

Expanded column ranges retain separate glyph insets and alignment advances.
The existing glyph visitor, raster/export projections, caret geometry and
text justification consume that data; no second shaping or painting path is
introduced. The range/caret browser probes remain under `temp/`, including
`ruby-column-caret-browser-probe.json`.

Two prerequisites surfaced during integration. First, CSS Ruby §3.5 forces
`unicode-bidi` isolation on the container and internal roles. Computed CSSOM,
native style, explicit inheritance and the inline boundary classifier now use
the same value transform. An existing inheritance branch incorrectly copied
the parent's direction enum into `unicode-bidi`; it now uses the computed
property consumer. Isolate boundaries reset the existing kerning context, so
a preceding space cannot kern with the first base glyph and inflate its union.
Base bidi collection excludes annotation levels; their own isolate contexts
continue through the existing bidi resolver.

Second, justification expanded descendant advances without refreshing their
inline ancestors. The later block refresh deliberately skips isolates, so
the Ruby box retained its old width despite correct child positions. The
shared justification walk now finalizes changed inline bounds bottom-up,
reusing the same finalizer as line shifting and leader expansion.
`RubyAndIsolateBoundsIncludeJustifiedDescendantAdvances` exercises ordinary
inlines, isolates and Ruby with causal line-width changes.

Chromium 152 reports `unicode-bidi: normal` for Ruby and annotation roles,
unlike CSS Ruby §3.5's forced computed value. It also uses different
annotation-only line metrics in the independent probe
`temp/ruby-isolation-empty-browser-probe.json`. The implementation follows the
specified computation and virtual-base structure; the raw Chromium references
and comparison tolerances remain unchanged.

Qualification exposed a shared empty-inline whitespace defect in
`multicol-fill-balance-031`: the initial text trim removed line-leading spaces
but left the preflight's `had_leading_space` flag set. A first overlong word
therefore wrapped after an empty inline's allocation. The text path now clears
the consumed opportunity, following CSS Text 3 §4.1.2. The regression test
fails before the fix for ordinary decorated inlines, isolates and annotation-only
Ruby; all pass after the fix, including retained width changes and a nonempty
preceding-word control that must still wrap. The original multicolumn case
again fully matches. Logs: `temp/ruby-empty-inline-regression-before.log`,
`temp/ruby-m3-multicol-fixed.log`; independent ordinary-inline browser controls
are in `temp/ruby-empty-base-wrap-browser-probe.json`.

Focused validation passes all eleven Ruby/justification/empty-inline checks.
The raw WPT run has **31 full matches and 181 mismatches**, with no execution
errors and no lost matches relative to M1/M2's 20. Newly matching cases include
different-sized bases, four text-combine cases, text collapse and one line
breaking case. Logs: `temp/ruby-columns-native-8.log`,
`temp/ruby-after-columns-3.log`,
`temp/ruby-columns-final-score-delta.json`. The layout dimension lint and
`git diff --check` pass.

M3 qualification completes with **4973 full matches, 301 accepted partials,
and zero layout regressions** across the required layout baseline suites;
all **446 native text-query checks** pass. The complete Radiant gate still
fails the existing browser-global profile and ten slide teardown exits after
their assertions pass. The additional slide caret, table, editor and sidebar
cases reproduce with the saved unchanged `c5e40c521` executable, including the
intermittent sidebar-delete exit. This does not claim the aggregate gate is
green. Logs: `temp/ruby-m3-radiant-qualified.log`,
`temp/ruby-m3-head-extra-slide-controls.log`,
`temp/ruby-m3-head-slide-controls-2.log`,
`temp/ruby-m3-head-sidebar-delete-1.log`; gate artifacts are copied to
`temp/ruby-m3-qualified-gate/`.

All input native runners are rebuilt before `make test-input`. With native
graphics and bounded nested concurrency it completes **4594/4621 checks**,
retaining exactly the 27 pre-existing wiki/prose/PDF failures already confirmed
on the unchanged base during M2. Every CSS runner passes; no WebGL capture is
skipped or times out. Logs and summary:
`temp/ruby-m3-input-qualified.log`,
`temp/ruby-m3-input-output/test_summary.json`,
`temp/ruby-m3-input-failures.json`. The Ruby module's normalized types and
builder/column helpers remain private to its implementation; its header
exposes only measurement, placement and line-finalization entry points.
After that interface cleanup, the host/native runner rebuild, all eleven
focused checks, the 31/212 raw WPT result, the dimension lint and diff checks
remain unchanged (`temp/build-ruby-m3-private-model.log`,
`temp/ruby-m3-final-focused.log`, `temp/ruby-m3-final-wpt.log`).

This milestone does not complete whitespace-gap normalization, annotation
auto-hiding, break suppression, synchronized wrapping within or across columns,
vertical/RTL placement, anonymous ruby outside a container, or overhang.
Complex parallel min-content breaks and spanning wrap opportunities remain
part of that flow work. These are subsequent finite deliveries against the
shared model, not exceptions in the fixture harness.

## M4: hidden annotation participation

Priority: P1. Redundant readings and explicitly collapsed annotations must not
widen columns or raise lines, while subsequent readings must keep their base
pairing. CSS Ruby 1 §2.4 defines that difference from ordinary `visibility:
hidden` and from removing an annotation before pairing.

Compute hiding on normalized paired boxes before measuring columns. Compare
untransformed source text content, ignoring element boundaries; retain the
normalization model's source links and pairing. Layout, intrinsic measurement,
paint and text queries must agree on the annotation's absent participation.
Do not overwrite authored style or source text. Keep paired flags in the
existing scratch scope and comparison buffers call-scoped (**D4.5.1v4**).

Acceptance: an identical reading with a larger annotation font contributes no
size; changing the reading to different text and back reverses that effect;
nested markup compares equal text; source whitespace and text transformation
are compared before rendering; `visibility: collapse` removes size while
`visibility: hidden` reserves it; a later reading retains its original base;
and a spanning anonymous annotation uses its full paired base content.
The engine does not implement `ruby-merge`; M4 covers default separate
annotations. Add that property in a later delivery together with its sizing
and alignment behavior, including its disabling of hiding.
Use focused native layout, intrinsic, query and glyph checks during editing;
run the raw Ruby suite and required gates at the milestone boundary.

The bundled Chromium 152 probe does not hide identical annotations and does
not remove a collapsed annotation's column contribution
(`temp/ruby-hiding-browser-probe.json`). Preserve those browser references;
native assertions follow the specification for this unsupported behavior.
The separate plain-text probe also checks small/large annotation fonts and a
Japanese kana reading (`temp/ruby-autohiding-plain-browser-probe.json`); none
auto-hide in Chromium 152.

The normalized model now computes hiding after pairing and before intrinsic
measurement. It compares source text with the shared `dom_collect_text_content`
helper, promoted from the DOM adapter into the source-DOM layer and reused by
textarea initialization as well. Two reusable native buffers are released
before normalization returns; no text or source node is copied into retained
view state. Hidden boxes keep their unit indices but contribute zero width.

Used layout suppresses the hidden source range's geometry and text rectangles.
A selected-view `ruby_annotation_hidden` output distinguishes that result from
authored visibility. The ordinary flow clears it when a node participates
again; lifecycle reset, temporary measurements and retained flow replay all
save/reset it with the corresponding view geometry. The common visibility
classifier applies it to paint, focus and hit queries without allowing a
descendant's authored `visibility: visible` to restore a hidden reading.
Rendered Range text excludes it while raw Range text retains the source.
These remain selected-view outputs under **D4.1.4v5 / D4.5.1v4**.

`RubyHiddenAnnotationsRetainPairingWithoutContributingSize` fails before the
implementation: the identical-reading case measures/uses 110px rather than
70px, and a nested identical reading uses 80px rather than 40px. It now passes
the nine geometry cases, source whitespace/transform comparisons, spanning and
empty text, preserved later pairing, initial/retained layout, and text,
display-role and visibility mutations. Range rectangles disappear/reappear,
raw text remains `XX`, and the shared paint walk visits the three visible text
nodes without visiting the hidden reading. All nine focused Ruby/whitespace
checks pass (`temp/ruby-m4-native-4.log`); the dimension lint passes.

The earlier atomic annotation line-metric fixture gave both its base and its
reading empty text. That must auto-hide under §2.4, so its reading now contains
distinct text to keep the independent metric control visible. A separate
empty-content case explicitly requires auto-hiding and a 50px base-only line;
the visible atomic controls still require a 100px line.
The terminal-space fixture had the same issue with identical whitespace-only
source text. Its distinct 5px reading plus 10px atomic box now requires a 15px
annotation, excluding the trailing collapsible space. This preserves that
independent whitespace assertion while the new empty-content test covers
mandatory auto-hiding.

The raw Chromium comparison currently has **30 full matches and 182
mismatches**. Its only lost match is `ruby-text-collapse`: Chromium retains
collapsed annotation sizing, contrary to §2.4. The native collapse/hidden/
visible mutation control checks the required 70px/190px distinction. References
and tolerances remain unchanged. Logs: `temp/ruby-m4-reproducer-before.log`,
`temp/ruby-m4-final-wpt.log`, `temp/ruby-m4-wpt-delta-1.json`. The shared text
content helper also serves the DOM adapter, requiring both Radiant and Lambda
baseline qualification.

The Radiant aggregate completes with **4973 full layout matches, 301 accepted
partials and zero layout regressions**. It reports the known browser-global
profile and the twelve slide teardown exits already reproduced on the base,
plus the terminal-space control's old visible-annotation expectation. After
correcting that fixture, the entire native text-query component passes
**447/447** checks. The engine implementation is unchanged between that
aggregate and the component replay. The updated focused run also verifies
that a descendant's explicit `visibility: visible` cannot restore a collapsed
annotation, while it still overrides ordinary inherited `visibility: hidden`.
Logs: `temp/ruby-m4-radiant-baseline.log`,
`temp/ruby-m4-radiant-output/`,
`temp/ruby-m4-focused-final.log`,
`temp/ruby-m4-text-query-qualified.log`. This does not claim the raw aggregate
gate exited successfully. The pure hidden-ancestor query is inline in the view
header so Range-only/native state-store consumers do not acquire a graphics
pool linkage dependency.

The required Lambda baseline completes **8501/8502** checks: all **2112/2112**
input baseline checks and **6389/6390** runtime checks. Its sole failure is
`proc_mir_mvp_roots`, whose final subnormal boxed-value comparison prints
`false` instead of the golden `true`. Both the current host and saved unchanged
`c5e40c521` host reproduce exactly that output, so the unrelated failure is
retained rather than changing its golden or runtime during Ruby work.
Logs: `temp/ruby-m4-lambda-baseline.log`,
`temp/ruby-m4-lambda-output/`,
`temp/ruby-m4-core-roots-head.txt`,
`temp/ruby-m4-core-roots-current.txt`.

The full input gate is rebuilt and completes **4594/4621** checks, with the
exact same 27 wiki/prose/PDF failures as M3 and no new failure. All CSS runners
pass, and no PDF/WebGL case is skipped. Logs and summary:
`temp/build-ruby-m4-input-runners.log`,
`temp/ruby-m4-input-qualified.log`,
`temp/ruby-m4-input-output/test_summary.json`.
The native host is restored after Lambda's debug-host gate; the final raw Ruby
run remains 30/212. Dimension lint and diff checks pass. This closes default
annotation hiding; merge behavior, whitespace boundaries between hidden and
visible annotation sequences, and general wrapping remain later flow work.

## M5: annotation forced-break normalization

Priority: P1. Annotation `<br>` and preserved source newlines currently create
extra lines. CSS Ruby 1 §2.2 step 7 instead converts forced annotation breaks
using collapsible segment-break rules, irrespective of authored `white-space`.
Their source DOM and computed style remain intact. HTML's rendering model
describes `<br>` as a newline control; its used break behavior must therefore
follow this normalization as well as ordinary text processing.

Use one computed-role policy for layout, intrinsic measurement, bidi and text
projection. Extend the existing collapsible-space ownership/trim path to a
break-marker box rather than creating fake DOM text or a separate glyph path.
Retain the source marker's used geometry in its existing view ownership domain
and journal it with the corresponding view state (**D4.1.4v5 / D4.5.1v4**).

Acceptance: direct/nested annotation BRs and preserved newlines; leading,
repeated and trailing converted breaks; surrounding collapsed spaces;
preserved ordinary spaces; arbitrary elements with annotation display roles;
text/range and glyph positions; intrinsic/used size agreement; retained role
changes. Ordinary text and base independent formatting contexts must retain
their forced breaks. Annotation breaks inside nested formatting contexts
follow §2.2's annotation rule. Full synchronized wrapping remains subsequent
work against the shared column model.

Chromium 152's Ahem probe confirms a converted BR has a space allocation and
that an independent inline-block in the base still breaks
(`temp/ruby-br-inlinification-browser-probe.json`). Some pinned WPT references
expect all Ruby-related BRs to disappear without a separator; use the current
specification and independent normalization controls, preserving the raw
references. Base-only BR/newline handling requires its own audit rather than
blanket suppression based on a tag name.

Verification budget: focused native normalization and existing Ruby/whitespace
checks in the edit loop; raw Ruby suite and dimension lint; required Radiant
and input gates at the stable milestone boundary. Add other required gates if
the owning stage expands beyond layout/CSS projection.

### M5 implementation and evidence

`layout_ruby_annotation_content` derives annotation membership from computed
roles, including descendants inside independent formatting contexts. Used LF
processing overrides the break policy without changing authored spaces or
computed `white-space`. Converted BRs retain their own source element/view and
space advance. The existing trailing-space trim accepts that box as an owner;
`Linebox::clear_trailing_space` clears both the advance and owner together.
The existing view snapshot already journals the marker's geometry, while its
line-context borrow remains pass-local (**D4.1.4v5 / D4.5.1v4**).

Intrinsic collectors and the generic inline-run aggregation recognize converted
BRs as separators. The shared normalizer takes explicit inline-edge context so
it does not discard a separator merely because it crosses a source-node
boundary. Annotation formatting contexts bound that lookup. Bidi receives
U+0020 instead of a paragraph boundary; repeated source LFs retain their source
ownership with zero advance. Paint export, raster/shadows and reversed-cluster
measurement use the same space-survival decision. This fixes a mixed Hebrew/
Latin control where treating each raw LF independently displaced glyphs by
20px even though the annotation's total width was correct.

`layout_ruby_break_normalization.html` covers direct/nested BRs, repeated and
line-edge breaks, preserved spaces/newlines, split inline owners, arbitrary
annotation roles, independent annotation/base contexts, RTL and mixed bidi.
The native test checks used/intrinsic agreement, glyph placement against a
single-space control, LF Range geometry, unchanged source/computed style and
retained role changes. The focused Ruby/whitespace selection passes 11/11.
The complete native text-query component in the Radiant aggregate passes
448/448. Logs: `temp/ruby-m5-reproducer-before.log`,
`temp/ruby-m5-bidi-control-before.log`,
`temp/ruby-m5-focused-final.log`, `temp/ruby-m5-radiant-output/`.

The raw suite remains **30 full matches / 182 mismatches**, with no lost full
match or execution error. Annotation normalization improves the element match
rates of `ruby-line-break-suppression-001` (6.3% to 18.8%) and `-003` (0% to
17.6%); their remaining failures include base-level behavior. Raw references
and comparison tolerances are unchanged. Logs:
`temp/ruby-m5-wpt-first.log`, `temp/ruby-m5-wpt-delta.json`.

Qualification finishes with **4973 full layout matches, 301 accepted partials
and zero layout regressions**, including **448/448** native text-query checks.
The aggregate reports nine slide teardown exits, all among the cases already
reproduced on unchanged `c5e40c521`; no new failing case appears. This run's
browser-global profile and the other previously failing slide cases pass.
The gate remains red; no exemption is added. Logs and summary:
`temp/ruby-m5-radiant-baseline.log`,
`temp/ruby-m5-radiant-output/test_summary.json`.

The rebuilt full input gate completes **4594/4621**, with exactly the same 27
wiki/prose/PDF failures as M4, no new or resolved failing case and no skipped
case. CSS runners pass. Logs: `temp/build-ruby-m5-input-runners.log`,
`temp/ruby-m5-input-qualified.log`,
`temp/ruby-m5-input-output/test_summary.json`.
Dimension lint and diff checks pass. The generated CSS support inventory is
refreshed because source/test line references moved; its registrations and
qualification fields are unchanged, and the generator's check passes.

The next diagnostic target is textless inline annotation geometry. In the
saved `ruby-align-001` output, all text and base-column geometry match, while
each visible annotation containing a 160px-wide zero-height atomic child has
zero height. Chromium exposes its 8px font box. Inspect the shared inline
font-box/strut path and preserve descendant overflow independently before
changing placement; do not add a per-fixture annotation height.

## M6: inline font boxes around atomic content

Priority: P1. Diagnose the textless-inline content-area path shared by ordinary
inlines and Ruby annotations. CSS 2.2 §10.6.1 defines the inline content area's
height from its font, while child atomic boxes can overflow it. CSS Ruby
§3.1.2/§3.3 uses the annotation's inline content box, and §3.6 excludes its
authored line-height from Ruby line spacing. Keep annotation font-box geometry
and descendant overflow/line contributions distinct.

First qualify a native reproducer against Chromium for zero-height and taller
atomic descendants, nested inline owners, inherited and differing fonts,
zero/large annotation line-height, over/under placement and computed role
changes. Reuse the existing inline font/strut/fragment helpers; preserve source
identity and selected-view ownership (**D4.1.4v5 / D4.5.1v4**). Include zero
font-size and hidden annotations as controls. Run focused checks during the
edit loop, then raw Ruby, dimension/inventory/diff checks and the required
Radiant milestone gate when stable. Broaden input qualification only if
source/interface changes or unresolved concerns require it.

The Chromium control confirms 20px font boxes for ordinary atomic-only
wrappers, 10px boxes for the annotation owners, and zero font boxes at zero
font size. Zero and large annotation line-height do not change those content
heights (`temp/ruby-inline-font-box-browser-probe.json`). The native
reproducer narrows the failures to zero line-height and zero font size.

The existing content-area helper is promoted from `layout_text.cpp` and shared
by inline construction, final line alignment and collapsed-span fixup. It
keeps a resolved zero font size at zero instead of falling back to leading.
Construction and final alignment both normalize a real inline extent even
when its line-height is zero. This also removes a duplicated font-height
fallback in final alignment. The first edit fixed annotations but the
ordinary controls exposed final line alignment overwriting their geometry;
both phases now use the same content-area policy. Focused adjacent font/Ruby
checks pass 14/14; final qualification is recorded below.

The new native test preserves the ordinary atomic baselines and descendant
heights, checks nested owners and differing child fonts, and exercises
retained font/display/visibility changes without changing source identity.
Logs: `temp/ruby-m6-reproducer-before.log`,
`temp/ruby-m6-focused-first.log`, `temp/ruby-m6-focused-second.log`.

The corrected font boxes exposed a second root cause: Ruby spacing compared
preliminary box coordinates with a finalized paragraph baseline, adding the
paragraph's leading twice. Three alignment cases lost their previously
matching text positions in that intermediate build. Spacing now accumulates
the complete over/under stack and reserves only its shortfall against the
existing line height (CSS Ruby §3.6). Additional leading goes to the sides
whose annotation extents exceed the available half-leading. This allows
annotations to overflow their own line box while keeping equivalent successive
Ruby lines apart. The existing initial-letter adjustment is retained.

A visible zero-font annotation still establishes an annotation level, even
though a tall atomic descendant overflows its zero-height font box. The
Chromium control gives that row a 20px base extent, while a base-only Ruby at
zero line-height leaves a zero-height line. The model distinguishes these
cases by visible annotation geometry and reuses the shared font/atomic baseline
queries for the base extent. Native retained controls reproduce both.

The raw suite improves to **33 full matches / 179 mismatches**, with no lost
full match or execution error. New matches are `nested-ruby-pairing-001-ref`,
`no-break-opportunity-at-end-ref` and `ruby-autohide-001`. The three intermediate
alignment regressions are recovered; other partial mismatches remain recorded
without changing references or tolerances. Logs:
`temp/ruby-m6-wpt-spacing.log`, `temp/ruby-m6-wpt-spacing-delta.json`,
`temp/ruby-m6-focused-spacing-rebuilt.log`.

The Radiant gate's text-query component passes **449/449**. Every layout
baseline category passes: **4974 full matches, 300 accepted partials and zero
regressions**. One CSS Text baseline case improves from partial to full. The
aggregate remains red on the same twelve slide teardown cases already
reproduced on the unchanged base; the browser-global profile passes. Logs:
`temp/ruby-m6-radiant-baseline.log`,
`temp/ruby-m6-radiant-output/test_summary.json`,
`temp/ruby-m6-radiant-output/test_radiant_layout_baseline_results.json`.
The rebuilt input gate completes **4594/4621**, with the same 27 failures as
M5 and no new/resolved failure, skip or timeout. Logs:
`temp/build-ruby-m6-input-runners.log`, `temp/ruby-m6-input-qualified.log`,
`temp/ruby-m6-input-output/test_summary.json`.
Dimension lint and diff checks pass. The generated CSS inventory is refreshed
and its check passes; only source/test references change. This closes M6's
font-box and spacing scope. No core runtime or formal ruling is changed.

## M7: nested Ruby block-axis envelopes

Priority: P1. CSS Ruby §3.1.2 requires a base container to include the base and
annotation containers of descendant Ruby participating in its inline
formatting context. The saved `nested-ruby-pairing-001` output now matches
15/16 elements and 13/14 text nodes. Its outer `Southeast` reading is 10px too
low; the outer base bounds include the inner Ruby's font box but omit its
annotation level. Evidence: `temp/ruby-m6-nested-pairing-differences.json`.

Extend the shared normalized-box bounds using the existing bounded view walk,
including participating nested Ruby boxes while stopping at independent
formatting contexts and out-of-flow descendants. Preserve each source box's
own font/content geometry and DOM identity; the container envelope is layout
structure (**D4.1.4v5 / D4.5.1v4**). Do not add a separate nested-Ruby placement
algorithm or a per-test offset.

Acceptance: inner/outer over and under combinations, multiple nested levels,
hidden inner annotations, retained font/role changes, glyph positions and
source identity. Verify the focused native Ruby controls and raw Ruby suite,
then run the required Radiant gate at a stable boundary. General wrapping,
non-inline containers, overhang and vertical layout remain open families.

### M7 implementation and focused evidence

The shared bounded box walk now has a container-measurement policy. Alignment
and baseline collection retain their existing atomic treatment of nested Ruby;
container bounds descend through inline Ruby structure and include its internal
boxes and direct base/annotation roots. The walk stops at independent formatting
contexts and excludes out-of-flow/undisplayed content. Only the scratch bounds'
block-axis envelope expands; source inline font boxes and horizontal column
allocation do not change. No new retained payload or normalization path is
introduced (**D4.1.4v5 / D4.5.1v4**).

`NestedRubyContainersIncludeParticipatingAnnotationLevels` reproduces same-side
overlap before the fix and passes afterward. It covers all four inner/outer
position combinations, three nested levels, nesting inside a reading, an atomic
formatting-context boundary, a larger base font, initial/retained geometry,
collapse/restore, font and display mutations, source identity and painted glyph
baselines. Three compact Ahem lines each reserve 40px for their contiguous
20px base and two 10px readings; adjacent stacks do not overlap. The roomy-line
controls keep their authored 100px spacing and each Ruby inline's own 20px font
box. All eleven focused controls pass: `temp/ruby-m7-focused-final.log`.

The Chromium probe agrees on the position combinations, larger base, nested
readings, font/role mutations and compact Ahem spacing (except the already
recorded collapse behavior and a 5px font cell raster-metric difference).
Probe: `temp/ruby-nested-envelope-browser-probe.js` and `.json`.

The raw suite remains **33/212 full matches, 179 mismatches**, with no new/lost
full match or execution failure. Two block-Ruby partial text scores improve;
`nested-ruby-pairing-001` falls from 15/16 elements and 13/14 text nodes to 1/16
and 2/14. Its annotations now stack without overlap relative to their base,
but reserving the complete physical stack expands the line from 26px to 35px
and moves the base down. The saved Chromium reference reserves 24px in that
CJK/font-fallback case. A fresh browser probe with the harness's Liberation
Serif and Noto faces reproduces the reference's geometry; the Ahem controls
reserve the complete stack. This browser spacing discrepancy remains visible;
there is no CJK/font-specific suppression of the CSS Ruby §3.6 spacing rule.
Evidence: `temp/ruby-nested-envelope-wpt-browser-probe.json`,
`temp/ruby-m7-nested-pairing-first-differences.json`,
`temp/ruby-m7-wpt-first.log`, `temp/ruby-m7-wpt-first-delta.json`.

### M7 qualification

The required Radiant gate finishes with **450/450 native text-query tests**,
**4974 layout full matches, 300 accepted partials and zero regressions**.
The aggregate remains red: eleven previously documented slide teardown cases
fail, while `slide_editor` passes this run. `slide_spelling_blocks` has one
assertion failure under aggregate load; all nine assertions pass in isolation
on the saved unchanged-base executable and in both repeats on the current
executable. This transient assertion failure remains a qualification limit,
without a fixture/harness change or an unrelated spelling repair. Logs:
`temp/ruby-m7-radiant-baseline.log`,
`temp/ruby-m7-radiant-output/test_summary.json`,
`temp/ruby-m7-radiant-output/test_radiant_layout_baseline_results.json`,
`temp/ruby-m7-spelling-head-control.log`,
`temp/ruby-m7-spelling-current-control.log`.

Dimension lint, generated CSS inventory checks and diff checks pass; the
inventory changes only source/test references. The implementation is private
to Ruby layout, with no input/runtime change. This closes M7's nested-envelope
scope; the broader objective remains active.

## M8: non-inline Ruby formatting

Priority: P1. Blockified Ruby (including floated, positioned and grid-item
Ruby) retains its Ruby inner display but `layout_block_inner_content` has no
Ruby dispatch. Its content bypasses the shared normalized formatter. CSS Ruby
§2.1.2 requires a block container with an anonymous inline Ruby containing the
original content, inheriting the non-inline container's formatting values.

Reuse the existing Ruby model and formatter for that anonymous formatting role,
while retaining the source block's width, margins, padding, border and used
position. Keep its source DOM identity and the selected-view ownership rules
(**D4.1.4v5 / D4.5.1v4**). Do not add a second column or annotation algorithm.

Acceptance: a simple authored block Ruby, arbitrary-element Ruby, floated and
positioned Ruby, blockified grid item, annotation-only content, paired bases,
padding/border and width preservation, plus retained font/display changes.
Use native geometry/paint controls and the original block-Ruby WPT documents.
General within-base wrapping, vertical Ruby and multicol fragmentation remain
separate families. Verify focused controls and raw suite during the edit loop,
then dimension lint, inventory and the required Radiant gate at the boundary.

The raw suite also includes testharness documents whose browser snapshots have
a reporting UI that native layout deliberately omits. For example,
`line-break-around-ruby-001` has a 605.72px browser `#log` with two report
sections, while the native `#log` is empty. Its six layout rows therefore have
a common vertical origin difference even where their geometry agrees.
`temp/ruby-m7-line-break-around-ruby-001-differences.json` records this capture
limitation; no reporting shim, fixture, reference or tolerance is changed.

### M8 implementation and focused evidence

`layout_block_inner_content` dispatches non-inline Ruby to the existing Ruby
formatter. The source block keeps its principal geometry; scratch normalization
represents its anonymous inline Ruby. Both paths share column resolution,
annotation positioning and final block-axis alignment. The anonymous inline is
finalized while that model is alive, then the ordinary block finalizer owns
used dimensions, overflow and baselines. Annotation-only Ruby contributes an
anonymous base strut without writing the source block's content height.

The grid control exposed a second owning-stage defect: the generic intrinsic
height estimator stacked a base and its annotation as two block siblings,
producing a 136px minimum instead of the 76px natural border-box height.
`layout_ruby_intrinsic_height` now measures with the same formatter. Existing
`LayoutMeasureScope`, full flow capture/replay and a counter checkpoint preserve
selected scalar geometry, text fragments, generated effects and caller state
(**D4.1.4v5 / D4.5.1v4 / D4.5.2**). This is an exact flow measurement, without a
second font/column-height estimator. The ordinary intrinsic-height cache owns
the scalar result.

Retained block-to-inline changes also exposed the Ruby publication path
omitting the ending decoration. Its border-box publication now obtains both
decoration edges from the shared inline-edge helper and leaves margins outside
that box. The formatter no longer receives a margin-inclusive starting edge
from its caller.

`NonInlineRubyRetainsItsBlockBoxAndFormatsPairedContent` fails before dispatch
is added and passes afterward. It covers authored and arbitrary-element block
Ruby, float/position/grid blockification, generated base content, annotation-only
content, paired columns, grid stretch, centered/right alignment, border/padding,
painted glyph positions, initial/retained layout, font/display changes and
external margins. Repeated intrinsic queries with different available widths
preserve the selected readings' rectangles and paint positions and the caller's
active line, view and sizing state. The existing nested-Ruby paint checks share
the same single-glyph assertion helper. All twelve focused controls pass:
`temp/ruby-m8-focused-final.log`.

The browser agrees on the non-inline boxes, grid stretch, generated base and
center/right controls: `temp/ruby-non-inline-browser-probe.json`. Its known
annotation-only empty-base and unsupported explicit-base-role behavior remains
different from the CSS model; those results do not replace native spec checks.

The raw suite remains **33/212 full matches, 179 mismatches**, with no new/lost
full match or execution error. Block-Ruby 002 and 005 improve substantially;
005 reaches 95.8%/96.6% element matches in the test/reference documents.
Block-Ruby 003/004 partials and the decorated intrinsic-width case also expose
remaining failures. Scores and all changed partials are retained in
`temp/ruby-m8-wpt-final.log` and `temp/ruby-m8-wpt-final-delta.json`.

### M8 qualification

The required Radiant gate finishes with **451/451 native text-query tests**,
**4974 layout full matches, 300 accepted partials and zero regressions**. The
aggregate remains red on ten of the previously reproduced slide teardown
failures. There are no new failing cases compared with M6; direct-text and
home-ribbon teardown cases pass this run, as does the transient spelling case
observed in M7. Logs: `temp/ruby-m8-radiant-baseline.log`,
`temp/ruby-m8-radiant-output/test_summary.json`,
`temp/ruby-m8-radiant-output/test_radiant_layout_baseline_results.json`.

The freshly rebuilt input gate completes **4594/4621**, with the exact same
27 failures as M6 and no new/resolved failure, skip or timeout. Logs:
`temp/build-ruby-m8-input-runners.log`, `temp/ruby-m8-input-qualified.log`,
`temp/ruby-m8-input-output/test_summary.json`. Dimension lint, inventory
generation/check and diff checks pass; the inventory changes only source/test
references. This closes M8's non-inline formatting scope. The isolated-inline
margin defect below and broader Ruby flow integration remain open.

## M9: isolated inline margins during bidi placement

Priority: P1. The decoration publication correction exposes an existing shared
bidi defect when text precedes a decorated Ruby. `bidi_append_atomic_fragment`
adds external margins for atomic inline boxes, but not isolated inline boxes.
The latter already supply complete border-box widths, and therefore must also
supply their separate margin advances. Without them, placement moves a Ruby
backward across its starting margin and loses its ending margin before the
following text. The same path serves ordinary isolated inlines.

Keep this repair in the shared bidi fragment construction. Acceptance: ordinary
isolated inline and Ruby controls after a text prefix, matching border boxes,
base/glyph positions and following-text advances; decorated/margin first-item
controls and retained changes. Use CSS Writing Modes §2 bidi isolation and
CSS 2.1 §8.3 inline margin-box rules. Preserve source geometry ownership
(**D4.1.4v5 / D4.5.1v4**), without Ruby-specific offsets or width inflation.
Verify focused bidi/Ruby controls, the raw Ruby suite and required Radiant
baseline at the stable boundary. Full parallel Ruby wrapping remains open.

### M9 implementation and qualification

The shared bidi atomic-fragment builder now includes external margins for
isolated inline boxes as well as atomic inlines. Complete border-box widths
continue to include padding/border and exclude margins; no Ruby-specific
placement branch or width inflation is added. Isolated fragments reuse the
existing physical source-edge ownership and slice/clone policy for their
starting/ending margins. A contents proxy contributes no margin advance.

`IsolatedInlineMarginsKeepBorderBoxesAndFollowingTextAdvances` reproduces the
same lost advances for Ruby and ordinary isolated inlines before the repair.
It covers positive/negative margins, text prefixes, first-item placement,
atomic/contents controls, RTL paragraphs, initial/retained layout, live margin
changes, source identity and Ruby glyph positions. Border boxes remain 56px
while external advances change. Chromium agrees with all control positions:
`temp/ruby-isolated-margin-browser-probe.json`. The contents check uses its
descendant positions and computed role, since its internal view retains a
descendant union rather than a principal CSS box.

All **35 focused bidi/Ruby controls pass**:
`temp/ruby-m9-focused-final.log`. The raw suite remains **33/212 full matches,
179 mismatches**, without a lost full match or execution error. Decorated
intrinsic-size and block-Ruby 004 partial scores improve, with no worse partial
score. Logs: `temp/ruby-m9-wpt-qualified.log`,
`temp/ruby-m9-wpt-qualified-delta.json`. Wrapped Ruby still has a separate
fragment-layout disagreement, retained in
`temp/ruby-m9-ruby-intrinsic-isize-003-differences.json`.

The required Radiant gate passes **452/452 native text-query tests** and all
layout categories: **4974 full matches, 300 accepted partials, zero regressions**.
Its aggregate remains red on nine of the previously reproduced slide teardown
cases; all belong to the known M6 failure set, including the direct-text and
home-ribbon cases that happened to pass in M8. Logs:
`temp/ruby-m9-radiant-baseline.log`,
`temp/ruby-m9-radiant-output/test_summary.json`,
`temp/ruby-m9-radiant-output/test_radiant_layout_baseline_results.json`.
Dimension lint, inventory generation/check and diff checks pass; only generated
source/test references change in the inventory. This closes M9's margin-advance
scope. No parser, runtime, public measurement API or paint emitter changes in
this milestone; the input qualification recorded under M8 is not claimed as a
fresh M9 input run.

## M10: base-only Ruby inline fragmentation

Priority: P1 prerequisite for parallel annotation wrapping. A minimal Ahem
control with `word-break:break-all`, a text prefix and a decorated Ruby without
readings exposes three owning stages. The generic text-node adapter copies only
min/max widths and omits computed wrap policy and edge-segment metadata, so a
direct-text Ruby base loses its internal opportunities. `span_has_line_filled`
then always probes Ruby's max-content width even when its normalized result
does carry a legal leading-segment opportunity. After base flow wraps, the Ruby formatter treats
the union of several lines as one column, shifts every fragment to the original
starting cursor and subtracts cursors belonging to different lines. Native
ordinary/isolated inline controls occupy 180px; Ruby occupies 240px and moves
its base fragments. Probe: `temp/ruby-fragment-probe.html`,
`temp/ruby-fragment-native-probe.json`,
`temp/ruby-fragment-browser-probe.json`.

Preserve the computed text measurement's full segment protocol through the
generic node adapter, reusing the existing text-line measurement/options path.
Use the normalized leading unbreakable segment for lookahead rather than the
whole Ruby when parallel break opportunities exist. When normalization produces
no annotation levels, there is no parallel column-sizing constraint: retain the
ordinary base-level flow and its per-line cursors instead of realigning the
multi-line union. Preserve normalization/discarding, computed roles, source
identity and the common inline fragment/bidi machinery
(**D4.1.4v5 / D4.5.1v4**). This is the degenerate Ruby formatting model, not a
tag-name exception or an alternate column solver.

Acceptance: base-only Ruby and ordinary isolated inline controls at constrained
widths, text prefixes/suffixes, margins/borders/padding, split inline owners,
initial/retained geometry and width changes; unbreakable annotated Ruby keeps
its existing leading-unit constraint. Verify normalized intrinsic prefix data,
source text rectangles and glyph positions. Chromium's ordinary and isolated
controls select different soft breaks in the diagnostic; keep that evidence
visible rather than making a per-browser packing rule. General parallel
annotation fragmentation, overhang and vertical Ruby remain separate families.
Run focused Ruby/bidi checks, the raw suite, lint/inventory and the required
Radiant gate at the stable boundary.

### M10 implementation and focused evidence

The text-node intrinsic adapter now retains the shared line measurement's
computed wrap policy and complete edge-segment result. Three measurement
consumers share one options constructor, preserving selected fonts and the
annotation-only segment-break normalization. Ruby lookahead consumes that
leading-segment protocol with external margins; decoration edges remain in
the intrinsic result. A model without annotation levels keeps ordinary base
flow instead of applying parallel alignment to a multi-line union. Non-inline
Ruby still finishes its anonymous last line, including intrinsic height trials.

`layout_ruby_base_fragments.html` covers direct text, a nested inline and an
arbitrary element with `display:ruby`, widths 90/100px, then retained mutations
to 80/140/90px. The native check compares source rectangle chains, following
advances and all four painted glyphs with isolated inline controls, while
retaining source identity. Min-content is 28px and max-content is 96px for the
decorated breakable item; the annotated nowrap control remains 96px for both.
The original reproducer failed at all three owning stages:
`temp/ruby-m10-reproducer-before.log`. The focused checks pass 36/36:
`temp/ruby-m10-focused-final.log`. Repeated bounded glyph collection is now a
shared test utility rather than another local callback variant.

The raw suite remains **33/212 full matches, 179 mismatches**, with no lost full
match. The decorated intrinsic-size 003 case improves from 63.2% to 71.1% of
elements and from 63.3% to 73.3% of text. Block Ruby 005 also improves. Some
internal-box element ratios and the within-base wrapping case worsen slightly;
those cases still apply the single-column solver to parallel fragments. The
new intrinsic lookahead exposes the latter's earlier legal start without
repairing its subsequent annotation fragmentation. Logs:
`temp/ruby-m10-wpt-final.log`, `temp/ruby-m10-wpt-final-delta.json`,
`temp/ruby-m10-basic-differences.json`.

### M10 qualification

The required Radiant gate passes **453/453 native text-query tests** and
**4974 full layout matches, 300 accepted partials, zero regressions**. Its ten
slide teardown failures all belong to the previously reproduced M6 control
set; there is no new failing name. Logs: `temp/ruby-m10-radiant-baseline.log`,
`temp/ruby-m10-radiant-output/test_summary.json`. After the test-utility
extraction, a freshly rebuilt native runner again passes **453/453**:
`temp/ruby-m10-build-native-final.log`, `temp/ruby-m10-native-qualified.log`.

Freshly rebuilt input runners pass **4594/4621**, retaining exactly the same
27 previously reproduced failures and no skips or timeouts. All CSS runners
pass. Logs: `temp/ruby-m10-build-input-and-native.log`,
`temp/ruby-m10-input-qualified.log`,
`temp/ruby-m10-input-output/test_summary.json`. Dimension lint, inventory
generation/check and diff checks pass; the inventory changes only generated
layout/source and native-test references. This closes the base-only fragment
scope. Parallel annotation fragmentation remains open.

## M11: unbroken parallel Ruby units

Priority: P1 common annotation prerequisite. A 40px container with an 80px
break-all base and a nowrap reading has no legal internal Ruby break under
[CSS Ruby §3.4.2](https://drafts.csswg.org/css-ruby-1/#break-within).
The normalized intrinsic protocol already records this, but ordinary base
flow still splits into two lines before the column solver runs. The resulting
multi-line union becomes a 150px-tall row; Chromium and an ordinary unbroken
inline control occupy one 60px line, overflowing horizontally. A real
`display:block ruby` reproduces the same defect. Probes:
`temp/ruby-unbroken-unit-probe.html`,
`temp/ruby-unbroken-unit-native-probe.json`,
`temp/ruby-unbroken-unit-browser-probe.json`.

Share normalized model measurement between intrinsic and used layout. When
the parallel model has no internal opportunity, constrain soft breaks in its
base flow without changing computed white-space/text-wrap or source boxes.
Use an IFC-scoped, stack-borrowed constraint so independent formatting
contexts retain their own wrap policy (**D4.1.4v5 / D4.5.1v4**). Preserve forced
base breaks and the existing annotation-only segment-break normalization.
The enclosing inline's leading fit must consume the same complete-unit width.

Acceptance: direct/nested/explicit bases and arbitrary computed Ruby roles,
inline and non-inline roots, a preceding collapsible separator, initial and
retained widths, source rectangles and painted glyphs; a nested atomic IFC
continues wrapping internally. Intrinsic break metadata and computed styles
remain unchanged by used flow. This milestone covers models without an
internal parallel opportunity; general coordinated fragments within and
between columns remain subsequent work. Budget: one meaningful before/after
native reproducer, focused Ruby/bidi checks during implementation, raw Ruby
suite, lint/inventory and the required Radiant gate at the stable boundary.
Broaden input checks only for new shared consumer changes or unresolved concerns.

### M11 implementation and focused evidence

`ruby_measure_model` now shares normalized measurement between intrinsic and
used layout. Its column storage is reset and reused when transitioning from
intrinsic constraints to used bounds. The base formatter borrows
`LayoutInlineKeepScope` only when the model has no used-layout break. Text
flow, inline lookahead and boundary checks honor that scope within its owning
IFC; independent flow boxes keep their computed policies. The common computed
white-space/text-wrap readers remain independent of this used-flow constraint.
The enclosing inline's unbroken-fit check consumes the same model permission.

CSS min-content metadata alone cannot establish that permission:
[CSS Text 3 §5.4](https://drafts.csswg.org/css-text-3/#overflow-wrap-property)
excludes `overflow-wrap:break-word` opportunities from min-content sizes.
`has_used_content_break` therefore travels beside the minimum's edge protocol
through text and inline-run measurement. Ruby intersects the parallel boxes'
permissions separately for intrinsic sizing and used layout. Emergency
availability reuses the existing scalar-or-grapheme unit helper promoted from
`text_metrics.cpp`; a single combining cluster remains indivisible. Atomic
inline contributions hide their internal opportunities from the outer IFC.
The text-line options also retain computed `text-wrap-mode`, independently of
space preservation, including both wrap/nowrap overrides.

The original native reproducer fails with split source rectangles, shifted
glyphs and a **150px row versus 60px**:
`temp/ruby-m11-reproducer-before.log`. The implementation passes all **84
focused Ruby, bidi, wrapping, grapheme and intrinsic controls**:
`temp/ruby-m11-focused-qualified-final.log`. The new fixture covers direct/nested/explicit
bases, arbitrary roles, a real block Ruby, prefix separators, retained width
changes, glyphs, independent atomic flow and both text-wrap overrides. Its
emergency checks prove an 80px minimum remains 80px while used breaks stay
available when every parallel level allows them; a nowrap reading forbids
that internal split. A normal parent keeps its 40px minimum between two
nowrap children (80px maximum), while an anonymous base made of two ordinary
inline pieces plus a space stays a 100px unit when its reading forbids an
internal break. Both pieces' source rectangles and glyphs match an ordinary
unbroken inline control. These checks qualify break availability, not coordinated
placement of breakable annotation fragments, which remains open.

The raw suite remains **33/212 full matches, 179 mismatches**, with no lost full
match. Three existing whitespace partial scores decline; their comparisons
remain visible in `temp/ruby-m11-wpt-qualified.log` and
`temp/ruby-m11-wpt-qualified-delta.json`. In the intra-level 002 case, ordinary
inline `rb` children form one anonymous base under the current computed roles;
the unbroken parallel reading now keeps that base together. This does not
qualify the test's intended explicit-base wrapping. Trailing-space and general
multi-fragment alignment remain flow work, as do emergency boundaries joining
different source boxes.

### M11 qualification

The required Radiant gate passes **454/454 native text-query tests** and
**4974 full layout matches, 300 accepted partials, zero regressions**. Its ten
slide teardown failures all belong to the previously reproduced M6 control
set. The insert-ribbon case fails again while sidebar-delete happens to pass;
there is no newly failing name against that reproduced set. Logs:
`temp/ruby-m11-radiant-baseline.log`,
`temp/ruby-m11-radiant-output/test_summary.json`.

After adding the grouped-anonymous-base check, the freshly rebuilt focused
runner passes **84/84** again: `temp/ruby-m11-build-input-and-native.log`,
`temp/ruby-m11-focused-qualified-final.log`. Dimension lint, inventory
generation/check and diff checks pass; only generated source/test references
change in the inventory. Fresh input runners pass **4594/4621**, with exactly
the same 27 failures as M10 and no skips or timeouts; every CSS runner passes.
Logs: `temp/ruby-m11-input-qualified.log`,
`temp/ruby-m11-input-output/test_summary.json`. This closes the unbroken-unit
core scope. General paired fragments and source-boundary emergency breaks
remain subsequent flow work; the overall objective remains active.

## M12: line flow between unbroken Ruby segments

Priority: P1 common Japanese mono-Ruby flow. In a 40px row, a 20px base with a
30px reading followed by a second 20px unit needs two lines. The grouped Ruby
currently consumes only 60px of block space and overflows to 50px inline;
the same units in separate Ruby containers, and Chromium's grouped Ruby,
correctly use two 60px lines. The base formatter chooses breaks before
annotation width is reserved, and the later solver puts every segment on one
linear cursor. Probe: `temp/ruby-segment-flow-probe.html`,
`temp/ruby-segment-flow-native-probe.json`,
`temp/ruby-segment-flow-browser-probe.json`.

Plan a flow unit for each segment that has no internal parallel break.
Preserve its source-node window in the scratch model, including out-of-flow
source positions and normalization's discarded nodes. Choose legal boundaries
and reserve the complete unit width before base placement; keep its parallel
levels together, contribute its line metrics before line finalization, and
align each reading with that unit's actual line. Reuse the existing column
solver, spacing, nested-envelope and inline-fragment helpers rather than
creating a second placement algorithm. Preserve borrowed model lifetimes and
selected fragments (**D4.1.4v5 / D4.5.1v4**).

Boundary permission must distinguish ordinary minima from used emergency
opportunities across source boxes and use the correct common style owner.
Reuse/promote the existing boundary helpers. A sequence with no legal boundary
stays one unbroken group; never force a break merely because a segment ends.
[CSS Ruby §3.4](https://drafts.csswg.org/css-ruby-1/#line-breaks) requires each
fragment's alignment to be independent after its legal break.

Acceptance: grouped Ruby versus the same units in separate containers at
narrow/wide and retained widths; equal and annotation-expanded units;
direct/nested bases and arbitrary computed roles; explicit level containers;
prefix/suffix boundaries; compact lines with unequal annotation heights and
over/under positions. Compare source rectangles, glyphs, following advances
and source identity. Keep independent IFCs and unbroken groups intact.
Coordinated internal splits within a segment and broader whitespace/vertical
contexts remain later flow work. Budget: one failing native reproducer, focused
Ruby/bidi/intrinsic checks during implementation, raw Ruby comparisons,
lint/inventory and required Radiant qualification at the stable boundary;
fresh input qualification for shared boundary/measurement changes.

### M12 implementation and focused evidence

Normalized segments now retain their original flattened source windows. For a
sequence of internally unbroken segments, the formatter reserves each paired
column width before committing its base, keeps that unit's parallel levels
together, and contributes annotation spacing before the selected line closes.
Ordinary and emergency break groups cache their total and remaining advances
in linear passes; a fitting word uses an ordinary opportunity before the word
instead of an emergency split within it. The shared column solver,
`ruby_place_segment`, annotation positioning and line-spacing contribution
serve both formatting paths. Segments with internal parallel or forced base
breaks retain the existing partial fragmentation path.

A stack-owned selected-model scope exposes only committed segments on the
current line to line-final annotation alignment. Annotation fragments receive
their base's outer line membership, stopping at independent formatting
contexts. The scope unwinds before its scratch model is released, preserving
**D4.1.4v5 / D4.5.1v4** ownership. Source DOM parents, text and box associations
remain unchanged across retained width changes.

The intrinsic boundary helper is shared with used flow and resolves the common
source style owner. Used emergency seams preserve grapheme boundaries without
changing CSS min-content widths ([CSS Text §5.4–§5.5](https://drafts.csswg.org/css-text-3/#overflow-wrap-property)).
The existing break-candidate recorder also carries legal closing-inline seams
to following normal text, including the candidate's line metrics. A parallel
keep now suppresses soft `<wbr>` opportunities; forced base breaks remain
forced. Text-edge and adjacent-source scans keep the originating annotation
context, so reading text cannot create or preserve a base separator.

An isolated inline's bidi contribution now uses the current line's fragment
bounds and shifts only that line. Shared inline fragment collection and the
existing current-line shift/source-edge helpers supply these operations;
empty inline anchors move with their chosen line. Published single-column
geometry retains alignment space around atomic bases. `display:contents`
contributes no own decoration, and misparented readings retain their anonymous
Ruby advance in ordinary flow.

`RubyUnbrokenSegmentsUseTheirPairedWidthsAndChosenLines` compares grouped Ruby
with separate-unit controls in 12 initial/retained/narrow/wide cases, including
centered lines, over/under readings, unequal heights, explicit levels, arbitrary
roles, direct text, unbroken words and emergency seams. It checks base/reading
rectangles, paint glyphs, source identities, following advances, outer line
membership and intrinsic sizing. `RubyParallelKeepSuppressesSoftWbrWithoutChangingComputedWrapping`
checks an overflowing complete unit with a soft base break and a nowrap
reading. Browser evidence: `temp/ruby-m12-fixture-browser-qualified.json`.

The first broad qualification found a separator regression in
`TerminalSpacesCollapseBeforeEmptyInlineDecorations`: adjacent-source lookup
entered a reading's separate inline context. Its scope is now anchored at the
originating source. The run was stopped after the native failure; it is not
qualification evidence. The focused old isolate/decorated-empty controls also
caught and resolved missing empty-anchor translation and `display:contents`
decoration in the shared fragment query. Qualification below must use the
corrected build.

The prefix control also exposed the difference between an overhanging painted
column and its consumed advance ([CSS Ruby §5.1](https://drafts.csswg.org/css-ruby-1/#ruby-overhang)).
A paired 30px column starts 5px inside the preceding space while consuming only
25px after that space. A line-local `LayoutInlineAdvance` records the chosen
content advance independently of ink bounds. Values live in `pass_arena`, are
cleared by line reset, and follow current-line translation; they do not borrow
the formatter's shorter-lived model. Existing `Linebox` save/restore preserves
this line state (**D4.1.4v5 / D4.5.1v4**). The initial prefix check pins the
browser-observed 35px source origin in addition to comparing equivalent forms.

The corrected native binary passes **456/456** text/query tests; dimension lint
and inventory generation/check pass. Logs: `temp/ruby-m12-native-qualified-final.log`,
`temp/ruby-m12-lint-final.log`, `temp/ruby-m12-inventory-final.log`. Only reference
columns 13/14 change in the generated inventory. The corrected raw Ruby run
remains **33/212** full matches, with no full match lost. Its former
`ruby-whitespace-002-ref` and text-collapse partial losses are resolved.
Logs: `temp/ruby-m12-wpt-qualified-final.log` and
`temp/ruby-m12-wpt-qualified-final-delta.json`.

The still-failing internal-fragment case
`break-within-bases/no-break-opportunity-at-end` has a lower partial score.
Its base formatter splits the base before its reading is available, then the
old global column placement translates already-finalized lines. The former
whole-isolate bidi translation incidentally compensated for part of that
invalid placement; correct current-line translation cannot repair older line
positions. This remains the next coordinated-fragment milestone, not a reason
to move other lines during bidi. It is not claimed as repaired.

A completed aggregate run additionally flagged `wpt-css-text`'s existing
partial `word-space-transform-008`: the WBR guard had incorrectly suppressed
ordinary explicit opportunities in `pre`/`nowrap`. Current Chromium confirms
those opportunities remain active, whereas a parallel Ruby keep suppresses a
base WBR when its reading forbids the split. The guard now queries the shared
physical-flow keep separately from computed wrapping; normal text still uses
both. The native WBR check includes normal/pre/nowrap controls. Probe:
`temp/ruby-wbr-wrap-policy-browser-probe.json`. This corrects the owning policy
without adding fixture/property exceptions or changing baseline scores.
The aggregate with that new policy regression is also not final qualification;
the corrected build must rerun its affected text and aggregate checks.

The block Ruby score review exposed a separate M12 caller error: after
`ruby_finish_block_line`, the context already names the next line. The shared
annotation alignment helper now receives its owning line explicitly; the
block caller captures that line before finalization. The expanded
`NonInlineRubyRetainsItsBlockBoxAndFormatsPairedContent` adds a grid-block Ruby
using normal line-height and checks the annotation envelope and paint baseline
in initial/retained flow. It fails before the repair with an 8px gap:
`temp/ruby-m12-normal-block-before.log`. Existing Ahem/60px-leading cases
retain their coverage. This uses completed fragment ownership rather than a
fresh cursor (**D4.5.1v4**).

### M12 qualification and wrap-up

The final build passes **87/87** focused checks and **456/456** complete native
text/query checks, including the normal-line-height block regression.
Dimension lint, generated inventory/check and whitespace checks pass. Only the
generated inventory's source-reference columns change. Final native evidence:
`temp/ruby-m12-focused-completed-line.log`, `temp/ruby-m12-wrapup-native.log`,
`temp/ruby-m12-lint-completed-line.log`,
`temp/ruby-m12-inventory-completed-line.log`.

After the completed-line repair, all 16 layout baseline categories again
qualify: **4974 full / 300 partial / 0 regressions**, with no skips.
Fresh input executables pass **4594/4621**, with exactly the same 27 failures
as M11, no skips, and no new failures. Logs:
`temp/ruby-m12-wrapup-build.log`, `temp/ruby-m12-wrapup-layout.log`,
`temp/ruby-m12-wrapup-input.log`,
`temp/ruby-m12-input-wrapup-output/test_summary.json`;
`temp/ruby-m12-wrapup-checks.json` records stage exits. The preceding complete
Radiant aggregate (`temp/ruby-m12-radiant-verified.log` and its output summary)
has exactly the 12 UI failures already reproduced in M6 and no new failures;
that aggregate precedes the final block-line repair. The final native, full
layout baseline and fresh input checks above follow that repair.

The final raw Ruby run remains **33/212**, with no full match gained or lost.
The block-related partial-score changes are restored. The only remaining score
delta against M11 is the already-failing within-base punctuation case described
above. Evidence: `temp/ruby-m12-wrapup-wpt.log` and
`temp/ruby-m12-wrapup-wpt-delta.json`. Browser references, comparison tolerances,
upstream fixtures and qualification thresholds are unchanged.

This closes the unbroken-segment core scope. At the user's wrap-up request,
work stops here; coordinated breaks within a base/reading pair and the broader
Ruby suite remain unfinished. No subsequent feature milestone has started.
The minimal next-case probe is retained only under `temp/`.


## M13: finalize annotation content baselines

P1 prerequisite for coordinated fragment layout: annotation levels currently
return from inline flow without vertically aligning their children. The
existing `ruby-align-001` capture has a zero-height annotation child at -5px
while its 8px reading box starts at 16px. The native font-box fixture also
places a zero-height child 45px above its reading under `line-height:0`.
This is an internal baseline error before Ruby column stacking, independent
of browser differences in the final annotation envelope.

[CSS Ruby §3.1](https://drafts.csswg.org/css-ruby-1/#interlinear-layout)
requires the readings to form an annotation-level inline formatting context;
§3.3 excludes annotation/annotation-container line-height from their own box
geometry. Finalize its selected roots with the shared vertical alignment path
before measuring/placing columns. Base-line callbacks must continue to skip
reading boxes belonging to their separate context. Reuse an explicit active
level scope and retain selected-view/style ownership (**D4.5.1v4 / D4.1.4v5**).

Acceptance: inherited, zero and very large annotation leading; zero/tall atomic
children, nested inline children, differing child fonts, computed-role readings,
over/under positions, initial and retained reflow, and the existing text-only
and nested Ruby controls. Verify child baselines using independent ordinary
inline controls and Chromium evidence. Do not replace normative font-box
stacking with browser-specific offsets. Budget: focused native reproducer and
Ruby/inline metrics coverage, raw Ruby scores, dimension lint/inventory, and
one required Radiant aggregate when stable. Internal source-range fragmentation
remains next; no copied/NUL-modified DOM source is introduced.


The baseline regression fails before the change and passes after adding the
shared vertical-alignment phase to the active annotation level. The normal
base-line callback still skips those boxes. Annotation font-box positioning
uses its font content height rather than authored leading, per §3.3.
A placeholder for an empty reading is not a rendered annotation line; only
levels with committed line content enter the finalization phase. This restores
the two dynamic-removal partial scores changed by the initial prototype.

The expanded existing fixture checks baseline-aligned zero/tall atomic
children against their reading's loaded font baseline, including nested and
computed-role readings, unequal child fonts, zero/large leading, over/under,
and initial/retained layout. Empty/decorated-empty controls check that an empty
level adds no line height while a decorated empty reading retains its font
box. A provisional assertion that an undecorated empty reading has zero DOM
height exposed an older independent mismatch: the existing native empty box
retains its font height while Chromium reports zero. That assertion is outside
the baseline-phase acceptance; no existing assertion or browser reference changed.
The empty-level line-height check remains. Evidence: existing empty RT geometry
in the before/after dynamic-removal captures, and
`temp/ruby-m13/font-box-browser-with-empty.json`.


M13 is qualified on main-workspace base `6d4bed9df`. All **474/474** native
text/query tests pass. The complete required Radiant aggregate exits zero:
**9071 passed / 304 partial / 0 failed / 2 skipped** of 9377 checks, including
752 UI checks and all 16 layout baseline categories (**4974 full / 300 partial /
0 regressions**). Dimension lint, generated inventory/check and whitespace
checks pass; inventory changes are test-reference line numbers only.

The raw Ruby suite improves **33 → 36 / 212**: `ruby-align-001`,
`ruby-align-001a`, and `ruby-autohide-003` become full matches. No full match or
partial score decreases; three other partial scores improve. Evidence:
`temp/ruby-m13/{reproducer-before,focused-boundaries-final,radiant}.log`,
`temp/ruby-m13/radiant-output/test_summary.json`,
`temp/ruby-m13/wpt-content-line.log`, `temp/ruby-m13/wpt-delta.json`, and
`temp/ruby-m13/qualification-result.json`. Source hashes stay unchanged through
the gate. The goal remains active with 176 Ruby mismatches; coordinated source
fragments and other recorded limits are not claimed as implemented.


## M14: preserve column allocations through zero-area bounds

P1 shared geometry invariant: a 30px reading paired with a zero-size atomic
base loses its Ruby container's allocated width during bounds recomputation.
The current native probe reports Ruby width 0px and reading width 30px;
Chromium reports 30px for both. The zero-area early return in
`compute_span_bounding_box` skips the existing column-geometry application
used by its other exit paths. [CSS Ruby §3.1.1](https://drafts.csswg.org/css-ruby-1/#interlinear-inline)
sizes each column by its widest content, independent of its base's ink area.

Reuse `layout_apply_ruby_column_geometry` at the missing exit, preserving the
shared selected geometry contract (**D4.5.1v4 / D4.1.4v5**). Acceptance: explicit
and anonymous zero-area bases, zero-area readings, explicit level containers,
following text advance, ordinary zero-area controls, initial/retained reflow
and live atomic-width changes. Check used and intrinsic widths and retain DOM
source identity. Do not change the separate zero-height base-envelope policy.
Budget: one failing native reproducer, focused Ruby/bidi/intrinsic controls,
raw Ruby score comparison, lint/inventory and the required Radiant gate after
this invariant is stable.


Implementation separates the existing natural/content bounds calculation from
one shared column-allocation step. Empty, zero-area and collapsed-content
returns now all reach that step; existing helper calls are consolidated rather
than copied into additional branches. The native reproducer fails before the
repair; all **88** focused Ruby/bidi/wrapping/intrinsic checks pass after it.
Used/intrinsic widths, source identity and following advances survive retained
layout and 60px → 0px → 20px atomic-width mutations.

The browser controls use atomic following text to avoid its optional 5px
end overhang from changing the consumed Ruby advance. These horizontal column
checks do not qualify the separate vertical envelope differences. Saved M13
and current captures of the identical fixture show three zero-area containers
changing from width 0px to 30px, without changing their block extents.
Evidence: `temp/ruby-m14/{columns-before,columns-after,columns-browser-atomic}.json`
and `temp/ruby-m14/{reproducer-before,focused-final}.log`.

The structured raw-suite run remains **36/212** with no full match lost.
`ruby-align-space-around-ref` improves **42.9% → 57.1%** element matches with
100% text retained. No partial score decreases. Evidence:
`temp/ruby-m14/wpt-qualified.log` and `temp/ruby-m14/wpt-delta.json`.
Dimension lint, inventory generation/check and whitespace checks pass; only
reference columns 13/14 change in the inventory.


M14 is qualified on local base `8a7b76f8b`. The complete required Radiant gate
passes: **9072 passed / 304 partial / 0 failed / 2 skipped** of 9378 checks,
including **475/475** text/query checks and **752/752** UI checks. All 16 layout
baseline categories retain **4974 full / 300 partial / 0 regressions**.
Source inputs remain unchanged through qualification. Evidence:
`temp/ruby-m14/radiant.log`, `temp/ruby-m14/radiant-output/test_summary.json`,
and `temp/ruby-m14/qualification-result.json`.

A separate controlled-prefix capture also verifies the same 30px allocation
at x=40px. Atomic text on both sides excludes optional overhang; native and
browser geometry agree. Captures: `temp/ruby-m14/columns-prefix-{native,browser}.json`.
The goal remains active with **176 Ruby mismatches**. Next priority is
coordinated within-base/reading fragments; source-range design findings are
retained in `temp/ruby-m14/next-fragment-plan.json`. Browser references,
upstream fixtures, thresholds and baselines remain unchanged.


## M15: coordinated text fragments within a base/reading pair

P1 common Japanese paragraph flow. `break-within-bases/no-break-opportunity-at-end`
currently wraps the base before its reading is available, then moves already
finalized lines during global column placement. CSS Ruby §3.4/§3.4.2 requires
opportunities inside all parallel boxes and independent alignment of each
chosen fragment; use live line availability and shared tail lookahead so a
following nonbreaking punctuation run stays with the final fragment.

Introduce bounded original-source text flow with append-only selected
TextRects, and collect measured opportunities through the existing intrinsic
scanner and normalizer. Reuse the column solver, placement, line metrics and
selected-model callbacks. Shared helpers retain source indices, whitespace,
clusters and text transformation; no copied/NUL-modified DOM source is used.
Preserve selected snapshots and borrowed scope lifetimes (**D4.5.1v4 /
D4.1.4v5**). The first formatter capability is one inline horizontal LTR segment
with a direct text run in each single-column parallel box, collapsing whitespace
and ordinary interior opportunities. Preserved whitespace, mixed/atomic or forced
content, nested ruby/independent formatting contexts, source floats/positioned
children, generated children, decorated units, block ruby, multiple columns/segments,
vertical/RTL or first-line-styled flow, conditional
hyphens and `overflow-wrap: break-word` contexts retain their existing paths.
This is one flow milestone, not a claim that the remaining Ruby family is done.

Acceptance: the Japanese punctuation case against independently split controls,
wide/narrow and retained flow, different base/reading lengths, nowrap controls,
unchanged source identities and byte ranges, and glyph/range geometry. Budget:
focused source-range and Ruby/bidi/whitespace/intrinsic checks during the edit
loop, raw Ruby score comparison, lint/inventory and required Radiant
qualification on stable production inputs; broaden only for affected consumers.

`LayoutTextSliceScope` now supplies original-source byte intervals to the shared
text scanner. Continuations append rectangles and retain the source node and
indices. The existing intrinsic scanner collects measured ordinary opportunities;
its normalizer optionally maps processed byte boundaries back to source bytes.
Paint and layout share source-aware word-start handling. No source strings are
copied or temporarily terminated.

The Ruby formatter selects approximately proportional cuts using the live line
availability, lays out both sides, and applies the existing column solver before
closing the base line. Shared boundary/tail lookahead reserves a forbidden
following run. Collapsed separators are trimmed in every selected parallel
fragment before alignment, retaining their source ownership. Selected-line
bounds and shifts exclude rectangles on completed lines; allocated advances live
on each rectangle rather than in a single whole-box column offset.

A scalar selected-view flag on the Ruby root lets the final line callback rebuild
and select the relevant line. Flow snapshots capture that flag, while prefix
plans and borrowed source scopes remain in scratch/stack storage
(**D4.5.1v4 / D4.1.4v5**).

M15's inline plain-text milestone is implemented. The original
`no-break-opportunity-at-end` case now matches the browser. Native controls cover
source identity/byte intervals, range and painted glyph geometry, retained
wide/narrow reflow, three successive lines and repeated leading/interior/trailing
collapsed separators. Independent browser captures agree on 88 element geometry
coordinates/dimensions. Unsupported formatting protocols retain their prior flow;
Ruby support remains partial.

Qualified on unchanged production inputs:

- Raw Ruby: **37/212 full matches**, up from 36; **175 mismatches** remain.
  No lost full matches or partial-score changes.
- Required Radiant gate: **9,380 total**, **9,074 passed**, **304 partial**,
  **0 failed**, **2 skipped**. Native text/query **477/477**, UI **752/752**;
  layout **4,974 full + 300 partial**, **0 regressions**.
- Dimension lint, diff checks and CSS inventory checks pass. Inventory remains
  421 registered names/IDs; only source/reference columns changed.
- Ancillary module-boundary validation still rejects six deferred imports
  (`view_get_client_matrix`, `doc_state_request_repaint`,
  `view_geometry_block_viewport_origin`,
  `editing_geometry_text_control_for_each_selection_rect`,
  `dom_boundary_from_source_pos`, `radiant_dom_m4b_src_get`). Strict validation
  DSOs link. The associated source files are unchanged by M15; the allowlist is
  unchanged.
- A fresh full input build stops at `test_input_pdf_gtest`: its local static
  `read_binary_file` redeclares the external `lib/file.h` function under
  `-fms-extensions`, producing a duplicate symbol with `lib/file.c`. Those files
  are unchanged by M15. No fresh full input pass is claimed.

Evidence: `temp/ruby-m15/qualification-result.json`, `source-inputs.json`,
`radiant-stable.log`, `radiant-stable-output/test_summary.json`, `wpt-stable.log`,
`browser-comparison.json`, `boundary-final.log` and `build-input.log` under the
same task directory. `lambda-qualified.exe` retains the tested CLI. Earlier
interrupted qualification logs are not the aggregate evidence above.

The goal remains active. Next core priority is coordinated text fragments across
multiple simple segments, reusing between-base group/boundary policy and the
selected-line protocol. The finite next plan is retained in
`temp/ruby-m15/next-fragment-plan.json`. Browser references, upstream WPT fixtures,
thresholds and baseline floors remain unchanged.


## M16: coordinated breaks across inline text runs

P1: the failing `break-within-bases/basic` and `nowrap` documents contain several
ordinary inline text runs in one base/reading pair. Ahem loads with the expected
50px advances, ruling out the font/asset hypothesis. The base wraps before the
reading is available, and whole-box placement subsequently moves completed
lines. Extend M15's source-backed fragments across those inline sources before
extending multiple Ruby segments; this follows the observed failures rather
than the earlier tentative segment order.

Keep ordinary break selection and source offsets in the shared text scanner.
Compose its plans with the existing common-owner inline boundary policy, and
flow only the selected source intervals through the existing Ruby level-child
hook. Reuse selected-line bounds, alignment, column placement and line metrics.
Clear retained source views once before rebuilding selected fragments; never
reparent DOM nodes or concatenate copied text for layout. Scratch plans and
borrowed scopes preserve **D4.5.1v4 / D4.1.4v5**. CSS Ruby §3.4.2 permits an
interior split only when every parallel box permits it, including seams between
nowrap child spans whose common owner permits wrapping.

Acceptance: normal child spans with a cut inside a later source; nowrap child
spans with coordinated seam breaks; three lines with wider readings; retained
wide/narrow flow, original source parents/identities, byte intervals, glyph and
range geometry; the original M15 punctuation case; and the imported basic/nowrap
families. Verification budget: focused native controls and minimal browser
captures in the edit loop, raw Ruby delta and relevant wrapping/whitespace/bidi
neighbors, then one required Radiant gate on stable inputs plus lint/inventory.
Multiple Ruby segments, mixed/atomic/decorated/generated content, conditional
breaks and preserved-space protocols remain separate work; do not mask failures
or claim the complete suite is supported.

M16 implements composed opportunity plans for plain inline text leaves within
one parallel box. The existing scanner supplies normalized-to-source offsets,
leading separators and terminal advances; the common-owner boundary helper
supplies seams. Intrinsic sizing and used fragment selection share that plan,
so duplicate span-edge spaces collapse with the surviving source's font.
Selected paths flow through the existing level-child hook; unselected sources
stay absent from the current line. Retained views reset once before emission,
and original DOM parents, text storage and byte intervals remain intact.
Break elements still require the full-flow protocol: annotation `<br>` produces
a separator that is absent from a plain text-leaf plan.

Two shared causes also needed repair. Inherited `overflow-wrap` and `word-break`
now resolve independently through the existing declaration-aware helper,
including CSS-wide and substituted values. Ruby lookahead stops once its first
parallel opportunity fits, rather than treating nowrap descendants as one
unbroken sequence and probing into the following suffix. Tail fitting starts
from the live Ruby container while selected descendant views are not yet built.

Six focused checks pass, including the previous M15 cases and annotation break
normalization. The complete text/query gate passes **480/480**. Independent
Chromium captures match **504** row/source-rectangle coordinates for the new
fixtures. Native controls also verify initial/retained flow, wide/narrow CSSOM
changes, mixed source fonts, nowrap seams, glyph positions and character ranges.
Evidence: `temp/ruby-m16/focused-5.log`, `browser-comparison.json`, and the native
and browser `*-final.json` captures.

The unchanged raw WPT comparison remains **37/212** full matches; no full match
is lost. `nowrap` element matches improve **47.1% → 58.8%**. `basic` and
`basic-ref` text matches decrease **5.6% → 0%**: the initial prefix now shares
the correctly split base line, exposing the existing 25px first-line leading
shift. In the ordinary `basic` paragraph all horizontal cuts match the browser,
but its height is 150px versus 125px. Multi-column placement is also incomplete.
These documents remain failed; the milestone does not claim whole-document
conformance. Evidence: `temp/ruby-m16/{wpt-3.log,delta-3.json,basic-final.json}`.

Next priority is the shared Ruby/line-spacing metric owner under CSS Ruby §3.6,
with baseline and block-extents checked before extending multi-segment or
multi-column fragmentation. The finite plan is
`temp/ruby-m16/next-milestone.json`. References, upstream WPT documents,
comparison thresholds and baseline floors remain unchanged. The goal stays
active with 175 mismatches.

M16 is qualified on base `c3405c2a3`. The required Radiant gate passes:
**9077 passed / 304 partial / 0 failed / 2 skipped** of 9383 checks, including
**480/480** text/query and **752/752** UI checks. Layout baselines retain
**4974 full / 300 partial / 0 regressions**. Dimension lint, inventory check
and whitespace checks pass; only inventory reference columns 14/15 change.
Frozen production/test inputs remain unchanged through qualification.
Evidence: `temp/ruby-m16/radiant.log`,
`temp/ruby-m16/radiant-output/test_summary.json`, `source-inputs.json`, and
`qualification-result.json` in the same task directory.

The fresh module-boundary build links the strict DSOs, then rejects the same
six pre-existing deferred imports recorded by M15 (140 current imports,
165-entry baseline). No boundary allowlist changes are included. The unrelated
input/PDF duplicate-symbol build limit from M15 was not rerun or changed.
See `temp/ruby-m16/boundary.log` and `qualification-result.json`; neither
ancillary gate is claimed as passing.

## M17: Ruby annotations use available block-start space

P1: after M16 fixes the horizontal cuts in `break-within-bases/basic`, the
ordinary first paragraph is 150px tall versus Chromium's 125px; its first base
line is shifted down by the 25px reading height. Trace `ruby_contribute_line_metrics`,
generic baseline alignment and line advancement. CSS Ruby §3.6 separates
ordinary base alignment from additional spacing needed to keep repeated Ruby
lines apart, and permits annotations outside their line box. Preserve selected
view/style ownership and stack/scratch lifetimes (**D4.5.1v4 / D4.1.4v5**).

Acceptance: minimal Ahem over/under Ruby lines with preceding text, three-line
spacing, plain neighboring lines, ample leading, and initial/retained/live
line-height changes; compare baseline and block extents with independent browser
captures. Recheck the original ordinary/indented WPT paragraphs. Multi-column
fragmentation and multi-segment text splitting stay separate work. Budget:
focused native/browser geometry during diagnosis, affected Ruby/line-height
neighbors and raw Ruby delta, then one required Radiant gate on stable inputs
and dimension lint/inventory. Do not encode first-line offsets or fixture guards.

Minimal browser controls narrowed the cause to available block-start space,
not fragmentation itself. With no margin/padding, an over-annotation reserves
leading normally; a 20px top margin or padding can host the same 10px reading
without raising its base. The original WPT's 50px collapsed paragraph margin
similarly hosts its 25px reading. [Chromium's June 2026 annotation-space change](https://chromium.googlesource.com/external/github.com/web-platform-tests/wpt/+/4a010dd92dac7b0cf8220edde084718403b67a5a)
confirms that policy; [CSS Ruby §3.6](https://drafts.csswg.org/css-ruby-1/#line-height) permits annotations to overflow line boxes.
Carry the available space in the transient block context, initialized by block
placement from padding and resolved adjoining margins. Borders and clipping
bound access to outside margins. Consume the space only for the first formatted
line's over-leading; retain the ordinary repeated-line metrics otherwise.
Evidence: `temp/ruby-m17/spacing-browser-3.json`,
`basic-browser-live.json`, and the failing `reproducer-before.log`.


M17's horizontal block-start-space path is implemented. The regression covers
ordinary over/under and ample-leading lines, coordinated source fragments,
positive/negative collapsed margins, live line-height changes, nested adjoining
margins, padding, border/overflow limits and independent inline-block/flow-root
contexts. All **51** Ruby/leading/margin neighbors pass. Independent browser
captures match **860** minimal-fixture coordinates. The ordinary and indented
paragraphs of the original `basic` WPT match all **56** paragraph/text coordinates;
its multi-column paragraph remains incorrect. Evidence:
`temp/ruby-m17/neighbors-1.log`, `browser-comparison.json`,
`basic-paragraph-comparison.json`, and `spacing-browser-4.json`.

Raw WPT results improve **37 → 51/212** full matches, with **14** new matches
and no full match lost. Both `nowrap` documents now fully match. `basic` improves
to 78.9% element / 75% span / 72.2% text matches; `basic-ref` improves to
83.3% / 75% / 72.2%. Three partial scores decrease: `block-ruby-005`,
`pseudo-first-line` and `ruby-float-handling-001`. The latter captures still
show unsupported annotation-float placement; the first-line case still places
its reading above the first line despite the `::first-line` under-position rule.
Do not treat those documents as passing. Evidence:
`temp/ruby-m17/{wpt-2.log,delta-2.json,pseudo-after.json,floats-after.json}`.
No browser reference, upstream WPT document, comparator threshold or baseline
floor is changed. Vertical annotation-space transfer, block-end padding space,
multi-column fragmentation and remaining conditional styling/float behavior
remain partial. Required aggregate qualification is recorded below.


M17 is qualified on base `0d8c749dc`. The required Radiant gate reports
**9077 passed / 305 partial / 0 failed / 2 skipped** of 9384 checks, including
**481/481** text/query and **752/752** UI checks. Layout baseline floors pass
with **4973 full / 301 partial / 0 regressions**. One CSS Text case,
`letter-spacing-212`, changes from full to partial: its saved reference expects
30px Ruby lines while current Chromium and this implementation both use 20px
lines after borrowing collapsed margin space. A focused predecessor comparison
isolates that document; all **44** relevant current-browser element coordinates
match. The saved reference remains unchanged. Evidence:
`temp/ruby-m17/{text-ruby-delta.json,letter-spacing-browser-comparison.json}`.

Dimension lint, inventory and whitespace checks pass. The 421-row inventory
changes only reference columns 14/15. Frozen source/test inputs remain unchanged
through qualification. The fresh strict module DSOs build, then boundary
validation rejects the same six pre-existing deferred imports (140 current,
165-entry baseline); no allowlist change is included. The unrelated input/PDF
symbol build failure from M15 was not rerun or changed. Aggregate and boundary
evidence: `temp/ruby-m17/radiant.log`,
`temp/ruby-m17/radiant-output/test_summary.json`, `boundary.log`,
`source-inputs.json`, and `qualification-result.json` in the same task directory.

The goal remains active with **161 Ruby mismatches**. Next priority is paired
Ruby projection through CSS columns, starting with the remaining middle
paragraph of the original `basic` WPT. The source-line/annotation ownership
findings and finite acceptance plan are in `temp/ruby-m17/next-milestone.json`.

## M18: completed outer lines own column projection

P1: the remaining middle paragraph of `break-within-bases/basic` counts every
base text rectangle as a separate line during column distribution, and leaves
readings at their original positions. CSS Ruby §3.4 keeps annotations paired
with their bases; CSS Break §4.1 breaks outer inline flow between line boxes.
Column projection must consume completed line geometry rather than infer it
from the top edges of glyph runs or annotation-level content.

Capture completed outer line boxes in a borrowed stack scope while the multicol
formats its narrow first pass. Nested formatting contexts cannot append to the
outer scope. Reuse source line membership and the generic selected-line shift
to project all base/readings rectangles once. Keep this transient data out of
retained views (**D4.5.1v4 / D4.1.4v5**). Share existing fragmentation/bounds
helpers and computed Ruby role checks; do not introduce tag-specific traversal.

Acceptance: original basic/basic-ref, multiple text runs on one line, a source
split across columns, CSS display Ruby roles, an unsplit neighboring Ruby, and
initial/retained plus column-count/width mutations with unchanged source parents,
byte intervals, glyph and range geometry. Begin horizontal inline-only column
flow; mixed block-flow, vertical/RTL and independent fragmentation protocols
remain separately tracked. Budget: focused native/browser controls, Ruby and
multicol neighbors, unchanged-reference Ruby delta, then one stable required
Radiant gate, lint/inventory and boundary validation for the new borrowed scope.


The original `basic` and `basic-ref` documents now fully match. Capture is
scoped to the outer IFC during the narrow formatting pass; nested contexts
cannot become outer lines, and records do not escape into retained views.
Column planning uses those records' actual start/extent instead of counting
source rectangles or estimating Ruby leading from glyph tops. The existing
selected-line shift projects all parallel text and atomic content together.
The recorded line list removes the old 512-rectangle truncation for this path.
When a fragment cannot hold the requested orphan lines, the break policy uses
CSS Break §4.4's relaxation rather than forcing a whole paragraph to overflow.

Six focused source/spacing/column checks and 66 Ruby/multicol neighbors pass.
The raw Ruby suite improves **51 → 53/212**, gaining `basic` and `basic-ref`,
with no lost full match or partial-score change. Independent minimal-fixture
captures match 424 coordinates. Evidence: `temp/ruby-m18/focused-2.log`,
`neighbors-1.log`, `wpt-1.log`, `delta-1.json`, and `browser-comparison.json`.

The neighboring column baseline exposed a separate cursor-frame requirement:
`multicol-nested-006` keeps BR cursors in local inline coordinates while text
moves to later columns. Current Chromium confirms the saved reference.
The generic fragment shift now lets its column caller retain that paragraph
frame: its first-column inline origin is zero for LTR, and the rightmost column
for RTL. A live `getclientrects-001` capture confirms that RTL leading BR remains
at the paragraph's right edge. Normal inline shifts apply one translation to
all content. No baseline/reference change is included. The neighbor gate is
rerun after this shared correction.

M18 is qualified on main-workspace base `2c42c6514`. The final raw comparison
retains **53/212 full matches**, **159 mismatches**, and no partial-score
regression. The required Radiant gate passes **9,078 passed / 305 partial /
0 failed / 2 skipped** of 9,385 checks, including **482/482** native text/query
and **752/752** UI checks. Layout baselines retain **4,973 full / 301 partial /
0 regressions**; the multicol category retains **334 full / 28 partial**.
The source hashes and candidate CLI remain unchanged through qualification.
Dimension lint, whitespace checks and the generated CSS inventory check pass;
the inventory retains 421 names/IDs with changes only in reference columns
14/15. Evidence: `temp/ruby-m18/radiant.log`,
`radiant-output/test_summary.json`, `source-inputs.json`, `neighbors-3.log`,
`wpt-2.log`, `delta-2.json`, `browser-comparison.json`, and
`qualification-result.json` in the same task directory.

Fresh module-boundary validation links the strict DSOs, then rejects the same
six pre-existing deferred imports recorded by M15–M17 (140 current imports,
165-entry baseline). The allowlist remains unchanged. The unrelated input/PDF
duplicate-symbol build limit from M15 is unchanged and was not rerun.
See `temp/ruby-m18/boundary.log`; neither ancillary gate is claimed as passing.

The goal remains active. Coordinated fragments across multiple Ruby segments,
mixed/atomic and preserved-space source protocols, and vertical/RTL Ruby flow
remain open. The next finite investigation is retained in
`temp/ruby-m18/next-milestone.json`. Browser references, upstream WPT documents,
comparison thresholds and baseline floors remain unchanged.

## M19: shared inline seams retain Unicode opportunities

P1 prerequisite found while investigating multi-segment flow: the imported
`ruby-line-breaking-001` puts seven source boxes on its second overflowing line,
where current Chromium ends that line after `に、`. The same boundary mechanism
is used by ordinary source spans. The text-node preflight recognizes only
ID-to-ID adjacency, missing legal breaks after closing punctuation and at mixed
Latin/ideographic seams. CSS Ruby §3.4.1 uses ordinary inline base boundaries;
CSS Text §5/§5.5 requires Unicode opportunities across those source boundaries.

Repair the shared preflight with the existing Unicode seam helper and common
source-owner policy. Do not add Ruby/tag-specific packing or change source DOM,
selected-view ownership (**D4.1.4v5 / D4.5.1v4**) or browser references.
Multi-segment parallel placement remains the next independent flow protocol.

Acceptance: the original punctuation document; split ordinary and base-only
Ruby sources versus unsplit text; closing/opening punctuation and mixed Latin/
ideographic seams; nowrap, keep-all and a nowrap common owner with normal
children; initial/retained flow and wide/narrow mutations, source identities
and glyph geometry. Budget: prove the focused native failure, shared wrapping/
Ruby neighbors and live browser geometry, raw Ruby delta, then one stable
required Radiant gate and lint/inventory. General parallel-segment wrapping
and independent intrinsic-size disagreements remain open.

The focused test fails before the seam repair: the split punctuation row uses
240px instead of the unsplit control's 360px. Reusing the common-owner Unicode
helper repairs all its source glyph positions. The original Ruby shape also
contains an empty misparented reading: the existing contribution path adds its
inherited leading despite having no annotation line. Reuse the existing empty
strut classifier to exclude that absent annotation-line contribution, consistent
with CSS Ruby §3.3. Keep nonempty and decorated annotation behavior unchanged.

The original WPT additionally disables kerning. Its preflight incorrectly reads
the kerning pair's previous codepoint, which is conditional on kerning and can
be cleared by a font change. Read the existing unconditional text-character
state instead; combined text publishes its outer edge there too. The focused
fixture now disables kerning initially and mutates it with the row width, so
the original trigger is covered. No new retained payload or scratch ownership
is introduced.

The 42 focused Ruby/wrapping/punctuation checks pass. The raw suite retains
**53/212 full matches** and improves 12 partial documents, with no lost full
match or partial-score decrease. `ruby-line-breaking-001` improves from
33.3% to **100% text** and 23.5% to **88.2% elements**; its empty RTC/RT element
boxes remain different from the browser, so this is not a whole-document pass.
Dynamic insertion/removal cases also gain correct line contributions from the
empty-strut classification. Evidence: `temp/ruby-m19/neighbors-2.log`,
`wpt-1.log` and `delta-1.json`.

The eight comparable fixture rows and their controls match **276** live-browser
coordinates for row sizes and source text rectangles relative to their row.
The common-owner control intentionally requires 60px while Chromium uses
120px: CSS Text §5.5 assigns the seam's white-space to the nearest common
ancestor, which is nowrap here. Keep this normative assertion and the browser
capture separate; neither references nor tolerances change. Empty annotation
element boxes are outside those source-coordinate checks. Chromium computes
the misparented RT as inline at the parent's font size; HTML §15.3.4 specifies
the RT ruby-text UA role, which remains the engine's computed role.
Evidence: `temp/ruby-m19/browser-comparison.json`,
`seams-{browser,native}-final.json`,
[CSS Text §5.5](https://www.w3.org/TR/css-text-3/#line-break-details),
and [HTML phrasing defaults](https://html.spec.whatwg.org/multipage/rendering.html#phrasing-content-3).

The first aggregate exposes two new Text baseline regressions,
`trailing-ideographic-space-013/014`: an emergency seam before U+3000 bypasses
its required hanging placement. Share the existing line-end space policy
between preflight and glyph overflow/accumulation, so CSS Text §4.1.3 hanging
still precedes overflow-wrap. Add both emergency modes to the existing mixed
hanging-space regression without altering those upstream documents or floors.
The aggregate also has four UI child exits with code 137 and no machine result,
while another worktree runs UI tests concurrently; their attribution remains
pending isolated replay. Neither this failed aggregate nor that concurrency
observation proves successful qualification.

The corrected 013/014 documents fully match their unchanged references on both
the saved M18 CLI and the new candidate. All 43 focused checks pass, including
the added native full-width hanging character Range controls. The complete
Text category returns to **894 full / 86 partial / 0 regressions**. The final
review keeps preserved ASCII/native-whitespace handling separate from the
shared ideographic-space predicate, preserving its existing policy.

All four UI fixtures pass three isolated repetitions on each of the saved M18
and corrected M19 binaries with four jobs. This does not attribute the earlier
code-137 exits or turn the first aggregate green. Retain that aggregate and
those controlled replays separately, and run the required aggregate again on
the final frozen inputs after the hanging-space correction. Evidence:
`temp/ruby-m19/space-probe.log`, `neighbors-4.log`, `text-probe-2.log`,
`ui-{base,candidate}-{1,2,3}.{log,json,status}`, and `ui-control-inputs.json`.

M19 is qualified on main-workspace base `52c1ff264`. The fresh required Radiant
aggregate passes **9,079 passed / 305 partial / 0 failed / 2 skipped** of 9,386
checks, including **483/483** native text/query and **752/752** UI checks.
Layout baselines retain **4,973 full / 301 partial / 0 regressions**, including
the corrected Text category. Final production/test hashes and the candidate
CLI remain unchanged through this aggregate. Dimension lint, whitespace and
inventory checks pass; the inventory retains 421 names/IDs and changes only
reference columns 14/15. Final Ruby results retain **53 full / 159 mismatches**
with 12 partial documents improved and no score regression.

Evidence: `temp/ruby-m19/radiant-stable.log`,
`radiant-stable-output/test_summary.json`, `source-inputs-final.json`,
`neighbors-final.log`, `wpt-final.log`, `delta-final.json`,
`space-probe-final.log`, `browser-comparison.json` and
`qualification-result.json`. The earlier failed aggregate remains in
`radiant.log` and `radiant-output/`; it is not the final qualification.
No module API or ownership layout changes in M19, so boundary validation was
not repeated. M18's six deferred-import violations and M15's unrelated
input/PDF build limit remain documented; no fresh pass of either is claimed.

The goal remains active. A fresh three-segment probe still shifts the long
base's already-completed fragment and leaves its reading unsplit. The next
flow milestone is anchored to that failure in
`temp/ruby-m19/next-milestone.json`, reusing common emission and break-group
policy. References, upstream documents, tolerances and baseline floors remain
unchanged.

## M20: coordinated fragments share flow with neighboring segments

P1 common annotated-paragraph flow. The verified three-segment probe formats
the long base before the parallel reading, then moves its already completed
rectangles as a whole. Source identity is intact, but the first base fragment
starts at 40px instead of 20px and the reading remains on the first line.
The selected-source formatter accepts only one segment with interior breaks
in every level, while the whole-unit formatter rejects a model containing
any such break. Mixed models therefore lose the chosen-line protocol.

Prepare applicability before emitting sources. Compose breakable text pairs
with indivisible neighbors, sharing the existing column, annotation-baseline,
placement and line-contribution stages instead of adding another formatter
copy. Reuse base seam policy and linear cached break groups, including the
first legal paired prefix and any forbidden following tail. CSS Ruby §3.4.1
and §3.4.2 require paired levels and independent alignment of each fragment.
Plans and normalized boxes remain scratch-owned; selected outputs and borrowed
callbacks retain **D4.5.1v4 / D4.1.4v5**.

Acceptance: original short/long/short failure, unequal-width neighbor readings,
ordinary Japanese seams and forbidden Latin seams/tails; initial/retained and
wide/narrow flow; original source parents, data, glyph and character-range
geometry against independent explicitly split controls. Begin plain horizontal
LTR inline one-column segments. Mixed/atomic/generated/decorated, conditional
or preserved-space protocols, block Ruby and vertical/RTL flow remain separate.
Budget: focused native reproducer/browser controls and existing Ruby/source
neighbors, unchanged-reference raw Ruby delta, then one stable required Radiant
gate and dimension lint/inventory; boundary validation only for API/ownership
changes. The 159 remaining raw mismatches are not an accepted baseline.

The formatter now prepares every participating text level before clearing any
selected output. One loop commits eligible fragments and indivisible neighbors;
the existing whole-unit and selected-source paths share column resolution,
baseline placement and line contribution. Cached forbidden groups stop at the
first legal paired prefix, so a final piece reserves its following tail without
rescanning the whole model. Unsupported models still use their existing flow
protocol before any source is emitted.

The Latin control exposed a second shared defect: isolate-edge trimming read
raw source bytes after CSS Text §4.1.3 had already suppressed the fragment's
separator. Alignment first allocated 40px correctly, then a later isolate trim
removed another 10px. Both the trim and Ruby Range edge resolver now use the
existing painted-source interval. Suppressed reading separators share the
visible end's allocated column edge; original source lengths and parents stay
intact. Temporary trace logging was removed.

Focused verification passes all 33 selected Ruby/isolate/whitespace neighbors,
including initial/retained and wide/narrow paint and per-character Range checks.
The five actual/control row pairs match Chromium on all 356 checked coordinates
(0px maximum delta). The unchanged-reference imported suite remains **53/212
full matches**, with no partial-score changes or regressions. These imported
documents do not exercise the selected mixed-segment case; this milestone
repairs the independently reproduced common flow without claiming a WPT gain.
The selected-source path retains M15's deferred overhang scope.

The stable required Radiant gate passes: **9387 checks; 9080 passed, 305 partial,
0 failed, 2 skipped**. Native text/layout passes 484/484 and UI 752/752. Layout
retains **4973 full / 301 partial / 0 regressions**; CSS Text retains **894 full /
86 partial / 0 regressions**. Dimension lint, diff and generated-inventory checks
pass; the inventory still has 421 names/IDs, with changes only in reference
columns 14/15. All six frozen source/test inputs and the qualified CLI hash
remain unchanged. No module API, import or ownership placement changed, so the
previous boundary audit was not repeated. Qualification:
`temp/ruby-m20/qualification-result.json`.

The M20 core is complete; the goal remains active. The next investigation is
an existing RTL atomic-reading defect in `ruby-bidi-003-ref`: its text acquires
an x coordinate of `-FLT_MAX`. The same explicit-font probe reproduces this
with both qualified M19 and M20, while ordinary text has matching advances.
Trace the independent annotation line's unbounded edge and its RTL/atomic
placement before modifying the owning stage; do not clamp the output. The
finite diagnostic plan and controls are retained in
`temp/ruby-m20/next-milestone.json`. References, upstream fixtures, comparison
tolerances and baseline floors remain unchanged.

## M21: finite coordinates for independent RTL Ruby content

P1 shared placement defect. `ruby-bidi-003-ref` and an explicit-font control
publish an atomic reading at `-FLT_MAX`, with the base losing its right-side
placement. Both the qualified M19 and M20 executables reproduce this. Ordinary
text in the explicit ProbeSerif control agrees with Chromium on advance, so
the separate default-font disagreement is not evidence for this cause.

Trace source flow, the independent annotation line, bidi placement and column
translation to locate the first invalid coordinate before editing the owning
algorithm. The current annotation line uses an unbounded right edge; verify
which consumer treats that edge as a finite origin. Reuse existing measurement
and coordinate-frame services. Do not clamp the final coordinates or substitute
fixture offsets. CSS Ruby §2.1.1 / §3.1 / §3.5 retain independent annotation
contexts and paired columns. Scratch and selected-view ownership remain
**D4.5.1v4 / D4.1.4v5**.

Acceptance: the imported reference and horizontal LTR/RTL ordinary, direct-text
and inline-block base/reading controls; initial/retained and direction/width
mutations; finite right alignment, source identities, paint and character Range
geometry. Begin the shared atomic-reading placement path; defer independent
default-font metrics, vertical flow and optional overhang refinements.
Budget: focused failing native/browser controls, relevant bidi/Ruby neighbors
and unchanged-reference Ruby scores, then one stable required Radiant gate
plus dimension lint/inventory. Run boundary validation for actual module API,
import or ownership changes. The raw Ruby failures remain open.

The trace confirms two ownership errors. Atomic RTL placement uses the
annotation's `FLT_MAX` edge as a finite alignment origin, losing its width to
float precision. Column translation then moves both the atomic box and its
local text. The annotation line now uses its measured column frame and an
explicit keep scope for the selected parallel interval. Whole-flow translation
shares the fragment shifter, preserves inline font boxes and fragment unions,
and stops at independent atomic coordinate frames. Single-line shifts retain
their previous selection and bounds-recomputation behavior.

Qualification exposed a related UA-style prerequisite: atomic reading children
inherited the paragraph's leading. The shared HTML default table now supplies
`rt` with local `normal` leading, also serialized into secondary-view UA CSS.
This is a browser-compatible UA policy following the annotation-local leading
intent of CSS Ruby Appendix A.1; its sample value `1` is informative. Explicit
author leading, inheritance, font shorthand and resets still take precedence.
`all:initial` now materializes initial normal leading so a descendant cannot
fall through its parent to an older ancestor. No view-specific property copy
or source mutation is introduced (**D4.5.1v4 / D4.1.4v5**).

The earlier forced-break regression's atomic reading now expects one 20px line
of normal Ahem leading, replacing its previous 100px inherited paragraph line.
Its width/source assertions remain strict. CSS Ruby §2.2 step 7 still suppresses
annotation segment breaks inside independent descendants; Chromium's two-line
40px result for that authored control is recorded without copying it.

All 66 focused Ruby/UA/bidi/fragmentation neighbors pass, including source,
paint and per-character Range checks through retained direction/width edits.
The raw suite reaches **55/212 full matches**: `ruby-bidi-003-ref` and
`ruby-text-combine-upright-002-ref` become full matches, with none lost. Four
other partial documents improve. `ruby-align-space-around-ref` text drops from
100% to 50%: the old invalid local translation canceled an existing empty
zero-font-base anchor mismatch. Atomic height and local text origin now agree
with the ordinary-box control, but the parent annotation remains at 104px
instead of 88px in the first example. That separate parent-anchor mismatch
stays open; the raw failure and unchanged reference retain it. Evidence:
`temp/ruby-m21/geometry-trace-1.log`, `geometry-trace-2.log`,
`space-ref-native-ua.json`, `space-ref-browser.json`, and `delta-ua-2.json`.

The explicit-font browser comparison covers 160 row/source coordinates. All
but one agree exactly: Chromium leaves a 10px block-axis gap for the mixed
direct-base/atomic-reading RTL row, while native controls keep the stacked
containers adjacent under CSS Ruby §3.1.2. The difference remains visible in
`temp/ruby-m21/browser-comparison.json`. Zero-leading atomic boxes agree on
local text origins; that check does not claim their parent anchors agree.

The stable required Radiant gate passes: **9389 checks; 9082 passed, 305 partial,
0 failed, 2 skipped**. Native text/layout passes 486/486 and UI 752/752. Layout
retains **4973 full / 301 partial / 0 regressions**; CSS Text retains **894 full /
86 partial / 0 regressions**. Dimension lint, diff and generated-inventory checks
pass; all 421 names/IDs remain, with changes only in reference columns 13–15.
All eight frozen source/test inputs and the qualified CLI hash remain unchanged,
so the stable raw Ruby run applies to this qualified candidate without replay.
Qualification: `temp/ruby-m21/qualification-result.json`.

The strict module DSOs link; the boundary checker rejects the same six deferred
imports recorded by M18, with no additions or allowlist edits. That audit remains
a qualification limit, not a passing gate. The unrelated M15 input/PDF build
limit was not rerun. Evidence: `temp/ruby-m21/boundary-comparison.json` and
`boundary.log`.

M21's coordinate-ownership core is complete; the goal remains active with 157
raw Ruby mismatches. The next diagnostic milestone verifies an embedded CJK
Ruby min-content discrepancy against current Chromium and explicit-font controls
before assigning its owning stage. That common shrink-to-fit flow precedes the
rare empty-zero-font parent-anchor tail. The saved comparison and finite plan
are in `temp/ruby-m21/intrinsic-embedding-saved-comparison.json` and
`next-milestone.json`. References, upstream fixtures, comparison tolerances and
baseline floors remain unchanged.

## M22: paired flow across columns in a shared segment

The planned intrinsic investigation confirms the saved 18px/34px discrepancy
in current Chromium. Explicit-font controls isolate it to min-content sizing:
at a fixed one-character width, Chromium wraps the same embedded base-only
Ruby without overflow, exactly like native flow. Native retains the smaller
minimum allowed by those opportunities; do not inflate widths to reproduce
the browser's inconsistent intrinsic result. Evidence:
`temp/ruby-m22/{intrinsic-original-browser,intrinsic-probe-browser,fixed-width-browser}.json`.

P1 shared pairing defect: the coordinated source formatter rejects segments
with multiple columns. Its fallback emits all base sources before annotation
placement, allowing bases to wrap independently from their readings. Extend
the existing formatter with scratch-owned per-column units, preserving the
normalized model for intrinsic measurement and source parents/data under
**D4.5.1v4 / D4.1.4v5**. CSS Ruby §3.4 requires paired levels to fragment
together; §3.4.1 retains the base text's real seam opportunities.

Acceptance: complete non-spanning columns with text breaks or indivisible
readings, anonymous/explicit level containers, multiple annotation levels and
ordinary neighbors; compare source paint and character Range geometry with
independently segmented controls through initial/retained and wide/narrow
reflow. Begin horizontal LTR text flow. Spanning/missing columns, generated or
decorated sources, vertical/RTL and forced-break protocols remain separate.
Budget: failing focused controls, existing Ruby/intrinsic/fragment neighbors,
unchanged-reference raw Ruby delta, then one stable required Radiant gate and
dimension lint/inventory. No module API/import change is planned.

The formatter now prepares complete, non-spanning columns as scratch-owned
flow units and uses its existing fragment-selection and placement loop. Each
unit borrows only its own source range; source identities, parents and text
remain intact. Shared annotation-container baseline alignment excludes already
placed sibling columns. Fully collapsed units emit no line or placement and
cannot translate the shared container of earlier visible readings (CSS Ruby
§2.4). The trace isolates an unwanted 80px translation before the fix:
`temp/ruby-m22/hidden-translation-stages.log`.

Independent browser controls exposed a related selected-interval defect. The
leading probe correctly chose 40px at x=40 in a 90px line, but wrapper pre-wrap
and the text scanner's nowrap restart still made their own decisions. A selected
`AA ` interval temporarily advanced 60px before its trailing separator was
trimmed. Both paths now respect the existing IFC-scoped keep constraint, so the
formatter's chosen interval stays together; independent atomic contexts retain
their own wrapping. Evidence: `temp/ruby-m22/{leading-stages,emission-stages,kept-overflow-stages}.log`.
Temporary trace logging was removed.

All 100 Ruby/intrinsic/bidi/fragmentation neighbors pass. The regressions compare
paint, every character Range, source ownership and min/max-content through
initial/retained and wide/narrow flow, including multiple levels, collapsed
readings and a fitting ordinary prefix. The supported independently segmented
browser controls agree on all 86 checked coordinates, with 0px maximum delta
under the harness's font policy. Chromium rejects `ruby-base`,
`ruby-base-container` and `ruby-text-container`, so explicit-level role behavior
is verified natively rather than claimed as a whole-fixture browser match.
The unchanged-reference raw suite remains **55/212 full matches**, with no full
or partial score changes.

The first stable Radiant gate passed **9390 checks; 9083 passed, 305 partial,
0 failed, 2 skipped**, including 487/487 native text/layout and 752/752 UI.
Layout retains 4973 full / 301 partial and CSS Text 894 full / 86 partial, both
with zero baseline regressions. However, an adversarial check added after that
gate exposed a regression: breakable columns with 10px and 30px reading fonts
no longer share their original annotation-level baseline. The smaller reading
moves 4px; the qualified M21 binary and an equivalent unwrapped control retain
the common baseline. That first qualification does not cover the correction.

`RubySharedColumnLevelsKeepCommonBaselinesAcrossUnequalFonts` reproduced failures
for unequal reading fonts and a crossed tall-base/tall-reading envelope. CSS Ruby
§3.1 requires readings within one level to share an inline formatting context.
Flow units now retain that original level and finalize its current-line baseline,
stacking and contribution together. Existing baseline and placement stages are
shared by ordinary and coordinated flow. Only the current line's fragment
rectangles participate; a wrapped smaller reading cannot inherit the preceding
line's larger font metrics. Line cursors and break opportunities remain owned by
the selected columns. The source links stay scratch-owned under
**D4.5.1v4 / D4.1.4v5**. Evidence: `temp/ruby-m22/{shared-baseline-reproducer.log,
shared-baseline-breakable-before.json,shared-baseline-breakable-candidate.json,
qualification-run1.json}`.

The corrected wide and wrapped controls pass through retained reflow. Ordinary
browser inline controls independently confirm all 24 relative baseline/font-box
coordinates, with 0px maximum delta, without relying on unsupported Ruby display
roles. Evidence: `temp/ruby-m22/{neighbors-helper-final.log,
ordinary-baseline-comparison.json,browser-control-comparison-shared-final.json}`.

The final stable required Radiant gate passes **9391 checks; 9084 passed,
305 partial, 0 failed, 2 skipped**, including 488/488 native text/layout and
752/752 UI. Layout retains 4973 full / 301 partial and CSS Text 894 full /
86 partial, with zero baseline regressions. Dimension lint, diff and generated
inventory checks pass; all 421 names/IDs remain, with changes only in reference
columns 14–15. The seven frozen source/test inputs and CLI hash remain unchanged.
The final raw Ruby run stays **55/212 full matches**, with no full or partial
score changes relative to M21. Evidence:
`temp/ruby-m22/{qualification-result.json,radiant-shared.log,delta-shared-final.json}`.

M22's complete-column core is qualified. Mixed atomic/text composition is the
next diagnostic milestone; its static applicability gap still needs a dynamic
reproducer before implementation. Spanning/missing columns and vertical/RTL
source fragmentation remain open. No module API/import or ownership placement
changed, so the M21 boundary audit's six deferred imports remain recorded rather
than rerun. The unrelated M15 input/PDF build limit was not rerun. The full goal
remains active with 157 raw Ruby mismatches; references, upstream fixtures,
comparison tolerances and baseline floors remain unchanged.

## M23: mixed atomic and breakable text units

Four before-build controls reproduce lost pairing when an atomic base or
reading precedes or follows a breakable text pair: native rows are 150px rather
than the 120px shared by Chromium and independently segmented native controls.
The reading stays on one line while its base wraps. Changing only the atomic
neighbor's display selects the failing fallback. Evidence:
`temp/ruby-m23/{mixed-atomic-pairs.html,mixed-atomic-comparison.json}`.

The text formatter requires text-leaf plans in every segment; the whole-unit
formatter requires every segment to lack interior paired opportunities. Their
applicability gap sends this ordinary composition to the all-bases-first
fallback. CSS Ruby §3.4 requires paired fragments to stay together and §3.4.1
preserves real base-text seams. Atomic descendants retain their own formatting
context under CSS Display §2.6–§2.7.

P1 outcome: prepare whole and text units before source reset, then select and
emit both through the existing coordinated loop and shared placement stages.
Reuse the ordinary level walker and unbroken-unit predicate; do not add a third
formatter or force the whole group onto a fresh line. Plans stay scratch-owned
and source identities/parents/data remain under **D4.5.1v4 / D4.1.4v5**.

Acceptance covers atomic prefix/suffix bases and readings, independent internal
atomic wrapping, a fitting ordinary prefix, paint and every character Range,
and initial/retained plus wide/narrow reflow. Existing shared-column and seam
neighbors must remain strict. Spanning/missing columns and vertical/RTL source
fragmentation stay open. Verification budget: failing native/browser controls,
focused neighbors and raw Ruby delta, then one stable required Radiant gate,
dimension lint and inventory.

Whole units and text fragments now use one selection/emission loop. The
existing unbroken-unit predicate determines whether a source without a text
plan can participate; a real unsupported interior opportunity still rejects
preparation before any source reset. Ordinary level walking, keep scopes,
break-group caching and final placement remain shared. Selected-line envelopes
now include atomic leaf boxes in their parent line's frame, while their local
descendants remain independent. The first native run exposed that missing
envelope after pairing was repaired; no coordinate offsets were added.

All 101 focused Ruby/intrinsic/fragment/bidi neighbors pass, including strict
paint, every character Range, source identities and internal atomic wrapping
through retained wide/narrow flow. The forbidden Latin seam probe matches
Chromium on all 84 coordinates. The broader explicit-font browser comparison
checks 500 coordinates: two reading positions differ by 20px. Native keeps
the reading adjacent to its corresponding 20px base; Chromium leaves the same
20px gap even in the independently segmented control beside a tall atomic
base. Native retains CSS Ruby §3.1.2 adjacency, and the browser discrepancy
stays visible. Evidence: `temp/ruby-m23/{reproducer.log,focused-flow-2.log,
neighbors-flow-1.log,browser-comparison-flow-2.json,browser-gap-analysis.json,
forbidden-comparison.json}`. A final native cross-check also pairs an atomic
column with fragmentable text inside a shared segment: all 29 source/row
coordinates agree with independently segmented controls, including internal
atomic wrapping (`shared-atomic-column-comparison.json`).

The stable required Radiant gate passes **9392 checks; 9085 passed, 305 partial,
0 failed, 2 skipped**, including 489/489 native text/layout and 752/752 UI.
Layout retains 4973 full / 301 partial and CSS Text 894 full / 86 partial, with
zero baseline regressions. Dimension lint, diff and generated inventory checks
pass; all 421 names/IDs remain, with changes only in reference columns 14–15.
All four frozen source/test inputs and the qualified CLI hash remain unchanged.
Raw Ruby stays **55/212 full matches**, with no full or partial score changes.
Evidence: `temp/ruby-m23/{qualification-result.json,radiant.log,delta-flow-2.json}`.
No module API/import or ownership placement changed; the M21 boundary and M15
unrelated input/PDF limits remain recorded without redundant reruns.

The mixed whole/text core is qualified. The next diagnostic starts from the
imported `collapse-trailing-whitespace`: the saved native `n ` rectangle is
0px rather than 20px, while its reading retains a 40px column rather than 20px.
Following text agrees with the browser. Verify the first trimming/allocation
disagreement with fresh source controls before changing its owning stage;
`temp/ruby-m23/next-milestone.json` records that finite plan. The full goal stays
active with 157 raw mismatches. References, upstream fixtures, tolerances and
baseline floors remain unchanged.

## M24: line-owned trailing spaces and completed Ruby columns

P1, completed with the boundary-check limit below. The imported
`collapse-trailing-whitespace` gave `n ` a 0px native rectangle rather than
Chromium's 20px, while its reading retained an earlier 40px allocation. Wide
ordinary isolates and Ruby also lost a valid internal space. Explicit-font
captures with the qualified M23 binary reproduce the defect; forced breaks and
NBSP distinguish the affected paths.

Debugger evidence isolates the first incorrect write: the line formatter
correctly trims 40px to 20px, then `trim_isolated_inline_text_edge` removes
another 20px. That post-line pass also trims spaces away from line edges. It is
removed; CSS Text **§4.1.2 Phase II** owns line-edge removal, and CSS Ruby §2.5
keeps base-level whitespace in that common processing context. The initial
native fixture omitted the harness's `result` wrapper and stopped before
assertions; that load failure is excluded from verification.

Late trimming also reconciles already allocated columns. A scratch-owned
normalized model snapshots their current-line allocation before the source
trim. Existing alignment additions are removed, then the existing measurement,
column solver and placement stages allocate the surviving content. The owner
reports the actual consumed advance to the line formatter: a wider reading can
absorb the removed base space without shortening the column. Old-line geometry,
source identities/parents/data and atomic local frames retain their ownership
(**D4.5.1v4 / D4.1.4v5**). No source re-emission or retained scratch pointer is
introduced. This core covers horizontal LTR flow and undecorated internal Ruby
levels; nested Ruby, decorated internal boxes and other axes retain their
existing paths pending independent work.

The dominant-reading control exposed a separate range boundary: Ruby end
padding belongs to the last surviving character, while a consumed source space
maps to the allocated column end with zero width. The existing suffix scanner
is shared by explicit paint suppression and that query boundary. Consuming a
space's advance does not remove its logical bytes. An intermediate candidate
unconditionally suppressed soft-wrap separators in exports, causing seven
strict paged-media failures; it was rejected. Restoring the existing export
policy while sharing the suffix calculation repairs those regressions.

Acceptance covers ordinary isolates, soft/forced breaks, internal spaces and
NBSP; a dominant reading preserving line consumption; character ranges, visible
glyph positions and original source identity through retained wide/narrow
reflow. All **148 focused neighbors** and **504 paged-media tests** pass. The
seven controls agree with Chromium on **198 coordinates, 0px maximum delta**.
The final capture has the identical complete layout tree as that browser
comparison. Dimension lint and the 421-name inventory check pass; only inventory
source/test reference columns change, with no coverage claims changed.

The final required Radiant aggregate passes: **9,086 passed, 305 partial,
2 skipped, 0 failed / 9,393 checks**, including **490 native text-query** and
**752 UI automation** checks. Nine frozen inputs and the saved candidate CLI
remain unchanged after the gate. Evidence and exact qualification inputs:
`temp/ruby-m24/{source-inputs.json,qualification-result.json,
neighbors-export-contract.log,paged-export-contract.log,
radiant-qualified-candidate.log}`.

The unchanged-reference Ruby suite remains **55/212 full matches**, with none
gained or lost. Twenty-five partial documents change. The original whitespace
case improves from **71.4% to 100% text** and **77.8% to 88.9% elements**. Its
remaining mismatch is the auto-hidden `hon` annotation: native excludes its
layout/serialized rectangle, while Chromium retains a 60×10px `rt` rectangle.
All other source and element rectangles agree; hidden-annotation CSSOM exposure
remains unresolved.

`ruby-bidi-002` text improves from 29.7% to 31.6%, while its element report falls
from 9.1% to 7.7%. The actual harness at its 5.5px tolerance finds the same two
geometric matches before/after. Reported matches change from 5/55 to 4/52 because
three forgiven whitespace/type mismatches become two. The lower raw percentage
stays visible; explicit Ruby-role/tree differences remain open. Fresh final
captures of this and the original whitespace document retain the identical
complete layout trees used by these audits. Evidence:
`temp/ruby-m24/{delta-qualified-candidate.json,remaining-original-difference.json,
bidi-percentage-explanation.json,raw-capture-transfer.json,
browser-comparison-qualified-candidate.json}`.

The strict boundary DSOs link. The checker rejects the same six recorded
deferred imports as M21, with no additions or allowlist changes (140 current /
165 baseline imports). This remains a qualification limit. Evidence:
`temp/ruby-m24/boundary-export-contract-comparison.json`.

The goal remains active with 157 raw mismatches. Next is the imported
`ruby-with-floats-003`: saved captures place the base/under-reading 50px too far
left beside stacked floats while vertical coordinates agree. Verify the line's
occupied exclusion band and source interval selection before changing code;
under/over Ruby and ordinary tall-inline controls must distinguish the cause.
The finite diagnosis plan is `temp/ruby-m24/next-milestone.json`.

## M25: whole-Ruby float bands before source selection

P1, qualified with the recorded boundary-check limit. Fresh captures of
`ruby-with-floats-003` reproduce the 50px horizontal error with the qualified
M24 binary: the base and under-reading begin at 58px rather than Chromium's
108px. Float geometry and vertical Ruby coordinates agree. Read-only debugger
breakpoints show ordinary queries using an 18px band, then Ruby increasing the
line requirement to 26px without another query at that height. The second,
wider float intersects the grown line. Under/over Ahem controls reproduce the
same cause, while an ordinary tall inline-block already avoids that float.

[CSS 2.2 §9.5](https://www.w3.org/TR/CSS22/visuren.html#floats) defines float
adjacency through the line's vertical extent; [CSS Ruby §3.6](https://drafts.csswg.org/css-ruby-1/#line-height)
requires additional leading when annotations exceed the specified line-height.
A line now retains its selected float query height independently of final line
advance. Nested wrappers cannot replace a taller selected band with their
smaller strut; a line reset and independent annotation IFC clear the band.

Before emitting a complete Ruby run without an internal used/forced break,
the formatter reuses the existing scoped height measurement. Its assigned BFC
isolates float placement, the existing counter checkpoint rolls back generated
effects, and selected fragments restore under **D4.5.1v4 / D4.1.4v5**. The shared
float service updates the band and shifts already committed preceding content.
The existing traversal can include enclosing non-atomic inlines, preserving
atomic descendants' local frames. Existing boundary/lookahead and float-Y
search services choose a legal break or clearance before actual source emission.
The original CSSOM sources are neither reparented nor re-emitted to retry a line.

Core scope is horizontal LTR whole Ruby runs. Internally split Ruby needs a
band measured for each selected source interval; it retains its existing path.
Vertical/RTL Ruby and shape-outside remain separate. No formal ruling changes.

The regression covers under/over annotations, a tall-inline control, fitting
and wrapping prefixes, adjacent unbreakable Ruby, a nowrap Ruby with a legal
outer boundary, nested prefixes, clearance, and a generated counter. Initial,
retained and width-mutation checks pin source identity, character rectangles,
visible glyph positions, float geometry and a counter value of 1. All **214**
affected neighbors pass. Three browser control states agree on all **924**
element/source coordinates with 0px maximum delta; the qualified M24 binary
differs on 64 coordinates in the initial state. Evidence:
`temp/ruby-m25/{float-metrics.log,browser-comparison-3.json,
browser-comparison-before-3.json,neighbors-band-3.log,source-inputs.json}`.

The frozen raw suite reaches **56/212 full matches**: `ruby-with-floats-003`
newly passes, no full match is lost, and no partial score changes. Dimension
lint and the 421-name inventory check pass; only source/test reference columns
change. Strict boundary DSOs link, but the checker rejects the same six recorded
deferred imports as M24 (140 current / 165 baseline), with no additions or
allowlist changes. The required Radiant aggregate passes: **9,087 passed, 305 partial, 2 skipped,
0 failed / 9,394 checks**, including **491 native text-query**, **504 paged-media**
and **752 UI automation** checks. Layout retains 4,973 full / 301 partial and
CSS Text 894 full / 86 partial, with no baseline regressions. All seven frozen
inputs and the qualified CLI hash remain unchanged after the gate. Evidence:
`temp/ruby-m25/{wpt-final.log,delta-final.json,lint.log,boundary-comparison.json,
radiant.log,qualification-result.json}`. The goal remains active with 156 raw
mismatches. Whole-Ruby float composition is qualified; per-selected-fragment
float bands remain open.

The next diagnosis extracts the three relative-positioning behaviors asserted by
`position-relative.html` into causal native/browser controls, separating them
from its generated test-report UI before assigning a root cause. A probe of
`ruby-overhang:none` did not demonstrate the suspected layout violation: its
direct/inherited geometry agrees with Chromium, and current CSS Ruby §5.1
aliases `none` to `spaces`; `auto` overhang amount remains UA-defined. No patch
is made from that older-spec hypothesis. Evidence and next plan:
`temp/ruby-m25/{overhang-none-comparison.json,next-milestone.json}`.

## M26: relative offsets survive Ruby column and line placement

P1, qualified. The three causal
behaviors asserted by imported `position-relative.html` are checked separately
from its test-report UI. Ruby and ordinary ancestor motion already work, but
column/late annotation placement cancels the reading's own `left`/`top`. The
relative-descendant walk also enters atomic local content, translating glyphs
again. Fresh explicit-font controls and completed read-only debugger callbacks
prove these owners; the first trace stops on an optimized-out variable and is
excluded. Evidence: `temp/ruby-m26/{relative-comparison-before.json,
relative-trace-2.log}`.

[CSS Position §3.3](https://www.w3.org/TR/css-position-3/#relative-positioning)
keeps relative movement separate from normal-flow sizing. Ruby now derives its
column and annotation-level constraints from normal-flow rectangles while
publishing the corresponding visual offsets. The existing applied inline-Y
accumulator is generalized to a scoped offset pair, shared by Ruby and generic
inline bounds. Ordinary ancestor offsets stay in the shared Ruby frame; only
internal offsets are removed from constraints. The shared descendant walk stops
at independent atomic frames.

An atomic base exposes a second stage dependency: declared offsets can still be
deferred, so they cannot be subtracted as though already applied. PositionProp
records both applied components. Fresh inline/atomic placement resets them;
repeated vertical alignment retains the already shifted X while rebuilding Y.
The generic inline-bounds path reads those facts too, converting source-axis
facts after vertical publication. Full flow results already copy PositionProp
by value. Scalar measurement scopes now preserve both applied components beside
the restored geometry (**D4.5.1v4 / D4.1.4v5**). No formal ruling changes.

The strict regression covers root/ancestor/annotation accumulation, an atomic
reading and a relatively positioned atomic base, unaffected following siblings,
negative/reset offsets, original source identity, per-character Range and visible
glyph positions through initial/retained CSSOM edits. Glyph captures use their
documented local origin; document-coordinate assertions add the resolved text
frame origin. A real percentage-inset trial changes the containing dimensions,
then verifies restoration of the original position and column bounds.

All five rows agree with Chromium on **432 element/source coordinates** across
initial, accumulated and negative states, with 0px maximum delta. The qualified
M25 build differs on 15 coordinates in the shifted state, up to 100px. All **154
affected neighboring tests** pass after the scalar-snapshot completion, including
the focused restoration regression. All **492 native text-query tests** also pass
in the final aggregate.
Evidence:
`temp/ruby-m26/{browser-comparison-final.json,browser-comparison-qualified-before.json,
neighbors-final.log,focused-relative-final.log}`.

The raw Ruby suite remains **56/212 full matches**, with none gained or lost.
`position-relative` improves from **66.7% to 100% text** and **33.3% to 50%
elements**, with no other score changes. Its source container, Ruby, reading and
all text rectangles now agree exactly. The remaining element mismatch is the
browser's generated 642.67px report panel and its effect on document dimensions;
no reporting shim or reference change conceals it. Optional default overhang
in an earlier unequal-width control stays separate from the exact equal-width
controls. Evidence: `temp/ruby-m26/{wpt-qualified-candidate.log,delta-qualified-candidate.json,
original-remaining-difference.json}`.

The required Radiant aggregate passes: **9,088 passed, 305 partial, 2 skipped,
0 failed / 9,395 checks**, including **504 paged-media** and **752 UI automation**
checks. Layout retains 4,973 full / 301 partial matches. Dimension lint and the
421-name inventory check pass; only source/test reference columns change.
Strict boundary DSOs link, but the checker rejects the same six recorded
deferred imports as M25, with no new import or allowlist change. All eleven
frozen source inputs and the qualified CLI hash remain unchanged after the
gate. Evidence: `temp/ruby-m26/{source-inputs.json,radiant.log,
qualification-result.json,boundary-comparison.json,lint-final.log}`.

The goal remains active with 156 raw mismatches. The next finite milestone
addresses block-axis annotation margins: explicit-font over/under controls
show that an 11px separating margin is ignored while padding and borders agree
with Chromium; repeated lines advance 30px instead of 50px. CSS Ruby §3.1.2
requires margin-box level envelopes. Hidden-annotation CSSOM exposure stays
deferred: Chromium's collapse probe retains layout effects contrary to §2.4,
so its geometry alone cannot justify a repair. Diagnosis and acceptance cases:
`temp/ruby-m26/{margin-comparison-before.json,margin-multiline-browser.json,
next-milestone.json}`.

## M27: block-axis margin envelopes for interlinear levels

P1, qualified. Imported
`ruby-annotation-with-margin` and `interlinear-block-margin-box` expose an
ordinary reading/base separation defect. Explicit-font over/under controls
reproduce an ignored 11px separating margin while padding/border controls
agree. With 9px/11px outer margins, successive 20px base lines advance only
30px instead of 50px. The qualified M26 binary differs from Chromium on 48
coordinates in the strict fixture. Evidence:
`temp/ruby-m26/{margin-comparison-before.json,margin-multiline-browser.json}`
and `temp/ruby-m27/{focused-before.log,browser-comparison-before.json}`.

[CSS Ruby §3.1.2](https://drafts.csswg.org/css-ruby-1/#interlinear-block) sizes
base/annotation containers around their units' margin boxes. The shared
normal-flow bounds adapter now includes block-axis margins for computed
ruby-base/ruby-text roles, using the existing line-relative boundary-edge
mapping. Column width and published source border boxes retain their existing
owners; level stacking and line-leading contribution consume the corrected
envelope. No tag exception, new storage, module API or formal ruling is added
(**D4.5.1v4 / D4.1.4v5**, **D7.3.3–D7.3.4**).

The strict regression checks two successive lines for over/under readings,
plain and padding/border decorations, following atomic tails, source identity
and first/last character rectangles. Initial, retained, zero-margin and changed
margin states pass. Three browser states agree on all **588 element/source
coordinates**, with 0px maximum delta. The first capture attempt used external
normal-priority overrides that lost to the document stylesheet; recaptures
explicitly give both engines the same override precedence. The native live
CSSOM regression does not need that capture adapter. Evidence:
`temp/ruby-m27/{focused-margin.log,browser-comparison.json,plan.json}`.

All **182 affected neighboring tests** pass. The frozen raw suite reaches
**57/212 full matches**, gaining `interlinear-block-margin-box` with no lost
match. `ruby-annotation-with-margin` improves from 0% to 48.4% elements and
44.4% text; no other partial score changes. Dimension lint and the 421-name
inventory check pass, with only source/test reference columns changed. No
module API/import changes require a new boundary audit; the recorded M26
six-import checker limitation is not claimed resolved. Evidence:
`temp/ruby-m27/{neighbors.log,wpt-final.log,delta-final.json,lint.log,
inventory-check.log,source-inputs.json}`.

The required Radiant aggregate passes: **9,089 passed, 305 partial, 2 skipped,
0 failed / 9,396 checks**, including **493 native text-query**, **504 paged-media**
and **752 UI automation** checks. Layout retains 4,973 full / 301 partial
matches. All four frozen source inputs and the qualified CLI hash remain
unchanged after the gate. Evidence:
`temp/ruby-m27/{radiant.log,qualification-result.json}`.

The goal remains active with 155 raw mismatches. The next finite milestone
addresses inline-axis annotation margins. Fresh left/right/asymmetric controls
show lost column width and reading offsets; 5px/15px margins contribute 80px
instead of 100px to a minimum, and 90px instead of 110px to a maximum with a
10px tail. A padding-only control already agrees with Chromium and can reuse
the existing source-range/glyph comparison helper. Investigate both normalized
intrinsic outer contributions and the conversion from column margin-box
allocations to published border boxes before editing. Evidence and acceptance:
`temp/ruby-m27/{inline-margin-comparison.json,inline-margin-intrinsic-comparison.json,
inline-margin-control-comparison.json,next-milestone.json}`.

## M28: inline margin contributions and source border-box publication

P1, qualified. The
remaining horizontal geometry in imported `ruby-annotation-with-margin` loses
reading start/end margins from column sizing and placement. Fresh asymmetric
controls also show 80px/90px parent min/max contributions where 100px/110px
are required. Ordinary inline content in an anonymous base exposes the same
owner. The qualified M27 binary differs on 40 of 396 initial element/source
coordinates, up to 20px. Evidence: `temp/ruby-m28/{focused-before-base.log,
browser-comparison-before.json}`.

[CSS Ruby §3.1.1](https://drafts.csswg.org/css-ruby-1/#interlinear-inline) and
[CSS Sizing §2.2](https://www.w3.org/TR/css-sizing-3/#intrinsic-contribution)
require outer contributions. Ruby's normal-flow unit bounds now include inline
margins, including ordinary inline sources inside anonymous units. A shared
computed-role/line-relative adapter retains the existing special block-axis
margins for ruby bases/readings. Column publication converts its allocated
margin box back to a source border box, then applies the existing relative
offset. Margins therefore affect constraints and flow advance without inflating
the source border box or canceling its inset.

Normalized intrinsic measurement now uses the ordinary inline parent's margin
contribution calculation. That calculation is extracted into a shared layout
helper rather than copied; existing segment-edge and length-resolution helpers
retain their owners. Ordinary callers keep their existing policy for an
unsegmented minimum that already consumed used margins. No new persistent
state, source reparenting or formal ruling is added (**D4.5.1v4 / D4.1.4v5**);
the helper remains inside the layout module (**D7.3.3–D7.3.4**).

The strict regression covers left/right/asymmetric margins, padding/border
controls, preferred min/max-content widths, a computed `ruby-text` span and
an ordinary inline base. Initial/retained, zero and swapped margins preserve
source identities, every character rectangle and visible glyph positions,
using the existing source-pair comparison helper. All **183 affected neighbors**
pass. Three browser states agree on **1,188 element/source coordinates**, with
0px maximum delta. Initial test expectations incorrectly treated preferred
min/max-content parent widths as unconstrained contributions; they now reflect
the existing CSS Sizing preferred-size normalization, without an engine change
for that contract. Evidence: `temp/ruby-m28/{focused-final.log,neighbors.log,
browser-comparison-final.json}`.

The frozen raw suite remains **57/212 full matches**, with none gained or lost.
`ruby-annotation-with-margin` improves from 48.4% to **90.3% elements** and
44.4% to **83.3% text**. `ruby-bidi-002-ref` also improves, with no other score
change. Dimension lint and the 421-name inventory check pass; only source/test
reference columns change. The shared declaration triggers a fresh boundary
audit: strict DSOs link, while the checker rejects exactly the same six
recorded deferred imports as M26, with no added import or allowlist change.
Evidence: `temp/ruby-m28/{wpt-final.log,delta-final.json,lint.log,
inventory-check.log,boundary-comparison.json,source-inputs.json}`.

The required Radiant aggregate passes: **9,090 passed, 305 partial, 2 skipped,
0 failed / 9,397 checks**, including **494 native text-query**, **504 paged-media**
and **752 UI automation** checks. Layout retains 4,973 full / 301 partial
matches. All six frozen inputs and the qualified CLI hash remain unchanged
after the gate. Evidence: `temp/ruby-m28/{radiant.log,qualification-result.json}`.

The goal remains active with 155 raw mismatches. All six horizontal readings
in the original margin document now agree exactly. Its last three vertical-lr
readings have matching sizes and inline positions, but lie on the opposite
physical side of their bases. Fresh horizontal/vertical-rl/vertical-lr controls
confirm that Ruby placement confuses semantic line-over/under with block-start/end;
the ordinary inline formatter already has the required direction helper.
The next milestone promotes and reuses that policy while tracing placement,
line metrics and after-line alignment together. CSS Ruby §3.6 permits annotation
overflow and requires sufficient spacing for equivalent repeated Ruby; a
separate 41px/51px loose-line difference alone does not justify forcing
Chromium's line-height choice. Evidence and bounded acceptance cases:
`temp/ruby-m28/{original-remaining-difference.json,line-side-comparison.json,
next-milestone.json}`.

## M29: semantic Ruby sides use the line's source-axis mapping

P1, qualified with the recorded BR/boundary limits below. The
last three readings in imported `ruby-annotation-with-margin` have correct
inline positions/sizes but appear on the wrong side in vertical-lr. Fresh
horizontal/vertical-rl/vertical-lr controls reproduce the side reversal; the
qualified M28 binary differs on 29 coordinates in the final initial fixture,
up to 50px. Evidence: `temp/ruby-m29/{focused-before.log,
browser-comparison-before.json}` and M28's original-case audit.

[CSS Ruby §4.1](https://drafts.csswg.org/css-ruby-1/#rubypos) places annotations
line-over/under, while [CSS Writing Modes §6.3](https://www.w3.org/TR/css-writing-modes-3/#line-directions)
distinguishes those sides from block-start/end. The ordinary inline formatter's
existing direction helper is promoted through the layout header and reused.
Ruby alternation stays semantic; placement and extent measurement map that
side into the owning flow's source block coordinates. Ascent/descent
contributions retain their line-over convention. After-line placement uses
the same mapping, rather than adding a final visual correction.

Causal padding controls expose a second stage: M17's block-start-space adapter
returned zero for every vertical context. Internal padding now uses the shared
line-relative edge mapper, and the first line consumes that space from the
corresponding semantic leading side. The existing external adjoining-margin
protocol still uses physical Y; that separate vertical transfer remains open.
No new persistent state or formal ruling is added (**D4.5.1v4 / D4.1.4v5**);
the promoted helper stays inside the layout module (**D7.3.3–D7.3.4**).

The regression covers ordinary and top/bottom/all margins in all three writing
modes, three repeated Ruby lines, internal block-start padding, source identity
and every reading character rectangle. Initial/retained and live under/over
changes preserve physical side, separation and non-overlapping equivalent line
envelopes. The padding assertions initially fail without the owning-stage fix
and pass afterward without weakening expectations. All **230 affected neighbors**
pass. Evidence: `temp/ruby-m29/{focused-padding-final.log,
focused-padding-qualified-candidate.log,neighbors-final.log}`.

Two full browser comparisons retain all **1,352 element/source coordinate**
checks. Every remaining difference is one of **16 vertical BR rectangle
coordinates**; annotation/base/flow/source-text geometry agrees within the
existing 0.11px capture tolerance. Those BR failures remain in the artifact,
and the raw comparator/references are unchanged. A separate 41px/51px
loose-line example does not justify forcing Chromium's line-height policy:
[CSS Ruby §3.6](https://drafts.csswg.org/css-ruby-1/#line-height) permits
annotation overflow while requiring equivalent repeated Ruby not to overlap.
Evidence: `temp/ruby-m29/browser-comparison-final.json` and M28's line-side probe.

The frozen raw suite reaches **59/212 full matches**, gaining both
`ruby-annotation-with-margin` and its reference, with no lost match or changed
partial score. Dimension lint and the 421-name inventory check pass; only
source/test reference columns change. Strict boundary DSOs link; the checker
rejects the same six recorded imports as M28, with no added import or allowlist
change. Evidence: `temp/ruby-m29/{wpt-final.log,delta-final.json,lint-final.log,
inventory-check.log,boundary-comparison.json,source-inputs.json}`.

The required Radiant aggregate passes: **9,091 passed, 305 partial, 2 skipped,
0 failed / 9,398 checks**, including **495 native text-query**, **504 paged-media**
and **752 UI automation** checks. Layout retains 4,973 full / 301 partial
matches. All seven frozen inputs and the qualified CLI hash remain unchanged
after the gate. Evidence: `temp/ruby-m29/{radiant.log,qualification-result.json}`.

The goal remains active with 153 raw mismatches. The next milestone addresses
the imported `pseudo-first-line` case. Fresh explicit-font controls check 192
element/source coordinates; only the first reading box/text Y differs, with
the reading staying over instead of inheriting first-line under positioning.
Color-only, local Ruby position override, constant-under and later-line controls
agree geometrically. CSS Pseudo §2.1.2–2.1.3 permits `ruby-position` and defines
fragment-specific inherited values. Trace that inheritance at the source line
and retain the effective value through metric and after-line phases; avoid
coupling it to a dummy first-line font override or patching only final geometry.
The existing font resolver correctly declines rules with no font properties.
Evidence and bounded acceptance cases:
`temp/ruby-m29/{first-line-comparison.json,next-milestone.json}`.

## M30: first-line Ruby position follows the selected source fragment

P1, completed with the boundary-check limit below. Imported
`pseudo-first-line` ignores the applicable first-line `ruby-position`. An
explicit-font probe isolates two incorrect reading/text Y coordinates while
color-only, local Ruby override, constant-under and later-line controls agree
geometrically. The strict regression also demonstrates an incorrect tight-line
advance before the repair. Evidence: `temp/ruby-m29/first-line-comparison.json`
and `temp/ruby-m30/focused-before.log`.

[CSS Pseudo §2.1.2–2.1.3](https://drafts.csswg.org/css-pseudo-4/#first-line-styling)
applies `ruby-position` to first-line fragments through inherited style. The
shared native keyword consumer now accepts an optional originating pseudo in
its inheritance path. Local declarations resolve first; an inheriting child
then visits the originating pseudo before the normal block. The existing
substitution, validation, CSS-wide defaulting and initial-value consumer remain
shared. The existing three-argument API remains intact, and ordinary CSSOM
ancestry is unchanged. No dummy font is allocated to activate a non-font rule.

Ruby initial placement, metric contribution and after-line alignment pass the
explicit selected source line number. Existing segment membership and completed
line callbacks already retain that fact, including source-text fragments; no
new persistent style cache or source mutation is needed. The pseudo origin is
borrowed only for the synchronous read (**D4.5.1v4 / D4.1.4v5**), and the shared
API stays within the CSS/layout module (**D7.3.3–D7.3.4**).

Two regressions cover inherited position, color-only/local/ancestor overrides,
inherit/initial/unset/invalid substitution, normal under with first-line over,
soft/forced breaks, tight-line padding/advance, and live width/custom-property
changes. A single Ruby source also spans one, two or three selected lines;
only its first fragment inherits the pseudo. Source identities and reading
character geometry survive edits, and normal computed keyword reads stay
ordinary. All **150 focused neighbors** pass. Evidence:
`temp/ruby-m30/{focused-fragments.log,neighbors-final.log}`.

Five browser states agree on all **3,000 element/whole-source bounding coordinates**,
with 0px maximum delta. Native JSON exports separate text fragments, so the
local analysis helper groups them by their original source offsets and compares
source unions. A separate full fragment audit retains **3,072 checks**, including
six last-fragment width differences: native pairs two reading words with the
first two base words, while Chromium pairs one reading word with them and two
with the last base word. [CSS Ruby §3.4.2](https://drafts.csswg.org/css-ruby-1/#break-within)
permits different matching break opportunities. Those differences remain in
the artifact; no engine, upstream reference or raw comparator change forces
a particular UA partition. Evidence: `temp/ruby-m30/{browser-union-comparison-final.json,
browser-comparison-final.json,fragment-choice-audit.json}`.

The raw suite reaches **60/212 full matches**, gaining `pseudo-first-line`,
with no lost match or changed partial score. Dimension lint and the 421-name
inventory check pass; only source/test reference columns change. Strict boundary
DSOs link; the checker rejects the same six recorded imports as M29, with no
added import or allowlist change.
Evidence: `temp/ruby-m30/{wpt-final.log,delta-final.json,lint-final.log,
inventory-check.log,boundary-comparison.json,source-inputs.json}`.

The required Radiant aggregate passes: **9,093 passed, 305 partial, 2 skipped,
0 failed / 9,400 checks**, including **497 native text-query**, **504 paged-media**
and **752 UI automation** checks. Layout retains 4,973 full / 301 partial
matches. All six frozen inputs and the qualified CLI hash remain unchanged
after the gate. Evidence: `temp/ruby-m30/{radiant.log,qualification-result.json}`.

The goal remains active with 152 raw mismatches. Next is containing-block float
exclusion during Ruby column completion. A reduced source probe places the
same base at x22 for ordinary inline, unannotated Ruby and a disabled reading;
a visible reading moves it back to x8 across a 14px float margin box. LLDB
confirms that column alignment translates an already-flowed local x14 to the
obsolete x0 cursor. CSS Ruby §2.1.1 retains containing-block float ownership.
Chromium instead computes nested Ruby floats to `none` in the causal controls,
so that browser behavior cannot justify suppressing native floats. Trace the
source-origin/column-completion protocol, keeping ordinary and disabled-reading
controls and retained edits. Evidence and bounded acceptance cases:
`temp/ruby-m30/next-milestone.json` and
`temp/ruby-m31/{float-cause-coordinates.json,float-cause-debugger.log,float-browser.json}`.

## M31: Ruby columns retain newly introduced source float exclusions

P1, completed with the recorded limits below. A visible reading
causes the same base that clears a leading float without annotations to move
14px into its margin box. LLDB confirms the first incorrect translation:
source flow has already selected local x14, but column allocation uses the x0
cursor captured before the float was installed. Ordinary inline and disabled
reading controls retain the correct position. Evidence:
`temp/ruby-m31/{float-cause-coordinates.json,float-cause-debugger.log,
focused-before-corrected.log}`.

[CSS Ruby §2.1.1](https://drafts.csswg.org/css-ruby-1/#formatting-context)
keeps internal floats in the Ruby container's containing block. A shared
stack-owned source-origin descriptor now records the advance, exclusion edge
and selected line before source emission. If that same line gains a different
float exclusion edge, column completion reads the already-flowed base origin;
otherwise it retains the captured cursor, which includes decorations. Whole
and selected segment completion share this protocol. The descriptor owns no
retained pointer or allocation (**D4.5.1v4 / D4.1.4v5**); APIs and ownership
placement stay private to layout (**D7.3.3–D7.3.4**).

An intermediate implementation unconditionally inferred the origin from base
bounds. Raw qualification exposed block-Ruby decoration regressions; that
implementation was rejected. The final exclusion-change protocol restores
both documents' previous full text matches and preserves all existing full
Ruby matches. The suite remains **60/212 full matches**; only
`ruby-float-handling-001-ref` changes, improving **0% → 25% text matches**.
References, upstream fixtures, tolerances and baselines remain unchanged.
Evidence: `temp/ruby-m31/{delta-origin-1.json,delta-origin-2.json}`.

Two regressions cover direct/nested leading floats, shorter/longer readings,
padding, shared whole segments, ordinary/disabled-reading controls, left/right
mutations and retained width/reading removal. Source character ranges and tail
advance retain the occupied interval. A browser control independently confirms
that trailing Ruby alignment space belongs to the final character's range.
External-float browser controls retain **292 coordinate checks**: the twelve
remaining differences are Chromium's optional 5px end overhang on the four
long-reading examples. Their base/reading/source/float geometry agrees; those
differences remain visible. Chromium computes nested Ruby floats to `none`,
which is not used as justification to remove native floats. Evidence:
`temp/ruby-m31/{focused-final.log,no-float-browser.json,
float-external-comparison-final.json,float-browser.json}`. The frozen candidate's
native before/after audit retains all 468 coordinates: only 68 horizontal
coordinates move, each by the required 14px exclusion; other coordinates and
tree/source structure remain identical (`fixture-before-after-audit.json`).

Annotation-emitted floats remain partial. A separate padded unannotated-Ruby
probe also retains an extra 10px origin discrepancy; the disabled-reading and
ordinary controls agree. This milestone does not claim that independent path
is repaired. Evidence: `temp/ruby-m31/float-cause-padding-native.json`.

All **149 focused neighbors** pass. Dimension lint and the 421-name inventory
check pass; only source/test reference columns 14–15 change. The required
Radiant aggregate passes: **9,095 passed, 305 partial, 2 skipped, 0 failed /
9,402 checks**, including **499 native text-query**, **504 paged-media** and
**752 UI automation** checks. Layout retains 4,973 full / 301 partial matches.
All four frozen inputs and the qualified CLI hash remain unchanged after the
gate. No module API/import or persistent ownership placement changes, so M30's
same-six-import boundary limit remains recorded without a redundant audit.
Evidence: `temp/ruby-m31/{neighbors-final.log,lint-origin-2.log,
inventory-check-final.log,radiant.log,qualification-result.json,source-inputs.json}`.

The goal remains active with 152 raw mismatches. The next ownership milestone
starts from an annotation float whose 104px outer bottom disappears before
following `clear:both`: ordinary/external/base controls clear at 104px, while
the reading case clears at 40px. LLDB records the new count/extent, then
`LayoutContextScope` restores the empty BFC snapshot. An ordinary child context
preserves the owner, and an existing list hides the clearance symptom through
linked-node aliasing. Trace committed annotation ownership and speculative
measurement separately; keep first/seeded lists and current/ancestor BFC
controls. CSS Ruby §2.1.1 and [CSS 2.2 §9.5.2](https://www.w3.org/TR/CSS22/visuren.html#flow-control)
govern containing-block ownership and clearance. Evidence and finite acceptance:
`temp/ruby-m31/next-milestone.json` and
`temp/ruby-m32/{clear-coordinate-audit.json,annotation-float-clear-debugger.log,
owner-control-audit.json}`.

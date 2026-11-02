# CSS Ruby WPT implementation

Status: active. Started 2026-11-02 on `codex/css-ruby-wpt`, source base
`c5e40c521`. The imported suite contains 212 documents. The initial worktree
run reports 19 full matches, 193 geometry mismatches, and no execution errors.
Computed roles, HTML construction, shared columns and annotation hiding are
qualified with existing aggregate failures documented below. Annotation-break
normalization, inline font boxes, Ruby spacing, nested envelopes and non-inline
Ruby formatting and isolated-inline margin advances are also qualified. General flow
integration and the remaining raw suite failures are still open. Base-only
fragment flow and unbroken parallel units are qualified. The objective
is not complete.

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

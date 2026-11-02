# CSS Ruby WPT implementation

Status: active. Started 2026-11-02 on `codex/css-ruby-wpt`, source base
`c5e40c521`. The imported suite contains 212 documents. The initial worktree
run reports 19 full matches, 193 geometry mismatches, and no execution errors.
Computed-role/metric and HTML-construction milestones are qualified with the
existing aggregate failures documented below. Segment/column implementation
is the next active milestone; the objective is not complete.

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
Do not overwrite authored style or source text. Keep any transient comparison
data within the existing scratch scope (**D4.5.1v4**).

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

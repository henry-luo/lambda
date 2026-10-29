# WPT writing modes and bidi conformance

Status: checkpointed at M57; user requested wrap-up on 2026-10-28. Source base `4378c71d8`.

The latest qualified milestone (M57) reports **1,669 / 1,980 full geometry
matches**, 311 mismatches and zero execution errors. The initial run had 646
matches and 156 execution errors. The conformance goal remains incomplete;
remaining contribution families and the next finite milestone are recorded below.

## Objective and constraints

Fix `make layout suite=wpt-css-writing-modes` through shared, specification-based
engine changes. Preserve the upstream fixtures, raw Chromium captures,
comparison tolerances and failure reporting. Independently audited browser gaps
use separate source-checked standards controls with their derivation recorded.
An observed score is evidence,
not an accepted baseline. Close finite milestones and retain unresolved conformance work explicitly.

CSS Writing Modes [Level 3](https://www.w3.org/TR/css-writing-modes-3/) and
[Level 4](https://www.w3.org/TR/css-writing-modes-4/) govern layout behavior;
the HTML and XML specifications govern document loading. Existing Lambda
contracts remain authoritative: **D2.10.1v2** for XHTML admission,
**D4.1.4v5 / D4.5.1v4** for allocation ownership, **D5.3.3 / D5.4.1** for
precise roots and evaluator lifetime, and **D7.3.3–D7.3.4** for module boundaries.
No CSS fix requires a Lambda language semantic change.

## Initial evidence

Source: `cdfa40e55`, with the preceding suite import changes present.
Binary SHA-256: `a3c4b7a54d943e24a45e2e9e8310dad5f31f76ceef0bb59454a64412c1d8abf6`.
The ordinary margin control matches Chromium fully.

The first complete run discovers 1,980 documents and reports 646 full matches,
1,334 failures, including 156 execution errors. One of four strided batches
segfaults after 252 result frames. The remaining batches terminate normally
with failed document frames. Logs: `temp/wpt-writing-initial.log` and
`temp/wpt-writing-control.log`.

Distinct findings, requiring separate repairs:

- The crash stack enters `StateStore::destroy` from `free_document`, through
  `reset_foreign_document_cache` during script-runtime batch reset. Isolate
  the owning document and allocation before changing destruction order.
- `block-flow-direction-004.xht` is rejected at `&nbsp;`. **D2.10.1** explicitly
  excludes non-predefined named entities. Expanding that policy is pending
  user direction; it must use the HTML Standard's recognized public-ID
  catalog, with corresponding formal and working-design updates.
- `orthogonal-root-resize-icb-003.html` cannot load its iframe `data:` document:
  the local-file reader receives a null path, so the iframe load callback
  never releases `reftest-wait`. The snapshot refusal is correct evidence of
  incomplete loading; changing its deadline would not repair the cause.
- Geometry failures span positioning, orthogonal sizing, text/bidi and forms.
  Rank shared defects after stabilizing document execution.

## Milestone 1: document execution and lifetime

Priority: P0. Reliable document loading and teardown are prerequisites for
interpreting layout comparisons.

Acceptance cases:

1. Reproduce the batch teardown crash with a minimal ordered document sequence;
   repeated loads and resets must release each document-owned state exactly once.
2. Load the original orthogonal iframe fixture through its existing resource
   loader, execute its load callback and animation frames, and reach explicit
   snapshot readiness. Verify the horizontal and vertical neighboring variants.
3. Keep malformed XHTML rejection and unrelated document ownership behavior
   intact. Entity-catalog expansion, if approved, gets its own acceptance cases.

Design sequence: trace allocation owner → parent/child document ownership →
cache references → deferred destruction → runtime reset. Reuse existing loader,
resource decoding and destruction helpers; do not add fixture conditions.
Record the confirmed cause and ownership invariant before implementing a fix.

Verification budget: focused native/document sequence and original WPT cases
in the edit loop; relevant document/iframe neighbors once stable; the required
Radiant baseline at the milestone boundary. Use ASan when the lifetime diagnosis
requires it. Preserve both raw execution errors and geometry mismatches.

## Following milestones

Choose the next finite CSS behavior from measured failure groups: writing-mode
inheritance/propagation, logical-to-physical box mapping and orthogonal sizing,
then bidi/inline geometry. Each milestone must name its failing fixtures,
the normative specification section, the first disagreeing pipeline stage,
the shared code owner, and focused/neighbor/gate acceptance evidence before
implementation. Reassess ordering when a serious shared defect appears.

## Confirmed teardown cause and repair design

The two-document sequence `bidi-dynamic-iframe-001.html` followed by
`orthogonal-root-resize-icb-003.html` reproduces the crash. A current
`debug_asan` build reports a use-after-free of the iframe's lifecycle registry:
`layout_single_file` destroys its loader pool, which cascades through the
parent Input memory context, before the next runtime reset drains the removed
iframe. Report: `temp/wpt-iframe-reset-asan.stderr`.

The CLI duplicates document teardown and omits the deferred/embedded-document
release already owned by `free_document`. Use that canonical teardown for
batch documents as well. Expose optional phase timing through the same
function, keeping the existing unprofiled entry point for native clients.
Transfer the CLI's URL and loader-pool aliases before the canonical owner
releases them. A refused teardown retains its backing pool; it cannot proceed
to parser-input destruction. This preserves **D4.1.4v5 / D4.5.1v4** ownership
and **D5.4.1** quiescent runtime transitions without a second cleanup algorithm.

Regression: a standalone iframe fixture adopts text and removes its frame,
then a second scripted document runs in the same CLI batch. Require both
output documents and the adopted text, and rerun the original two-file WPT
sequence under ASan. The XHTML entity-catalog extension is approved by the
user on 2026-10-26; implement it after the lifetime repair.

The first canonical-teardown probe exposed the second half of the ownership
defect: an adopted text node still occupies the iframe's native arena. An idle
queue cannot release that arena while the parent keeps the adopted node.
Retain retired iframe documents as ordinary parent-owned document resources,
using the same shared destructor as adopted foreign documents. Release their
embedding pins immediately, then release their physical storage after the
parent's state and view teardown. The parent Input context therefore cannot
cascade ahead of its children. Remove the obsolete thread-local queue and its
idle drains rather than add another lifetime decision. This uses the existing
tracked allocation/arena/resource ownership model, **D4.1.4v5 / D4.5.1v4**.

## Completion record

No accepted baseline changes. The initial failures, document-load diagnostics
and crash stack are retained under `temp/`; implementation qualification follows.

The retired-iframe resource ownership and canonical CLI cleanup now pass both
the original two-document sequence and the standalone adoption sequence under
the current ASan binary (leak detection disabled; the native memtrack shutdown
also reports no leaks). The original second WPT document still fails snapshot
readiness because its `data:` iframe has not loaded; it no longer crashes.

Data-URL design: decode through the existing `parse_data_uri` implementation,
then select HTML or XHTML using the shared MIME classifier. Bypass filename
dispatch for opaque data payloads. Percent decoding, base64 and fragments stay
in the shared decoder; no new transport parser is introduced. This follows
[Fetch §6](https://fetch.spec.whatwg.org/#data-urls) and **D2.10.1v2**. Initial
admission covers HTML/XHTML MIME types; unsupported types do not become HTML.

Entity design: recognized public IDs select the existing `html_entity_lookup`
table after validating the doctype. Keep strict XML syntax and exact,
semicolon-terminated names. The approved **D2.10.1v2** revision and
`vibe/input/Input_XML.md` describe the same catalog boundary. Native parser
coverage includes text/attributes, multi-codepoint/supplementary replacements,
unknown IDs/entities, and rejected internal subsets.

### Milestone 1 focused results

The current native checks pass: seven `HtmlParserTest.Xhtml*` cases, the
HTML/XHTML data-URL transport test, the removed-iframe batch regression and
the existing unfinished-snapshot batch control. Original WPT
`block-flow-direction-004.xht` and `orthogonal-root-resize-icb-003.html` now
match Chromium fully.

The next complete WPT run finishes all 1,980 documents with **zero execution
errors and no batch crash**, reporting 676 full matches and 1,304 geometric or
text mismatches. The initial run had 646 full matches and 156 execution errors.
Log: `temp/wpt-writing-after-load.log`. The Radiant baseline passes (6,323
reported cases, including existing partial/skipped cases); the combined Lambda
and input baseline passes 8,251/8,251. The Radiant dimension-cast lint passes.
The additional input-processing gate initially ran out of disk space. After
removing disposable PDF-test sandboxes it completes with 4,533/4,559 passing;
the 26 remaining failures cover wiki formatting options, prose source-format
round trips, a missing PDF reference fixture, and PDF visual/output checks.
Report: `temp/wpt-writing-input-gate-retry-summary.json`. These remain a
qualification limit; no exclusions or baselines were added to conceal them.

### Reference integrity prerequisite for CSS geometry

`abs-pos-non-replaced-vlr-013.xht` has one source text node `1 2 34`, but the
stored Chromium capture contains synthetic siblings `2`, ` 2 3`, and `1 2 34`.
The extractor groups range rectangles by Y and then uses each group's minimum
and maximum source offsets; in vertical bidi text this creates overlapping
source segments. Do not treat that corrupted text inventory as an engine
expectation. Diagnose and repair capture using actual Chromium DOM ranges,
preserving source text exactly and retaining the browser's measured geometry;
recapture affected references only through that corrected extractor.

There is a separate genuine box discrepancy in that fixture: the paragraph's
default margins remain on physical top/bottom instead of the block axis.
Trace the user-agent defaults and logical-property mapping before changing
layout arithmetic. HTML §15.3.3 and CSS Writing Modes §7 govern that behavior.

### Capture milestone: source-preserving text fragmentation

Priority: P1 prerequisite for trustworthy inline comparisons. Chromium's raw
range report (`temp/wpt-range-diagnostic.json`) confirms two columns for the
original `1 2 34` source. A wrapping space has zero-height carets on both
columns, while visible characters occupy only their actual columns.

Project range grouping onto the computed writing mode's block/inline axes.
Join adjacent bidi runs on the same block interval, keep separate multicolumn
regions, and assign each Unicode scalar to one measured line. Emit contiguous
source spans in source order, preserving unpainted characters, and measure each
span with DOM Range. Never derive overlapping spans from per-line min/max
offsets. [CSSOM View §9](https://www.w3.org/TR/cssom-view/#extensions-to-the-range-interface)
defines range rectangles; CSS Writing Modes §6 defines the axes. This changes
capture only, without a Lambda semantic/design ruling.

Acceptance: preserve the source text exactly across horizontal wraps, both
vertical directions, RTL/bidi, columns, supplementary characters and hidden
text. Check the original vertical RTL span contents and measured column
geometry. Run one browser integration fixture and recapture original WPT
examples before regenerating affected suite references. No hand-edited JSON,
comparison tolerance changes or accepted-baseline updates.

### Milestone 2: HTML UA spacing follows the element's flow

Priority: P1. Ordinary paragraphs, headings, quotations and lists get margins
and indentation on the wrong axis in vertical documents, displacing following
content and reducing available inline size. The first disagreement is default
style resolution, before block layout. Both the initial UA application and
the font-size refresh write physical top/bottom; the refresh also duplicates
the default table and loses nested-list suppression.

Use the shared `HtmlUaElementDefault` table and `layout_element_logical_sides`
to apply logical margin/padding defaults once before author resolution and
refresh untouched UA lengths after the computed font/flow resolve. The same
helper serves both stages; serialized UA CSS uses logical property names.
Body's all-side 8px margin remains physical. Explicit physical/logical author
declarations win through the existing specificity mechanism, including `all`
resets. Directional presentational hints are applied before choosing sides.
This follows HTML §§15.3.3, 15.3.6, 15.3.7 and CSS Logical Properties §§4.2/4.4;
no Lambda S#/D# ruling changes.

Acceptance: horizontal and both vertical flows; inherited RTL and explicit
direction; changed font size; headings, paragraphs, pre, blockquote, dd, lists
and nested lists; author overrides and `all:initial`; horizontal descendants
inside a vertical parent. Native checks inspect used spacing and an ordinary
paragraph's box. Compare original abs-pos fixtures and nearby block-flow
cases, then run the Radiant baseline and dimension-cast lint at this boundary.
Do not include unrelated positioning/sizing residue in this finite milestone.

The focused spacing check passes all initial flow/font/override cases. Adding
retained mutations and local `dir=rtl` exposed an existing shared-direction
query defect: logical-side selection consulted CSS and ancestors but skipped
the element's own HTML directionality hint. Resolve the element direction in
one helper (explicit CSS, local hint, inherited direction), reused by logical
sides, logical float/clear and box-property application. Explicit CSS
inheritance/initial values retain their precedence over presentational hints.

The original absolute-positioning fixture now has correct paragraph inline
extent and source text fragments. It still exposes separate defects: vertical
parent/first-child block margins add instead of collapsing, and the absolute
span's static block coordinate is displaced. Preserve both failures for the
following finite milestones rather than adjusting offsets in UA resolution.

Qualification in progress: all 218 native text-query checks pass, including
explicit CSS ltr/initial/inherit overriding HTML hints and retained mutations.
The original `block-flow-direction-004.xht` still matches fully. The selected
vlr/vrl block-flow neighbors retain geometry failures; do not classify them as
fixed. The dimension-cast lint passes. The Radiant aggregate passes (6,324
reported cases: 6,179 pass, 143 existing partial results, two skipped).
Milestone 2's UA spacing core is complete; writing-mode layout remains partial.

### Next milestone diagnosis: adjoining block margins in vertical flow

The independent probe `temp/wpt-margin-collapse-probe.html` places an 8px
parent margin around a nonempty child with 16px block margins, followed by a
20px sibling. Chromium places the LR parent and child at the same 16px block
offset, gives the parent a 40px block size, and places the following sibling at
72px. The RL equivalents mirror these positions at 144px and 108px in a 200px
block extent. The horizontal control confirms the same collapse relationship.
Browser evidence: `temp/wpt-margin-probe/wpt-margin-collapse-probe.json`.

CSS Writing Modes §7.4 maps CSS2.1 §8.3.1 collapsing onto block-start/end.
Trace `vertical_flow_effective_block_start_margin`, vertical extent calculation,
physical publication and final root sizing as separate stages. A propagated
margin must move the parent border box while being consumed once at the child
edge; it must not remain inside the parent's content extent or be added again
by root sizing. Reuse existing edge/BFC predicates and margin-chain helpers.
Confirm native probe geometry before choosing the implementation owner.

Native evidence confirms the horizontal control exactly. In LR, parent and
child start at 16px, but parent width is 56px and the following sibling starts
at 80px; in RL the extra content width shifts the parent's start as well.
The vertical extent calculation includes an adjoining child's trailing margin
inside the parent, while the parent's outward end margin remains unpropagated.
The start-adjacency predicate also mistakes an inline replaced image for an
adjoining block child, and root finalization uses raw body margins instead of
the propagated collapse set.

Milestone 3 core acceptance: nonempty parent/child block chains in LR/RL,
positive and mixed-sign margins, a sibling after the chain, padding/BFC guards,
a definite block-size guard, and a minimum size below the content extent. Verify the ordinary
paragraph/image WPT case's parent/child geometry. Self-collapsing empty chains,
floats/clearance combinations and fragmented margins remain separate residue
unless these checks reveal a shared blocker.

Design: one edge-adjacency query, parameterized by block-start/end, selects
normal in-flow blocks through collapsed whitespace; inline content interrupts
adjacency. Accumulate the existing positive/negative margin-chain components
through adjoining non-BFC ancestors. Use the same query for content extents,
child placement and outward margins, and consume adjoining child margins
exactly once. Promote the shared outward-edge calculation for root sizing;
remove its separate trailing-margin scan. Focused native/probe and original
WPT checks precede the Radiant gate and dimension-cast lint at this boundary.

Chromium independently confirms 24 probe configurations across the three
flows, including an 8/−16/20 start chain whose collapsed margin is 4px. The
small nonzero minimum size does not prevent block-end collapse for a nonempty
child; CSS2 §8.3.1 restricts that exception to a collapse set also adjoining
the parent's start margin. Do not introduce an unconditional min-size guard.
Report: `temp/wpt-margin-probe/wpt-margin-cases.json`.

The first implementation passes the ordinary vertical edge/BFC/size cases.
The mixed chain exposes two additional shared causes: logical spacing resolves
a two-value pair once and writes the first value to both sides; horizontal
nonempty start propagation and bottom-chain construction fold signed components
too early. Keep the measured expectations and repair those owners. Logical
pair projection uses the existing component helper, ordered block-start/end
and inline-start/end (including RL/RTL). The edge-chain collector now shares
resolved pre-collapse margins with horizontal nonempty start propagation;
horizontal end propagation preserves both signs in its existing chain fields.
This expands Milestone 3 to the concrete shared defects exposed by its control.

Focused result: all 24 flow/collapse configurations now pass, including the
horizontal mixed-sign control, and the UA spacing regression still passes.
The original `abs-pos-non-replaced-vlr-013.xht` now matches every paragraph,
image, root/body and container box, plus its two source text fragments. Its
remaining difference is the absolute span's 160px static block offset. This
isolates the next positioning milestone. The Radiant gate and lint are running
for the current margin/pair implementation.

The aggregate catches a traversal regression before milestone completion:
`flow_adjoining_child` follows raw sibling links past the first placed node,
including skipped text DOM nodes without a text view. The assertion stack is
`view_require_text` → `flow_adjoining_child` → `flow_margin_chain`; the existing
content-visibility, page-load and cascade-memory checks reproduce it. Skip
unplaced siblings consistently with the placed-edge API, then repeat affected
checks and the required gate. Do not mask their process failures.

The placed-view correction passes nine focused content-visibility/keyboard UI
cases and the real-page cascade-memory check. The new nonempty-chain collector
also only replaces a horizontal collapse set when its edge query identifies
the same child; existing inline-continuation ownership stays with the existing
collapse path. `block-container-block-in-inline-001` again matches fully. The
two focused margin/UA native checks still pass; a fresh aggregate is running.

That rerun clears the process failures and inline-continuation regressions,
leaving `ortho-writing-mode-001` and `stretch/block-height-008`. Their first
disagreement is a missing formatting-context boundary: the shared BFC predicate
does not recognize a changed writing mode. CSS Writing Modes §3.2 requires an
independent block formatting context there, including opposite vertical block
flows. Recognize the used flow transition in that shared predicate; keep the
percentage margin inside its orthogonal root instead of restoring the old
percentage-specific placement exception. Recheck both original sizing tests,
the finite margin/spacing cases, and the aggregate.

Both original sizing regressions now match Chromium fully, and the finite
native checks still pass. The final aggregate for Milestone 3 is running.

The next gate leaves four vertical-position cases with only a body-box origin
discrepancy; all their positioned descendants match. The root cause precedes
positioning: `layout_block_writing_mode(html)` reports the root's computed
horizontal value when a vertical body establishes the principal writing mode.
The new BFC predicate therefore mistakes the body for a changed-flow boundary.
Existing root finalization only compensates late for vertical body geometry.
CSS Writing Modes §§8/8.1 require the body's computed writing mode to supply
the root's **used** mode, while preserving the root's computed mode for
inheritance, logical-property mapping and lengths. Repair the shared used-mode
query using the existing DOM body lookup, rather than exempting body from BFC
rules. Verify used/computed separation and a retained body-mode change, then
the four original positioning controls, sizing controls and native cases.

The used-mode query fixes all boxes in the original positioning control but
exposes premature wrapping of its leading text. Root context initialization
clears `content_height`; the vertical auto-inline branch marks a definite
height without publishing its content/border-box extent. A parallel vertical
body then lays out against a zero provisional inline extent and is enlarged
only after its text has wrapped. Publish the root's definite inline size and
context content extent together before laying out descendants, for explicit
heights and auto vertical inline sizing. A retained root/body test now checks
that a short multiword inline remains on one line as the body changes mode.

The extended native test reproduces the premature wrap (only three of eight
source bytes in the first rectangle), then passes with the complete source on
one line after root extent publication. `vrl-ltr-ltr` again matches elements,
spans and text fully. Principal-mode inheritance also uses the shared computed
writing-mode query: `inherit`/`unset` walk ancestors and `initial` selects
horizontal-tb. Remove the duplicate intrinsic fallback parser so both queries
apply the same inheritance policy.

Capture milestone complete: 1,980/1,980 primary browser captures succeed, then
165 affected single-line bidi captures are regenerated. The audited inventory
has 41,152 text segments, no non-whitespace multi-rectangle fallback segments
and no invalid UTF-16 text lengths. Browser integration checks pass. Evidence:
`temp/wpt-writing-reference-audit.json`, the primary recapture log and the
targeted union recapture log. No fixtures, tolerances or accepted baselines
were changed.

Pre-gate qualification for the final Milestone 3 binary:
`218c60a376dddf8efda8e2266675155c6ee7a85d428133238247fcb39e35ac8f`.
All 220 native text-query checks pass; the complete positioning and sizing
components pass all 139 and 776 required baseline cases. The dimension-cast
lint and whitespace checks pass. The complete writing-modes run now reports
**765 full matches, 1,215 layout/text mismatches, zero execution errors and no
batch crash**. Log: `temp/wpt-writing-after-margins.log`; mismatch inventory:
`temp/wpt-writing-after-margins-failures.json`. The earlier count was 676 full
matches; capture correction contributes to this difference, so do not attribute
the entire change to engine fixes. The final required aggregate is running.

Milestone 3 qualification complete: the final Radiant baseline passes, with
6,326 reported cases (6,181 pass, 143 existing partial results and two skipped).
Log: `temp/wpt-principal-radiant-gate.log`. Proceed to the static-position
milestone; the broader writing-modes family remains partial (1,215 mismatches).

### Following milestone probe: static positions in logical axes

Independent browser probe: `temp/wpt-static-position-probe.html`, with measured
geometry in `temp/wpt-static-position-probe/wpt-static-position-probe.json`.
It covers horizontal/LR/RL × ltr/rtl, direct/nested inline origins, margins and
explicit insets, using unequal physical box dimensions. Browser static X is
the column's block offset (20px LR, 80px RL in a 120px block extent), independent
of inline direction. Inline direction instead determines Y (40px ltr, 20px rtl
in an 80px inline extent). Explicit left/top insets remain physical.

The current engine disagrees in 14 of the 24 probe cases, including horizontal
RTL. The positioning path computes physical X through the horizontal inline
cursor and can later reflect it using inline RTL, even when X is the vertical
block axis. Nested inline origins also enter a distinct publication path.
CSS Position 3 §3.5.2 and CSS Writing Modes §7 govern the repair. Trace the
recorded hypothetical inline position through final line alignment and
containing-block conversion before choosing a shared logical-axis owner.
Do not fix this by adjusting the observed WPT column offset.

Capture qualification also adds a single-line RTL bidi control. Its multiple
range rectangles must form one complete measured line rectangle; otherwise
the comparator reads only the first run. The integration check reproduces and
now verifies this case. After the primary recapture finishes, recapture every
reference still containing a non-whitespace text segment with multiple raw
range rectangles through Chromium; those identify the affected fallback.

### Milestone 4: absolute static rectangles and vertical line constraints

Priority: P1. Wrapped RTL/vertical inline content placed absolute anchors in the
wrong column or at the wrong margin edge. The initial largest related families
had 68 RL and 64 LR failures. CSS Position 3 §3.5.2 defines the static rectangle;
CSS Writing Modes §§2/7 define its parent formatting axes. This milestone covers
normal self-alignment in horizontal-tb/vertical-lr/vertical-rl flow and preserves
existing owners for explicit self-alignment, sideways, flex/grid static
rectangles and multicol fragment anchors.

Core acceptance: an independent 24-case Chromium probe with unequal physical
dimensions covers horizontal/LR/RL × ltr/rtl × direct/nested origins, directional
margins and explicit insets. A further 36 browser controls cover asymmetric
padding/borders, a relative inline containing block, an orthogonal absolute
child, signed indentation on empty lines, center/end alignment, and block-origin
rectangles before/after inline content. The original WPT case is
`abs-pos-non-replaced-vlr-013`. Existing bidi/signed-indent, inline-CB, nested
multicol, inline-table baseline and intrinsic/stretch sizing controls remain
required.

Final design and ownership:

- `layout_setup_inline_direction` shares the existing direction, upright text,
  plaintext and outside-marker policy between normal flow and the final flex
  content passes. The RTL flex line previously reached FriBidi with an LTR base.
  A three-sibling LTR/RTL/LTR regression checks both parent and sibling leakage.
- `layout_inline_content_measure` selects the resolved vertical inline extent
  from physical height at initial setup and after block-axis shrink-to-fit.
  Pending automatic sizing keeps its existing measurement path; decoration-only
  provisional height cannot be used as an inline constraint. Atomic inline fit
  checks use the same parent inline axis as cursor advancement.
- Recursive intrinsic child measurement receives the queried vertical parent's
  known inline constraint in a scoped containing context, then restores its
  caller. The existing cache key therefore records the actual parent size.
  Previously, a query could substitute an unrelated ancestor's height and floor
  an otherwise 116px container at 248px.
- A pass-local `PositionProp` record retains the hypothetical static rectangle
  in its parent formatting context. An inline origin keeps a zero-inline-extent
  cursor through the existing bidi and line-alignment owners; a block origin
  spans the parent's inline content and follows any preceding formatted line.
  Existing line completion adjusts its retained block distance if later content
  enlarges that line. `BlockContext::inline_block_start` records the initial
  surrogate block edge for both normal and manual line initialization.
- Final static-descendant resolution selects the physical margin edges only
  after parent width, child size and inline-CB geometry are definite. Its block
  axis depends on LR/RL, independently of inline RTL. Reuse the existing
  parent-to-CB walk with the retained anchor's physical-coordinate contract.
  Author insets remain owned by the positioned-axis solver.
- Multicol already publishes physical fragment anchors. Promote and reuse the
  existing multicol-ancestor query to preserve that owner; do not republish those
  boxes with the unfragmented static rectangle at final document resolution.

The captured failures also verified two reference defects in the sizing gate:
`vert-block-size-small-or-larger-than-container-with-min-or-max-content-2a/2b`
repeated source characters in their vertical text fragments. Fresh Chromium
captures preserve `container: ` / `min-` / `content.` while retaining the same
measured element geometry (first outer 212px, first container 116px). Refresh
both full and cached schema-v2 references through the existing serializer.
Original captures are backed up under `temp/wpt-static-sizing-reference-backup`;
new unmodified browser evidence is in `temp/wpt-static-sizing-controls`.
No WPT source, comparator tolerance or accepted baseline was changed.

Diagnosis evidence and rejected alternatives: explicit 20px absolute-child
sizes preserved the initial failures, excluding auto-size timing as their only
cause. Bidi output was consistent with its incorrect flex paragraph input;
no vendor-library change was needed. A proposed change to the general atomic
cross-metric helper failed focused controls and was reverted. The actual
intrinsic-width cause was the missing recursive containing inline constraint.
That helper's broader surrogate metric contract remains unchanged.

Focused qualification:

- The original WPT case matches every element, span and text comparison.
- All 60 independent static-rectangle controls, the flex direction regression
  and the existing native controls pass; the full native text-query suite is
  223/223 (`temp/wpt-static-final-native2.log`).
- The inline component passes all 169 required cases; sizing passes all 776
  required cases (`temp/wpt-static-final-inline2.log`,
  `temp/wpt-static-final-sizing2.log`). Two sizing cases outside its recorded
  baseline and 20 missing references remain unchanged qualification limits.
- Dimension-cast lint and `git diff --check` pass.
- The full writing-modes run has **918 full matches / 1,980**, **1,062 layout/text
  mismatches**, **zero execution errors** and no batch crash. Relative to the
  qualified Milestone 3 run, 153 fixtures improve and none regress. Log:
  `temp/wpt-writing-static-qualified.log`; inventory:
  `temp/wpt-writing-static-qualified-failures.json`.
- Required aggregate qualification passes: 6,329 reported cases, 6,184 pass,
  143 existing partial results and two skips, with zero failures. Log:
  `temp/wpt-static-final-radiant-gate.log`. Qualified binary SHA-256:
  `308ab8ac13d8f62e59430b3963c2f5b012ffc275fc831f402c37e875f971cb5a`.

### Milestone 5: orthogonal automatic inline sizes (active)

Priority: P1. A vertical text block inside a normal horizontal auto-height
container is assigned no real inline constraint; decoration-only provisional
height can leave it as a very wide, short strip. The original
`sizing-orthog-vlr-in-htb-001` expects a 66×800 border box at the suite's
1200×800 viewport. This affects ordinary embedded vertical text and is a shared
prerequisite for intrinsic parent sizing, ahead of isolated bidi residue.

Use CSS Writing Modes §§7.3.1–7.3.2: fit-content inline size is bounded by its
min/max-content sizes and stretch-fit space. A definite containing block gives
that space; otherwise the fallback is capped by the initial containing block,
the containing block's fixed min/max bounds and the nearest applicable
scrollport. An arbitrary fixed-height non-scroller ancestor is not a fallback.
Preserve definite zero, content-box versus border-box decorations, and original
CSS auto-ness for percentage/cyclic sizing. Do not infer definiteness from a
positive provisional used size.

Finite first core: LR/RL blocks in horizontal flow, short unbreakable text,
long unbreakable text and soft-wrapping text, each with definite versus automatic
containing height. Include the original sizing fixture and `available-size-004`
as controls. Establish a shared intrinsic-inline measurement contract from the
existing text/element measurement owners before editing. Keep mixed formatting,
multicol fallback, parent shrink-to-fit and the opposite orthogonal direction
as subsequent milestones unless a control reveals a shared blocker. Verify
focused native/browser geometry, the relevant sizing families, then the required
Radiant aggregate at the boundary.

Milestone 5 baseline on the qualified binary: the original LR fixture is
46×1181.4 instead of 66×800 (`temp/wpt-orthog-m5-initial.log`), confirming that
it wraps against a horizontal ancestor's width instead of a vertical inline
constraint. Independent 320×200 browser measurements in
`temp/wpt-orthogonal-inline-browser.json` cover the 12-case core plus definite
zero and definite 400px parents (24 cases total). A definite containing height
larger than the viewport is not capped by the viewport fallback.

First implementation contract: reuse `layout_collect_inline_text` for direct
text/forced-break content and the existing text intrinsic-line measurement
owner, preserving the current font and text/wrap options. Return an unsupported
query for mixed element formatting; do not flatten styled/atomic children into
a single font. Resolve the physical-height fit-content constraint before block
flow begins, retain its used-size state separately from the authored CSS auto,
and let existing vertical publication determine the resulting columns. The
fallback-space helper belongs with containing/percentage size resolution and
reuses scroll-container classification and resolved physical min/max values.

Milestone 5 focused results: the original LR fixture matches all six elements
and five text comparisons. The vertical-in-horizontal sizing family has
40/76 matches, with 16 gains and no regressions versus Milestone 4; its
remaining 36 failures are reference-document layouts with independent
positioned/percentage-sizing limitations. All 24 core controls and 16 boundary
controls pass, including live auto → 120px → auto changes, fixed zero and a
400px definite parent. Logs: `temp/wpt-orthogonal-core-final.log`,
`temp/wpt-orthogonal-boundary-final.log`, `temp/wpt-orthogonal-family-first.log`.

Boundary evidence: `temp/wpt-orthogonal-boundary-browser.json` records the raw
Chromium measurements. Chromium uses the 200px viewport instead of an outer
120px fixed scrollport or 100px maximum scrollport in this probe. The normative
Level 3 §7.3.1 fallback is 120/100px respectively. Explicit border-box height
controls independently verify the resulting 130×120 and 250×100 columns;
evidence is in `temp/wpt-orthogonal-boundary-controls.json`. Native expectations
for those rows follow the specification. No original WPT reference was changed
to conceal this browser gap. The native viewport is explicitly set to 320×200
to match the independent probe; the engine reads the current document layout
context's viewport, preserving document overrides rather than assuming the
shared UI's viewport.

This milestone implements the stable Level 3 fit-content model. Level 4's
at-risk automatic multicol triggering remains outside this finite core.
Broader native/component and required aggregate qualification are pending.

Milestone 5 pre-gate qualification: all 225 native text-query checks pass.
Sizing preserves all 776 required baseline cases and positioning all 139;
three previously partial positioning cases also improve. Dimension-cast lint
and whitespace checks pass. The complete writing-mode suite now reports
**944/1,980 full matches, 1,036 layout/text mismatches, zero execution errors**,
with 26 gains and no regressions relative to Milestone 4. Inventory:
`temp/wpt-writing-orthogonal-core-failures.json`. Required aggregate log:
`temp/wpt-orthogonal-radiant-gate.log` (running). Candidate binary SHA-256:
`a7cdf455529ebf03dd4c357436741010f1c1f336ef55619de741f84f826b6f6f`.

Milestone 5 qualification is complete: required Radiant baseline passes with
6,331 reported cases (6,186 pass, 143 existing partial results, two skips and
zero failures). The qualified binary hash is recorded above. The family remains
partial for mixed styled/atomic inline content and the opposite orthogonal
direction; these are separate finite milestones.

### Milestone 6: horizontal inline size and sibling flow in vertical parents

Priority: P1. Complete the opposite common orthogonal text embedding before
isolated bidi residue. `sizing-orthog-htb-in-vlr-001` is 778×66 instead of
1200×46, and its following paragraph overlaps the horizontal block. Evidence:
`temp/wpt-orthogonal-opposite-initial.log`.

Root causes and shared owners:

- The existing horizontal orthogonal fit-content branch uses the caller's
  surrogate content width, which represents the vertical parent's physical
  height. Generalize Milestone 5's containing-space helper by physical inline
  axis and reuse it for this fit-content query. The containing block's CSS
  definiteness and the Level 3 fallback policy remain the same in either axis.
- That branch subtracts padding/borders from its fit-content border-box result
  before a common normalization step subtracts them again. Keep the shared
  normalization owner and pass it a border-box candidate.
- `layout_publish_vertical_children` returns early for an orthogonal child
  before advancing the shared logical block cursor. Advance that cursor once
  for every participating child before choosing its physical publication path;
  following siblings must consume the orthogonal block's physical width.

Finite acceptance: independent 320×200 browser measurements cover LR/RL parents
with auto, 120px, zero and 400px width × short, unbreakable and wrapping text
(24 cases, `temp/wpt-orthogonal-horizontal-browser.json`). A before/orthogonal/
after sequence in both flows has an auto parent width of 370px; its horizontal
block is 320×70, with preceding 20px and following 30px blocks. Browser evidence:
`temp/wpt-orthogonal-horizontal-siblings.json`. Preserve all 40 Milestone 5
controls, margins, vertical inline tables, intrinsic sizing and positioned
baseline cases.

Verification budget: new native controls and original opposite-direction WPT,
the two orthogonal sizing families, required sizing/positioning components,
then full writing-mode inventory and required Radiant aggregate at the boundary.
Keep scroll/flex/grid algorithm-specific definiteness and mixed formatting
extensions separate unless a named control exposes a common regression.

Milestone 6 focused qualification: the original opposite-direction WPT now
matches every element/text comparison, including the following paragraph's
physical block offset. All 24 new dimension/overflow controls, both sibling
sequences and the 40 Milestone 5 controls pass. The complete native suite is
227/227; sizing preserves all 776 required cases and positioning all 139.
Dimension-cast lint and whitespace checks pass. The full writing-mode run is
978 matches / 1,980, with 1,002 geometry/text mismatches and zero execution
errors. Log: `temp/wpt-writing-orthogonal-horizontal.log`; mismatch inventory:
`temp/wpt-writing-orthogonal-horizontal-failures.json`. Required aggregate is
running in `temp/wpt-orthogonal-horizontal-radiant-gate.log`.

Milestone 6 qualification is complete: required Radiant baseline passes with
6,333 reported cases (6,188 pass, 143 existing partial results and two skips).
Qualified binary SHA-256:
`d607a89ae8b81bdf7cb2e73808d93f73fe399a39b7c8e5e8fed5581ded4ac47a`.

Tracking refinement: displayed fixture basenames can repeat between a test and
its reference (for example `bidi-normal-001`). Earlier score deltas and named
failure comparisons are useful aggregate evidence, but do not prove individual
identity across duplicates. Record subsequent full runs by canonical relative
fixture path through the existing harness API, preserving all comparator and
threshold behavior. Baseline inventory: `temp/wpt-writing-path-m6.json`.

Next diagnostic finding: the basic `bidi-normal-001` failure first differs in
font metrics, before bidi placement: CSS requests `ezra_silregular` from
`/fonts/sileot-webfont.woff`, but the engine uses fallback metrics (28px rather
than 36px content height). Existing WPT root-resource resolution already serves
stylesheets, images and scripts; font path admission only calls its separate
missing-relative-mirror helper. Verify that missing root-relative font sources
are routed through the existing shared resolver before investigating bidi.
The `orthogonal-parent-shrink-to-fit` family also contains browser-generated
WPT report sections absent from the engine's builtin report adapter; preserve
that diagnostic separately from actual CSS geometry.

Path-audit correction: its first helper used the JavaScript API's 5px default,
whereas the `make layout` CLI already uses 5.5px. The one apparent batch-sensitive
result (`underline-mixed-vrl-002`) has identical native geometry in both modes,
with a 5.5px CJK offset. Set the audit to the command's unchanged 5.5px policy;
there is no evidence of a batch font-state defect from this comparison.
Canonical-path baseline with matching policy:
`temp/wpt-writing-path-m6-qualified.json`. Earlier 5px audit inventories are
retained as diagnostic artifacts and are not qualification baselines.

### Milestone 7: shared resource resolution for declared web fonts

Priority: P1. Font-loading failures precede bidi placement in several common
RTL test families. The first differing stage in `bidi-normal-001` is a failed
open of `/fonts/sileot-webfont.woff`, followed by system-font metrics, not a
verified bidi algorithm error. Trace: `temp/wpt-root-font-native.log`.

Reuse the existing shared `radiant_resolve_resource_path` / WPT root resolver
for local font sources before its existing missing-relative-mirror fallback.
Do not introduce a per-family/path/font-name substitution or patch font vendors.
Keep HTTP download lifecycle, ordered `src` fallback, data/local faces and
stylesheet-relative URL bases in their current owners. Font allocations remain
font-owned under **D4.5.1v4 / D4.1.4v5**.

Finite acceptance: root-relative TTF, root file URL and root-relative WOFF
sources must produce the same glyph geometry as direct file-URL controls using
the identical font files. Preserve existing native web-font/resource lifecycle
controls. Verify the original bidi fixture and the normal/embedding/isolation
families, then the required Radiant gate and full path-aware writing-mode run.
Browser-generated WPT result panels in the parent-shrink-to-fit fixtures are a
separate harness integration issue; do not alter CSS comparisons to hide them.

Milestone 7 qualification: all three new source forms fail before the fix and
match their direct-file controls afterward. All 228 native tests pass, as do
all 22 normal-bidi fixtures. CSS Fonts 3 §4.3 requires URL resources to resolve
against their stylesheet base; the font loader now uses the same established
local-root policy as other document resources. The full requested command and
canonical-path audit both report **1,169 / 1,980**, with 811 mismatches, zero
execution errors, **191 new full matches and zero path regressions** against
Milestone 6. Required Radiant baseline passes: 6,334 reported, 6,189 pass,
143 existing partial results, two skips. Lint and whitespace checks pass.
Evidence: `temp/wpt-root-font-{before,after,native-suite,radiant-gate}.log`,
`temp/wpt-writing-root-font-qualified.log`, `temp/wpt-writing-path-m7.json`,
`temp/wpt-root-font-delta.json`. Qualified binary SHA-256:
`414243445da0266eda5ad6fac403307807c52bf328b9de813912c20fa5386f94`.

### Milestone 8: vertical inline constraints and physical box decorations

Priority: P1. Parallel vertical block children must stretch their margin boxes
to the containing inline size, rather than add their own padding/borders after
filling its content size. CSS Writing Modes 3 §§7.1–7.2 maps the sizing algorithm
to inline/block dimensions while keeping physical border/padding sides physical.
An authored body inline size also survives principal-flow root publication.

The first failing stage is used sizing, with separate evidence for its owners:
`block-flow-direction-vrl-002` has child border heights 220px versus 180px, a
60px column versus 20px, and a final body height overwritten to 784px. Browser
controls isolate the same problems in ordinary nested blocks, so the fix must
not depend on body tags or WPT filenames. Use the existing stretch-fit/minmax
and box conversion helpers; keep CSS keywords distinct from resolved sizes.
Logical flow measurements must remove surrogate-axis decorations and add the
correct physical-axis decorations exactly once before geometry publication.
Retained views remain owned under **D4.5.1v4 / D4.1.4v5**.

Finite acceptance: 24 controls across vertical-lr/rl, nested/root flow, plain,
inline-only border, block-only border, asymmetric padding, margins and both box
sizing modes. Browser evidence and before-fix geometry:
`temp/wpt-vertical-stretch-browser.json`. Preserve explicit inline sizes,
orthogonal fit-content, inline tables, min/max constraints and positioned flow.
Verification budget: focused native controls and original block-flow family,
neighboring native text/sizing/inline tests, canonical WPT path delta and the
required Radiant gate once stable. Orthogonal mixed-inline intrinsic sizing
and unrelated float/sideways/report-panel support remain separate milestones.

Focused evidence: all 24 initial controls and their 180→120→180 CSSOM reflows
pass, as do all 229 native cases. First full WPT audit reaches 1,204 matches
(35 gains, zero path regressions against Milestone 7). Neighboring qualification
identified three related vertical intrinsic-size regressions: inline-blocks
previously passed through the ordinary parallel-block stretch path, whose
padding error happened to equal their min-content overflow. Inline-blocks and
floats require fit-content inline sizing; preserve the ordinary stretch fix and
reuse the existing direct-text intrinsic owner for these atomic flow roots.
The first aggregate is not a qualified boundary: its layout component reports
these three regressions. Evidence: `temp/wpt-vertical-stretch-sizing-detail.log`.

The final shared classification uses stretch-fit for ordinary parallel blocks
and fit-content for atomic flow roots. Rename the transient fit-content flag to
`auto_inline_fit_content_resolved`, clear both derived-auto states before reuse,
and share the existing logical text intrinsic/minmax solver. Add 12 atomic and
ordinary-block controls under block/inline-block parents in both vertical modes.
The expanded control also exposed a separate post-shrink error: recomputing a
parallel child's auto block size through a block-child-only extent erased its
text columns. A parent block-axis shrink leaves that child's inline constraint
unchanged, so preserve its already measured used block size.

Final focused qualification: all 230 native tests pass, including the 24 initial
controls and 48 CSSOM reflows. All 776 sizing, 169 inline and 139 positioning
baseline cases pass. Full command and path audit report **1,232 / 1,980**, with
748 mismatches, zero execution errors, **63 new full matches and zero path
regressions** against Milestone 7. Evidence:
`temp/wpt-vertical-stretch-final-native.log`,
`temp/wpt-vertical-stretch-boundary-components.log`,
`temp/wpt-writing-vertical-stretch-final-qualified.log`,
`temp/wpt-writing-path-m8-final.json`, `temp/wpt-vertical-stretch-delta.json`.
The fresh required aggregate is running in
`temp/wpt-vertical-stretch-final-radiant-gate.log`.

Milestone 8 aggregate qualification passes: 6,336 reported, 6,191 pass,
143 existing partial results and two skips. Dimension-cast lint and whitespace
checks pass. Qualified binary SHA-256:
`be913d022fa3f7d211b500833049ee748d260fd24f3c3b1ad10cf68c1d8a8a69`.

### Milestone 9: translate CSS bidi scopes into the reordering input

Priority: P1. CSS Writing Modes 3 §§2.2, 2.4.2 and 2.4.4 require CSS inline
embeddings/overrides to contribute Unicode control codes before UAX #9.
`layout_bidi.cpp` currently flattens their characters without those codes.
For example, `bidi-override-007` paints the overriding span before the following
number, while both its explicit-control reference and Chromium place it after
the number. Font metrics and computed properties now agree, isolating the
first defect to the reordering input. Plaintext inline scopes are also missing
from the existing isolate classifier, despite the normative FSI/PDI mapping.

Use one scope translation table for counting and filling zero-advance controls,
with no DOM/text mutation. Preserve source control codes, synthetic-fragment
ownership, decoration ranges and existing library UAX #9 implementation. Controls
have no text rectangle/paint width and must not acquire a span's border edge.
Retain the existing opaque outside/independent inside treatment for isolates;
plaintext uses its own first-strong paragraph direction, and isolate-override
also supplies the inner override. Root inline overrides apply to block containers
as required by §2.2. Scratch arrays remain line-owned under **D4.5.1v4**.

Finite acceptance: CSS scopes versus equivalent source-control geometry for
normal/embed/override under both directions, pure Latin plus following numbers,
nested scopes, block overrides and plaintext isolation. Keep existing inline
edge/atomic/static-position behavior. Verify the original override/plaintext
families, native bidi/text controls, neighboring text/inline/position baselines,
then the path-aware full WPT inventory and required Radiant gate. Paragraph-wide
soft-wrap state, exhaustive source-control/markup interference and fragmented
border fidelity remain separate unless a selected acceptance case exposes them.

Focused evidence: the 16 CSS/source-control pairs fail before the change and
pass afterward. Six root/nested/atomic controls also match Chromium after fixing
two incorrect fast-path admissions: CSS scopes must remain active on atomic
lines, and source controls outside inline wrappers still belong to the whole
paragraph. Synthetic controls carry no text rectangle and are excluded from
span decoration ranges. Plaintext joins the shared isolate classifier and its
P2 first-strong search excludes nested isolates.

All 232 native cases pass. All required neighboring inline (169), text (889
pass / 91 existing partial) and positioning (139) cases pass. Full path audit
reports **1,253 / 1,980**, 727 mismatches, zero execution errors, **21 new full
geometry matches and zero path regressions** against Milestone 8. Every override
fixture matches (24/24); plaintext is 23/24, with its BR paragraph case still
failing. Evidence: `temp/wpt-bidi-{scope-before,source-control-native,scope-components}.log`,
`temp/wpt-bidi-scope-browser.json`, `temp/wpt-writing-path-m9-second.json`,
`temp/wpt-bidi-scope-delta.json`. Required aggregate is running in
`temp/wpt-bidi-scope-radiant-gate.log`.

Rendering audit: these scores compare layout geometry, not WPT pixel equality.
The new fragment pass resolves character visual order but currently publishes
only each source rectangle's visual union. Generic paint still consumes logical
source bytes; its existing uniform-RTL special case does not cover CSS overrides.
Carry the resolved visual runs into paint as the next bounded milestone, using
layout-owned metadata rather than a second heuristic bidi implementation.

Milestone 9 aggregate qualification passes: 6,338 reported, 6,193 pass,
143 existing partial results and two skips. Dimension-cast lint and whitespace
checks pass. Qualified binary SHA-256:
`04143676432de55208a9ccfb82d68e81af4578fca8b8e2edec38c6fad763320e`.

### Milestone 10: carry resolved bidi runs into ordinary text paint

Priority: P1. Geometry qualification does not establish glyph order. The line
pass resolves visual character positions, but discards their run directions
after publishing source-rectangle unions. Ordinary paint consumes logical bytes;
its existing reverse-cluster path is admitted only for overflow/clamped text.
The first acceptance must distinguish CSS RTL override `ABCD` from the required
visual `DCBA`, with raster input positions and vector glyph bytes checked.

Preserve source/query rectangles. Publish immutable, source-indexed directional
paint runs from the existing UAX #9 result, relative to each owning TextRect.
Use existing TextRect/view-pool allocation, recycling and snapshot ownership
under **D4.5.1v4 / D4.1.4v5**. Audit every clone/release path before adding child
run storage. Reuse the existing render paint-entry/RTL cluster pipeline for
ordinary bidi runs, preserving logical source labels and grapheme boundaries.
Clipping and ellipsis retain their existing owner and consume the same entries;
paint must not run a second approximate bidi algorithm.

Finite core: horizontal paragraph overrides and natural RTL runs, mixed
directions in one source rectangle, combining/emoji clusters, nested isolates,
plain versus clipped paint and retained reflow. Raster/vector inputs must agree
with an explicit visual-order control. Keep DOM text/query geometry unchanged.
Vertical glyph orientation and contextual shaping refinements retain their own
owners; extending their paint geometry requires separate evidence rather than
guessing a horizontal projection. Verification budget: new native glyph-order
controls, existing shaping/overflow/vector controls, full path-aware WPT audit
and required Radiant gate at the stable boundary.

Implementation retains source/query rectangles and adds relative, source-indexed
paint-run children allocated through the existing TextRect pool. Snapshot clone,
failure cleanup and release recurse through those children; recycling reuses
their pool nodes. Leader copies discard derived runs before their own line
publication. Paint collection consumes these runs through the existing RTL
grapheme/overflow entries. Vector combination stops at each resolved run and
retains later runs sharing the same source rectangle. Ordinary bidi painting
now admits this plan even without ellipsis or line-clamp.

Focused acceptance passes: RTL `ABCD` reaches raster as glyphs at descending
positions and vector as `DCBA`, with logical labels preserved through RTL→LTR→RTL
reflow. Combining/emoji clusters remain intact; mixed-direction runs continue
after the first RTL run. Clipped glyphs and positions match an independent
visual-order control (`…BA`). Snapshot replay owns independent paint-run copies
and remains valid after capture release. All 236 native cases pass. Neighboring
inline (169), text (889 pass / 91 existing partial) and positioning (139) gates
pass. Full path audit preserves every layout result: 1,253 matches, zero errors,
zero per-fixture geometry changes against Milestone 9.

AddressSanitizer's separate host completes 40 script-driven reflow/snapshot
cycles and SVG paint/teardown without address errors. The output contains the
`completed` sentinel. macOS address checks use `detect_leaks=0`; this is not a
LeakSanitizer result. Evidence: `temp/wpt-bidi-paint-{before,final-native,components}.log`,
`temp/wpt-bidi-paint-layout-delta.json`,
`temp/wpt-bidi-paint-asan-{layout,render}.log` and
`temp/wpt-bidi-paint-asan.svg`. The required aggregate is running in
`temp/wpt-bidi-paint-radiant-gate.log`.

Milestone 10 aggregate qualification passes: 6,342 reported, 6,197 pass,
143 existing partial results and two skips. Dimension-cast lint and whitespace
checks pass. Qualified binary SHA-256:
`33a01d5ae5e6bc5bd4b0bc2142dc13204a829ab72862dee96031f69bef3c0c59`.

### Milestone 11: plaintext direction belongs to paragraphs, not wrapped lines

Priority: P1. `bidi-plaintext-br-001` exposes the remaining paragraph-state defect:
the line-finalization helper recomputes P2/P3 from each visual line. A soft-wrapped
Latin line after initial Hebrew therefore aligns left instead of retaining its
paragraph's RTL base; the opposite sequence similarly flips on its Hebrew line.
CSS Writing Modes 3 §2.4.1 / UAX #9 P2–P3 determine this base from the whole bidi
paragraph, independent of automatic wraps. Forced paragraph breaks restart it.

Resolve the paragraph containing the current source anchor. Scan logical inline
content across soft wraps, stop at BR/preserved paragraph separators, exclude
nested CSS/Unicode isolates from the first-strong search, and use P3's LTR default
for a neutral-only paragraph. Reuse the shared inline-boundary style resolver
rather than duplicate its CSS-wide/HTML-direction handling. Keep computed
`direction` unchanged, as required by §2.2, and share the paragraph query between
block plaintext and inline plaintext isolation. Ownership remains unchanged under
**D4.5.1v4 / D4.1.4v5**.


Finite acceptance: RTL→Latin and Latin→RTL soft wraps, neutral-only leading lines
whose first strong character occurs later, BR/preserved-newline resets, and one
causal first-strong mutation. Compare against independent explicit-direction
controls and the original WPT. Preserve existing isolates, source controls,
ordinary text alignment and paint-run metadata. Verification budget: native
paragraph/bidi controls, original WPT family, neighboring text/inline/position
baselines, full path-aware WPT audit and required Radiant gate at the boundary.
Paragraph-wide embedding-level state beyond P2/P3 remains separate.

The initial six soft-wrap/BR/newline controls and their source mutations pass;
the original plaintext family now matches 24/24. Additional P2 exclusions exposed
an atomic dependency: the engine did not wrap ordinary text at an object edge
without a literal space, and its bidi pass counted atomic boxes from earlier
visual lines. Chromium confirms a second line at x=23.34375 for the independent
explicit-RTL control; native text instead overflowed its first line. Adding a
space restores the wrap, isolating the missing object-edge opportunity rather
than a font/available-width error. Evidence:
`temp/wpt-plaintext-atomic-{browser,native}.json` and
`temp/wpt-plaintext-atomic-spaced-native.json`.

Reuse the existing atomic-boundary glue/wrap-mode admission in ordinary first-
word fit testing, keep atomic inline boxes neutral within P2, and use the shared
line-membership helper for bidi counting/filling. An RTL paragraph still requires
reordering when a wrapped line contains only Latin/neutral/atomic content. These
repairs are required by the selected atomic control, not fixture-specific
positions. Add narrow/wide atoms to exercise both earlier-line and current-line
membership; preserve all existing nowrap, glue and inline baseline controls.


Milestone 11 qualification passes: all 238 native cases, neighboring inline
(169), text (890 pass / 90 existing partial), positioning (139) and sizing (776)
checks pass. The path audit and user command both report **1,254 / 1,980**,
726 mismatches, zero execution errors: one new full match and zero path losses.
The required aggregate reports 6,344 cases: 6,200 pass, 142 existing partial
results and two skips, with zero failures. Dimension-cast lint and whitespace
checks pass. Evidence: `temp/wpt-plaintext-paragraph-{delta.json,components.log,
radiant-gate.log}`, `temp/wpt-writing-path-m11-second.json` and
`temp/wpt-writing-plaintext-paragraph-qualified.log`. Qualified binary SHA-256:
`dc8cef518cfcdf43ec262867ffad7d22cb8f42529051ea4d5e5d98df78c4cf94`.

### Deferred milestone: mixed inline content contributes along its logical axis

Priority: P1. The auto inline-size fit-content path introduced in Milestone 5
accepts direct text/BR only. A vertical orthogonal root containing inline spans
or inline-blocks therefore bypasses intrinsic fitting and accumulates an
unconstrained physical height. `available-size-001` and the fixed-height spacer
variants distinguish this from an incorrect available-space fallback.

CSS Writing Modes 3 §7.3.1 uses the logical inline min/max-content sizes;
CSS Sizing 3 §5.1 and CSS Text 3 §5 require styled text, inline decorations and
atomic boxes to participate in the same intrinsic sequence. Extend the existing
intrinsic traversal with a logical-inline query, reusing its normalization,
fonts, segment seams and forced-line accumulation. Keep physical width queries
and their caches distinct; a logical result must never populate a physical-width
memo. Do not flatten styled descendants or infer logical height from physical
width. Atomic fixed heights contribute their border-box heights; parallel atomic
auto heights use their own logical content contribution, constraints and edges.

Finite core: parallel vertical flow with text, ordinary inline spans, nested
parallel inline-blocks and fixed-height atoms, both block directions, authored
font/inline edges, forced breaks, and available-size mutations. Admit only flows
whose contributions are represented by this query; retain existing fallback for
nested orthogonal formatting contexts, forms/replaced boxes, flex/grid/table,
floats, multicolumn and containment until their axis-specific owners are joined.
No new ownership is introduced (**D4.5.1v4 / D4.1.4v5**).

Verification budget: causal native controls versus independent explicit-height
controls, original available-size family, neighboring sizing/inline/text gates,
path-aware full WPT audit and required Radiant gate at the stable boundary.


Wrap-up disposition: the logical-axis prototype is **removed**. Its causal Ahem
controls passed, but the full path audit exposed regressions in existing
available-size and text-combine fixtures. The style-probe ownership defect was
isolated: a descendant probe must own the active view and restore its parent
context. Reusing the existing shaping-context helper also restored the Mongolian
joining control. These repairs were insufficient to qualify the complete query:
the final trial still lost four available-size and two text-combine matches,
with one unrelated gain. Evidence: `temp/wpt-mixed-inline-second-delta.json` and
`temp/wpt-writing-path-m12-second.json`. Preserve this as investigation evidence;
none of the prototype engine changes or its unqualified test is retained.

The earlier requested wrap-up stopped at qualified Milestone 11. Remaining mixed-inline intrinsic work
must join the atomic wrapping and text-combine axis owners before broad admission.
Keep the existing physical-width caches separate from future logical queries.


Final wrap-up verification of the restored sources passes: the user command
again reports **1,254 / 1,980**, 726 mismatches and zero execution errors
(`temp/wpt-writing-wrap-up-suite.log`). The rebuilt native text suite is
238/238. The final required Radiant aggregate reports **6,344 total, 6,200 pass,
142 existing partial results, two skips and zero failures**
(`temp/wpt-writing-wrap-up-radiant-gate.log`, exit 0). Dimension-cast lint and
`git diff --check` pass. Final rebuilt binary SHA-256:
`05a2cc2e39ccfe72b0a4721f6544c8a6f486e7282828f50d1a72e391809fd43a`.


### Milestone 12: reconcile end insets after final physical size publication

Priority: P1. An absolutely positioned vertical box with `bottom` and automatic
height retains the provisional top after its inline content publishes the used
physical height. `abs-pos-non-replaced-vrl-014` has the correct 80px height but a
top displaced by 80px. Its computed bottom is correctly 160px. The finalization
guard deliberately skips a second vertical auto-height calculation, but also
skips the bottom constraint reconciliation nested inside that calculation.

CSS 2.2 §10.3.7 / §10.6.4 and CSS Writing Modes 3 §7.1 require physical insets,
margins and the final border-box extent to satisfy the containing-block equation.
Reconcile end-only insets after final sizing, independently of which algorithm
published that size. Reuse the existing physical-axis positioned solver and
containing-block geometry, rather than add a vertical-only coordinate formula.
Retain static-axis and explicit start-inset ownership; no allocation change is
needed (**D4.5.1v4 / D4.1.4v5**).

Finite acceptance: both vertical block directions and paragraph directions,
auto versus definite child inline-size, physical padding/borders/margins,
bottom-only versus top-only placement, and a causal size/containing-block
mutation. Compare with the CSS equation and original WPT geometry. Test normal
horizontal positioning as neighboring coverage. Verification budget: selected
native controls and original failures in the edit loop, positioning baseline,
full canonical WPT audit, then the required Radiant gate at the stable boundary.

Resumed baseline is unchanged by the merge: **1,254 / 1,980**, zero execution
errors and zero canonical-path gains/losses against Milestone 11. Evidence:
`temp/wpt-writing-resume-merged.log`, `temp/wpt-writing-path-resume-merged.json`.


The initial end-inset controls fail by exactly the final automatic child height.
The original WPT span moves from y=232 to the normative/browser y=152, retaining
its correct 80px dimensions. Finalization now calls the existing positioned axis
solver for end-only axes using the final physical border box and containing-block
offsets. The redundant late right/bottom formulas are removed from the sizing
branches; static and explicit start-inset ownership remains with its existing
solver.

Focused acceptance passes: 64 combinations cover both vertical block directions,
LTR/RTL, fixed/percentage insets, top-only/bottom-only, automatic/definite heights,
content-box/border-box and asymmetric physical decorations/margins. Each separately
mutates source text and containing-block height. All 241 native cases and all
139 neighboring positioning cases pass. The complete path audit reports
**1,266 / 1,980**, 714 mismatches, zero execution errors: **12 gains and zero
path regressions**. Evidence: `temp/wpt-end-inset-{before,after,native,position}.log`,
`temp/wpt-end-inset-delta.json`, `temp/wpt-writing-path-m12-end-inset.json`.
The required aggregate is running in `temp/wpt-end-inset-radiant-gate.log`.


Milestone 12 aggregate qualification passes: **6,422 total, 6,278 pass,
142 existing partial results, two skips and zero failures**. Dimension-cast
lint and whitespace checks pass. Qualified binary SHA-256:
`2f80aea9d16bb16a051d81b61a14c6844e2feed44aa0f18a885dafc2af03f6d5`.
The user-command confirmation is `temp/wpt-writing-end-inset-qualified.log`.


### Milestone 13: computed orientation selects font-relative advance metrics

Priority: P1. CSS Writing Modes 3 §5.1 makes text-orientation inherited; CSS
Values 4 §6.1.1 defines ch/ic through the glyph advance in the element's inline
axis. Sideways writing uses horizontal typographic metrics. The native length
resolver reads only the local specified text-orientation and treats all vertical
box axes alike. An empty box inheriting upright is 40px for 4ch while an otherwise
identical box declaring upright locally is 80px. Empty boxes isolate conversion
from text layout; native CSSOM's inherited font computation already has the
correct basis.

Reuse the shared cascaded-value consumer in css_prop_table.cpp for a typed
keyword query, including inheritance, CSS-wide keywords and variable validation.
Use it to select the font metric's computed writing-mode/text-orientation pair.
Expose the typed computed keyword for native consumers without adding another
ad hoc ancestor walk. This milestone converts the font-unit owner; independent
direction/geometry consumers retain their own subsequent acceptance cases. Preserve the selected declaration
owner and allocation boundaries (**D4.5.1v4 / D4.1.4v5**).

Finite acceptance: local/inherited upright versus initial/mixed/sideways,
inherit/unset, variable-supplied values and an inherited-value mutation; vertical
versus sideways versus horizontal writing modes; ch/ic dimensions.
Compare inherited cases to identical explicitly styled controls, and audit the
original ch-unit family. Glyph orientation/shaping/painting and independent
vertical-flow sizing remain partial and outside this font-unit milestone.
Verification budget: causal native comparisons plus Chromium probe, existing
font/style controls, full canonical suite audit and required Radiant gate.


The typed native keyword consumer reuses CSSOM's complete cascade/substitution
walk and returns only an enum, so no temporary value escapes its owning pool.
The font-unit converter queries the computed writing-mode/orientation pair;
sideways modes select horizontal metrics even when upright is inherited.

Causal A/B is verified with the new native test actually selected: replacing
only the converter with its preceding implementation fails the inherited ch
control (40px versus its 80px local control); restoring the repair passes.
80 source combinations cover five writing modes, ch/ic, inherited/local/reset/
variable values and three ancestor mutation states. All 242 native cases pass,
and the live CSSOM font-unit fixture passes 58/58 assertions. The full path audit
retains **1,266 / 1,980**, zero errors and zero per-fixture metric changes.
Evidence: `temp/wpt-font-orientation-native-{before-qualified,after}.log`,
`temp/wpt-font-orientation-native.log`, `temp/wpt-font-orientation-cssom.log`,
`temp/wpt-font-orientation-delta.json`.

Chromium's empty-box probe independently confirms equality of inherited and
local upright, but gives 92px for both, versus native's repaired 80px for both.
That absolute vertical-font metric difference remains unresolved; this milestone
does not claim glyph/metric parity or qualify the still-failed original ch-unit
family. The required aggregate is running in
`temp/wpt-font-orientation-radiant-gate.log`.


Milestone 13 aggregate qualification passes: **6,423 total, 6,279 pass,
142 existing partial results, two skips and zero failures**. Dimension-cast
lint and whitespace checks pass. Qualified binary SHA-256:
`b9ece4aaca3da851052b48bb74225bf027c0446dad92341ff6a4eca88159fd90`.


### Milestone 14: atomic inline objects share bidi placement and line alignment

Priority: P1. The four remaining vrl positioned-image reference documents expose
an ordinary horizontal RTL defect. In `abs-pos-non-replaced-vrl-006-ref`, both
images are 850px too far right; 850px is exactly the line's unused inline space.
Their relative separation and vertical inset are already correct. Replaced boxes
receive provisional RTL positions, then bypass bidi placement (`has_atomic` or
zero text rectangles), and line alignment applies the unused space again.

CSS 2.2 §9.4.2 / §16.2 and CSS Writing Modes 3 §2.4.3 require atomic inlines to
participate as U+FFFC objects in UAX #9, with alignment applied to the resulting
line. Reuse the existing atomic-inline classifier and character fragment path for
replaced/other atomic boxes, including lines with no text rectangles. Use the
existing logical flow origin rather than an already provisionally aligned box.
Preserve relative offsets when assigning the resolved positions; extract the
existing physical-to-flow relative-offset projection and reuse it instead of
copying its writing-mode mapping. No retained ownership is added
(**D4.5.1v4 / D4.1.4v5**).

Finite acceptance: image/inline-block lines with and without a Latin prefix,
LTR/RTL, start/left/center alignment, first/last relatively positioned object,
fixed/percentage offsets, disabling/restoring positioning and containing-block
resize. Verify both source order and normal-flow spacing independently of visual
relative offsets. Include vertical neighboring controls when the shared atomic
advance changes. Verification budget: selected native controls and original WPT,
inline/text/position neighbors, canonical full suite and required Radiant gate.

The earlier absolute font-unit difference is not being fitted to Chromium.
Explicit Times and default Times New Roman are different selected fonts, and
CSS Writing Modes 3 §5.1.1 leaves synthesized vertical metrics unspecified.
The reduced own-font and preceding-content controls are stable. A separate
absolute metric change needs evidence of a font-selection or consistency defect.


Milestone 14 implementation routes replaced objects and atomic-only lines through
the existing U+FFFC bidi fragments. Margin-box advances retain signed margins;
visual relative movement is excluded from line ordering and then restored.
The vertical neighbor exposed a second owner: physical publication rebuilt
positions in DOM order from coordinates already shifted by relative positioning.
It now excludes relative movement from normal-flow calculations and restores it
on physical publication. For a definite physical inline measure it maps the
finalized surrogate inline coordinate, preserving bidi order and text alignment.
The inline placement stage uses top margins on its vertical surrogate cursor.
CSS Position 3 §3.3 requires relative movement to leave subsequent normal-flow
placement unchanged.

The first aggregate rejected applying this projection to automatic inline sizes:
`dominant-baseline-mixed-writing-modes-002` and three `vert-block-size-...-2`
fixtures regressed. Their mixed-content intrinsic measure remains incomplete;
the surrogate wraps in the wrong dimension before publication. A definite inline
measure is therefore an explicit precondition of this milestone. Automatic lines
retain their existing reconstruction pending repair of that earlier sizing owner.
No reference, tolerance, baseline or result classification was changed.

Finite vertical acceptance now includes physical top/bottom margins (including
negative margins), fixed and percentage relative offsets, static/relative/static
mutation, LTR/RTL and start/left/center alignment. Cross-axis baseline parity and
mixed-content automatic inline measurement remain outside this repair. Initial
canonical inventory reached 1,288 / 1,980 with zero lost passes and zero execution
errors; qualification is pending the corrected regression controls, canonical
audit and required aggregate. Evidence and final results will follow below.


The corrected Milestone 14 controls restore inline **169/169** and sizing
**776/776**, including all four rejected neighbors. All **244 native tests pass**;
the vertical control covers 96 source combinations and three positioning states.
The canonical audit and the user's exact `make layout` command agree on
**1,288 / 1,980**, **692 mismatches**, **zero errors**, **22 gained passes and zero
lost passes** versus Milestone 13. One already-partial reference,
`float-contiguous-vrl-006-ref`, decreases from 88.9% to 77.8% element agreement;
its wrapped RTL replaced-object line remains unresolved. It is retained as a
failure, with unchanged reference and tolerance. The earlier CJK partial decrease
is absent with the corrected definite-measure precondition.
Evidence: `temp/wpt-m14-definite-publication-{native,audit}.log`,
`temp/wpt-m14-definite-regression-inventory.log`,
`temp/wpt-m14-definite-publication-delta.json`, and
`temp/wpt-m14-definite-user-suite.log`. Required aggregate rerun:
`temp/wpt-m14-definite-radiant-gate.log`.


Milestone 14 is qualified: required aggregate **6,425 total, 6,281 pass,
142 existing partial results, two skips and zero failures**. Dimension-cast lint
and whitespace checks pass. Qualified binary SHA-256:
`1d73beb3565b0e969ee250340cc7c75c38e8b78d2fd39b1234b5ccf25a5dd8a3`.

### Milestone 15: percentage inline sizes use the orthogonal fallback basis

Priority: P1. `sizing-orthog-prct-vlr-in-htb-001` specifies height:50% inside an
indefinite horizontal containing block. Native clears the percentage as an auto
block height and fills an 800px fallback; the reference resolves 50% of 800px,
then adds 70px of borders, yielding 470px. The percentage's owner is the child's
inline axis, so applying CSS 2.2 §10.5's indefinite block-height rule loses it.
CSS Writing Modes 3 §7.3.1 instead supplies a definite fallback basis for inline
computations; §7.3.2's fit-content rule applies to automatic preferred sizes.

Reuse `layout_orthogonal_inline_basis`, including fixed containing maximum,
nearest fixed scrollport and initial containing block constraints. Share the
orthogonal-inline predicate between used length conversion and automatic-height
classification. Keep its effective basis in resolved-style geometry dependency
keys so ancestor constraint and viewport mutations cannot reuse stale values.
No new retained pointers are needed (**D4.5.1v4 / D4.1.4v5**).

Finite acceptance: perpendicular horizontal/vertical boxes; percentage and calc
preferred inline sizes; definite/indefinite/zero containing dimensions; fixed
containing maximum and min/max floor; content/border sizing; selected min/max
constraints; viewport/ancestor mutations; ordinary parallel percentage block
sizes retain their existing automatic behavior. Exclude absolute-positioned
percentage sizing, owned by its containing-block solver. Verification budget:
causal native A/B, browser probes, focused original family, native and relevant
sizing/position controls, canonical audit and required Radiant aggregate.

Preimplementation Chromium probe (`temp/wpt-orthogonal-percentage-browser.json`)
confirms 48/56 equations, including zero, min/max floor and box sizing. Its eight
nearest-scrollport percentage results use the viewport instead of the normative
fixed scrollport fallback. Keep that browser/spec disagreement explicit; do not
fit the shared fallback helper to those browser values. The specification's
scrollport rule remains the expected native behavior.


Milestone 15 implementation repairs both the preferred-size converter and the
later cyclic-height guard; plain percentages and equivalent calc expressions
now share the fallback basis. Deferred physical-width min/max re-resolution uses
that same basis. The resolved-style key includes its scalar X/Y values so live
ancestor and viewport changes invalidate the appropriate axis. No new retained
ownership is introduced.

The causal pre-fix native test fails; the repair passes 336 source combinations
and 1,392 snapshots across perpendicular axes, percentage/calc, box sizing,
percentage min/max, definite zero, containing limits and scrollport fallback,
including changing a definite parent to automatic and resizing only an ancestor.
All **245 native tests pass**. Neighbor suites pass: sizing **776/776**,
positioning **139/139**, inline **169/169**. The canonical audit improves to
**1,304 / 1,980**, **676 mismatches**, **zero execution errors**, **16 gains,
zero lost passes and zero per-fixture metric decreases** versus Milestone 14.
The gains span all four horizontal/vertical orthogonal percentage families.
Evidence: `temp/wpt-percentage-native-{before,second-after}.log`,
`temp/wpt-percentage-native.log`, `temp/wpt-percentage-components.log`,
`temp/wpt-writing-path-m15-percentages.json`, `temp/wpt-percentage-delta.json`.
Required aggregate qualification is pending.


Milestone 15 is qualified: required aggregate **6,426 total, 6,282 pass,
142 existing partial results, two skips and zero failures**. Dimension-cast lint
and whitespace checks pass. Qualified binary SHA-256:
`535b8c6617103c40cbefd5c434123d634b794af51bd812026e71e2af4963e653`.
Final readable regression fixture: `temp/wpt-percentage-final-controls.log`.
Aggregate evidence: `temp/wpt-percentage-radiant-gate.log`.

### Milestone 16: principal vertical flow preserves published box geometry

Priority: P1. Most remaining orthogonal-sizing mismatches are reference documents
whose normal-flow originals already pass. Empty vertical roots reduce the cause
without their text: root finalization replaces a valid zero body block size with
normal font height, computes automatic body inline size from the viewport while
omitting root block-end padding, and only positions an automatic root block size
when both physical dimensions are automatic. The empty-child margin predicate
also examines physical height rather than the block axis.

CSS 2.2 §9.4.2 gives empty lines no block extent; §8.3.1 collapses an eligible
empty block's adjoining margins. CSS Writing Modes 3 §§7.1–7.2 maps these rules
to block/inline axes without changing physical decoration sides. Publication
must consume used sizes, including zero, and a definite inline dimension must
not suppress automatic block-axis publication. Reuse physical box conversion,
stretch-fit and existing signed margin-collapse helpers. Generalize the existing
root child self-collapse predicate by axis rather than copying it. Selected views
retain their existing owners (**D4.5.1v4 / D4.1.4v5**).

Finite acceptance: vertical-lr/rl roots with automatic block size and automatic
or definite inline size; empty bodies and absolute-only bodies with/without
collapsible whitespace; asymmetric root padding/borders; normal-flow content
restored by a retained position mutation. Verify root/body origins and dimensions,
including zero block size and collapsed margins. Keep existing horizontal root,
explicit-size, float/overflow, nested vertical and positioned controls. Broader
principal-mode/style propagation and mixed-inline intrinsic sizing remain outside
this milestone. Verification budget: causal native controls and browser
reductions, focused failed references, native and relevant neighboring suites,
canonical inventory and required Radiant gate.

Initial reduction evidence: `temp/wpt-empty-vertical-browser.json`,
`temp/wpt-empty-vertical-delta.json`, and
`temp/wpt-orthogonal-reference-inspect.log`. A 320×200 viewport with root
padding 40px/60px gives an automatic body's 84px inline size in Chromium;
native gives 144px and an erroneous 18px block size. With an explicitly
200px root content inline size, the automatic body is 184px; native gives 200px.


Milestone 16 implementation preserves the body's published physical block size,
including zero, rather than inventing a font strut. It derives automatic body
inline size from the root's already-definite content inline constraint through
the shared stretch-fit helper, so asymmetric physical padding/borders are counted
once. Root block-axis publication is independent of explicit inline sizing; its
LR/RL origins include physical root decorations. The existing self-collapsing
child predicate now uses the block axis, and the existing signed collapse helper
combines eligible empty-body margins. Root absolute-position layout remains with
its own containing-block solver.

All **56 Chromium snapshots** independently match the native regression's box
constraint equations; the native control demonstrably fails before the repair
and passes after it. All **246 native tests pass**. Neighbor suites pass: sizing
**776/776**, positioning **139/139**, inline **169/169**. The canonical audit is
**1,359 / 1,980**, **621 mismatches**, **zero execution errors**, **55 gains,
zero lost passes and zero per-fixture metric decreases** versus Milestone 15.
Evidence: `temp/wpt-root-publication-browser.{json,log}`,
`temp/wpt-root-publication-native-before.log`,
`temp/wpt-root-publication-focused.log`, `temp/wpt-root-publication-native.log`,
`temp/wpt-root-publication-components.log`, and
`temp/wpt-root-publication-delta.json`. Dimension-cast lint and whitespace checks
pass. Required aggregate and exact user-command qualification are running in
`temp/wpt-root-publication-{radiant-gate,user-suite}.log`.


Milestone 16 is qualified: required aggregate **6,427 total, 6,283 pass,
142 existing partial results, two skips and zero failures**. The exact user
command agrees with the canonical 1,359/1,980 audit. Qualified binary SHA-256:
`6fdea1fabb0adbcb84d661ff83b4f926efa5c3bbce312116947b5f88fb18ef9b`.

### Milestone 17: resolve vertical inline constraints before wrapping

Priority: P1. Parallel vertical blocks must receive their available inline size
and min/max constraints before formatting text. The body-only exclusion and late
root overwrite produced correct-looking outer heights with incorrect line breaks.
The containing basis also used a parent's raw CSS border-box size instead of its
content height. These are shared sizing-stage defects. CSS Writing Modes 3
[§§7.1–7.2](https://www.w3.org/TR/css-writing-modes-3/#abstract-layout) maps CSS 2.2
§§10.3.3/10.4 to the inline axis: normal block margin boxes fill available inline
space, subject to min/max constraints.

**Design and finite acceptance.** Use the existing stretch-fit/minmax helpers for
bodies and ordinary parallel blocks, before wrapping; remove the body-tag
exclusion and redundant late body-height overwrite. Cover vertical-lr/rl, direct
Ahem text with known wrapping, fixed/percentage min/max, both box-sizing modes,
asymmetric root/body decorations, empty/restored text and viewport changes.
Preserve prior root, orthogonal and positioned controls. Root pseudo-element
siblings, containment propagation and general mixed-inline intrinsic composition
remain separate owners. No retained ownership changes are required
(**D4.5.1v4 / D4.1.4v5**).

**Blocking prerequisites found by neighboring checks.** The corrected content-box
basis exposed callers that accidentally depended on intrinsic style resolution
leaking a descendant's given dimensions into their live BlockContext. A causal
source A/B and the exact harness font/style setup isolate this dependency; manual
CLI runs without fixture-font setup do not reproduce the same materialization
path. Public width-query misses and height-query hits/misses now preserve caller
state with the existing BlockContextScope. The reduced query control proves the
40×30 descendant leak before the repair and preserves dimensions, direction and
active view after both query kinds and retained descendant-size changes.

Restoring state then exposed shared geometry owners that had been compensated by
that leak. Repair those owners rather than restoring the leaked values:

- Vertical multicol intrinsic measurement balances physical widths along the
  block axis. Its physical height comes from the logical inline track sum, with
  shared intrinsic-gap computation. Anonymous columns publish a definite inline
  containing constraint. Unequal rectangles and two/three-column mutations
  challenge the axis mapping; reuse the existing balancing search.
- Balanced and clamped flow owns its published extent. Do not re-sum source-tree
  advances or expand it from an unfragmented intrinsic floor afterward. Flex,
  grid and table intrinsic contributions retain their separate sizing role;
  the fragmentation cap does not apply to them.
- A valid zero line-height remains zero during intrinsic measurement instead of
  falling back to normal font leading (CSS 2.2 §10.8.1). Inherited leading and
  positive/restored-zero mutations cover the former inline-flex width error.
- Top/bottom alignment uses physical margin-box edges against the full vertical
  line-over/under edges, including negative margins and relative movement
  (CSS Writing Modes 3 §7.5).
- [Quirks Mode §3.5](https://quirks.spec.whatwg.org/#the-percentage-height-calculation-quirk)
  selects percentage-height ancestors through automatic block containers.
  Percentage definiteness and the late used-height guard now share that lookup;
  the selected ancestor supplies the actual content basis. This removes five
  accepted canvas cases' dependence on a leaked given height. The reduced
  control mutates the ancestor height through zero/one/two automatic wrappers.
- Empty columns retain their count/gap structure with zero-width auto tracks;
  that inline contribution must not become physical block width. Ordinary
  fragmented inline unions retain all measured fragments, rather than being
  clipped against their ancestor's provisional block extent. Positioned-inline
  containing-block edge handling remains a separate existing path.

**Verification budget and evidence.** Use causal native controls and original WPT
cases in the edit loop, then full native tests, the accepted inline/sizing/
multicol/positioning neighbors, canonical writing-modes audit, exact user command,
layout-dimension lint and the required Radiant aggregate gate. Reference geometry,
fixtures, baseline expectations and tolerances remain unchanged.

The nine new native cases cover the early wrapping constraints and their
prerequisites; all **255 native cases pass**. Chromium independently matches the
420 early-wrap snapshots, six unequal-rectangle column states, six leading-width
states, 25 quirks/empty-column/fragment-union states and six spanner text-change
states. Browser evidence:
`temp/wpt-body-inline-browser.json`, `temp/wpt-vertical-column-axes-browser.json`,
`temp/wpt-vertical-zero-leading-browser.json`,
`temp/wpt-m17-prerequisite-browser.json`, and
`temp/wpt-m17-spanner-inline-browser.json`.

The exact user command and canonical audit report **1,360 / 1,980 matches**, 620
mismatches, zero execution errors, one gained match and no lost matches or score
decreases against Milestone 16. The gain is
`css-writing-modes/sizing-orthog-vlr-in-htb-012-ref.xht`. Accepted neighbors pass:
169 inline, 776 sizing, 139 positioning, and all 362 multicol expectations
(333 full, 29 existing partial). All four initially rejected baseline-source
inline cases recover. Lint and `git diff --check` pass. Evidence:
`temp/wpt-m17-spanner-native.log`, `temp/wpt-m17-spanner-neighbors.log`,
`temp/wpt-writing-path-m17-spanner-final.json`, `temp/wpt-m17-final-delta.json`,
`temp/wpt-m17-spanner-user-suite.log`, and `temp/wpt-m17-spanner-lint.log`.

Milestone 17 is qualified: required aggregate **6,436 total, 6,292 full passes,
142 existing partial results, two skips and zero failures**; see
`temp/wpt-m17-spanner-radiant-gate.log`. Qualified binary SHA-256:
`75e393a3cdb5d43e3c14dd52770cd85ab8f1470d5498409f41ae241c4532aaca`.

**Remaining limits.** The independent positive-leading control finds a separate
vertical inline-flex cross-size error: 50px leading produces an 80px physical
height instead of Chromium's 60px; the width is correct. The width regression
asserts the intrinsic-width behavior repaired here; the broader failing probe is
retained in `temp/wpt-m17-zero-leading-focused.log`. Do not change its expected
height or compensate with a width rule. Its flex cross-size owner, root
pseudo-sibling publication/containment, general mixed-inline intrinsic composition
and advanced column fragmentation remain explicit follow-up work.

**Final qualification audit.** The first required aggregate passes 6,435 total
(6,290 full, 143 accepted partial, two skipped, zero failures), but comparison to
the previous source identifies one extra partial: `hit-test-in-vertical-rl` drops
from full geometry to 85.7% because its `column-span:all` child receives the new
anonymous column's 93px constraint instead of the container's 200px inline size.
A full source A/B reproduces exactly that single multicol score change. This is
a blocking prerequisite, despite meeting the older accepted partial threshold.
Extend the existing per-child multicol preparation hook to select the full
computed track span for spanners before style/layout, while ordinary children
receive one column. Challenge it with direct spanner text and retained text
wrapping, so a late outer-size correction cannot conceal an incorrect measure.

The direct-spanner hook now selects the full computed inline track span before
style/layout, and the retained text control passes in both vertical modes.
Chromium independently confirms all six text-change states. All **362 multicol
per-fixture scores exactly match the preceding qualified implementation**,
including the recovered hit-test case; see
`temp/wpt-m17-multicol-final-delta.json` (empty delta). This source A/B changes no
fixtures, baseline expectations or tolerances. The final native/canonical/neighbor
checks above and the final aggregate are fresh after the repair. This finite
milestone is complete; the conformance goal remains active with 620 mismatches.

### Milestone 18: principal-flow propagation eligibility

**Priority and finite outcome.** P1. Root/body writing mode affects every vertical
page and must remain correct when pages use containment or hide the body. CSS
Writing Modes 4 [§8](https://drafts.csswg.org/css-writing-modes-4/#principal-flow)
restricts the HTML body special case to a displayed body, while CSS Containment
1 [§2](https://www.w3.org/TR/css-contain-1/#contain-property) disables body
propagation when either html or body has non-none used containment. Computed
root values continue to govern inheritance, length computation and logical
property mapping. The selected original failure is
`wm-propagation-body-contain-root.html`.

Acceptance covers six root/body mode pairings, both containment owners and seven
containment/visibility states: none, paint, a substituted paint variable, unset,
restored none, hidden body and restored body. Verify used mode, unchanged computed
root mode, physical root/body dimensions and empty-root origin in all 84 states.
Preserve ordinary principal flow, decorated/zero roots, body wrapping and RTL
controls. Root pseudo-element siblings and other principal-flow consumers remain
separate owners; this milestone does not rewrite general root sizing.

**Evidence and design.** The original WPT's body/text sizes already match, but
root paint containment fails to prevent propagation and moves the whole page to
the right. The existing `layout_block_writing_mode` always selects the body, and
root finalization separately falls back to a vertical body even if the selected
root mode is horizontal. Repair the decision, then its consumers:

- The used-mode helper checks body visibility and containment on both elements.
  A typed CSSOM value consumer shares existing cascade, substitution and reset
  semantics before an unresolved body's layout style exists. Current resolved
  containment flags provide the fast path. Its temporary pool owns the read;
  no expression or view payload escapes (**D4.5.1v4 / D4.1.4v5**).
- Remove the later body-mode fallback. Root physical origins and block margins
  follow the selected root used mode, even when its body is orthogonal.
- Block orthogonality uses the parent's used layout axis. Comparing against the
  root's computed axis incorrectly shrank a parallel horizontal body from the
  viewport width to its 40px child. The child's inherited computed mode remains
  available before its own layout-style materialization.
- Publish the final vertical root origin independently of body-specific margin
  handling. An empty vertical-rl root still starts at the viewport's right edge.

The initial native matrix reproduces the defect before repair. Fresh Chromium
documents independently confirm every equation in the expanded **84-state**
matrix. A separate live Chromium mutation probe yields 20 root dimensions that
differ from fresh documents with identical final styles, even after two rendering
frames. Preserve the CSS constraints and the fresh-document equations in retained
native layout rather than fitting to that browser invalidation discrepancy.
Evidence: `temp/wpt-m18-contain-root-before.log`,
`temp/wpt-m18-principal-before.log`,
`temp/wpt-m18-principal-{static-browser,browser}.json`.

**Verification budget and results.** Causal native/browser controls and original
WPT in the edit loop; full native tests and accepted inline/sizing/multicol/
positioning neighbors once stable; canonical writing-modes audit, exact command,
layout-dimension lint and required Radiant gate at the boundary. No fixture,
reference, comparator, tolerance or accepted-baseline changes.

All **256 native tests pass**. The exact user command and canonical audit report
**1,365 / 1,980 matches**, 615 mismatches, zero execution errors, five gained
matches and no lost matches or score decreases against Milestone 17. Gains:
`wm-propagation-body-{040,044,048,052}.html` and
`wm-propagation-body-contain-root.html`. All per-fixture scores of 169 inline,
776 sizing, 139 positioning and 362 multicol neighbors exactly match Milestone 17
(333 full multicol results, 29 existing partial). Dimension-cast lint and
`git diff --check` pass. Evidence: `temp/wpt-m18-native.log`,
`temp/wpt-writing-path-m18-principal.json`, `temp/wpt-m18-delta.json`,
`temp/wpt-m18-neighbor-delta.json`, `temp/wpt-m18-neighbors.log`,
`temp/wpt-m18-user-suite.log`, and `temp/wpt-m18-lint.log`.

Milestone 18 is qualified: required aggregate **6,437 total, 6,293 passes,
142 existing partial results, two skips and zero failures**; see
`temp/wpt-m18-radiant-gate.log`. Qualified binary SHA-256:
`f765f8ba35ebf16b05323c5127694f8ed982f215274b951d952b64e7b844e050`.

**Remaining consumers.** Root generated siblings still lose normal-flow space
when late root publication reconstructs its extent from the body alone. Principal
direction/scroll origin, unusual multiple-body DOM structures, general mixed-inline
intrinsics and the positive-leading flex cross-size limit remain separate work.
The conformance goal remains active.

### Milestone 19 plan: publish the complete root normal flow

Priority: P1. A root generated block is part of its normal flow, so root sizing
and body placement must include it. `wm-propagation-body-033.html` and `-037.html`
both require a 472px root and a body beginning at x=124px. The current 356px root
is reconstructed from the body alone, and the body is moved back to x=8px. The
undeclared-root variant also retains y=116px instead of y=8px, while the explicit
root `horizontal-tb` variant gets y=8px. Evidence:
`temp/wpt-m19-root-sibling-before.log` and the corresponding saved inspector trees.

Owner/hypothesis: ordinary vertical publication requires BlockProp metadata and
returns immediately without it. A root with no ordinary CSS declarations can
acquire its vertical used mode from the body without allocating that layout
state. The later root-specific body reconstruction then discards normal-flow
extent from other root children. Trace finalized flow extents and publication
before editing. Establish required transient layout metadata without changing
computed style, and reuse the ordinary flow/margin publication owners instead
of adding a root-pseudo offset or another body-only extent formula. CSS Writing
Modes 3 §§7.1–7.2, CSS 2.2 §§9.4.1/8.3.1 and generated-content box ordering govern
this behavior; ownership remains **D4.5.1v4 / D4.1.4v5**.

Finite acceptance: the two original failures; vertical-lr/rl body modes; roots
with no ordinary declarations versus explicit horizontal-tb; fixed before/after
blocks and asymmetric margins; retained generated-block visibility and body-size
changes. Preserve the principal eligibility/computed-mode matrix, decorated and
zero roots, positioned roots and prior body wrapping cases. Root inline text
composition, principal direction/scroll propagation and advanced mixed-inline
intrinsics remain separate consumers unless they block these cases.

Verification budget: causal native and independent browser controls, focused
original WPT cases during implementation, then full native/relevant neighbors,
canonical audit, exact user command and the required Radiant gate. Completion
requires the selected normal-flow behavior and those gates; the broader 615-case
conformance backlog is not redefined around this milestone.

Milestone 19 trace confirms both hypotheses. For each original case ordinary
finalization computes the correct **472px** root, which is subsequently replaced
by the 356px body-only reconstruction. In the undeclared-root case BlockProp is
null and vertical publication is skipped, leaving body y=116px; the declared-root
case publishes body x=124px/y=8px correctly before the later overwrite. Evidence:
`temp/wpt-m19-root{033,037}-trace.log`.

The reduced 36-state generated-box control fails before repair, and independent
Chromium box-model queries confirm all root/body and pseudo-element equations,
including visibility/restoration and body-width mutations. Reuse the existing
single-in-flow-child scan by promoting it from inline layout to the module
header; the body-only collapsed-margin shortcut is valid only when the body is
that sole child. Other roots retain the already-correct ordinary flow extent and
placement. Ensure/reset vertical publication metadata before root flow, without
changing computed writing-mode. No fixture-specific positions are introduced.

The retained native control must emit the documented DOM attribute mutation
notification after the low-level class setter, matching the script adapter.
With that correction the cascade and root flow respond correctly; the remaining
retained failure is stale dimensions on hidden root children. The root loop
skips those children instead of using the ordinary dispatcher, which already
clears their placed geometry and handles page strings. Route root `none` and
`contents` children through that shared owner. The 36-state browser control
includes both-pseudo suppression, restoration and body-size mutations; hidden
boxes contribute no extent under [CSS Display 3 §2.5](https://www.w3.org/TR/css-display-3/#box-generation).

**Milestone 19 qualification.** The completed
36-state native root/generated-block control passes, as do all 257 native tests.
Canonical identities and the exact requested command both report **1,369 /
1,980** full matches, 611 mismatches and zero execution errors. The four gains
are `wm-propagation-body-{033,037,049,054}.html`; no prior pass or metric decreases.
The dynamic two-body fixture also improves text coverage but remains a failure.
All 169 inline, 776 sizing, 362 multicol and 139 positioning expectations retain
identical per-fixture metrics to Milestone 18. Lint and diff checks pass.
Evidence: `temp/wpt-m19-{native,canonical,neighbors,user-suite,lint}.log`,
`temp/wpt-writing-path-m19-root-flow.json`, `temp/wpt-m19-delta.json` and
`temp/wpt-m19-neighbor-delta.json`. The required Radiant aggregate passes 6,438 total: 6,294 full passes, 142
existing partial results, two skipped and zero failures
(`temp/wpt-m19-radiant-gate.log`). Qualified binary SHA-256
`c554f14a3fb5afa79d75fbb8499d704986c465b0144c92a9919ea2f7fee0949c`.

Milestone 19 is complete by behavior: generated root siblings retain ordinary
normal-flow extent and placement, undeclared propagated roots own transient
publication state, and hidden retained children use shared cleanup. Broader
principal direction/scroll propagation, mixed-inline composition and the
positive-leading flex cross-size limit remain separate work. The conformance
goal remains active.

### Milestone 20 plan: directional line setup and alignment

Priority: P1. Correct RTL text alignment is a common-page requirement and is
shared by tables and atomic inline containers. The three retained failures
`CSS2/bidi-text/direction-applies-to-{007,012,015}.xht` have matching element
geometry and 75% text matches. Their Ahem `X` is 50px wide inside a 100px box: the
cell/caption text is at x=8 instead of 58, while the inline-block text is at 108
instead of 58 (`temp/wpt-m20-direction-before.log`). Diagnose these distinct
consumer paths before assuming a single cause.

[CSS Text 3 §6](https://www.w3.org/TR/css-text-3/#text-align-property) retains
logical `start` through inheritance; used alignment resolves against the
paragraph direction. Existing `layout_setup_inline_direction` owns paragraph
state, including plaintext and upright behavior. Table cell/caption adapters
currently set `block.direction` after constructing line state without using that
owner; confirm the first disagreement in `line.inline_base_direction`. Trace the
inline-block's text position before/after its own final line and parent atomic
placement to distinguish repeated alignment from coordinate publication.
Selected style/view ownership remains **D4.5.1v4 / D4.1.4v5**.

Finite acceptance: the three originals; block, inline-block, cell and caption
controls with inherited/default `start` versus explicit physical alignment; RTL
and LTR; retained direction/alignment changes; preserve prior atomic bidi and
plaintext controls. Instrument first, then repair the established shared owner.
Verification budget: causal native controls and browser equations, focused WPT
cases, full native/canonical/relevant neighbors, exact user command and Radiant
gate after stable implementation. No production change for this investigation
is included in the qualified Milestone 19 binary.

### Milestone 20 confirmed causes and implementation plan

The original-fixture traces confirm two separate owning-stage defects
(`temp/wpt-m20-trace-{007,012,015}.log`). Cell and caption paragraphs have RTL
`block.direction` but LTR `line.inline_base_direction` at final alignment. Reuse
`layout_setup_inline_direction` at these adapters' paragraph entry; in captions
it must precede `line_reset`, which consumes direction for first-line setup.
The inline-block's first Latin text rectangle already starts at 50px before
final alignment, which then adds the required 50px again. `output_text` performs
a provisional right-edge placement specifically for horizontal RTL inline-block
contents. Ordinary bidi reordering and final text alignment now own that placement;
remove the inline-block exception while preserving the separate initial-letter
placement consumer. This fixes the responsible stages without changing measured
advances or applying an inverse offset.

The causal native control covers block/inline-block/cell/caption containers with
13 retained direction/alignment states each. Default inherited start, explicit
left/center/right/start/end and CSS-wide initial/unset distinguish logical
alignment from physical alignment and challenge provisional placement. The
existing bidi, plaintext and initial-letter gates remain required controls.

**Milestone 20 qualification.** The reduced
52-state native control fails on the old engine; independent Chromium confirms
all 52 source-based coordinate equations (`temp/wpt-m20-direction-browser.json`).
After repair the three original WPT fixtures match fully, as do the reduced
control and existing atomic bidi, vertical and plaintext controls. All 258
native tests pass. Canonical identities and the exact requested command report
**1,372 / 1,980** full matches, 608 mismatches and zero execution errors. The only
changed fixtures are the three gains; no previous pass or metric decreases.
All 169 inline, 776 sizing, 362 multicol and 139 positioning expectations retain
identical per-fixture metrics to Milestone 19. Lint and diff checks pass.
Evidence: `temp/wpt-m20-{native,canonical,neighbors,user-suite,lint}.log`,
`temp/wpt-writing-path-m20-direction.json`, `temp/wpt-m20-delta.json` and
`temp/wpt-m20-neighbor-delta.json`. The required Radiant aggregate passes 6,439 total: 6,295 full passes, 142
existing partial results, two skipped and zero failures
(`temp/wpt-m20-radiant-gate.log`). Qualified binary SHA-256
`17a0d329e44d465537661df559c0443c5b262c120b87760b712767f1503d4e6d`.

Milestone 20 is complete by behavior: first table-cell/caption lines use the
paragraph direction, and ordinary RTL inline-block text receives final alignment
without an earlier extra placement. The native control covers all 52 retained
states and has independent browser evidence. No expected result, tolerance,
fixture or comparator changed. The conformance goal remains active.

### Milestone 21 investigation: reordered text fragments reach every consumer

Priority: P1. Mixed-direction inline markup needs consistent layout/query and
paint fragments. Inspecting `bidi-005a.xht` shows a structural disagreement before
most reported geometry disagreements: Chromium reports multiple source-ordered
range fragments for a text node containing directional controls, whereas the
engine JSON emits its single visual union. This shifts child identities in the
comparison. For example the engine's first decorated span already has the correct
x=129.2px/width=110.8px; the comparison instead pairs the following span with
that browser span after text-fragment counts diverge. Do not compensate span
positions based on those shifted paths. Evidence:
`temp/wpt-m21-bidi-before.log` and saved `bidi-005a` inspector trees.

Owner/hypothesis: bidi placement already computes per-character source positions
and paint runs, but JSON/range consumers receive union rectangles. In addition,
`bidi_publish_paint_runs` publishes runs only when a visible character has an odd
embedding level. A source range containing only even-level visible glyphs can
still be discontiguous after formatting controls cross an inline boundary. The
trailing `PDF + m` range in the original fixture has an x=78.2px visual union,
while the actual final glyph belongs near x=299.9px. Confirm its paint projection
with a reduced causal control before editing; the current evidence establishes
query fragmentation mismatch, not a completed paint diagnosis.

Normative owners: [CSSOM View §9](https://www.w3.org/TR/cssom-view/#extensions-to-the-range-interface)
for source-range rectangles and font advances, CSS Writing Modes bidi inline
model and [UAX #9 X9/§5.2](https://www.unicode.org/reports/tr9/#Retaining_Explicit_Formatting_Characters)
for directional controls. Reuse existing source-indexed bidi data and fragment
iterators; preserve **D4.5.1v4 / D4.1.4v5** selected-view ownership and retained
cloning/recycling. Do not split authored DOM or add fixture-specific serialization rules. Preserve
browser geometry and comparison tolerances. The qualification follow-up below
records the separately demonstrated legacy normalization defect.

Finite acceptance: reduced Ahem sequences with RLO/LRO/PDF crossing span
boundaries, including an even-level trailing glyph; both base directions;
decorated spans and retained source changes; original `bidi-003` and `bidi-005a/b`
where the causal behavior applies. Establish source fragment identity before
assessing geometry. Native query/paint controls and independent browser ranges
lead implementation; full native, canonical/neighbor audits, exact user command
and required Radiant aggregate qualify the milestone. Persian fallback font
metrics in `bidi-lines-001` are a distinct measured failure and remain separate
from this fragment representation repair.

Milestone 21 reduced paint evidence confirms the even-level final glyph is
painted at x=20px instead of x=240px; the other 12 glyphs are correct
(`temp/wpt-m21-paint-before.log`). Per-character Chromium ranges show each
explicit format code at the following logical glyph's inline-start caret,
including across element boundaries. FriBidi's retained BN visual slot does
not have that source-caret meaning. Correct this first at character-position
publication before computing source unions. The earlier paint-run guard
hypothesis is not sufficient: the incorrect union alone explains this tail
paint failure, so do not change run allocation without separate causal proof.
The six-state reduced control now includes both paragraph directions and
retained final-glyph changes; Chromium verifies the same alphabetical visual
order with the RTL alignment origin shifted by the available free space.

The control-anchor repair passes all 259 native tests, including six retained
paint states, and keeps all 1,372 prior corpus matches with no metric decreases.
`bidi-011` improves but remains a failure. Next, publish distinct source
`TextRect` fragments when a reordered source rectangle covers disconnected
inline intervals. Use resolved character geometry to form connected physical
regions, then partition source bytes whenever their region changes. This is a
layout representation repair: ordinary JSON, range traversal, painting and
retained cloning/recycling already consume `TextRect` lists, so no capture-only
normalization is needed. Keep per-level paint runs inside each connected region.
Anchor formatting codes before constructing regions; carry trailing layout
metadata and transient line aliases to the final source fragment. Allocation
failure must preserve the existing source representation. Partial range queries
inside a connected region still need to consume its resolved directional runs.

The fragment prototype gains eight corpus matches with no losses or metric
decreases; accounting for newly opened inline edges also repairs the zero-width
control caret in decorated `bidi-005a/006a`. The source fragment regression passes
exact byte ownership and geometry in all six states. A new real DOM Range probe
then confirms partial selections inside the connected `h/LRO/g/PDF/f` fragment
remain wrong, including a negative width (`temp/wpt-m21-partial-range-before.log`).
The range adapter measures the visual union as a logical forward run, and its
shared glyph-advance helper treats absent directional control glyphs as document
font `.notdef` advances. Preserve zero-advance semantics at that shared helper;
publish resolved runs with format-code source prefixes attached to their next
logical glyph; project partial range rectangles through those runs, reversing
odd-level endpoint advances. This is a shared query consumer repair, not a JSON
adapter or capture exception.

**Milestone 21 engine evidence.** All 259 native
text/layout tests and 79 DOM Range tests pass, including six retained source
fragment/paint states, partial selections and caret projections. Canonical
identities and the exact requested command report **1,386 / 1,980** full matches,
594 mismatches, zero execution errors, fourteen gains and no lost match or
metric decrease against Milestone 20. Gains: `empty-span-001`, both a/b variants
of `bidi-005` through `bidi-010`, and `line-breaking-bidi-003`. Evidence:
`temp/wpt-m21-{native,dom-range,canonical,user-suite,lint}.log`,
`temp/wpt-writing-path-m21-final.json`, `temp/wpt-m21-delta.json`.

The first neighbor run exposes four legacy sizing captures: clone/slice
`nowrap-intrinsic-size-bidi` and their refs. They contain a single source-query
rectangle where the current capture tool publishes multiple disconnected pieces.
Fresh captures preserve all 68 visible element boxes and source unions within
0.01px rounding. The initial experiment refreshed four raw and four compact
captures; **all eight originals are restored** with guards against intervening
edits. No reference or accepted baseline changes remain.

The first required aggregate then exposes the same source-query mismatch in
three CSS Text cases: `text-autospace-mixed-001`, `letter-spacing-bidi-001` and
`empty-span-001`. A source A/B of all 980 accepted CSS Text cases isolates exactly
these three changes. Fresh captures are unsuitable as a compatibility repair:
Hebrew fallback font metrics differ from the original environment. Their staged
captures and alternate font-policy experiment were not published.

**Normalization follow-up design.** The existing
`mergeConsecutiveTextNodes` already aligns multiple engine pieces to a legacy
browser source node by their logical content, but retains only the first piece's
geometry. A source range covering disconnected visual intervals consequently
loses most of its bounds. Repair that existing adapter: when the reference has
one rectangle and all consumed pieces share a physical line band, retain their
bounding union. Keep the existing first-rectangle behavior for wrapped or
multi-rectangle legacy captures, and preserve direct fragment comparisons for
modern captures. This changes neither engine geometry nor browser expectations,
comparison tolerances, accepted scores, filtering, or failure reporting. It
replaces the initial no-comparator-change plan because source A/B and reduced
controls demonstrate an independent defect in that adapter.

`node test/layout/text_fragment_normalization.test.js` proves the old adapter
fails a disconnected source-range union and the correction passes. A deliberately
misplaced later piece must still fail even when the first piece alone matches;
overlap uses a union rather than summed widths; source objects stay unchanged;
top-level/nested/direct reference boxes, modern fragments and legacy multiline
captures preserve their conventions. The correction was tested from staged code
before publication in the linked corpus. Evidence:
`temp/wpt-m21-normalization-{before,after}.log` and
`temp/wpt-m21-normalized-{inventory.log,neighbor-delta.json}`.

All 980 CSS Text cases retain their prior per-fixture metrics (890 full and 90
partial), as do the 169 inline, 776 sizing and 139 positioning cases. Multicol
has no decreases: the union corrects a previously truncated `b1 b2` source query
in `multicol-margin-003`. That fixture still reports a pre-existing comparison
error in another child; the harness's metric-only classification calls it full,
but this is **not an engine conformance gain or a resolved fixture**. Its
unrelated comparison error remains visible and belongs in the harness backlog.
No published reference capture, font-capture policy, accepted baseline score or
comparison tolerance changed.

Engine binary before the restored CLI-object rebuild:
`e56914ec285da4d31a8b56fcef264299c6052d3c81a4a36916f86fb317e7492f`.
The qualification below records the final rebuilt host identity.

ASan CLI qualification passes **321 bidi documents plus four sizing controls**
through streamed batches, including dynamic documents and pool teardown. Every
writing-mode comparison metric matches the normal candidate; all frames arrive;
all batch exit codes are zero; no sanitizer diagnostic or execution error. The
host is instrumented with AddressSanitizer (`lambda-debug-asan.exe`);
`detect_leaks=0` leaves leak checking outside this run. Native regression
executables use the normal profile. Evidence: `temp/wpt-m21-asan-{build,clang,bidi}.log`
and `temp/wpt-m21-asan-bidi.json`.

The first Milestone 21 aggregate is **not qualified**: CSS Text reports one
accepted-score regression (`text-autospace-mixed-001`, text 80% → 60%) and one
additional partial result; all other components pass. Aggregate: 6,440 total,
6,294 full, 143 partial, two skipped, one failed. Original evidence is preserved
in `temp/wpt-m21-radiant-gate.log`,
`temp/wpt-m21-first-gate-baseline-results.json` and
`temp/wpt-m21-wpt-css-text-classification.json`. The source A/B and normalization correction above resolve these compatibility
regressions while preserving the remaining mixed-script autospace metric failure.
The required aggregate must be rerun before qualifying the milestone.


**Milestone 21 qualified.** The exact requested command and normalized canonical
path audit both retain **1,386 / 1,980** full matches, 594 mismatches and zero
execution errors. Comparing all canonical records before/after the adapter repair
shows **zero metric changes**: the fourteen gains belong to the engine changes.
All 259 native text/layout cases and 79 DOM Range cases pass. The repeated
required Radiant aggregate exits zero with **6,440 total, 6,297 full, 141 partial,
two skipped and zero failed**. Its one multicol classification improvement is
the legacy normalization effect already limited above, not an additional engine
fix. The full AddressSanitizer bidi/document evidence remains valid for this
unchanged engine source. Dimension-cast lint, Node syntax checks, normalization
regressions and whitespace checks pass.

Qualification evidence: `temp/wpt-m21-normalized-{canonical,user-suite,radiant-gate}.log`,
`temp/wpt-writing-path-m21-normalized.json` and
`temp/wpt-m21-normalization-writing-delta.json`. Final host SHA-256:
`8394989b5f14ad1e73d56d962f7d25dd1b6d4437ce9ff214995e79fcf63528bb`.
The conformance goal stays active; remaining source-range fragmentation edge
cases, mixed-script fallback/autospace measurement, mixed-inline intrinsic sizing
and vertical inline baseline alignment are not claimed complete.

### Next milestone diagnosis: vertical inline alignment keeps one line-metric owner

Priority: P1. Vertical paragraphs with differently sized inline text and baseline
shifts are common page content. The original `vertical-alignment-002/004` and
`vertical-alignment-vlr-011` isolate incorrect physical cross-axis placement:
the containing box and inline-axis positions are already correct. In the 60px
Ahem/180px line, the unshifted glyph is at x=20px instead of x=68px after
final line alignment, beside a smaller top/bottom-aligned inline. A super-aligned
vertical-lr child moves toward the physical left although line-over is right.
Evidence: `temp/wpt-vertical-align-before.log` and saved inspector trees.

CSS Writing Modes 3 **§4.2/§4.4** selects central dominant baselines for mixed or
upright vertical typography and aligns corresponding child/parent baselines;
**§6.3/§7.5** separates line-over from block-start in vertical-lr. CSS 2.2
**§10.8.1** assigns half-leading to each inline box and applies top/bottom to its
aligned subtree. Source owners to trace before editing: `line_baseline_position`,
`layout_inline_font_metrics`, `view_vertical_align`, `layout_inline_font_box_y`,
`span_vertical_align` and the final vertical geometry adapter. The current adapter
adds block-wide leading after alignment, while its vertical text branch uses the
bottom of the font box as the item baseline. Child inline line-heights and the
line's physical over edge must remain authoritative through final publication;
do not add offsets based on the observed 48px or fixture names.

Proposed finite acceptance: reduced Ahem paragraph with an unchanged direct glyph
and one locally sized ordinary inline, mixed/upright vertical-rl and vertical-lr,
baseline/top/bottom/text-top/text-bottom and positive/negative baseline shifts;
horizontal controls; inherited numeric versus fixed line-height; a forced second
line and a retained font/line-height mutation. First obtain independent browser
ranges and native causal controls. Central-baseline choice, per-inline leading
and line-over mapping may require coordinated ownership repair; no prototype is
retained yet. Preserve **D4.5.1v4 / D4.1.4v5** and existing nonvertical behavior.
Use focused controls and original alignments in the edit loop; qualify with native,
relevant inline/text/sizing suites, canonical writing audit, exact user command
and the required Radiant aggregate at the finite boundary.


Reduced same-font evidence now covers 33 cases: three writing modes times plain
text and ten inline alignments, with an embedded identical Ahem face and fixed
logical container extents (`temp/wpt-m22-alignment-{before.json,before.log}`,
`temp/wpt-m22-alignment-probe.js`). Horizontal baseline/middle/sub/super/length
controls match Chromium. Horizontal top/bottom/text-top/text-bottom incorrectly
align the glyph area instead of the inline box: the 30px child in a 90px inline
box loses its 30px half-leading. This shared failure precedes vertical mapping
and must be repaired at that owner, rather than compensated in the vertical
adapter. Vertical plain text is already displaced without a child: its font box
is at 12px/108px in rl/lr instead of 60px in both. The lr baseline-to-super shift
is -21px where Chromium gives +21px, independently confirming the line-over
mapping error. These are distinct measurements, not a single offset correction.

Refine the implementation sequence before editing:

1. Reuse/extract the content-box placement shape from `layout_inline_font_box_y`
   so text and its non-replaced inline wrapper align the same inline line-height
   and apply that box's half-leading once. Preserve per-inline inherited numeric
   versus fixed line-height, explicit zero and the selected rendering baseline;
   unrelated fallback glyph metrics remain a separate owner.
2. Establish the paragraph's dominant baseline and each child's corresponding
   baseline in logical line coordinates. Share the font-content/strut metric
   adapter; do not replace vertical text's baseline with its font-box bottom.
3. Publish vertical geometry from finalized line metrics. Remove duplicate
   leading insertion there, and distinguish line-over from block stacking in lr.
   A forced second line with different leading must challenge any proposal that
   derives a line origin from a uniform line-height or centers the whole block.

No prototype engine changes or unqualified native test remains after this
investigation. Milestone 21 remains the qualified working source. The next turn
must first select the finite acceptance boundary for these coordinated owners
and write its causal native controls, then implement and qualify that boundary.


### Milestone 22 implementation in progress: authoritative inline line metrics

The acceptance boundary is the coordinated text/inline font-box calculation,
dominant baseline and per-line over-edge mapping described above, including
fixed/numeric/normal leading, wrapped inline context and unequal forced-line
heights. Existing 259 native controls must remain valid, then native/browser
controls, the canonical audit, accepted neighboring suites and required aggregate
qualify the complete boundary. This milestone is not qualified yet.

The text branch aligned the glyph rectangle directly, whereas
`layout_inline_font_box_y` already aligned an inline line-height box and then
positioned its content inside that box. Extracted that common placement shape;
text and BR now retain their own half-leading. The paragraph's primary and
fallback font metrics use the same dominant-baseline adapter. Central synthesis
uses the font content-area midpoint, per CSS Writing Modes 3 §4.2. Font size and
parent font references retain their resolved owners (**D4.5.1v4 / D4.1.4v5**).

The line-relative offset calculation now maps line-over to block-start for
vertical-lr while the finalized current line's extent is available. This avoids
introducing a uniform-height or whole-block centering rule. The final vertical
adapter consumes already aligned font boxes; duplicate leading and glyph
centering there are removed. No per-line allocation or new retained pointer is
needed.

All initial 33 independent Ahem cases and 260 native tests pass. The first full
trial gains 40 but loses 44 matches: normal glyph contributions still use raw
alphabetic ascents, inflating central-baseline line heights. Normalizing that
shared contributor restores every old full match: **1,428 / 1,980**, 42 gains,
zero lost full matches, zero errors. Sixteen already failing cases still have
metric decreases and need classification before qualification. Evidence:
`temp/wpt-m22-{baseline,fallback}-delta.json` and corresponding canonical logs.

Twelve extended same-font browser/native controls then expose lost inline style
context at forced breaks. The final alignment walk starts at a text leaf and
skips enclosing inline ancestors, so a continuation can receive baseline
alignment instead of its parent's top alignment. Start at the existing
`layout_inline_fragment_root`, use the block's parent-font context, and visit
only the current line's root siblings. Existing line membership guards protect
other fragments. This makes the prior shifted-span special realignment and
separate forced-break rebasing redundant; remove those duplicate paths. BR
content boxes use the same leading and dominant-baseline calculation as text.

All twelve extended cases now match Chromium in host boxes, inline boxes and
every source glyph: normal leading; numeric versus fixed leading; two forced
lines; a second line with different height; both vertical block directions.
Evidence: `temp/wpt-m22-fragment-context-extended.{json,log}`. Retained mutation
coverage is being added in `VerticalLineBoxesKeepInlineContextAcrossForcedBreaks`;
remaining corpus metric decreases, relevant neighbors and aggregate are pending.


The retained forced-line control now passes twelve mutation states across both
vertical block directions, covering unitless/fixed/normal leading, a second line
with a different height and restoration of the original value. All **261 native
text/layout tests pass** (`temp/wpt-m22-focused-native.log`,
`temp/wpt-m22-owner-native.log`). These controls exposed the missing continuation
font/alignment context, not a need to store per-line history or infer positions
from a uniform line-height.

The accepted neighbor run is initially not qualified: CSS Text, sizing and
positioning retain their scores, while six inline cases and one multicol case
regress. The generic offset solver's existing contract is line-relative; atomic
boxes already convert their physical placement in their own publisher. Moving
vertical-lr's over-to-flow conversion into the shared font-content placement
owner restores that contract and the first/last textarea baseline controls.
Text-box trimming also needs the same synthesized baseline as its saved line
metrics, so the dominant-baseline adapter now accepts the owning block and is
reused by that consumer. The final geometry adapter's obsolete central-baseline
argument is removed from its declaration and all callers.

A multiline inline's membership now controls preservation of its earlier
font-box fragments for ordinary inlines as well as inline list items. This
repairs `multicol-oof-inline-cb-001`; its text/inline-block continuation had been
collapsed to its last font box during the newly correct ancestor traversal.
The original accepted partial scores are preserved or improved, with no reference
or comparator changes in this milestone.

Focused neighbor recheck: five of seven regressions are resolved, including
trim-start, first/last textarea sources and multicol. Two remain:
`baseline-source-first-002` (the inline-table target) and
`baseline-source-last-003` (the clamped legacy inline-box target), each with a
6px displacement of a neighboring 20px atom. Their other boxes and glyphs pass.
Evidence: `temp/wpt-m22-neighbor-regressions.{json,log}`. Trace the exported
baseline and zero-line-height strut ownership; do not compensate the observed
6px or change tolerances. Sixteen partial-case metric decreases in the earlier
canonical trial also need reevaluation after the axis-owner correction, including
atomic inline baselines and sideways/combined text. The full required Radiant
gate has not been repeated and Milestone 22 remains **unqualified**.


The post-owner canonical audit remains **1,428 / 1,980**, 42 new full matches,
zero lost full matches and zero execution errors
(`temp/wpt-writing-path-m22-owner.json`, `temp/wpt-m22-owner-delta.json`).
Dimension-cast lint and whitespace checks pass. This is still a candidate because
the two accepted baseline-source regressions and partial-case metric decreases
are unresolved; the top-level qualified status remains Milestone 21.

Temporary source traces in `temp/wpt-m22-{first-table,last-clamp}-trace.log`
identify a further baseline-owner mismatch: empty inline atoms still synthesize
an alphabetic bottom-edge baseline while text lines now use their central
baseline. The 20px neighboring atom reports item baseline 20, and the nested
empty-atom lines cache baselines at their lower edges (e.g. first 45 / last 106
for the clamped box). CSS Writing Modes 3 §4.3 instead synthesizes the central
baseline at the midpoint between the margin edges when that baseline is needed.
The atomic baseline decisions and line contributions are duplicated between
initial placement, final alignment and the later contribution branch; they need
one consistent synthesis owner. Trace alone is not a completed fix: first add a
reduced control proving the cached baseline agrees with the actual margin-box
center, including zero line-height and first/last export. Reuse the module's
baseline and axis helpers; do not reintroduce whole-block glyph centering or
subtract the observed 6px.

All temporary baseline trace statements are removed after capture. Current
production source retains the M22 candidate; no new synthesis prototype is
retained yet. The next step is to unify atomic synthesis/contribution, rerun the
two accepted cases and partial atomic cases, then complete the milestone's
orientation/direction coverage, neighboring gates, exact command and required
Radiant aggregate. No full aggregate success is claimed for this candidate.

Current clean candidate host SHA-256:
`8897a2125b0a8366df8e8a928ef5b7cd9037ac65f32df91ec12ac8322a52fc04`.
The latest owner delta has **13** partial-case metric decreases (down from 16);
all 42 full-match gains remain and no old full match is lost. The long-running
goal stays active.


Atomic-baseline dependency acceptance now starts with a nonsquare empty inline
atom, 80px in the baseline's transverse axis and 20px in the inline axis. Check
both vertical block directions, asymmetric physical margins, zero versus large
line-height, retained restoration, actual border-box placement and the exported
line baseline. The central baseline must be the margin-box midpoint, and merely
changing the atom's inline extent cannot change that transverse baseline. Source
and final alignment must consume the same synthesized baseline; keep forms and
actual content-baseline exports with their existing owners. A shared extent or
baseline adapter must also be used by the later line contribution path instead
of introducing another per-kind copy. Browser geometry and the native causal
failure lead implementation; the existing full-match audit and neighboring gates
remain required before qualification.


Atomic dependency progress: the margin-box central baseline and transverse-size
control now passes all twelve mutation states, and all 262 existing/new native
controls pass before adding the source-origin control. The shared synthesis
owner now feeds initial placement, final alignment and line contribution. The
new 18-state flex/grid/table source-origin control matches Chromium independently
(`temp/wpt-m22-source-origin-browser.{json,log}`). Before repair, flex/grid exports
count padding twice (10px then 20px as padding changes); their shared helper must
return a content-relative baseline because both publishers add the content offset.
That correction passes all flex/grid states. The same dominant-font adapter is
now used by explicit textarea editable-line exports, with selected-view metrics
kept local per **D4.5.1v4 / D4.1.4v5**. Last-line overflow remains under diagnosis.

The remaining table defect occurs at the source-to-physical publication boundary.
Pre-publication row baselines use the logical grid block axis correctly; the
post-publication recursive query still sums physical Y (now the inline axis).
The old last-row path instead centers a row and also visits back over the first
row. Capture the actual first/last row baseline before the swap, then rebase both
by the same grid-origin translation used by publication, including caption margins
and decoration. Completed vertical tables export that captured set; do not infer
a text baseline from a row center or use a cell's inline position. Retain the
horizontal query after positioned layout. Add the asymmetric-cell/caption case
to the reduced retained control; require restored source selection and padding.
Current Milestone 22 remains unqualified; 1,428/1,980 describes the earlier clean
candidate, not the in-progress atomic dependency.


Textarea diagnosis is now causal rather than inferred from a square WPT control.
The initial reduced box accidentally allowed two errors to cancel: a vertical
baseline was clamped to physical height (the inline extent), then a separate
half-font `clamped_baseline_tail` shifted neighbors. With block-size 90px,
inline-size 60px, padding 7px, border 5px and 40px editable leading, four lines
fail by 20px in both vertical directions, initially and after retained source
changes (`temp/wpt-m22-textarea-axis-before.log`). Chromium independently agrees
with the baseline at the physical block-end border and has zero delta in all
18 states (`temp/wpt-m22-textarea-axis-browser.{json,log}`). The owner must clamp
against the actual block-axis border extent, use block-start content decoration,
and count wrapping in the control's inline extent. Retire the compensating
sibling/text shift and its line snapshots/API parameter. Ordinary line ascent
and descent already include the neighboring atom and strut; the scroll-container
clamp does not justify adding another half-font tail. Preserve the overflow
measure used to allocate the actual neighboring below-baseline extent.


The shared source-origin control now also covers asymmetric cell fonts, multiple
cell lines, caption margins, and flow/flex fieldsets with retained padding changes.
Captioned tables pass. Flow fieldsets fail by 8px initially and 13px after the
padding mutation with controlled Ahem; flex fieldsets pass. Chromium independently
aligns each probe to the selected body line, excluding the legend
(`temp/wpt-m22-fieldset-browser.{json,log}`). Trace confirms the flow cache's first
baseline comes from the legend (32px), while body publication and normalization
position the body separately (`temp/wpt-m22-fieldset-trace-wpt-layout.log`).
HTML Rendering §15.3.12 places the rendered legend outside the anonymous fieldset
content box. Model that boundary explicitly: after laying out the legend, start
a fresh body baseline set and capture the actual source body cursor in a local
layout frame. On vertical legend normalization, translate those baseline offsets
from that source cursor to the used body origin at the legend's block-end edge
plus content padding. Keep the geometry publisher and baseline export on that
same origin, and remove the old width-difference/padding baseline adjustment and
fieldset-y correction. An empty body must retain an absent baseline set. This is
a coordinate/owner repair, not an adjustment by the observed 6/8/13px deltas.


All 264 native controls now pass after the owner fixes, and all seven originally
regressed accepted neighbors match fully (`temp/wpt-m22-atomic-source-native.log`,
`temp/wpt-m22-atomic-source-neighbors.log`). The expanded 36-state source-owner
matrix passes, including retained captioned-table and fieldset mutations. No
trace logging remains. The canonical audit is **1,418/1,980**, zero execution
errors, 41 gains and nine lost full matches versus qualified M21
(`temp/wpt-m22-atomic-source-delta.json`). This is explicitly unqualified: eight
native checkbox/radio baseline cases and the body propagation reference regress,
with 25 metric decreases total. The next bounded dependency is the remaining
native-control/atomic baseline consumer contract; preserve the shared central
baseline and investigate the dedicated owner rather than restoring bottom-edge
synthesis for all atoms. Accepted-neighbor inventory is running. The top-level
qualified status remains M21; no aggregate qualification is claimed.


Two further dependencies now have reduced causal controls. A replaced image
plus an inert `source` node makes a single-line inline report multiple lines and
export a 104px union instead of its 20px font box (40px image mutation gives
44px). A real forced line must still remain multiple lines. The cause is
`inline_fragment_union_extends_child_bounds`: overflow plus the font descent is
not evidence of a second source line. Remove that geometric classifier; retain
actual text/atomic line membership and explicit split-inline handling.
Evidence: `temp/wpt-m22-replaced-fragment-before.log`.

The nonsquare atomic-advance control passes without spaces but with controlled
Ahem spaces measures 140px instead of 100px in an auto vertical inline extent;
actual child positions still pass. The extent accumulator adds both the source
cursor gap and the same whitespace rect's advance. Publication already consumes
that leading gap only for the first atomic item; use the same single owner in
measurement. Extend the control to mixed/sideways orientation and positive
leading. Separately, top/bottom atomic boxes must enter the second-pass extent
constraints, not also add an unrelated synthesized baseline plus strut descent;
that explains the 322px bottom-aligned image becoming 331px in the sideways body
reference. Preserve the actual baseline contributors and solve the top/bottom
minimum extent as the existing line aligner does. No accepted scores or fixtures
are changed; the candidate remains unqualified.


The fragment classifier required removing both geometric guesses: the overflow
union check alone was insufficient because an inert inline sibling and image
still have different Y positions on one source line. It now consumes the
existing text/atomic/inline line IDs and explicit split-inline state, without
entering an atomic child's independent formatting context. All 266 native tests
pass and the picture regression matches fully. The gap/edge controls now pass
48 retained states across mixed/sideways orientation and both vertical block
directions. The body propagation reference also matches fully again. The mixed
writing-mode spiral and three nested sizing neighbors remain unresolved.

A checkable-control control causally reports an 8px baseline error for a 40×12px
box. Reuse the existing `FORM_INPUT_CAP_CHECKABLE` capability when choosing the
no-internal-baseline fallback; text controls retain their separate source owner.
Chromium agrees with central alignment without margins, but its native appearance
exports a border-box center with asymmetric margins: the 6px/14px probe differs
by 4px from CSS Writing Modes 3 §4.3 margin-box fallback. Do not claim that native
platform baseline is implemented. The current Lambda control has no intrinsic
baseline set, so the standard margin-box fallback remains the acceptance invariant
(`temp/wpt-m22-checkable-before.log`, `temp/wpt-m22-checkable-browser.{json,log}`).
The vertical cross-metric pass must preserve already captured dominant-baseline
ascent/descent rather than center alphabetic font boxes a second time. This is
the remaining shared source of the extra strut tail in sideways atomic lines.


The post-checkable/captured-line audit is **1,423/1,980**, zero errors, with three
lost full matches (`sizing-orthog-vlr-in-htb-012-ref`, the two vertical form manual
pages). All 267 native controls pass. The original native checkbox cases and
sideways body reference match fully again. Current acceptance is still pending.

The explicit text-edge baseline path also fails a reduced Ahem control in all
three principal modes. CSS Inline 3 §3.3 and Appendix A.3 require matching the
parent's selected baseline, synthesizing text-over at the over margin edge and
text-under at the under margin edge. The current shared adapter handles central
but maps every other atomic baseline to alphabetic; a separate `text-top` special
case then substitutes the atomic extent as a font baseline. Replace those
substitutions with one parameterized dominant-baseline position mapping, shared
by font and atomic synthesis; font ascent/descent retain their original total.
Text-over/text-under are endpoints and central is the midpoint. Ordinary font
alphabetic metrics remain the default. Remove the separate text-top line reset
so the same baseline-relative ascent/descent drives the generic line solver.
The reduced Chrome HTML probe does not honor text-edge dominant baselines and
is recorded as an interoperability limitation, not asserted as browser proof
(`temp/wpt-m22-dominant-edge-before.log`,
`temp/wpt-m22-dominant-edge-browser.{json,log}`). Acceptance here derives from the
specified baseline table; the diagnostic-font spiral is the original neighbor
and must be checked again after implementation. No reference change is planned.


The shared text-edge mapping passes its reduced control; all **268 native tests
pass**, and the canonical result remains **1,423/1,980**, zero errors
(`temp/wpt-m22-dominant-edge-native.log`, `temp/wpt-writing-path-m22-text-edge.json`).
The browser-style probe resolves the discrepancy: Chromium returns false from
`CSS.supports('dominant-baseline','text-top')`, stores no declared value, and
computes `auto`. Its Ahem and diagnostic-font HTML geometry therefore cannot
validate the specified text-edge behavior
(`temp/wpt-m22-dominant-edge-browser-style.{json,log}`). Do not restore the old
atomic-height substitution just to match a browser that rejects the property.
Keep the spec-derived native invariant and unchanged browser references; record
the accepted spiral mismatch as an unsupported reference feature, not a passed
gate. The candidate remains unqualified and the common nested sizing regressions
still require source-cursor/publication tracing. Temporary cursor traces are the
next diagnostic only; remove them after capture.


The nested sizing trace now locates the first disagreement: the outer atomic
container is laid out with an auto inline measure that wraps every child onto
its own surrogate line (`line=0..5`, X resets to zero), then physical publication
uses those cross-axis line advances as inline offsets. Before this milestone,
the incorrect atomic extent happened to hide that unresolved intrinsic sizing
path. Its auto fit-content preparation currently accepts only direct text
intrinsics; extend intrinsic composition for atomic children at the owner before
changing publication. Evidence: `temp/wpt-m22-atomic-cursor-trace-layout.log`.
All temporary cursor traces are removed after capture.

The three remaining lost full matches have another direct shared defect: the
later content-baseline contribution computes descent as physical height minus
baseline, even in a vertical line. The ordinary block reference expands a 26px
cross size to 126.5px; the text control expands a 57px cross size to 406px. The
initial aligner already has the correct transverse extent. Reuse that extent in
the later content and edge-alignment contributors, and normalize a text control's
virtual font baseline through its owner as for textarea. Add a reduced control
that changes inline-size while preserving the parent cross extent before the
repair. This is independent of the auto intrinsic-composition dependency.


Content-source verification now passes all **269 native tests** and the dimension
cast lint (`temp/wpt-m22-content-owner-native.log`,
`temp/wpt-m22-content-owner-lint.log`). The reduced content/text-control check
matches Chromium in all twelve mutation states; changing inline-size leaves the
parent's transverse extent equal to the atom's border extent
(`temp/wpt-m22-content-extent-browser.{json,log}`). After the dimension repair,
the cross-metric pass must also preserve a captured actual content baseline,
not add a second synthesized center for the same atom. All three prior full-match
losses recheck at 100% (`temp/wpt-m22-content-extent-wpt.log`).

The exact user command `make layout suite=wpt-css-writing-modes` now reports
**1,427/1,980** matches, **553** mismatches, no execution errors
(`temp/wpt-m22-user-command.log`). Current CLI SHA-256:
`6166c29e7af32e91ca55df66f9e57b054a500c74ab85abb9015d78dff2028364`.
The required `make test-radiant-baseline` is running at this boundary. This is
still an unqualified candidate: the nested atomic auto-inline intrinsic
composition dependency remains, and the accepted diagnostic-font spiral uses
an explicit baseline value rejected by this Chromium. Keep references, scores
and tolerances unchanged. The top-level qualified result remains M21 until
required acceptance is actually satisfied; the long-running goal remains active.


The nested auto-size trace narrows the intrinsic-composition hypothesis further:
`setup_inline` falls back to physical content width as the inline wrapping
measure for an orthogonal vertical auto-size box when direct-text intrinsic
preparation is unavailable. That is the wrong available-space axis, independent
of the eventual min/max intrinsic query. The module already owns the correct
CSS Writing Modes §7.3.1 bound in `layout_orthogonal_inline_basis` (fixed parent
limit / scrollport / initial containing block). Reuse that bound for provisional
inline layout, with the same content-box decoration conversion, while preserving
an already resolved fit-content or authored inline size. The used auto inline
extent can then be derived from the real formatted line. This does not claim
complete mixed-content min/max composition; retain that broader query work in
the backlog. Extend the existing atomic advance control to an inline-block root:
100px of inline content fits the 200px viewport even when its cross size is only
80px; changing that cross size must not create or remove inline wrapping. The
existing 48-state matrix supplies both directions, orientations, whitespace,
definite/auto inline sizes and retained restoration. Gate build has finished;
the UI runner is still active, so the new control is not built until it ends.


The aggregate boundary completed with all native/UI/DOM/page/render runners
passing, but **12 layout baseline regressions** remain (form 1, inline 1, sizing
3, images 1, lists 2, Markdown 4; `temp/wpt-m22-radiant-gate.log`). This is a
failed required gate, not a qualification. Additional category classification is
running to identify the exact cases. The source-line classifier's creation-line
marker also needs scrutiny: opening an inline before a soft wrap does not prove
it rendered a fragment on that line. Add a retained control in which the whole
inline moves from the line where it was opened; only actual text/atomic/BR
fragments, or non-empty inline decoration, can establish a previous line.


The available-space repair restores the original nested sizing reference to 100%
(`temp/wpt-m22-orthogonal-available-wpt.log`). The stronger inline-block control
then exposes a separate physical-X shrink floor: definite-inline vertical-lr
boxes become 140px/180px wide instead of their formatted 80px/120px cross extent.
That late path applies horizontal shrink-to-fit to physical width even when width
is the block axis. Its existing vertical-rl-only line-height cap hides the same
mistake in that direction. Compute the intrinsic inline shrink floor only when
physical width is the inline axis; for vertical block sizing retain the formatted
flow extent, actual in-flow box contributions and normal min/max constraints.
Remove the writing-mode-specific cap and redundant vertical exceptions. This is
axis ownership, not a new per-mode estimate or replacement intrinsic algorithm.
Evidence: `temp/wpt-m22-orthogonal-available-native.log`.


The 48-state atomic inline advance matrix now passes after the available-space
and block-axis sizing repairs (`temp/wpt-m22-block-axis-native.log`). Four late
consumer defects have independent failing controls before repair:

- Replaced canvas contribution adds the parent descender to an already complete
  synthesized margin-box split. CSS 2.1 §10.8.1 already includes the parent strut
  separately. Remove that duplicate contribution for all replaced atom paths.
- Inside marker font metrics retain their alphabetic split in a central-baseline
  line. Normalize local contribution metrics through the shared dominant-baseline
  owner, preserving stored marker paint metrics and total line-height.
- An inline opening cursor precedes an atomic soft wrap even when the inline has
  no rendered fragment on that earlier line. Determine source-line membership
  from actual text/BR/atomic fragments rather than the ordinary inline's opening
  line ID. Preserve explicit fragment unions.
- Checkable widgets already supply their native alphabetic border-edge baseline.
  Synthesize their missing central baseline only when the parent selects it
  (CSS Writing Modes 3 §4.3); preserve the supplied horizontal baseline. The
  strengthened retained control fails by exactly the authored 14px bottom margin
  before repair (`temp/wpt-m22-checkable-policy-before.log`).

Verification budget: reduced failing controls, full native text-query suite,
category classification for the failed aggregate categories, then canonical
writing-mode inventory and the required aggregate at a stable boundary. No
fixture references, accepted scores or comparison tolerances change. M22 remains
unqualified while the required aggregate has failures.


Late-consumer verification restores all prior form, canvas, decimal marker and
Markdown losses; the sizing ref/2a/2b and replaced-element-012 controls also
recheck at 100% (`temp/wpt-m22-late-consumers-{neighbors,sizing}.log`). Full native
verification catches one dependent ownership error: flex correctly publishes a
90px physical width while the generic finalizer sees an unused 15px flow cursor
(`temp/wpt-m22-published-width-trace-layout.log`). Preserve the published physical
block size of axis-aware layouts instead of inventing an intrinsic block-axis
floor. The existing zero-leading flex mutation test rejects the overwritten size.

The fragment classifier additionally needs real generated-marker membership. An
inside marker can remain on one source line while its following text wraps. Store
the marker's line ID after its own wrapping decision, and include that actual
fragment (or a reserving outside marker) in the shared classifier. Pure outside
markers do not establish an inline fragment. A reduced 100→160→100px retained
control fails before repair; this fixes source identity rather than restoring the
ordinary inline's speculative opening cursor. Temporary source traces are removed
from production and test code after capture.


Further source traces show the flex query itself returns the correct physical
50px width at 50px leading, but `calculate_item_intrinsic_sizes` swaps its physical
width/height again before storage. Bypass that legacy conversion for the branch
using the module's physical width/height queries, retaining the conversion for
its legacy logical/manual branch. Strengthen the retained test to inspect each
item as well as its container (`temp/wpt-m22-owner-detail-layout.log`). The finalizer
must preserve the flex owner's used width after this source correction.

Generated-marker line identity is necessary but not sufficient for a marker-only
first line: the marker placement must also expose its indivisible edge to the
existing soft-wrap solver, and adjacent text lookup must recognize that real
fragment. Reuse the existing atomic-boundary wrap policy, including common-parent
nowrap and glue decisions. The reduced 100→160→100px case matches Chromium's
40→20→40px union (`temp/wpt-m22-marker-fragment-browser.{json,log}`); the pre-fix
trace puts both marker and overflowing text on source line zero. Keep the reduced
case unchanged and repair that missing metadata. Pure outside markers stay out
of the text-boundary stream.

The provisional orthogonal available-space branch must also preserve already
resolved fit-content constraints and exclude positioned contexts, whose owner
resolves their used inline size. The canonical audit exposes the latter through
20 RTL positioned text losses. The four ancestor-scrollport available-size
controls expose a separate mixed-text intrinsic-composition dependency; investigate
its used fit-content extent rather than accepting changed browser scores.


The physical width repair now restores the zero-leading/50px/zero-leading flex
container and each item's cross extent. The exploratory assertion on each item's
physical height exposes the existing independent vertical max-content-height
estimation (it uses horizontal line-height for atomic inline advances). Keep that
query limitation in the backlog; it predates this repair and is not a changed
reference or accepted score. The retained native gate continues to assert the
selected block-axis width behavior and the two real item widths.

Two clone/slice sizing neighbors reveal a real decoration-only start fragment:
ignoring every ordinary inline opening cursor is correct for an undecorated whole
wrap, but loses an actually materialized empty decorated fragment. Record optional
one-based source-line ranges on fragment unions through the existing union helper;
zero means no recorded source identity. Text and actual decoration publication
provide the source line, while generic geometric unions and inline-CB snapshots
do not invent one. Reset ranges when the union's presence bit is reinitialized.
The classifier combines these actual source ranges with text/BR/marker/atomic
fragments, retaining the existing block-split union path. Promote the existing
inline decoration predicate for reuse rather than copying its edge calculations.
Only actual start decoration (or cloned end decoration) can publish an empty
start fragment. The new retained slice/clone control challenges this boundary
alongside the existing undecorated text/image whole-wrap controls.


The decoration browser probe refines the trigger precisely: a separator before
the inline moves its whole decorated box (one fragment), whereas the separator
inside the inline can leave an empty start fragment (two fragments). Cover both
variants for slice/clone and 100→160→100px retained widths, using the observed
source-fragment invariant rather than the earlier assumption that every decorated
opening must render on the previous line. Chromium confirms both variants
(`temp/wpt-m22-decoration-{fragment,space}-browser.{json,log}`). The undecorated
whole-wrap, tall atomic overflow, marker-only first line and zero-leading width
controls pass together. Aggregate qualification is still pending.


Stable boundary: **273/273 native tests pass**, dimension-cast lint and diff
checks pass. The canonical writing-mode audit reports **1,427/1,980**, no execution
errors: **45 gains / 4 full-match losses versus qualified M21**. The remaining
losses are available-size-003/005/013/014, all the ancestor-scrollport mixed-content
case described above; no other M21 full match is lost. Current Chromium reproduces
the unchanged reference's 180.625px orthogonal inline extent, confirming that this
is not merely a stale capture (`temp/wpt-m22-available-current-browser.{json,log}`).
Its expected fit-content result still requires a specification-derived mixed-content
inline contribution, not the longest formatted line used by the current fallback.
Keep these four losses unresolved and preserve the qualified M21 heading.

Accepted text/inline/sizing/multicol/position classification restores every prior
loss except the unsupported explicit dominant-baseline diagnostic-font spiral;
clone/slice now recheck at 100%. Evidence:
`temp/wpt-m22-fragment-boundary-{native,inventory,neighbors,sizing}.log`, canonical
`temp/wpt-writing-path-m22-fragment-boundary.json`. CLI SHA-256 at this boundary:
`c569fdcf32a121842d85a279325d4da252abe88b47b4ebbf2468e40099fd4173`. The required
Radiant aggregate is now running with these source changes. M22 remains
unqualified and the long-running goal remains active.


The first aggregate attempt stops during compilation with `No space left on
device`, before any test result (`temp/wpt-m22-fragment-boundary-radiant-gate.log`).
Remove only disposable generated `temp/layout_batch_<pid>_<epoch>_<batch>`
directories (2,904, about 0.3GiB); retain reduced sources, source backups, canonical
inventories, browser probes and classification summaries. The required aggregate
retry has completed compilation and entered its test runners; evidence is
`temp/wpt-m22-fragment-boundary-radiant-retry.log`. This environment failure does
not qualify the candidate or alter acceptance criteria.


Required aggregate retry completes: **6,454 total, 6,310 full passes, 141 partial,
2 skipped, 1 failure** (`temp/wpt-m22-fragment-boundary-radiant-retry.log`). All
native/UI/DOM/page/view/render runners pass, including 273 native text-query
controls and all 729 UI automation cases. Layout has 3,156 full / 137 partial /
1 failure out of 3,294: the diagnostic-font
`dominant-baseline-mixed-writing-modes-002` remains the sole accepted-baseline
loss. The required gate therefore fails; it is not a qualification. Keep the
spec-derived explicit text-edge behavior and the unsupported Chromium reference
discrepancy visible. Independent DOM range verification also passes **79/79**
(`temp/wpt-m22-dom-range-{build,native}.log`). Source inspection confirms that
the union snapshot owner copies the entire expanded structure, including its
new line IDs; retained native mutation controls verify restored geometry. No manual snapshot field copy
or new ownership rule is introduced.

The user's exact `make layout suite=wpt-css-writing-modes` command completes
at this unchanged boundary with **1,427 matches / 553 mismatches**, no execution
errors (`temp/wpt-m22-final-user-command.log`). Canonical gains/losses and host
identity are summarized in `temp/wpt-m22-final-delta.json`. SHA-256 stays
`c569fdcf32a121842d85a279325d4da252abe88b47b4ebbf2468e40099fd4173`. Remaining work
within M22 is the four ancestor-scrollport mixed-content inline sizing losses
and the explicit-text-edge comparison discrepancy; the qualified heading remains
M21 and the unbudgeted long-running goal stays active.


M22 continuation: the previous turn is progress (shared fixes, 273 native passes,
79 DOM-range passes, canonical audit and completed required gate). Current tree
and artifacts confirm the same candidate, with no live verification process.

The available-size-003 owner trace changes the next action: line measure is the
correct 96.3281px, and the atom sees cursor=right=96.3281, prior content and a valid
wrap opportunity (`temp/wpt-m22-sizing-owner-layout.log`). The source wrap is
correct. Later publication reconstructs an auto atomic inline's position from
the maximum predecessor text extent rather than its actual source-line X, and
the auto physical block-size pass can replace the sum of all formatted lines
with a first-line cross extent. These are source geometry ownership defects,
independent of the still incomplete logical-inline intrinsic composition.

Finite repair: preserve the complete formatted block extent, use each atom's
finalized source inline coordinate, and publish its parent-line block offset
from the shared final line solver. The solver has the real line origin, selected
content/synthesized baseline, all contributors and top/bottom constraints; the
geometry publisher must not synthesize another first-line baseline. Cache one
pass-produced scalar beside the atomic line ID, reset it before layout and copy
it through the existing geometry snapshot owner. Keep D4.5.1v4 region/pool
ownership and copy-as-value contracts; no language ruling changes.

Acceptance before broad qualification: 24 retained wrapping states (two vertical
directions, auto/definite inline constraints, equal/unequal atomic cross sizes,
80→160→80px restoration), the original four sizing controls and all existing
central/margin/content/fieldset/table/marker/fragment tests. A source-line counter
proves the intended line and geometry checks verify both its origin and centering.
Then run the full native gate, canonical writing-mode audit, relevant neighboring
categories and required aggregate only at a stable boundary. Defer the separate
min/max inline composition until geometry publication itself preserves the real
source lines. No reference or tolerance changes.


Implementation refinement: cache the shared solver's **block-axis alignment
correction** relative to the atom's actual source Y, rather than a second absolute
line origin. Subsequent fieldset/body translations and relative-position removal
then preserve the parent-line position by normal affine translation, without new
shift walkers. Reset the scalar to zero before block layout and copy it alongside
the atomic line ID in measurement snapshots. Inline publication consumes actual
source X, and block publication consumes source Y plus this correction; neither
reconstructs an inline cursor or recomputes a first-line baseline. Existing
multicol/block-flow ownership remains separate. The browser agrees with all
reduced auto/definite wrapping states (`temp/wpt-m22-source-lines-browser.json`),
so the original four controls are not grounds to assume a general Chromium
orthogonal sizing bug. Preserve the evidence and resolve their real mixed-content
interaction. Temporary source traces are removed before implementation.


The hot DOM size ratchets reject storing this scalar on every `DomNode` (80→88
bytes, also enlarging text and elements). Preserve those limits: keep the scalar
in existing optional `DomElementExt` metadata, with zero reads/resets avoiding
allocation on cold elements. Fragment reset also clears it; measurement snapshots
copy it explicitly with the atomic line ID. This follows D4.5.1v4/D4.1.4v5's
existing pool retention and copy-as-value ownership, without changing either
ruling. Browser geometry validates all 24 new retained states, including automatic
orthogonal sizing. The before-repair native matrix has the intended failures
(source line IDs are correct; widths and physical positions are wrong), rather
than a fixture-loading failure (`temp/wpt-m22-source-lines-before-native.log`).


### Wrap-up checkpoint — 2026-10-27

The user requests wrapping up. Stop further feature work and restore the last
verified M22 candidate as a unit. The source-line publication experiment passes
the new 24-state wrapping control, but full native verification reports 269/274
with five regressions (`temp/wpt-m22-source-line-owner-native.log`); it is not
ready to retain in the working implementation. Preserve it as
`temp/wpt-m22-source-line-publication-unqualified.patch`, the reduced test as
`temp/wpt-m22-source-lines-pending.cpp`, and browser/source trace evidence. Remove
only that new experimental test from the registered native suite with its rolled
back implementation; preserve every previously verified test, fixture, reference,
accepted score and tolerance. The current candidate therefore returns to its
previous 273-test native boundary.

Restart evidence: (1) complete surrogate block-flow extent may supplement atomic
inline line sizing, but a normal block-flow parent has a separate physical owner
and must not be enlarged by a copied provisional flow cursor; (2) content baseline
exports use block-start coordinates while the line solver uses line-over, and
currently several consumers add physical margin-top rather than the parent's
transverse over margin. A shared baseline-consumer conversion must precede use of
the final line solver result for vertical-lr first/last textarea and fieldset
margins. Do not fit these differences with per-control constants; (3) mixed-content
logical-inline min/max composition remains separate. The source trace proves
source wrapping itself is correct for available-size-003; publication was losing
the selected line.

Only the unqualified experiment is rolled back across six files. All previous
M12–M22 changes remain, including marker metrics, canvas and checkable baseline
repairs, physical flex intrinsic-query ownership, real fragment source-line
ranges and accepted neighbor restorations. Rebuild and verification are running
under `temp/wpt-m22-wrapup-*`. The last required aggregate remains explicitly
failed (6,454 total, 6,310 full, 141 partial, 2 skipped, 1 diagnostic failure); do
not relabel M22 qualified. Preserve the full objective, and pause its long-running
execution at the user's wrap-up request after the checkpoint is verified.


Wrap-up verification completes after restoration: **273/273 native**, **79/79 DOM
range**, dimension-cast lint and diff checks pass. The exact writing-mode command
returns **1,427/1,980 matches, 553 mismatches** again. Logs:
`temp/wpt-m22-wrapup-{build,native,dom-range,lint,user-command}.log`; machine-readable
checkpoint: `temp/wpt-m22-wrapup-state.json`. The saved unfinished patch passes
`git apply --check` against this restored source, and the restoration manifest is
`temp/wpt-m22-wrapup-restored-sources.json`. Rebuilt CLI SHA-256:
`e375722f5bab9688bd6e6711fd50f59bc0589cb4f9fdf5d1efcbe3180c4510ed`.
The required aggregate was not repeated after rolling back to its previously
verified source boundary; its one diagnostic failure remains unresolved and M22
remains unqualified. Four previous writing-mode matches are still lost in the
ancestor-scrollport sizing group. The goal is incomplete; pause execution at the
user's request with all source changes and restart evidence preserved.

## Inline baseline repair — 2026-10-27

**Outcome / priority.** Repair the sole `wpt-css-inline` aggregate regression,
`dominant-baseline-mixed-writing-modes-002`, as a bounded follow-up. The prior
writing-mode goal remains stopped. Preserve the selected-style ownership rules
of **D4.5.1v4 / D4.1.4v5**; no formal CSS baseline ruling covers this behavior.

**Diagnosis and design.** The fixture is in quirks mode. `line_break` contributes
the block's explicit root strut even when the existing quirks predicate says
that no root strut should exist. Fix that contribution at its owner, before
baseline placement and height calculation. The diagnostic font has valid
800/-200 metrics; its name denotes its BASE alphabetic coordinate, not zero
ascent. Independently, Chromium rejects both `dominant-baseline:text-top` and
the upstream reference's `alignment-baseline:text-top`. Investigate a supported
CSS equivalent through the existing source-fingerprinted standards-control
framework, keeping the raw capture and accepted scores unchanged. Nested
orthogonal geometry must be explained by CSS baseline/axis rules before any
additional engine edit.

**Acceptance / scope.** Reduced quirks-versus-standards cases must exclude or
retain the block strut respectively, including a font-size change. Existing
text-edge/orthogonal baseline invariants must pass. The original inline category
must pass without fixture-specific engine branches, skipped cases or relaxed
tolerances. Independent writing-mode sizing work remains outside this repair.

**Verification budget.** Use reduced browser probes and targeted native tests
while editing, then the complete native text-query suite, dimension-cast lint,
the 169-case inline category and the required Radiant aggregate once stable.

Authorities: [Quirks Mode §3.4](https://quirks.spec.whatwg.org/#the-blocks-ignore-line-height-quirk),
[CSS Inline 3 §4.1](https://www.w3.org/TR/css-inline-3/#dominant-baseline-property),
[§4.2.2](https://www.w3.org/TR/css-inline-3/#alignment-baseline-property)
and [§5](https://www.w3.org/TR/css-inline-3/#line-height).

**Implemented behavior.** The quirks predicate now selects the line's `flow_box`
rather than requiring a BFC owner, and includes limited quirks as required by
the standard. The suppressed root strut cannot reappear in the reported line
height. Vertical auto inline sizing reuses the existing predecessor-extent
helper, so text before an orthogonal atom contributes even when transverse
baseline alignment puts the atom's surrogate y at zero. RTL inline sizing
retains the already-complete surrogate advance rather than adding it a second
time or dropping its text portion in non-sideways vertical modes.

Intrinsic measurement now shares the selected font baseline and leading
calculation across its sideways-width and horizontal-height paths. An
orthogonal baseline-aligned atom contributes before/after extents to the same
union as direct text. Taking just the maximum height lost fractional leading,
underestimated max-content, and introduced unintended wraps inside nested
shrink-to-fit boxes. No numeric rounding allowance was added.

**Browser oracle.** Both the fixture's inherited `dominant-baseline:text-top`
and its upstream reference's explicit `alignment-baseline:text-top` select
corresponding text-over baselines (CSS Inline 3 §4.1 / §4.2.2). For this fixture's
single-line undecorated spans, the supported shorthand
`span { vertical-align: text-top; }` expresses that alignment. A browser-generated
`dominant-baseline__dominant-baseline-mixed-writing-modes-002.standards.darwin.json`
in the linked `lambda-test/layout/reference/wpt` corpus records this stylesheet,
its specification/reason, upstream match and fixture SHA-256. The raw capture,
fixture, accepted scores and tolerances remain unchanged. Regenerate with the
exported `extractLayoutFromFile` function in `test/layout/extract_browser_references.js`,
passing the original fixture, force=true, platform=`darwin`, category=`wpt-css-inline`
and the saved `reference_control` as `options.referenceControl`.

**Completion record.** The original case matches all 15 element and 12
text boxes against that control; `make layout suite=wpt-css-inline` passes all
169 required fixtures. Three native regressions cover quirks/standards struts,
both inline directions in vertical/sideways sizing, and retained baseline changes
with fractional leading; the intrinsic query also preserves its caller's flow
owner. The complete native suite passes **276/276**. The final
`make test-radiant-baseline TEST_JOBS=1` exits **0**: **6,471 total, 6,328 full
passes, 141 accepted partial passes, 2 skips, zero failures**. Its layout
component passes, including all 169 required inline cases. Dimension-cast lint
and diff checks pass. Final aggregate log:
`temp/inline-baseline-qualified-aggregate.log`; focused geometry evidence:
`temp/inline-baseline-qualified-original.log`. This bounded inline repair is
complete; the separate writing-mode backlog recorded above is unchanged.

## M23 — preserve wrapped vertical inline geometry (qualified, 2026-10-27)

**Priority / outcome.** Restore correct wrapping and physical geometry for mixed
text/atomic vertical lines before extending intrinsic composition. This is a
shared prerequisite for the four known available-size losses and other wrapped
orthogonal content. The fresh canonical checkpoint has **1,428/1,980 full
matches, 552 mismatches, zero execution errors**, with no losses against the
restored M22 checkpoint. The separate inline repair is qualified; M22's old
aggregate failure is superseded by that repair, while its four sizing losses
remain open.

**Design and ownership.** CSS Writing Modes 3 §7.3.1–§7.3.2 determines the
orthogonal available size and fit-content measure; §6.3/§7.5 maps line-relative
geometry to physical axes. The inline solver owns each atom's source line and
final position. Geometry publication must project those coordinates instead of
reconstructing an inline cursor from maximum predecessor extents. For inline-only
vertical contexts the complete formatted block advance must survive; a
first-line baseline union cannot replace the sum of all lines. Normal block-flow,
multicolumn and axis-aware flex/grid/table owners retain their separate extents.

Implement in steps: preserve formatted inline-only block extent; project the
actual source inline coordinate; then reconcile baseline-consumer coordinate
origins before publishing final per-line transverse alignment. The previous
unqualified patch is evidence only, not an implementation to restore wholesale.
In particular, content baselines measured from block-start must be converted to
the parent's line-over coordinate with the corresponding physical margin.
Any required cached scalar belongs in optional element metadata and existing
geometry snapshots, following **D4.5.1v4 / D4.1.4v5**; do not enlarge hot DomNode
storage or introduce an ownership ruling.

**Acceptance / scope.** Reproduce the 24 retained wrapping states with both
vertical block directions, auto/definite inline measures, equal/unequal atomic
cross extents and 80→160→80px restoration. Verify the real source line ID, physical
inline origin, cross alignment and complete block extent. Preserve every current
native baseline/textarea/fieldset/root-flow control and every current full WPT
match. Recheck available-size-003/005/013/014 after geometry repair; their separate
mixed-content fit-content discrepancy must remain visible if unresolved.

**Verification budget.** Reduced native/browser cases per step; complete native
text-query gate after the shared repair; then canonical 1,980-case delta,
dimension-cast lint and required Radiant baseline at a stable milestone boundary.
No fixture, reference, accepted-score or tolerance changes are planned.

Authorities: [CSS Writing Modes 3 §7.3](https://www.w3.org/TR/css-writing-modes-3/#orthogonal-flows),
[§6.3](https://www.w3.org/TR/css-writing-modes-3/#line-directions)
and [CSS Inline 3 §5](https://www.w3.org/TR/css-inline-3/#line-height).

M23 step evidence: source projection plus preserving complete inline-only block
advance leaves all 277 existing native cases passing; only the new wrapping
control still fails, now exclusively on transverse alignment. The unchanged
Chromium probe confirms all 24 expected states. The first archive build hit disk
capacity; clearing 54 MiB of disposable WPT profiles/batches and rebuilding the
failed archive serially restored the build. Logs: `temp/wpt-m23-projection-native.log`
and `temp/wpt-m23-source-lines-browser.json`. No fixture or acceptance changes.

M23 ownership refinement: the final inline solver can store the source block
coordinate directly in the atom's existing geometry. Existing geometry snapshots
and affine translations already preserve it, so no optional metadata scalar or
new snapshot field is needed. Pure projection passes all 24 wrapping states.
The remaining native failures expose the intended dependencies: inline extent
measurement still reads transverse source y, and exported content baselines
still use physical margin-top/block-start at line-over consumers. Measurement
now uses actual source inline x; RTL preserves the existing line-local advance
fallback until its automatic inline measure is resolved. A shared
`layout_inline_content_baseline` converts exported border-box block-start
baselines to the parent's margin-box line-over coordinate, including opposite
vertical block directions and asymmetric physical margins. Both initial
contribution and final alignment call that conversion. The existing source
baseline regression is strengthened with asymmetric margins; expectations are
still structural alignment with the selected descendant, without fitted offsets.
The obsolete publication-time line solver and its scalar parameters are removed.

M23 candidate: **279/279 native tests pass**, including asymmetric baseline-source
margins, 24 retained wrapping states, nonzero-leading text-edge alignment and
positioned percentage-sized canvas reflows. The latter exposed an inherited
caller `flow_box` in `setup_inline`; establishing the actual line owner there
restores the single newly lost WPT case. The canonical audit has **1,445/1,980
full matches, 535 mismatches, zero execution errors: 17 gains and no losses**
against this resumed run. Required aggregate qualification is running in
`temp/wpt-m23-required-aggregate.log`.

Correct the older four-case diagnosis: fresh Chromium queries return
min-content ≈12px, max-content ≈181px and ancestor scrollport content height
≈96px, yet automatic orthogonal height chooses max-content. A definite available
measure makes it wrap to ≈96px and places the green span over the red oracle,
as the WPT assertion requires. CSS Writing Modes 3 §7.3.1–§7.3.2 therefore does
not justify fitting the engine to that raw 181px capture. The prior claim that
mixed-content min-content composition explains this result is disproved by the
browser's own min-content query. Keep the raw capture unchanged while auditing
a source-checked standards control and its text-range geometry. Evidence:
`temp/wpt-m23-available-sizing-browser.{json,log}`. This browser gap is separate
from the real source-line geometry defects fixed above.

M23 aggregate follow-up: the first required gate exposes four table-source
fixtures and one textarea-overflow fixture in `wpt-css-inline`. Strengthened
native checks now assert the complete table width (four authored glyph columns,
captions and decorations) and the textarea's parent line extent as well as
baseline coincidence. Table measurement was querying a physical inline extent
as a block extent; it now preserves the cell formatter's complete source block
advance, and custom cell formatting explicitly selects its own flow owner.
The textarea trace proves a correct formatted line extent followed by an old
textarea-specific overflow addition in the generic child-width query. Remove
that special case and the obsolete first-line baseline reconstruction. Inline-only
block sizing uses the actual formatted advance; atomic margin-box queries do
not import descendant scroll/ink overflow. No fitted offsets or replacement
reference data are introduced.

The repaired candidate passes **279/279 native** and **169/169 inline**. Canonical
and exact-command writing-mode checks both report **1,445/1,980**, **17 gains,
zero losses, zero execution errors** versus the resumed checkpoint. Dimension-cast
lint and diff checks pass. The second required aggregate is running in
`temp/wpt-m23-required-aggregate-repaired.log`; the first gate remains recorded
as failed rather than being presented as qualification. Disk recovery removed
only stale isolated Radiant test caches older than a day with no live owner;
all fixtures, source backups, checkpoints and logs remain. Cleanup manifest:
`temp/wpt-m23-stale-cache-cleanup.json`.

**M23 completion.** The repaired required gate exits **0**: **6,488 total,
6,345 full passes, 141 accepted partial passes, 2 skips, zero failures**.
All 279 native cases and all 169 inline baselines pass. The exact writing-mode
command and canonical audit agree on 1,445 matches, with 17 gains and no lost
matches against this resumed run. Dimension-cast lint and diff checks pass.
CLI SHA-256: `a614357871f3942fe8cc353df355b902125ef08838fb7ce7c0e0c4d7f9a746a4`.
Evidence: `temp/wpt-m23-{required-aggregate-repaired,user-command,final-delta}.log/json`
(the concrete files are `wpt-m23-required-aggregate-repaired.log`,
`wpt-m23-user-command.log` and `wpt-m23-final-delta.json`). The complete conformance
goal remains active.

The temporary scrollport standards-control audit matches every text box and
the two-column placement, but exposes a separate engine discrepancy: after
wrapping, the orthogonal auto inline size shrinks to the longest painted line
(≈84px) rather than the fit-content result (≈96px). The control has no fitted
dimensions: a fixed-size query container exposes its authored content height via
`100cqh`. Raw Chromium instead chooses ≈181px by omitting the ancestor fallback.
Neither mismatch should be hidden by changing accepted scores. The next finite
milestone is an axis-correct intrinsic inline query and fit-content sizing;
references remain unchanged until that behavior and the control's equivalence
are independently verified.

## M24 — mixed-inline orthogonal fit-content sizing (qualified, 2026-10-27)

**Priority / outcome.** Retain the CSS Writing Modes 3 §7.3.2 fit-content inline
size when text and atomic inline children wrap. This dependency precedes broader
float/conformance residue: final source geometry is now correct, but selecting
the longest rendered line can still shrink an auto-sized orthogonal box below
its specified sizing result.

**Owning stage / hypothesis.** The pre-layout branch in `layout_block` resolves
fit-content only when `layout_measure_direct_inline_text_intrinsics` succeeds;
its collector rejects atomic children. The later geometry measurement therefore
has no resolved inline-size to preserve. Investigate a logical inline contribution
query that shares existing text normalization, min-run boundary rules and font
measurement. Do not treat a physical-width query or formatted line maximum as a
logical max-content contribution. Reuse the common sizing formula and existing
measurement/snapshot contracts (**D4.5.1v4 / D4.1.4v5**); keep caller font, flow
owner and published geometry intact across probes.

**Acceptance.** Strengthen the 24-state wrapping regression to assert the used
inline size: auto 80px under the narrow available constraint, max-content 120px
under the wide constraint, with definite 80/160px unchanged. Add a causal mixed
text/atomic intrinsic case that distinguishes minimum, available and maximum
contributions. Preserve pure-text, zero-leading, baseline, source-line and
positioned controls. Verify the four ancestor-scrollport fixtures against the
temporary standards controls and their green-over-red assertion. Audit any
remaining text-range or used-size disagreement before persisting a control;
the original raw captures, accepted scores and tolerances remain intact.

**Scope / verification budget.** Finish the useful inline-only flow path first;
record unsupported independent layout families rather than inventing guessed
contributions. Use reduced native/browser probes while choosing the shared
query, then the full native gate, canonical delta against qualified M23,
dimension-cast lint and the required Radiant baseline at the next stable boundary.
Checkpoint: `temp/wpt-m23-qualified-state.json`, source backup directory
`temp/wpt-m23-qualified-source`, binary `temp/wpt-m23-qualified.exe` and patch
`temp/wpt-m23-qualified.patch`. M23 remains the qualified implementation until
M24 meets its acceptance cases and required gates.

**Implementation.** The pre-layout query now accepts direct
text mixed with atomic inline contributions. Text collection shares its existing
whitespace/forced-break scanner; the query folds logical advances with the shared
min-run boundary algorithm. Atomic boxes contribute their CSS margin-box size:
definite preferred sizes use the authored-size helper (a length need not have a
keyword tag), while parallel auto flow roots recursively contribute their
min/max-content sizes with padding, border and min/max constraints. Descendant
break metadata stays inside each atomic formatting context. The available-space
fit-content formula is unchanged.

The query uses `LayoutMeasureScope`, per-child `LayoutViewScope`, block and font
scopes (**D4.5.1v4 / D4.1.4v5**). Selecting the measured child before style
resolution is necessary: resetting the caller's view instead corrupts retained
auto sizing. The child also receives the shared computed-display box role before
style resolution: an unclassified inline has no block sizing slots in the legacy
vertical style adapter. Measurement restores that role with its scalar snapshot.
Unsupported contribution families return an unresolved query rather
than substituting a previously rendered size: cyclic percentage sizes/box edges,
orthogonal auto block sizes, replaced auto sizes, flex/grid/table content,
ordinary nested inline wrappers and generated content remain separate work.

`OrthogonalFitContentUsesAtomicInlineContributions` distinguishes the minimum
(60px), available (80px) and maximum (156px, then 186px after a child-font
mutation), includes asymmetric inline padding/border/margins, and checks that a
repeated scalar query preserves geometry and caller handles. Independent Chromium
captures confirm all 24 mutation states (`temp/wpt-m24-intrinsics-browser.json`).
The strengthened 24-state source-line regression also asserts the used root
inline size, retaining definite-size controls.

**Qualification finding — ratio transfer.** The first required aggregate exposed
one sizing neighbor, `aspect-ratio/block-aspect-ratio-003.html`: an empty vertical
box with definite width and preferred aspect ratio was assigned zero fit-content
height before the existing ratio transfer could run. CSS Sizing 4 §4.2 makes this
automatic axis ratio-dependent on the definite opposite axis. The fit-content
eligibility now preserves that ordering and delegates to the existing ratio and
box-sizing implementation. Inline line measurement also consumes that transferred
size before the existing automatic content-based minimum is applied; using the
physical content width there kept a text-containing vertical box unwrapped.
`AspectRatioDeterminesOrthogonalAutoInlineSize`
checks both vertical directions, empty/wrapping/unbreakable text, both box-sizing
modes and 100→160→100px width mutations. Chromium independently confirms all 36
states, including the automatic min-content floor above the preferred ratio.
The initial aggregate failure remains recorded
in `temp/wpt-m24-required-aggregate.log`; the repaired gate passes below.

**Standards captures.** Four separate `.standards.darwin.json` captures for
`available-size-{003,005,013,014}` now accompany the unchanged raw references in
the linked `lambda-test` corpus. The control is
`#scroller { container-type: size } #ortho { inline-size: 100cqh }`.
CSS Writing Modes 3 §7.3.1–§7.3.2 supplies the nearest fixed scrollport's content
height and fit-content formula; CSS Conditional 5 §7 defines the container unit.
These fixtures have definite scroller width/height after min-size clamping.
Independent min/max-content measurements put that available height strictly
between the intrinsic bounds. The audit verifies unchanged scroller geometry
and exact green-over-red coverage in all four cases; it retains the intervening
parent's automatic sizing and spacer. Every capture records the original source
hash, transformation, specification and reason. No fitted dimension, tolerance,
accepted score or upstream fixture changed. Evidence:
`temp/wpt-m24-scrollport-audit.json`, `temp/wpt-m24-role-controls.log`.

**Qualification / current checkpoint.** The canonical suite inventory and exact
user command report **1,454/1,980**, 526 mismatches, zero errors: **nine gains,
zero lost full matches versus M23**. Five gains are against unchanged raw
captures; four additionally use the audited standards controls. Native tests
pass **281/281**. The required Radiant aggregate passes **6,490 total, 6,347
full, 141 accepted partial, two skips, zero failures**; inline is **169/169** and
sizing is **776/776**. Dimension-cast lint and `git diff --check` pass. The
qualified binary SHA-256 is
`789f63b9b84577655a4ed3b369bda9b1a17fa62feb2262d07afaf1fd7f7b5933`.
Evidence: `temp/wpt-writing-m24-final.json`, `temp/wpt-m24-final-delta.json`,
`temp/wpt-m24-final-native.log`, `temp/wpt-m24-required-aggregate-repaired.log`,
`temp/wpt-m24-user-command.log`, `temp/wpt-m24-qualified-lint.log`.
The goal remains active; the 526 remaining mismatches are not accepted passes.

## M25 — logical grid tracks and replaced contributions (qualified, 2026-10-27)

**Priority / outcome.** Follow the remaining logical-axis contribution dependency
into grid sizing. Both `img-intrinsic-size-contribution-001.html` and `-002.html`
remain at 20% element matching. Their source assertion requires a vertical
parent's intrinsic inline contribution to use the image's physical height,
including when the image itself uses horizontal writing. CSS Writing Modes 3
§7.1 governs that mapping. First trace the existing natural-size query and grid
track adapter. The M24 probe confirms that the image itself retains its correct
200×100px physical dimensions, while the auto-width grid remains 800px wide
instead of 204px, and its item is 798×201px instead of 202×150px. The existing
reference matches fully. This is a grid coordinate-space defect, not an image
resource-dimension failure (`temp/wpt-m25-image-initial.log`).

**Owning stages / design.** `layout_grid_container` feeds physical width/height
to logical columns/rows; `measure_grid_items` and `collect_item_contributions`
query physical X for every column; placement/alignment and the final item-flow
context retain those assumptions. The final `layout_publish_vertical_children`
cannot repair constraints chosen on the wrong axis. Establish the container's
column (inline) and row (block) physical axes once in pass-owned grid state.
Keep track geometry logical and view geometry physical, mapping areas through
the shared writing-mode/start-direction helpers. Query intrinsic contributions,
constraints, box edges and overflow on the selected physical axis. Size automatic
container block extent from row tracks; preserve definite inline extents. Normal
grid-item flow should reuse the canonical block content formatter with its
assigned physical box, rather than growing another vertical-only flow solver.
Absolute grid areas and baseline exports must consume the same mapping; remove
the old publication representation switch when that mapping owns the geometry.

**Causal prerequisite.** The new 30-state grid regression is independently
confirmed by Chromium (`temp/wpt-m25-grid-browser.json`) and fails on M24
(`temp/wpt-m25-causal-before.log`). Its horizontal control also catches a shared
recursive height-query defect: the flow parent passes its whole available width
to an auto-sized canvas's ratio transfer. At a 250px grid inline size the actual
canvas remains 200×100px, but the query predicts a 124px height; changing the
bitmap width to 120px inflates that prediction further. Before mapping grid
tracks, normal-flow replaced children must query their own used width through
the existing intrinsic width API, preserving the parent's percentage basis.
CSS 2.1 §10.3.2/§10.6.2 owns this choice; a flex/grid-assigned replaced size keeps
its separate algorithm-owned constraint. The prerequisite preserves all 281
previous native cases and repairs the horizontal controls before track mapping
(`temp/wpt-m25-natural-native.log`).

**Implementation / current evidence (unqualified).** Grid state borrows its
container; columns/rows retain logical inline/block coordinates, and the common
area mapper publishes physical rectangles with direction-aware starts. Track
contributions select physical constraints and the existing width/height queries.
Automatic block extent comes from row tracks. Normal grid-item flow reuses
`layout_block_inner_content`; the obsolete post-layout vertical swap is removed.
Physical width queries solve rows for vertical grids instead of returning the
column extent. The original image cases and the 30-state replaced/retained grid
matrix pass; native tests initially pass 283/283.

The first full inventory is **1,420/1,980**, two gains but 36 lost matches
(`temp/wpt-m25-first-delta.json`), so this is not a qualified improvement. The
losses identify a shared formatter regression: `BlockContext::given_*` can hold
provisional dimensions during absolute layout. It cannot establish final used
size ownership. An explicit borrowed `used_size_owner`, set by the grid formatter
and checked against the current box, distinguishes algorithm-assigned dimensions
from provisional constraints and prevents inheritance into descendant boxes
(**D4.5.1v4 / D4.1.4v5**). Restore ordinary absolute/normal-flow sizing while
retaining grid-assigned wrapping and physical publication.

The alignment matrix covers fixed tracks, gaps, distributed content alignment,
physical padding, logical self-alignment, and explicitly placed absolute grid
areas in all three principal writing modes and both directions. A cached computed
style already carries correct self-alignment in `BlockProp`, but fresh per-pass
grid-role adapters initially retain `auto`; project the computed keywords after
style resolution. Additional auto-item controls use nonsquare replaced content
and percentage margins to challenge column-cache/physical-width confusion and
the grid-area logical-inline percentage basis. Independent browser evidence:
`temp/wpt-m25-grid-auto-areas-browser.json` (18/18 states). These repairs await
native, canonical-delta and required-gate qualification. Margin-trim reversal,
automatic absolute self-alignment and full vertical baseline sharing remain
separate conformance work; no acceptance claim is made for them here.

**Required-gate feedback.** The ownership repair recovers all 36 lost matches:
**1,456/1,980**, two raw-reference gains, zero lost full matches, zero errors;
native checks pass **283/283**. The first required aggregate nevertheless fails
six sizing entries (`temp/wpt-m25-required-aggregate.log` and
`temp/wpt-m25-sizing-failures.json`): ratio-grid automatic minima (039/042),
the button descendant in flex-aspect-ratio-042, intrinsic keyword constraints
on grid canvases, and two percentage-sized range-control cases. Keep this failed
qualification as evidence.

Track definiteness and final automatic block extent need separate decisions.
A ratio transfer is definite for track sizing, while its automatic block size
still takes the content minimum during publication. Retain the existing ratio
provenance instead of treating every positive `given_*` as an authored block size;
containment and parent-assigned boxes retain their independent used constraints.
Definite contribution shortcuts must yield to intrinsic min/max keywords and
call the existing query.
Buttons contain CSS flow even though the widget adapter classifies them as
replaced; a shared CSS-replaced predicate separates that distinction without
altering the broader UI classification. Replaced grid content has no normal CSS
flow to finalize: retain its already-resolved used box and resource/control
baseline instead of applying the flow finalizer's intrinsic minimum again.
These changes are under focused qualification; they do not waive the six losses.

The repaired six fixtures now match fully
(`temp/wpt-m25-sizing-repair-focused.log`). New retained native controls exercise
ratio-grid content growth/shrinkage with and without a maximum, and canvas
intrinsic min/max block constraints while the authored height changes between
zero and a larger definite size. Together with the logical grid matrices, all
**285 native cases** pass (`temp/wpt-m25-sizing-repaired-native.log`). A fresh
canonical inventory and required aggregate are still needed for qualification.

The second aggregate exposes the cost of conflating those decisions: after
replacing the track-definiteness predicate with CSS automatic-size detection,
two ratio-track sizing/alignment cases, four containment cases and two inset-sized
root grids regress (`temp/wpt-m25-required-aggregate-repaired.log`,
`temp/wpt-m25-neighbor-wpt-css-{sizing,position}-failures.json`). Restore positive
transferred constraints for track sizing; resolve the ratio-derived automatic
content minimum only at the shared container-size publication boundary. The
native ratio control now also asserts centering within the ratio-defined row.
Run both affected categories to completion before another aggregate attempt.

Both affected categories now pass completely: **776/776 sizing**, **139/139
positioning** (`temp/wpt-m25-track-neighbors.log`), with **285/285 native cases**
(`temp/wpt-m25-track-minimum-native.log`). Ratio controls also distinguish an
explicit zero minimum and scrolling overflow from the content-based automatic
minimum, per [CSS Sizing 4 §4.3](https://www.w3.org/TR/css-sizing-4/#aspect-ratio-minimum).
The positive-font, nonsquare-content alignment control isolates axis ownership;
the earlier zero-font probe's 18px strut remains a separate investigation
(`temp/wpt-m25-auto-alignment-before.log`), not an acceptance claim here.

**Finite acceptance / budget.** Compare both originals and their reference, then
reduce to a nonsquare replaced box in each vertical grid direction with parallel
and orthogonal child writing modes. Distinguish natural width/height from track
inline/block axes, retain declared grid sizes and automatic minima, and mutate
the available inline size and resource dimensions. Reuse the existing resource,
box-metric and contribution APIs; preserve selected-view/region ownership
(**D4.5.1v4 / D4.1.4v5**). Use focused native/browser checks while establishing
the cause, then relevant grid/replaced neighbors, the canonical M24 delta,
dimension-cast lint and the required Radiant milestone gate. Do not absorb the
independent WPT reporter-DOM differences or font-metric backlog into this stage.

**Qualified outcome.** Both original image-contribution cases now match fully.
The final canonical inventory and exact user command report **1,456/1,980**,
524 mismatches and zero execution errors: **two gains, zero losses versus M24**.
The other 1,978 fixtures retain every element/span/text score, including partial
scores. No fixture, reference, tolerance or baseline changes were needed for M25.
The 30-state replaced/retained grid matrix includes physical intrinsic-width
queries; the 18-state alignment matrix includes auto-sized nonsquare content and
percentage margins. Ratio and intrinsic-keyword regressions retain their state
across CSSOM changes. All **285 native tests** pass.

The required Radiant aggregate exits successfully: **6,494 total, 6,351 full,
141 accepted partial, two skips, zero failures**. Inline is **169/169**, sizing
**776/776**, positioning **139/139**; dimension-cast lint and `git diff --check`
pass. Binary SHA-256:
`0c65202be5e527531cb248a3d334c03898217c2ead0f6208c28c209eecb037a8`.
Evidence: `temp/wpt-writing-m25-qualified.json`,
`temp/wpt-m25-qualified-delta.json`, `temp/wpt-m25-qualified-user-command.log`,
`temp/wpt-m25-track-minimum-native.log`, `temp/wpt-m25-track-neighbors.log`,
`temp/wpt-m25-required-aggregate-final.log`,
`temp/wpt-m25-track-final-lint.log`. The two failed aggregate attempts remain
recorded above. M25 is complete within its finite scope; the overall conformance
goal remains active and incomplete.

## M26 — orthogonal flex-item content and contributions (qualified 2026-10-27)

**Priority / finite outcome.** Continue into common flex layout with orthogonal
items. `flexbox_align-items-stretch-writing-modes.html` remains at 60% element
matching. The horizontal item and cross-axis stretch are already correct. Each
vertical item is 50px wide instead of 100px, and its two 50×50px block children
still progress along physical Y instead of the item's logical block axis.
The captured reference requires 50/100/100px item widths within the 250×100px
container (`temp/wpt-m26-initial-probes.log`).

**Trace / design.** Inspect physical intrinsic-width contributions and the flex
item's content formatter independently before editing. Determine where the
item's writing mode stops affecting block-child progress, and whether the width
query disagrees with canonical flow measurement. Reuse existing block formatting,
logical-axis mapping and intrinsic APIs; preserve the flex algorithm's main/cross
used constraints and selected-view ownership (**D4.5.1v4 / D4.1.4v5**).

**Acceptance / budget.** Fully match the original and reduce it to rectangular
block children in both vertical directions, with a parallel horizontal control.
Retain the document while mutating child dimensions and the available flex size;
assert contributions, used sizes and physical child placement. Use focused
native/browser checks, then affected flex/sizing neighbors, the canonical M25
delta, dimension-cast lint and the required aggregate at the stable boundary.
The separate padding/min-width and ancestor-overflow disagreements observed in
the same probe log remain in the backlog unless the trace proves a shared cause.

**Causal evidence / implementation shape.** The retained rectangular-child matrix
is independently confirmed by Chromium (30/30 item states,
`temp/wpt-m26-flex-blocks-browser.json`) and fails on M25 before production edits
(`temp/wpt-m26-causal-before.log`). The module width query independently returns
40px instead of 80px for two 40px-wide vertical block children; the used flex
item and physical child positions also fail. Non-flex width measurement already
uses the shared physical width API, so its final text-metric swap is not the
owning cause. The generic width walker takes a maximum over normal block
children on both writing axes. For a vertical block sequence, accumulate their
physical block contributions using the existing used-flow adjoining-margin gap
and trailing-margin rules. Keep mixed inline/float intrinsic runs on their
existing path until separately addressed. Final vertical flex-item flow must
call the canonical block formatter rather than mapping only text fragments.

**First implementation evidence (unqualified).** The original now matches
fully; all 286 native cases pass before extending the margin controls. The first
canonical inventory is **1,457/1,980**, one full-match gain, zero lost full
matches; two previously failing direction-upright captures also improve their
element scores (`temp/wpt-m26-first-delta.json`). Retained publication requires
resetting `vertical_geometry_published` before new source flow. The canonical
formatter owns physical publication and assigned dimensions, so the old flex
content-height refinement must not add its padding a second time.

The expanded browser matrix covers parallel/no-parallel controls, decorations,
adjoining block margins and retained dimension changes (100/100 item states,
`temp/wpt-m26-flex-margin-controls-browser.json`). It catches a shared logical-box
candidate defect: `LayoutCssBoxCandidates` always projects logical margins as
horizontal/LTR, making vertical block margins enlarge physical height. Replace
that duplicate mapping with the cascade's existing component winner/projection
APIs. Sizing neighbors expose `ortho-writing-mode-001`: the width fold reads a
provisional percentage margin based on outer width instead of the orthogonal
inline basis. Re-resolve that margin through the existing spacing helper inside
a measurement snapshot, preserving **D4.5.1v4 / D4.1.4v5**. `button-min-width`
also needs its native block-axis label centering at canonical text publication,
where the old flex-only text mapper already supplied it. Both neighbor losses
remain unqualified (`temp/wpt-m26-neighbors.log`).

**Repaired checks / allocation audit.** Both sizing neighbors now match fully,
and the complete neighboring inventories pass **776/776 sizing** and **139/139
positioning** (`temp/wpt-m26-repaired-neighbors.log`). The 100-state adjoining-margin
matrix and retained button text/fragment centering controls pass; all **287
native tests** pass (`temp/wpt-m26-final-native.log`). The exact user command
reports **1,457/1,980**, 523 mismatches, zero errors. The canonical delta retains
every previous score and improves three fixtures (one to a full match).

Before closing, the source audit identifies a second automatic width assignment
inside `finalize_block_flow`, after the assigned-size check. Add a causal flex
grow/shrink control with `min-width:0`: assigned main sizes must survive while
fixed child blocks overflow in the container's logical direction. The independent
browser control confirms nine item states (`temp/wpt-m26-flex-allocation-browser.json`).
The additional native assertion is pending; do not transfer the earlier checks
to this enlarged acceptance case without verifying it.

The first aggregate passes **6,496 total, 6,353 full, 141 accepted partial, two
skips, zero failures** (`temp/wpt-m26-required-aggregate.log`), but the independent
allocation control fails as predicted (`temp/wpt-m26-allocation-before.log`):
vertical item widths return to 80px instead of their assigned 150px or 30px.
Guard the second automatic width publication with the existing used-size owner.
The expanded flex/grid browser control confirms 18 item states. The repaired
native suite passes **288/288**, and all sizing/positioning neighbors still pass
(`temp/wpt-m26-assigned-final-native.log`, `temp/wpt-m26-final-neighbors.log`).
The final canonical writing-mode inventory remains **1,457/1,980**, zero errors,
one full-match gain and no lost scores. A fresh aggregate must qualify the final
publication guard before M26 is closed.

**Qualified outcome.** The final required aggregate passes **6,497 total,
6,354 full, 141 accepted partial, two skips, zero failures**
(`temp/wpt-m26-required-aggregate-final.log`). All **288 native cases** pass,
including the retained 100-state contribution/margin matrix, vertical native
button label/fragment centering, and flex/grid grow/shrink allocation with
logical overflow. Inline remains **169/169**, sizing **776/776**, positioning
**139/139**. Dimension-cast lint and `git diff --check` pass.

The final canonical inventory and exact user command report **1,457/1,980**,
523 mismatches and zero execution errors. Compared with M25 there is **one
full-match gain, zero lost full matches and no reduced element/span/text score**.
The original flex stretch case now matches fully; the two direction-upright
captures improve their element scores but remain incomplete. The other 1,977
fixtures retain every score. M26 changes no fixture, reference, tolerance,
baseline or harness. Binary SHA-256:
`793765f5bec88b98138a35ff5e5bb0808497832199f4f79adb72a2f3b472b593`.
Evidence: `temp/wpt-writing-m26-final.json`, `temp/wpt-m26-final-delta.json`,
`temp/wpt-m26-final-user-command.log`, `temp/wpt-m26-assigned-final-native.log`,
`temp/wpt-m26-final-neighbors.log`, `temp/wpt-m26-qualified-lint.log` and the
final aggregate above. The earlier failed causal and neighbor checks remain
recorded. M26 is complete within its finite scope; mixed inline/float intrinsic
sequences remain separate work, and the overall conformance goal stays active.

## M27 — automatic vertical block-size constraints (qualified 2026-10-27)

**Priority / outcome.** Preserve ordinary minimum/maximum sizes when vertical
normal flow publishes its automatic physical block width. This shared sizing
boundary affects padded orthogonal content and its shrink-to-fit ancestors;
it precedes isolated text-orientation and font-metric residue. On the M26 binary,
`orthogonal-child-with-padding.html` still publishes its inner width as 50px
instead of 100px after a retained height change. The 50px content minimum and
50px horizontal padding are already present in the source; the outer box is
correct (`temp/wpt-m27-initial-probes.log`).

**Owner / hypothesis.** Trace the computed minimum, initial size, final automatic
width assignment and physical geometry publication before editing. The normal
vertical finalizer assigns a new content-derived width after earlier constraint
resolution. Reuse `layout_apply_min_max_axis` with the actual candidate box
convention, applying constraints before child geometry uses the final width.
Keep overflow extents distinct from the constrained used border box, and retain
parent-owned flex/grid sizes (**D4.5.1v4 / D4.1.4v5**).
[CSS Sizing 3 §3.1 and §3.3](https://www.w3.org/TR/css-sizing-3/#sizing-properties)
govern min/max and box edges;
[CSS Writing Modes 3 §7.3](https://www.w3.org/TR/css-writing-modes-3/#orthogonal-flows)
governs orthogonal axis mapping. No formal Lambda ruling change is proposed.

**Acceptance / scope / budget.** Match the original, then use both vertical
directions with content-box/border-box, empty and rectangular block content,
minimum/maximum constraints, decoration floors and retained content/constraint
mutations. Assert the used box, physical child positions and independently
observable overflow. Establish a causal native failure and independent browser
expectations before production changes. Run focused checks, affected sizing and
positioning neighbors, the canonical M26 delta, dimension-cast lint and the
required Radiant aggregate at the stable boundary. The ancestor-overflow case
`sizing-orthog-htb-in-vlr-007.xht` remains separately diagnosed: its definite
400px child currently enlarges body/html through overflowing descendants. Do
not fold that independent contribution rule into this milestone without causal
evidence of a shared defect.

**Causal checks.** The initial 28-state browser matrix confirms the independent
used-box and child-placement expectations; the native counterpart fails on M26
(`temp/wpt-m27-constraints-browser.json`, `temp/wpt-m27-causal-before.log`).
The finalizer drops min/max when it replaces the provisional physical width with
the measured automatic block extent. Apply the existing border-box-aware
constraint helper at that assignment, before physical publication. The original
now matches and all 28 child boxes/positions are correct; the native test still
fails ancestor widths under maxima (`temp/wpt-m27-first-native.log`).

That retained control isolates two further sizing decisions: the horizontal
parent's `layout_block_child_width_contribution` substitutes a child's overflowing
content extent for its used width even when that width is the child's vertical
block axis; only a provisional inline-axis stretch needs this substitution.
The intrinsic width query also ignores the maximum entirely when minimum exceeds
maximum, instead of raising the effective maximum to the minimum. Fix these
shared contribution boundaries and add direct query assertions plus a parallel
horizontal control. This is evidence for enlarging M27 to its shrink-to-fit
consumer; it does not establish a cause for the separate vertical-parent
`vertical_child_block_contribution` overflow rule recorded above.

The expanded 42-state browser control also confirms the horizontal case. Its
native counterpart exposes the same parent contribution error: the provisional
inline-axis content proxy must retain the child's min/max constraints, while a
vertical block-axis width already has its used owner. Direct width queries
independently fail minimum-over-maximum precedence
(`temp/wpt-m27-contribution-causal-before.log`). Preserve these three distinct
checks rather than treating the original fixture's success as qualification.

**Stable candidate / pre-aggregate evidence.** The repaired shared boundaries
pass all **289 native tests**, including the 42 retained browser-verified
constraint states and independent physical intrinsic queries
(`temp/wpt-m27-native.log`). Complete sizing and positioning neighbors pass
**776/776** and **139/139** (`temp/wpt-m27-neighbors.log`); dimension-cast lint
and `git diff --check` pass. The canonical inventory is **1,458/1,980**, 522
mismatches and zero execution errors: one full-match gain, zero losses and no
reduced scores versus M26. `clearance-calculations-vrl-004.xht` improves from
50% to 87.5% element matching but remains incomplete; all other 1,978 fixture
scores stay unchanged (`temp/wpt-m27-first-delta.json`). These checks precede
the required aggregate qualification; no fixture/reference/harness change is
involved.

**Qualified outcome.** The exact user command confirms **1,458/1,980**, 522
mismatches and zero execution errors (`temp/wpt-m27-user-command.log`). The
required Radiant gate exits successfully with **6,498 total, 6,355 full, 141
accepted partial, two skips, zero failures**
(`temp/wpt-m27-required-aggregate.log`). Inline is **169/169**, sizing **776/776**,
positioning **139/139**; all **289 native cases**, dimension-cast lint and
`git diff --check` pass. The canonical delta records one gained full match,
one partial improvement and no reduced scores, with no baseline/tolerance
changes. Binary SHA-256:
`f295e08d8ee6d227e8ba132db6b060ca45716b9cee6cfd5539eb1bacf580996b`.
The earlier causal failures and first-fix ancestor failures remain recorded;
the final 42-state matrix is independently verified by Chromium and includes
direct intrinsic queries, retained mutations, overlapping constraints,
decoration floors and both vertical directions with a horizontal control.
M27 is complete within its finite scope. The next contribution boundary below
remains unresolved, and the overall conformance goal stays active.

## M28 — used block contributions versus descendant overflow (qualified 2026-10-27)

**Priority / outcome.** Keep a definite vertical child box's used block extent
as its contribution to an automatic ancestor and to following siblings. This
common normal-flow sizing boundary has greater dependency value than isolated
text-orientation differences. The existing orthogonal sizing fixture
`sizing-orthog-htb-in-vlr-007.xht` has correct child/text geometry, but its 400px
child's descendant overflow enlarges body to 473.5px and html to 673.5px instead
of 400px and 600px (`temp/wpt-m27-initial-probes.log`, captured on M26).

**Owner / hypothesis.** Trace the child's authored/used width, scalar intrinsic
contribution, vertical parent flow cursor, descendant overflow and root width
independently. `vertical_child_block_contribution` recursively promotes overflow
only for a definite vertical width; determine whether that rule confuses
intrinsic sizing with used normal-flow progress. Reuse the existing used-flow
and overflow owners, preserving margins and selected-view ownership
(**D4.5.1v4 / D4.1.4v5**). The relevant normative boundaries are
[CSS 2.2 §10.6.3/§10.6.7](https://www.w3.org/TR/CSS22/visudet.html#normal-block)
and their [CSS Writing Modes 3 §7.4 mapping](https://www.w3.org/TR/css-writing-modes-3/#logical-to-physical).
No implementation change or acceptance claim has been made for this stage.

**Finite acceptance / budget.** Reproduce the original on the qualified M27
binary, then establish independent browser and causal native controls with
nonsquare descendants overflowing a definite child in both vertical directions.
Check the automatic ancestor, a following sibling, the child's own box and its
overflowing descendant; mutate child/content sizes and include a naturally
auto-sized child control. Preserve existing margin/orthogonal behavior. Use
focused checks, affected sizing/positioning neighbors, the canonical M27 delta,
dimension-cast lint and the required Radiant milestone gate. Keep fragmentation,
floats and out-of-flow overflow extensions outside the initial acceptance scope
unless they expose a shared defect in these owners.

**Causal evidence / design decision.** The original reproduces on the frozen M27
source and binary (`temp/wpt-m28-original-before.log`). Chromium confirms all 30
retained control states across horizontal/vertical flow, asymmetric decoration,
fixed size/content mutations and an automatic-size control
(`temp/wpt-m28-block-contribution-browser.json`). Before production changes,
the native control fails with an automatic LR parent at 118px instead of 68px
and its following sibling at 101px instead of 51px; direct intrinsic queries
already return the correct 68px (`temp/wpt-m28-causal-before.log`).

Both used-flow extent and physical sibling publication call
`vertical_child_block_contribution`, whose recursive descendant scan substitutes
90px overflow for a definite 40px child. CSS 2.2 §10.6.3 and the Writing Modes
§7.2 mapping advance by the used child box. Remove this separate overflow
contribution helper and consume the already-final physical child width at both
call sites. Existing margin helpers retain adjacency; existing content-bounds
and scroll owners retain overflow. Do not apply an intrinsic-size contribution
rule to the normal-flow cursor. The repeated native query setup is extracted
into one fixture helper, retaining its caller-view preservation assertion
(**D4.5.1v4 / D4.1.4v5**).

**Neighbor feedback / line ownership.** The first implementation passes all
290 native cases and raises the canonical inventory to **1,462/1,980**: four
gains, no losses or reduced scores (`temp/wpt-m28-first-delta.json`). The sizing
neighbor gate fails three atomic-inline fixtures, while positioning stays
**139/139** (`temp/wpt-m28-neighbors.log`). Preserve these failures; the candidate
is unqualified. Parent `vertical-align:bottom` had reclassified its atomic
inline children as block-flow boxes. The removed recursive scan accidentally
supplied their line advance. Neither the parent's alignment in its own line nor
its definite inline size changes the formatting category of its children.

Independent Chromium controls isolate overflowing Ahem atomic inlines in both
vertical directions, with parent bottom/baseline alignment and retained width
changes (`temp/wpt-m28-atomic-browser.json`). The native control fails for the
bottom-aligned parent at 32px instead of 112px; its baseline-aligned counterpart
already uses line flow, with a separate 2px extent difference
(`temp/wpt-m28-atomic-causal-before.log`). Remove the alignment-dependent
classification from used-flow, inline-extent and physical publication, leaving
atomic placement to the existing line-box/baseline owner. Check the residual
baseline extent before qualification rather than restoring recursive overflow.

**Baseline coordinate trace.** The debugger records the child's exported last
baseline as 47px, with a parent central strut split of 4/4px and authored 13px
line-height (`temp/wpt-m28-baseline-debugger.log`). The existing leading helper
assigns the odd remainder below line-over. In vertical-lr, line-over is block-end;
exporting that raw line-over offset as a block-start baseline reverses the
remainder and shortens each enclosing line by 1px. Vertical-rl already agrees.
Keep the leading calculation and font metrics unchanged. A shared baseline
export helper reuses the existing line-over/block-end predicate and converts
the line's baseline to block-start coordinates before caching or consuming it.
Use it for both committed lines and the active last-line path. Chromium confirms
20 retained atomic states with odd/even line-height, both directions and both
parent alignments (`temp/wpt-m28-atomic-browser.json`). These controls still
require native and aggregate qualification.

**Wrap-up candidate.** Both formatting owners now agree with the independent
controls: all **291 native cases** pass, including 30 retained block-flow and 20
retained atomic-line states, direct intrinsic queries, caller-view preservation
and signed overflow bounds (`temp/wpt-m28-final-native.log`). Full sizing and
positioning neighbors pass **776/776** and **139/139**
(`temp/wpt-m28-final-neighbors.log`). The final canonical inventory is
**1,462/1,980**, 518 mismatches and zero execution errors: four gained full
matches, two partial improvements, no lost full matches and no reduced scores
(`temp/wpt-m28-final-delta.json`). The other 1,974 fixtures retain every score.
Dimension-cast lint and `git diff --check` pass. The required aggregate is
running before the requested wrap-up; do not treat this candidate as qualified
until it finishes. No fixture, reference, tolerance or baseline changes were
made. The user requested stopping after this milestone on 2026-10-27; preserve
the remaining conformance inventory and pause the long-running goal after the
final result is recorded.

**Required-gate feedback.** The first aggregate has one failure out of 6,500:
`wpt-css-multicol/baseline-005`, while inline, sizing, positioning and all 291
native cases pass (`temp/wpt-m28-required-aggregate.log`). The isolated complete
multicol gate confirms the same single regression
(`temp/wpt-m28-multicol.log`). The export helper unnecessarily replaced the
line's raw metric with `line_baseline_position`'s strut fallback, giving an empty
multicol line an additional baseline and shifting the flex sibling by 16px.
Preserve the original metric for export, converting its coordinate only when
line-over and block-start differ. A strut lookup may supply the active line's
height, but must not replace the exported metric. This corrects the helper's
overreach without adding a multicol exception. Keep the failed gate as evidence
and rerun qualification on the repaired binary before closing M28.

**Repaired checks.** The metric-preserving export passes all **291 native
cases** (`temp/wpt-m28-metric-preservation-native.log`). Sizing is **776/776**,
positioning **139/139**, and multicol **334 full plus 28 accepted partial, zero
failures** (`temp/wpt-m28-repaired-neighbors.log`). The repaired canonical
inventory has exactly the same score for every fixture as the preceding
candidate: **1,462/1,980**, four gains and no lost/reduced scores versus M27
(`temp/wpt-writing-m28-repaired.json`, `temp/wpt-m28-repaired-delta.json`).
Dimension-cast lint and `git diff --check` pass. A fresh aggregate is running to
qualify this final binary; the earlier aggregate failure remains recorded above.

**Qualified wrap-up.** The repaired required aggregate exits successfully with
**6,500 total, 6,357 full, 141 accepted partial, two skips, zero failures**
(`temp/wpt-m28-required-aggregate-repaired.log`). All **291 native tests** pass;
inline remains **169/169**, sizing **776/776**, positioning **139/139**, and
multicol **334 full plus 28 accepted partial, zero failures**. Dimension-cast
lint and `git diff --check` pass. The exact user command confirms
**1,462/1,980**, 518 mismatches and zero execution errors
(`temp/wpt-m28-repaired-user-command.log`). The final canonical delta is four
full-match gains, two partial improvements, no losses or reduced scores versus
M27; all other 1,974 fixture scores remain identical. No fixture, raw reference,
tolerance, accepted baseline or harness was changed.

Binary SHA-256:
`e920cf1cbccfc205d575749477c0356c487bb9470b4ea9ace2914d00ef43e639`.
The frozen binary, 17 owned source files, patch, inventory, delta and gate
evidence are indexed by `temp/wpt-m28-qualified-state.json`; the M27 checkpoint
is retained. M28 closes its finite used-flow and atomic-line ownership scope.
The overall suite remains incomplete. Work is paused at the user's request;
no next milestone has started. Resume from the 518 mismatches in
`temp/wpt-writing-m28-qualified.json`, keeping the failed qualification attempts
above as evidence rather than an accepted baseline.


### Milestone 29: orthogonal blocks retain containing-flow inline placement

**Priority / outcome.** P1. Mixed horizontal/vertical components must retain their
inline margins and container direction when the parent publishes physical
geometry. The merged checkout starts at `de0bf2e16`; its fresh canonical audit is
identical to M28 for all 1,980 records: 1,462 full matches, 518 mismatches, zero
execution errors (`temp/wpt-writing-m29-resumed.json`,
`temp/wpt-m29-resumed-delta.json`). This is the comparison base for this milestone.

**Evidence / owner.** In `percent-margin-vlr-007.xht` both orthogonal children's
physical sizes and x positions already agree, but their y positions lose the
resolved 89px and 20px top margins. `layout_publish_vertical_children` explicitly
assigns `child_block->y = content_top` in its orthogonal branch. The same branch
ignores the containing block's RTL inline-start in `abs-pos-border-offset-001`.
The independent float placement defects found during triage remain separate.
Original evidence: `temp/wpt-m29-triage.log`.

**Standards / design.** CSS Writing Modes 3 [§7.3](https://www.w3.org/TR/css-writing-modes-3/#orthogonal-flows)
uses the child's writing mode for sizing and the containing block's writing mode
for placement. [§7.2](https://www.w3.org/TR/css-writing-modes-3/#dimension-mapping)
maps CSS2 §10.3 inline margin calculations onto the containing inline axis and
resolves margin percentages against that inline size. Preserve the existing
percentage resolver and used dimensions; position the border box from the
physical content origin plus its resolved margin, choosing the overconstrained
edge from the containing flow. Reuse the existing in-flow margin/axis helpers
rather than adding an orthogonal-only margin solver. Explicit self-alignment
continues through the existing CSS Align 3 §6.1.1 owner after base placement;
auto margins retain priority. No ownership or language ruling changes
(**D4.5.1v4 / D4.1.4v5**).

**Finite acceptance.** Vertical-lr and vertical-rl containers, LTR/RTL container
direction independent of child direction; asymmetric content edges; fixed,
negative and percentage inline margins; one/both auto margins; retained parent
inline-size and child margin changes. Check overflowing fixed-size children and
explicit self-alignment if they challenge the same placement owner. Keep child
sizes, sibling block progression and descendant-local geometry stable. Include
original percentage-margin and bordered static-position WPT cases. Floats,
fragmentation, table internals and inline atomic placement retain their own owners.

**Verification budget.** One independent Chromium geometry matrix and a native
regression that fails before the repair; focused originals after each relevant
edit, full native text-query and sizing/position/inline neighbors when stable;
canonical writing-mode delta, dimension lint, diff check and required Radiant
baseline once at this milestone boundary. Preserve failed checks rather than
changing references, comparison policy or accepted baselines.


**Causal control.** Chromium independently confirms all 176 retained states
(`temp/wpt-m29-inline-placement-browser.{js,json,log}`). The same native matrix
fails before the change, including the simplest 7px top margin and the
unmargined RTL following sibling (`temp/wpt-m29-causal-before.log`). The shared
horizontal margin function is generalized through `LayoutAxisRefs`, preserving
existing callers on X and using Y for orthogonal publication. Reset auto slots
before distributing free space; an overflowing auto margin must follow CSS2
§10.3.3's overconstraint rule instead of losing the RTL constrained edge. The
existing alignment resolver handles non-normal alignment after margin placement.
No child sizing, descendant coordinates or percentage basis is replaced.


**Boundary-cache control.** The first repair passes every main-child y check
and both original `percent-margin` documents, with the sizing percentage-margin
original also at 100% (`temp/wpt-m29-first-originals.log`). Its native matrix
still fails on the unmargined following sibling and the child RTL descendant,
both already failing before this change (`temp/wpt-m29-first-native.log`). The
shared normal-flow margin solver and its horizontal caller were guarded by the
presence of `BoundaryProp`; a box with no boundary declaration therefore never
received RTL overconstraint placement. Return the used physical start margin
from the same solver even when its optional cache is absent. Persist margin
values only into existing slots; the caller consumes the returned offset without
allocating an otherwise unnecessary boundary. The same 176-state control keeps
these pre-existing adjacent failures visible until repaired. Partial bordered
WPT cases still include parallel-flow/static-position residue and are not yet
claimed as full matches.


**Neighbor feedback / column coordinates.** The canonical audit gains eight
full matches with no losses or reduced scores: 1,470/1,980, zero errors
(`temp/wpt-m29-first-delta.json`). All 301 native cases, sizing 776/776 and
positioning 139/139 pass. Inline has one regression,
`initial-letter/Initial-letter-breaking-rtl`, recorded in
`temp/wpt-m29-neighbors-wpt-css-inline-failures.json`; qualification is withheld.
The newly corrected physical RTL child offset is collected as a logical column
inline offset, then reflected again at final column publication. The observed
354px error is exactly column width minus the 230px child width. CSS Multicol 1
[§2](https://www.w3.org/TR/css-multicol-1/#the-multi-column-model) makes column boxes
the containing blocks for their in-flow children; changing column progression
must preserve placement inside each column. Convert the collected normal-flow
physical offset to inline-start coordinates before column distribution, reusing
the existing reverse-column predicate. Spanners retain their full-width owner.
Chromium confirms 24 reduced states across LTR/RTL, boundary presence, zero,
asymmetric and auto margins and retained widths
(`temp/wpt-m29-columns-browser.{js,json,log}`). The new native control reproduces
the coordinate defect before its repair
(`temp/wpt-m29-columns-causal-before.log`). This is a qualification prerequisite
exposed by the shared placement repair, not an initial-letter exception.


**Column repair.** The main column-flow collector now converts the child's
normal-flow offset from physical-left to inline-start before the existing RTL
column reflection. The 24 reduced column states and all 176 orthogonal states
pass natively (`temp/wpt-m29-columns-native.log`). The exact rejected inline
fixture returns to 100% element/text agreement
(`temp/wpt-m29-inline-regression.log`). Full native, neighboring categories and
canonical inventory are rerunning on this candidate; the required aggregate has
not yet qualified it.


**M29 qualified.** All 302 native cases pass
(`temp/wpt-m29-final-native.log`). Sizing is 776/776, positioning 139/139,
inline 169/169, and multicol 334 full plus 28 accepted partials with zero failures
(`temp/wpt-m29-final-neighbors.log`). The canonical inventory and exact requested
command agree on **1,470/1,980**, 510 mismatches, zero execution errors
(`temp/wpt-writing-m29-final.json`, `temp/wpt-m29-user-command.log`). There are
eight gained full matches, three partial improvements, no losses or reduced
scores; all other 1,969 fixture scores remain identical to the resumed checkout
(`temp/wpt-m29-final-delta.json`). The required Radiant aggregate exits 0 with
**6,525 total, 6,383 full, 140 accepted partials, two skips, zero failures**
(`temp/wpt-m29-required-aggregate.log`). Dimension-cast lint and diff checks pass.
No fixture, reference, tolerance, accepted baseline or harness changed.

The qualified binary SHA-256 is
`34047c5d1e8b71c68c3763edccf7769bc6deaeef68df8f1a15f430ac9f0d0132`.
`temp/wpt-m29-qualified-state.json` indexes the frozen binary, six changed source
files, patch, canonical inventory, delta and evidence. M29 closes the orthogonal
margin/RTL placement and necessary column-coordinate scope. The goal remains
active; the next finite milestone is parallel vertical block inline placement,
using the independently verified 528-state matrix rather than expanding into
unrelated float or static-anchor residue.


### Milestone 30: normal vertical block children share inline placement

**Priority / outcome.** P1. Complete the same everyday containing-flow margin
placement for parallel vertical block children. M29 repaired orthogonal children;
parallel publication still uses raw physical top margins and ignores the
containing direction and auto margins. `abs-pos-border-offset-001` retains this
parent-box error even though its orthogonal parent boxes now agree. Native block
sizes and positioned descendants must remain owned by their existing stages.

**Design / standards.** CSS Writing Modes 3 §7.2–§7.3 and CSS2 §10.3.3 require the
same inline margin equation regardless of whether the child's flow is parallel
or perpendicular. Move M29's axis-based normal-block placement to the common
publication owner; preserve atomic line, float and multicol owners. CSS Writing
Modes 4 §3.2 maps sideways-lr inline-start oppositely to ordinary vertical-lr.
Use the shared logical-to-physical start predicate for this distinction, including
self-alignment; remove the normal-block path's separate bottom-edge override.
The existing surrogate mapping continues to own atomic lines. Do not change
font metrics, normal-flow block progression or child sizing. Ownership contracts
remain **D4.5.1v4 / D4.1.4v5**.

**Finite acceptance.** Extend the existing retained margin control across
horizontal, vertical-lr and vertical-rl child flows, independently of parent and
child direction. Its browser-only principal-mode matrix already confirms 528
states (`temp/wpt-m30-inline-placement-browser.{js,json,log}`). Include sideways
parents in this same geometry control so the shared axis predicate cannot silently
reverse their normal-block alignment. Keep the existing zero/negative/percentage,
auto, overflow and explicit alignment states, parent resizes, sibling progression
and descendant-local checks. Compare the original bordered parent-box family.
Relative positioning and distinct abspos static-anchor defects remain separate.

**Verification budget.** Independent browser matrix and causal native failure,
focused originals, complete native and sizing/position/inline/multicol neighbors,
canonical writing-mode comparison against frozen M29, lint/diff and required
Radiant aggregate once the candidate stabilizes. Retain failed experiments and
preserve fixtures, references, baselines and comparison policy.


**Causal controls / first candidate.** The extended independent Chromium matrix
confirms all 1,056 states, including both sideways parent modes
(`temp/wpt-m30-inline-placement-browser.{js,json,log}`); the earlier 528-state
principal-mode evidence is retained under `temp/wpt-m30-principal-inline-placement-browser.*`.
Before the repair, the native control disagrees on containing-flow y placement,
while child used widths and descendant-local geometry remain correct
(`temp/wpt-m30-causal-before.log`). The first candidate shares M29's margin
solver across normal block children, resolves sideways-lr inline-start in the
existing axis predicate, and retains the separate line/fragmentation owners.
Its geometry matrix and neighbor qualification remain pending.


**First-candidate feedback / publication order.** All 1,056 definite-inline states
and all 302 native cases pass. The writing-mode candidate is unqualified:
1,473 full matches, five gains but two lost matches and six reduced records
(`temp/wpt-m30-first-delta.json`); inline rejects
`initial-letter-float-001-slr`, while sizing, positioning and multicol pass
(`temp/wpt-m30-neighbors.log`). The normal-block predicate incorrectly included
floats, suppressing their line-side publication. Exclude them from the new normal
placement owner, retaining the existing float/line mapping.

The other regressions reveal an earlier automatic-size ordering defect:
`finalize_block_flow` publishes children before assigning the parent's final
height. The slr-047 child's y is 140px too small, exactly its 140px height, because
publication sees only the parent's 40px decorations instead of the measured
180px border box (`temp/wpt-m30-regressions.log`). CSS positioning must consume
the containing used inline extent, including min/max and transferred ratio
constraints, rather than a provisional size. Move the existing publication call
after height finalization and auto-child stretching, before content alignment;
do not duplicate its size calculations or synthesize a replacement height.
Chromium confirms 32 retained automatic/min/max/restored states across all four
parent modes (`temp/wpt-m30-auto-inline-browser.{js,json,log}`); the new native
control records the pre-repair disagreement
(`temp/wpt-m30-auto-causal-before.log`). Keep the failed candidate as evidence;
recheck every reduced score and neighbor after the stage correction.


**Repaired focused and corpus checks.** Publication now follows final used
height, ratio/min/max handling, auto-child stretching and BFC sizing, before
content alignment. Floats retain their previous line-side mapping. All 32
automatic/constrained and 1,056 definite-inline controls pass
(`temp/wpt-m30-used-size-native.log`); the two lost originals return to 100%,
the logical-mapping document returns to its preceding score, and the inline
float original returns to 100%
(`temp/wpt-m30-used-size-{originals,inline}.log`). All **303 native cases** pass
(`temp/wpt-m30-final-native.log`). Sizing **776/776**, positioning **139/139**,
inline **169/169**, and multicol **334 full plus 28 accepted partials** pass
(`temp/wpt-m30-repaired-neighbors.log`).

The repaired canonical audit reports **1,475/1,980**, 505 mismatches, zero errors:
five gained full matches, two partial improvements, no losses or reduced scores
versus qualified M29, with 1,973 records entirely unchanged
(`temp/wpt-writing-m30-repaired.json`, `temp/wpt-m30-repaired-delta.json`).
Dimension-cast lint and diff checks pass. The required aggregate is now running
on this candidate; M30 is not yet qualified. Its current binary SHA-256 is
`ef78b3ac712935692c200d50a87eb19d4c1ce07624c7fc0b9a7e73e34c478c50`.


**M30 qualified.** The required Radiant aggregate exits 0 with **6,526 total,
6,384 full, 140 accepted partials, two skips, zero failures**
(`temp/wpt-m30-required-aggregate.log`). All 303 native cases and the affected
neighbors pass. The exact requested command confirms **1,475/1,980**, 505
mismatches and zero execution errors (`temp/wpt-m30-user-command.log`): five
new full matches and two partial improvements, with no lost or reduced scores
versus M29. Dimension-cast lint and diff checks pass. The qualified binary is
`ef78b3ac712935692c200d50a87eb19d4c1ce07624c7fc0b9a7e73e34c478c50`.
The frozen binary, six changed source files, patch, inventory, delta and gate
evidence are indexed by `temp/wpt-m30-qualified-state.json`; M29 is retained.
No fixture, reference, baseline, harness or tolerance changed. M30 closes its
normal-block inline placement and necessary final-size publication dependency.
The overall goal remains active with 505 mismatches; the next bounded owner is
the deferred static-position anchor for sideways flows.


### Milestone 31: sideways static positions use the deferred logical anchor

**Priority / outcome.** P1. An absolute overlay with auto insets must follow its
hypothetical position inside a sideways component, independently of the outer
positioned containing block's flow. `abs-pos-border-offset-002` still misplaces
sideways-lr RTL descendants by a parent/child width difference. The existing
`positioned_capture_vertical_static_anchor` explicitly rejects sideways modes,
leaving these boxes on a horizontal RTL fallback. M30 already supplies the shared
physical inline-start mapping needed to remove that exclusion.

**Design / standards.** CSS Position 3 [§3.5.2](https://www.w3.org/TR/css-position-3/#static-position-rectangle)
defines the hypothetical static rectangle in the original formatting context;
CSS Writing Modes 4 §3.2 and §7 map it to physical coordinates. Extend the existing
logical anchor capture, line completion/bidi tracking and final projection to
sideways flows. Keep the line-relative cursor/direction until publication;
project both the cursor and selected margin edge through the shared axis mapping.
The parent's block direction chooses physical X independently of inline RTL.
Reuse the existing anchor fields and containing-block offset owner, adding no
allocation or second positioned layout path (**D4.5.1v4 / D4.1.4v5**).

**Finite acceptance.** Original bordered sideways cases; horizontal/principal
vertical/sideways static parents with independent containing-block mode and
direction; asymmetric parent and containing-block decorations; a preceding block
removed/inserted/restored; inherited versus orthogonal positioned writing mode
and retained positioned-box size changes. Extend the existing font-controlled
static-anchor test to sideways inline/empty-line/aligned/indented/block-origin
cases. Its 36 added sideways expectations are independently verified in
`temp/wpt-m31-static-inline-browser.{js,json,log}`; the initial 360-state block
matrix is retained in `temp/wpt-m31-static-block-initial.*` and will be extended
for retained child sizes before implementation. Preserve the existing flex/grid,
fragmentation and explicit self-alignment owners and their admission conditions.

**Verification budget.** Browser and causal native controls, focused original,
full native and position/inline/sizing neighbors, canonical delta versus M30,
lint/diff and required Radiant gate once stable. Trace any reduced score to its
owner; do not replace captures or change comparison policy.


**Causal evidence / candidate.** Chromium confirms the extended 540-state block
matrix, including inherited/orthogonal and resized/restored positioned boxes
(`temp/wpt-m31-static-block-browser.{js,json,log}`). Both new/extended native
controls fail on M30 (`temp/wpt-m31-causal-before.log`). Capture now admits all
vertical-axis modes under the same formatting-context/self-alignment conditions.
The existing final anchor projector derives line-axis reversal from the shared
logical-start predicate and parent direction, then projects the cursor and
selected margin edge together. Cache comments distinguish the retained
line-relative cursor from its eventual physical position; fields and ownership
are unchanged. Focused and corpus qualification are pending.


**First candidate / preceding block axis.** The extended inline/indent/alignment
control passes all 72 cases, including its 36 new sideways expectations, and
`abs-pos-border-offset-002` reaches 100% (`temp/wpt-m31-first-native.log`,
`temp/wpt-m31-first-originals.log`). The 540-state block control still exposes a
shared prior defect after a non-square sibling: a 13×11px box advances the
captured static block distance by its surrogate 11px height instead of its used
13px vertical block size. Existing 40×40px predecessor controls could not expose
this disagreement. The original `abs-pos-border-offset-003` has the same cause
with a 10×20px predecessor. Preserve the strict failure rather than fitting an
offset or weakening the control.

Reuse the existing vertical flow-extent traversal with an optional stopping
child and prefix adjustment result. It already owns physical child block sizes
and collapsed block margins. Compare that completed normal-block prefix with
its surrogate end (excluding visual relative movement), retaining any subsequent
inline advance. Anchor capture consumes this adjustment. No third independent
block-sequence walk or size/margin solver is added; ordinary extent callers keep
their complete traversal. Mixed atomic-prefix handling retains its existing
owner rather than applying a normal-block correction to line-owned geometry.
Focused controls and corpus qualification must be repeated on this candidate.


**Focused and corpus checks.** Both static-anchor controls now pass: 540 retained
block states and 72 inline/indent/alignment cases. The prefix query stops at the
anchor's direct flow owner when nested in an inline wrapper, so future siblings
cannot enter its hypothetical position. Both original bordered documents reach
100% (`temp/wpt-m31-prefix-{native,originals}.log`). All **304 native cases** pass
(`temp/wpt-m31-full-native.log`), as do positioning **139/139**, inline **169/169**
and sizing **776/776** (`temp/wpt-m31-neighbors.log`). The canonical audit reaches
**1,479/1,980**, 501 mismatches, zero execution errors: four gained full matches,
no lost or reduced scores, and 1,976 records entirely unchanged versus M30
(`temp/wpt-writing-m31-first.json`, `temp/wpt-m31-first-delta.json`). The other
two gains are `margin-collapse-vlr-023` and `margin-collapse-vrl-022`, which use
the same normal-block static position. Dimension-cast lint and diff checks pass.
The required Radiant aggregate is running on binary
`ee0212aeb730b1f8ad6c4ca5535f91581688a3ef5e16686d3b9eaba91015127b`;
M31 remains unqualified until that result is recorded. No corpus or harness
policy changes were made.


**M31 qualified.** The required Radiant aggregate exits 0 with **6,527 total,
6,385 full, 140 accepted partials, two skips, zero failures**
(`temp/wpt-m31-required-aggregate.log`). All 304 native cases and the affected
position/inline/sizing categories pass. The exact requested command confirms
**1,479/1,980**, 501 mismatches and zero execution errors
(`temp/wpt-m31-user-command.log`). The canonical delta is four gained full
matches, no lost or reduced scores versus M30, and 1,976 unchanged records.
Across M29–M31 this continuation gains 17 full matches with no reduced scores.
Dimension-cast lint and diff checks pass. The qualified binary is
`ee0212aeb730b1f8ad6c4ca5535f91581688a3ef5e16686d3b9eaba91015127b`.
The frozen binary, eight changed source files, patch, inventory, delta and gate
evidence are indexed by `temp/wpt-m31-qualified-state.json`; M29 and M30 remain
available. No fixture, raw capture, accepted baseline, tolerance or harness was
changed. M31 closes its sideways deferred-anchor and completed-block-prefix
scope. The overall goal remains active and incomplete.

**Next bounded milestone / M32 diagnosis.** Prioritize fixed-size float placement
in vertical BFCs with a definite inline constraint. In the original
`float-contiguous-vlr-003`, the four 100px images are placed in one horizontal
row and the parent publishes a zero block size, instead of two vertical pairs
and a 200px used block extent (`temp/wpt-m29-triage.log`). The shared float search
and `block_context_add_float` currently consume physical width/height, while
vertical publication treats floats as an ordinary block sequence. Trace and
repair the coordinate contract between the existing float search, BFC exclusion
records, BFC used block extent and physical publication; do not add a second
float-packing algorithm. CSS Writing Modes 3 §7.1/§7.4 maps float sides to line
left/right and CSS2 §9.5.1 supplies the packing/clearance rules. M31's shared
normal-block prefix query can supply a preceding in-flow block position without
copying its size/margin walk.

Before implementation, confirm a reduced browser/native matrix with horizontal
controls, both principal vertical block directions, left/right/mixed sides,
non-square fixed boxes, asymmetric/negative margins, definite inline resizes and
restoration, and BFC automatic block extents. Include the original contiguous
image pair in each vertical direction. Keep nested/fragmented and intrinsic
float sizing as explicitly separate residue unless a focused control identifies
a necessary shared prerequisite. Existing text/initial-letter wrapping must
remain protected by native and inline neighbors. Budget focused controls,
float-related neighboring categories, the canonical audit, dimension lint/diff
and one required Radiant aggregate at this boundary. No M32 source change or
qualification has started; continue from the M31 checkpoint and retain all 501
mismatches as failures.


### Milestone 32: BFC float placement shares one logical coordinate contract

**Priority / outcome.** P1. Fixed-size floated components and image groups in
vertical flows must pack along the line and advance in the block direction;
BFC automatic block sizes must contain their margin boxes. The M31 binary matches its frozen checkpoint; its eight source files remain
archived for comparison. Start from 1,479/1,980,
501 mismatches. Current diagnosis confirms physical width in float fitting,
physical height in BFC exclusion records, and ordinary-block placement during
vertical publication. These stages disagree on their coordinate contract.

**Design / standards.** CSS Writing Modes 3 [§7.1](https://www.w3.org/TR/css-writing-modes-3/#vertical-layout)
and §7.4 map float sides to line-left/right and dimensions to inline/block;
CSS2 [§9.5.1](https://www.w3.org/TR/CSS22/visuren.html#float-position) supplies the
shared packing constraints. CSS2 [§10.6.7](https://www.w3.org/TR/CSS22/visudet.html#root-height)
includes contained floats in an automatic BFC block size. Keep the existing
search and float lists. Project each box's size and margins into their line/
block frame at the boundary, use that same frame for records and queries, and
publish physical coordinates once after the containing used size is finalized.
Floats cannot enter the normal-block cursor or margin-collapse sequence.
Resolve final BFC block extent on the block axis; do not enlarge the vertical
inline size using a logical float-bottom value. Reuse M31's prefix correction
where a prior normal block supplies the source cursor. No allocation or view
ownership changes (**D4.5.1v4 / D4.1.4v5**).

**Finite acceptance.** Existing contiguous-image originals in both vertical
block directions. A browser/native matrix covers horizontal and four vertical/
sideways parent flows, LTR/RTL, left/right/mixed sides, non-square boxes,
asymmetric decorations and signed logical margins, a prior normal block,
inline resize/restoration, and automatic versus definite BFC block sizes.
Independent text/initial-letter wrapping remains protected by existing native,
inline and baseline checks. Nested/fragmented/intrinsically sized combinations
remain separate residue unless they expose a necessary shared prerequisite.

**Verification budget.** Confirm browser model and causal native failures
before implementation. Use focused originals/matrix while editing; once stable,
run full native and relevant float/inline/sizing/position neighbors, canonical
writing-mode delta, dimension lint/diff and one required Radiant gate. Preserve
all original fixture/reference/baseline/comparison policy.


**Causal matrix / first implementation.** Chromium independently confirms all
300 retained states (`temp/wpt-m32-floats-browser.{js,json,log}`). The native
control fails only in vertical/sideways flows; every horizontal state passes
(`temp/wpt-m32-causal-before.log`). A shared `LayoutFloatMetrics` value projects
physical dimensions/margins to line-left/right and block-start/end for fitting
and BFC registration. The line-left direction helper reuses the existing logical
start predicate, including sideways-lr, and replaces M31's equivalent inline
reversal calculation. Normal-flow classification excludes floats. Their own
publication reuses `layout_map_vertical_box`, with no new packing or physical
projection algorithm. BFC automatic vertical block extent consumes the logical
float-bottom bound; horizontal height-expansion paths no longer consume that
bound as a physical height in vertical flows. The candidate is building before
its first focused qualification. Raw originals remain recorded in
`temp/wpt-m32-original-before.log`.


**First focused result / computed logical sides.** Both original contiguous-image
cases reach 100% (`temp/wpt-m32-first-originals.log`). The 300-state native
control now passes all horizontal and principal vertical cases; only sideways-lr
inline positions remain, offset by the difference between the authored start/end
margins (2px or 11px; `temp/wpt-m32-first-native.log`). The float projection
correctly selects physical bottom as line-left, but the CSS logical-property
resolver had mapped `margin-inline-start` onto physical top. CSS Writing Modes 4
§3.2/§6.4 requires the opposite inline direction in sideways-lr. Correct
`layout_element_logical_sides` and make the computed-property box-side consumers
use that same side selection, including the existing spacing/inset/border paths.
Do not change float placement or introduce compensating offsets. No ownership
or language ruling changes. Focused and neighboring qualification remain pending.


**Qualification found shared prerequisites.** The first canonical candidate has
18 gains but loses two prior full matches and reduces ten other records; the
inline neighbor loses `initial-letter-block-position-margins-slr`. M32 remains
unqualified. Clearance is resolved before publication, but normal vertical
publication reconstructs the block cursor without that lower bound. Floated
clearance also depended on an optional boundary allocation, before float fitting
replaced the source position. CSS2 §9.5.2 distinguishes a normal block's border
edge from a float's outer edge. Make float placement own clearance and packing
in one BFC frame, and share a clearance-aware normal block offset between the
extent walk and publication. Advance the normal cursor from that used offset.

The computed sideways-lr margin correction exposes an older inline-span edge
consumer that still interprets RTL alone as physical-bottom start. Extract the
shared writing-mode line-axis reversal predicate and use it for logical sides,
block axis starts, and inline-span edge consumption. Initial-letter physical
inset projection already agrees with the standard and must stay unchanged.
These are necessary prerequisites, not a broader initial-letter or nested-BFC
feature milestone. The new 60-state browser control confirms clear/reset for
normal and floated successors, with/without asymmetric block margins in all
five flows (`temp/wpt-m32-clear-browser.{js,json,log}`). Its causal native run
fails 34 assertions before repair, exclusively in vertical/sideways cases
(`temp/wpt-m32-clear-causal-before.log`). Keep horizontal behavior and retained
reset as acceptance cases. The extra shapes category has no stored baseline;
its raw failures cannot be called regressions. Compare it directly with the
frozen M31 executable instead of changing harness classification.


**Stable candidate / remaining scope.** The 300 packing states and 60 clearance
states pass; full native text/layout is 306/306. The raw canonical audit is
1,504/1,980, 476 mismatches, zero execution errors: 25 full-match gains, zero
losses or reduced element/span/text records against M31
(`temp/wpt-writing-m32-clear.json`, `temp/wpt-m32-clear-delta.json`). Inline
169/169, position 139/139, sizing 776/776 and multicol 334 full plus 28 accepted
partial cases pass (`temp/wpt-m32-clear-neighbors.log`). Dimension lint/diff pass.
The exact user command and required aggregate are still being qualified.

The direct shapes comparison retains 80/294 full matches and zero execution
errors, with 17 lower partial scores and five improved partial records; no full
matches are lost (`temp/wpt-m32-shapes-delta.json`). These cases use unsupported
`shape-outside` contours, documented in `doc/HTML_CSS_SVG_Support.md` §7. Chromium
with `shape-outside:none` independently confirms the candidate's affected parent
and float rectangles in all 17 cases
(`temp/wpt-m32-shapes-fallback-confirmation.json`). The prior apparent parent or
float matches arose from treating a float as a normal block and computing an
incorrect inline-content block extent. Keep the correct rectangle layout and
retain contour wrapping as an explicit unsupported feature. Do not fit the
container width or float position to contour-aware references. Their failing
scores remain visible and are not an accepted baseline.

Float placement/clearance and definite inline extents are the finite M32 core.
Known residue includes BFC/replaced boxes avoiding floats in a vertical parent,
nested shared-BFC coordinate conversion, multicol fragmentation and intrinsic
float sizing. The alongside originals recover their M31 partial scores after
the clear fix but remain failures: the BFC avoidance consumer still treats
physical width as the available inline extent, applies physical-x shifts, and
vertical publication replaces them. This is a separate stage, suitable for a
finite next milestone; shape-contour support is independent and deferred.


**M32 qualified.** The required Radiant aggregate exits 0: **6,529 total,
6,387 full, 140 accepted partials, two skips, zero failures**
(`temp/wpt-m32-required-aggregate.log`). The exact requested command confirms
**1,504/1,980**, 476 mismatches and zero execution errors
(`temp/wpt-m32-user-command.log`). The canonical delta against M31 is 25 gained
full matches, zero lost/reduced writing-mode scores and 1,940 unchanged records.
Both new native controls and all 306 native cases pass. Relevant inline,
position, sizing and multicol neighbors pass; all 17 reduced partial Shapes
records are accounted for by independently verified rectangular fallback, with
contour wrapping explicitly unsupported. No fixture, reference capture, baseline,
comparison tolerance or harness policy changed. Dimension lint/diff pass.
The binary remains
`8d1eb54b9918f6891bed215d579e416742debd4cb9279765b6efe6c39aec649d`.
The checkpoint manifest `temp/wpt-m32-qualified-state.json` indexes the binary,
source archive, patch and evidence. M32 closes fixed-size float packing,
clearance and BFC containment with a definite containing inline constraint.
The overall goal remains active and incomplete.

**Next finite milestone / M33 plan.** P1: a normal BFC or block-level replaced
box alongside floats must use its containing flow's inline/block frame. This
completes an everyday float/card interaction before independent shape-contour
or fragmented/intrinsic float features. Original acceptance cases include
`ortho-htb-alongside-vrl-floats-002` and `-006`, plus their simpler horizontal
controls: select the earliest available block position, honor the required
inline border box, avoid float margin boxes, and publish the final inline
shift/block advance without replacing them during vertical publication.
CSS2 §9.5 and CSS Writing Modes 3 §7.1 govern these constraints.

Trace `should_avoid_floats` / `find_block_float_fit`, width reduction, used size
resolution and `layout_publish_vertical_children`. Current evidence shows
physical width/height queries and x shifts in a consumer that now receives
logical float records. Repair that boundary using existing axis/box helpers;
keep one search algorithm and preserve horizontal behavior. Before editing,
confirm a reduced browser/native control with definite non-square BFC boxes,
left/right floats, a box fitting beside a float versus one forced past it,
both principal vertical directions and a horizontal control, asymmetric
margins/decorations, and retained resize/restoration. Add orthogonal/sideways
controls only where the identified owner requires them. Verify focused
originals, native controls, affected inline/position/sizing neighbors, the
canonical writing-mode delta, lint/diff and one required aggregate at the next
milestone boundary. Nested BFC offset conversion, contours, fragmentation and
intrinsic sizing remain separate unless a causal control proves a prerequisite.
No M33 source change has started; resume from the M32 checkpoint.


### Milestone 33: block float avoidance consumes the containing flow axes

**Design / causal evidence.** M32's binary and all twelve checkpoint files match
its manifest. Chromium confirms all 144 retained states in a closed-form control
of horizontal/vertical-lr/vertical-rl parents, LTR/RTL, left/right floats,
parallel/orthogonal flow-root children, asymmetric decorations/margins and a
fitting/forced-past/restored inline size. The native causal run has 120 assertion
failures: 112 vertical and eight horizontal
(`temp/wpt-m33-avoid-browser.json`, `temp/wpt-m33-causal-before.log`).

The horizontal prerequisite is CSS2 §9.5's border-box exclusion constraint:
a BFC border box must avoid float margin boxes; its own margins can occupy the
excluded region. The current fit result adds an entire intrusion to the normal
margin, and its width reduction likewise consumes margins twice. Express the
available margin-box interval by expanding the free border interval by the box's
own non-auto margins, clamped to the containing interval. Use that same interval
for fit, automatic-size reduction and normal-flow margin resolution. Keep the
existing stepped search; consolidate the duplicate auto/definite fit paths.

Select the containing flow's inline and block physical axes through existing
`LayoutAxisRefs`, box-size conversion and `LayoutFloatMetrics`. Resolve definite
sizes/percentages and decorations on the selected axes before querying. Retain
logical block shift and inline exclusion result as transient BlockProp state;
normal vertical extent/publication consumes them once, including RTL margin
resolution. Existing source-prefix correction supplies prior normal content.
Use the same query and margin equation for horizontal and vertical consumers;
do not introduce a second search or final-coordinate corrections. The view and
property owners remain unchanged (**D4.5.1v4 / D4.1.4v5**).

The original plan's finite acceptance and verification budget still apply.
Intrinsically sized replaced/fragmented/nested combinations stay explicit
residue unless a control proves them necessary. No M33 candidate is qualified.


**First result / necessary prerequisites.** The first 144-state control passes,
including the eight horizontal margin failures. The acceptance matrix now also
covers both sideways modes and definite-size block-level canvas replacements:
all 360 browser-derived and native states pass. The alongside originals' BFC
rectangles are corrected, but root bounds still fail. Two reduced controls
identify eight root extent failures and eight changed-sideways BFC failures
(`temp/wpt-m33-expanded-before.log`). CSS Writing Modes 4 §3.2 requires an
independent BFC for a different CSS writing-mode value, even when normalized
block directions coincide. Extract the existing principal-flow source selection
so BFC detection preserves distinct CSS values while retaining root/body used
value propagation (§8). Computed inheritance/property mapping stays separate.

Root finalization's single-body shortcut discards float containment. In the
original `-006`, descendant float records end at 724px in the provisional source
frame, but the resolved first-child/body block margin is 16px rather than the
source 8px, giving the browser's 732px root extent. This is a necessary shared-BFC
publication prerequisite. Centralize BFC source offsets in the existing
`block_context_offset_to_bfc`: invert published vertical rectangles before
accumulating each ancestor, retain source coordinates for unpublished parents,
and keep inline wrappers out of the block-offset sum. Refresh the float records
once before the single-body root adjustment, and share the ordinary BFC float
block-extent calculation with that shortcut. The root then anchors the body
using its final size as before; no fitted offset or duplicate float-bound walk.
The 10 reduced root states independently confirm root containment while the
body remains sized only by its normal-flow content. Browser evidence includes
the three original root/child rectangles (`temp/wpt-m33-root-browser.json`).
Nested/fragmented behavior outside this prerequisite remains partial.


**First audit / zero-size interval prerequisite.** All 309 native cases pass.
The canonical corpus reaches 1,521/1,980: 17 full-match gains, zero lost or
reduced writing-mode records, zero execution errors
(`temp/wpt-m33-first-delta.json`). Both original orthogonal cases and the
sideways alongside originals now fully match. Inline/position/multicol
neighbors pass; two sizing cases and one display case regress. The display neighbor exposes `display-flow-root-002`: treating
zero width as definite (correctly) reaches an old fit-query clamp that changes
negative free intervals into zero. A zero-size box must still have a non-crossing
interval for its full block-axis span. Preserve the signed interval in the
existing fit comparison; do not revert zero-size definiteness or special-case
that fixture. Add a retained 200/80/200px block-span control over two staggered,
opposite-side floats to challenge this distinction. M33 remains unqualified.


**Stretch neighbor prerequisite.** The full neighbor report also contains two
sizing regressions, `stretch/{bfc,replaced}-next-to-float-2`; the early progress
statement omitted them and is corrected above. The normal fixed-size control
passes and Shapes has 294 unchanged records versus M32, but a 10px margin in
the stretch path is counted twice. The new free margin-box interval already
incorporates leading/trailing margins; the old `stretch_fit_leading_margin`
addition must be removed from stretch/minmax sizing. Preserve that one interval
as the sole available-space owner, rather than adding a compensating special
case. A Chromium-checked retained control covers div/canvas, LTR/RTL,
stretch/min-stretch/max-stretch, and 0/+10/-10/restored leading margins. M33
remains unqualified until both required neighbors and the aggregate pass.


**Stable candidate / qualification running.** The signed interval is retained
both by the BFC query and by the fit comparison, so crossed exclusions remain
distinct from an actual zero-width interval. The new zero-size control fails
before repair and passes afterward. The stretch margin control fails 36
assertions before removal of the duplicate compensation and passes all 48
Chromium-checked retained states afterward. All five new native cases pass,
including 360 definite-size states, 12 changed-mode states and 10 root states.
Full native is **311/311** (`temp/wpt-m33-final-native.log`).

The final canonical audit is **1,521/1,980**, 459 mismatches, zero errors:
17 full-match gains, zero losses or reduced records, and 1,959 unchanged records
versus M32 (`temp/wpt-writing-m33-final.json`, `temp/wpt-m33-final-delta.json`).
Inline **169/169**, position **139/139**, sizing **776/776**, display **145/145**
and multicol **334 full + 28 accepted partial** all pass
(`temp/wpt-m33-final-neighbors.log`). The Shapes direct comparison has **294
unchanged records**, including its existing unsupported-contour failures
(`temp/wpt-m33-final-shapes-delta.json`). Dimension lint/diff pass. The required
Radiant aggregate is running on
`68c0af8e65e7cda9f5b089aefc1c8cea9a3f5efdd922b406ae15ecc3e46cebfe`.
M33 remains unqualified until that gate finishes. No corpus, reference,
baseline, tolerance or comparison policy was changed.


**Next-stage evidence collected during the gate.** A frozen copy of the final
candidate (same SHA-256) isolates the next clear/avoidance state-ordering issue.
Chromium confirms 60 retained none/both/none states in five flows, both
paragraph directions and both float sides (`temp/wpt-m34-clear-browser.json`).
The CLI snapshot probe has 40 unique initial states: 30 match and ten fail,
exclusively `clear:both` (`temp/wpt-m34-clear-causal.json`). Eight failures are
vertical/sideways inline offsets that still include the pre-clear exclusion;
two are horizontal RTL positions resolved against pre-clear available space.
The existing clear branch updates physical-x offsets conditionally and never
refreshes the stored vertical exclusion state or recomputes RTL margins.
These probes are diagnosis only: native retained-reflow qualification and M34
implementation have not started. Keep the current M33 finite result separate
from this explicit remaining interaction.


**M33 qualified.** The required Radiant aggregate exits 0 with **6,534 total,
6,392 full, 140 accepted partials, two skips, zero failures**
(`temp/wpt-m33-required-aggregate.log`). All 311 native cases pass. The exact
requested command confirms **1,521/1,980**, 459 mismatches and zero execution
errors (`temp/wpt-m33-user-command.log`). The canonical M32 delta is 17 gained
full matches, zero lost or reduced scores and 1,959 unchanged records. All
selected inline/position/sizing/multicol/display neighbors pass. Shapes has
294 unchanged records. Dimension lint/diff pass. The binary remains
`68c0af8e65e7cda9f5b089aefc1c8cea9a3f5efdd922b406ae15ecc3e46cebfe`.
The checkpoint `temp/wpt-m33-qualified-state.json` indexes the frozen executable,
fourteen changed source/document files, patch, audits and gate evidence. No
fixture, reference, accepted baseline, tolerance or harness policy changed.

M33 closes definite-size BFC/replaced float avoidance on the containing axes,
shared margin/exclusion interval ownership, exact-value sideways BFC isolation,
and single-body root float containment after publication. It preserves
horizontal behavior and verifies signed-margin stretch consumers and zero-size
interval distinctions. The wider feature remains partial, particularly the
clearance interaction below, general nested coordinate conversion, automatic/
intrinsic sizing, contours and fragmentation. The overall goal stays active.

**Next finite milestone / M34 plan.** P1: resolve combined `clear` and BFC float
avoidance from the same final block position and available inline interval.
This closes a common clearfix/component interaction in the selected float core
before moving to independent features. The frozen-M33 diagnosis has ten static
mismatches out of 40 unique cases; all 60 retained browser states pass. Add a
native retained none/both/none control before production edits, with horizontal,
principal vertical and sideways flows, LTR/RTL and both float sides. Include
selective clear-left/right controls so unrelated float sides cannot introduce
clearance, and asymmetric margins or an automatic inline-size case where it
challenges the same available-space owner.

Trace the pre-layout `find_block_float_fit` result, normal clearance branch,
RTL margin resolution, transient avoidance fields and vertical publication.
Clearance changes the candidate block position; exclusion offsets, automatic
inline-size constraints and normal margins must be recomputed against space
there. Reuse one shared interval/search and one margin equation. Remove stale
state rather than offsetting final coordinates. Preserve CSS2 §9.5.2 clearance
and margin-collapse semantics and CSS Writing Modes line/block mappings;
existing ownership rulings remain **D4.5.1v4 / D4.1.4v5**. Do not conflate
clearance with an arbitrary BFC shift in margin-collapse bookkeeping.

Scope the milestone to that shared ordering/state repair, with the existing
normal/float clearance controls and originals as regressions. Keep intrinsic,
fragmented and general nested conversions separate unless a causal case proves
a prerequisite. Verification budget: focused native/browser controls and
originals, affected inline/position/sizing/display neighbors, full native and
canonical writing-mode delta, dimension lint/diff and one required Radiant
aggregate at the milestone boundary. M34 production/native test changes have
not started; resume from the M33 checkpoint and preserve all 459 failures.


### Milestone 34: clearance and avoidance share the final placement interval

**Design / evidence.** The M33 binary and all fourteen checkpoint files match
the manifest. The expanded native control reproduces the stale-state defect;
Chromium independently confirms **400 retained states** across five flows,
LTR/RTL, both float sides, selective clear-left/right/both and restoration,
fixed/automatic inline sizes, asymmetric margins, and a box already forced
past floats without clear (`temp/wpt-m34-clear-expanded-browser.json`,
`temp/wpt-m34-causal-before.log`). The wide case protects CSS2 §9.5.2's
hypothetical clear:none position: being below the relevant float already does
not introduce clearance.

Resolve the hypothetical BFC fit with the existing search. If its border edge
requires clearance, query the same search again with the clear lower bound.
Only the final fit supplies inline exclusion, width/height reduction and normal
margin resolution. Record actual clearance separately from the resulting
block-axis displacement; preserve `saved_clear_y` and `has_clearance` for margin
collapse. A transient phase marker identifies clearance already resolved in
that placement frame so vertical publication does not reinterpret surrogate
physical-y margins as a second lower bound. Ordinary non-BFC clear remains
owned by the existing normal-flow rule. Remove the late physical-x/width
reconciliation paths, whose partial updates caused the stale geometry.

Centralize the repeated active-clear side predicate before adding another
caller. Reuse the existing fit, BFC offsets, axis/margin helpers, automatic-size
constraints and publication. Do not add fixture-specific offsets, separate
packing or a second margin equation. Selected view/property ownership remains
**D4.5.1v4 / D4.1.4v5**. The preceding M34 finite plan and gate budget apply;
production implementation and qualification follow this causal result.


**Stable candidate / qualification running.** The original 400-state control
fails 88 assertions before repair and passes afterward. The final control adds
block-level replacements: all **500 browser/native retained states** pass.
A real Ahem paragraph also verifies that the expanded available inline measure
changes wrapping from two lines to one before child layout, then restores it;
all six browser/native states pass. Existing float and normal-clear controls
pass; full native is **313/313** (`temp/wpt-m34-full-native.log`). The phase
marker prevents already-resolved BFC clearance from being reconstructed from
surrogate physical-y margins; actual `has_clearance` remains distinct and is
false for the already-lowered hypothetical wide case. The repeated active-clear
predicate is shared; the obsolete late physical offset/width patches and their
private query helper are removed.

The canonical writing-mode audit retains **1,521/1,980**, 459 mismatches,
zero errors and **1,980 unchanged records** against M33
(`temp/wpt-m34-first-delta.json`). This milestone fixes a shared confirmed
clearance/avoidance defect without claiming new full WPT matches. Inline
169/169, position 139/139, sizing 776/776, display 145/145 and multicol
334 full + 28 accepted partial all pass (`temp/wpt-m34-neighbors.log`). Shapes
has 294 unchanged records. The empty collapsing-through
`clearance-calculations-vrl-006` retains its previous failure and is independent
margin-chain residue. Lint/diff pass. The required Radiant aggregate is running
on `d0f284302c72578173ff424f660ff3c50500a3bb2bab003d7b67cb4b39843ab1`.
M34 remains unqualified until the gate finishes; the full goal is incomplete.

**Next feature diagnosis.** Prioritize usable text-combine-upright for compact
numbers/dates in vertical prose, rather than extending the isolated empty-margin
case. The 56 remaining text-combine records include substantial ordinary-layout
prerequisites: both `text-combine-upright-compression-001` and its fake-combine
reference have 146px vertical inline extent where Chromium has 74px, so root,
body and paragraph bounds fail even without actual text-combine. The reference
uses a definite 72×72px horizontal inline-block in an indefinite-inline vertical
paragraph. The glyph-combination case additionally reports a 91px physical
block extent for a glyph whose horizontal width is 43.2px. Trace auto inline
measurement/atomic line publication before the combination shaping/paint owner;
do not fit a special combined box over an incorrect parent extent. Initial
triage is `temp/wpt-m35-triage.log`. M35 implementation has not started.


**Selective opposite-side check.** An additional browser control has short left
and tall right floats with an automatic-inline BFC. Clearing only the left
float retains the taller right float's exclusion; clearing right/both removes
it. All 25 browser retained states and 20 unique native CLI snapshots match
(`temp/wpt-m34-selective-{browser,candidate}.json`). The snapshot runner uses a
frozen copy of the same candidate SHA while the required gate runs, without
replacing its executable. This independently rejects an implementation that
merely zeros every intrusion after any clear. The two new native tests and
other milestone evidence are unchanged.


**M34 qualified.** The required Radiant aggregate exits 0 with **6,536 total,
6,394 full, 140 accepted partials, two skips, zero failures**
(`temp/wpt-m34-required-aggregate.log`). Full native is **313/313**. The exact
requested command confirms **1,521/1,980**, 459 mismatches, zero errors
(`temp/wpt-m34-user-command.log`); all 1,980 canonical records are unchanged
versus M33. All selected inline/position/sizing/display/multicol neighbors pass,
and Shapes retains 294 unchanged records. Dimension lint/diff pass. The binary
is `d0f284302c72578173ff424f660ff3c50500a3bb2bab003d7b67cb4b39843ab1`.
The manifest `temp/wpt-m34-qualified-state.json` indexes the executable,
fourteen source/document files, patch, controls and gate evidence. No fixture,
reference, accepted baseline, comparison policy or tolerance was changed.

M34 closes combined normal clearance/BFC avoidance interval ownership for the
selected fixed/automatic-inline/replaced cases, retained reflow and real text
wrapping, while preserving actual-clearance margin bookkeeping and opposite-side
exclusions. It repairs a confirmed shared defect rather than claiming additional
full WPT matches. Empty collapsing-through margins, general nested conversion,
intrinsic and fragmented sizing remain partial. The full goal stays active
with all 459 failures visible.

**Next finite milestone / M35 plan.** P1: vertical prose containing horizontal
atomic runs must derive its automatic inline extent from the used inline box,
without including unrelated block progression or margins. This is an ordinary
layout prerequisite for compact text-combine numbers/dates, so finish it before
compression/decoration residue. Named acceptance includes the fake-combine
`text-combine-upright-compression-001-ref` and a reduced 1em horizontal inline
block in an indefinite-inline vertical paragraph. Current evidence shows a
correct 72px child and 146px parent inline extent where Chromium has 74px;
the parent has only left/right 72px margins and zero top/bottom margins, so the
extra inline extent cannot be attributed to authored inline spacing. The two
reference paragraphs both expose it; the real combination has an additional
independent glyph/shaping discrepancy.

Before editing, verify a reduced browser/native control across vertical-lr/rl
and a horizontal control, independent font/line-height and block-margin changes,
a non-square definite atomic box, and retained size/mode restoration. Trace
intrinsic inline measurement, surrogate line cursor/max-width accumulation,
`layout_compute_vertical_in_flow_child_inline_extent`, stretching and final
publication; diagnose the first disagreement instead of forcing a parent size.
Reuse existing sizing/axis helpers. CSS Writing Modes §7.3 and CSS Inline's
atomic-inline line sizing govern the prerequisite; text-combine-upright §9.1
continues to govern the next composition stage. Keep allocation ownership
**D4.5.1v4 / D4.1.4v5** unchanged.

Scope M35 to that shared parent/atomic extent repair with explicit border boxes
and an indefinite containing inline measure. Combined-run horizontal shaping,
compression, decorations, parser/CSSOM reflection and fragmentation remain
separate until their owner is reached. Verification budget: causal browser/
native control, the named reference/original pair, affected native and inline/
sizing/position neighbors, canonical writing-mode delta, dimension lint/diff,
and one required Radiant gate. No M35 production/native test change has started;
resume from M34 and keep the overall goal intact.


### Milestone 35: automatic vertical inline size uses inline contributions

**Causal design.** M34's executable and all fourteen checkpoint files match
the manifest. Chromium confirms 90 retained states: horizontal/vertical-lr/
vertical-rl, LTR/RTL, independent 0/7/20px block margins, square/non-square
atomic boxes, font changes, writing-mode changes and restoration
(`temp/wpt-m35-atomic-browser.json`). The frozen M34 snapshot control matches
32/72 initial states. Increasing a block margin by 7px or 20px increases the
vertical parent inline size by exactly that amount, while the atomic box and
block progression remain correct. The retained native control reproduces the
cause (`temp/wpt-m35-causal-before.log`).

`layout_compute_vertical_in_flow_child_inline_extent` treats a normal child's
source x as an inline offset, although that value includes a physical block-axis
margin. The resulting oversized parent then stretches otherwise correct normal
children. Use each normal child's inline margin-box contribution; source x
belongs only to atomic inline placement in this traversal. Do not subtract a
font size, margin constant, or fixture-specific amount.

A separate necessary RTL prerequisite appears even at zero block margins:
the auto-sized paragraph gets the right inline extent but leaves its atom at a
negative inline coordinate. The deferred-alignment gate mistakes any measured
atomic child for a normal block child. Classify actual normal block flow with
the existing helper, then run the existing deferred alignment against the final
inline extent. Its collected atomic sizes/exclusions must use the source line/
block frame when called before vertical publication; existing horizontal and
post-publication consumers retain their default frame. Parameterize that shared
collector and reuse `LayoutFloatMetrics` for box/exclusion dimensions rather
than creating another alignment algorithm. CSS Writing Modes §7 and CSS Inline
line sizing govern the repair. Ownership stays **D4.5.1v4 / D4.1.4v5**.

The preceding finite M35 acceptance and verification budget apply. Composition
shaping/compression/decorations remain independent. No M35 candidate is yet
qualified; preserve all upstream references and comparison policy.


**First focused result / native probe frame.** The named fake-combine reference
now fully matches. The real combination's element geometry also reaches 100%,
leaving its independent glyph width/position discrepancy
(`temp/wpt-m35-first-originals.log`). The existing bidi/atomic regression passes.
The first new native control run incorrectly compares the atom's immediate-
paragraph coordinates with browser coordinates relative to the outer container.
Correct the actual probe to use `view_geometry_ancestor_offset(child,result)`;
keep every browser-derived expected value and assertion intact. This test-frame
correction is necessary before attributing remaining assertions to production
layout. The frozen initial snapshot control already uses matching outer-container
coordinates and independently proves the margin-dependent extent defect.

**Qualification prerequisite.** The first inventory has 27 gains, no lost full
matches, and ten reduced partial records. Two `digits` cases expose Chromium's
unsupported value: independent `CSS.supports`/computed-style probes reject
`digits 2/3/4` and compute `none`, while equivalent `all` controls occupy the
one-em inline measure required for these short runs by CSS Writing Modes 4
§9.1 (`temp/wpt-m35-digits-browser.json`). Keep raw scores and digits support.

The other eight records are ordinary vertical form pages. Their ancestors had
accidentally benefited from the removed block-margin contribution. A plain
inline wrapper containing text and a non-square inline block isolates a shared
publication defect: its atom keeps source x/y while text and wrapper are mapped;
the wrapper also bounds the physical width instead of the source inline size.
Fix source-frame bound measurement and publish nested atomic positions with the
existing vertical box mapper, without entering each atom's formatting context.
Extend the retained M35 sizing control with this wrapper/text prefix; preserve
horizontal controls, used box dimensions, bidi order, and mode restoration.

The form intrinsic stage has a separate axis prerequisite: text inputs project
logical intrinsic metrics, while textarea/select/input buttons do not. Before
editing it, establish independent horizontal/vertical transposition controls,
authored physical dimensions, and retained reflow. Centralize logical-to-physical
intrinsic projection before CSS used-size resolution and map UA button padding
as logical pairs; do not rotate authored physical declarations. Scope this
prerequisite to those existing control classes and UA button spacing. Other
widget interaction/paint and native-font fidelity stay partial. CSS Writing
Modes §7.2 and HTML rendering §15.5.2–4/§15.5.16–17 govern these consumers;
ownership remains **D4.5.1v4 / D4.1.4v5**. Add the form native coverage and form
layout neighbors to the existing verification budget before the required gate.

The nested retained control independently confirms 90 browser states. Its first
native probe must sum block ancestors only: ordinary inline wrapper coordinates
already share their block frame. Correcting the probe does not alter expected
geometry. RTL also exposes `layout_inline_content_measure` treating a provisional
automatic physical height as a definite wrapping constraint and otherwise using
physical-width intrinsics as an inline measure. Remove this direction-specific
constraint for indefinite auto sizes; ordinary available-space layout measures
the full line and the existing final deferred alignment positions RTL content.
Keep actual definite, parent-assigned, ratio and multicol constraints intact.

**Form metric and terminal-space ownership.** The independent browser control
confirms that six text/row-based control classes transpose their automatic
metrics across all four vertical writing modes; a 71×37px physical border box
does not transpose, and restoration returns the natural size. The native
retained control fails for textarea/select/input buttons before projection and
passes after central projection and logical UA padding. Select theme metrics
are border-box metrics: resolve them directly when auto instead of installing
them into the CSS `given_width/given_height` slots before projection. Definite
CSS/containment and the preferred-ratio transfer retain their existing owner.
Button authored-box detection compares the used logical block decoration.

The original label then isolates another shared line-state cause: the U+3000
before an atomic control remains in `Linebox::hanging_space` and is subtracted at
line finalization, although the control makes it internal whitespace. A plain
wrapped atom following an Ahem preserved ASCII space isolates this without
platform fallback-font metrics. Its 90 browser states have the same full inline
advance as two glyphs; add them to the existing retained matrix. Promote the
existing non-space line-state helper from `layout_text.cpp` to the module API
and call it after atomic placement, so the atom commits preceding whitespace
through the same transition as a following glyph (CSS Text §4.1.2, Phase II).
Do not adjust an ancestor height or special-case a control, label or space code
point. Add CSS Text neighbors to qualification because this shared state also
serves horizontal lines. M35 is still unqualified.

The preserved ASCII-space matrix is a positive adjacent control and already
passes before the transition repair. The causal native failure is the separate
U+3000 control: vertical automatic inline extent is 53px instead of 73px.
Chromium independently confirms the latter, and the horizontal empty atom's
bottom-edge baseline plus Ahem descent gives a 51px control block. Both the
U+3000 control and all 270 retained direct/wrapped/preserved-space states pass
after sharing the existing transition. Keep this distinction in the evidence.

**Root-control qualification prerequisite.** Accepted form neighbors expose
`select-root-crash`: after DOM replacement the root select is 22×2 rather than
22×19. Its natural height had been retained only because select measurement
wrote the CSS `given_height` slot. Root layout already measures the control,
then generic finalization discards the auto height. Reuse the existing physical
`BlockContext::used_size_owner` contract for the measured root size and keep
ordinary vertical viewport seeding away from the replaced root. Do not reinstall
natural metrics as authored CSS. Independent browser controls cover the actual
replacement, three writing modes, physical 71×37 sizing and restoration; add
the same retained native control before editing this owner (CSS 2.1 replaced
sizing / CSS Writing Modes §7.2; **D4.5.1v4 / D4.1.4v5** unchanged).

**M35 final candidate and partial-record audit.** The final inventory has
1,556/1,980 full matches, 424 mismatches and zero execution errors: 35 gains,
no lost full match, five reduced partial records, and 1,909 unchanged records
(`temp/wpt-m35-final-delta.json`). The five reductions remain visible:

- The two `digits` records retain their verified Chromium support gap; the
  native one-em control also passes retained `digits 2/3/4`, `all` and `none`.
- The original and reference `direction-upright-002` compound pages remain
  partial. Removing provisional RTL wrapping constraints makes the vertical
  upright test containers match their LTR control containers' size. Relative
  descendant geometry equality improves from 0/38 and 3/38 to 13/38 each;
  table ordering, ruby and other compound placement still disagree. Their
  changed float wrapping shifts already-incorrect global coordinates and loses
  three accidental element matches while doubling the raw text match rate
  (`temp/wpt-m35-upright-pair-audit.json`). Do not restore the incorrect
  direction-specific constraint for those matches.
- `button-appearance-native-computed-style` now rotates input-button intrinsic
  axes and uses logical UA button padding. Its existing multiline input-label
  defect is exposed: `calc_button_size` measures LF-separated labels as one
  line, so the multiline vertical input has 183.4px inline measure instead of
  the single-line label's approximately 126.34px. Previously that erroneous
  measure lay on physical width. The taller atom shifts the entire inline row;
  this explains the lost ancestor/text matches. Independent browser controls
  confirm `white-space:pre` and equal inline measures for the single/multiline
  labels (`temp/wpt-m35-native-button-browser.json`). Record this existing
  label-measurement owner for the next milestone rather than undo axis mapping.

Accepted neighbors pass: inline 169, position 139, sizing 776, multicol 334 full
plus 28 partial, CSS Text 891 full plus 89 partial, and final form 368 full plus
10 partial. The expanded CSS UI audit has fifteen failures, all reproduced
unchanged with frozen M34: ten caret-reference execution errors, two missing
reference fixtures, and three geometry mismatches
(`temp/wpt-m35-ui-inherited-audit.json`). CSS UI is not a passing category;
these findings do not become an accepted baseline change. The required Radiant
aggregate is still pending. No fixture, capture, comparison or policy changed.

**Next finite milestone / M36 plan.** P1: native input-button labels with forced
line breaks must grow in block progression while retaining the longest line's
inline measure. This common form-control prerequisite precedes combined-glyph
compression residue. Named acceptance is the native-button computed-style page
above and a reduced Ahem `XX` versus `XX\nX` label in horizontal/vertical-lr/rl
flow. Confirm UA `white-space:pre`, independent font/line-height and logical
versus physical padding, retained value/mode changes, restoration, and authored
physical sizes before editing. Trace the first disagreement in
`calc_button_size`; reuse shared forced-line intrinsic text measurement and
line-height/font helpers instead of counting a fixture's lines or fitting its
box. CSS Text whitespace processing and HTML button rendering govern it;
ownership remains **D4.5.1v4 / D4.1.4v5**. Scope native button/submit/reset label
measurement; HTML button flow children remain a control. Platform fallback-font
fidelity and the separately observed vertical textarea baseline fallback remain
partial. Budget focused browser/native controls, the original, accepted form/
inline/CSS Text neighbors, canonical writing-mode delta, lint/diff and one
required Radiant gate. Do not begin M36 implementation before M35 qualification.

**Root owner review.** The first aggregate passes, but source review catches a
necessary owner distinction before checkpointing: `root_is_form_control` also
includes HTML buttons, whose real DOM flow children are not native intrinsic
metrics (the existing `layout_form_control_has_native_intrinsic_size` contract).
Restrict the root used-size owner to that existing predicate. An independent
root-button positive probe confirms that viewport seeding was already wrong in
M34 for vertical flow buttons; multiline flow sizing also remains partial.
Do not assign native metric ownership to those children. Rebuild and rerun the
root controls, affected inventories, and required aggregate after this final
owner refinement; the first aggregate belongs to the preceding candidate.

The final predicate review leaves all 1,980 writing-mode records unchanged.
The root-flow probe remains explicitly partial: restoring the flow owner keeps
horizontal results at M34 and permits multiline vertical block growth, but
root-button logical decoration/line sizing is still incorrect. These are not
passing controls and do not broaden the qualified native-root claim.

**Gate scheduling diagnosis.** The aggregate after this owner refinement has
one `test_page_load_gtest` timeout (`md_uuid-readme`, original 15s limit); all
other components pass. The preceding candidate's aggregate passed every
component. Preserve both reports. `TEST_JOBS=1` controls build parallelism;
`test/test_run.js` still schedules twelve test binaries on this machine. Use
the documented Developer Guide §7 isolation procedure, retaining the same page
and timeout, before assigning a cause. The supported
`LAMBDA_TEST_MAX_CONCURRENT` setting bounds suite-level scheduling; no timeout,
test selection, assertion or engine behavior may be weakened for qualification.

The exact UUID page passes three times on each executable within the unchanged
15s bound (`temp/wpt-m35-uuid-isolation.json`), and the complete 111-case page
component passes three isolated runs (`temp/wpt-m35-page-load-isolation.json`).
This is consistent with the documented load-sensitive runner timeout. Recheck
the final aggregate with `LAMBDA_TEST_MAX_CONCURRENT=1`: serialize test binaries
while retaining their own supported workers, fixtures, assertions and limits.
The failed default-scheduling report remains
`temp/wpt-m35-default-scheduling-aggregate.log` and its page output remains
`temp/wpt-m35-page-load-timeout.log`; do not erase it from the completion record.

**M35 completion / 2026-10-28.** The final bounded aggregate passes all 29
components: 6,541 total, 6,400 full passes, 140 accepted partials, one existing
skip and zero failures (`temp/wpt-m35-required-aggregate.log`). It includes
318/318 native text/layout cases, 111/111 page loads, 729/729 UI cases and
237/237 Radiant View cases. One View check skipped in the earlier run executes
successfully here. The default-scheduling timeout and its isolated rechecks
remain recorded above. Dimension lint and `git diff --check` pass.

The exact requested `make layout suite=wpt-css-writing-modes` completes with
exit 0 and still reports 424 raw mismatches, with 1,556 full matches
(`temp/wpt-m35-user-command.log`). The canonical final inventory is
`temp/wpt-writing-m35-owner-final.json`; all 1,980 records are unchanged by the
last root-owner refinement. Relative to M34 there are 35 full gains, no lost
full matches, five audited partial-score reductions, and 1,909 unchanged
records. No fixture, reference, tolerance, baseline or test policy changed.

Delivered behavior: normal block-axis margins no longer enlarge an automatic
vertical inline size; final RTL line alignment uses the measured inline extent
and source-axis atomic bounds. Nested atomic positions publish through the
shared vertical mapper. An atomic item commits preceding internal whitespace
through the existing non-space line transition. Text/row-based native controls
project intrinsic axes before CSS physical sizing, UA padding follows logical
axes, and a replaced root retains its measured used size without writing it
into CSS auto-size slots. Real HTML button flow children retain their own size
owner. Existing ownership rulings **D4.5.1v4 / D4.1.4v5** are unchanged.

The final executable SHA-256 is
`9471bf9f6ea55bf98f05bab8174818d93ae28c58cd11fdb3eeddfa53709d62e5`.
Its fingerprint and all fifteen changed code-file fingerprints remain identical
through the aggregate and exact command. The qualified snapshot, source copies,
patch and manifest are `temp/wpt-m35-qualified.exe`,
`temp/wpt-m35-qualified-source/`, `temp/wpt-m35-qualified.patch` and
`temp/wpt-m35-qualified-state.json`. The support matrix now describes bounded
geometry coverage rather than claiming all box geometry is correct.

M35's finite acceptance is complete. The overall conformance goal remains
active and incomplete. Continue with the M36 native multiline-button plan
above; the five raw partial-score reductions, fifteen inherited CSS UI
failures, root-flow/button-baseline limits, and remaining 424 writing-mode
mismatches are not waived by this checkpoint. The prepared M36 browser probe
is `temp/wpt-m36-button-lines-browser.js`; it has not yet been run.

### Milestone 36: native button labels preserve forced lines

**Preflight and first disagreement.** M35's executable and seventeen source
fingerprints match the qualified manifest. Its preceding goal turn made verified
progress; no build or test process remains live. The initial browser control
matches 42/48 closed-form expectations. The other six are native input buttons
whose author 28px line-height is forced to computed `normal` by Chromium's
intact theme; custom borders disable native appearance and respect 28px.
CSS UI 4 §7.2 permits disregarding internal native-widget properties while
requiring ordinary computed-value rules. Do not copy that computed-value
adjustment or clamp an authored line-height merely to match these six cases.
Keep them as an independent native-theme compatibility limit and add an
`appearance:none` control for deterministic CSS layout.

The frozen M35 native snapshot has the expected dimensions for all HTML button
flow controls and single-line input controls in the 48-case model, but every
multiline input label fails. Its computed whitespace is `normal`, where the
browser's UA default is `pre`; `XXX\nX` measures 116×26 instead of 76×46 at
Ahem 20px. Author `white-space:normal` really collapses the LF in Chromium, so
preservation must come from the UA/default cascade, not an unconditional
line-break rule in the form measurer. The independent whitespace probe also
confirms that a terminal LF does not create another line box, leading/empty
intermediate forced lines do, and empty native buttons retain one
label line (`temp/wpt-m36-button-whitespace-browser.json`).

**Design before editing.** Share an element-level UA whitespace query between
the HTML default producer and inherited-property preservation; use the existing
input capability table for button/submit/reset. Keep authored normal, initial,
inherit/unset and restoration authoritative. Measure the control's own resolved
whitespace with `layout_measure_text_intrinsic_lines`, extending that shared
forced-line traversal with an optional line count. Exclude a
terminal empty segment from its line count; the native control supplies its
one-line minimum. Do not clone the editable textarea's caret-line counter.
`calc_button_size` uses maximum line inline advance and the sum of line block
advances before the M35 physical-axis projection. Keep non-normal authored
line-height authoritative. Reuse `LayoutFontScope` for selected-font setup and
restoration. Preserve the existing normal-line-height policy in this milestone;
its missing fallback-font contribution remains a separate limitation.

Named acceptance and verification remain the finite M36 plan above, extended
with author whitespace overrides and terminal/leading LF controls. Native
retained tests must fail before production changes; browser/CLI controls retain
raw case identities. Native glyph paint is already a separate incomplete
consumer and is not qualified by this geometry milestone. CSS Text §3–4,
HTML rendering §15.5.3/§15.5.12 and CSS Writing Modes §7.2 govern the repair;
allocation/selected-view ownership stays **D4.5.1v4 / D4.1.4v5**. No raw fixture
or reference changes are authorized.

**Implementation refinement before qualification.** The first build found a
const mismatch in the new UA helper; use the existing mutable DOM API and check
the HTML namespace before applying defaults. Inspection also shows that
`font_measure_text().height` contains only primary-font metrics. Remove that
unqualified metric extension rather than treating it as fallback-aware. M36
delivers whitespace, line count and logical-axis geometry with the existing
per-line height policy. Normal fallback line height and native label paint stay
partial, with no claim of their repair.

**Focused failures and next repair.** The native forced-line axis matrix now
passes. Authored `normal` exposes the low-level intrinsic measurer's contract:
its input must already have collapsed whitespace and trimmed line edges. Extract
the existing `intrinsic_append_normalized_inline_text` byte-range traversal so
DOM inline collection and native labels share it, retaining its processed forced
break markers. Do not add another whitespace collapse loop. Explicit `initial`
also copies an absent keyword from the lazy block default. Resolve an absent
keyword after a CSS-wide reset from the property database's initial keyword in
the existing simple-keyword resolver, rather than adding a button exception or
changing unresolved block defaults globally.

**Candidate results; required gate pending.** Both added retained regressions
pass, covering 72 value/font/mode/padding states and 168 whitespace/reset/label
states. Their causal pre-change run failed both tests
(`temp/wpt-m36-causal-before.log`). The focused shared-path selection passes
14/14 and the complete native text/layout binary passes 320/320. Chromium's
168-case whitespace matrix matches every closed-form expectation; all 48
`appearance:none` browser/native axis controls agree. The separate native-theme
matrix matches the CSS model in all 48 native CLI cases, retaining the six
Chromium theme line-height differences described above. Evidence is
`temp/wpt-m36-whitespace-browser-assertions.json`,
`temp/wpt-m36-button-lines-primitive-native.json`,
`temp/wpt-m36-fourth-focused.log` and `temp/wpt-m36-full-native.log`.

The unchanged canonical 1,980-case comparison retains 1,556 full matches,
424 raw mismatches and zero execution errors. There are no lost full matches
or reduced partial scores; 1,979 records are identical. The original
`forms/button-appearance-native-computed-style.html` improves from 0% to
44.44% element matches and 55% to 65% text matches. Its multiline input label
now keeps the same 126.4px inline extent as its single-line counterpart and
uses two 15px block advances; missing fallback-font line-height contribution
and the page's other baseline/script-output mismatches remain. This is a
behavior delivery, not an additional full-fixture pass.

Accepted form neighbors pass 368 full + 10 partial, CSS Inline 169 full,
and CSS Text 891 full + 89 partial, with no failures. Dimension lint and
`git diff --check` pass. The required Radiant gate is running with the supported
`LAMBDA_TEST_MAX_CONCURRENT=4` scheduling limit and unchanged assertions and
timeouts (`temp/wpt-m36-required-aggregate.log`); M36 is not yet qualified.
The pre-gate executable fingerprint is
`31412ed5b554d59033543245f2aed2a648e23806ef02f1057955a86d383dbb29`.
The frozen sixteen-code-file state is
`temp/wpt-m36-candidate-code-state.json`. No fixture, reference, tolerance,
accepted baseline or test-policy changes were made.

**M36 qualification and checkpoint.** The required Radiant aggregate exits 0
with all 29 components accepted: 6,543 cases, 6,401 full passes, 140 accepted
partials, two existing conditional skips and no failures. UI automation passes
729/729, layout baseline 3,158 full + 136 partial, native text/layout 320/320,
paged media 504/504 and page loads 111/111. Radiant View passes 236/237 with
the opt-in macOS speech smoke test skipped because `LAMBDA_SPEECH_SMOKE` is not
set; the other skip belongs to the existing render baseline. No skip or timeout
policy was changed. Preserve the View detail in
`temp/wpt-m36-required-view.log`. Bounded concurrency is scheduling only;
all assertions, baseline limits and timeouts remain unchanged.

The exact `make layout suite=wpt-css-writing-modes` exits 0 and reports
1,556 full matches and 424 raw mismatches
(`temp/wpt-m36-user-command.log`). The canonical final inventory and delta are
`temp/wpt-writing-m36-candidate.json` and `temp/wpt-m36-candidate-delta.json`:
no lost full match, no reduced partial score, one improved partial record and
1,979 identical records. Executable and sixteen code fingerprints remain
identical through the aggregate and exact command. Dimension lint and diff
checks pass. The qualified snapshot, source copies, patch and manifest are
`temp/wpt-m36-qualified.exe`, `temp/wpt-m36-qualified-source/`,
`temp/wpt-m36-qualified.patch` and `temp/wpt-m36-qualified-state.json`.

Delivered behavior: input button/submit/reset UA whitespace preserves forced
lines while authored overrides and CSS-wide resets remain live. Native labels
share DOM inline whitespace normalization, keep the longest line's inline
measure, omit an empty terminal line, and sum block advances before physical
axis projection. Selected-font setup restores the surrounding layout state.
The ownership rulings **D4.5.1v4 / D4.1.4v5** are unchanged. Native label paint,
normal fallback-font line height, broader control baselines and the remaining
424 raw mismatches are still partial. M36's finite acceptance is complete;
the overall goal remains active and incomplete.

**Next finite milestone / M37 plan.** P1: native button labels with
`line-height:normal` should derive their block advance from their selected
font and participating fallback faces. The existing 15px content-height policy
ignores authored font sizes and leaves the original Japanese input at 21/36px
single/two-line block extents where the reference has 25/44px. These shared
form-label metrics affect ordinary authored and multilingual forms, so they
precede isolated conformance residue. Before editing, verify reduced
`appearance:none` Ahem 12/20/24px labels and an Arial label with Japanese
fallback, in horizontal/vertical-lr/rl flows, including one/two lines,
retained font/value changes, restoration and explicit non-normal line-height
controls. Reproduce the actual first metric disagreement on M36 and add native
regressions that fail there. Reuse the glyph-selected normal metrics already
owned by inline text (`contribute_glyph_line_metrics` /
`contribute_shaped_run_line_metrics`) and the shared forced-line traversal;
do not use `font_measure_text().height` as a fallback metric, select metrics
by fixture text, or fit the reference's dimensions. CSS 2.1 §10.8.1,
CSS Text forced-line processing and CSS Writing Modes §7.2 govern the repair;
native-theme computed-value clamping and label paint remain separate limits.
Budget focused browser/native controls, the original, accepted form/inline/
CSS Text neighbors, canonical writing-mode delta, lint/diff and one required
Radiant gate. No M37 code change or new probe has started.

### Milestone 37: native normal line height shares inline font metrics

**Preflight and first disagreement.** The preceding goal turn made verified
progress: M36 delivered forced-line geometry and completed its required gate.
The current executable and all eighteen source/document hashes still match
the qualified M36 manifest. Chromium agrees on the logical block extents of
input buttons and real HTML button flow in all 144 controls across Ahem
12/20/24px, Arial 13.333/20/24px, Latin/Japanese/mixed forced lines, three writing
modes and UA versus authored physical padding. Frozen M36 matches only 33/144
native input extents, while its real HTML button flow matches all 144 browser
controls (`temp/wpt-m37-button-normal-browser.json`,
`temp/wpt-m37-button-normal-before.json`). The fixed 15px native content policy
ignores selected font size; its custom-decoration branch uses primary font
cell height instead of normal leading and fallback participation.

**Design before editing.** Add a measurement entry point around the existing
`contribute_shaped_run_line_metrics`, retaining its glyph-selected fallback and
dominant-baseline policy. Use `LayoutContextScope` to restore block, line and
font state. Initialize a primary-font normal strut with the shared inline font
helper, collect glyph ascender/descender maxima and the normal-line metric, and
return their union. The shared forced-line traversal optionally sums this
normal extent per line; different lines must not all use the tallest face from
the label. Native button sizing consumes this sum, with one primary strut for
an empty label. HTML button flow retains its existing owner. Keep explicit
non-normal line heights authoritative, including zero: the current float-only
helper conflates explicit zero with normal, so report that distinction through
its existing resolver rather than checking for a positive scalar. Add causal
retained native tests before production edits. CSS 2.1 §10.8.1 and HTML
rendering §15.5.3/§15.5.12 govern the metric choice; transient ownership remains
**D4.5.1v4 / D4.1.4v5**. The preceding finite scope and verification budget apply.

**Candidate validation; required gate pending.** The new retained regression
fails on M36 and passes after the repair. Its 270 states cover button/submit/
reset, normal primary and fallback fonts, unequal font metrics on forced lines,
zero/small/positive explicit leading, empty labels, font/value/mode mutation
and restoration. All nine native form-control tests and all 321 native
text/layout cases pass (`temp/wpt-m37-first-focused.log`,
`temp/wpt-m37-full-native.log`). Browser/native logical block extents agree
in all 144 normal cases and 144 explicit-leading cases. Two initial concurrent
CLI probes shared temporary filenames; those results are excluded and retained
as `*collision-invalid*`. The accepted reruns use separate namespaces:
`temp/wpt-m37-button-normal-candidate.json` and
`temp/wpt-m37-button-explicit-candidate.json`.

The canonical 1,980-case comparison retains 1,556 full matches, 424 raw
mismatches and zero execution errors, with no full losses or partial-score
reductions. Only the original native-button computed-style page changes:
element matches improve from 44.44% to 55.56%; text remains 65%. All twelve
native and HTML button boxes now have corresponding single/two-line block
extents, including 25/44px Japanese labels; baseline and generated report-DOM
differences remain. There are 1,979 identical records
(`temp/wpt-m37-candidate-delta.json`). Form neighbors pass 368 full + 10 partial,
CSS Inline 169 full, and CSS Text 891 full + 89 partial. Dimension lint and
diff checks pass. The required Radiant aggregate is now running with the same
supported scheduling limit of four and unchanged assertions/timeouts
(`temp/wpt-m37-required-aggregate.log`). M37 remains unqualified until it ends.
The executable SHA-256 is
`ca0ffea8a03843745b90fd729fdcf7b8b6ecb16c0c6403bd8481a522514ba877`;
the sixteen-code-file fingerprint set is
`temp/wpt-m37-candidate-code-state.json`.

**M37 qualification and checkpoint.** The required Radiant aggregate exits 0
with all 29 components accepted: 6,544 cases, 6,402 full passes, 140 accepted
partials, two existing conditional skips and no failures. UI automation passes
729/729, layout baseline 3,158 full + 136 partial, paged media 504/504, native
text/layout 321/321 and page loads 111/111. Radiant View passes 236/237; its
existing opt-in speech smoke check is skipped with `LAMBDA_SPEECH_SMOKE` unset.
The other skip belongs to the existing render baseline. Preserve the View
detail in `temp/wpt-m37-required-view.log`. No assertions, timeouts, skip rules,
fixtures, references, accepted baselines or comparison policies changed.

The exact requested command exits 0 and still reports 1,556 full matches and
424 raw mismatches (`temp/wpt-m37-user-command.log`). The canonical inventory
is `temp/wpt-writing-m37-candidate.json`; its delta against M36 has one improved
partial record, no losses or reduced partial scores, and 1,979 identical
records. Native label block extents now use selected/fallback normal inline
metrics per forced line; explicit zero is distinct from normal, and empty
native labels retain a primary strut. Font and line measurement restore the
surrounding layout context. Executable and sixteen code fingerprints remain
identical through the required gate. Dimension lint and diff checks pass.

The qualified snapshot, source copies, patch and manifest are
`temp/wpt-m37-qualified.exe`, `temp/wpt-m37-qualified-source/`,
`temp/wpt-m37-qualified.patch` and `temp/wpt-m37-qualified-state.json`.
M37's finite acceptance is complete. Broader native baselines, native glyph
paint, generated WPT report DOM differences and the remaining 424 raw
mismatches are still partial; the overall goal remains active and incomplete.
Existing ownership rulings **D4.5.1v4 / D4.1.4v5** are unchanged.

**Next finite milestone / M38 plan.** P1: publish native input-button label
baselines from their actual line metrics. Common multilingual or multiline
form rows still misalign even though M37 makes their box sizes agree. In the
original page, single/two-line native inputs remain at the same y=119.4px,
while corresponding HTML button flow exports distinct last-line baselines.
The current native baseline producer still uses physical top decoration plus
the primary font ascender; trace it through block publication and inline
consumption before attributing the whole mismatch to that producer.

Before editing, compare independent browser baseline markers for input
button/submit/reset and real HTML button flow: one/two lines, Latin/Japanese
and mixed fonts, normal and zero/small/positive explicit leading, horizontal
and parallel vertical flows, an orthogonal control, authored physical block
sizes, retained value/font/mode mutation and restoration. Confirm how native
content centering and first/last baseline selection behave rather than
fitting the original row's offsets. Reuse the shared line-font collector,
forced-line traversal and existing logical baseline conversion helpers;
do not add another glyph scan or physical-axis baseline formula. CSS Inline
3 §§3–4, CSS Align 3 §9.1, HTML button layout and CSS Writing Modes dimensional
mapping govern this work; **D4.5.1v4 / D4.1.4v5** remain the ownership contract.
Keep textarea-specific baseline policy, native glyph paint and report-DOM
fidelity as separate limits. Budget causal browser/native controls, the
original, relevant accepted form/inline/alignment neighbors, canonical
writing-mode delta, lint/diff and one required Radiant gate. No M38 code or
probe has started.

### Milestone 38: label baselines follow line metrics and used content origins

**Preflight / causal evidence.** The preceding goal turn made verified M37
progress. Its executable and eighteen source/document fingerprints still match
the qualified manifest. Browser input/HTML button baseline offsets agree in
all 360 controls; frozen M37 matches six native offsets and 48 flow offsets.
The controls cover five parallel/orthogonal parent/child mode combinations,
Latin/Japanese mixed labels, normal/zero/14px/32px leading, physical padding
and definite physical block size. Independent first/last baseline-source
controls confirm selection and centering. Evidence is
`temp/wpt-m38-button-baseline-browser.json`,
`temp/wpt-m38-button-baseline-before.json` and
`temp/wpt-m38-button-source-browser.json`.

**Owning-stage diagnosis and design.** Native labels currently publish a
primary-font first-line ascender regardless of their forced lines or leading.
Extend the shared line collector to return block extent and block-start
baseline for normal and explicit leading; accumulate first/last offsets in
the existing forced-line traversal, then add logical decoration and used
content centering after native sizing. Keep the metrics transient until final
border-box baseline publication. Existing first/last baseline selection and
parent-axis conversion remain consumers.

The flow control exposes adjacent shared defects: horizontal button text is
centered after its baseline cache is written; vertical publication translates
text origins without translating baseline caches and centers each fragment
independently. Center the complete flow group using its line-box block extent,
preserve forced-line progression, and translate the same cached baseline set.
Promote the existing fieldset baseline-shift helper rather than copying it.
Remove the zero-sized-atomic vertical-lr position exception: a nonzero marker
probe isolates it from label metrics and confirms ordinary atomic placement
must also handle empty boxes. Orthogonal native replaced-box fallbacks must
use the parent axis and shared baseline synthesis instead of physical height.

There is an additional button-specific orthogonal-browser behavior: with
identical explicit 26×76 boxes, Chromium exports a horizontal baseline at
68px for input/button and 76px for an ordinary inline-block span. CSS Inline
3 A.3 prescribes margin-box synthesis for generic orthogonal atoms; preserve
this distinction for later diagnosis rather than introducing an 8px
adjustment (`temp/wpt-m38-ortho-baseline-browser.json`). It remains a raw
compatibility limit until its internal content-baseline owner is established.
The selected core is parallel line baselines, shared content-origin
publication and standards-based orthogonal fallback; no result is waived.
CSS Inline 3 §§3–4/A.3, CSS Align 3 §9.1 and HTML button layout govern the
repair; **D4.5.1v4 / D4.1.4v5** still govern transient ownership. Add failing
retained regressions before production edits; keep the preceding verification
budget and record every original/corpus disagreement honestly.

**First implementation and boundary refinement.** The retained regression
fails on M37 and passes after the first repair, including first/last selection,
line progression, explicit zero leading, centering, padding, zero/nonzero
markers and restoration. All ten form-control cases and 322 native cases pass.
Browser agreements improve from six to 252 native offsets and from 48 to 252
flow offsets. Parallel Ahem cases agree throughout; twelve parallel Arial
normal cases differ by 0.5px because the existing dominant-font projection
includes normal leading before synthesizing a central font baseline. A separate
odd-height font probe and existing `RADIANT_TRACE_FONT` output distinguish that
font-metric precision limit from baseline publication. Do not add a 0.5px fit or
weaken the probe comparison (`temp/wpt-m38-font-baseline-browser.json`,
`temp/wpt-m38-font-trace.log`).

The remaining orthogonal difference has a concrete owner: with an authored
200px content inline size, input/button export the anonymous content edge at
208px while the equivalent span exports its outer margin edge at 216px.
Asymmetric physical padding independently confirms the content-box center in
a vertical parent. The earlier compatibility observation is therefore refined
to an anonymous-button-content baseline source, without changing generic
atomic margin synthesis (`temp/wpt-m38-ortho-inline-size-browser.json`). Add a
causal native regression including ordinary span controls, then share that
content-rectangle baseline source between line collection and final alignment.
The new orthogonal regression fails on the first candidate before this repair
(`temp/wpt-m38-ortho-causal-before.log`). Preserve the twelve font-metric
precision disagreements as a separate partial limit; the selected baseline
publication repair does not qualify every font rounding convention.

**Stable candidate; required gate pending.** The final retained form selection
passes 11/11. The parallel baseline regression covers 468 states, including
independent value, font-size, writing-mode and RTL changes, first/last selection,
normal/zero/small/positive leading, physical sizing, padding and restoration.
The orthogonal regression includes input/button content-box sources and
ordinary span margin-box controls, including an authored 200px inline size.
The causal before logs remain `temp/wpt-m38-causal-before.log` and
`temp/wpt-m38-ortho-causal-before.log`; final focused evidence is
`temp/wpt-m38-final-focused.log`. The 323-case native suite passes before the
last test-only font-size extension; the required gate will re-run that complete
binary with the final tests.

Final browser/native baseline agreements are 348/360 for both native and flow
buttons (`temp/wpt-m38-button-baseline-anonymous-final.json`). The twelve
remaining parallel Arial normal-leading offsets differ by 0.5px and remain
explicitly failed in that unchanged probe. Anonymous-content baseline
selection resolves the orthogonal cases without modifying generic atom
synthesis. The original computed-style page improves from 55.56% to 83.33%
element matches and 65% to 80% text matches. Its generated report DOM and
default-whitespace `<br>` text geometry remain incomplete.

The canonical inventory improves to **1,558/1,980 full matches**, 422 raw
mismatches and zero execution errors. There are two full gains
(`forms/button-appearance-native-vertical.optional.html` and
`forms/button-appearance-none-vertical.optional.html`), two improved partial
records, no full losses or reduced partial scores, and 1,976 identical records
(`temp/wpt-writing-m38-candidate.json`, `temp/wpt-m38-candidate-delta.json`).
Form neighbors pass 368 full + 10 partial and CSS Inline 169 full. Expanded
CSS Align has 306 full + two partial and six required failures; all six
reproduce with identical comparisons on frozen M37
(`temp/wpt-m38-align-inherited-audit.json`). CSS Align is not a passing
category and those failures are not waived or added to an accepted baseline.

Dimension lint and diff checks pass. The required Radiant aggregate is running
with the supported concurrency limit of four and unchanged tests/timeouts
(`temp/wpt-m38-required-aggregate.log`). The executable SHA-256 is
`5b0e59e45d1c69d65a6a58f0d992f41bc8c4525439b7848a7c2c4e4787da2e4a`;
the seventeen-code-file fingerprint set is
`temp/wpt-m38-candidate-code-state.json`. No fixture, reference, tolerance,
baseline or test-policy change was made. M38 is not yet qualified.

**M38 qualification and checkpoint.** The required Radiant aggregate exits 0
with all 29 components accepted: 6,546 cases, 6,404 full passes, 140 accepted
partials, two existing conditional skips and no failures. The final 323-case
native suite passes with all 468 retained baseline states, including the
test-only font-size extension. UI automation passes 729/729, layout baseline
3,158 full + 136 partial, paged media 504/504 and page loads 111/111.
Radiant View passes 236/237; its existing opt-in speech smoke test is skipped
with `LAMBDA_SPEECH_SMOKE` unset. The other skip is the existing render
baseline skip (`temp/wpt-m38-required-view.log`). No test, assertion, timeout,
skip rule, accepted baseline, fixture, reference or tolerance was changed.

The exact `make layout suite=wpt-css-writing-modes` exits 0 and reports
1,558 full matches and 422 raw mismatches
(`temp/wpt-m38-user-command.log`). The canonical inventory/delta remain
`temp/wpt-writing-m38-candidate.json` and `temp/wpt-m38-candidate-delta.json`:
two full gains, two improved partials, no losses or reductions, 1,976 identical
records and zero execution errors. Form and Inline neighbors pass; the six
identical inherited Align failures are retained as a separate qualification
limit. Executable and all seventeen code fingerprints remain identical through
the gate; dimension lint and diff checks pass. The qualified snapshot, source
copies, patch and manifest are `temp/wpt-m38-qualified.exe`,
`temp/wpt-m38-qualified-source/`, `temp/wpt-m38-qualified.patch` and
`temp/wpt-m38-qualified-state.json`.

Delivered behavior: native button labels publish first/last baselines from the
shared per-line font/leading collector after logical decoration and used
content centering. Flow content and its baseline caches share origin shifts;
vertical inline-only publication preserves line progression and aligns the
complete button content group. Empty atomic boxes use ordinary placement.
Orthogonal button layout supplies the anonymous content-box baseline, while
ordinary atoms retain their margin-box source. First/last selection, font-size,
value, mode and RTL mutation and restoration pass retained controls. Ownership
still follows **D4.5.1v4 / D4.1.4v5**. The twelve 0.5px font-metric differences,
native glyph paint, report-DOM fidelity, default-whitespace `<br>` text
publication and the remaining 422 writing-mode mismatches remain partial.
M38's finite baseline milestone is complete; the overall goal remains active
and incomplete.

**Next finite milestone / M39 plan.** P1: vertical button flow with explicit
`<br>` must preserve separate columns and per-line inline alignment through
the final geometry publisher. The original native-button computed-style page
still places both lines of a vertical-lr HTML button at the same x coordinate,
and the shorter line starts at the long line's inline position. Its box sizes
and baseline source are now correct; trace the first coordinate disagreement
through line breaking, collection, intrinsic probes, actual flow and the final
mapper before editing. The legacy flex flow mapper still has a per-fragment
button-centering path; establish whether it owns the observed overwrite
rather than inferring that from the final coordinates alone.

Acceptance: compare reduced Ahem `XXX<br>X` under normal whitespace with
`XXX\nX` under pre, in horizontal/vertical-lr/vertical-rl flow, auto and fixed
inline/block sizes, normal and authored leading, RTL and retained label/style/
mode changes and restoration. Verify each line's text rectangles and the
button's baseline/box dimensions against an independent browser, plus the
original page. Reuse forced-line and shared group-alignment/axis publication
helpers; do not copy another mapper or fit either line to reference offsets.
Keep native value-label paint and minor font metric precision separate.
CSS Text forced breaks/alignment, HTML button layout and CSS Writing Modes
dimensional mapping govern this repair; **D4.5.1v4 / D4.1.4v5** still govern
ownership. Budget causal browser/native controls, the original, relevant
accepted form/inline/flex-path neighbors, canonical writing-mode delta,
lint/diff and one required Radiant gate.

**M39 diagnosis (in progress).** The twelve reduced Ahem controls show that
both preserved LF and `<br>` retain their separate line numbers and block
coordinates through line layout and the vertical mapper. In an automatic
vertical-lr button, the mapper produces x=3/23 for two 20px columns; final
capture instead reports x=13/13. `finalize_block_flow` then calls the horizontal
shrink-to-fit deferred aligner on already physical vertical rectangles, using
the physical width as its inline measure. Independently, `line_align` defers
alignment based on physical `given_width`, even when vertical `given_height`
has already resolved its inline measure. This leaves the short line at the
long line's inline start before mapping. Repair both axis/phase decisions in
the existing flow and line helpers; do not add a button-only placement path.
The reduced flex host also exposes a separate 66x56 versus 46x76 automatic
size discrepancy. Attribute it after the line repair before expanding scope.
Evidence: `temp/wpt-m39-button-lines-{browser,before}.json` and
`temp/wpt-m39-trace-cli.log`. CSS Text 3 §§5/6 (forced breaks and each line's
inline alignment), CSS Writing Modes 3 §3.2 (column progression), and HTML
§15.5.3 (button formatting context and fit-content inline size) govern these
coordinates. **D4.5.1v4 / D4.1.4v5** remain unchanged.

**M39 implementation and focused evidence.** `line_align` selects the physical
axis that owns the inline measure before deciding whether shrink-to-fit needs
deferred alignment. `finalize_block_flow` keeps horizontal deferred alignment
and horizontal child stretching within horizontal flow; vertical source lines
have already aligned before their physical projection. These are shared
flow/line decisions, with no form-specific rectangle correction, new mapper,
constant displacement or ownership change. Diagnostic logging is removed.

The new retained regression fails before this repair and passes all 120 states
afterward: LF versus `<br>`, horizontal/vertical-lr/vertical-rl, UA versus
asymmetric physical decoration, label-only changes and restoration, normal/
14px/32px leading, automatic/fixed logical dimensions, mode changes and RTL
center/start/end alignment. Chromium independently agrees with the derived
font/line/decoration equations in all 216 matrix controls, including a fixed
block size with automatic inline size. Native geometry improves from 168/216
to **216/216** against those unchanged browser controls
(`temp/wpt-m39-button-lines-matrix-comparison.json`,
`temp/wpt-m39-browser-derivation-audit.json`). All 324 native cases pass.

The original computed-style page improves from 80% to **100% text matches**;
its element result remains 83.33% because the generated WPT report DOM is
absent. The canonical 1,980-file delta has one improved partial, 1,979 identical
records, no loss or reduction, 1,558 full matches, 422 raw mismatches and zero
execution errors (`temp/wpt-writing-m39-first.json`,
`temp/wpt-m39-first-delta.json`). Form neighbors pass 368 full + ten partial;
CSS Inline passes 169 full. Dimension lint and diff checks pass. An initial
neighbor invocation used nonexistent category names and was discarded before
the real categories ran; it did not execute tests or change harness policy.

The four reduced vertical flex-host cases remain identically wrong on frozen
M38 and M39: 66x56 automatic boxes versus Chromium's 46x76, with the same
text coordinates. The shared repair neither creates nor resolves that sizing
defect (`temp/wpt-m39-button-lines-{before,first}.json`). Treat its intrinsic
physical-axis selection as the next finite investigation, not a placement
exception in this repair. Native value-label paint, the twelve prior font
precision differences and report-DOM fidelity remain separate limits.
The required Radiant aggregate is running with the supported concurrency limit
of four, unchanged assertions/timeouts and accepted baselines
(`temp/wpt-m39-required-aggregate.log`). M39 is implemented but not yet
qualified; the overall goal remains active.

**M39 qualification and checkpoint.** The required aggregate exits 0 with all
29 components accepted: 6,547 cases, 6,405 full passes, 140 accepted partials,
two existing conditional skips and no failures. UI automation passes 729/729,
layout 3,158 full + 136 partial, native text/layout 324/324, paged media
504/504 and page loads 111/111. View passes 236/237 with its existing opt-in
speech smoke skipped; render retains its existing baseline skip
(`temp/wpt-m39-required-aggregate.log`, `temp/wpt-m39-required-view.log`).
The exact requested command also exits 0 and reports 1,558 full matches and
422 raw mismatches (`temp/wpt-m39-user-command.log`). No assertion, timeout,
fixture, reference, tolerance, skip policy or accepted baseline changed.

The executable SHA-256 is
`2c2f6b0a169b19e016044031b8a7aa9a60968658b221f31fa73934d847e645ee`;
all seventeen code fingerprints remain unchanged through qualification.
The qualified executable, nineteen source/doc copies, patch and manifest are
`temp/wpt-m39-qualified.exe`, `temp/wpt-m39-qualified-source/`,
`temp/wpt-m39-qualified.patch` and `temp/wpt-m39-qualified-state.json`.
The canonical delta retains 1,979 identical records and one improved partial,
with no regression or execution error. M39's finite line-publication milestone
is complete: both forced-break forms preserve their column progression and
each line's inline alignment. The overall conformance goal remains active.

**Next finite milestone / M40 investigation plan.** P1: diagnose automatic
physical sizing of orthogonal flex items containing ordinary flow. The reduced
vertical button in a horizontal row-flex host is still 66x56 instead of 46x76
despite the same content measuring correctly in ordinary flow. This is a
common control/container sizing path, rather than the deferred 0.5px font
precision residue. Trace intrinsic measurement, flex base/hypothetical cross
size, assigned used sizes and actual child formatting. Identify the first
disagreement before changing either the shared intrinsic helper or the form
control branch. Relate the owner to the remaining WPT sizing failures before
selecting an implementation scope; do not expand this into all flex behavior.

Acceptance for a selected repair: independent Ahem controls for HTML buttons,
native labels and ordinary atomic flow; row/column containers with parallel
and orthogonal axes; forced breaks, auto versus authored dimensions, asymmetric
decoration and one retained content/mode/size mutation/restoration sequence.
Use CSS Flexbox intrinsic/base/cross-size rules and CSS Writing Modes
orthogonal dimension mapping, retaining **D4.5.1v4 / D4.1.4v5** ownership.
Keep glyph paint, font quantization, script-generated report DOM and compound
table/ruby behavior separate. Budget focused causal browser/native evidence,
relevant accepted neighbors, canonical writing-mode delta, lint/diff and one
required aggregate after a stable implementation. No M40 code or probe has
started; only the unchanged M39 flex counterexample and source reading exist.

**M40 scope selected from diagnosis.** The 60 native-label/HTML-button/ordinary-
flow controls expose several independent sizing defects (11/60 boxes currently
agree). Do not combine their repairs. Existing writing-mode fixtures
`forms/input-range-zero-inline-size.html` and its reference expose a shared
container defect: a vertical flex container's provisional 1,184px physical
width is inferred definite from the horizontal available-space constraint;
its single line then stretches to that width instead of the 16px content
extent. `sideways-lr-main-axis.html` has the same wrong container width plus
an independent inline-progression reversal defect. Diagnostic evidence is
`temp/wpt-m40-range-trace.log`; sizing controls are
`temp/wpt-m40-flex-sizing-{browser,before}.json`.

Select P1 **automatic logical block sizing of vertical flex containers** as the
M40 implementation. The first disagreement is definiteness inference in
`init_flex_container`, followed by physical-Y-only automatic extent publication.
The horizontal available width can stretch a horizontal inline axis; it does
not make an automatic vertical block axis definite. Generalize existing
automatic-axis publication rather than adding a form/fixture-specific width
override. Preserve explicit sizes (including zero), parent-assigned sizes,
size containment and min/max constraints. Include empty containers, decoration,
row/column directions and retained content/style/mode restoration. Verify the
two zero-inline-size range fixtures and the original sideways fixture without
claiming its independent direction issue solved. CSS Flexbox §§9.2/9.4 and CSS
Writing Modes §7 govern sizing; **D4.5.1v4 / D4.1.4v5** ownership is unchanged.
Native-label stale metrics, forced-line intrinsic measurement and sideways
direction remain separate follow-ons. Qualification budget stays as planned.

**M40 implementation / counterexample refinement.** Generalize the existing
automatic-height finalizer to publish either physical axis from the same flex
line extents, with empty content, decoration and min/max constraints included.
An automatic vertical block width is not inferred definite from horizontal
available space. Explicit/assigned/contained dimensions still own their used
sizes. The first controls had a definite containing width and all 280 already
passed; preserve them as positive controls. Their automatic-parent variants
fail at the observed owning decision: frozen M39 agrees in 152/280 cases,
while the initial candidate agrees in 280/280. The new retained regression
passes 400 states; all 325 native cases pass before the counterexample repair.

Additional flex/grid-assignment and containment controls expose 32 new nested-
flex failures and eight inherited horizontal containment failures. The owning
stage is `layout_flex_item_content_pass`: it swaps physical content dimensions
into surrogate coordinates even when the nested formatter is flex and already
owns physical axes. This hides the parent's assigned width from the new
definiteness policy. Keep nested flex in its physical content frame and add
retained assigned-width restoration coverage. Grid assignment controls pass;
horizontal size-containment width disagreement reproduces on frozen M39 and
is a separate existing policy defect. Evidence:
`temp/wpt-m40-owner-{browser,before,first}.json`. No required aggregate has run
yet; finish counterexamples before qualification.

The physical-frame correction alone does not resolve the assigned-width
counterexample. A second trace finds the actual constraint overwrite: a
redundant child-view initialization loop resolves the last child's 20x30 style
into `lycon->block.given_width/height` immediately before nested-container
initialization. The assigned container is 200x120 but receives 20x30 constraints
(`temp/wpt-m40-owner-trace.log`). Remove that duplicate loop; the shared nested
collector already initializes every child after capturing the container's
constraints. Keep the physical-frame correction as its independent prerequisite.
Do not infer ownership from a positive provisional size or add a compensating
width restoration after layout. The retained assigned-width regression remains
failed until this producer/consumer handoff is corrected.

**M40 final implementation / focused outcome.** After both handoff corrections,
all **326 native tests pass**, including 400 automatic-block retained states
and 60 parent-width mutation/restoration states. The final executable agrees
with all **280/280** automatic-parent Chromium controls. The assignment and
containment matrix improves to **112/120**, with all flex/grid assignments
passing, no new failures, and the same eight horizontal size-containment
disagreements as frozen M39 (`temp/wpt-m40-owner-final-audit.json`). The
280 definite-parent controls were already passing on M39 and remain separate
positive evidence. Native final logs and browser derivations are
`temp/wpt-m40-final-native.log`, `temp/wpt-m40-container-final.json` and
`temp/wpt-m40-container-browser-derivation.json`.

The sideways main-axis page improves from 35.29% to 76.47% element matches:
container block sizes now match; its independent inline-progression reversal
remains. Range-zero fixtures shrink from a viewport-wide 1,184px block extent
to the native control's stale horizontal 129px intrinsic extent; Chromium
expects 16px. Their raw pass rates remain unchanged. That native intrinsic
projection is the next finite consumer repair, not a special width here.

The canonical inventory is **1,558/1,980 full matches**, 422 raw mismatches and
zero execution errors. There are no full gains or losses, one improved partial,
**two reduced partial records**, and 1,977 unchanged records
(`temp/wpt-writing-m40-final.json`, `temp/wpt-m40-final-delta.json`). Both
`direction-upright-002.html` and its reference lose one of 161 text matches
(2/161 to 1/161). Their vertical flex widths independently match Chromium at
18px, replacing 133.6/141.6px. A ruby D's local offset remains 43px before and
after, versus Chromium's 88px; its absolute y changes when narrower floats no
longer wrap onto the previous extra row. The inference is that correcting flex
width removes an accidental absolute-position match amid existing table/ruby/
typographic errors; the complete pages remain incorrect and the reductions
are retained, not waived (`temp/wpt-m40-upright-partial-audit.json`,
`temp/wpt-m40-upright-browser.json`).

Form neighbors pass 368 full + ten partial, CSS Inline 169 full. Dimension lint
and diff checks pass. No fixture, reference, tolerance, accepted baseline,
assertion or skip policy changed. The required Radiant gate is now running
against the stable candidate (`temp/wpt-m40-required-aggregate.log`), whose
eighteen code fingerprints are recorded in
`temp/wpt-m40-candidate-code-state.json`; M40 is not yet qualified.

**M40 qualification and checkpoint.** The required Radiant aggregate exits 0
with all 29 components accepted: **6,549 cases, 6,407 full passes, 140 accepted
partials, two existing conditional skips and no failures**. UI automation
passes 729/729, layout baseline 3,158 full + 136 partial, native text/layout
326/326, paged media 504/504 and page loads 111/111. View passes 236/237 with
the existing opt-in speech smoke skipped; render retains its existing baseline
skip (`temp/wpt-m40-required-aggregate.log`,
`temp/wpt-m40-required-view.log`). The exact requested command exits 0 and
reports 1,558 full matches and 422 raw mismatches
(`temp/wpt-m40-user-command.log`). All final eighteen code fingerprints and
the executable remain unchanged through the gate; lint/diff checks pass.

The canonical delta and the two raw compound-page reductions above remain the
reported WPT result. Final versions of both compound layout trees are identical
to those audited before qualification (`temp/wpt-m40-upright-final.log`).
No assertion, fixture, reference, tolerance, baseline or policy was changed.
The qualified executable, twenty source/doc copies, patch and manifest are
`temp/wpt-m40-qualified.exe`, `temp/wpt-m40-qualified-source/`,
`temp/wpt-m40-qualified.patch` and `temp/wpt-m40-qualified-state.json`.

Delivered core behavior: vertical flex containers use their content's logical
block extent instead of a provisional horizontal available width. The shared
axis finalizer preserves decoration, empty content, explicit zero, min/max and
assigned sizes. Nested flex keeps its physical content frame and initializes
children through the shared collector after capturing its own constraints;
retained width changes and restoration pass. Ownership remains
**D4.5.1v4 / D4.1.4v5**. This finite milestone is qualified; native intrinsic
projection, sideways direction, eight inherited horizontal containment controls,
compound typography/table/ruby and 422 raw writing-mode mismatches remain open.
The overall goal is active and incomplete.

### Milestone 41: current native control metrics for flex and grid

**Next finite milestone / M41 plan.** P1: make flex sizing consume current
native-control intrinsic dimensions in the physical axis being sized. The
remaining zero-inline-size range fixtures use a horizontal 129px intrinsic
extent for a vertical block axis that should use the 16px native block extent.
Trace the current producer (`layout_measure_form_control`), retained form role
metrics, flex basis and hypothetical cross-size consumption before editing.
Refresh the initial 60-case control matrix against qualified M40 rather than
assuming earlier scores still describe it. Reuse the native measurement/axis
adapter and convert content/border dimensions exactly once; keep authored
zero/definite sizes authoritative.

Done criteria: both zero-inline-size range fixtures, parallel/orthogonal row and
column native-control controls, asymmetric decoration, and one retained mode/
font/value/size mutation/restoration sequence. Include HTML button and ordinary
flow controls to challenge role dispatch, while keeping their independent
forced-line intrinsic-flow repair separate. CSS Writing Modes dimensional
mapping, CSS Flexbox §§9.2/9.4 and HTML widget writing-mode rules govern this
work; **D4.5.1v4 / D4.1.4v5** continue to govern ownership. Budget focused
browser/native causal cases, relevant accepted neighbors, canonical WPT delta,
lint/diff and one required Radiant aggregate after the implementation stabilizes.
No M41 probe or code has started.

**M41 diagnosis (in progress).** Qualified M40 reproduces the original 60-case
matrix unchanged: native input buttons agree in 5/20 boxes, HTML buttons in
4/20 and ordinary atomic flow in 2/20. Keep the latter two independent flow
intrinsic defects separate. Native sizing disagrees at three connected stages:
range defaults are not reset/projected like logical text/row metrics;
`flex_form_intrinsic_size` and the indefinite-main contribution path read stale
resolver-time role fields for most native controls; and
`layout_measure_form_control` substitutes stale values for valid zero content
dimensions. Cross-size consumption additionally recomputes font leading after
the form formatter already resolved native metrics and authored dimensions.

Trace and repair these producer/query/consumer contracts together. Range
logical defaults, including the existing datalist block-size policy, must come
from shared input metadata before one writing-mode projection per layout.
Native measurement returns current physical content dimensions, including zero;
flex adds decoration once and preserves authored dimensions. Do not extend the
native-label path to HTML button children or ordinary flow to conceal their
separate errors. The new 300-case browser matrix covers native range/button/
text/textarea/select controls in parallel/orthogonal row and column contexts,
UA/asymmetric decoration and auto/zero/authored inline size, with ordinary-flow
controls (`temp/wpt-m41-native-browser.json`). CSS Flexbox §§9.2/9.4, CSS
Writing Modes dimensional mapping and HTML §§15.5.2/15.5.8 govern the behavior;
**D4.5.1v4 / D4.1.4v5** ownership is unchanged. No engine edit yet.

**M41 design refinement / implementation.** Natural intrinsic metrics and used
content sizes are distinct query contracts. Reusing a used-size measurement for
the automatic minimum would incorrectly make an authored 150px main size the
control's natural minimum. Extract the existing intrinsic producer from form
layout, share it with a scoped natural-metrics query, and leave used dimensions
and baseline publication in the actual formatter. The natural query updates
only role metrics under the existing measurement snapshot, preserving committed
geometry and avoiding baseline writes. Flex bases/minima consume its physical
content dimensions; hypothetical cross sizes continue to consume current used
content dimensions. This reuses the existing font/label/option producers and
does not add another glyph or line-measurement path.

Range defaults and the existing list-height option now share input metadata
between UA initialization and the intrinsic producer, then project once per
layout. Used-size measurement retains exact zero content dimensions. The
indefinite main-axis contribution also uses the current natural query, and the
cross-size consumer no longer performs a second font-height calculation after
form layout. New tests fail before the repair: range zero measurements return
129 instead of zero, vertical range axes stay horizontal, and flex controls
disagree with identical native controls in ordinary flow. Retained tests cover
query purity, both measurement meanings, mode/size restoration and native
natural automatic minima under authored main sizes. Build is in progress;
no qualification claim yet.

**M41 adjacent-consumer correction.** Initial focused tests pass, all 329 native
cases pass, and the canonical inventory gains eight full range fixtures with
no loss or reduced partial. The 300-case browser matrix improves from 112 to
240 agreements: all 60 cases each of range/button/text/textarea agree; the 60
select cases retain the existing two-pixel native block-size discrepancy,
while flex now agrees with ordinary-flow widths and heights. All six independent
authored-size/automatic-minimum controls agree (zero agreed on M40).

A targeted adjacent grid control exposes the same stale range-height consumer:
the child is physically 16x129, but an intrinsic grid row reads resolver-time
`form->intrinsic_height` and stays 16px tall. Four of six controls agree before
this repair (`temp/wpt-m41-grid-first.json`). Route that input-height path
through the existing current used-content query, retaining exact zero rather
than falling back to a font height. Add retained grid track/child agreement
through mode/inline-size restoration and decoration. This completes another
consumer of the selected query contract without adding grid-specific sizing
or a compensating track minimum. No required aggregate has run yet.

The decorated retained grid control finds a second contract mismatch after its
track size is corrected: non-stretch alignment assigns the replaced formatter's
content height directly to the border-box height, dropping authored padding and
border. Replaced final-content layout explicitly stores the content box, unlike
the legacy ordinary-flow field used by that alignment branch. Convert through
the existing content-to-border helper for replaced boxes; do not add a native-
control-specific adjustment. This extends the selected producer/query/consumer
repair to the observed final geometry owner. Plain grid browser controls pass
6/6; decorated controls and the retained grid test must pass before qualification.

**M41 final implementation / focused outcome.** Both shared query contracts
now use the current physical control metrics. The natural query reuses the
intrinsic producer before authored size resolution; the used query retains
valid zero content dimensions. Flex consumes the appropriate query for natural
minima/bases versus hypothetical cross sizes. Grid intrinsic input heights use
the used query, and final non-stretch replaced-box alignment adds decoration
through the existing box conversion helper. Range UA metadata, including the
existing list-height option, is shared rather than duplicated. No fixture
condition, fitted dimension, reference, tolerance or harness policy changed;
**D4.5.1v4 / D4.1.4v5** ownership is unchanged.

Four new native regressions cover 702 retained states: 96 range default/zero/
query-purity states; 540 native flex-versus-flow states through font, label,
mode, direction and authored size edits; 18 authored-main-size/natural-minimum
states; and 48 decorated/undecorated grid track states. All **330/330** native
tests pass (`temp/wpt-m41-final-native-suite.log`). The initial causal failures
remain in `temp/wpt-m41-causal-before.log`.

The final consolidated independent Chromium size matrix has 378 cases
(`temp/wpt-m41-final-controls-input.json`, `temp/wpt-m41-final-controls.json`,
`temp/wpt-m41-final-controls-audit.json`). Against frozen qualified M40:

| Family | M40 agreements | M41 agreements | Remaining evidence |
|---|---:|---:|---|
| Native controls in parallel/orthogonal row/column flex | 112/300 | 240/300 | All 60 range, button, text and textarea cases agree; all 60 select cases retain an existing two-pixel logical block-size discrepancy. |
| Authored main size with smaller natural automatic minimum | 0/6 | 6/6 | None in this finite matrix. |
| Grid track and replaced child, including decoration | 2/12 | 12/12 | Both host and child widths/heights agree. |
| Initial native/HTML-button/ordinary-flow dispatch controls | 11/60 | 26/60 | Native inputs now 20/20; HTML buttons remain 4/20 and spans 2/20. |

There are no newly failed size cases. These probes compare widths/heights;
they do not qualify positions or glyph/control paint. The final canonical
inventory is **1,566/1,980 full geometry matches**, 414 mismatches, zero
execution errors (`temp/wpt-writing-m41-final.json`). The exact M40 delta is
**eight full gains, zero losses, zero reduced partials, 1,972 unchanged**
(`temp/wpt-m41-final-delta.json`). The gains are the zero-inline-size range
fixture/reference and six vertical range appearance/painting-named fixtures;
their names do not expand this geometry qualification to paint conformance.

Accepted neighbors remain **form 368 full + 10 partial** and **wpt-css-inline
169 full**, with no failures (`temp/wpt-m41-final-neighbors.log`). Dimension
lint and diff checks pass. The code and executable fingerprints are frozen in
`temp/wpt-m41-candidate-code-state.json`; the required Radiant aggregate is
running with the existing concurrency bound and unchanged assertions/timeouts.
M41 is not yet qualified; the overall conformance goal remains active.

**Next finite milestone / M42 plan.** P1: trace the remaining ordinary-flow
forced-line intrinsic measurement consumed by flex sizing. The refreshed
dispatch matrix now separates native inputs (20/20) from HTML buttons (4/20)
and spans (2/20). Those ordinary children must retain their DOM layout path.
Existing reduced evidence includes a preserved `XXX\nX` whose horizontal
max-content width accumulates both forced lines instead of taking the widest
line, and a vertical HTML button with disagreeing inline/block contributions.
Refresh those cases against qualified M41 before selecting the implementation.

Trace CSS Text whitespace/forced-break processing into min/max inline
contributions, block advances and flex row/column consumers. Reuse the existing
`layout_measure_text_intrinsic_lines` producer and related helpers if they own
the needed contract; do not copy glyph loops or replace child structure with a
native label. Normative rules are CSS Text §§3–5, CSS Sizing intrinsic sizes,
CSS Flexbox §§9.2/9.4 and CSS Writing Modes §7. Existing transient ownership
remains **D4.5.1v4 / D4.1.4v5**.

Acceptance scope: preserved LF versus `<br>`, horizontal/vertical-lr/vertical-rl,
parallel and orthogonal row/column parents, auto/authored inline sizes, and
retained label/leading/mode/direction/restoration. Include ordinary wrappers
and decorated child structure to avoid treating every atomic box as flat text.
Confirm terminal/empty-line semantics in the browser before asserting them.
Add root-cause regressions after identifying the first disagreement, preserve
the M41 native/minimum/grid controls, run relevant accepted neighbors and the
canonical delta, then one required Radiant aggregate at the stable boundary.
Broader native-select theme height, font precision, compound table/ruby flow,
script-report DOM and sideways direction remain separate backlog. No M42 probe
or code has started.

**M41 required-gate counterexample / contract refinement.** The first required
aggregate fails only `range-percent-intrinsic-size-2` and `-2a` in
`wpt-css-sizing`: an inline range with `height:50%; min-height:min-content`
inside an auto-height inline-block shrinks to 8px rather than its natural 16px.
The other groups pass, including 330 native tests, 729 UI checks, paged media
and accepted form/inline neighbors. Preserve the failed gate as
`temp/wpt-m41-first-required-aggregate.log` and the detailed cases as
`temp/wpt-m41-gate-regressions-wpt-css-sizing-failures.json`.

The initial hypothesis was to substitute natural metrics while resolving the
intrinsic minimum. Independent controls refine the root cause: a definite
percentage must still resolve normally, and an orthogonal inline percentage
retains its definite fallback. The ordinary-flow entry already clears cyclic
percentage heights, but `layout_measure_form_control` bypasses that entry and
consumes a provisional percentage sample. The query consequently feeds a
smaller used height back into intrinsic contribution and minimum resolution.
CSS Sizing [§5.2.1](https://www.w3.org/TR/css-sizing-3/#cyclic-percentage-contribution),
CSS 2.1 §10.5 and CSS Writing Modes §7.3.1 require those distinct contexts;
forcing every query to a natural minimum would erase the definite cases.

Extract the existing flow reset as `layout_clear_cyclic_percentage_height` in
the percentage module and reuse it under the form measurement snapshot before
the actual formatter. Native controls intentionally do not admit the resolved
style cache, so the shared reset also consumes their existing deferred-percent
metadata. Preserve computed declarations, valid zero sizes,
algorithmically definite containing blocks and orthogonal fallback. This fixes
the query's used-value contract without a range special case or copied
percentage logic. A fifth native regression fails before the repair and covers
96 retained percentage/mode/decoration/nesting states, including query purity;
48 independent Chromium preferred/max-height observations confirm its expectations.
The max-height controls additionally expose the other query contract: when the
preferred input height is automatic and the caller explicitly asks for its
intrinsic content size, measuring the used height reintroduces `max-height`
while resolving `min-height:min-content`. That branch now uses the shared
natural query; ordinary contributions and authored preferred sizes retain the
used query. The raw-before controls record both defects without tolerance
changes. Repeat the affected sizing family and required
qualification after the shared repair stabilizes. Ownership remains
**D4.5.1v4 / D4.1.4v5**; no baseline or tolerance change is authorized or needed.

**M41 repaired focused outcome.** The shared percentage reset and the automatic
preferred-height intrinsic branch pass all **331/331** native tests
(`temp/wpt-m41-repaired-native-suite.log`). Five new regressions exercise
**798 retained states** in total. The affected accepted sizing family returns
to **776/776**, with no partials or regressions
(`temp/wpt-m41-repaired-neighbors.log`); the original gate failures are retained
as causal evidence rather than reclassified.

The final consolidated matrix includes the original 378 controls plus 48
percentage/intrinsic-constraint controls (`temp/wpt-m41-repaired-controls-input.json`,
`temp/wpt-m41-repaired-controls.json`, `temp/wpt-m41-repaired-controls-audit.json`).
All observations and case metadata are paired with the same independent browser
capture. Against qualified M40 it improves **135/426 → 332/426** size agreements,
with no newly failed size cases. The added constraint family improves
**10/48 → 48/48**; its preferred and maximum percentage sizes, definite/cyclic/
orthogonal contexts and decoration all agree. The other four families retain
the preceding table's results. The 94 remaining size disagreements are the
60 select cases, 16 HTML-button cases and 18 ordinary-span cases; widths/heights
alone do not qualify position or paint.

The repaired full inventory preserves **1,566/1,980** full geometry matches,
414 mismatches and zero execution errors
(`temp/wpt-writing-m41-repaired-final.json`). Its exact M40 delta remains eight
full gains, zero losses/reduced partials and 1,972 unchanged records
(`temp/wpt-m41-repaired-final-delta.json`). Dimension lint and diff checks pass.
The final code and executable are frozen again in
`temp/wpt-m41-candidate-code-state.json`; a second required Radiant aggregate
is running because the first gate exposed defects that required source changes.
M41 remains unqualified until that gate and the exact user command finish.

**M41 qualification and checkpoint.** The repaired required Radiant aggregate
exits 0 with **6,554 total, 6,412 full passes, 140 accepted partials, two
existing skips and zero failures** across 29 groups
(`temp/wpt-m41-required-aggregate.log`). UI automation passes 729/729, paged
media 504/504, page loads 111/111 and the native text/layout runner 331/331.
The accepted layout gate passes 3,158 full + 136 partial, including form
368 full + 10 partial, CSS inline 169/169 and CSS sizing 776/776. View passes
236/237 with the existing opt-in speech skip; the other skip is the existing
render baseline case (`temp/wpt-m41-required-view.log`). The bound remains
`LAMBDA_TEST_MAX_CONCURRENT=4`, with unchanged assertions and timeouts.

The exact user command `make layout suite=wpt-css-writing-modes` exits 0 and
reports **1,566 matches / 414 mismatches**, consistent with the canonical
inventory (`temp/wpt-m41-user-command.log`). All 21 code-file fingerprints and
the executable hash remain unchanged through the passing gate and exact
command. Dimension lint and final diff checks pass. The eight new geometry
matches are qualified without changing corpus, references, comparison policy
or expected failures. The first failed aggregate and both causal regressions
remain available in the preceding evidence records.

The qualified executable, 23 source/doc copies, patch and manifest are
`temp/wpt-m41-qualified.exe`, `temp/wpt-m41-qualified-source/`,
`temp/wpt-m41-qualified.patch` and `temp/wpt-m41-qualified-state.json`.
The snapshot records the final canonical delta, all 426 browser size controls,
five new regressions with 798 retained states, required gate and remaining
limits. Existing ownership rulings **D4.5.1v4 / D4.1.4v5** are unchanged.
This finite milestone is qualified. The long-running goal stays active with
414 raw writing-mode mismatches; the M42 ordinary-flow forced-line intrinsic
plan above is next, and no M42 probe or code has started.

### Milestone 42: forced text lines retain both intrinsic axes

**Verified starting point and scope.** M41's 23 source/doc fingerprints and CLI
hash match its qualified manifest. The previous goal turn made progress: it
qualified shared native-control, percentage and grid sizing with eight WPT
gains. The refreshed 60 dispatch controls retain native input 20/20, HTML
button 4/20 and ordinary span 2/20. The new 160-case independent flex/ordinary-
flow matrix distinguishes direct LF text, `<br>`, inline wrappers and atomic
icons (`temp/wpt-m42-lines-browser.json`, `temp/wpt-m42-lines-before.json`).
Ordinary flow agrees on all 120 direct LF/BR/wrapper controls; flex agrees in
only 22/120. Icon cases expose an independent ordinary-flow inline advance
difference and remain explicit controls, not a reason to flatten child boxes.

The owning defect is intrinsic measurement before flex assigns dimensions.
The single-text shortcut sends a raw preserved LF to the segment-width API
and synthesizes one block advance. Other paths substitute horizontal estimates
or native form fallback metrics for a vertical HTML button's ordinary DOM
content. Final ordinary-flow layout already preserves the forced columns.

**P1 outcome and design.** Share the existing processed direct-text/BR collector
and `layout_measure_text_intrinsic_lines` producer with a physical content-size
query. It measures max inline advance per forced line and sums the used line
block advances, preserving zero and normal leading, then projects once through
the element's writing mode. Reuse `intrinsic_element_line_height` and
`layout_measure_text_line_metrics`; add no glyph, whitespace or box-decoration
measurement loop. Flex's ordinary text items and HTML button natural sizing
consume that query. Authored sizes remain the later used-size constraint.
Do not classify native input labels or structured DOM as direct text.

This milestone delivers direct text and `<br>` forced lines across parallel/
orthogonal row and column flex, horizontal/vertical-lr/vertical-rl and auto/
authored inline sizes, plus retained label, leading, direction, mode and size
restoration. Wrappers/icons remain counterexamples that must retain their DOM
path and must not regress; full structured-inline intrinsic contributions are
the following milestone if this diagnosis holds. This is a producer/consumer
boundary, not a redefinition of the remaining conformance objective.

CSS Text [§§3–5](https://www.w3.org/TR/css-text-3/#white-space-property), CSS
Sizing [§5.1](https://www.w3.org/TR/css-sizing-3/#intrinsic-sizes), CSS Flexbox
[§§9.2/9.4](https://www.w3.org/TR/css-flexbox-1/#algo-main-item) and CSS Writing
Modes §7 govern the result; ownership stays **D4.5.1v4 / D4.1.4v5**.
Verification budget: one failing retained native case, focused browser and
query-purity controls, related accepted form/inline/sizing checks, full WPT
delta, dimension lint/diff, and one required Radiant aggregate after source
stabilizes. Preserve references, fixtures, tolerances and raw residuals.

**M42 implementation.** `layout_measure_direct_text_intrinsic_size` reuses the
existing direct text/BR normalization, logical forced-line widths and optional
used-line metrics. Its content result is physical; flex never transposes it
again. Equal min/max inline extents establish that the block advance cannot
depend on soft wrapping. Otherwise the query declines and the constrained-flow
path keeps ownership; pseudo content, multicolumn/containment and structured
inline descendants likewise retain their existing paths. This is a measurement
contract, not a DOM-label substitute. HTML button width and flex natural-size
consumers reuse the same result instead of native role fallback metrics.

The new retained regression fails on qualified M41
(`temp/wpt-m42-causal-before.log`). It exercises 384 live states across both
element kinds, direct LF/BR, parent axes, font leading including zero, label
edits, authored inline size, direction and mode restoration. Query assertions
also check natural dimensions independently of authored size and preserve
committed subtree geometry. Independent line-edge controls confirm larger/zero
leading and leading/terminal/intermediate empty lines
(`temp/wpt-m42-line-edges-browser.json`). Build is in progress; no qualification
claim yet. **D4.5.1v4 / D4.1.4v5** ownership is unchanged.

**M42 consumers and focused outcome.** The first candidate confirms that the
same physical result must reach both main and hypothetical cross sizing. The
remaining vertical auto-inline failures came from the cross-height estimate;
zero-leading column buttons also entered a positive-only fallback that used
text width as a height. Main/cross consumers now share the result, and the
duplicated HTML-button fallback is centralized so a measured zero stays valid.
The existing ratio and min/max finalizer still owns used-size constraints.

All **332/332** native tests pass, including the new 384 retained states and
natural-query/geometry-purity assertions
(`temp/wpt-m42-second-native-suite.log`). Browser size controls improve:

| Family | Qualified M41 | M42 candidate | Explicit limits |
|---|---:|---:|---|
| M41 native/minimum/grid/dispatch/percentage controls | 332/426 | 366/426 | Dispatch now 60/60; 60 select-height differences remain. |
| Direct LF/BR, wrapper and icon flex controls | 30/160 | 96/160 | All 80 direct LF/BR cases agree; 64 wrapper/icon flex cases remain. Ordinary flow stays 144/160. |
| Leading/terminal/intermediate empty lines and zero/larger leading | 17/42 | 42/42 | Ordinary flow remains 42/42. |

The paired audit `temp/wpt-m42-controls-audit.json` records **379/628 →
504/628** size agreements, with zero newly failed size cases and no ordinary-
flow reductions. These checks compare physical widths/heights, not positions
or glyph paint. Accepted neighbors pass form **368 full + 10 partial**, CSS
inline **169/169**, CSS sizing **776/776** (`temp/wpt-m42-neighbors.log`).

The full WPT inventory remains **1,566/1,980 matches**, 414 mismatches, zero
execution errors (`temp/wpt-writing-m42-candidate.json`). Every one of the
1,980 raw records is unchanged versus M41
(`temp/wpt-m42-candidate-delta.json`); do not report this milestone as WPT
score improvement. It removes a shared intrinsic-sizing defect and its
upstream failure dependencies still require further work. Code and CLI
fingerprints are frozen in `temp/wpt-m42-candidate-code-state.json`. Dimension
lint/diff pass. The required Radiant aggregate is running; M42 is unqualified
and the conformance goal remains active.

**Next priority / M43 investigation plan.** The current upstream inventory has
only four remaining documents with authored flex display: the compound
`direction-upright-002` test/reference, `sideways-lr-main-axis`, and
`percentage-padding-in-shrink-to-fit` (raw list:
`temp/wpt-m42-remaining-flex-forms.json`). The latter directly exercises the
percentage-padding and shrink-to-fit size dependency of an empty vertical flex
child in a horizontal float. Prioritize that general sizing contract next,
rather than expanding only the reduced wrapper/icon matrix without evidence
that it reaches these upstream failures. Structured-inline intrinsic sizing
remains open and retains its existing controls; it is not dropped from the goal.

P1 outcome: an empty shrink-to-fit float uses the correct intrinsic contribution
when a child has percentage padding, and later percentage resolution uses the
containing block's inline axis. Read CSS Writing Modes §7 dimensional mapping,
CSS 2.2 padding percentages and CSS Sizing cyclic percentage contributions;
trace intrinsic contribution → float used inline size → child percentage basis
→ final flex geometry. Refresh the exact upstream fixture against qualified
M42 and use a minimal horizontal/parallel/orthogonal control plus one retained
containing-size change to identify the first disagreement. Reuse shared
percentage/cascade/box helpers; no percent-value exception or empty-fixture fit.
Bound verification to the original case, causal controls, affected accepted
sizing/position neighbors, canonical delta and one required Radiant aggregate
after stable source. Ownership stays **D4.5.1v4 / D4.1.4v5**. No M43 probe or
code has started; qualify M42 first.

**M42 qualified checkpoint.** The required Radiant aggregate exits 0: **6,555
total, 6,413 full passes, 140 accepted partials, two existing skips, zero
failures**, across 29 groups (`temp/wpt-m42-required-aggregate.log`). Its native
runner passes 332/332; accepted layout categories, 729 UI cases and 504 paged
cases pass. View has its existing opt-in speech skip; rendering retains the
other existing skip (`temp/wpt-m42-required-view.log`). Assertions, timeouts
and the concurrency bound remain unchanged.

The exact user command exits 0 with **1,566/1,980 matches and 414 mismatches**
(`temp/wpt-m42-user-command.log`). The final canonical delta has all 1,980
records unchanged (`temp/wpt-writing-m42-final.json`,
`temp/wpt-m42-final-delta.json`). The 22 code-file fingerprints and CLI hash
remain unchanged through qualification; lint and diff checks pass. The
qualified executable, 24 source/doc copies, patch and manifest are
`temp/wpt-m42-qualified.exe`, `temp/wpt-m42-qualified-source/`,
`temp/wpt-m42-qualified.patch` and `temp/wpt-m42-qualified-state.json`.
This closes the direct forced-line sizing milestone, not the conformance goal.
The goal stays active; investigate the upstream percentage-padding dependency
under M43 next. No M43 probe or code has started, and ownership remains
**D4.5.1v4 / D4.1.4v5**.

### Milestone 43: cyclic percentage padding in shrink-to-fit contributions

Verified M42 source/doc and executable fingerprints match its qualified
manifest. The previous turn made progress by delivering and qualifying shared
forced-line intrinsic sizing; the remaining raw upstream inventory is still
414. The exact selected upstream fixture reproduces a 23,680px-wide float and
a 473,600px child instead of zero-sized empty boxes
(`temp/wpt-m43-upstream-before.json`).

The first disagreement is intrinsic padding contribution: a child of a measured
auto-sized shrink-to-fit parent retains percentage padding resolved against a
provisional containing size. `intrinsic_horizontal_box_edges` only recognizes
an explicit negative context width as cyclic, while the intrinsic width-size
consumer already recognizes auto shrink-to-fit DOM parents. The old used bound
therefore enters the parent's intrinsic width, and final percentage resolution
amplifies the wrong width. Do not zero all padding or clamp the resulting box.

P1 design: share the existing cyclic-contribution classification with padding
edge measurement and its cache basis. Re-evaluate winning padding declarations
through the existing math/substitution/box helpers with the cyclic percentage
term at zero; retain fixed calc terms and borders. Preserve definite bases and
resolve final padding using the containing block's inline axis after its size
exists. CSS Writing Modes §7.2, CSS Sizing §5.2.1 and CSS 2.2 §8.4 govern the
behavior; ownership remains **D4.5.1v4 / D4.1.4v5**.

Acceptance: the original upstream case; percentage and calc padding with empty
and fixed-content children; horizontal float/inline-block shrink-to-fit parents
and definite controls; parallel/orthogonal child writing modes; and one retained
parent-size/child-size restoration. The independent 216-case matrix also keeps
vertical parents as counterexamples (`temp/wpt-m43-padding-browser.json`,
`temp/wpt-m43-padding-before.json`): 64 currently agree, including all 24
horizontal definite controls, while all 48 horizontal float/inline-block cases
fail. Use these to challenge the selected cause, without expanding into
unrelated flow defects. Verification budget is the original/causal cases,
relevant accepted sizing/position neighbors, canonical delta, lint/diff and one
required Radiant aggregate after stable source. No engine edit yet.

**M43 implementation and second consumer.** The intrinsic query now classifies
a measured auto/intrinsic horizontal inline-size parent as an indefinite
percentage basis and includes that distinction in its cache slot. The existing
auto shrink-to-fit classification is shared with padding edges. Cyclic edges
are re-read from winning declarations rather than reused from a prior bound;
both physical padding axes preserve fixed terms and borders. The existing
zero-percentage math resolver is shared by padding, margins and constraints.

The first candidate fully matches the original WPT and all 24 horizontal float
controls, with no newly failed cases in the 216 matrix. The retained test also
exposes a later consumer: inline-block finalization reopens the containing width
from the child's post-percentage overflow. Extend the existing cyclic-child
finalization policy to winning percentage padding. Child width adjustment must
preserve its own logical axis and the border/padding floor; a vertical child's
automatic block extent cannot be replaced by a horizontal inline constraint.
This completes the same intrinsic → used dependency rather than adding an
empty-box exception.

The new regression fails on M42 (`temp/wpt-m43-causal-before.log`) and covers
192 retained states with empty/fixed content, percentage/math padding, border,
float/inline-block parents, definite size edits and auto restoration. Its
geometry and intrinsic-query expectations are independently checked by 96
border/basis browser controls (`temp/wpt-m43-bordered-browser.json`). The second
build is in progress. No qualification claim yet; **D4.5.1v4 / D4.1.4v5** are
unchanged.

**M43 expression and range refinement.** The 192 positive-term retained states
pass after box dependency metadata uses the shared recursive percentage
detector, rather than recognizing only a plain percentage token. Negative
fixed terms expose a necessary range rule: resolving the percentage term at
zero can make a valid padding calculation negative. Padding retains its
nonnegative range in both intrinsic edge measurement and committed used style;
the shorthand now reuses the longhand spacing setter. Margins still allow
negative values. The expanded regression covers **288 retained states**, with
48 independently captured negative-term browser cases. No value-specific
condition or fitted dimension is added.

**M43 final focused evidence.** All **333/333** native tests pass, including
the new **288 retained states** (`temp/wpt-m43-final-native.log`). The 360
paired browser controls improve **136 → 256** size agreements, with zero new
failures (`temp/wpt-m43-controls-audit.json`). All **216 horizontal-parent**
controls agree, including every float/inline-block, definite basis, border and
positive/negative padding-math case. The 104 vertical-parent disagreements are
inherited and remain explicit; this milestone does not qualify those paths.
The size audit scores both parent and child widths/heights, not positions or
paint.

The canonical inventory improves to **1,567/1,980 full geometry matches**, 413
mismatches and zero execution errors (`temp/wpt-writing-m43-candidate.json`).
The exact M42 delta is one full gain
(`percentage-padding-in-shrink-to-fit.html`), one improved partial
(`baseline-with-orthogonal-flow-001.html`: elements 50% → 62.5%, text unchanged),
zero losses/reduced partials and 1,978 unchanged records
(`temp/wpt-m43-candidate-delta.json`). Accepted sizing 776/776 and position
139/139 pass (`temp/wpt-m43-neighbors.log`). Box geometry passes 17/17 with
the raw inventory helper (`temp/wpt-m43-box-inventory.json`); the baseline-only
scratch classifier cannot postprocess that category because it has no saved
baseline, and its postprocessing error is preserved in the neighbor log.
No test harness or reference was changed to address that utility mismatch.

Dimension lint/diff pass. The 22 code files and executable are frozen in
`temp/wpt-m43-candidate-code-state.json`; the required Radiant aggregate is
running with the established concurrency bound and unchanged assertions and
timeouts. M43 is not yet qualified; the overall goal remains active.

**Next priority / M44 plan.** Investigate the remaining upstream vertical
`progress`/`meter` documents as one native-widget sizing family. The plain HTML
fixtures contain an ordinary paragraph and one widget with authored writing
mode, so refresh their exact failing geometry against qualified M43 before
choosing a repair. Current source supplies UA em-based inline/block defaults
through replaced measurement; unlike text/range form controls these widgets
do not share the native form-role intrinsic producer. The first hypothesis is
that logical UA defaults reach a physical replaced-size consumer without the
proper axis projection. Do not transpose image/video/SVG natural dimensions or
replace authored physical width/height.

P1 acceptance: the exact meter/progress native and appearance-none vertical
fixtures and horizontal neighbors, current/default em metrics, authored
physical/logical dimensions, and one retained font/writing-mode/size restoration.
Trace UA defaults → current intrinsic facts → inline/replaced used box before
editing. Inspect the paint consumer and keep any independent fidelity gap
explicit; geometry matching does not by itself qualify value/fill painting.
Use HTML rendering rules and CSS Writing Modes dimensional mapping; existing
ownership stays **D4.5.1v4 / D4.1.4v5**. Bound verification to causal native and
browser controls, accepted form/sizing neighbors, the full WPT delta, lint/diff
and one required Radiant aggregate after stable source. Compound ruby/table,
sideways progression, structured-inline measurement and script-report DOM
remain open, with their previous raw evidence retained. No M44 probe or code
has started; qualify M43 first.

**M43 qualified checkpoint.** The required Radiant aggregate exits 0 with
**6,556 total, 6,414 full passes, 140 accepted partials, two existing skips and
zero failures** across 29 groups (`temp/wpt-m43-required-aggregate.log`). Its
native runner passes 333/333, UI automation 729/729, paged media 504/504 and the
accepted layout gate 3,158 full + 136 partial. View retains the existing opt-in
speech skip; rendering has the other existing skip
(`temp/wpt-m43-required-view.log`). No assertions, timeouts or comparison
policies changed.

The exact requested command exits 0 with **1,567 matches / 413 mismatches**
(`temp/wpt-m43-user-command.log`). The final canonical inventory and M42 delta
are `temp/wpt-writing-m43-final.json` and `temp/wpt-m43-final-delta.json`: one
full gain, one improved partial, zero losses/reduced partials, 1,978 unchanged,
zero execution errors. All 22 code-file fingerprints and the executable hash
remain unchanged through qualification; dimension lint and diff checks pass.
The qualified executable, 24 source/doc copies, patch and manifest are
`temp/wpt-m43-qualified.exe`, `temp/wpt-m43-qualified-source/`,
`temp/wpt-m43-qualified.patch` and `temp/wpt-m43-qualified-state.json`.

This closes the measured horizontal shrink-to-fit percentage-padding contract,
including fixed math terms, borders, final percentage resolution and padding's
used range. The inherited vertical-parent controls and remaining upstream
failures stay open. Ownership remains **D4.5.1v4 / D4.1.4v5**. The full goal is
active; next is the M44 progress/meter sizing investigation, with no M44 probe
or code started yet.

### M44 — Logical UA defaults for meter/progress sizing

**Priority and finite contract.** Complete the already planned native-widget
sizing milestone before independent structured-inline and ruby/table work.
M43's 24 source/document hashes and CLI hash were revalidated. The previous
turn made progress: the qualified cyclic-padding change gained one upstream
full match and improved one partial result. The goal remains active.

**Diagnosis and design.** The twelve exact horizontal/vertical meter/progress
fixtures reproduce six horizontal matches and six vertical failures
(`temp/wpt-m44-upstream-before.json`). A vertical meter remains 80×16 instead
of 16×80; progress remains 160×16 instead of 16×160. The first disagreement is
in UA sizing, before inline line-height propagation. HTML Rendering
[§15.5.14–15](https://html.spec.whatwg.org/multipage/rendering.html#the-meter-element-2)
defines em-based inline/block dimensions; CSS Writing Modes
[§7.2](https://www.w3.org/TR/css-writing-modes-3/#dimension-mapping) maps these
onto physical axes using the widget's own writing mode. Current initial and
post-cascade UA producers, replaced measurement fallback and intrinsic-width
fallback each assume horizontal writing. Extract one shared UA physical-size
producer from these repeated shapes, then let the existing authored physical/
logical-size cascade continue to win. Preserve image/video/SVG natural axes,
explicit auto values, and existing selected-view/style ownership
(**D4.5.1v4 / D4.1.4v5**); no new payload or formal ruling is needed.

**Acceptance and verification budget.** Independently captured 192 Chromium
controls vary widget, three writing modes, native/none appearance, 20/32px
computed fonts and eight authored/default size combinations
(`temp/wpt-m44-widgets-browser.json`). Use these same sources with predecessor
and candidate; qualify the exact upstream failures and horizontal neighbors,
a retained font/mode/size-restoration regression, focused intrinsic-query
purity, accepted sizing/form neighbors and the canonical 1,980-file delta.
Run dimension lint/diff checks and one required Radiant aggregate after the
source stabilizes. Inspect painting independently: no meter/progress value/fill
paint implementation was found among the render/paint consumers. This
milestone's evidence establishes sizing, not a rendered gauge or its direction;
that paint gap stays explicit rather than claiming full widget support.

**M44 first-candidate evidence and refinement.** All twelve exact upstream
fixtures now match; the 192 fresh widget controls improve from 88 to 192
agreements. The retained regression still exposes stale automatic height:
`ReplacedIntrinsicFacts` reads the previous used border box after a font or
size mutation. For CSS-sized widgets these are not natural dimensions. Repair
that producer to read the current resolved CSS axes (converted to content
sizes), with the shared UA fallback only for an unoverridden dimension. Keep
natural metadata and all other replaced tags unchanged. Remove the now
redundant fallback from `layout_measure_replaced` rather than repairing the
same cause separately in the positioned caller.

An additional independent 72-case inline/flex/grid parent capture improves
from 16 to 48 size agreements. All 24 remaining disagreements are ordinary
inline parents missing HTML's -0.2em widget vertical alignment; the widget's
own sizes are already correct. Apply that UA default before the cascade and
refresh its em offset from the computed font only when no author alignment
wins. These refinements close the selected default-sizing/retained-state
contract rather than fitting upstream dimensions. See
`temp/wpt-m44-first-focused.log` and `temp/wpt-m44-flow-first.json`.

**M44 second-candidate boundary checks.** The 432 retained states now pass,
including restored fonts and absent/explicit/automatic physical or logical
sizes. The central facts producer no longer treats prior used widget geometry
as natural metadata. Fresh widget controls remain 192/192; thirty independently
captured auto/initial/unset/revert/inherit controls improve from 8 to 30 matches.
The standard cascade already removes reverted author declarations before the
shared UA producer reads them.

Decorated controls reveal a separate representation mismatch in the positioned
consumer: content-box intrinsic height is passed to an inset solver that later
subtracts border-box edges. Convert that result with the existing shared CSS
box-size helper before the final conversion, for all replaced objects using
that path. A retained 48-state border/padding/content-size regression plus six
authored-alignment/restoration states covers the cause. Natural widget metadata
must remain absent. The nonnegative content floor and ordinary image/iframe/SVG
behavior remain part of neighboring regression coverage.

The HTML baseline default improves intrinsic-parent control agreement further
to 56/72. The sixteen remaining vertical-widget inline-parent heights differ
by 1px at 20px font and about 1.61px at 32px font; widget dimensions already
agree. Retain this fractional parent-line/descent discrepancy as a separate
line-metrics limit, not a widget size correction or a fitted offset. These raw
controls use 0.1px size agreement, independently of the unchanged WPT comparator.

**M44 stable focused result.** All **335/335 native tests pass**, including the
two new regressions' 432 default/cascade states, 48 decorated facts states and
six authored-alignment/restoration states (`temp/wpt-m44-final-native.log`).
The causal predecessor and intermediate failures remain in
`temp/wpt-m44-causal-before.log`, `temp/wpt-m44-first-focused.log` and decorated
control captures. Both new tests pass together after the final conversion
(`temp/wpt-m44-third-focused.log`). Query reads preserve committed geometry;
current CSS dimensions, not old fragment sizes, determine widget facts.

The final independent browser control audit reports **152 → 326 agreements
out of 342**, with **zero new size failures**: widgets 88→192/192, intrinsic
parents 16→56/72, decorated/zero controls 40→48/48 and keywords 8→30/30.
All widget dimensions agree; the sixteen parent-line differences described
above remain. Sources, paired observations and audit are
`temp/wpt-m44-final-controls-input.json`, `temp/wpt-m44-final-controls.json`
and `temp/wpt-m44-controls-audit.json`. Positions and painting are outside
this control-size comparison.

The canonical WPT candidate reports **1,573/1,980 full matches**, **407
remaining**, zero execution errors: six full vertical meter/progress gains,
two computed-style report geometry improvements (0→50%), no full losses or
reduced partials and 1,972 unchanged records. Both improved report documents
remain partial; do not count them as CSSOM/script-report conformance.
See `temp/wpt-writing-m44-candidate.json` and
`temp/wpt-m44-candidate-delta.json`. Accepted sizing (776/776), positioned
(139/139) and raw box (17/17) neighbors pass. Dimension lint and diff checks
pass. The 23 code-file/CLI fingerprints are frozen in
`temp/wpt-m44-candidate-code-state.json`. The required Radiant aggregate is
next; M44 is not yet qualified. Ownership remains **D4.5.1v4 / D4.1.4v5**,
and no corpus, references, tolerances, assertions or skips have changed.

**Next priority / M45 plan.** Investigate ordinary `select` automatic sizing
with `appearance:none` next: three actual upstream documents remain partial
(horizontal 75%, vertical-lr/rl 25%), while the corresponding native horizontal
and vertical documents already match. These are plain paragraph/widget cases,
not test-report DOM, and the same option sizing serves common page controls.
Refresh their exact box/font differences from qualified M44 before editing.
Current `calc_select_size` forces appearance-less combo option contributions
to min-content and includes historical theme-overhead heuristics. Determine
the first disagreement and which parts belong to CSS intrinsic measurement,
UA decoration or the native theme; do not replace one fitted constant with
another. Reuse the option measurement, logical-axis and shared form box helpers.

Finite acceptance: the three source failures and native neighbors, options
with spaces versus an unbreakable label, authored versus default fonts and
physical/logical sizes, plus retained option/font/appearance restoration. Check
HTML select rendering and CSS UI appearance behavior against independent
browser controls, and inspect existing select painting's appearance flag.
Keep selected-view/style ownership **D4.5.1v4 / D4.1.4v5**. Budget causal native
and browser checks, affected accepted form/sizing neighbors, canonical WPT
delta, lint/diff and one required Radiant aggregate after stable source. The
sixty previously recorded native select block-height controls are related
evidence, not automatic permission to expand this milestone into every theme
combination; address them here only if the same root cause owns them.

Meter/progress value/fill painting, sixteen fractional inline-parent metric
controls, structured inline/soft wrapping, vertical-parent percentage cycles,
compound table/ruby, sideways progression and script-report DOM remain open.
No M45 probe or engine change has started. Qualify M44 before starting it.

**M44 qualified checkpoint.** The required Radiant aggregate exits 0 with
**6,558 total, 6,416 full passes, 140 accepted partials, two existing skips and
zero failures** across 29 groups (`temp/wpt-m44-required-aggregate.log`).
Native text/layout is 335/335, UI 729/729, paged media 504/504 and accepted
layout 3,158 full + 136 partial, including form 368 full + 10 partial and inline
169/169. View retains its opt-in speech skip; rendering retains its existing
skip (`temp/wpt-m44-required-view.log`). No gate policies, timeouts or
assertions changed.

The exact requested command exits 0 with **1,573 matches / 407 mismatches**
(`temp/wpt-m44-user-command.log`). The canonical final inventory and M43 delta
are `temp/wpt-writing-m44-final.json` and `temp/wpt-m44-final-delta.json`:
six full gains, two partial improvements, zero losses/reduced partials and
zero execution errors. All 23 code-file fingerprints and the executable hash
remain unchanged through qualification. The qualified CLI, 25 source/document
copies, patch and manifest are `temp/wpt-m44-qualified.exe`,
`temp/wpt-m44-qualified-source/`, `temp/wpt-m44-qualified.patch` and
`temp/wpt-m44-qualified-state.json`.

This closes the selected logical widget UA sizing and retained-data milestone,
including author overrides, absent natural dimensions and decorated positioned
content-size conversion. It does not qualify widget painting or erase the
sixteen fractional inline-parent differences. Ownership remains
**D4.5.1v4 / D4.1.4v5**. The full goal remains active with 407 raw geometry
mismatches. Next is M45 appearance-less select sizing; no M45 probe or code has
started.

### M45 — Primitive select intrinsic sizing uses the complete label

**Entry and finite contract.** Revalidated all 25 M44 source/doc hashes and its
CLI fingerprint. The previous goal turn made progress: six full WPT gains,
two partial improvements and passing required qualification. M45 follows the
planned ordinary select sizing priority. The five exact source fixtures
reproduce native H/V matches and three primitive failures: the horizontal
select is 39.3×19 instead of 53×17; the vertical selects transpose the same
wrong logical dimensions (`temp/wpt-m45-upstream-before.json`).

**Root cause and design.** Appearance-none combo layout measures the longest
word rather than the full option, then reuses a native-only minimum and omits
the normal border from its inline intrinsic size. An early intrinsic-query
exception also suppresses the select's minimum box edges, and flex has another
select-width recipe. Repair the shared shape: one content-inline producer
rounds the maximum full label and adds only native arrow space; the used form
producer adds the actual logical padding/border exactly once. Remove the
appearance-specific min-content/edge workaround and let flex consume the
central form result. Keep the existing native theme arrow size, not a newly
fitted constant. Primitive block sizing uses normal text metrics, with the
native minimum retained only for native appearance. Physical/logical authored
sizes still override the intrinsic box after its one axis projection.

[CSS UI 4 §7.2](https://www.w3.org/TR/css-ui-4/#appearance-switching) distinguishes
primitive appearance from native decoration while preserving widget semantics
and ordinary CSS styles. [HTML Rendering §15.5.16](https://html.spec.whatwg.org/multipage/rendering.html#the-select-element-2)
provides select's default preferred-size role. Chromium's own menu-list sizing
code independently confirms full-label measurement and separate CSS/internal
padding; it is diagnostic evidence, not a copied implementation or a replacement
for those standards. Selected-view/style ownership remains **D4.5.1v4 /
D4.1.4v5**; no formal ruling changes or new lifetime payloads are required.

**Evidence and verification budget.** Captured 144 independent controls varying
H/VLR/VRL, native/none, default/20px Ahem fonts, short/spaced/unbreakable labels,
and auto/physical/logical/percentage inline sizes. M44 matches 90/144
(`temp/wpt-m45-select-browser.json`, `temp/wpt-m45-select-before.json`). A
separate line probe shows Ahem's ordinary 20px line but Chromium's primitive
select at 24px including a 2px border; M44 is 22px. This is the inherited
select internal-content/theme-height gap, not permission to add a fitted 2px
term. Keep that residual explicit unless the same owning rule explains it.
Default Arial's primitive select uses its ordinary 15px line plus 2px border.

Acceptance includes the three original failures and native neighbors; complete
option labels, short/empty labels and removed/authored box edges; default and
authored fonts; physical/logical axes; retained label/font/appearance changes;
and shared flex/grid intrinsic consumers. Use causal native checks, unchanged
browser controls plus edge/parent counterexamples, affected accepted form/sizing
neighbors, canonical WPT delta, dimension lint/diff and one required Radiant
aggregate after stable source. Select painting already guards its native arrow
with `appearance_none`; this sizing change does not qualify vertical label or
popup paint. Other M44 backlog families remain open.

**M45 first result and label refinement.** All five exact upstream fixtures
match after the shared content/edge repair. Fresh controls improve 90→108/144;
the 36 remaining cases retain the Ahem block-height gap. Empty/removed/authored
edge controls improve to 36/36. Parent controls reach 24/36; inspect their
remaining native-height differences separately from label width. The retained
label-attribute mutation test still fails because both sizing and the painter
ignore a nonempty option `label`. Add one borrowed-label selector in the
existing view/select module, reuse it in the full-label measurement and existing
paint text accessor, and preserve normalized DOM-text fallback. This directly
aligns the label being measured with the label displayed, with no new storage
or ownership change (**D4.5.1v4 / D4.1.4v5**).

A Chromium UA-shadow inspection (`temp/wpt-m45-shadow-browser.json`) further
narrows the height residue: native inner boxes have 1px block padding per side;
primitive ones have zero. The default Arial inner line is 15px, but the Ahem
20px select inner line is explicitly 22px despite the ordinary 20px text line.
These current browser UA metrics do not justify a universal added constant or
a font-name condition. Keep the unresolved content-line/theme distinction
visible while qualifying the complete-label and CSS-box contract.

**M45 retained cascade refinement.** Shared label selection removes most of
the retained failures. The remaining cases expose previous author padding
surviving removal: the select UA producer restores borders and font but leaves
its zero padding implicit. Explicitly restore logical zero UA padding for
native/primitive selects before the author cascade, using the existing logical
padding helper; base-select retains its separate defaults. This is a cascade
restoration fix, not a width adjustment. The same retained test alternates
zero, authored and absent edges and detects the stale input. The temporary
first/second focused logs retain both causal failures.

The 36 intrinsic-parent capture must be classified accurately: all twelve
remaining cases are grid cases (including a fractional used height and a
vertical logical-block contribution), not a uniform native theme offset.
There are no new size failures versus M44 in that family. Keep this grid
contribution residue separate; row/column flex now consume the shared form
result and all their captured sizes agree.

**M45 stable focused evidence.** All **336/336 native tests pass**, including
the new regression's **981 retained states**: 972 option-label/font/appearance/
box-edge/size combinations plus nine default-font block-size restoration
states (`temp/wpt-m45-final-native.log`). Measurement queries preserve committed
geometry. The same displayed-label accessor used by painting is checked against
the current attribute on every label state. A 200×80 raster equivalence check
finds all 16,000 pixels identical between label-attribute and equivalent DOM-text
controls, with 438 visible differing pixels against a fixed-size empty control
(`temp/wpt-m45-paint-audit.json`). This checks horizontal label painting; it does
not qualify vertical glyph orientation or popup fidelity. The initial render
invocation used an unsupported viewport option; rerunning with the documented
`--viewport-width`/`--viewport-height` completed normally.

Independent unchanged browser captures improve **108→168/228 full size
agreements**, zero newly failing cases (`temp/wpt-m45-controls-audit.json`).
The ordinary select matrix improves 90→108/144; all 36 remaining cases retain
the Ahem internal-height difference. Flex/grid parents improve 6→24/36, with
all twelve remaining cases confined to grid. Empty/authored/removed edges
improve 12→36/36. All twelve label-attribute inline widths now agree (six before),
but their full size comparisons still retain the Ahem height difference, so
count none of them as full size matches. No tolerances or captures changed.

The canonical inventory reports **1,576/1,980 full geometry matches**, **404
mismatches**, zero execution errors. The three full gains are the primitive
select horizontal/vertical-lr/vertical-rl documents; 1,977 records are unchanged,
with no losses or reduced partials (`temp/wpt-writing-m45-candidate.json`,
`temp/wpt-m45-candidate-delta.json`). Accepted form remains 368 full + 10 partial,
and sizing 776/776, with zero failures (`temp/wpt-m45-neighbors.log`). Dimension
lint and diff checks pass. The 24 code-file and CLI hashes are unchanged through
these checks (`temp/wpt-m45-candidate-code-state.json`). M45 awaits its required
Radiant aggregate; the full conformance goal remains active. Existing ownership
is unchanged (**D4.5.1v4 / D4.1.4v5**).

**M45 height root cause established before closing.** A causal browser font
restoration changes primitive Ahem height 24→22 while preserving its width;
native height remains 24 (`temp/wpt-m45-font-readiness-browser.json`). The
initial primitive capture retained a pre-webfont inner line-height. Do not
rewrite those captures or count them as passes. Separate font-ready captures
preload the face before creating the select, and preserve the exact resulting
DOM for native comparison (`temp/wpt-m45-font-metrics-browser.json`).

Together with the UA-shadow inspection and Chromium's menu-list block-size
producer, this identifies the owning model: the selected primary font's cell
(ascent + descent, without CSS normal leading), plus actual native inner block
padding, plus author/UA CSS box edges. The existing native minimum of 17px
content only happened to represent default Arial; it fails as font size changes.
Model the native inner padding as a named UA-theme default (1px per block side,
observed directly as padding), with primitive appearance zero. It is not an
added geometric residual or a font-specific correction. Reuse the existing
font content-area metric helper; preserve base-select's separate legacy path.

This is the same block-size cause admitted by M45's plan and should be repaired
here, including the sixty earlier native select controls, rather than left as
unexplained debt. The first required aggregate is still running on the prior
unchanged candidate. Let it terminate before replacing binaries; after the
height implementation, rerun affected checks and a final required aggregate.
That rerun is justified by a newly established shared sizing cause, not by an
unchanged gate result. Existing control captures and first-candidate logs stay
available, and all baseline policies remain unchanged.

**M45 height implementation.** The first aggregate completed with 6,417 full
passes / 6,559 total, 140 partial, two existing skips and zero failures. Its
unchanged CLI and code hashes are preserved as `temp/wpt-m45-before-height.exe`
and `temp/wpt-m45-before-height-code-state.json`. The extended 1,305-state
retained regression fails before the height edit
(`temp/wpt-m45-height-causal-before.log`), including a 1px Ahem counterexample
against the old minimum.

The revised non-base combo producer reads the existing `font_get_cell_height`
helper rather than CSS normal leading. Native appearance adds the named UA
inner block padding (scaled as a CSS length with zoom); primitive appearance
does not. The observed 1px inner padding is a platform-theme policy value,
separate from author CSS padding/border and from a fitted residual. Base-select's
previous separate height behavior is preserved. Existing broader select
baseline publication and vertical glyph/popup painting remain outside this
height contract; final qualification must still detect any affected geometry.

**M45 final height-focused result.** All **336/336 native tests pass** after the
height refinement, including the extended **1,305 retained states**
(`temp/wpt-m45-final-height-native.log`, `temp/wpt-m45-height-focused.log`).
The font-ready matrix matches **36/36**, including Arial leading and 1px Ahem.
All **60/60** inherited M41 native-select cases now match, in both flex and
ordinary flow, versus 0/60 for M44 and the pre-height candidate. This closes
the previously recorded sixty-case native block-height discrepancy for those
captured contexts, without changing their sources or observations.

The final paired audit is **111→288/324 full size agreements**, zero new size
failures (`temp/wpt-m45-final-height-controls-audit.json`). Breakdown: ordinary
selects 90→126/144, parents 6→24/36, edges 12→36/36, label attributes 0→6/12,
font-ready cases 3→36/36 and inherited native selects 0→60/60. The remaining
36 cases are twelve grid contributions and 24 primitive initial-webfont height
captures whose causal font-restoration evidence is retained. They are still
recorded as mismatches; no capture, threshold or baseline policy was changed.

The refreshed 1,980-file inventory remains **1,576 full / 404 mismatches**,
zero execution errors, with exactly the same raw records as before the height
refinement. The M44 delta remains three full gains, zero losses/reduced partials
and 1,977 unchanged (`temp/wpt-writing-m45-height-candidate.json`,
`temp/wpt-m45-height-candidate-delta.json`,
`temp/wpt-m45-height-refinement-delta.json`). The horizontal raster equivalence
still passes with all 16,000 pixels identical and 438 visible label pixels
against the empty control (`temp/wpt-m45-final-paint.log`). Final dimension
lint and diff checks pass. The current 24 code-file/CLI hashes are frozen in
`temp/wpt-m45-candidate-code-state.json`; the final required aggregate must
qualify this newer binary. The first passing gate remains separately recorded.
Ownership stays **D4.5.1v4 / D4.1.4v5**, and the full goal remains incomplete.

**Next priority / M46 plan.** Return to a plain upstream orthogonal-flow failure
rather than extending the now corrected native select theme cases. The pair
`sizing-orthog-htb-in-vlr-004.xht` / `004-ref.xht` still has 0% geometry and text
agreement, while the vertical-rl `004.xht` neighbor is already a full match.
The VLR pair programmatically sets the root `scrollLeft` to `scrollWidth`;
it has no testharness report scaffolding. Establish whether the first wrong
state is orthogonal automatic width, overflow extent, CSSOM root scrolling or
viewport-relative capture coordinates before choosing a code owner. This has
higher dependency value for real vertical pages than the isolated select/grid
control residue, and the existing baseline must remain unchanged.

Finite acceptance: those exact VLR documents, the accepted VRL neighbor, a
source-derived unscripted control separating size from scrolling, definite/
automatic orthogonal containing widths and retained root scroll restoration.
Use CSS Writing Modes orthogonal sizing and CSSOM View scrolling/coordinate
rules; reuse existing axis and viewport-state helpers. Preserve existing
selected ownership (**D4.5.1v4 / D4.1.4v5**) and precise roots if a DOM/script
owner is involved. Budget causal browser/native geometry and any affected DOM
checks, focused accepted layout neighbors, canonical WPT delta and one final
Radiant aggregate after stable code; add the required core/DOM gate if the fix
changes that source area. Do not expand this into test-report DOM or unrelated
font precision. Grid select contributions, initial-webfont browser controls,
compound ruby/table, structured inline/soft wrapping, sideways flow and broader
control baselines/paint remain separately recorded. No M46 probe or engine
edit has started; finish M45 qualification first.

**M45 qualified wrap-up checkpoint.** The final required Radiant aggregate
exits 0 with **6,559 total, 6,417 full passes, 140 accepted partials, two
existing skips and zero failures** across 29 groups
(`temp/wpt-m45-final-required-aggregate.log`). Native text/layout is 336/336,
UI automation 729/729, paged media 504/504 and accepted layout 3,158 full +
136 partial. Form remains 368 full + 10 partial, sizing 776/776 and inline
169/169. View retains its opt-in speech skip; rendering retains the other
existing skip (`temp/wpt-m45-required-view.log`). The earlier passing aggregate
covered the prior height implementation; the final gate verifies the refined
font-cell/inner-padding producer. No assertions, timeouts or baseline policies
changed.

The exact requested command exits 0 with **1,576 matches / 404 mismatches**
(`temp/wpt-m45-user-command.log`). The final canonical inventory and M44 delta
are `temp/wpt-writing-m45-final.json` and `temp/wpt-m45-final-delta.json`:
three full gains, zero losses/reduced partials, 1,977 unchanged records and
zero execution errors. All 24 code-file and CLI fingerprints remain unchanged
through final qualification and the exact command. Dimension lint and diff
checks pass. The qualified CLI, 27 source/document copies, patch and manifest
are `temp/wpt-m45-qualified.exe`, `temp/wpt-m45-qualified-source/`,
`temp/wpt-m45-qualified.patch` and `temp/wpt-m45-qualified-state.json`.

This closes complete-label and actual-box-edge select sizing, retained UA
padding restoration, shared label selection for horizontal painting, and the
selected font-cell/native-inner-padding contract. The sixty prior native
select size controls now all match. The twelve grid controls, 24 original
primitive initial-webfont captures, broader baselines/vertical painting and
the remaining upstream failures stay open. Ownership remains **D4.5.1v4 /
D4.1.4v5**. Work stops at the user's wrap-up request; no active goal remains
registered, and the broader objective is not complete. M46 is only a recorded investigation plan,
with no probes or engine edits started and no qualification processes left
running.

### M46 — Horizontal viewport range uses committed root geometry

**Entry and finite contract.** M45 is committed and merged; the checkout at
`4378c71d8` starts clean. All M45 engine/test source hashes remain unchanged;
the support matrix has unrelated merged updates. Rebuilt the merged CLI and
native runner before reproducing. The exact VLR `004` source and reference
still fail every geometry/text record, while the VRL neighbor still matches.
Fresh browser captures (`temp/wpt-m46-browser.json`) separate size from scroll:
all three documents have the correct used sizes. VLR and its horizontal
reference have a 1,400px scrolling area in a 1,200px viewport and scroll 200px;
Radiant moves VLR only 100px and the reference zero. Orthogonal sizing is not
the first disagreement.

**Cause and design.** `finalize_block_flow` computes an element range against
the provisional root border box. Root finalization then grows vertical auto
width, including body margins, but only has a separate *vertical* viewport
range producer. Explicit horizontal root width is similarly compared with
itself instead of the viewport. Add the horizontal viewport finalization after
committed root geometry, without changing its used size or adding offsets.
Reuse descendant bounds, declared root/body overflow propagation, resolved
writing mode/direction and the existing signed StateStore range setter. Preserve
vertical range handling and selected ownership (**D4.5.1v4 / D4.1.4v5**).

[CSSOM View §2](https://www.w3.org/TR/cssom-view/#scrolling-area) defines the
viewport scrolling area's edges from the initial containing block and
descendants; its root `scrollLeft` delegates to the viewport.
[CSS Overflow 3 §3.3](https://www.w3.org/TR/css-overflow-3/#overflow-propagation)
governs root/body overflow propagation. No formal ruling changes are needed.

**Acceptance and budget.** Exact VLR pair and accepted VRL neighbor; independent
unscripted/scroll-to-end controls with auto/definite orthogonal parent sizes;
an oversized authored horizontal root; overflow removal/restoration and viewport
resize in one retained document. Validate signed VRL ranges independently from
the separate existing root CSSOM negative-setter limitation. Use focused native
geometry/state checks and browser controls during edits, accepted sizing/scroll
neighbors, unchanged canonical WPT delta, dimension lint/diff, and the required
Radiant aggregate once source is stable. DOM/runtime edits would add their
required gates; do not expand this range repair into unrelated CSSOM plumbing,
grid control contributions, capture font readiness or root used-height policy.

**Focused implementation evidence.** The new retained native regression fails
before the repair because the authored oversized horizontal root has no pane
(`temp/wpt-m46-causal-before.log`). Horizontal finalization now runs after root
geometry, reuses the existing descendant-bounds and viewport-overflow producers,
and writes the signed range through StateStore. The overflow producer has been
renamed from its iframe-specific name and reused by all existing callers;
there is no duplicated root/body cascade recipe. Final viewport application
stores the clamped request so growth/removal cannot restore a stale offset.

All nine focused orthogonal/scroll checks pass, and the complete native runner
passes **337/337** (`temp/wpt-m46-first-focused.log`, `temp/wpt-m46-native.log`).
Thirty independent browser controls vary VLR/VRL, automatic/240px/600px
orthogonal parent widths, zero/37px margins, unscripted/scripted scroll, and
authored horizontal root width with visible/hidden/clip overflow. At identical
320×200 viewports, full x/y/width/height agreements improve **22→30/30**, zero
new failures (`temp/wpt-m46-controls-audit.json`). Original source/metadata are
paired exactly. An initial scratch invocation used the generic adapter's
1,200×800 viewport; its output was discarded before this comparison. Captures,
production harnesses and tolerances remain unchanged.

The original VLR pair now matches all geometry/text records, with the accepted
VRL neighbor unchanged. Canonical WPT improves **1,576→1,578 / 1,980**, zero
execution errors and no full losses (`temp/wpt-m46-candidate-delta.json`). One
existing partial decreases: `sizing-orthog-htb-in-vrl-001-ref.xht` changes from
33% element / 25% text agreement to zero. Entry and candidate have identical
used boxes: its absolute table is already 1,744px wide rather than Chromium's
1,472px. Correct viewport subtraction now yields 544px scroll for that wrong
1,744px extent instead of the prior accidental 272px. Fresh Chromium agrees
with the original capture (`temp/wpt-m46-table-browser.json`); no reference
correction is justified. Keep this exposed absolute-table percentage/math
sizing defect open rather than capping the new range to retain accidental
coordinate matches. Root/body/table/row/cell dimensions remain unchanged.

Accepted sizing is **776/776**, positioned layout **139/139**. The scratch
neighbor classifier requires a non-null per-suite baseline; its attempted
`baseline` category has none and cannot produce an accepted verdict. Use the
required aggregate's actual baseline selection instead. Dimension lint and diff
checks pass. Six code/test hashes and the CLI fingerprint are frozen in
`temp/wpt-m46-candidate-code-state.json`. The required Radiant aggregate is
running on that source; do not replace its executables until it terminates.
Ownership remains **D4.5.1v4 / D4.1.4v5** and the full goal stays active.

**Required-gate blocker and bounded refinement.** The merged UI inventory's
`css_grid_capacity` fails all 24 assertions on the first candidate. Its root
has 24px used height, with a wide out-of-flow grid extending the horizontal
viewport range. Enabling the real scrollbar exposes a shared consumer bug:
`scrollpane_target` puts the horizontal hit strip at the root box's bottom,
covering the top-row buttons, rather than at the viewport bottom. Scrollbar
painting, thumb hit classification, page stepping and dragging likewise read
the root's used dimensions. Do not hide the scrollbar or grow the root box.

Extend M46 with one block-local scrollport rectangle: ordinary elements use
their border box, the top-level root uses the viewport and compensates its
document-box origin. Promote the existing top-level-viewport predicate rather
than copying its root/embedding classification. Reuse the rectangle across
scrollbar painting/targeting/press/page/drag consumers, deriving the viewport
thumb's content extent from the committed signed range. Add a small native
hit/paint regression for a short and relatively shifted root, preserve the
existing grid UI fixture, and rerun affected checks plus the final required
aggregate after source is stable. The first aggregate must finish before
replacing its executables. This is a required shared-scrollport correction,
not permission to weaken the UI fixture or special-case its buttons.

The first aggregate terminates with **6,609 total, 6,466 full passes, 140
accepted partials, two existing skips and one failure**: only
`css_grid_capacity` (`temp/wpt-m46-first-aggregate-summary.json`). Its isolated
replay confirms 0/24 assertions before the consumer repair. The first scrollport
consumer repair restores 24/24 assertions, without changing that fixture
(`temp/wpt-m46-grid-fixed.log`). Its new native pixel test still catches a
shifted-root mismatch: scrollbar geometry is viewport-relative, but the root
overflow clip is still anchored at the root's shifted border box. Promote the
rectangle to `layout_scrollport_rect` and use it in the root clip producer as
well as the scrollbar consumers. This preserves a single origin conversion,
without moving the root box or adding a paint-only correction. The final
candidate must pass the native hit/pixel and retained tests before qualification.

**Final focused candidate.** The shared clip/scrollport model passes the new
native pixel, hit and thumb tests for unshifted → shifted → restored roots,
plus the retained range/resize checks (`temp/wpt-m46-final-focused.log`). All
**338/338 native tests pass**. The five unchanged UI fixtures
`css_grid_capacity`, `root_scroller_shrink`, `scroll_root_explicit_height`,
`scroll_negative_range` and `test_state_store_scrollbar_interaction` pass
(`temp/wpt-m46-final-scroll-ui.log`), including the original 24 assertions.
Independent browser controls remain **30/30** and the canonical inventory
remains **1,578/1,980**, 402 mismatches, no execution errors
(`temp/wpt-m46-final-controls-audit.json`, `temp/wpt-m46-final-delta.json`).
The two full gains and exposed table partial are unchanged from the first
candidate. Final lint/diff checks pass. Ten code/test hashes and the newer CLI
are frozen in `temp/wpt-m46-candidate-code-state.json`; the first candidate's
hashes and binary remain separately preserved. The final required Radiant gate
is running, justified by the repaired gate failure and shared paint/input code.

**M46 qualified checkpoint.** The final required Radiant aggregate exits 0:
**6,610 total, 6,468 full passes, 140 accepted partials, two existing skips and
zero failures** across 29 groups (`temp/wpt-m46-final-required-aggregate.log`,
`temp/wpt-m46-final-aggregate-summary.json`). This includes native text/layout
338/338, UI 741/741, paged media 504/504 and accepted layout 3,158 full + 136
partial. Viewer checks are 243 full with the existing opt-in speech skip; the
other existing skip remains in rendering. The first failed aggregate is retained
as causal evidence and is superseded by this final passing gate.

The original VLR source/reference pair now fully matches, with its VRL neighbor
preserved. Canonical WPT is **1,578/1,980**, 402 mismatches, zero execution
errors: two full gains, zero full losses, one exposed existing table partial
decrease and 1,977 unchanged records. The table's used dimensions were wrong
before this milestone and remain wrong; correct viewport subtraction exposes
their larger scroll extent. No cap, fixture condition or comparator change was
introduced to preserve accidental partial matches. The exact user command and
final source fingerprints are recorded with the checkpoint below.

This closes horizontal viewport range finalization, retained clamp publication,
and the shared root scrollport for clip/scrollbar paint and input. Independent
browser geometry improves 22→30/30, the retained hit/pixel regression covers a
short and relatively shifted/restored root, and unchanged scrolling UI fixtures
pass. General root border/used-height contexts, negative root CSSOM setters and
the absolute-table percentage/math defect remain partial. Ownership stays
**D4.5.1v4 / D4.1.4v5**; no formal ruling or corpus-policy changes are needed.

The exact `make layout suite=wpt-css-writing-modes` exits 0 with 1,578
matches / 402 mismatches (`temp/wpt-m46-user-command.log`). The qualified CLI,
12 source/document copies, cumulative patch and manifest are
`temp/wpt-m46-qualified.exe`, `temp/wpt-m46-qualified-source/`,
`temp/wpt-m46-qualified.patch` and `temp/wpt-m46-qualified-state.json`.
Final source and CLI hashes remain unchanged through qualification; document
bookkeeping updates are copied into the same checkpoint. No check is running.

**Next priority / M47 plan.** Investigate the exposed absolute-table width via
ordinary absolute boxes first. `layout_absolute_containing_block` already
returns the initial viewport size, but `layout_resolve_percent_size_for_child`
only revisits direct percentages; percentage math can retain a width computed
against a different flow parent. Establish this first disagreement with static
versus positioned roots, viewport/root size differences, direct percentages
versus math, and ordinary div/table controls before editing. This has common-page
value for positioned panels and overlays, beyond the compound table fixture.

Use [CSS Position 3 §2.1](https://www.w3.org/TR/css-position-3/#def-cb) and
[CSS Values 4 math computation](https://www.w3.org/TR/css-values-4/#calc-computed-value).
Acceptance: the exact `sizing-orthog-htb-in-vrl-001-ref.xht`, bounded width/height
controls distinguishing initial and established containing blocks, and retained
containing-size/style restoration. Reuse the existing math evaluator and selected
computed-value owner; do not add a table-specific formula or reparse CSS strings.
Preserve **D4.5.1v4 / D4.1.4v5**. Budget causal native/browser cases, affected
accepted positioning/sizing neighbors, canonical WPT delta and a required Radiant
gate after stable source. Table-grid algorithms, general root negative scrolling
and the existing longer-tail families remain separate unless they block this
finite contract. M47 has no probes or engine edits yet; the full goal stays active.

### M47 — Positioned percentage math uses the actual containing block

**Entry and diagnosis.** Revalidated all M46 source/document and CLI hashes;
the preceding turn made verified progress and closed its required gate. At
320×200, 48 independent browser controls distinguish the initial viewport,
positioned root and positioned body, with two root widths, div/table boxes,
and direct percentages, calc, min and clamp (`temp/wpt-m47-browser.json`).
M46 matches **18/48**. Direct percentages on ordinary initial-CB divs work,
but math retains the 400×250 body basis: calc(50% + 10px) becomes 210×135
instead of 170×110. A positioned 480×300 root still gives direct percentages
the viewport's 320×200 basis, rather than the root padding box. Absolute table
layout additionally restores a height resolved against the wrong body context.
These are shared containing-block/used-value faults, not table-specific widths.

**Design and finite acceptance.** Distinguish the fallback initial containing
block from a root that actually establishes an absolute/fixed containing block.
Extract and reuse the ancestor eligibility predicate currently split across
positioned/fixed walks. Carry the child's positioning type into CB geometry;
use the resulting CB identity for offsets. Apply dependent computed style in
the same scoped CB used by positioned sizing and content layout, reusing the
existing selected computed-value cache and math evaluator. Preserve separate
DOM inheritance and hypothetical-flow static-position inputs. Direct and math
percentages must share this basis; do not encode math as a separate formula.

[CSS Position 3 §2.1](https://www.w3.org/TR/css-position-3/#def-cb) defines
ancestor-established blocks and the initial fallback; fixed positioning does
not use a merely relative ancestor. [CSS Values 4 §10.11](https://www.w3.org/TR/css-values-4/#calc-computed-value)
retains unresolved percentages in math until used-value information exists.
Selected style/view ownership remains **D4.5.1v4 / D4.1.4v5**; no new retained
payload or formal ruling is required.

Acceptance includes the original `sizing-orthog-htb-in-vrl-001-ref.xht`, all
48 controls, fixed-versus-absolute and transformed-root counterexamples needed
by the shared predicate, actual padding-edge geometry, and retained containing
size/style restoration. Add zero definite bases only if they expose the same
owning distinction. Use a causal native regression, focused browser/geometry
checks, affected accepted positioning/sizing neighbors, unchanged canonical WPT
delta, lint/diff and one required Radiant gate after source is stable. Keep
independent grid/table algorithms and M46's other residual families separate.

**First implementation and causal refinement.** A shared ancestor eligibility
producer now serves absolute/fixed walks and root-CB geometry. The root identity
check is named separately from the initial fallback, and CB offsets use the
returned geometry's identity. Positioned used layout scopes the actual CB for
computed-style reapplication and subsequent content; out-of-flow flex children
do not acquire the flex-item percentage override. No CSS math recipe is added.

The 48 original controls improve **18→48/48**. Eighty fixed/transform/padding
counterexamples improve **23→80/80** (`temp/wpt-m47-context-browser.json`).
The new regression's **480 retained states** pass; the complete first native
runner passes 339/339. Canonical WPT improves **1,578→1,599/1,980**, no full
losses and zero execution errors; 21 full gains include the original table
reference and close M46's reduced partial. One other existing RTL reference
partial decreases (`temp/wpt-m47-first-delta.json`). Accepted sizing is 776/776.

Accepted positioning catches a first-candidate regression in
`position-sticky-flexbox`: two ordinary green flex siblings lose their 100px
width and green background. Entry/candidate trees show that their computed
style is being cleared (`temp/wpt-m47-sticky-cause.json`), rather than an auto
width changing on the script-created absolute wrapper. The new style call can
arrive through deferred flex layout while `lycon->view` still names a sibling.
Bind both current DOM node and view around that call using `LayoutViewScope`,
and restore both afterward, preserving the existing selected owner contract
(**D4.5.1v4 / D4.1.4v5**). Add a small retained flex-sibling geometry/background
regression. Keep the neighbor fixture and its reference unchanged; the first
candidate is not qualified until this shared context error is repaired.

**Paired-context result and zero-basis boundary.** The paired DOM/view scope
passes the retained sizing and new flex-sibling regressions. Accepted positioning
returns to **139/139** and sizing stays **776/776**. WPT remains 1,599/1,980.
The remaining reduced RTL partial is now isolated: its table/row/cell dimensions
are correct, including 52px side cells and a 1,200px data cell, but the root and
body remain at x=0 instead of Chromium's -104px. M46 already had that root
origin error and a wrongly wider table; their offsets accidentally preserved a
few old matches (`temp/wpt-m47-rtl-{before,second,browser}.json`). Keep the raw
partial decrease and prioritize horizontal RTL root placement separately.

Sixteen definite-zero CB controls expose four failures in the same percentage
basis contract: calc's fixed 10px term and clamp's 40px minimum are lost on the
block-size axis. The percentage leaf resolver treats a given height of zero as
unresolved and returns NaN, making the whole expression automatic. Accept the
existing nonnegative definite-height sentinel (auto remains -1), so zero
percentage terms can participate in math. Extend retained tests with zero and
decorated-zero body CBs; this is a definiteness repair, not a fitted minimum.

**Stable focused candidate.** All **340/340 native tests pass**, including the
expanded **544 retained sizing states** and the deferred flex-sibling guard
(`temp/wpt-m47-final-native.log`). The final paired browser audit improves
**47→144/144**, zero new failures: core 18→48/48, fixed/transform/padding
23→80/80 and zero bases 6→16/16 (`temp/wpt-m47-final-controls-audit.json`).
Sources, observations and metadata are paired exactly; no capture changed.
The final canonical inventory is **1,599/1,980**, 381 mismatches, no execution
errors. It retains 21 full gains, no full losses, the one explained RTL root
partial decrease and 1,958 unchanged records (`temp/wpt-m47-final-delta.json`).
Accepted positioning/sizing passed after the active-view repair; final required
qualification also covers the zero-height leaf change. Lint/diff checks pass.
Twelve cumulative code/test hashes and the CLI are frozen in
`temp/wpt-m47-candidate-code-state.json`; the required Radiant aggregate is
running on that candidate. Keep its binaries unchanged until it terminates.
Ownership remains **D4.5.1v4 / D4.1.4v5**, and the full goal stays active.

**M47 qualified checkpoint.** The required aggregate exits 0 with **6,612
total, 6,470 full passes, 140 accepted partials, two existing skips and no
failures** across 29 groups (`temp/wpt-m47-required-aggregate.log`,
`temp/wpt-m47-aggregate-summary.json`). Native text/layout is 340/340, UI
741/741, paged media 504/504 and accepted layout 3,158 full + 136 partial;
viewer checks retain the existing speech skip. The exact requested command
exits 0 with **1,599 matches / 381 mismatches**
(`temp/wpt-m47-user-command.log`). This command's exit status does not make
the remaining raw geometry mismatches pass.

This closes actual positioned-CB percentage/math sizing, eligible root versus
initial fallback identity, paired style/view context during deferred layout,
and definite-zero percentage leaves. Browser controls improve 47→144/144;
the original exposed table reference now fully matches. One reduced existing
RTL partial remains explicitly recorded with its root-origin cause. No fixture,
reference, tolerance or acceptance-policy changes were made. The qualified CLI,
17 source/document copies, cumulative patch and manifest are saved under
`temp/wpt-m47-qualified{.exe,-source/,.patch,-state.json}`. Frozen code and CLI
hashes match throughout qualification; no test remains running.

**Next priority / M48 plan.** Correct horizontal normal-flow root placement
against the initial containing block. An oversized RTL root currently remains
at x=0; ordinary block constraints place its border box against the right
edge. This affects RTL page alignment and viewport overflow, beyond the
`sizing-orthog-htb-in-vrl-013-ref.xht` partial exposed by the preceding repair.

Use [CSS 2.2 §10.3.3](https://www.w3.org/TR/CSS22/visudet.html#blockwidth)
for used margins and [CSS Writing Modes 3 §8.1](https://www.w3.org/TR/css-writing-modes-3/#principal-flow)
for principal flow propagation. Reuse `layout_resolve_in_flow_margins` and the
existing root/body used-writing-mode source; preserve computed direction and
logical-property mapping. First compare independent narrower/wider root boxes,
authored/auto margins, padding/borders, root/body direction conflicts, min/max
constraints and relative offsets. Include retained width/direction restoration
and the exact failing reference. Distinguish used principal direction from
computed root direction before editing.

Budget causal native/browser controls, affected root/position/sizing neighbors,
the canonical WPT delta, integer-cast lint and one required Radiant gate on
stable source. Keep vertical root sizing and absolute/fixed inset algorithms
separate unless a shared defect blocks the selected contract. Preserve
**D4.5.1v4 / D4.1.4v5**. M48 has no probes or code edits yet; the full goal
remains active.

### M48 — Principal direction and horizontal root margin placement

**Entry and first disagreement.** M47's saved source/CLI fingerprints match.
The 160 independent 320×200 browser controls cover two explicit widths and
auto width, authored/auto margins, padding/borders, root/body LTR/RTL conflicts,
min/max constraints, relative offsets and the existing body visibility/
containment propagation boundary (`temp/wpt-m48-browser.json`). The qualified
M47 engine agrees on **96/160**: core 76/120, constraints 12/24 and source
selection 8/16 (`temp/wpt-m48-before-audit.json`). Dimensions already agree;
the first mismatch is the root border-box origin. Narrow LTR roots also fail
centering because only table roots have an auto-margin special case.

The owning path is `layout_html_root`: it starts x at the authored left margin
and bypasses the normal margin constraint after the used width is constrained.
The principal-flow helper already selects the first eligible body for used
writing mode; share it for used direction. Keep the computed-direction helper
unchanged. Apply the shared in-flow margin solver after root min/max sizing
and before descendant layout, so initial-CB positioned descendants can account
for the root's real origin. Use the same principal direction for the viewport's
horizontal range orientation. Existing root absolute/fixed insets and vertical
flow algorithms retain their own paths. The finite plan and verification budget
above remain applicable; no engine edit has been made at this point.

**Stable implementation and focused result.** The shared root/body source now
serves used direction and writing mode. Computed direction remains unchanged.
After min/max sizing, horizontal normal-flow roots use
`layout_resolve_in_flow_margins` before descendants are laid out; the old
table-only auto-centering branch is removed. The viewport range uses the same
principal direction. No second margin formula, fixture predicate or retained
style payload is introduced (**D4.5.1v4 / D4.1.4v5**).

The causal regression fails on M47 and passes after the repair. All **342/342
native tests pass**, including **144 retained margin/direction states** and
**16 propagation/range states** covering visibility, containment, inheritance
and restoration (`temp/wpt-m48-{causal-before,final-focused,final-native}.log`).
Independent browser agreement improves **96→160/160**, with no control losses
(`temp/wpt-m48-first-controls-audit.json`). Accepted positioning and sizing
remain **139/139 and 776/776** (`temp/wpt-m48-neighbors.log`).

Canonical WPT is **1,600/1,980**, 380 mismatches, zero execution errors: the
exact `sizing-orthog-htb-in-vrl-013-ref.xht` is the sole full gain, with no
losses, no reduced partials and 1,979 unchanged records
(`temp/wpt-m48-first-delta.json`). This closes M47's exposed RTL partial.
Lint/diff checks pass. Thirteen cumulative code/test hashes and the CLI are
frozen in `temp/wpt-m48-candidate-code-state.json`; the required Radiant gate
is running on that candidate. No engine/binary changes are permitted during
qualification. General root zero-size handling, signed margin contributions
to CSSOM scroll extents and broader fixed/fragmented contexts remain separate.

**M48 qualified checkpoint.** The required Radiant aggregate exits 0 with
**6,614 total, 6,472 full passes, 140 accepted partials, two existing skips and
no failures** across 29 groups (`temp/wpt-m48-required-aggregate.log`,
`temp/wpt-m48-aggregate-summary.json`). This includes native text/layout
342/342, UI 741/741, accepted layout 3,158 full + 136 partial and paged media
504/504. Viewer and rendering retain their existing skips. The exact requested
command reports **1,600 matches / 380 mismatches**, exit 0
(`temp/wpt-m48-user-command.log`); raw conformance remains incomplete.

This closes horizontal normal-flow root used margins and principal direction
for root placement/viewport orientation, including retained computed-style
preservation. The sole WPT gain is the original exposed RTL reference; all
other records are unchanged. Independent controls are 160/160 and new retained
coverage has 160 states. Frozen source/CLI hashes match through qualification.
The qualified executable, 18 source/document copies, cumulative patch and
manifest are `temp/wpt-m48-qualified.exe`, `temp/wpt-m48-qualified-source/`,
`temp/wpt-m48-qualified.patch` and `temp/wpt-m48-qualified-state.json`.
No fixtures, captures, tolerances or acceptance policies changed. The full goal
remains active under **D4.5.1v4 / D4.1.4v5**.

**Next priority / M49 plan.** Investigate positioned image box sizing. The
completed inventory's cached `abs-pos-vlr-border-001.html` tree and unchanged
browser reference already isolate one mismatch: the final broken image is
100×40 instead of 104×46; its RTL static x then differs by the missing
horizontal border. The other div/canvas boxes match. Evidence is copied by
identity into `temp/wpt-m49-static-cause.json`; no new M49 probe has run.

The positioned image branch replaces resolved border geometry with
`lycon->block.given_width/height`, whereas CSS dimensions describe the box
selected by `box-sizing`. Establish that first disagreement with ordinary and
positioned loaded/broken images, HTML/CSS declared dimensions, padding/borders,
content/border-box and logical/physical writing modes. Anchor both start and
end edges, and retain dimension/decoration/resource-state restoration. Use
[CSS Sizing 3 §3.3](https://www.w3.org/TR/css-sizing-3/#box-sizing),
[CSS 2.2 §10.3.8](https://www.w3.org/TR/CSS22/visudet.html#abs-replaced-width)
and [HTML image rendering](https://html.spec.whatwg.org/multipage/rendering.html#images-3).
Reuse existing box conversions and replaced sizing facts; add no image-specific
border constants, alternate formulas or fitted offsets. Loaded auto-ratio
behavior is a counterexample if its shared producer changes; general alt-text
font/icon fidelity remains separate.

Acceptance includes the original border/padding test and reference pairs,
independent minimal browser controls and a causal retained regression. Budget
focused checks, affected sizing/positioning/replaced-image neighbors, canonical
WPT delta, integer-cast lint and a required Radiant gate after stable source.
Selected owner contracts remain **D4.5.1v4 / D4.1.4v5**. M49 has no engine
edit yet; keep this checkpoint intact before starting its probes.

### Writing-mode baseline admission — requested 2026-10-28

The user explicitly requested a baseline and inclusion in both layout and
Radiant baseline gates. Admit the qualified M48 observations without changing
engine geometry, Chromium captures or tolerances: **1,980 recorded fixtures,
1,600 full matches, 380 per-metric partial-score floors, zero execution errors**.
This protects current behavior while keeping incomplete conformance visible;
it does not close the long-running CSS goal or relabel partials as full passes.

`build_lambda_config.json` → `test.layout_baseline_suites` now owns the shared
default category inventory, including `wpt-css-writing-modes`. The existing
Make override remains available. Both aggregate targets already use the same
layout runner, so no second runner or suite list is added. Regenerate the build
configuration with `make generate-premake`; do not edit generated Lua.

The corpus lives in the linked `../lambda-test` repository. Its new
`layout/data/wpt-css-writing-modes/baseline.txt` records every discovered file,
including **109 repeated test/reference basenames**, in the existing harness's
discovery order. Each occurrence has its own entry/score; duplicate comments
identify canonical fixture paths. Preparation asserts an exact 1,980-file
identity match to M48 and verifies that reducing a recorded partial metric is
still rejected (`temp/wpt-writing-baseline/{prepare.cjs,inventory.json}`). This
avoids the existing updater's basename deduplication when creating the initial
suite baseline; the harness/classifier itself is unchanged.

The focused shared runner reports **1,600 passed, 380 partial, zero failed or
skipped**, and executes all 1,980 entries
(`temp/wpt-writing-baseline/focused.log`). Full configured layout and Radiant
integration verification are pending. Existing engine ownership contracts
**D4.5.1v4 / D4.1.4v5** are unchanged. M49 remains planned and has no new
probe or engine edit while this requested admission is verified.

**Admission qualified.** The configured layout target passes **5,274 total:
4,758 full, 516 partial, zero failures/skips**, using the just-qualified native
build (`make test-layout-baseline -o build-test`, log
`temp/wpt-writing-baseline/layout-baseline.log`). The full requested
`make test-radiant-baseline` also exits 0: **8,594 total, 8,072 full passes,
520 partials, two existing skips and no failures** across 29 runner groups
(`temp/wpt-writing-baseline/{radiant-baseline.log,radiant-summary.json}`). Its
layout record includes all 1,980 writing-mode fixtures, 1,600 full and 380
partial, with no failure or skip. The 15 pre-existing layout categories retain
their prior outcomes. Make's focused suite override and configuration generation
also pass.

The saved M47 observations correctly fail the newly required RTL root-placement
entry (`temp/wpt-writing-baseline/predecessor-regressions.json`); reduced partial
scores are also rejected. All duplicate occurrences remain covered. Engine code
is unchanged from M48; admission changes are limited to configuration, the
new corpus baseline and documentation. Raw conformance remains incomplete,
and the original long-running goal remains active. Save the configuration,
corpus fingerprints and reports in `temp/wpt-writing-baseline/qualified-state.json`;
both repositories contain changes that belong to this admission.

### M49 — Positioned images preserve the content/border-box distinction

**Entry and first disagreement.** The previous goal turn made verified progress:
M48 and the requested baseline admission both closed their required gates.
Revalidated HEAD, all 13 engine/test hashes, the CLI, configuration/document
fingerprints and both linked corpus files against those checkpoints. No work
is running. The selected M49 plan above remains the finite scope.

Independent 320×200 Chromium controls cover 172 sources: 96 decorated positioned
images, 24 ordinary-flow counterparts, 16 bare boundaries, 12 auto-inset static
edges and 24 loaded natural-ratio counterexamples. Modes are horizontal-tb,
vertical-lr and vertical-rl; sources are loaded or broken, dimensions are HTML
hints or CSS, and both content/border-box and absolute/fixed anchors participate
(`temp/wpt-m49-browser.json`). M48 agrees on **88/172**: 48/96, 24/24, 16/16,
0/12 and 0/24 respectively (`temp/wpt-m49-before-audit.json`).

The first disagreement is late publication in `layout_abs_block_internal`:
`calculate_absolute_position` has already built the correct declared border
box, but the image branch overwrites it with unconverted given sizes. Loaded
auto axes additionally transfer the ratio through CSS box dimensions rather
than natural content dimensions. The ordinary-flow controls isolate this from
resource admission, cascade and font metrics.

**Implementation design.** Keep the positioned axis/inset solver as the single
owner of declared used geometry. A broken image must preserve that result.
For loaded images, reuse `ReplacedIntrinsicFacts` and
`layout_replaced_content_size`; share the existing min/max constraint adapter
with normal image sizing rather than adding a second ratio/clamp recipe.
Publish used content sizes and border sizes through existing box conversions,
then reuse the positioned origin/auto-margin machinery when an auto axis changes
size. Preserve authored automatic-axis identity across retained resource and
decoration changes. Remove the superseded image-only 40×30 fallback and
handwritten intrinsic ratio branch; do not replace them with fitted constants.
Keep general alt-text/icon rendering and independent resource-selection policy
separate. Ownership remains **D4.5.1v4 / D4.1.4v5**; no new payload is needed.

Acceptance/budget: causal retained declared-size and loaded-auto controls first,
the exact upstream border/padding test and reference pairs, the 172 immutable
browser observations, affected accepted positioning/sizing/image neighbors,
canonical raw WPT delta, integer-cast lint/diff, and the required Radiant gate
after stable source. The newly admitted baseline must retain all existing
floors; do not lower them to qualify this repair.

**First candidate and HTML-hint refinement.** The shared constraint adapter
and content-size publication repair every CSS-declared sizing control, including
all 24 loaded auto-ratio controls. The first candidate nevertheless agrees on
only 104/172 and loses 32 prior control agreements: HTML-declared dimensions
become automatic (`temp/wpt-m49-first-audit.json`). The retained regression
exposes this too, so the first candidate is not qualified.

The removed image override had concealed a second fault:
`positioned_axis_is_auto` treats every missing CSS size declaration as automatic,
even when HTML width/height hints have already supplied definite given sizes.
Broken HTML heights therefore collapse to decorations; loaded HTML boxes select
their natural dimensions instead of the declared hints. Reuse the existing
`layout_block_has_automatic_size` fallback only when no CSS declaration exists.
Keep explicit/intrinsic CSS keyword handling in its existing owner. This is the
same size-identity contract, not a raw attribute reparse or image-only exception.
The native regression includes both HTML/CSS declarations and retained override
removal, so it checks this distinction through the real style pipeline.

**Boundary results and intrinsic-keyword refinement.** The hint repair passes
all 172 original controls and 432 retained states. Added definite-zero cases
challenge the removed arbitrary fallback: 48 browser controls improve 0→48;
32 min/max adapter controls improve 0→32. The retained sizing regression now
covers **528 states**, including zero, loaded/broken resource changes and
restoration. All 343 first native tests pass. Canonical WPT gains the two
original border/padding HTML documents, with no losses or reduced partials.

Accepted positioning catches one regression in
`position-absolute-replaced-no-intrinsic-size.tentative`: a ratio-only SVG with
width 100px and height max-content keeps the provisional 150px fallback height,
instead of deriving 100px through its 1:1 ratio. The shared solver had treated
the provisional intrinsic block-size result as definite. CSS Sizing 3 §3.2
defines min/max-content block sizes as automatic unless specified otherwise.
Map those logical block keywords to automatic inputs at the image adapter,
preserving intrinsic inline-size handling. A second retained regression covers
natural/ratio-only sources, both box models, three element writing modes,
auto margins and keyword restoration. Thirty-six independent keyword controls
improve **0→36/36**, including the physical-width block axis.

**Stable focused candidate.** All **344/344 native tests pass**, including
**576 new retained states** across the two regressions
(`temp/wpt-m49-{fourth-focused,final-native-344}.log`). One consistent final CLI
run improves **88→288/288** source-paired browser agreements, no losses
(`temp/wpt-m49-final-controls-audit.json`). Accepted positioning, sizing and
image neighbors pass **139/139, 776/776 and 56/56** after the keyword repair
(`temp/wpt-m49-final-neighbors.log`). Canonical raw WPT is **1,602/1,980**,
378 mismatches and zero execution errors: two full gains, zero losses/reduced
partials and 1,978 unchanged records (`temp/wpt-m49-final-delta.json`).

The original two test documents now fully match. Their reference documents'
image rectangles also match; their unchanged residual is a normal RTL div with
`width:max-content` staying at x=1 after shrinking, rather than x=81.45. Width,
height and every image already agree (`temp/wpt-m49-originals.json`). This is
a separate in-flow margin-finalization defect to prioritize next; no image
offset compensates for it. Integer-cast lint/diff checks pass. Fourteen
cumulative code/test hashes and the CLI are frozen in
`temp/wpt-m49-candidate-code-state.json`; the required Radiant gate is running
on this stable candidate. Preserve the admitted baseline floors and binaries
through qualification. The full conformance goal remains active.

**M49 qualified checkpoint.** The required Radiant aggregate exits 0:
**8,596 total, 8,076 full passes, 518 accepted partials, two existing skips and
zero failures** across 29 groups (`temp/wpt-m49-required-aggregate.log`,
`temp/wpt-m49-aggregate-summary.json`). Native text/layout is 344/344, UI
741/741, paged media 504/504, and accepted layout 4,760 full + 514 partial.
Writing modes contributes 1,602 full + 378 partial without any recorded-score
regression. Viewer and render retain their existing skips.

This closes positioned-image declared content/border-box publication, HTML
dimension identity, natural ratio transfer, zero/min/max boundaries and
intrinsic logical block keywords in the measured scope. It removes the
superseded handwritten ratio/margin branch and arbitrary image fallback;
shared facts, constraints and box/axis helpers own the result. The two original
HTML test documents fully match; their references' independent normal RTL div
placement remains partial and unchanged. All 288 browser controls and 576
retained states pass. Existing owner contracts remain **D4.5.1v4 / D4.1.4v5**.

After the aggregate terminated, strengthen the two unique repaired baseline
entries from scored partials to required full matches, preserving every other
entry and all fixtures/captures/tolerances. The prepared promotion rejects both
saved M48 failures and retains all 1,980 entries. A focused shared baseline run
qualifies the stricter floors; its result and the exact user command are recorded
with the final checkpoint below. No broad rerun is needed for these stricter
records: the completed aggregate already observed both full matches on the same
frozen code/CLI, and the focused runner rechecks all writing-mode expectations.

**Next priority / M50 plan.** Correct used margins after intrinsic width
commitment for normal RTL blocks. The references' ordinary div has correct
39.5×46 geometry, but remains at x=1 instead of 81.45. Static source inspection
shows `layout_resolve_in_flow_margins` runs before the intrinsic-width branch;
the post-width `layout_resolve_auto_margins_after_width_change` exits when no
margin is automatic. This leaves the initially constrained edge after the box
shrinks. It is common-page behavior for RTL content-sized cards and panels,
independent of positioned images.

First establish independent narrower/overflowing min/max/fit-content controls,
explicit-width counterparts, parent/child direction conflicts, authored/auto
and signed margins, decorated/undecorated boxes and retained width/font
restoration. Include the exact two reference divs, plus float/atomic and
explicit self-alignment counterexamples before editing. Reuse the shared
in-flow margin solver and refresh affected context geometry at the owning
stage; introduce no separate RTL offset or duplicate cascade query.
[CSS 2.2 §10.3.3](https://www.w3.org/TR/CSS22/visudet.html#blockwidth) owns the
used constraint equation. Keep root placement and unrelated table/fragment
algorithms in their existing paths. Budget causal native/browser checks,
affected accepted neighbors, raw canonical delta, lint/diff and one required
Radiant gate on stable source. Preserve **D4.5.1v4 / D4.1.4v5**. M50 has no
probe or engine edit yet; the full conformance goal remains active.

The strengthened writing-mode baseline exits 0 with **1,602 full, 378 partial,
zero failures/skips**, executing all 1,980 records
(`temp/wpt-m49-promoted-baseline.log`). Only the two unique repaired partial
entries become required full matches; the promotion audit is
`temp/wpt-m49-baseline-promotion.json`. The exact requested command exits 0 and
still exposes **378 raw mismatches** (`temp/wpt-m49-user-command.log`).
Final code/CLI hashes are unchanged through qualification. The qualified CLI,
24 main source/document copies, linked corpus snapshots, cumulative main patch
and manifest are `temp/wpt-m49-qualified.exe`,
`temp/wpt-m49-qualified-source/`, `temp/wpt-m49-qualified.patch` and
`temp/wpt-m49-qualified-state.json`. The baseline promotion also has its own
auditable patch. No test remains running; M50 remains planned without probes.

### M50 — Resolve normal-flow margins after intrinsic width commitment

**Entry and diagnosis.** The preceding goal turn made verified progress: M49
closed its native/browser/required gates and strengthened the two repaired
baseline entries. Revalidated HEAD, all main source/document hashes, CLI and
linked corpus fingerprints. The independent M50 controls compare the same
embedded Ahem source in Chromium and the saved M49 engine. The 252-case matrix
matches **173/252**: core 128/192, intermediate fit-content 6/8, signed margins
8/16, orthogonal ancestors 4/8, self-alignment 15/16 and excluded float/atomic
boxes 12/12 (`temp/wpt-m50-before-audit.json`). Sixteen sources without authored
boundary properties match 10/16 (`temp/wpt-m50-absent-before-audit.json`).

Most failures first differ in x, but the intermediate decorated RTL fit-content
case also becomes 110px instead of 100px. The initial width is provisional;
resolving its constrained RTL margin before intrinsic sizing makes that used
margin feed back into the available-width calculation. A final x adjustment
alone would therefore leave wrong sizing. Normal block horizontal margins must
be committed after intrinsic width, using authored spacing inputs during that
calculation. Float/atomic zero-auto behavior stays separate.

**Structured implementation design.** Keep the existing shared margin solver.
Initially normalize auto margins to zero and retain authored non-auto spacing
while resolving the width. After intrinsic commitment, resolve normal-flow
horizontal margins and the constrained RTL edge before child flow. Generalize
the existing post-width helper and reuse it after multipass content as well;
use the containing block's used direction and preserve positional self-alignment.
Materialize existing used-margin storage when a previously boundaryless RTL box
needs it, so repeated finalization is idempotent. Refresh the owning BFC offset
and already registered descendant float geometry when the box moves. No new
retained payload or independent width/RTL formula is needed; preserve
**D4.5.1v4 / D4.1.4v5**.

Acceptance remains the two original reference divs, narrower/overflowing and
intermediate intrinsic widths, explicit-width counterparts, independent parent/
child directions, auto/authored/signed margins, missing boundary caches,
retained font/width restoration and float/atomic/self-alignment counterexamples.
The planned verification budget applies. The first causal regression is added
before the engine repair; no engine edit has been made at this point.

**Focused implementation and verification.** The causal native regression
fails on the saved M49 implementation at x=5 rather than x=105 with the correct
140px width. Generalized the existing post-width auto-margin helper, retained
authored spacing until intrinsic commitment and reused the shared solver after
both intrinsic and multipass widths. Missing used-margin storage is allocated
through the existing selected-view owner; changed origins refresh the BFC
offset and registered descendant float geometry. No separate RTL offset or
width formula was added.

All **346/346 native tests pass** (`temp/wpt-m50-first-native.log`). The first
new regression checks initial boundaryless placement and 648 retained
direction/font/width/min/max/margin states; the second checks 18 retained float,
clearance and flow-root states. Independent exact-source browser controls
improve **183→268/268**, with zero losses, including intermediate fit-content,
signed/auto margins, opposing child directions, absent boundary properties,
orthogonal ancestors and positional self-alignment. Eight additional float/
clearance controls improve **4→8/8**, checking all 32 panel/float/cleared-child/
following-sibling rectangles (`temp/wpt-m50-float-audit.json`). The other control
audits are `temp/wpt-m50-{first-audit,first-absent-audit}.json`.
Accepted sizing **776/776**, positioning **139/139**, inline **169/169** and
text **891 full + 89 partial** retain every recorded expectation
(`temp/wpt-m50-neighbors.log`). Canonical WPT improves to **1,606/1,980**,
374 mismatches and zero execution errors: four full gains, zero losses/reduced
partials and 1,976 unchanged records (`temp/wpt-m50-final-delta.json`). The two
original references and both `bidi-table-001` documents now fully match.
Integer-cast lint and diff checks pass. Fourteen cumulative code/test hashes
and the CLI are frozen in `temp/wpt-m50-candidate-code-state.json`; the required
Radiant aggregate is running on that candidate. Prepared baseline promotions
bind all four canonical fixture identities through the existing occurrence
selection, including the repeated basename; the saved M49 observations fail
each stricter entry. The live baseline stays unchanged until qualification.

**M50 qualified and baseline refreshed.** The required Radiant aggregate exits
0 with **8,598 total: 8,082 full passes, 514 accepted partials, two existing
skips and zero failures** across 29 groups
(`temp/wpt-m50-required-aggregate.log`,
`temp/wpt-m50-aggregate-summary.json`). Native text/layout is 346/346, UI
741/741 and paged media 504/504. Accepted layout is 4,764 full + 510 partial,
including every writing-mode fixture, 1,606 full + 374 partial.

After that gate terminated, promoted the four repaired partial entries to
required full matches, preserving all other floors and all 1,980 records.
The configured layout-only target exits 0 with **5,274 total, 4,764 full,
510 partial and no failures/skips** (`make test-layout-baseline -o build-test`,
using the just-built matching binaries; `temp/wpt-m50-layout-baseline.log`).
This rechecks all 16 categories against the stricter records. No second Radiant
aggregate is needed: its unchanged code/CLI already observed all four full
matches, and the layout target verifies the only intervening input change.

The exact `make layout suite=wpt-css-writing-modes` command exits 0 and
continues to expose **374 raw mismatches**, while accepting all recorded
expectations (`temp/wpt-m50-user-command.log`). The user-requested baseline
and both configuration-driven gate integrations are complete. M50 closes the
measured normal RTL intrinsic-width placement and fit-content spacing defect;
it does not claim complete writing-mode conformance. Owner contracts remain
**D4.5.1v4 / D4.1.4v5**. Save the qualified code/CLI, source copies, linked
corpus fingerprints and promotion patch in `temp/wpt-m50-qualified-state.json`.
The original conformance goal remains active; no next milestone probe has run.

### M51 — Preserve physical relative offsets through vertical publication

**Entry / priority.** The preceding turn made verified progress: M50 qualified
the intrinsic-width margin repair, strengthened four baseline entries and closed
the requested shared baseline integration. Revalidated all 24 main hashes, the
CLI, HEAD and linked corpus fingerprints. Triage shows ordinary relative boxes
retain correct dimensions but lose their complete inset translation in vertical
flow: `overconstrained-rel-pos-rtl-top-bottom-vrl-006.xht` is 80px too low;
`box-offsets-rel-pos-vrl-004.xht` loses every 75/275/125/325px horizontal and
50/225px vertical offset (`temp/wpt-m51-triage.json`). These are common positioned
content semantics, preceding independent font fidelity and table residue.

**Owner and design.** `layout_relative_positioned` applies a surrogate flow
offset before the containing vertical flow publishes physical geometry.
`layout_publish_vertical_children` removes and restores that translation only
for atomic inlines; normal parallel/orthogonal block placement overwrites it.
Generalize that existing extraction/publication seam so relative motion stays
outside normal-flow spacing and returns exactly once in physical coordinates,
including the orthogonal early-return path. Reuse the existing axis/inset
solver and logical-axis mapping for over-constrained pairs; its current
horizontal-RTL-only branch cannot express vertical block/inline start edges.
Confirm each direction rule independently before changing it. Shared flex/grid
axis callers remain counterexamples. No new payload or ownership rule is needed;
preserve **D4.5.1v4 / D4.1.4v5**.

[CSS Position 3 §3.3](https://www.w3.org/TR/css-position-3/#relative-position)
defines visual movement without size/flow changes and gives the containing
block's start edge precedence when both insets are specified.
[CSS Writing Modes 3 §7.1](https://www.w3.org/TR/css-writing-modes-3/#vertical-layout)
maps that decision through the containing axes. Upstream assert comments are
not substitutes for these rules or independent observations.

**Finite acceptance / budget.** Check the original relative-offset/overconstraint
families against their unchanged references; source-pair minimal Chromium
controls for fixed start/end/both/percentage insets, independent parent/child
directions, normal/atomic and parallel/orthogonal boxes, the two sideways modes,
logical insets, upright/plaintext counters and existing flex/grid callers.
Retained native cases must restore static/relative/static state, preserve
following-sibling placement, dimensions and descendant origins, and repeat a
clean geometry query without relayout. Use focused checks first, then affected
accepted position/inline/sizing neighbors, canonical all-fixture delta,
integer-cast lint/diff and one required Radiant gate on frozen source/CLI.
Keep unrelated root, sticky, table, fragmentation and font defects explicit.

**Confirmed repair and retained prerequisite.** Before editing, 119/244 exact
relative rectangles agree with Chromium. Paired static controls agree 232/244;
the other 12 already have independent static placement differences. Relative
motion/subtree/sibling invariants improve **124→244/244** after the shared
axis/publication repair; absolute rectangles improve **119→232/244**, with no
losses (`temp/wpt-m51-{before,first}-motion-audit.json`,
`temp/wpt-m51-first-audit.json`). Keep the static differences visible rather
than offsetting them away.

The retained regression exposes a blocking RTL line defect even while position
remains static: adding inactive insets moves an inline-block from x=191 to
x=365. Debugger traces show identical line bounds/advance on initial and retained
passes; the retained following block sets `BidiLineCounts::has_atomic`, aborting
the current line's bidi placement. Alignment then adds its 174px free space to
the already provisionally RTL-placed item. Initial layout has not materialized
that following block yet (`temp/wpt-m51-{causal-before,lldb-boundary}.log`).

[CSS 2.2 §9.2.1.1](https://www.w3.org/TR/CSS22/visuren.html#anonymous-block-level)
separates inline runs around in-flow block siblings. Bound both bidi counting
and filling at that shared paragraph boundary, including nested inline scopes;
do not abort valid current-line bidi work because another block's retained
geometry exists. Add a later isolated inline paragraph to the retained case so
the boundary also prevents consuming later scope controls. This is a required
shared prerequisite for static/relative restoration, not a test bypass.

**Stable focused candidate.** Both bidi scans now share the in-flow block
paragraph boundary and propagate termination through nested inline scopes.
All **480 retained states** pass with a later isolated paragraph present, and
all **347/347 native tests** pass (`temp/wpt-m51-{second-focused,final-native}.log`).
Final paired relative motion is **244/244**; absolute controls are **232/244**,
with the same 12 independent static-placement differences and zero lost matches
(`temp/wpt-m51-{second-motion-audit,second-audit}.json`). Position 139/139,
inline 169/169, sizing 776/776 and text 891 full + 89 partial retain their floors.

Canonical WPT is **1,617/1,980**, 363 remaining mismatches and zero execution
errors: **11 full gains**, no losses or reduced partials, 1,969 unchanged records
(`temp/wpt-m51-final-delta.json`). Both original relative-offset tests and all
eight over-constraint tests now fully match; `horizontal-rule-vlr-003.xht` also
gains a full match through the shared publication fix. The two relative-offset
reference documents have independent unchanged residuals. Integer-cast lint and
diff checks pass. Freeze the 15 cumulative code/test hashes and CLI for one
required Radiant gate; keep the live baseline unchanged until that gate closes.

**M51 qualified.** The required Radiant aggregate exits 0 with **8,599 total:
8,094 full passes, 503 accepted partials, two existing skips and zero failures**
across 29 groups (`temp/wpt-m51-required-aggregate.log`,
`temp/wpt-m51-aggregate-summary.json`). Native text/layout is 347/347, UI
741/741 and paged media 504/504. All 16 accepted layout categories retain their
floors: 4,775 full + 499 partial, including 1,617 full + 363 partial writing-mode
fixtures. Code/CLI hashes remain frozen through qualification.

This closes physical relative movement through normal/atomic and parallel/
orthogonal vertical publication, logical start-edge precedence in the shared
axis solver and the retained RTL paragraph-boundary prerequisite. All 244
source-paired movement controls and 480 retained states pass. Twelve unchanged
absolute static-placement controls, the table/float-based relative-offset
references and the remaining raw WPT failures stay open. In particular, this
does not qualify complete inline, table or writing-mode conformance. Existing
owner contracts remain **D4.5.1v4 / D4.1.4v5**.

After the aggregate terminated, strengthened the eleven repaired baseline
entries from partial scores to required full matches, preserving all 1,980
entries and every other floor. The shared focused runner exits 0 with **1,617
full, 363 partial, zero failures/skips**
(`temp/wpt-m51-promoted-baseline.log`). Preparation binds canonical occurrences
through the existing selector and rejects all eleven saved M50 failures
(`temp/wpt-m51-baseline-promotion.json`). No broad rerun is needed for these
stricter records: the completed aggregate observed all eleven full matches on
the same frozen code/CLI, and the focused runner rechecks every writing-mode
expectation. Final exact-command and checkpoint results follow below.

The exact `make layout suite=wpt-css-writing-modes` command exits 0 while
retaining **363 raw mismatches** in its summary
(`temp/wpt-m51-user-command.log`). The immutable candidate, 26 main source/
document copies, linked corpus snapshots, cumulative patch and qualification
manifest are `temp/wpt-m51-qualified.exe`, `temp/wpt-m51-qualified-source/`,
`temp/wpt-m51-qualified.patch` and `temp/wpt-m51-qualified-state.json`; the
baseline promotion has a separate patch. No test remains running. The full
conformance goal remains active. Next, compare the table/float sizing residue
in the unchanged reference documents with other high-impact remaining families
before selecting M52; no next-milestone probe or edit has run.

### M52 — Distribute explicit table height from the row-grid budget

**Entry / priority.** The preceding turn made verified progress: M51 added
eleven full matches and closed all required gates. Revalidated HEAD, all 26
main hashes, the CLI and linked corpus fingerprints. The two remaining
relative-offset references are horizontal inline tables, independent of the
repaired relative positioning. Their outer 300px table is correct, but rows
29 + 104 + 25 consume only 158px of the 200px interior; 42px remains unallocated
below the last row. Chromium gives that space to the automatic middle row and
centers its 100px image in the resulting 146px row
(`temp/wpt-m52-triage.json`). Fixed-height tables with mixed row constraints are
common page structure, preceding independent vertical glyph and caption residue.

**Owning stages and design.** The outer explicit-height distribution guard
uses `current_y`, which already includes the table's top border/caption, as
though it were row height, then subtracts borders again from available height.
The later section-summary calculation already has the row-only quantities and
excludes captions. Use those facts to decide growth, counting each border,
padding and spacing contribution once. The nonpercentage growth branch also
ignores `row_has_specified_height`, although the existing reference-height
branch already records and honors that identity. Prefer automatic recipients
when present, then reuse the existing proportional row-extra helper and final
row/cell realignment. Extract shared recipient filtering before adding another
count/sum/apply traversal. No copied row algorithm or fixture offsets are needed;
transient metadata remains scratch-owned under **D4.5.1v4 / D4.1.4v5**.

[CSS 2.2 §17.5.3](https://www.w3.org/TR/CSS22/tables.html#height-layout)
requires the declared table height to act as a minimum and defines row minima
and cell vertical alignment; it leaves the extra-height allocation policy
open. Preserve the existing proportional weighting within the recipient class.
The automatic-row priority also appears in the
[CSS Tables 3 §3.10.5 draft](https://www.w3.org/TR/css-tables-3/#height-distribution),
whose proposed equal increments differ from the existing implementation and
Chromium when multiple rows receive growth. This milestone does not claim the
complete draft distribution algorithm.

**Finite acceptance / verification budget.** First capture matching sources
with automatic, mixed and all-specified cell/row heights; asymmetric borders,
padding, spacing, content/border boxes, block/inline tables, captions, floats,
minimum/undersized table heights and percentage counterexamples. Initial 64
controls agree **28/64** (`temp/wpt-m52-before-audit.json`). Eight collapsed
content-box controls additionally expose the pre-existing width/height box
conversion defect (native 200×240, browser 205×265); preserve that independent
residue explicitly rather than compensating in row distribution. Add a causal
retained native case before editing, with size/style restoration and following
flow checks. Then run affected accepted table/sizing/inline neighbors, the raw
canonical delta, integer-cast lint/diff and one required Radiant gate on frozen
source/CLI. Keep the original captures, tolerances and baseline floors.

The additional 60 boundary controls agree **14/60** before editing, for
**42/124** total (`temp/wpt-m52-boundary-before-audit.json`). The native causal
case fails before repair: an initially hidden caption is still tagged and
measured, moving the first row to y=40 instead of y=20; its height is 60px
instead of the authored 25px (`temp/wpt-m52-causal-before.log`). Table marking
must route `display:none` through the existing flow dispatcher before assigning
table roles. This is a necessary caption-restoration prerequisite, not a
special caption offset. Use the grid extent `current_y - content_area_top_y`
for the height budget: its established origin already excludes the caption,
top border/padding and first spacing edge. Add the two spacing edges once.
This also removes the redundant section-summary reconstruction and covers
direct CSS rows without treating their natural height as zero. Retained
acceptance covers 192 states across cell/row declarations, both table displays,
both separate-border box models, normal/float children and caption restoration.

The first candidate repairs the grid budget and agrees with 118/128 independent
controls (44/128 before), but the two original references still misplace the
middle image by 21px. Its natural 104px line height and final 146px cell are
correct. `parse_cell_attributes` overwrites the computed UA middle alignment
by searching authored declarations through all ancestors, reaching the outer
inline table's `vertical-align:top`. The HTML UA defaults also flatten rows
and cells to middle instead of preserving their inheritance chain.
[HTML Rendering §15.3.8](https://html.spec.whatwg.org/multipage/rendering.html#tables-2)
gives row groups and direct table rows middle alignment, and rows/cells inherit.
Update the shared UA defaults/serialization, resolve that direct-parent value
before author CSS, and consume the computed cell alignment without a second
ancestor search. Presentational `valign` belongs on its own element before the
author cascade; remove the old parent-row attribute search. This is an original
acceptance prerequisite under the same D4.5.1v4 / D4.1.4v5 owners.

Before this alignment repair, the new retained native regression fails on the
first direct-row block case; 108 exact-source XHTML controls cover direct/grouped
rows, data/header cells, block/canvas/image content, ancestor overrides, explicit
inheritance, initial/unset and restoration. Keep the prior 128 controls and their
ten independent collapsed-box/percentage differences visible.

**Candidate acceptance.** Both original reference files now match fully. The
canonical 1,980-file inventory reports **1,619 full, 361 remaining, zero execution
errors**, two gains, no full loss or reduced partial metric and 1,978 unchanged
records (`temp/wpt-m52-qualified-delta.json`). The 128 grid/boundary controls
retain 118 agreements. Defined Ahem metrics give **108/108** matching alignment
controls (60/108 in M51); 48 adjacent presentational-hint controls all agree
(24/48 before). The original zero-font controls improve 48→68/108, with 40
2px inline placements still different; an explicitly middle-aligned predecessor
control already reproduces that independent zero-font line-metric residue.
Combined controls improve **176→342/392, no lost agreement**
(`temp/wpt-m52-qualified-controls-audit.json`).

All **349 native queries** pass before the final hint expansion; the two causal
cases then pass the completed **348 retained states** (192 grid/caption and
156 alignment/hint states), including reset and restoration. HTML parsing inserts
a tbody around direct source rows; independent XHTML controls retain genuinely
direct rows. Accepted neighbors remain tables 57/57, sizing 776/776 and inline
169/169. Integer-cast lint and diff checks pass. Code and CLI are frozen in
`temp/wpt-m52-candidate-code-state.json` (17 cumulative source/test files), and
the required Radiant gate is next. The prepared baseline promotes only the two
repaired references and rejects both saved predecessor results; leave live
baseline floors unchanged until the gate terminates.

**M52 qualified.** `make test-radiant-baseline` exits 0 across 29 runner groups:
**8,601 total, 8,098 full, 501 partial, two existing skips, zero failures**.
This includes all 349 native queries with the completed hint cases, 504 paged
checks, 741 UI cases and all 5,274 accepted layout expectations (4,777 full,
497 partial). Reports are `temp/wpt-m52-{required-aggregate.log,radiant-summary.json,
aggregate-summary.json,aggregate-layout.json,final-native.log}`. Source and CLI
hashes still match the frozen candidate. No broad rerun is needed for the two
stricter expectations: the aggregate observed both full matches on that binary.

After the gate terminated, promoted only the two repaired reference entries.
All other floors, fixture identities, duplicate occurrences, captures and
tolerances stay intact. The focused shared runner checks every entry and exits
0 with **1,619 full + 361 partial, no failures/skips**
(`temp/wpt-m52-promoted-baseline.log`). The exact
`make layout suite=wpt-css-writing-modes` command also exits 0, while explicitly
reporting the **361 raw mismatches** (`temp/wpt-m52-user-command.log`).

The milestone delivers explicit table grid budgeting, automatic-row growth and
computed HTML table alignment through retained changes. It does not qualify
complete table, inline or writing-mode conformance. The eight collapsed
content-box controls, two percentage-basis controls and 40 zero-font inline
placements remain recorded limitations. Owner contracts **D4.5.1v4 / D4.1.4v5**
are unchanged. Save the main-tree patch, binary, sources and linked corpus
fingerprints in `temp/wpt-m52-qualified-state.json`; the strict baseline has its
own promotion patch. No test remains running, and the long-running goal stays
active. Compare the remaining common table-box and inline line-metric defects
with other high-impact raw families before selecting M53; no next-milestone
probe or edit has run.

### M53 — Let orthogonal table cells own their content-flow axes

**Entry / priority.** M52 made verified progress: two new full matches, stricter
baseline expectations and a passing required gate. Revalidated all 29 main
hashes, the CLI and both linked corpus files. Representative remaining failures
are recorded in `temp/wpt-m53-family-triage.json`. The table-cell
`block-flow-direction-*` cases expose a shared BFC defect: the vertical-rl
example is 40×1,940 instead of 420×180, and its children stack downward.
This precedes the smaller collapsed content-box conversion residue. The 25
orthogonal shrink-to-fit documents additionally include browser-generated
testharness report DOM; their zero scores alone do not isolate a flow defect.

**Owning stages / hypothesis.** Table-cell intrinsic measurement takes the
widest child through its horizontal grid walk. The shared intrinsic vertical
block-sequence classifier excludes table cells despite their block-container
behavior. The custom cell-content loop also never publishes a vertical cell's
own flow when the containing table grid is horizontal. Reuse the existing
intrinsic and vertical geometry helpers, preserving table ownership of row and
column tracks. Do not mutate a cell's display/tag to borrow a different layout
path, copy the block formatter, or fit offsets to the reference image.

[CSS Writing Modes 3 §7.3](https://www.w3.org/TR/css-writing-modes-3/#orthogonal-flows)
separates sizing in the box's own axes from positioning in its containing flow;
its content-based block extent contributes to the containing shrink-to-fit
calculation. The selected core milestone is a vertical/sideways cell with a
definite own inline size in a horizontal table, including standalone CSS cells,
normal HTML cells, inline text and block sequences, both directions, decorations
and retained mode/size restoration. Horizontal parallel cells are controls.
General vertical-grid/horizontal-cell combinations, captions, collapsed-box
conversion and testharness-generated report DOM remain separate unless they
expose the same owning defect.

**Acceptance / budget.** Capture independent exact-source controls before
editing; include the four original cell block-flow cases, the four adjacent
caption cases as boundary observations, and a causal retained native regression.
Resolve the first axis disagreement, then verify intrinsic
size, physical child arrangement and following flow through mutation. Use
focused checks, accepted table/sizing/inline neighbors, the canonical WPT delta,
integer-cast lint and one required Radiant gate on frozen code/CLI. Preserve all
baseline floors/captures/tolerances. Keep transient measurements and published
geometry under the unchanged **D4.5.1v4 / D4.1.4v5** ownership contracts.

**Causal evidence.** The 120 exact-source controls initially agree 20/120
(`temp/wpt-m53-before-audit.json`). Four decorated horizontal RTL controls have
an independent 2px content-origin difference; keep those visible. The native
100-state retained regression fails first with a 40px cell width instead of
the CSS-derived 20+30+40=90px, and 40px block inline extents instead of 120px.
Using shared intrinsic measurement fixes the 90px width before flow changes
(`temp/wpt-m53-intrinsic-focused.log`). Own cell inline-size setup and shared
publication then fix physical stacking, but block children remain 40px tall:
`vertical_auto_inline_formatting` independently excludes a table-cell parent.
Extract a shared flow-container predicate for ordinary flow, flow-root, cells
and captions; reuse it in intrinsic classification and auto inline sizing.
The -019/-020/-059/-060 originals are captions, not cells; their separate
content producer remains a boundary rather than a claim of completed support.

**Caption prerequisite / scope update.** Enabling the shared flow-container
classification exposes a hard-coded 10,000px caption content height as a real
descendant stretch constraint. Caption setup also borrows another box's BFC
state. Replace that producer with `block_context_init`, a live caller-owned
parent scope, the caption's declared/indefinite height, and `setup_inline`.
Cell and caption publication share their natural-inline query and existing
vertical geometry helpers. All eight original cell/caption block-flow documents
then match fully (`temp/wpt-m53-caption-owner-originals.json`). This prerequisite
extends the retained regression to 200 cell/caption states instead of silently
leaving a newly exposed stand-in constraint live.

That retained check also finds a pre-existing horizontal RTL caption shrink
error: its three 20/30/40px blocks make the caption 320px instead of 40px.
The equivalent browser/native controls reproduce it before M53. Intrinsic
margin fallback reads the children's used RTL overconstraint adjustment as
though it were a computed margin. Copy the clean computed margin baseline
through the resolved-style module's existing freshness/context checks before
falling back to legacy boundary state; never borrow the record or change the
used positions. This preserves the **D4.5.1v4 / D4.1.4v5** split between
immutable computed inputs and published geometry. Caption controls initially
agree 46/120 and reach 78/120 after the BFC repair, with no lost agreement;
the independent margin, asymmetric box and atomic-line limitations stay visible.

**Axis / metric prerequisites.** The first canonical sweep gains eight full
matches without losing a full match, but lowers three partial records
(`temp/wpt-m53-first-delta.json`). The two `ch-units-vrl` reductions come from
ignored table-track writing modes leaking into descendant used flow. Separate
that source selection from computed writing-mode: the latter still determines
font-relative units, as explicitly described by
[CSS Cascade 5 §4.5.1](https://drafts.csswg.org/css-cascade-5/#applies-to).
Keep direction's existing inheritance independent. Cell logical declarations
also need the cell's own physical-property winner, rather than the grid's axis;
reuse `layout_cascaded_physical_declaration` throughout those size queries.
An inline line occupies its containing inline measure, not merely its painted
glyph advance ([CSS2 §9.4.2](https://www.w3.org/TR/CSS2/visuren.html#inline-formatting));
publish that full measure before table-cell vertical alignment.

Retained track/own-cell controls then expose a separate metric lifetime defect:
computed `line-height:5ch` can run before the selected `FontProp` has a font
handle, folding the fallback half-em into a cached value despite an available
Ahem face. LLDB records the correct computed descriptor with a null handle
(`temp/wpt-m53-metric-trace.log`). Resolve the computed descriptor through the
font context for both layout and CSSOM metric queries, and release the temporary
reference within the shared helper. Do not publish a speculative handle into
the `FontProp` or borrow a different box's metrics. This implements
[CSS Values 4 §6.1.1](https://www.w3.org/TR/css-values-4/#font-relative-lengths)
under the unchanged **D4.5.1v4 / D4.1.4v5** lifetime contracts. The causal native
checks cover 200 retained cell/caption states and 40 track/font-axis states;
both fail before their corresponding fixes. Qualification remains pending.

The direct 80-source browser `Range.getClientRects()` audit improves 14→51
agreements, with no loss, but exposes another owning-boundary error: horizontal
table alignment applies half-leading in physical y to an already published
vertical glyph rectangle. In RTL it additionally mistakes text-align's inset
for the flow origin. A vertical line's full inline extent and its physical flow
origin must survive publication separately from painted glyph bounds. Retain
that scalar origin in the existing view-owned `TableCellProp`, reset it with
cell flow, move it with alignment, and let vertical-content row measurement and
bounds consume the published region instead of horizontal line metrics.
The retained cell/caption test now includes inline text and wrap/unwrap, giving
400 states plus the 40 track/font-axis states. The exact-source browser/native
audit demonstrates the geometry-boundary error before that fix; the expanded
native test also catches a remaining sideways-lr RTL reversal after it
(`temp/wpt-m53-flow-region-focused.log`). The horizontal caption expectations
use the caption's min-content contribution to its otherwise empty table
(CSS2 §17.5.2). No D4 ownership ruling changes, new arena aliases or
source/reference adjustments are needed.

The shared text publisher reverses its horizontal surrogate for sideways-lr
LTR only, leaving RTL on the wrong physical inline edge. Reverse that mapping
for both directions: bidi already selected the surrogate start/end before the
axis transformation ([CSS Writing Modes 4 §3.2](https://www.w3.org/TR/css-writing-modes-4/#valdef-writing-mode-sideways-lr)).
Use the box's used writing mode for this geometry boundary. The direct text
audit has three bare RTL sideways-lr counterexamples across HTML/CSS cells and
CSS captions; keep the independent asymmetric-decoration differences visible.

The next canonical sweep reaches 1,630 full matches but lowers the two
`logical-physical-mapping-001` partials. LLDB isolates a zero inline extent at
publication despite a measured nonzero line: an auto-height block's provisional
height contains only its inline-axis borders. The publisher prefers that
positive provisional height to the completed flow height
(`temp/wpt-m53-axis-trace.log`). Make its scalar input explicitly the completed
physical border-box height. Ordinary flow passes the measured physical height;
positioned flow and cells/captions pass their finalized used height. Convert
that one input to content size once, instead of inferring completion from
`block->height > 0`. A new retained regression covers 160 direct/nested,
decorated, direction and auto/definite/restoration states before qualification.

The attempted full native run is not qualification: it stalls in Cocoa
initialization after sandbox service denials. A process sample confirms the
main thread in `glfwInit` → `NSApplication run`
(`temp/wpt-m53-native-sandbox-sample.txt`); the process was terminated with 143.
Rerun the full native query and required gate with native graphics access;
do not skip the resize case or modify the harness.

The explicit handoff also exposes an existing omission in its caller's size
ownership check: a parallel auto-height vertical child already has a definite
parent-assigned inline size, recorded by
`vertical_auto_inline_size_constrained`, but that flag was omitted when choosing
between its natural text advance and its used inline extent. Preserve the used
extent through that existing flag. Six prior full cases identify the omission;
LLDB records a 160px assigned box with a 140px publication input, and the root
case records the same mismatch against its viewport basis
(`temp/wpt-m53-{filled,root}-axis-trace.log`). Extend the auto-inline regression
with parent constraint/restoration, adding 32 states (192 for that case, 632
across the three new native tests). The 160 exact-source browser controls already
show 160/160 box agreement and text agreement improving 149→160; this is separate
from the accepted cell/caption partial limits.

**Candidate acceptance.** The final canonical inventory reports **1,632 full,
348 remaining, zero execution errors**. Against M52 there are **13 full gains,
no full losses, no reduced partial metric and 1,949 unchanged records**
(`temp/wpt-m53-qualified-delta.json`). The eight cell/caption block-flow cases,
three table-cell alignment documents and both logical/physical mapping documents
now match fully. All six prior full matches exposed by the publication handoff
are restored through their assigned-inline-size owner, without fitting offsets.

All **352 native queries** pass with graphics access, including the three new
cases' **632 retained states**. The independent control audit records box
agreement **226→296/400** and direct text-range agreement **163→222/240**,
with no lost agreement (`temp/wpt-m53-qualified-controls-audit.json`). The 240
text checks are a subset of the same 400 source controls, not an additional
source count. All 160 auto-inline controls agree in both box and text geometry.
The original 240 cell/caption controls retain asymmetric table decoration and
atomic-only line-packing differences; the 18 text differences are decorated
cells. They remain partial support, not hidden exclusions.

Tables 57/57 and inline 169/169 remain accepted. Sizing is 774/776, exposing two
positioned fit-content/percentage regressions after the clean-margin snapshot
change; resolve those before qualification. Integer-cast lint and diff checks
pass. The current code and CLI are frozen for diagnosis in
`temp/wpt-m53-candidate-code-state.json` (23 cumulative source/test files).
The prepared baseline promotes only the 13 new full matches and rejects those
13 saved M52 results. Live baseline expectations remain unchanged until the
required Radiant gate terminates. No fixture, capture, tolerance or formal
ownership ruling changed (**D4.5.1v4 / D4.1.4v5**).

**Neighbor prerequisite.** Both sizing failures are horizontal, so the vertical
publisher is not their owner. `layout_intrinsic_horizontal_margin_pair` seeds
a clean previously resolved percentage margin; its declaration reducer then
skips a plain percentage rather than replacing it with zero. The resulting
negative used margin leaks into shrink-to-fit width (50px instead of 100px).
Route percentages through the existing zero-percentage intrinsic length helper
as well, preserving fixed terms in `calc()`. This follows CSS Sizing 3 §5.2.1
and keeps the computed/used distinction under **D4.5.1v4 / D4.1.4v5**. Add a
retained absolute/inline-block regression with positive/negative margins,
fixed-plus-percentage math and reset on both sides (20 states).

The new regression also finds the matching inline-block publication defect:
its existing cyclic-inline dependency query includes percentage padding but
omits percentage margins. A correctly measured 100px intrinsic width becomes
150px from a child's used margin box. Include both spacing kinds through the
same `LayoutCssBoxCandidates` loop, so that overflow does not reopen the
containing intrinsic size. The existing publication reducer then applies its
intrinsic bound; no fixture-specific width or offset is introduced.

**Final frozen candidate.** After the spacing prerequisites, all **353 native
queries** pass, including **652 states across four new retained tests**.
Tables **57/57**, sizing **776/776** and inline **169/169** are accepted.
The fresh canonical delta still has **1,632 full, 348 remaining**, **13 gains,
zero losses/reduced partials/errors and 1,949 unchanged records**. The refreshed
400-control box and 240-range audits retain their 296 and 222 agreements,
respectively, with no losses. Reports are
`temp/wpt-m53-{qualified-final-native.log,qualified-neighbors.log,
qualified-delta.json,qualified-controls-audit.json}`. The final code/CLI freeze
is `temp/wpt-m53-candidate-code-state.json` (CLI SHA-256
`69e0e1f8682a9f066a089b29d08d70f4ffd20a36c5072d95ed73f73d3f10c288`).
The prepared promotion is bound to that freeze and the unchanged M52 baseline.
Proceed with the required aggregate gate; no further feature work is selected
before this milestone is qualified.

The first required-gate attempt terminates during build: four direct font
metric callers in `lib/test_font_path_gtest.cpp` still use the handle-based API.
Preserve that public query and expose the computed-face acquisition wrapper as
`css_resolve_font_metric_unit_px`; layout and CSSOM use the latter. The wrapper
still delegates to the unchanged raw metric implementation. No test/harness
change is needed. `temp/wpt-m53-aggregate-build-api-failure.log` retains the
failure; refresh the freeze and qualification after the API repair.

**Aggregate multicol prerequisite.** The API-compatible gate terminates with
8,605 total, 8,115 full, 487 accepted partials, two existing skips and one layout
regression: multicol `getclientrects-001` (100/100/96.2 → 93.8/90/0). All 741 UI,
353 native text/layout and 504 paged checks pass. The saved M52 binary still
accepts all 362 multicol baseline entries. Direct tree comparison isolates the
change to sideways-lr RTL: its formatter's inline-only branch pre-normalizes
source offsets and sends columns in physical top-to-bottom order, while the
shared publisher now correctly reverses the horizontal surrogate for both
paragraph directions. Normalize that producer's RTL column order and inline-end
offset to the same surrogate used by the other multicol branch, sharing the
column-index helper instead of retaining separate mode-specific arrangements.
Leading break anchors follow the same frame. Add 30 relative/absolute retained
paint-fragment states across five writing modes, both directions and restoration.
Keep the existing captures and score floors. The failed gate and comparison
reports remain in `temp/wpt-m53-{multicol-failed-radiant-summary.json,
multicol-failed-layout.json,multicol-single-before-result.json,
multicol-single-current-result.json,multicol-axis-trace.log}`.

The retained check also isolates horizontal RTL source ordering in the
inline-only pass: unlike block groups, it returns before the later horizontal
mirror step. Let the shared column-index helper explicitly finalize horizontal
direction there, while block-group callers retain deferred mirroring. Associate
native paint rectangles by source byte index; native Range queries can split
bidi runs further and are a separate API contract. The expected column/row
positions follow the leading forced line, three 90px columns, 10px gaps and
four 80px Ahem words, matching the WPT assertions rather than a native offset.

The relative-positioned control additionally exposes the producer's line-width
handoff: it sets the line's surrogate right edge from physical content width,
although a vertical fragmentainer's inline limit is its column height. Keep
`BlockContext::content_width` on its physical axis for block-child sizing, but
set the line cursor's right edge to the computed column inline extent in both
writing orientations. This is the existing CSS Multicol fragmentainer-size
rule, not a separate relative-position offset.

With the real column inline limit installed, ordinary line alignment already
owns the unused inline-end space. Remove the older vertical RTL end-offset
compensation from both fragment placement paths instead of adding that space
twice. Column placement now only translates each correctly aligned line by its
column pitch; the shared vertical publisher owns the orientation transform.

**Multicol acceptance / refreshed candidate.** All **354 native queries** pass,
including **682 states across five new retained tests**. The 30 multicol states
pass on relative and absolute containers. Twenty independent browser controls
improve **16→20 box agreements** and **7→20 text-fragment agreements**, with no
loss (`temp/wpt-m53-multicol-{before,current}-audit.json`). The original multicol
baseline is again **334 full + 28 accepted partials**, including its existing
`getclientrects-001` score floor. Tables 57/57, sizing 776/776 and inline 169/169
remain accepted (`temp/wpt-m53-final-neighbors.log`).

The canonical writing-mode result is still **1,632 full, 348 remaining, zero
errors**, **13 gains, zero losses/reduced partials and 1,949 unchanged records**.
The final freeze now covers **24 cumulative source/test files** with CLI hash
`70e791817d46d5e363f8b8fc89cafc873329e2300d3470c4e928326f0bc0ea62`.
Refresh the previous 400-control evidence on this freeze, then repeat the
required aggregate for the repaired shared multicol path. Preserve the failed
aggregate record and keep live baseline expectations unchanged until it passes.

**M53 qualified.** The final `make test-radiant-baseline` exits 0 across all
29 runner groups: **8,606 total, 8,116 full, 488 accepted partials, two existing
skips, zero failures**. This includes 741 UI checks, 354 native text/layout
queries, 504 paged checks and all 5,274 layout expectations (4,790 full,
484 partial). Final source and CLI hashes still match the 24-file freeze.
The full 420-control audit retains **242→316 box agreements** and **170→242
direct text agreements across 260 checks**, with no lost agreement. Reports are
`temp/wpt-m53-{required-aggregate.log,radiant-summary.json,
aggregate-summary.json,aggregate-layout.json,all-controls-audit.json}`.

After that gate terminated, promoted only the **13 new full writing-mode
matches**, preserving every other entry and metric floor. The prepared manifest
binds all 1,980 canonical occurrences, including 109 repeated basenames, and
rejects all 13 saved predecessor outcomes. The promoted focused baseline accepts
**1,632 full + 348 partials, zero failures/skips**. The exact user command
`make layout suite=wpt-css-writing-modes` exits 0 and still reports all **348 raw
mismatches**. Final baseline SHA-256 is
`8c72eeea5a11e4cdb99e61d5dcd78bed39eaed78e27eba816b1ea63f44d42c74`;
proof is in `temp/wpt-m53-{baseline-promotion.json,baseline-promotion.patch,
promoted-baseline.log,user-command.log}`. No broad rerun is needed for these
stricter expectations: the completed aggregate observed all 13 as full on this
same binary. Integer-cast lint and diff checks pass.

The qualified source/binary/linked-corpus checkpoint is
`temp/wpt-m53-qualified-state.json`. The long-running goal remains **active**:
348 writing-mode fixtures still lack full conformance. Keep the asymmetric
orthogonal cell decoration and atomic-line packing differences explicit;
rank the remaining shared defects by common-page impact before selecting M54.
No next feature probe has run, and **D4.5.1v4 / D4.1.4v5** remain unchanged.

### M54 — Preserve horizontal content inside vertical table cells

**Entry and priority.** M53 is qualified, and the requested baseline integration
has been rechecked: all 5,274 configured layout entries retain their floors.
The 348 raw mismatches remain. Plaintext bidi triage shows the same font-metric
differences in the explicit-direction references, rather than a paragraph
direction defect. Select the shared table-flow defect before font fallback or
isolated atomic-baseline residue: mixed writing-mode tables must preserve the
readable horizontal content of their cells.

**First disagreement and owner.** `table-cell-valign-001.html` has correct cell
boxes but every horizontal `XX` rectangle is transposed from 100 by 50 to 50 by
100. `table_publish_vertical_geometry` unconditionally invokes the vertical
cell text mapper, including for horizontal cells. Track coordinates belong to
the table; content coordinates and the applicable alignment belong to the
cell's own flow. CSS Writing Modes 3 §§7.2–7.3 and §7.5 define these mappings:
<https://drafts.csswg.org/css-writing-modes-3/#orthogonal-flows> and
<https://drafts.csswg.org/css-writing-modes-3/#line-mappings>.

**Finite outcome.** Fix the original cell text/alignment failure and independent
controls for horizontal cell text and block children in vertical and sideways
tables, LTR/RTL, top/middle/bottom alignment, declared/automatic physical sizes,
and symmetric decoration. Retained tests must change table/cell modes and sizes
and restore them, proving that publication uses the current physical cell box
without repeated transposition. Parallel cells and ordinary horizontal tables
must retain their current behavior. Collapsed-border conflict resolution,
arbitrary row-span sizing, unrelated captions and font fallback remain separate
unless they expose a prerequisite of this flow correction.

**Implementation and verification budget.** Capture source-identical Chromium
controls and compare M53 before editing production code. Add a causal native
test, then repair the shared content-frame/publication owner and reuse existing
cell layout/alignment helpers. Rebuild only the CLI and text-query runner in the
edit loop. Once stable, run the full native query suite, table/inline/sizing
neighbors, the canonical 1,980-file delta and the required Radiant aggregate.
Promote only new full matches after all gates terminate successfully; preserve
all partial floors, source fixtures, captures and tolerances. Ownership remains
**D4.5.1v4 / D4.1.4v5**; no formal ruling changes.

**Implementation, pending qualification.** Horizontal orthogonal cell content
now borrows a physical frame while the table retains logical grid dimensions.
Its physical height contributes to vertical-grid columns; its physical width
contributes to rows. Publication reuses the existing cell formatter and
alignment helpers against the completed physical box, instead of running the
vertical glyph mapper. Horizontal content widths exclude cell decoration, which
the row-size calculation adds separately. No fixture/tag substitution or
font-specific offsets are introduced.

The original valign fixture and its reference match fully. All **240** initial
source-identical browser controls agree for boxes, text and block children
(**48** complete agreements on M53). The first canonical delta has **five new
full matches**, one improved partial, no reduced metrics and no lost fulls.
Table/inline/sizing neighbors retain **57 / 169 / 776** accepted entries.

The shared-row acceptance challenge adds two cells whose physical widths differ
and exercises table direction independently from cell direction. It exposes a
pre-existing prerequisite: source-column placement considers RTL but omits the
sideways-lr reversal. Reuse `layout_logical_start_is_physical_start` at that
table-grid coordinate boundary, following
[CSS Writing Modes 4 §3.2](https://www.w3.org/TR/css-writing-modes-4/#valdef-writing-mode-sideways-lr).
The causal native test fails on precisely those
sideways-lr cell positions; shared-row width growth and wrapping already pass.
The independent extension contains **192** cases (M53: **12** complete
agreements). This is part of completing the selected physical-frame boundary;
no general collapsed-border or caption audit is added.

The two new native tests cover **840 retained target states**, including
table/cell mode restoration, automatic/declared widths, shared-row growth and
wrap/unwrap, both independent directions and decoration. An additional **120**
intermediate cell-mode publications test restoration from mapped geometry.
The first full native run passes **355** tests before the shared-row extension;
final code must qualify both new cases and all required gates. Baseline
expectations remain unchanged during qualification.

**Candidate frozen for the aggregate.** Final focused tests pass, including
both retained regressions and the prior orthogonal vertical-cell coverage.
All **432** independent source controls agree for boxes, text and block children
(M53: **60** complete agreements; no metric losses). The final canonical
inventory is **1,640/1,980 full, 340 remaining, zero execution errors**: **eight
new full matches**, three improved partial records, **1,969 unchanged**, zero
lost fulls or reduced metrics. The additional fulls cover sideways row/table
progression. Integer-cast lint and diff checks pass. The source/CLI freeze is
`temp/wpt-m54-candidate-code-state.json` (CLI SHA-256
`0e00ce4f360954049acf6dbd9614e0c069f07ea7ffcbe216108d5bce864a80f7`).

The prepared baseline promotes only those eight full matches and rejects all
eight saved M53 outcomes; it leaves the live baseline and every other entry
unchanged until the required aggregate terminates. Controls and canonical proof:
`temp/wpt-m54-{control-summary.json,final-focused.log,qualified-delta.json,
baseline-promotion.json}` and `temp/wpt-writing-m54-final-candidate.json`.
The required `make test-radiant-baseline` is running on this frozen candidate;
final native, aggregate and promoted-baseline qualification remain pending.

**M54 qualified.** The required `make test-radiant-baseline` terminates with
exit 0 across all **29** runner groups: **8,608 total, 8,126 full passes, 480
accepted partials, two existing skips and zero failures**. This includes all
**356** native query tests, **741** UI checks, **504** paged checks and all
**5,274** configured layout entries (**4,798 full + 476 partial**, zero failures
or skips). The source and CLI hashes remain identical to the candidate freeze.
Reports: `temp/wpt-m54-{required-aggregate.log,radiant-summary.json,
aggregate-summary.json,aggregate-layout.json,final-native.log}`.

After the aggregate terminated, promoted only the **eight new full matches**,
preserving every other entry and metric floor. The prepared manifest binds all
1,980 canonical occurrences, including repeated basenames, and rejects the
eight predecessor outcomes. The promoted focused suite accepts **1,640 full +
340 partials**, zero failures/skips. The exact requested
`make layout suite=wpt-css-writing-modes` exits 0 while explicitly reporting
the **340 raw mismatches**. Baseline SHA-256:
`44f6c88fa5421fb7b14bd4d3b62ebecd5b85890d3f18965c2737e2b863dddf1a`.
Proof: `temp/wpt-m54-{baseline-promotion.json,baseline-promotion.patch,
promoted-baseline.log,user-command.log}`. No broad rerun is needed for these
stricter expectations: the completed aggregate observed those same eight fulls.

The milestone delivers horizontal cell flow and top/middle/bottom alignment in
vertical logical grids, content-based physical row/column contributions,
retained mode/size restoration, shared-row wrapping and correct sideways-lr
column progression. All **432** independent controls agree; integer-cast lint
and diff checks pass. Parallel cells and horizontal-table controls retain their
observed behavior. Broader mixed-cell baseline alignment, collapsed-border,
caption, font and inline conformance remain partial. Existing ownership rulings
**D4.5.1v4 / D4.1.4v5** are unchanged.

The qualified source/binary/corpus checkpoint is
`temp/wpt-m54-qualified-state.json`. The long-running goal remains **active**:
**340** writing-mode fixtures still lack full conformance. Rank the remaining
shared defects by common-page impact before selecting M55; no next feature
probe has run.

### M55 — Export content baselines from vertical block sequences

**Entry and priority.** M54 is qualified; HEAD, all 37 main-file fingerprints,
the CLI and both linked corpus files still match its checkpoint. There are
**340** raw mismatches and no execution errors. Triage separates platform font
fallback/metric differences from a deterministic Ahem layout defect: in
`inline-block-alignment-003.xht` and `-005.xht`, the child blocks are positioned
correctly but adjacent inline text is displaced by the atom's 60px physical
block-start padding. Prioritize ordinary atomic-inline baseline export, which
controls text/icon alignment and line extents, before platform fallback choices
or unrelated table-sizing residue.

**Owner and hypothesis.** Block flow propagates descendant baselines through
the horizontal surrogate before vertical child positions are published. The
vertical publisher translates direct-text baselines to the physical block
origin but excludes boxes containing block-flow children. Parent decoration
and unequal child physical block/inline extents therefore need explicit causal
checks. Trace the completed first/last baseline set through publication and
the atomic-inline alignment/re-alignment consumers; repair the shared owner
rather than applying a displacement to surrounding glyphs.

**CSS contract and finite outcome.** CSS 2 §10.8.1 exports an inline-block's
last in-flow line baseline; CSS Writing Modes 4 §§4.2–4.4 selects and aligns the
dominant baseline. CSS Align 3 §9.1 defines the first/last sets of a block
container from its contributing in-flow lines/blocks:
<https://www.w3.org/TR/CSS2/visudet.html#leading>,
<https://www.w3.org/TR/css-writing-modes-4/#inline-alignment>,
<https://www.w3.org/TR/css-align-3/#baseline-export>.
Complete automatic content-baseline export for parallel vertical/sideways
inline-blocks with direct forced lines and block-child sequences, unequal
child block sizes, physical decoration, both directions and retained size/mode
restoration. Horizontal and overflow-synthesized baselines are controls;
explicit first/last selection challenges the same baseline-set owner where
the independent browser supports it. Keep generic font fallback, orthogonal
atomic synthesis, collapsed tables and separate inline-table sizing outside
this milestone unless evidence identifies a shared prerequisite.

**Verification budget.** Capture independent source-identical controls before
production edits, add a causal retained native regression, then repair the
first owning disagreement using existing baseline/geometry helpers. Focused
native and browser comparisons precede inline/sizing/table neighbors and the
canonical 1,980-file delta. Freeze stable code and run the required full Radiant
gate once; promote only new fulls after it terminates successfully. Preserve
every baseline floor, raw fixture, capture and tolerance. Ownership rulings
**D4.5.1v4 / D4.1.4v5** remain unchanged.

**Status:** planned; independent controls and causal ownership evidence are
next. No M55 production edit has been made.

**Owning corrections, under verification.** The independent 240-case matrix
confirms that unequal child inline/block extents, rather than decoration alone,
make the exported surrogate offsets wrong. Rebuild contributing first/last
block-child baselines after physical child placement. Project each parallel
child's cached set through its own block-start direction into the parent's
block-start frame, excluding visual relative offsets and out-of-flow children.
Keep inline-line contributions with their existing owner and leave fragmented
column baseline publication to the fragmentation stage. The common publisher
now receives its active layout context so cached and live first/last sets stay
coherent. This raises complete control agreements from **112 to 144** without
changing the horizontal controls.

The remaining sideways controls identify two independent overrides at the same
publication boundary: text realignment discarded its computed line-baseline
offset, while atomic placement and glyph conversion discarded source inline
cursor offsets. Remove those exceptions. Sideways-lr reverses the complete
source cursor once, in both paragraph directions; sideways-rl retains the
ordinary source inline direction. Glyph conversion reads the used block flow,
so a non-applying inline writing-mode cannot change the projection.

All **240** source-identical browser controls now agree for boxes and text,
including direct forced lines, unequal block sequences, decoration, overflow
controls, RTL/LTR and explicit first/last selection. The new retained native
regression checks **70** states with mode/size/selection restoration and an
out-of-flow trailing descendant. It fails before the repair; focused prior
atomic-overflow and orthogonal-cell regressions also pass. Original inline-block
fixtures `003`, `005`, `slr-009` and `srl-008` match fully. The separate
inline-table sizing boundary remains unchanged and partial.

A disk-space failure interrupted archive indexing, rather than a compiler or
runtime check. Losslessly archived older M52/M53 executables and redundant M54
per-fixture probe copies, verified their hashes, removed the failed generated
archive and rebuilt serially. Checkpoints and evidence remain recoverable via
`temp/wpt-binary-archive.json` and `temp/wpt-m54-probe-files-archive.json`.
No source/corpus data was removed. The frozen candidate is
`temp/wpt-m55-candidate-code-state.json` (CLI SHA-256
`5ba25b6aeca586372caa401ac29b01a6b3571f9b4ade880d912d23e79e6008f2`);
full native, canonical delta, neighbors and the required aggregate are pending.

**Native ownership correction and final candidate.** The first broad native
run found one regression in `VerticalSourceBaselineOriginsIncludePaddingOnce`:
the new walk included a fieldset's rendered legend in its body baseline set.
Reuse `find_fieldset_rendered_legend` to exclude that separate box, as required
by the [HTML fieldset rendering model](https://html.spec.whatwg.org/multipage/rendering.html#the-fieldset-and-legend-elements).
Atomic inline children remain contributions of their existing line owner.
The existing regression is unchanged, and all **357** native tests now pass.

The final source-identical matrix still has **240/240** complete agreements;
the canonical delta has **21 new full matches**, **1,959 unchanged records**,
zero reduced metrics/lost fulls and zero execution errors. Current result:
**1,661/1,980 full, 319 remaining**. Gains include the five inline-block baseline
cases, two sideways text-baseline cases and fourteen sideways vertical-alignment
cases. The inline-table sizing observations remain partial and unchanged.
The updated CLI SHA-256 is
`e60bf6668fc0bb58d6de73b50071591bf61b3684ee97ccdbde06a528db49f3cb`;
the freeze remains `temp/wpt-m55-candidate-code-state.json`.

The prepared baseline promotes only these 21 full matches and rejects all 21
saved M54 outcomes; every other entry and floor is unchanged. Final control,
native and canonical proof is in `temp/wpt-m55-{final-controls-audit.json,
final-native.log,qualified-delta.json,baseline-promotion.json}` and
`temp/wpt-writing-m55-final-candidate.json`. Neighbor and required aggregate
qualification remain pending; live expectations have not been promoted.

**Ready for aggregate qualification.** All selected neighbors retain their
recorded expectations: inline **169**, sizing **776**, tables **57**, multicol
**334 full + 28 partial**. Integer-cast lint and diff checks pass. The frozen
candidate remains unchanged, and the required `make test-radiant-baseline` is
running with serial compilation to limit archive/link disk pressure. Final
aggregate, promoted-baseline and exact-user-command qualification are pending.

**M55 qualified.** The required `make test-radiant-baseline` terminates with
exit 0 across all **29** runner groups: **8,609 total, 8,148 full passes, 459
accepted partials, two existing skips and zero failures**. It includes all
**357** native query tests, **741** UI checks, **504** paged checks and all
**5,274** layout entries (**4,819 full + 455 partial**, no failures or skips).
The source and CLI hashes remain identical to the final candidate freeze.
Reports: `temp/wpt-m55-{required-aggregate.log,radiant-summary.json,
aggregate-summary.json,aggregate-layout.json,final-native.log}`.

After the aggregate terminated, promoted only the **21 new full matches**.
The manifest binds every canonical occurrence, preserves all other entries and
floors, and rejects all 21 saved M54 outcomes. The promoted focused suite accepts
**1,661 full + 319 partials**, with no failures or skips. The exact requested
`make layout suite=wpt-css-writing-modes` also exits 0 while explicitly reporting
the **319 raw mismatches**. Baseline SHA-256:
`71d27917fee671750f78643aa2c530823d9d085b64bc69e5b6055660c236c0b3`.
Proof: `temp/wpt-m55-{baseline-promotion.json,baseline-promotion.patch,
promoted-baseline.log,user-command.log}`. The completed aggregate already
observed those same 21 full matches; stricter expectations need no broad rerun.

The milestone delivers content-baseline export from parallel vertical block
sequences using published child positions, coherent first/last selection and
padding through retained restoration, correct fieldset-body ownership, and
shared sideways line/cursor projection. All **240** independent controls agree;
integer-cast lint and diff checks pass. Horizontal and overflow-synthesis
controls retain their observed behavior. Separate inline-table sizing, generic
font fallback/metrics and broader mixed-flow/fragmentation conformance remain
partial; no fixture, reference or tolerance was changed. Ownership rulings
**D4.5.1v4 / D4.1.4v5** remain unchanged.

The qualified source/binary/corpus checkpoint is
`temp/wpt-m55-qualified-state.json`. The long-running goal remains **active**:
**319** writing-mode fixtures still lack full conformance. Rank remaining
shared defects by common-page impact before selecting M56; no next feature
probe has run.

### M56 — Publish automatic table decoration in physical axes

**Entry and priority.** M55 is qualified: HEAD, all 37 main files, the CLI and
both linked corpus files match its checkpoint. There are **319** raw mismatches
and no execution errors. The four inline-table alignment cases share an earlier
size disagreement: a 60px physical left padding becomes 60px of physical height,
and the table loses that width. CSS tables and anonymous table structures are
used by ordinary page components as well as vertical content, so fix this shared
box/publication boundary before isolated border-conflict or font residue.

**Root and owner.** `table_auto_layout` stores used border-box geometry for both
CSS box-sizing values. `table_publish_vertical_geometry` removes source-frame
decoration, restores physical decoration and translates the grid origin only
when CSS `box-sizing` is `border-box`. That gate confuses declaration
interpretation with the representation of the completed used box. A CSS table
with automatic sizes must publish the same physical geometry under either
box-sizing value; HTML's border-box UA default currently hides the distinction.

**CSS contract.** CSS Sizing 3 §3.3 defines box-sizing as the interpretation of
quantitative sizing declarations; automatic sizes are unaffected. CSS Writing
Modes 3 §§6.4–7.2 maps layout calculations between logical axes while physical
box edges remain physical:
<https://www.w3.org/TR/css-sizing-3/#box-sizing>,
<https://www.w3.org/TR/css-writing-modes-3/#abstract-to-physical>,
<https://www.w3.org/TR/css-writing-modes-3/#dimension-mapping>.
Reuse the existing table publication and box-metric helpers; do not adjust text
or tracks by a fixture-specific offset or change the authored box-sizing value.

**Finite acceptance.** Automatic HTML tables, CSS tables with explicit cells,
and CSS rows with anonymous cells must retain physical asymmetric/symmetric
decoration under content-box/border-box, block/inline display, both directions
and all horizontal/vertical/sideways modes. Verify cell/row/text positions,
inline baseline alignment and following block flow. Retained tests change
box-sizing, decoration and mode, then restore them. Qualify the four original
inline-table fixtures. Explicit physical table sizes, general caption sizing,
collapsed-border conflict resolution and unrelated table track algorithms stay
separate unless they reveal a prerequisite; their recorded floors remain
protected by neighbors and the full inventory.

**Verification budget.** Capture independent source-identical controls before
production edits and add a causal native retained regression. Repair the
completed geometry frame, then run focused native/browser checks, table/inline/
sizing/multicol neighbors, all native queries, the canonical 1,980-file delta
and the required Radiant aggregate on frozen code. Promote only new fulls after
the gate terminates, preserving all other floors, source fixtures, references
and tolerances. Use serial builds while disk headroom is limited. Existing
ownership rulings **D4.5.1v4 / D4.1.4v5** remain unchanged.

**Implementation.** Automatic table publication now converts the
completed border box independently of authored box-sizing. Normal block flow
preserves the table grid's intrinsic inline measure, and sideways cell content
uses the shared used-flow line-axis reversal. All **360** source-identical
separated-table browser controls agree, and the new retained regression covers
**360** states. The first canonical inventory has **1,665** full matches:
the four original inline-table cases improve, with no losses or reduced scores.

**Boundary prerequisite.** The additional 180 caption/collapsed controls expose
an existing distinction between authored and used table decoration. CSS 2.2
§17.6.2 ignores table padding and uses half resolved outer borders in collapsed
layout (<https://www.w3.org/TR/CSS22/tables.html#collapsing-borders>). Publication
receives those used edges through a stack-owned `BoxEdges` output from
`table_auto_layout` before its metadata is destroyed. The retained
padding/box-sizing invariance regression passes, and all separated controls
remain matched; this does not extend the border-conflict algorithm.
General vertical caption sizing and asymmetric collapsed track/cell conflicts
already disagree in the predecessor and remain separate work.

**Final candidate evidence.** Both native regressions pass, covering **460**
retained states; all **359** native tests pass. The four neighboring baseline
categories have zero regressions: tables **57**, inline **169**, sizing **776**,
and multicol **334 full + 28 partial**. All 360 main browser controls still
agree. Of the 180 adjacent controls, the same 28 fully agree before and after;
no complete control is lost. The used-edge correction removes the four
aggregate geometry-error increases seen with authored-edge publication.
The canonical final delta remains four gains, **1,976 unchanged records**,
zero reduced metrics and zero execution errors. Proof is in
`temp/wpt-m56-control-summary.json` and `temp/wpt-m56-qualified-delta.json`.
Integer-cast lint and `git diff --check` pass. All 24 modified code sources and
the CLI are frozen in `temp/wpt-m56-candidate-code-state.json`.

**M56 qualified.** The required `make test-radiant-baseline` terminates with
exit **0**: **8,611 total, 8,154 full, 455 accepted partials, two existing
skips and zero failures** across all 29 groups. UI automation passes **741**,
paged media **504** and native text/layout **359**. Layout has **5,274** cases:
**4,823 full + 451 partial**, zero regressions. The frozen source and CLI hashes
remain unchanged after the gate. Proof:
`temp/wpt-m56-required-aggregate.log`, `temp/wpt-m56-aggregate-summary.json`
and `temp/wpt-m56-radiant-summary.json`.

After the aggregate terminates, only the four new full matches are promoted.
Preparation binds all 1,980 canonical occurrences and rejects the saved M55
outcomes for all four strengthened entries. No other floor, source fixture,
capture or tolerance changes. The promoted shared baseline passes
**1,665 full + 315 partial**, zero failures/skips. The exact requested
`make layout suite=wpt-css-writing-modes` exits **0** while explicitly reporting
the **315 raw mismatches**. Baseline SHA-256:
`b5b633862d1bcd5e18b174ab0df3fcbc694ed8b95441072af6a2263ba60a72da`.
Proof: `temp/wpt-m56-baseline-promotion.json`,
`temp/wpt-m56-promoted-baseline.log` and `temp/wpt-m56-user-command.log`.

The qualified checkpoint is `temp/wpt-m56-qualified-state.json`. No Lambda
ruling changes; **D4.5.1v4 / D4.1.4v5** remain unchanged. The goal remains
**active**, with vertical caption sizing, asymmetric collapsed track/cell
conflicts, empty collapsed tables, font fallback/metrics and broader mixed-flow
conformance still partial. Rank the remaining shared defects by common-page
impact before selecting M57; no next feature probe has run.

### M57 — Lay out captions in the table wrapper's physical inline space

**Entry and priority.** M56's HEAD, all source and linked corpus hashes, and
the CLI match its qualified checkpoint: **1,665 full, 315 remaining**. Caption
flow is next because it is an ordinary table feature with a shared axis-owner
defect. The four `caption-side-vlr/vrl` failures reproduce a 50px caption becoming
100px wide and a 100px wrapper becoming 200px wide. This precedes asymmetric
collapsed-border conflict residue, native-widget theme differences and the
remaining script-driven conformance pages; it does not rank by fixture count.

**Root hypothesis.** Caption width refinement uses the surrogate grid width
as a physical width. Parallel vertical captions do not publish their own flow,
and the final table swap changes view bounds without mapping text rectangles.
Caption contributions are then mixed between surrogate height and physical
width, risking duplicate wrapper extent and wrong grid offsets. The owning
seam is caption refinement/finish plus table publication. Keep the grid's
logical track representation, and use shared physical caption flow and box
helpers rather than swapping text or fitting an expected rectangle.

**CSS contract.** CSS 2.2 §17.4 makes captions ordinary block containers in the
table wrapper, with their own content and decoration; the wrapper's inline
measure follows the grid. CSS Writing Modes 3 §7.4 maps caption-side top/bottom
to the table's block-start/block-end, while caption content uses its own flow:
<https://www.w3.org/TR/CSS22/tables.html#model>,
<https://www.w3.org/TR/css-writing-modes-3/#logical-direction-layout>.
Existing selected-view and transient-grid ownership **D4.5.1v4 / D4.1.4v5**
remain authoritative; no Lambda ruling change is proposed.

**Finite acceptance.** Automatic separated HTML tables with parallel captions
must lay out short text, wrapped text and normal block children using the final
wrapper inline measure, then place top/bottom captions and following flow once.
Cover horizontal, both vertical and both sideways modes, both directions,
block/inline tables, and asymmetric table/caption decoration and margins.
Retained mutations change caption side, content, decoration and mode, then
restore. Preserve existing orthogonal captions, explicit empty-caption block
sizes, baseline export and multicol fragmentation. General explicit table
block sizes, multiple-caption policy and collapsed-border conflicts stay
separate unless a shared-owner prerequisite is demonstrated.

**Verification budget.** Capture 240 source-identical browser controls before
production edits, add a causal retained native regression and keep the four
original WPT cases. Use focused caption/orthogonal/baseline tests in the edit
loop. Once stable, qualify neighboring tables/inline/sizing/multicol, all native
queries, the canonical 1,980-file delta and the required full Radiant aggregate
on frozen code. Promote only new full matches after the aggregate terminates;
preserve every other floor, fixture, capture and tolerance. Use serial builds
and workspace scratch files while disk headroom is limited.

**Implementation.** Parallel vertical caption refinement uses the grid's final
physical inline measure through a scoped `used_size_owner`, preserving authored
auto sizing. The caption BFC measures its own block extent and publishes through
shared vertical-flow helpers. Table publication swaps only grid-coordinate
descendants; it counts caption margin boxes once and moves the grid prefix to
the correct physical side. The native regression reproduces the old failure
and passes **140 retained states** after the correction, including text,
caption-side, decoration, mode and explicit inline-size restoration.

**Boundary prerequisite.** Forty explicit-inline-size controls expose two
remaining shared-owner errors: table publication overwrites a caption's used
height, and normal-flow inline placement ignores the wrapper's RTL/sideways
progression. Preserve the caption BFC's dimensions and reuse
`layout_resolve_in_flow_margins` with the table's logical inline direction
(CSS 2.2 §10.3.3:
<https://www.w3.org/TR/CSS22/visudet.html#blockwidth>). The final **240 main + 40
boundary** source-identical browser controls all agree, including geometry and
wrapped text. Wrapped-line capture reuses the existing harness's
`reference_text_fragments.js`; no browser reference or comparator is changed.
The four original caption-side fixtures reach full element/span/text matches.

**Final candidate evidence.** All **360** native tests pass. Neighboring table,
inline and sizing baselines pass **57 / 169 / 776** full cases; multicol passes
**334 full + 28 partial**, with zero regressions. Canonical writing-mode results
are **1,669 full, 311 remaining, zero execution errors**: the four caption-side
cases improve, **1,976 records remain unchanged**, and no metric is reduced.
The 24 modified code sources and CLI are frozen in
`temp/wpt-m57-candidate-code-state.json`; integer-cast lint and diff checks pass.
Proof: `temp/wpt-m57-full-native.log`, `temp/wpt-m57-neighbors.log`,
`temp/wpt-m57-control-summary.json` and `temp/wpt-m57-qualified-delta.json`.

**M57 qualified.** The required `make test-radiant-baseline` terminates with
exit **0**: **8,612 total, 8,159 full, 451 accepted partials, two existing
skips and zero failures** across 29 groups. UI automation passes **741**,
paged media **504** and native text/layout **360**. Layout has **5,274** cases:
**4,827 full + 447 partial**, zero regressions. The frozen source and CLI hashes
remain unchanged after the gate. Proof:
`temp/wpt-m57-required-aggregate.log`, `temp/wpt-m57-aggregate-summary.json`
and `temp/wpt-m57-radiant-summary.json`.

After the aggregate terminates, only the four caption-side entries are promoted
to required full matches. All 1,980 canonical occurrences remain bound, every
other floor is preserved, and the saved M56 results are rejected for all four
strengthened entries. The promoted shared baseline passes **1,669 full + 311
partial**, zero failures/skips. The exact requested
`make layout suite=wpt-css-writing-modes` exits **0**, explicitly retaining the
**311 raw mismatches**. Baseline SHA-256:
`9b9bc8a92d68d137b6654934b3154f0d6cbac3e13b4b88ebf8eaf0fd67241d8f`.
Proof: `temp/wpt-m57-baseline-promotion.json`,
`temp/wpt-m57-promoted-baseline.log` and `temp/wpt-m57-user-command.log`.

**Wrap-up checkpoint.** Source, CLI and linked corpus snapshots are recorded in
`temp/wpt-m57-qualified-state.json`. No fixture, reference, tolerance or Lambda
ruling changes; **D4.5.1v4 / D4.1.4v5** remain unchanged. The broader conformance
objective is incomplete, with 311 raw mismatches and the separately recorded
multiple-caption, collapsed-grid, font and mixed-flow limits. The user requested
wrap-up: stop here without selecting M58. Earlier generated M56 per-fixture copies and
M17–M54 raw control reports were losslessly archived and SHA-verified to recover
disk space after an archive-link failure; source files, corpus and saved
qualification summaries were preserved.

### M58 — Collapse adjoining margins through empty vertical blocks

**Entry and priority.** The user resumed the long-running conformance goal.
Merged HEAD `87eb25b6d` reproduces all 1,980 M57 records exactly: **1,669 full,
311 remaining, zero execution errors** (`temp/wpt-m58-entry-delta.json`). Empty
wrapper and spacer blocks are common in document flow. Their shared margin
adjacency defect precedes isolated table border conflicts and font residue.

**Root and owner.** Vertical flow currently advances its logical cursor at
every block and collapses only the previous end with the next start. Empty
blocks join both edges into one adjoining set, so advancing at them duplicates
that set. The shared self-collapsing predicate also assumes physical height
and top/bottom decoration. Repair the flow edge traversal and cursor owner;
keep both margin signs until the whole adjoining set is known. Empty border
positions must follow CSS's hypothetical nonzero end-border rule independently
of the cursor used by following content.

**CSS contract.** CSS 2.2 §8.3.1 defines transitive adjacency through zero/auto
block sizes, mixed-sign collapse and empty border-edge positions. Writing Modes
3 §7.4 maps those rules into the containing flow's block axis:
<https://www.w3.org/TR/CSS22/box.html#collapsing-margins>,
<https://www.w3.org/TR/css-writing-modes-3/#logical-direction-layout>.
Existing ownership **D4.5.1v4 / D4.1.4v5** remains unchanged.

**Finite acceptance.** Cover one/multiple/nested empty siblings, leading and
trailing empties, positive/negative margin sets, automatic and zero block sizes,
both vertical and sideways modes and both directions. Borders, minimum sizes,
clearance and BFC boundaries must retain their separating behavior. Retained
native cases mutate size/decorations/margins/mode and restore the initial state.
Keep horizontal controls as a comparison; separately observed horizontal
mixed-negative empty-border differences are not silently assigned to this fix.
General mixed inline fragmentation and float clearance extensions remain
separate unless they block these acceptance cases.

**Verification budget.** Before production edits, capture source-identical
browser controls (120 initial controls, 44 complete agreements) and preserve
the merged CLI. Add a causal native regression; use focused cases while editing.
Then run relevant baseline neighbors, all native queries, the full canonical
writing-mode delta and the required Radiant gate on frozen code. Preserve
fixtures, captures, tolerances and all existing baseline floors; only promote
new full matches after qualification.

**Boundary prerequisites.** The initial traversal repair agrees with 108/120
controls. The remaining vertical nested-empty disagreements reveal that
`finalize_block_flow` treats a completed zero block extent as missing, retaining
the surrogate inline measure as physical width. Publish zero extents normally.
The retained native sequence also reveals a removed `min-block-size:1px`
constraint surviving recascade. Reset preferred/minimum/maximum axis slots and
percentage/keyword state together through `LayoutAxisRefs` before rebuilding
computed style (CSS Cascade 5 §7 defaulting). Neither change adds a new owner.
Eighty independent parent-adjacency controls cover automatic/minimum/definite/
zero sizes, one-sided decoration and BFC boundaries before qualification.

**Measurement prerequisite.** The first canonical delta exposes list/text and
body-flow regressions. Isolated cache/publication variants retain them; restoring
the old margin-gap consumer removes them. Diagnostics identify a text-bearing
`ol` with no child views yet being classified as self-collapsing during intrinsic
measurement. The margin proof must also inspect in-flow DOM text before views
exist. Extend the existing inline-content walker to descend block containers
for that query, preserving its existing default behavior. Parent minimum-size
controls also require CSS 2.2's rule separating the parent end from an empty
child set adjoining its start. These are prerequisites, not accepted baseline
reductions; qualification remains pending.

**Candidate behavior.** Vertical margin queries now traverse adjoining empty
sibling/descendant sets and remove the preceding empty border's hypothetical
advance before placing the next set. Physical-axis self-collapse checks share
the existing predicate; intrinsic probes also use the existing DOM content
walker. The root's document-wrapper parent is not a margin-collapse boundary
participant: reuse the existing root classifier, matching CSS 2.2 §8.3.1.
This restores the body-flow regression and makes its original fixture fully
match. Zero extents publish normally and removed axis constraints reset before
retained recascade.

The candidate passes **363 native tests**, including two new regressions with
**107 retained states**. All **160 vertical/sideways browser controls** agree.
Across 200 controls, complete agreements improve **54 → 190**, with no losses;
the 10 horizontal disagreements remain unchanged. Tables/inline/sizing/multicol
neighbors have zero regressions (**57 / 169 / 776 / 334 full + 28 partial**).
The canonical inventory reaches **1,687 full, 293 remaining, zero execution
errors**: **18 new fulls**, two additional partial improvements, **1,960
unchanged records**, no losses or reduced metrics. Lint and diff checks pass.
Proof: `temp/wpt-m58-{control-summary.json,final-delta.json,full-native.log,
neighbors.log}`. Four code files and the CLI are frozen in
`temp/wpt-m58-candidate-code-state.json`. Preparation binds 18 stricter baseline entries to their canonical fixtures,
rejecting every saved predecessor result and preserving all other floors.


**M58 qualified.** The required `make test-radiant-baseline` terminates with
exit **0** across all 29 groups: **8,631 total, 8,196 full, 433 accepted partials,
two existing skips and zero failures**. UI automation passes **743**, paged media
**504** and native text/layout **363**. The combined layout baseline passes
**4,845 full + 429 partial**, zero regressions. The frozen CLI and four code
hashes remain unchanged. Proof: `temp/wpt-m58-required-aggregate.log`,
`temp/wpt-m58-{aggregate-exit.json,aggregate-summary.json,radiant-summary.json}`.

After the aggregate terminates, only the 18 new full matches are promoted.
Baseline SHA-256:
`aaffe036e920eeb4bbefe9a60cd55e00b11d6db79fb2636468e23c75d184cc83`.
All 1,980 canonical entries remain bound; fixtures, captures, tolerances and
all other floors remain unchanged. The promoted focused baseline accepts
**1,687 full + 293 partial**, zero failures/skips. The requested
`make layout suite=wpt-css-writing-modes` exits **0**, explicitly reporting the
**293 raw mismatches**. Proof: `temp/wpt-m58-{baseline-promotion.json,
promoted-baseline.log,user-command.log}`. Ownership rulings
**D4.5.1v4 / D4.1.4v5** remain unchanged.

The qualified checkpoint is `temp/wpt-m58-qualified-state.json`, based on merged
HEAD `87eb25b6d`. The long-running goal remains **active** with **293** raw
mismatches. The milestone closes transitive empty-block margin adjacency in
parallel vertical/sideways flow, including retained size/decoration/mode
restoration and measurement/root boundaries. General horizontal empty-margin
residue, mixed inline fragmentation, table conflicts, font metrics and broader
script-driven conformance remain partial. Rank the remaining shared defects by
common-page impact before selecting M59; no next feature probe has run.

### M59 — Preserve float exclusions across sibling block publication

**Entry and priority.** M58's source, CLI and linked baseline hashes still
match its qualified checkpoint: **1,687 full, 293 remaining**. The previous goal
turn completed verified progress. Triage separates Persian font choice from
geometry: native `Times` falls back to Geeza Pro, while Chromium resolves Times
New Roman; explicitly named Times New Roman/Arial agree on the shaped advance.
Keep the default-family mismatch open instead of changing aliases to fit it.
Evidence: `temp/wpt-m59-font-triage-{browser.json,native.json}`.

An ordinary float crossing sibling block wrappers is the next shared defect.
`float-contiguous-vrl-006.xht` places later images at physical y=8 instead of
y=43, overlapping the earlier 35px-high banner float. Its wrappers share a BFC;
their own empty block extents do not retire that exclusion. This affects common
float-and-content components and precedes isolated table border conflicts.

**Owner and hypothesis.** Inspect BFC float registration, source-to-physical
publication and coordinate refresh. A completed sibling's physical rectangle
must remain represented in the same BFC query frame used by later blocks.
Reuse the existing float metrics/coordinate helpers and context ownership;
do not compensate the following float by the banner size.

**CSS contract.** CSS 2.2 §§9.4.1/9.5.1 makes float exclusions common to their
BFC and constrains later floats by prior margin boxes. Writing Modes 3 §7.4
maps those rules to the containing flow:
<https://www.w3.org/TR/CSS22/visuren.html#float-position>,
<https://www.w3.org/TR/css-writing-modes-3/#logical-direction-layout>.
Selected-view/pool ownership **D4.5.1v4 / D4.1.4v5** remains unchanged.

**Finite acceptance.** Earlier and later floats in separate normal block
wrappers must share exclusions in horizontal, vertical and sideways modes,
both directions and float sides, with asymmetric container decoration. A new
BFC isolates its contents while its outer box avoids prior floats. Retained
controls mutate the earlier float, available inline size and writing mode,
then restore. Qualify the original contiguous-float family. Broader float
fragmentation, table track conflicts and unrelated font policy remain separate.

**Verification budget.** Capture independent source-identical browser controls
and preserve the M58 predecessor before editing. Add a causal native regression,
then use focused checks during implementation. Once stable, run neighboring
float/inline/sizing/multicol baselines, all native queries, the canonical 1,980
delta, integer-cast lint and the required full Radiant gate. Preserve all
fixtures, captures, tolerances and baseline floors; promote only qualified new
full matches. Keep one candidate checkpoint and workspace scratch artifacts.

**First disagreement and implementation.** The 80 initial controls agree in all
horizontal and isolated-context cases, but all 32 shared vertical cases fail.
Registration retains the earlier float correctly. The later wrapper queries at
its provisional source block offset (130px), then publishes at used offset 0px.
The first repair routes float placement and prescan through the existing BFC
conversion and corrects completed block prefixes; the causal retained test
passes 48 states and control agreements improve 48 → 64.

The remaining RTL controls reveal a second part of the same conversion: used
inline margins move the wrapper after its float query. Extract the existing
normal-flow inline-placement helper and share it with BFC conversion. The
original paragraph case also demonstrates physical block margins being read
as provisional inline offsets. Thirty-two adjacent wrapper-margin controls
all disagree before refinement; compute an in-flow anchor's exact used block
start with the existing gap/clearance solver, including leading empty blocks,
rather than approximating its prefix by a last end margin. No new ownership.

**Used-placement evidence.** Sharing the existing inline margin solver and
using the exact normal-block gap/clearance anchor brings all **80 primary + 32
wrapper-margin controls** into agreement. The two original contiguous-float
fixtures reach full matches; the separately failing reference keeps its prior
score. The extended native regression passes **112 retained states**, including
both directions/sides, earlier-float size and containing-inline-size changes,
BFC isolation and a horizontal-mode transition with restoration. Canonical and
neighbor qualification follows on this candidate; no baseline changes yet.

**Avoidance boundary prerequisite.** The first full inventory has two gains,
1,978 unchanged records and no reduced metrics. Full native qualification finds
two existing avoidance/clearance regressions: the old fit consumer adds the
child's start margin before applying the new complete border anchor, counting
that margin twice. Use the complete vertical border anchor directly and route
the fit's parent conversion through the same BFC helper. Preserve the original
horizontal margin policy. These failures block qualification; no floor changes.

**Final candidate evidence.** All **364 native tests** pass after the border-anchor
correction. The original 112 controls still all agree (M58: 48), with no losses.
Position/tables/inline/sizing/multicol neighbors have zero regressions:
**139 / 57 / 169 / 776 / 334 full + 28 partial**. The canonical final delta is
**1,689 full, 291 remaining, zero execution errors**: two gains, **1,978 unchanged
records**, no losses or reduced metrics. Integer-cast lint and diff checks pass.
Six cumulative code files and the CLI are frozen in
`temp/wpt-m59-candidate-code-state.json`. Proof:
`temp/wpt-m59-{control-summary.json,final-delta.json,full-native-final.log,
neighbors.log}`. The required Radiant aggregate is running; two stricter entries
are prepared with canonical identities and both predecessor results rejected.
No baseline promotion yet.

**Aggregate split-inline prerequisite.** The first required aggregate exits 2:
all 364 native tests and writing-mode floors pass, but three `wpt-css-display`
flow-root cases regress. The old avoidance walk added inline wrapper origins,
then subtracted a provisional line origin; a separate static advance compensated
again at publication. Replacing only the walk exposed that inconsistent owner.
CSS 2.2 §9.2.1.1 makes the split block a sibling of anonymous blocks in its
containing block's formatting context. Use the same block-only coordinate walk
as registration and remove both inline-origin compensations. The float fit owns
any exclusion shift and width reduction. Ownership **D4.5.1v4 / D4.1.4v5** is
unchanged.

Twenty-four independent browser controls vary direction, float side, padding and
whether the fragment crosses the exclusion. M58 agrees in twelve; the first M59
candidate agrees in eight. An all-origin walk restores the three baseline cases
but retains the twelve older control failures, confirming that restoring the raw
sum alone is insufficient. A causal native regression exercises 36 retained
placement states. Keep this finite prerequisite within M59 and requalify the
complete owner correction before promotion. Evidence:
`temp/wpt-m59-{first-required-aggregate.log,display-wpt-css-display-failures.json,
inline-before-audit.json,inline-predecessor-audit.json,inline-causal-native.log}`.

**Retained split-fragment boundary.** The complete origin correction makes all
24 cold controls agree, including the twelve previously failing at M58. The
retained test then isolates a stale wrapper union: its child moves correctly,
but the split-inline extrema still include the prior position. Rebuild that
union when its owning inline layout starts; keep the existing presence-bit and
extrema helper instead of clearing unrelated retained state. No new storage or
owner (**D4.5.1v4 / D4.1.4v5**). Verify the same 36 moved/restored states before
repeating the canonical and aggregate qualification.

**Complete candidate evidence.** All **365 native tests** pass, including the
**148 retained states** in the two new regressions. The four independent browser
control families all agree: **160/160**, versus **72/160** at M58, with no losses.
Both empty and populated preceding blocks cover the inline split. All six
neighbors have zero regressions: display **145**, position **139**, tables **57**,
inline **169**, sizing **776**, multicol **334 full + 28 partial**. The canonical
1,980-case delta remains **1,689 full, 291 remaining**, zero execution errors,
two full gains, **1,978 unchanged**, no losses or reduced metrics. Integer-cast
lint and diff checks pass. Seven cumulative code files and CLI SHA-256
`a360773d45bd2927eb30be43bc77c45aa8b3d17091f1b5265a6f6e1382748460` are frozen in
`temp/wpt-m59-candidate-code-state.json`. Evidence:
`temp/wpt-m59-{complete-control-summary.json,complete-native.log,
complete-neighbors.log,complete-delta.json}` and
`temp/wpt-writing-m59-complete.json`. These are the inputs to the corrected
aggregate qualification below; only the two full gains are promoted.

**Qualified M59 result.** The corrected required Radiant aggregate exits **0**:
**8,633 tests, 8,200 full passes,
431 accepted partials, 2 existing skips,
zero failures across all 29 nonempty groups**. Layout contributes
**5,274 cases: 4,847 full + 427 partial,
zero regressions**; native queries **365**, UI automation **743** and paged media
**504** pass. Evidence: `temp/wpt-m59-{aggregate-summary.json,
radiant-summary.json,aggregate-exit.json,required-aggregate.log}`.

Only the two newly full contiguous-float entries were promoted. Both M58
outcomes fail those strengthened requirements; all other baseline floors are preserved. The
promoted baseline check and the exact requested
`make layout suite=wpt-css-writing-modes` both exit **0**, explicitly retaining
**291 raw mismatches**. No fixture, capture, comparator or tolerance changes.
Evidence: `temp/wpt-m59-{baseline-promotion.json,baseline-promotion.patch,
promoted-baseline.log,user-command.log}`. Ownership rulings
**D4.5.1v4 / D4.1.4v5** remain unchanged.

The qualified checkpoint is `temp/wpt-m59-qualified-state.json`, based on merged
HEAD `87eb25b6d`. The long-running goal remains **active** with **291** raw
mismatches. This milestone closes sibling-float exclusion coordinates, the
split-inline BFC boundary and moved/restored split-fragment bounds. General
mixed-flow/fragmentation, table conflicts, default-font policy and font metrics,
and broader script-driven conformance remain partial. A static filename grouping
is saved in `temp/wpt-m59-remaining-static-groups.json`; it does not diagnose
roots. Rank common-page impact before selecting M60; no next feature probe ran.

### M60 — Preserve own constraints in size-contained intrinsic contributions

**Priority / entry.** M59's source, CLI and baseline hashes still match its
qualified checkpoint (1,689 full, 291 remaining). The previous goal turn made
verified progress. Triage finds `available-size-021/022` collapsing horizontal
orthogonal parents around `contain:layout size; width:calc(2em + 8px)` children.
Independent controls also lose `min-width:70px` while the child itself remains
70px wide. This is a shared component-sizing defect; it precedes isolated table
conflicts and the broader upright glyph/paint implementation. Font-unit and
upright text advances remain a separate, explicitly open path.

**Contract / owner.** CSS Containment 2 §3.1 sizes intrinsic content as empty;
it does not discard the containment box's own sizing constraints. CSS Sizing 3
§4 constrains its intrinsic contribution, including a minimum larger than a
maximum. `measure_element_intrinsic_widths` returns early for empty/synthetic
containment before its existing own-size constraint consumer. Its early
`authored_definite_width` check also excludes math expressions that the later
consumer already supports. Reuse that computation and the existing min/max
solver, preserving content-only versus contribution queries and cyclic
percentage policy. No new retained owner (**D4.5.1v4 / D4.1.4v5**).
Sources: <https://www.w3.org/TR/css-contain-2/#size-containment>,
<https://www.w3.org/TR/css-sizing-3/#intrinsic-sizes>.

**Finite acceptance / budget.** Own fixed lengths and equivalent nonpercentage
math, min/max constraints, omitted and synthetic intrinsic fallbacks, padding /
borders and content-box / border-box must produce the same constrained outer
contribution in parallel and orthogonal contexts. Retained controls change own
constraints, descendants and containment, then restore. Add a causal native
regression before the repair. Use focused browser/native controls in the loop;
then run the original family, relevant sizing/inline/grid/flex boundaries,
all native queries, canonical 1,980 delta, integer-cast lint and the required
Radiant baseline once stable. Preserve fixtures, references, comparators,
tolerances and baseline floors. Keep independent orthogonal scrollport,
fragmentation and font-metric discrepancies open.

Initial evidence: `temp/wpt-m60-triage-results.json` and
`temp/wpt-m60-containment-before-audit.json` (72/80 browser agreements; eight
minimum-width parent collapses). This is a root-cause hypothesis pending the
causal measurement trace; no production edit yet.

**Implemented / focused evidence.** Fold nonpercentage math with the shared
length evaluator before classifying a preferred width. Merge remembered,
synthetic and empty containment into one content replacement, then reuse the
existing border-box min/max solver. The corrected causal native test fails with
M59's intrinsic implementation and passes with the repair; 96 retained states
cover own constraints, containment, descendant width changes and restoration.
The final focused check passes; all 366 native tests pass on the same production
code. Proof: `temp/wpt-m60-causal-ab.json`, `temp/wpt-m60-final-focused-native.log`.

Browser agreements improve **138 → 256 of 264**, with no lost agreements or
worsened box fields. All 240 containment/own-constraint controls and all float /
flex boundaries agree. Eight prior grid row-height mismatches remain, with their
widths now correct. Sizing / inline / table / multicol floors pass. The canonical
delta has **two improved partial records**, 1,978 unchanged records, zero reduced
metrics or execution errors; strict matches remain **1,689/1,980**. Specifically,
`available-size-021` elements improve 6.67% → 86.67% and text 18.90% → 85.83%;
`available-size-022` elements improve 12.50% → 71.88%. Their remaining differences
are independent. No baseline floor changes are warranted before full matches.
Evidence: `temp/wpt-m60-{control-summary.json,neighbors.log,delta.json}`.
Eight cumulative code files and the CLI are frozen in
`temp/wpt-m60-candidate-code-state.json`; qualification is recorded below.

**Qualified M60 result.** The required Radiant gate exits **0** across all 29
nonempty groups: **8,634 cases, 8,201 full passes,
431 accepted partials, 2 existing skips, zero failures**.
Layout contributes 5,274 cases (4,847 full +
427 partial); native queries 366, UI automation 743 and paged media
504 pass. The exact `make layout suite=wpt-css-writing-modes` exits **0** against
the unchanged baseline and explicitly retains **291 raw mismatches**. Proof:
`temp/wpt-m60-{aggregate-summary.json,aggregate-exit.json,required-aggregate.log,
user-command.log}`.

M60 closes own preferred/min/max constraints for the selected size-contained
intrinsic contributions. Strict writing-mode matches remain **1,689/1,980**;
the two partial improvements do not justify promoting either fixture to a full
pass. No fixture, reference, tolerance, comparator or baseline edits. The
qualified checkpoint is `temp/wpt-m60-qualified-state.json`, based on merged
HEAD `87eb25b6d`, and the long-running goal remains **active**. Independent grid
row-height, orthogonal scrollport/margin/fragmentation, font/glyph and scripted
conformance limitations stay open. Rank the next common-page defect before M61;
no next feature probe ran. Ownership **D4.5.1v4 / D4.1.4v5** is unchanged.

### M61 — Resolve upright used direction without changing computed inheritance

**Priority / entry.** M60's qualified source, binary and baseline hashes match.
The previous goal turn made verified progress. Orientation triage confirms a
larger missing glyph path: layout/paint still use horizontal advances after
vertical rectangle publication. The independent 180-source matrix agrees in
115 complete cases; CoreText supplies vertical glyph substitutions, positions
and a rotation matrix, so blindly using horizontal glyphs or dropping that
matrix would not implement the feature. Evidence:
`temp/wpt-m61-{orientation-before-audit.json,coretext-matrix.json}`.

The first dependency is the existing used-direction override.
`layout_setup_inline_direction` reads local `text-orientation`, treats sideways
box axes as vertical typography, and lets plaintext overwrite upright's LTR
requirement. The bidi input does not implement upright's strong-LTR treatment.
Repair that shared computed/used contract before wiring glyph advances and
paint frames; keep those subsequent stages in the active goal rather than
claiming that registration or direction alone completes orientation support.

**CSS contract / owner.** Writing Modes 3 §5.1 forces used direction to LTR for
upright vertical typography and treats its characters as strong LTR for bidi.
Computed direction remains available for inheritance, including a nested
horizontal formatting context. Sideways writing modes use horizontal typography.
Reuse the typed cascaded keyword consumer and the existing used-flow source
selection, then feed the resulting class policy into the shared UAX #9 pass.
Preserve source bytes and computed style; no retained owner change
(**D4.5.1v4 / D4.1.4v5**).
Source: <https://www.w3.org/TR/css-writing-modes-3/#text-orientation>.

**Finite acceptance / budget.** Local/inherited/reset/variable orientation,
LTR/RTL and all five writing modes; block and inline upright runs; normal,
override, isolate and plaintext bidi; nested horizontal atomic content retains
its inherited RTL. A retained native regression must fail causally, then pass
orientation and writing-mode mutation/restoration. Use browser controls and an
explicit LTR-override reference for the normative bidi rule when browser
support differs. Glyph metrics, vertical substitutions and orientation painting
remain the next delivery stages. Qualify focused originals, relevant bidi /
inline / sizing neighbors, all native queries, the canonical delta, integer-cast
lint and the full Radiant gate once stable. Preserve raw failures and all floors.


**Implemented / finite evidence.** A shared used-orientation/direction selector
consumes cascaded orientation, respects the line typographic mode and table-track
applicability, and preserves computed direction. Block alignment, margins and
table source columns use it; inline/plaintext scopes and the UAX #9 input share
its strong-LTR policy. FriBidi receives character classes rather than replacement
characters. Source bytes and synthetic CSS controls retain their ownership.
Four retained native cases cover 718 states: 280 computed/used inheritance and
writing-mode/restoration states, 360 bidi scope states, 30 table-track states and
48 root-flow margin states. The initial two native regressions fail on predecessor
production code (`temp/wpt-m61-causal-before.log`).

The broader check exposed a prerequisite: normal-flow flex/grid blocks received
an automatic vertical inline size only after child placement. Supply the shared
margin-constrained stretch result before their algorithms, and retain its definite
identity in flex without replacing authored auto or the existing flex-item size
assignment protocol. The 16 principal-flow controls improve 8 -> 16 agreements.
An early overbroad flex-item classification regressed a zero-sized range control;
the final change distinguishes normal-flow size ownership and restores that case.
The 40 explicit-LTR orientation controls improve 26 -> 40 agreements. Raw Chromium
captures are retained separately because upright used-direction support differs.
Evidence: `temp/wpt-m61-{direction-normative-final-audit.json,
root-final-audit.json,final-focused-native.log}`. Final qualification is pending.

**Observed reference limitation; no floor edits.** The canonical check exposes
four prior full Chromium matches as snapshot disagreements:
`table-progression-{vlr,vrl}-{003,004}`. Each WPT assertion requires upright RTL
tables to have the same source-column order as LTR. Fresh Chromium captures still
reverse the RTL tables. Forcing only reference direction to LTR makes their cell
geometry match native layout; retain both raw and explicit-reference captures in
`temp/wpt-m61-table-{oracle,audit}.json`. Do not revert the normative used-direction
rule or lower baseline floors to hide that discrepancy. The existing
`direction-upright-001` partial also changes when upright RTL source characters
become strong LTR; its missing glyph metrics/paint and mixed-script details remain
open. No fixture, capture, comparator, tolerance or baseline mutation is authorized
as a way to pass those records. Run the required gate and report its actual result.


**Final M61 validation.** All 370 native tests pass, including the four new
retained tests (718 states). All 56 geometry controls agree, and sizing (776),
inline (169), tables (57) and multicol (334 full + 28 accepted partials) preserve
their floors. The final canonical inventory has **1,685/1,980 Chromium snapshot
agreements, 295 raw mismatches, zero execution errors**: 1,975 records unchanged,
four prior full snapshots now differ under normative LTR table ordering, and
one prior upright-bidi partial changes. The 96 cells across all 12 affected
upright tables agree with the explicit-LTR reference; 54 agree with raw Chromium.

The full Radiant gate completes all 29 nonempty groups with **8,638 cases:
8,201 full passes, 431 accepted partials, 2 existing skips, 4 failures**.
Only the four documented writing-mode table snapshots fail. Native queries 370,
UI 743, paged media 504, vector rendering 241, page snapshots 51 and page load
111 pass. The exact `make layout suite=wpt-css-writing-modes` also exits **2**
and names those same four required-pass failures. Integer-cast lint and diff
checks pass. Evidence: `temp/wpt-m61-{aggregate-summary.json,aggregate-exit.json,
required-aggregate.log,user-command.log,user-command-exit.json,final-lint.log}`.

The used-direction and selected root inline-size behaviors are implemented;
M61 remains an **unqualified candidate** because the required gate is nonzero.
The baseline, fixtures, official captures, comparators and tolerances are
unchanged. M60 remains the last zero-failure checkpoint. M61 source, executable,
cumulative/incremental patches and evidence are retained by
`temp/wpt-m61-candidate-state.json` and `temp/wpt-m61-implementation.patch`.
The long-running goal remains **active**. Continue the shared glyph advances,
vertical substitutions and paint pipeline next, with the documented oracle
limitation visible rather than fitting source order to Chromium. Ownership
**D4.5.1v4 / D4.1.4v5** remains unchanged.

### M62 — Orient glyph units through shared metrics and paint frames

**Priority / entry.** M61's frozen source, executable and unchanged baseline
hashes match; its previous goal turn made verified progress. Preserve its four
explicit normative-versus-Chromium table disagreements. The next core feature is
usable vertical text: glyph measurement and painting still consume horizontal
advances after physical text-rectangle publication. Correct direction alone does
not supply the vertical glyph path. This affects ordinary CJK vertical text,
upright labels and sideways Latin, so it precedes isolated conformance residue.

**Owner / causal evidence.** The existing 180-source orientation matrix contains
horizontal advances in upright vertical lines. A fresh 72-case native font probe
shows CoreText vertical forms applying `vrt2`, including pre-rotated Latin glyphs;
disabling that feature through native settings also suppresses required CJK
alternates in the probe. The selected fonts contain separate OpenType `vert`
single-substitution lookups. CSS instead requires `vert` and applies its own
orientation. Do not blindly use the native vertical-forms matrix or glyph IDs.
Evidence: `temp/wpt-m62-{coretext-features.json,coretext-features.svg,
gsub-probe.json}`. Source:
<https://www.w3.org/TR/css-writing-modes-3/#text-orientation>,
<https://learn.microsoft.com/en-us/typography/opentype/spec/features_uz>.

**Design / delivery stages.**

1. Add a shared font primitive for vertical advances/origins and `vert`
   substitutions. Reuse table-directory/bounds/coverage helpers and extract
   shared table shapes before adding a second parser. Support the registered
   single-substitution shape and its extension wrapper. Read `vhea`/`vmtx` and
   `VORG`/TrueType bounds; synthesize missing vertical metrics from the selected
   face's existing content cell and baseline, with no family-specific policy.
   Import generated UAX #50 data pinned to Unicode 17, matching utf8proc, with
   provenance/license and a reproducible generator.
2. Feed one typographic-unit orientation/advance policy into actual layout,
   intrinsic sizing, bidi redistribution and source range measurements. Upright
   units keep combining marks together and shape cursive units in isolation;
   mixed text uses UAX #50; sideways typography retains horizontal composition.
   Font-relative advances must agree with the glyph advances actually used.
3. Carry the same glyph IDs, offsets and explicit physical frame through native
   raster and semantic glyph runs to SVG/PDF. Reuse the shared glyph traversal,
   bitmap/path lowerers and retained paint ownership; do not clone a glyph loop
   or paint horizontally into a vertically published rectangle. Glyph placement,
   fallback and retained reflow must agree across layout and paint.
4. Verify the end-to-end behavior, then qualify the milestone. Parser/kernel
   checks alone do not complete this feature.

The CSS contract is Writing Modes 3 §5.1/§5.1.1/§5.1.2 and UAX #50. Native frame
and font-style captures retain the selected-view owners under **D4.5.1v4 /
D4.1.4v5**; synchronous selected-face callbacks borrow handles only during the
visit. No vendor edits, source-byte rewriting or new conservative roots.

**Finite acceptance / budget.** Ahem and proportional Latin, CJK punctuation
with a real vertical alternate, combining units, inherited/local/reset orientation,
fallback faces, vertical-lr/rl and both sideways modes, LTR/RTL, wrapping and
retained orientation/font/size mutation with restoration. Compare actual range
advances and glyph positions, then raster/SVG/PDF geometry at relevant 1×/2×
densities. Use causal native failures and the existing browser matrix; extend
controls only to challenge the chosen owner. General GSUB shaping beyond the
registered `vert` single-substitution feature, uncommon language-system-specific
variants and broader vertical decoration fidelity stay explicit limitations,
without claiming full text-orientation conformance. Focused checks during the
edit loop; relevant neighbors, canonical 1,980 delta, required integer-cast lint
and one full Radiant gate after the complete path is stable. Preserve fixtures,
official references, comparators, tolerances and baseline floors.

**M62 implementation in progress (not qualified).** The generated Unicode 17
orientation and bidi lookups match all 1,114,112 scalar/codepoint slots in their
official data files. The font kernel covers authored/synthesized vertical
advances/origins, `vert` single substitutions and extension siblings; `vrt2`
remains excluded. A shared typographic-unit visitor now feeds upright line and
intrinsic measurement, CSS font-relative metrics, and positioned glyphs for
native raster and SVG/PDF outlines. Sideways segments retain horizontal shaping
and carry an explicit clockwise/counterclockwise frame. These are implementation
claims awaiting complete end-to-end acceptance, not a completed orientation
milestone.

Two prerequisite root causes were reproduced rather than fitted:

- **Selected font identity (D4.5.1v4 / D4.1.4v5).** Native fallback selected a
  CoreText collection face while parsed tables described file face zero. In the
  Songti probe, the native water glyph ID was 1211 but the parsed cmap reported
  1528; punctuation incorrectly synthesized 28px instead of its authored 20px
  advance. A lazy table provider now retains the selected native face and copies
  requested tables into its own pool. Platform fallback cache hits require the
  same selected face; they no longer replace the face on an already published
  handle. Font export consumes the common table accessor as well. A native
  retained-handle regression checks cmap IDs against native glyph IDs and
  vertical advances against authored native metrics through other selections.
- **Canonical combining-unit matching (CSS Fonts 4 §5.3).** CoreText substituted
  Menlo for decomposed `A + acute` in Ahem, although Ahem contains precomposed Á.
  Selected-face shaping now tries the supported canonical singleton before
  fallback, as required by the cluster matching algorithm. The existing Lambda
  normalization implementation was promoted into a shared lib helper; language
  adapters retain their former operations. DOM text/byte offsets stay unchanged.

Intermediate evidence: selected-face correction improves the existing 180-case
browser geometry matrix from 115 complete agreements before M62 to 135 (146 box,
147 text agreements). The former CJK-space regression passes after that correction.
The tcu-font fixture intentionally contains empty horizontal and vertical
punctuation outlines; real Hiragino alternates additionally exercise visible ink.
Remaining work includes source-range geometry, bidi paint publication, retained
mutation/wrap and visible native/SVG/PDF checks, canonical-suite delta and required
gates. The M61 four table snapshot/spec disagreements remain explicit. No official
fixture, reference, comparator, tolerance or baseline-floor changes have been made.

**M62 consumer checks, still in progress.** Canonical cluster matching brings the
180-case geometry matrix to **147 complete / 158 box / 147 text agreements**
(`temp/wpt-m62-canonical-audit.json`), compared with 115 complete before M62.
The unchanged native query suite passes **370/370**, font kernels **25/25**, and
focused glyph/text paint tests **12/12**. A new retained font/size/orientation
regression exercises 96 states and checks inline extents, frame axes and shared
outlines; it passes. Integer-cast lint passes.

Visible checks exposed and repaired two additional consumer causes. Sideways
segments now preserve whole combining units at segment boundaries, and canonical
matching can reshape a normalized segment without rewriting its source offsets.
The PDF selected-glyph adapter previously discarded the orientation frame; it now
records a complete font-space text matrix composed from the common glyph frame
and synthetic italic shear. Its Lambda writer emits that matrix, with precise
RootFrame ownership (**D5.3.3**), and preserves the run's logical ActualText.

The bounded 36-case visible matrix covers Arial `Ag`, Hiragino `「水、`, and Ahem
`A + acute, X` in both vertical and both sideways modes, with mixed/upright/sideways
orientation. Browser ink bounds agree within one pixel for **36/36 native raster
at 1×, 36/36 at 2×, 36/36 SVG, and 12/12 PDF**
(`temp/wpt-m62-visible-audit.json`). PDF bounds are read at the exporter's natural
72dpi coordinate scale, without fitting its page extent to the PNG viewport.
A temporary output-name collision was corrected: PDF captures have a separate
`-pdf.png` suffix and the twelve overwritten native PNGs were recaptured before
the successful comparison. All 120 render commands exit successfully. This does
not qualify unimplemented source-range/bidi publication or non-macOS selected-ID
raster support; those remain outstanding, alongside required aggregate gates.

**Next owning stages, before qualification.** Vertical text publication currently
omits the relative `TextRect::bidi_runs` that horizontal paint and partial ranges
consume. Publishing these runs requires mapping their relative geometry through
the same writing-mode frame as the source rect. The paint iterator still stores
inline advances in physical x/width fields and reverses RTL clusters on that
axis; it needs one explicit inline-axis projection shared by slice/recombine and
range consumers. Sideways shaping must retain logical cursive context rather than
shaping separately reversed letters; upright units remain isolated. Add bounded
mixed Hebrew/Arabic controls in both directions and four writing modes, and
compare a source substring and an ink placement, before changing those consumers.
Do not interpret the completed plain-glyph render matrix as bidi/range completion.

**M62a glyph-path checkpoint (M62 remains active).** The canonical unchanged
1,980-case inventory reaches **1,722 full snapshot matches / 258 remaining / zero
execution errors** (`temp/wpt-m62-glyph-inventory.json`). Against the frozen M61
candidate: **37 full gains, zero full losses, zero reduced scores, 48 changed and
1,932 byte-identical records** (`temp/wpt-m62-glyph-delta.json`). No baseline
promotion is implied. The Lambda + input baseline passes **8,390/8,390**
(`temp/wpt-m62-lambda-baseline.log`), covering the shared normalization adapter and
PDF matrix writer. The strengthened retained test uses canonical `A + acute, X`
in both directions through 192 font/size/orientation states and passes
(`temp/wpt-m62-retained-canonical-test.log`). Plain-glyph visible evidence remains
36/36 per raster density and SVG, 12/12 PDF. Preserve this checkpoint before the
pending bidi/range changes. Full Radiant qualification and exact user-command
floor checks are still pending for the complete M62 path; the existing four M61
upright table snapshot/spec disagreements must remain visible.

**M62b active design — resolved direction and source ranges.** The first 32
Hebrew/Arabic controls expose real mixed-RTL paint ordering failures even when
the outer boxes match. They also reproduce the upright browser discrepancy
explicitly documented in Writing Modes 3 §5.1 (automatic used LTR/strong-LTR
behavior remains unsupported by major implementations). Keep raw captures; use
separate explicit-LTR `bidi-override` browser controls for that normative case.
CSSOM View §9 additionally requires full typographic-unit bounds for a partially
selected grapheme. An executable native regression now checks UAX visual order
and that each partial DOM range occupies its painted physical inline interval.

The owning contract is a selected-face text query with a resolved LTR/RTL/auto
shaping direction, optional source byte range, and outputs for glyph positions,
advance and range caret offsets. It borrows the selected face synchronously
(**D4.5.1v4 / D4.1.4v5**). The existing glyph visitor delegates to it; native
CoreText line creation, selected-face validation and range conversion are shared.
Canonical matching preserves original source offsets by mapping normalized
prefix boundaries only after the source range expands to whole typographic units.
This avoids giving each consumer a different shaping or measurement loop.

Publish vertical directional runs, map their relative rectangles with the common
writing-mode transform, and make the paint iterator project/recombine on the
physical inline axis. Sideways runs retain logical bytes and shape using the
resolved bidi direction; upright units stay isolated and follow resolved unit
order. Source ranges consume the same face/direction/advances, project onto y in
vertical flow, and expand partial combining-unit coverage before measuring.
Verify focused order/range regressions, the bounded mixed-script controls and
unchanged plain-glyph controls first. Only then run the canonical delta,
neighboring gates and the required Radiant aggregate. Broader non-macOS shaping,
complex vertical decorations and uncommon font features remain explicit partial
support; this stage must not silently replace the original goal with plain LTR
painting or snapshot fitting.

**M62b source-range and cursive projection implementation.** The shared physical
inline-axis projection now carries vertical bidi rectangles through paint and
partial ranges. Focused retained tests check Hebrew visual order, painted
substring intervals, combining-unit coverage, uppercase expansion, collapsed
spaces, and both halves of a UTF-16 surrogate pair. Selection start coverage uses
the shared UTF-16 floor conversion while existing caret offsets retain their
round-up conversion. No DOM source bytes are rewritten.

The bounded Arabic ink check exposed an additional cause: the paint collector
split vertical RTL text into isolated clusters, and recombination used their
unjoined widths. It now preserves the complete published logical run for the
shared directed shaper. A regression verifies its physical inline interval in
both directions and all four vertical/sideways modes. Upright Arabic remains
isolated by typographic unit. Aggregate qualification and the final visible /
canonical comparisons are pending; non-macOS selected-ID raster/shaping,
vertical collapsed-caret placement, uncommon cross-node units and complex
justification/decorations remain explicitly partial.

**Contextual bidi width prerequisite.** Production tracing shows that preserving
paint's logical run is necessary but insufficient: the bidi scratch pass starts
from isolated scalar widths and scales the whole source rectangle. In
`A + Arabic + CJK + B`, that gives Latin and CJK part of Arabic's joining delta,
so directional-run origins disagree with the shared glyph query even though the
source union has the correct extent. Resolve levels before width normalization,
measure a contiguous contextual directional run with `text_visit_glyph_unit` in
segment mode, and use that run's advance before the existing rectangle-scale
step. Whitespace/control/orientation boundaries retain their existing owners;
upright units remain isolated. The regression must compare the published cursive
run extent with contextual font metrics, not merely compare two consumers of
the same incorrect rectangle. Then repeat the bounded visible controls before
qualification. This is a shared bidi measurement cause, not an ink-bound offset.

**M62b focused acceptance reached; qualification pending.** Contextual run
measurement plus complete logical paint projection removes the production
Arabic overlap. The bounded mixed Hebrew/Arabic controls now agree within one
pixel for **32/32 native ink bounds**
(`temp/wpt-m62-bidi-ink-contextual-width-normative.json`). Raw browser captures
remain intact; only upright vertical cases use separately captured explicit-LTR
override controls documented by Writing Modes §5.1. The focused five-test
order/range/cursive group passes, including a contextual-metric assertion on the
published run. Plain-glyph native 1×/2×, SVG and PDF controls remain **36/36,
36/36, 36/36 and 12/12** after the direction/range integration. Build succeeds
with zero errors. Freeze this path for the canonical delta and required gates;
these focused checks alone do not promote M62 or alter baseline floors.

**First aggregate is not qualified.** The frozen M62b gate runs 8,647 cases:
8,246 full, 392 partial, two existing skips and seven failures
(`temp/wpt-m62-required-aggregate.log`, receipt
`temp/wpt-m62-aggregate-exit.json`). Four are the existing upright table oracle
disagreements. Two new positioning failures are the RTL-in-RTL multicol cases;
the frozen M62a binary passes all 139 positioning floors. One font-unit assertion
still equates CSS upright advance with CoreText's generic vertical advance.
Native queries pass 376/376, vector tests 244/244, and UI automation 743/743.

The positioning trace exposes collapsed source indentation receiving advances in
bidi normalization: `XX` plus trailing newline/spaces has raw width 200 at 20px,
so its two glyphs receive only 8px while the collapsed suffix consumes 32px.
Publishing disconnected source pieces makes this prior scratch-width defect
observable in inline geometry. The existing trailing-space normalization is
restricted to directional spans or strong-bidi inline content, excluding a base
RTL paragraph containing Latin text. Extend that context predicate using the
resolved line direction, while retaining its check that a surviving measured
space belongs to the rectangle. Verify equivalent collapsed source suffixes in
horizontal, vertical and sideways modes before the original multicol cases.

For the font-unit failure, preserve raw/native cache assertions. CSS Values 4
§6.1.1 defines ch/ic from the used glyph advance in the inline axis, including
orientation; the CSS expectation must distinguish authored vertical metrics and
the synthesized content cell from the platform's unrelated generic advance.
Confirm the fixture's table/cell provenance before revising that assertion.
Finish these prerequisites before closing M62 or starting text-combine work.

**Font provenance check finds a real additional cause.** The independent browser
probe loads the exact unit-test files: upright Liberation ch is 11px at 10px font
size, while Noto ch/ic are 10px (`temp/wpt-m62-css-unit-browser.json`). Lambda's
raw-file Liberation cell was 10px. `font_get_handle_platform_metrics` only uses
the retained native face for document/scanned/fallback handles; an ordinary
`font_load_from_file` handle re-resolves its family name, which can select an
installed fallback instead of the unregistered file. Use the retained face
whenever present, preserving existing authored USE_TYPO_METRICS selection. Add
an independent selected-native-cell assertion alongside the authored vertical
table expectation, while keeping all raw glyph/cache checks. Revising the old
assertion alone would miss this defect. Requalify the selected font path and
related layout/PDF/runtime gates after the repair.

**Interior-space prerequisite found by the next delta.** The first suffix repair
passes positioning 139/139 and the reduced suffix test, but drops 119 writing-mode
matches (112 absolute-position fixtures). Trace of `1 2 34` shows the retained
interior space has positive width and zero cursor advance: the old normalization
clears the advance of every space when it trims any hidden suffix. Restrict that
cursor suppression to the zero-width discarded suffix, preserving the surviving
interior gap. A new Ahem regression checks the natural three-cell / two-cell
wrapped line extents in horizontal, vertical and sideways modes. Do not accept
the earlier focused pass or begin another feature before this delta is repaired.

The interior-gap correction restores every lost full match and gains bidi-003:
1,723/1,980 full, zero full losses and execution errors. Four partial text-combine
records still lose scores because using the selected native metrics bypassed the
existing catalog metric-alias normalization (e.g. serif's Times ascent policy).
Promote and reuse that existing normalization after selected-face measurement;
do not re-resolve the font family or add fixture offsets. Content-cell and normal
line-height queries must share the same selected-face/alias policy. Authored
document/scanned/fallback metric behavior retains its existing distinction.

**Repaired candidate ready for aggregate qualification.** Sharing the existing
catalog alias normalization restores all four reduced partial scores. The final
canonical delta is **1,723/1,980 full, 257 remaining, zero execution errors**;
against M61 it has **38 full gains, zero full losses, zero reduced scores, 51
changed and 1,929 unchanged records**
(`temp/wpt-m62-complete-m61-delta.json`). Positioning floors pass **139/139**.
The new suffix/interior-gap and existing bidi/range tests pass together, and all
five font-metric tests pass with independent selected-face cell assertions.

Fresh visible evidence for this repaired binary remains **32/32 mixed-script
native bounds**, **36/36 plain native bounds at each of 1×/2× and SVG**, and
**12/12 PDF**, with all 152 render commands successful
(`temp/wpt-m62-bidi-ink-complete-metrics-normative.json`,
`temp/wpt-m62-complete-visible-audit.json`). Integer-cast lint and diff whitespace
checks pass. The exact user command still reports the four original upright
table oracle disagreements; references and baseline floors are unchanged.

Freeze this implementation for fresh Lambda/input and Radiant aggregates. The
first aggregate's seven failures remain recorded as negative evidence, not
qualification for this repaired tree. Broader non-macOS selected-ID shaping,
vertical collapsed carets, cross-node units, complex decorations/justification
and uncommon native caret ambiguities under explicit cursive overrides remain
partial. The latter has a source-edge invariant probe in
`temp/wpt-m62-override-range-backlog.json`; do not imply exhaustive range support
from the bounded normal-bidi acceptance cases.

**User-requested wrap-up, 2026-10-29.** No further feature work was started. The
repaired candidate passes the fresh **8,390/8,390 Lambda + input baseline**. Its
fresh Radiant run passes **378/378 native text queries, 244/244 vector tests and
504/504 paged/font tests**, then is stopped during UI automation at the user's
request. This is an interrupted qualification, not an aggregate pass
(`temp/wpt-m62-complete-qualification-interrupted.json`). The source, executable
and unchanged baseline hashes were verified before recording this wrap-up.

The canonical result remains **1,723/1,980 full**, **38 full gains over M61**,
zero full losses, zero reduced scores and zero execution errors. The exact
requested command reports only the four documented upright table reference
disagreements. All 139 positioning floors pass. The final executable, all tracked
and untracked implementation sources, cumulative patch and evidence manifest are
preserved under `temp/wpt-m62-complete-candidate*`; the candidate remains
unqualified and no baseline promotion is made. Work is stopped for this wrap-up;
the current thread has no registered active goal. Resume by revalidating that checkpoint and completing the
Radiant aggregate before selecting the next finite milestone. Text-combine
planning is saved in `temp/wpt-m63-composition-design.md`, without implementation.

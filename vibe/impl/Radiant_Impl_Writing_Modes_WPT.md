# WPT writing modes and bidi conformance

Status: paused at the user's wrap-up request, 2026-10-27; resumed base `f49780766`.

The latest qualified milestone (M28) reports **1,462 / 1,980 full geometry
matches**, 518 mismatches and zero execution errors. The initial run had 646
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

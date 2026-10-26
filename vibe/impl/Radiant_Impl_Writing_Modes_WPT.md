# WPT writing modes and bidi conformance

Status: wrapped up at the user's request after Milestone 11, 2026-10-26.

The final restored user-command run reports **1,254 / 1,980 full geometry
matches**, 726 mismatches and zero execution errors, compared with the initial
646 matches and 156 execution errors. The conformance goal remains incomplete.
The mixed-inline sizing prototype is removed; its investigation and remaining
dependencies are recorded under the deferred milestone below.

## Objective and constraints

Fix `make layout suite=wpt-css-writing-modes` through shared, specification-based
engine changes. Preserve the upstream fixtures, Chromium reference geometry,
comparison tolerances and failure reporting. An observed score is evidence,
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

Delivery stops at qualified Milestone 11. Remaining mixed-inline intrinsic work
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

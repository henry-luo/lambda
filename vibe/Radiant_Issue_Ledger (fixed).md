# Radiant Issue Ledger — Fixed Issues

Resolved native Radiant issues under D9.1.1v4 and D9.1.3v2. Entries retain
their original IDs, reproduction evidence, fixes and verification limits.
Active issues remain in the [Radiant Issue Ledger](Radiant_Issue_Ledger.md).

Verification for RI-013/RI-014/RI-015 (2026-11-11): Lambda's final
`make test-radiant-baseline` exits 0 with **8,163 passed / 299 partial / 20 skipped**
of 8,482 cases and no failures. Owner `make test-native` passes 902 tests with
one unrelated, reproducible speech-provider failure recorded as RI-016.
The Lambda core gate passes 7,412/7,413, with its separate watercolor timeout
recorded as LR12-45. References, tolerances, deadlines and expected outputs
are unchanged. Full evidence is under `../../lambda/temp/kianhup-style/`.

## Fixed issues

<a id="ri-280"></a>

### RI-280 — The macOS caret query aborts for a hidden text control

**Fixed, 2026-11-27.** Homer Town Builder aborted when AppKit requested a
caret rectangle after a camera gesture. A programmatic value write on its
hidden textarea publishes a caret, while `display:none` leaves the element
without a layout box. The caret geometry query used an asserting cast even
though the DOM control and its selection can exist independently of layout.
Radiant's caret, selection-rectangle, point-hit, and line-navigation queries
now return unavailable when the text control has no box (D9.1.3v2).

`DynamicLayoutTest.HiddenTextareaHasNoEditingGeometryAfterValueWrite`
reproduced the same `view_type_is_block` assertion before the fix. After the
fix, all 151 dynamic layout tests pass, including the visible textarea
soft-wrap control. The rebuilt Lambda host passes the native Meadow zoom and
pan replay with a copy of the saved board, plus the Harbour and Walk replays.
The platform caret callback aborted after zoom in an isolated unfixed Meadow
replay and returned no rectangle in the fixed replay. Both traces are in
`temp/town-caret-control/probe.log`. The broader baseline's custom native
test link error and `dom_view_role_nested` geometry mismatch also reproduce
with the original editing geometry. See `temp/town-caret-control/`.

RI-272 through RI-279 are recorded in the
[test repair implementation note](impl/Radiant_Impl_Test_Repair_2026-11-28.md).

<a id="ri-279"></a>

### RI-279 — Native regression fixtures use obsolete output, clipping and sizing inputs

**Fixed in the working tree, 2026-11-28.** Four device-loss tests created
headless virtual submixes, which correctly do not join the physical bus. They
now create device-backed submixes with offline rendering; a separate check
confirms a virtual submix survives device retirement. The direct boundary
painter receives a nonempty clip, the ruby intrinsic query requests the box's
preferred-size contribution, and the nested-flex replay uses a definite basis
while retaining its percentage-basis rejection checks (**D9.1.3v2**).
The full Radiant document binary passes 545/545 with macOS service access.

<a id="ri-278"></a>

### RI-278 — Textarea family changes lose the user-agent font size

**Fixed in the working tree, 2026-11-28.** Changing only a textarea's font
family made inherited medium size replace its 13.333px user-agent size. Font
resolution now retains that size for both named and generic families, while
explicit `medium` and `inherit` sizes continue to resolve separately
(**D9.1.3v2**). The extended `WinningFontFamilyDeterminesTheMonospaceMediumAdjustment`
case and Chromium controls pass.

<a id="ri-277"></a>

### RI-277 — Table row minimum classifies a substituted length as a percentage

**Fixed in the working tree, 2026-11-28.** The table cell's row-minimum path
classified `var()` as a possible percentage before substitution, dropping a
definite length. It now consumes the shared substituted CSS size facts
(**D9.1.3v2**). `TableCellLengthsUseTheDeclaringFontAndOrientation` passes its
retained font, writing-mode and `var(--extent)` states.

<a id="ri-276"></a>

### RI-276 — Flex intrinsic measurement drops the percentage term of mixed margins

**Fixed in the working tree, 2026-11-28.** Intrinsic flex measurement resolved
a mixed `calc()` margin against zero even when the containing inline size was
known. It now resolves against that size on the containing writing-mode axis,
and keeps zero as the cyclic fallback when no basis exists (**D9.1.3v2**).
`GridIntrinsicMarginsResolveChildVariablesFontAndPercentageBasis` passes its
live custom-property states and agrees with Chromium.

<a id="ri-275"></a>

### RI-275 — Ruby annotation realignment doubles an atomic child's relative offset

**Fixed in the working tree, 2026-11-28.** A later inline font-box alignment
replaced the wrapper's already-shifted vertical coordinate. Ruby then
subtracted the wrapper's offset a second time while positioning its atomic
child. Rebased font boxes now retain applied inline-chain offsets
(**D9.1.3v2**). `RubyRelativeOffsetsSurviveColumnAndLinePlacement` and the
full document binary pass.

<a id="ri-274"></a>

### RI-274 — Provisional flex heights and embedded SVG minimums use incorrect constraints

**Fixed in the working tree, 2026-11-28.** A provisional height inherited
from an auto-sized flex row was treated as a definite percentage basis, so an
SVG resolved to 30px instead of its 10px natural height. Embedded SVG also
missed its user-agent hidden overflow, and replaced flex automatic minimums
used an authored main size as the content suggestion. The percentage basis,
overflow default and content suggestion now retain their separate constraints;
the suggestion still receives a definite cross-size transfer (**D9.1.3v2**).
The auto/definite/auto height transition, dynamic overflow controls and the
eight existing image-basis cases pass; Chromium agrees with the tested geometry.

<a id="ri-273"></a>

### RI-273 — Aborted DOM edits leave host and descendant attributes changed

**Fixed in the working tree, 2026-11-28.** A rejected editing transaction
restored its host and descendant content but did not snapshot and restore their
attributes. The rollback path now captures host attributes alongside its tree
snapshots and restores each element through the shared attribute restore helper,
notifying DOM consumers as restored values are applied (**D10.2.1 / D5.5.1 /
D9.1.3v2**).
`RadiantDocumentTest.AbortedEditRestoresHostAndDescendantAttributes` passes.

<a id="ri-272"></a>

### RI-272 — Detached layout measurement closes an uninitialized counter checkpoint

**Fixed in the working tree, 2026-11-28.** A detached inline measurement can
create an idle `CounterCheckpoint` without a counter context. Its three
`lam::Up<>` owners had no null initializers, so `close()` could read an
indeterminate owner and crash while returning from scalar-control sizing.
Initializing the owners to null makes the idle checkpoint safe to close
(**D5.5.1 / D9.1.3v2**). `RadiantDocumentTest.ScalarControlSizingPreservesContainingFlexContent` passes.

<a id="ri-271"></a>

### RI-271 — Text measurement repeated writing-orientation queries

**Fixed, 2026-11-27.** Intrinsic text measurement resolved orientation
for every measured cluster without using the layout context's policy cache.
Text runs now share that cache through their owning DOM node. The resolved
inherited-declaration mask also proves when `text-orientation` or
`text-combine-upright` has its default throughout an ancestor chain
(**D5.5.1**). The debug layout baseline passed 5,632 cases. Profiles and
case timings are under `temp/radiant-case-tuning/`.

<a id="ri-270"></a>

### RI-270 — The animation mixer scanned all bindings for each sampled track

**Fixed, 2026-11-27.** CPU samples of the Fishing assisted-catch replay showed
animation sampling walking the mixer's property list for each channel. An
action now retains one binding per immutable channel when admitted, and the
existing property reference counts govern the lifetime of those bindings
(**S12.1.5v2 / D5.5.1**). Sampling and root-motion extraction use the retained
bindings. Removing an action releases its references before pruning unused
properties. Evidence is in `temp/radiant-under-five/`.

<a id="ri-269"></a>

### RI-269 — Custom-layout paint test uses a stale aggregate initializer

**Fixed, 2026-11-27.** `make test-radiant-full` failed while compiling
`test_layout_custom_gtest.cpp`: its two-field aggregate initializer still
placed the layer array first, but `CustomLayoutPaintState` now starts with a
document-resource owner (**D5.5.1**). The test now zero-initializes the state
and assigns `layers` and `layer_count` by name, leaving the owner empty because
this test covers stacking order only. The suite passes all 13 cases; the full
gate now compiles and reaches runtime tests, where unrelated failures and
timeouts remain.

<a id="ri-268"></a>

### RI-268 — Scene3D animation repeatedly revalidated live targets

**Fixed, 2026-11-27.** In the Fishing assisted-catch replay, CPU samples
showed repeated DOM ownership walks from `scene3d_animation_write`.
Sampling passed each weak target by value, so a successful validation
could not refresh its lifetime stamp after retirement elsewhere in the
document. Scene3D retains the validated stamp and shares one ownership
snapshot across bindings when a sampling tick needs to validate them
again. Every target still checks its identity and whether it remains in
the scene (**D5.5.1 / S12.1.5v2**).
The debug baseline passed all 659 native Scene3D cases. With the scene
and worker budget changes, the assisted-catch replay took 7.79 s.

<a id="ri-267"></a>

### RI-267 — Flex aspect ratios use the wrong width or wrapping height

**Fixed, 2026-11-27.** The viewer's small DOT preview enlarged its circular
inline flex nodes by transferring the available width before shrink-to-fit.
Wrapping columns also summed all item heights into the automatic minimum,
preventing `flex-aspect-ratio-008` from wrapping. Radiant now transfers the
measured width before line collection and uses the largest item extent for
a wrapping column's minimum (**D9.1.3v2 / D9.5.6v2**). The sizing baseline
passes all 776 cases, and `doc_viewer_preview_scrollbars` passes all 14
assertions. [CSS Flexbox line sizing](https://www.w3.org/TR/css-flexbox-1/#line-sizing).

<a id="ri-266"></a>

### RI-266 — A degenerate SVG ratio produces an infinite layout height

**Fixed, 2026-11-27.** `svg-no-ar-min-height-min-content.html` divided by
the natural ratio of a zero-width SVG and emitted `inf` in streamed layout
JSON. The invalid output aborted a sizing batch. SVG sizing now keeps
natural dimensions and publishes a ratio only when it is finite and
positive (**D9.1.3v2**). All 776 recorded sizing cases pass.
[CSS Images natural dimensions](https://www.w3.org/TR/css-images-3/#natural-dimensions).

<a id="ri-263"></a>

### RI-263 — Indefinite grid tracks collapse or absorb their gutters

**Fixed, 2026-11-27.** An auto-height grid with two flexible rows and
spanning content collapsed all four tracks to zero. In the transposed
max-content grid, the flexible column widths included the gutters a second
time. Radiant now runs the content-based flex fraction phase on indefinite
rows. The synthesized track space excludes gutters, while the reported
container extent includes them (**D9.1.3v2**).
[CSS Grid Level 2 §12.7](https://www.w3.org/TR/css-grid-2/#algo-flex-tracks).

Both axis reductions match Chromium with 58 px flexible tracks and a
152 px container. The 57 other passing grid checks and the three existing
flex-content alignment failures retain their results. Static and JS layout
outcomes are unchanged. The ordinary Figma Pricing viewport no longer has
overlapping cards. Both pairs clear high severity (**D10.1.7**).
[Regression](../test/online/render_grid_spanning_minimum.test.cjs) ·
[Round 223](../test/online/comparison/repairs.html#round-223).

<a id="ri-262"></a>

### RI-262 — Windowless file export skips native WebGL host acquisition

**Fixed, 2026-11-27.** HTML file export permits acquiring a native graphics
host on demand. Scene3D already requested that host, while the WebGL Canvas
provider required a window to exist before `getContext('webgl2')`. It now
follows the existing export policy when a canvas first requests WebGL. A
missing native provider still yields a null context (**D9.1.3v2 / D9.5.6v2 /
D10.1.7**).

The four WebGL export tests start windowless and check rendered red and blue
pixels through PNG, SVG and PDF. The preceding release fails the same
control. The Canvas 2D control also passes. Handhold draws its ribbon in
both ordinary native runs. Chromium returns no WebGL context on this host
for the shared local control and omits the ribbon in both current live
references. Its consent dialog is also inconsistent between pairs. Handhold
remains high. Evidence is in `temp/online-visual-round248/`.
[Export tests](../test/test_pdf_render_visual_gtest.cpp:7990).

<a id="ri-261"></a>

### RI-261 — A ratio-sized flex badge loses its descendant percentage height

**Fixed, 2026-11-27.** FramerAuth's initial SVG has no `viewBox` and fills
a percentage-height wrapper inside an aspect-ratio flex item. Radiant
gave the icon 150 px of height and its badge 166 px. Chromium used 24 px
and 40 px with the same markup and CSS (**D9.1.1v4 / D9.1.3v2**).

Intrinsic height queries now identify the box that supplies the transferred
height. During layout, a provisional flex item record from an `aspect-ratio`
declaration no longer overrides the actual parent formatting context. The
SVG viewport uses the current containing block when deciding whether its
percentage height is definite. [CSS Sizing 4 §4.2–4.3](https://www.w3.org/TR/css-sizing-4/#aspect-ratio).

Two reduced controls cover explicit `aspect-ratio: auto` and `inherit` on
the wrapper. Both now produce 24 px for the flex row, icon, wrapper and
SVG. The six existing intrinsic image controls also pass. Both ordinary
FramerAuth pairs show the compact badge. Its hero remains lower and the
cross-origin demo is blank. The static and JS layout gate results are
unchanged. Evidence is in `temp/online-visual-round248/`.
[Regression](../test/online/render_intrinsic_image_basis.test.cjs) ·
[Round 221](../test/online/comparison/repairs.html#round-221).

<a id="ri-259"></a>

### RI-259 — Cached previews repeat stylesheet, font and layout allocation

**Fixed 2026-11-26.** Switching between the math Markdown and intensive TeX
fixtures in `lambda view` recreated 298 parsed inline sheets per round in
the same document pool. Reconnecting an unchanged style element now reuses
its unexposed sheet. Stylesheet removal still withdraws its rules from the
cascade. An author that obtained the old CSSOM object retains its own copy.
Animation and font caches are invalidated when the changed sheets contribute
their at-rules. CSS font registration publishes directly to the shared font
registry (D5.5.1, D5.5.3, D10.2.4 and LI-127).

Retained layout replay now allocates live text rectangles from the same list
to which it returns them. Retiring a border releases its computed corners,
with layout and paint sharing the currently owned snapshot (D5.5.1, D5.5.3).

<a id="ri-258"></a>

### RI-258 — A broad rebuild loses the input measurements of its completed container cascade

**Fixed, 2026-11-26.** Broad DOM reconciliation applied the new cascade
against existing query container boxes, then reset their view resources.
Layout compared its first probe to an empty baseline and repeated the
full probe and cascade even when the query measurements were unchanged.
The Radiant rebuild now snapshots the same eligible container boxes
before releasing the view resources. Committed effects still follow the
settled query pass, and changed dimensions or names still recascade
(**D5.5.1 / D9.1.1v4 / D9.1.3v2**).

The stylesheet mutation control has five matching native and Chromium
CSSOM observations. Unchanged input needs two native passes, down from
three. Three changed input cases still need three. All 62 local render
tests and 13 UI fixtures pass. Dock’s stable broad rebuilds take about
8.5–8.8 seconds in the candidate trace, compared with 14.5–15.5 seconds
in the preceding release. Its ordinary capture still times out (RI-031).
Evidence is in `temp/online-visual-round244/`.
[Regression](../test/online/render_relational_mutations.test.cjs).

<a id="ri-257"></a>

### RI-257 — SVG auto-height measurement ignores its resolved width

**Fixed, 2026-11-26.** An SVG with 777 × 358 presentation attributes used
a CSS width of 576 px and a height percentage without a definite basis.
Its final viewport was 265.39 px tall, but its flex wrapper measured
358 px. Chromium used 265.38 px for both. Ease Health’s original hero
stylesheet produced the same mismatch at a larger width.

Intrinsic replaced-height measurement transfers a definite resolved width
through the SVG’s natural ratio when the block dimension is automatic.
The other intrinsic queries retain the natural-size fallback
(**D9.1.3v2 / D5.5.1**). The regression checks geometry and the area below
the wrapper. The ordinary captures restore the product animation, with a
residual crop at the right edge during the card reveal (**D10.1.7**).
[Regression](../test/online/render_svg_flex_ratio.test.cjs) ·
[Round 214](../test/online/comparison/repairs.html#round-214).

<a id="ri-256"></a>

### RI-256 — Flex layout loses ratio-transferred height

**Fixed, 2026-11-26.** A 240 px flex container with an 8:5 ratio and a short
text child shrank to 20 px. In a nested column, its parent assigned 150 px
and late child wrapping replaced that height with 120 px. Chromium kept
150 px in both cases. Refrakt reproduced the nested loss with its original
stylesheet and a script-free replay. Its 1,200 × 750 px gallery clipped to
125.4 px in Radiant. [CSS Box Sizing Level 4](https://drafts.csswg.org/css-sizing-4/#aspect-ratio).

Final flex sizing transfers a definite width through the preferred ratio
before distributing space. A content-based automatic minimum can grow the
box, while intrinsic probes retain their natural measurement. Late layout
of a nested column reuses the fixed-axis decision that already recognizes
the parent's assigned height (**D9.1.3v2 / D5.5.1**).

All 62 focused controls pass. The new geometry fixture covers row and column
flex, growth, overflow, max-height, border-box sizing and nested text wrap.
Both ordinary Refrakt captures restore the gallery and centered title at
the existing frame (**D10.1.7**). [Round 213](../test/online/comparison/repairs.html#round-213).

<a id="ri-255"></a>

### RI-255 — Shadow stylesheets skip their own host

**Fixed, 2026-11-26.** The scoped stylesheet walk began at the shadow
fragment. It matched the nodes inside the fragment and never collected
rules on the host. Linktree and Bento left their consent panels below the
viewport. Teachable drew its panel text over a transparent background
because the host's custom properties were missing.

Full and per element cascades now apply the host's own shadow sheets before
its descendants read inherited values. They use the source tree and
declaration context from [LI-125](<Lib_Input_Issue_Ledger (fixed).md#li-125>)
under **D9.1.1v4 / D9.1.3v2 / D5.5.1**. The fixture checks fixed geometry,
paint, a generated box, inherited values, and live class, attribute, CSSOM
and media changes. It passes in AST, MIR and Chromium. All 15 selected
AST shadow and slot checks and the Radiant integer cast lint pass.

Linktree and Bento restore the complete consent panel in ordinary matched
captures and clear high severity at 1.45% and 1.14% mean error. Teachable's
panel now has a readable background, text and button. The current Chromium
capture has no panel and shows the primary offer action. The current
Lambda frame still rates high. Evidence is in
`temp/online-visual-round235/` and [round 207](../test/online/comparison/repairs.html#round-207).

<a id="ri-254"></a>

### RI-254 — Nested flex height queries lose the percentage containing width

**Fixed, 2026-11-26.** Native layout ownership **D9.1.3v2**, with selected
measurement state governed by **D5.5.1**. Comand 524's hero column measures
355.6px instead of 420.4px, moving its correctly sized headline down 129.6px
and clipping the copy/action. The unchanged saved, script-free document
reproduces the live result. The RI-065 inline formatter remains correct.

The child-height fallback computes a fixed-width parent's content width,
subtracts child margins and passes the wrapping width into the intrinsic
query. It drops the distinct percentage containing width. Resolving the
child's `width:100%` then uses a stale ancestor basis: a reduced 240px query
becomes 1200px or 560px, losing a wrapped line. A percentage-width group
happens to materialize styles through a different earlier query and hides
the error. Recascading also hides the first-layout defect in the old test.

The shared fallback now performs the height query while supplying both
widths to the existing containing-block scope. Both callers use that helper;
there is no alternate line algorithm, authored-width override or site check.
The existing dynamic regression now checks the first fixed-width layout,
horizontal child margins, fixed/percentage group widths, recascade, break
visibility, repeated constrained queries and selected text preservation.
Its new cold-layout assertions fail before the repair by 36px.

**Qualification:** all 11 focused intrinsic-height, column, ratio and
percentage-basis tests pass; the layout integer-cast lint passes. The saved
Comand document now measures 420.4px and places the headline at 333.6px,
against current Chromium's 420.391px and 333.609px. The ordinary live PNG
also restores the copy and primary action placement. The missing background
video remains separate; this is not a complete Comand clearance. Broad
baselines have not been repeated for this layout change.

Evidence: `temp/online-visual-round234/{unit-before.log,unit-final.log,
fixed-group-trace2.log,fixed-group-after.json,comand-saved-after.json,
comand-geometry.json,lint.log}` and the ordinary qualified capture. Temporary
trace instrumentation is excluded from production source and the candidate.

The subsequent JS13-L88 repair restores Comand's video in two ordinary
captures. The earlier geometry repair and the timer repair are both present
in the release that clears the page at 2.09% mean error (**D10.1.7**).
[Round 208](../test/online/comparison/repairs.html#round-208).

<a id="ri-253"></a>

### RI-253 — Loading animation frames silently age held CSS effects

**Fixed, 2026-11-26.** The headless elapsed-load profile holds CSS age until
its selected post-load interval, while JavaScript and host timelines advance.
Pre-load rAF dispatch introduced by JS13-L86 called the shared scheduler's
host anchor. After its initial anchor, that function advanced current_time
without rebasing held CSS start/pause clocks. CSS effects therefore consumed
load time even though the frame preparation sampled only timeline drivers.
The no-rAF control selected the correct half-yellow one-second fade; the
same script/image load waits with rAF selected a fully finished green fade.
Both new maintained CSS-age assertions fail on the preceding release.

The existing host-anchor helper now accepts a preserve_css_time option.
Loading snapshot frames rebase CSS animation/transition start and pause clocks
as host time advances; timeline and media clocks retain their elapsed time.
Normal host and selected virtual frames keep their prior behavior. Lambda's
DOM bridge supplies the actual loading phase through its existing frame hook
(**D9.1.3v2 / D9.5.6v2 / D10.1.7**). No RuntimeState layout, authored CSS,
visibility, capture origin or timer budget changes. The CSS pixel assertion
is shared between the clock and frame fixtures rather than copied.

**Qualification:** 22 scheduler tests pass, including two new clock/paused
cases. Five AST controls, four MIR controls and two Chromium controls pass;
each loading-frame fixture now also checks CSS age and the authored pause.
The Radiant integer-cast lint passes. One accumulated JS-layout baseline is
**630/630**, with no failures, errors, skips or partials. Ordinary native
captures preserve Family's resolved hero/actions; Frame.io retains its dark,
low montage, so no further page clearance is assigned to this repair.
Report205 remains **68 high / zero critical / 999 low**.

Evidence: `temp/online-visual-round232/{maintained-css-age-before.log,
scheduler-unit.log,focused-ast-chromium.log,focused-mir.log,baseline-js.json,
lint.log,qualified-run,checkpoint.md}`. Final release SHA256:
`01392e8090f48504a81b27cc7c3ce8f89b1844c94e0da70e96994ad1c266a98c`.

<a id="ri-252"></a>

### RI-252 — Demo tile hover triggers a full catalog layout

**Fixed, 2026-11-26.** D9.1.1v4 and D9.5.6v2 place event handling in
Lambda's host adapter and native layout and painting in Radiant. On the
unnamed `lambda demo` page, the ancestor hover selectors for the Open pill
sent every tile enter and exit through full layout, even though the rules
only changed colors.

The hover adapter now uses the existing retained paint refresh for
paint-only selectors that reach their subject through child or descendant
combinators. It matches against the old and new hover paths to refresh
both tiles. Geometry-changing rules and selectors with broader effects
still use the existing restyle path. The demo's border transition now
uses a 90 ms ease-out.

Across 32 native macOS pointer moves in release builds, the median to the
first presented frame fell from 195 ms to 68 ms. The expected border
color crossed the sampled pixel threshold after 259 ms before the fix
and 68 ms after it. The headless profile showed eight hover reflows
before the fix and only the initial layout after it. Fifteen focused
headless replays passed, including scoped geometry and the extended
descendant pixel fixture. A native demo replay also verified that the
Open pills switch and clear when crossing between tiles.

<a id="ri-251"></a>

### RI-251 — Widening a window retains narrow flex and grid geometry

**Fixed, 2026-11-26.** Radiant owns layout and its property storage under
D9.1.3v2 and D5.5.1. Open `lambda demo`, narrow it below 760 CSS pixels,
then widen it. The media query stops matching, but the hero still stacks
its emblem above the title and the catalog keeps the sidebar above its
contents. A minimal page reproduced the same stale flex direction, gap
and explicit grid tracks.

Full style resolution now restores the flex and grid container inputs
from the allocation defaults before applying the current cascade. It
releases the old grid templates and keeps the last committed baselines
and used track text available until layout replaces them. The geometry
regression fails on both returns to the wide viewport before the change
and passes after it. All 151 tests in the native dynamic-layout suite pass.
The release demo restores its wide geometry after both breakpoints in a
native macOS window and headless. Eight additional headless replays and
the native archive closure pass.

<a id="ri-250"></a>

### RI-250 — Demo window resizing repeats shadow and cascade work

**Fixed, 2026-11-26.** Radiant owns native paint and layout under
D9.1.3v2 and D9.5.6v2. In the unnamed `lambda demo`, changing the
catalog width invalidated the shadow cache. Radiant blurred the whole
panel, including its offscreen extent. Each viewport change also
rematched the whole stylesheet between media-query breakpoints.

Outer shadows now convolve identical straight spans once and reconstruct
the original pixel image before applying the existing masks. The cascade
compares media conditions at the old and new viewport sizes and retains
the specified style when they agree. Layout still resolves lengths against
the new viewport. Shadow roots and container-query documents retain the
full recascade path.

Across 40 distinct native resize steps in the release build, median
resize-to-frame time was 47.7 ms before and 46.6 ms after. The 90th
percentile fell from 180.4 ms to 82.3 ms and the maximum from 225.3 ms
to 100.6 ms. Eight shadow captures at 1× and 2× matched pixel for pixel.
Ten focused replays covered the demo's responsive layout, imported and
nested media rules, observers, editors and clipping. Native Scene3D
resize and the Radiant integer-cast lint also passed.

<a id="ri-249"></a>

### RI-249 — macOS clears the visible drawable during native resize

**Fixed, 2026-11-26.** Radiant owns the native present and graphics
resources under D9.1.3v2 and D9.5.1. After RI-248 removed the interim
swap, a native probe still found black pixels in the NSGL front buffer
after both growing and shrinking the window and before reflow. The
drawable's storage is coupled to the window's size.

On macOS the existing GL presenter now draws the completed frame into a
separate IOSurface and publishes it through a non-animating Core
Animation transaction. The layer keeps the completed image at the
top left at its original pixel scale during reflow. A resized backing
buffer becomes visible after paint and GPU composition finish. The
presenter checks the native current GL context because nested Cocoa
resize callbacks can restore it without updating GLFW's bookkeeping.

The native regression verifies identical retained pixels after growth and
shrink, followed by replacement with the completed frame at the new size.
Native Markdown and GPU resize replays pass. Pixel checks across Markdown,
Scene3D and WebGL presentations report no differences beyond tolerance.
Fifteen native tests and ten focused UI tests pass.

<a id="ri-248"></a>

### RI-248 — Native resize presents a stale frame before repaint

**Fixed, 2026-11-26.** Radiant owns native presentation under D9.1.1v4 and
D9.1.3v2. Dragging a `lambda view` window edge showed the previous
viewport's frame in the new drawable. The framebuffer resize helper
explicitly presented the previous image before the window turn laid out,
painted, and presented the new viewport.

Framebuffer resize now allocates the new surface and invalidates paint. The
existing live resize turn publishes the completed frame after reflow. The
native regression checks that allocation leaves GL draw state untouched and
that the new pixels reach the front buffer after repaint at each size.
The draw-state check fails at all three sizes with the original resize code
and passes with the fix. Ten focused UI cases and the layout integer-cast
check pass.
This supersedes the early swap recorded in
[Proc Gen §2l–2m](impl/Lambda_Impl_Proc_Gen.md#2l-native-resize-presentation-2026-10-27).

<a id="ri-247"></a>

### RI-247 — Collapsed table borders spill out of scroll panes

**Fixed, 2026-11-26.** Radiant owns native paint under D9.1.1v4,
D9.1.2v2 and D9.1.3v2. Horizontal scrolling in the embedded CSV editor
painted cell borders over the file sidebar. The raster table painter
recorded resolved borders without the rectangular block clip. It now
applies that clip when a cell's border paint can cross the viewport.

The scroll replay checks both axes and the visible borders at the clip
edges. Five pixels outside the pane failed before the fix. All nine
assertions pass at 1× and 2×. The before-and-after replay in
`test_tab.csv` changes only pixels in the sidebar where borders spilled.
[`collapsed_table_overflow_clip.json`](../test/ui/collapsed_table_overflow_clip.json).

<a id="ri-246"></a>

### RI-246 — Rounded inline backgrounds spill past rectangular scroll clips

**Fixed, 2026-11-26.** Radiant owns native paint under D9.1.1v4 and D9.1.3v2.
In `lambda view AGENTS.md`, a rounded inline code background could extend
from the scrollable preview into the bottom toolbar. The rounded background's
vector fill bypassed the rectangular block clip.

The raster text painter now records the rectangular clip around a rounded fill
that can cross the clip boundary. The regression checks that the background
stays visible inside the pane and stops at its bottom edge before scrolling
and its top edge afterward. Both outside pixels failed before the fix. The
case passes at 1× and 2× display scale. A headless capture of the default
`AGENTS.md` viewer confirms that the fill stops at the toolbar boundary.
[`inline_code_overflow_clip.json`](../test/ui/inline_code_overflow_clip.json).

<a id="ri-244"></a>

### RI-244 — Textarea caret geometry ignores soft-wrapped lines

**Fixed, 2026-11-25.** Radiant painted textarea values on multiple visual
lines but used only newline characters for pointer positions, the caret and
selection geometry. The Lambda host also used newline-only navigation for
line movement. A click on the second line of a wrapped property value placed
the caret on the first. The text-control geometry now measures the painted
visual lines, and the host uses those lines for vertical and line-boundary
navigation (D9.1.1v4, D9.1.3v2). The UI value assertion also initializes a
textarea before reading its live value, so dynamically mounted default text
is checked correctly. Composition caret and underline painting use the same
visual-line geometry for provisional IME text.

The property wrap replay checks a second-line click, vertical navigation,
editing and saving. A standalone textarea replay checks selection, Unicode,
explicit newlines and a composition spanning a wrap. The native geometry
test checks caret placement, hit testing and composition rectangles. All 83
selected structured-editor replays pass. The
textarea interaction selection has 16 passing and nine state-dump failures,
and the same nine fail with the original Radiant files. No baseline was
changed.

<a id="ri-243"></a>

### RI-243 — Default focus outline misses dialogs and sits away from text fields

**Fixed, 2026-11-25.** An open dialog without `tabindex` acquired
`:focus-visible` through keyboard-mode focus and computed
`outline-style: none`. Radiant's default stylesheet restricted the outline to a tag and
attribute list. The rule now matches `:focus-visible` directly at the
normal user-agent origin (D9.1.1v4, D9.1.3v2). An author can still
replace or suppress the outline. [HTML rendering recommendation](https://html.spec.whatwg.org/multipage/rendering.html#phrasing-content-3) · [CSS cascade order](https://www.w3.org/TR/css-cascade-5/#cascade-sort)

The stylesheet also gave text fields a 2 px outline with a 2 px gap.
Chromium 154 on macOS reports a 1 px outline with zero offset and color
`#005fcc` for plain text inputs and textareas. Radiant now uses these
dimensions and color for its solid outline on form fields, with the existing
offset for checkboxes and radio controls. [Chromium stylesheet](https://github.com/chromium/chromium/blob/main/third_party/blink/renderer/core/html/resources/html.css#L1618)

The headless dialog replay fails the outline assertion on the earlier
host and passes on the rebuilt host. The field replay checks the one-pixel
width, zero offset, and author overrides.
The 148 native dynamic layout tests, seven focus and editor UI replays,
and the Radiant layout lint pass.
Evidence is under `temp/ua-focus/`.

<a id="ri-242"></a>

### RI-242 — SVG export omits metadata for graph nodes with empty labels

**Fixed, 2026-11-25.** The DOT corpus fixture `switch.gv` paints visible
unlabeled circles. The SVG walker emitted metadata only when a block
had children. The empty circles therefore lost their graph node IDs in
SVG. The SVG backend now receives an empty semantic group for a
childless graph node during content paint. Its bounds and paint still
come from the existing layout and render path (D9.1.3v2, D9.5.6v2).
The upstream comparison verifies visible node identities and the absence
of label glyphs. [Upstream graph corpus](../test/lambda/graph/upstream/README.md)

<a id="ri-240"></a>

### RI-240 — Fixed-direction text edits force direction-dependent document rebuilds

**Fixed, 2026-11-25.** Native mutation records marked every text and control
value edit direction-sensitive. If any author or UA sheet used `:dir()`, the
reconciler consequently fell back to a full-document cascade/layout even when
the edited content had fixed inherited direction. Child-list records already
checked the actual automatic-direction context. Shared DOM invalidation
follows **D9.1.3v2 / D9.5.6v2**.

Text, control values and child lists now reuse that existing context check.
Text starts from its parent; control values include the control's own `dir`.
An explicit LTR/RTL boundary prevents content changes from affecting automatic
ancestors. `dir=auto`, default BDI behavior, hidden first-strong text and the
existing conservative shadow-tree closure are preserved. The DOM direction
algorithm itself is unchanged; see [HTML's dir rules](
https://html.spec.whatwg.org/multipage/dom.html#the-dir-attribute).

Three of eight maintained fixtures fail before repair. All eight pass after
repair in AST, MIR and Chromium, including input/textarea values and automatic
direction reversals. The fixed-direction reductions remove three unnecessary
full-document direction fallbacks while keeping their measured widths. All
35 focused AST mutation/relational checks and the Radiant integer-cast lint
also pass. No source, timing, execution-budget or navigation-limit change.

Superpower's complete pre-fix logs show DOMContentLoaded exhausting the
120-second JS budget, after which settlement is skipped. The small reduction
independently establishes an unnecessary-layout cause; it does not by itself
prove that this is the only live-page cost. Ordinary page qualification is
recorded in the online comparison report. Diagnostic frames are excluded.
Evidence: `temp/online-visual-round229/` (`direction-text-before.log`,
`direction-before.log`, `direction-and-relational-after.log`,
`direction-mir.log`, `browser-fixtures.json`, `lint.log`, `qualified-run/`).

<a id="ri-239"></a>

### RI-239 — Layout redirects leak state created by the source document

**Fixed, 2026-11-25.** A page that reads a bounding rectangle and then
navigates leaves two render allocations (152 bytes) live at shutdown. The
redirect path destroyed the input DOM directly and skipped the owner-side
teardown of the view and retained display list. The layout CLI now uses its
normal document teardown for redirects and layout initialization failures
(**D5.5.1**, **D9.1.3v2**).

The debug memory tracker reports zero errors for redirects to a valid page
and to a missing page. The missing destination still reports a layout load
failure. The regression control is in
[`test_input_layout_runner.py`](../test/wpt/test_input_layout_runner.py).

<a id="ri-238"></a>

### RI-238 — Layout format detection reads the URL query and fragment

**Fixed, 2026-11-25.** A layout invocation for an HTML page with a query
ending in `.md` routed the page through the Markdown loader. Format selection
now reads the parsed pathname (**D9.1.3v2**).

The WPT runner now passes each variant as part of the document URL. Its former
assignment to the global `location` invoked the navigation setter and made
11 layout children try to load `[object Object]` (**D8.4.4**). Regression
controls cover extension-like suffixes and shell metacharacters. The
`wpt-input-events` category has 20 passes and 12 skips. The full layout
baseline has 5,630 passes, 294 partial results, 12 skips and zero failures.
Evidence is in [the layout report](../temp/wpt-input-events-fix/final-layout-report.json).

<a id="ri-237"></a>

### RI-237 — Native Canvas pixel readback is missing

**Fixed, 2026-11-25, for bounded RGBA8/sRGB readback.** Ebury's consent SDK
draws two colors then reads their actual pixels; the missing operation aborts
its banner. Root's JS13-L77 records the API failure and Chromium control.
Native already owns the bitmap, so the repair adds a synchronous copy through
the existing graphics waist (**D9.1.3v2 / D9.5.1 / D9.5.6v2**).

The copy borrows the surface inside ImageSurfaceReadScope, reverses alpha
premultiplication through the shared image helper, and pads samples outside
the bitmap with transparent black. Drawing transform, clip and global alpha
do not affect reads. JS owns the destination storage; no GC pixel pointer or
renderer pointer is retained across the boundary (**D5.5.2 / D6.3.3**).
Existing bitmap limits move to the shared canvas contract without changing
their values. No vendor or layout algorithm is modified.

Eleven native AST/MIR and eleven Chromium readback groups pass, plus fourteen
existing native Canvas transform/path groups. They exercise real painted
samples, negative/outside rectangles, alpha, state independence, copies,
resize, construction, receiver brands and coercion ownership. Integer-cast
lint and one accumulated dynamic baseline (**630/630**, zero errors/skips)
pass. Ebury's ordinary capture restores its consent panel at 1.10%
mean error /1.92% changed area against Chromium. Full image-origin taint
tracking, wide-gamut/float16 readback and other missing pixel APIs remain
unqualified. Evidence: Lambda `temp/online-visual-round214/`.

<a id="ri-236"></a>

### RI-236 — Wrapping flex children lose fitted cross sizes

**Fixed 2026-11-25.** A non-stretched row-flex child with `flex-wrap:wrap`
inside a column flex container received the parent's full cross width during
alignment. `flex_apply_intrinsic_cross_size` overwrote its hypothetical fitted
width merely because its contents could wrap. A centered 80px item in a 600px
parent consequently became 600px wide with its button at the left edge.

The wrapping-only override and its parameter are removed. The existing shared
fit-content calculation now supplies the cross width, retaining the later
min/max clamp and genuine stretch handling. This follows
[CSS Flexbox §9.4](https://www.w3.org/TR/css-flexbox-1/#algo-cross-item);
Radiant owns the layout repair under **D9.1.3v2**. No selector, site or authored
dimension receives special treatment.

Seven of fourteen focused cases fail before repair; all fourteen native cases
and fourteen Chromium controls pass afterward. Cases include wrap-reverse,
multiple items/gaps, constrained available width, min/max sizes, decorations,
start/end alignment, explicit widths, stretch, auto margins and a row-parent
control. The layout integer-cast lint passes. One ordinary Gladia capture
confirms that its action container moves to the center; its action text/filter
rendering and large Canvas background remain incorrect, so the page stays high.
No broad baseline repeats immediately after the preceding 630/630 dynamic
milestone. Evidence: `temp/online-visual-round212/{flex-wrap-before.log,
flex-wrap-after.log,browser-flex-wrap.json,int-cast-lint.log}` and
`test/online/render_flex_wrap_cross.test.cjs`.

<a id="ri-235"></a>

### RI-235 — Late positioning stretches intrinsic popovers

**Fixed, 2026-11-25.** Run `make layout test=overlay-transition-backdrop`.
The late vertical pass stretched the popover to 800px at y=0. The browser
reference measures 14px at y=393. The positioned axis classifier treated the
native `fit-content` default as automatic because that default has no
declaration and no resolved length. It now checks the intrinsic size keyword
before falling back to automatic sizing. Radiant owns layout under
**D9.1.1v4 / D9.1.3v2**. [HTML rendering rules](https://html.spec.whatwg.org/multipage/rendering.html#hidden-elements)

The original layout case matches all three reference boxes. The reduced
fixture also verifies authored `fit-content`, an authored `auto` override,
and stretching against a root whose height comes from flow. All 139 position
cases pass. The full Radiant baseline exits 0 with 8,177 passed, 298 partial
and 20 skipped under native macOS graphics access. Integer layout lint
passes. Evidence is in `temp/radiant-wpt-css-position/`.

<a id="ri-234"></a>

### RI-234 — Native edge scrolling retains a retired text row

**Fixed 2026-11-24.** Dragging past the source viewport could start the native
scroll timer as the editor was also recycling its rows. The next native tick
or input event then dereferenced the retired anchor view. Under D10.2.1 the
model surface claims its own scrolling. The native state store clears timers
when their targets retire or leave the view tree under D5.5.1.

<a id="ri-233"></a>

### RI-233 — Retained paint reuses an old selection highlight

**Fixed 2026-11-24.** Dragging over a preformatted editable line changed the
DOM range while the retained display list still contained the previous
highlight. The document recording key now includes the painted selection
revision under D10.2.1. `test_editing_retained_range.json` warms the recording
and checks highlight pixels while the drag grows and shrinks.

<a id="ri-232"></a>

### RI-232 — Pointer selection collapses on indented source lines

**Fixed 2026-11-24.** Clicking a heavily indented preformatted line can
place the caret at the line end. Dragging between two positions on that line
can resolve both endpoints to the same offset. The glyph walks in
`lambda/module/radiant/event.cpp` now honor preserved spaces for pointer and
caret geometry under D10.2.1. `edit_src_indented_pointer.json` verifies
the caret column, the selected span and the saved bytes at 2× device scale.

<a id="ri-231"></a>

### RI-231 — Source editor loses bytes after NUL on paste and drag

**Fixed 2026-11-24.** Copy and paste of `A`, U+0000, and `B` inserted just
`A`. The clipboard store and package event adapter now retain the byte length.
A mouse drag also truncated the selection because the range extractor took a
C-string copy. The extractor now returns the full span with its length, and
the source surface applies the drop to the target range with one undo step.
`edit_src_nul.json` and `edit_src_nul_drag.json` verify the source behavior under
D10.2.6v2, D10.2.1 and S2.5.9v2.

<a id="ri-230"></a>

### RI-230 — Form control labels truncate at embedded NUL

**Fixed 2026-11-24.** A button value, an input or textarea placeholder,
or a select option label with U+0000 rendered identically to its prefix.
Radiant now retains the attribute length through measurement and paint
under S2.5.9v2 and D9.5.6v2. `test/test_nul_text.py` checks each control
independently in the raster renderer.

<a id="ri-229"></a>

### RI-229 — Generated content from attribute values truncates at embedded NUL

**Fixed 2026-11-24.** Layout and marker paint previously took C-string
lengths for CSS `content: attr(...)`. A generated label containing
`FIRST`, U+0000, and `PSEUDOEND` drew only `FIRST`. Pseudo-element layout,
intrinsic measurement, and marker paint now use the stored byte span
under S2.5.9v2 and D9.5.6v2. The SVG regression in `test/test_nul_text.py`
checks that `::before` and `::marker` render the square and suffix.

<a id="ri-228"></a>

### RI-228 — Plain-text viewing and layout truncate at embedded NUL

**Fixed 2026-11-24.** On control checkout `0a8291edba`, `lambda view`
shows only `BEFORE` for a plain-text file containing `BEFORE`, U+0000,
and `AFTER`. A Lambda template with the same value also produces a
six-byte layout run. The viewer now attaches the file span as a text
node. Layout, whitespace checks, intrinsic measurement, and form text
painting use explicit lengths.

The paint path uses the font's U+0000 glyph when present and falls back
to U+25A0. SVG export uses U+25A0 to keep the XML parseable. S2.5.9v2
defines the source code point. D10.2.6v2 governs text admission in the
source editor. HTML source continues through the HTML tokenizer.

`test/test_nul_text.py` exercises the headless viewer, text runs at
three NUL positions, and SVG export. Control evidence is in the Lambda
checkout under `temp/radiant-nul-1n3w59gr/`.

<a id="ri-227"></a>

### RI-227 — Shadow fragments truncate positioned containing-block and paint walks

**Fixed, 2026-11-24.** Teachable's consent SDK creates its notice inside a
shadow root. Its authored fixed-position rule matches, but the initial
containing-block fallback walks raw DOM parents and stops at the fragment.
The notice consequently sizes against a zero-area root: its outer box is
48px wide at y=-48 rather than the browser's 960px bottom-aligned box.
The inner panel has zero content width, producing a long vertical text strip.

Initial-block discovery now uses the existing shadow-including parent helper.
It reaches the document root without treating top-layer paint boundaries as
new initial containing blocks. Paint/hit-test offset conversion uses the
existing formatting-parent helper across shadow roots and slot projection.
The latter repair is required separately: correcting sizing alone passes
geometry assertions but still paints the notice at its host's offset.
No selector, site condition, forced style or capture-clock change is added.
Native ownership follows **D9.1.1v4 / D9.1.3v2**; exports retain **D10.1.7**.

Seven focused cases cover fixed and absolute fallback, open/closed/nested
roots, transformed containing blocks, light DOM and top-layer dialogs.
Four fail before repair. All seven pass geometry and PNG checks afterward,
as do ten root-reflow and sixteen stacking/overflow controls (**33 total**).
All seven Chromium geometry controls and layout integer lint pass. The
unchanged authored-cookie replay expands the notice to 960px and its inner
panel to 540px. Unsupported `:host` variable styling remains separately open
under JS13-L2. This does not qualify full Shadow DOM support. Task blocker: no.

The subsequent accumulated milestone passes 630/630 dynamic cases and
3851/3852 static cases, with six reported static skips and zero errors.
The sole static failure exactly matches the preceding `dom_view_role_nested`
record. The live notice now has its correct width and bottom placement, but
its remaining transparent styling keeps Teachable high. Report 179 clears
pnpm and Podia through separate preceding-release rechecks; inventory is
88 high, zero critical, 979 low. No complete online sweep is repeated.
[Comparison evidence](../test/online/comparison/repairs.html#round-179).

Evidence: `temp/online-visual-round203/`,
`test/online/{shadow_positioned_fixtures.cjs,render_shadow_positioned.test.cjs}`;
[CSS Position 3 containing blocks](https://drafts.csswg.org/css-position-3/#def-cb).

<a id="ri-226"></a>

### RI-226 — SVG transition snapshots recompute context for resolved absolute lengths

**Fixed, 2026-11-24.** JS13-L68 restores Metronome's XHR-loaded Lottie animation,
exposing repeated native SVG transition snapshots. Two candidate captures
exceed the unchanged 150-second limit. A release-process sample places the
main thread in SVG descendant layout, transition baseline capture and SVG
font/viewport ancestry resolution. The shared property parser had already
converted absolute lengths and unitless numbers to their final scalar, but
baseline capture discarded that result and walked the SVG context again.

Baseline capture now retains the parser's resolved scalar. Percentages,
font-relative lengths and unresolved math still use the existing SVG context
resolver. No SVG node, transition snapshot or animation frame is skipped.
The final full-page capture completes in **77.9 seconds** with the product
animation visible; a separate blurred overlay still keeps the page high
severity. This is a native repair under **D9.1.1v4 / D9.1.3v2**, with unchanged
capture timing under **D10.1.7**.

All twelve focused SVG controls pass: six mutation/selector/transition cases
and six baselines covering unitless, px, pt, em, percentage and math values.
Layout integer lint passes. The preceding milestone already ran both
baselines; this isolated optimization uses focused checks. Evidence:
`temp/online-visual-round200/{sampled,optimized,svg-length-focused.log}`;
`test/online/render_svg_dash_mutation.test.cjs`.

<a id="ri-225"></a>

### RI-225 — A stacking owner's overflow scope clips escaping positioned layers

**Fixed, 2026-11-24.** After RI-224 repairs empty-context culling, a fixed
descendant still disappears when its stacking owner has `overflow:hidden`.
Both raster and shared SVG/PDF walkers opened the owner's content clip before
all signed and zero-level layers. Their existing containing-block-aware
overflow check handled intervening ancestors but could not undo the owner's
already active clip and scroll offset. This violates
[CSS 2.2 §11.1.1](https://www.w3.org/TR/CSS22/visufx.html#overflow-clipping)
when the positioned child's containing block lies outside that owner.

Normal children now paint within the owner's content scope; positioned layers
open that scope only when the shared `radiant_stack_overflow_applies` resolver
requires it. Raster traversal preserves separate owner/content coordinates.
Each backend reuses shared content-scope helpers, including the intervening
ancestry path. Positive and negative layers, scrolling, real containment and
clip paths retain their semantics. No clipping assertion is weakened.
Ownership follows **D9.1.1v4 / D9.1.3v2**; capture timing follows **D10.1.7**.

The combined RI-224/225 matrix has sixteen controls; ten fail on the preceding
release and all pass after both repairs, in Chromium and through SVG export
rasterized independently with librsvg. All **68 focused checks** pass: fifty-two
PNG checks and sixteen SVG checks. Layout integer lint passes. At the combined
milestone, dynamic baseline **630/630** and static **3851/3852** retain the exact
previous `dom_view_role_nested` failure; no reference changes. Evidence:
`temp/online-visual-round199/`, `test/online/render_stacking_overflow.test.cjs`.

<a id="ri-224"></a>

### RI-224 — Empty stacking contexts cull visible positioned descendants

**Fixed, 2026-11-24.** Maven Central's Osano panel initialized successfully.
An isolated unchanged SDK had the same visible style, opacity and 400×461
geometry in Native and Chromium, yet Native painted nothing. Its outer
stacking context has zero height and owns fixed descendants whose containing
block is the viewport. Paint culling checked the owner's border and in-flow
overflow bounds, missing that positioned paint. A non-stacking owner or a
one-pixel height hid the defect; neither is a valid application workaround.

The shared overflow predicate now accounts for positioned descendants owned
by a context, using the existing tree query. Viewport culling, dirty culling
and retained-fragment admission consequently stay conservative when the
owner's border cannot bound its paint. Real paint containment and subsequent
clip scopes remain effective. Changes stay native under
**D9.1.1v4 / D9.1.3v2**; no script or capture-clock override (**D10.1.7**).

The isolated panel and the live Maven consent/preferences panel now render.
Maven's manual major omission clears; small card spacing and input alignment
remain. Report175 has **92 high, zero critical, 975 low**. RI-225 addresses the
additional overflow-owner control found during qualification; all sixteen
controls and the combined verification described above pass. Native geometry
and browser references are unchanged. Evidence: `temp/online-visual-round199/`;
[comparison](../test/online/comparison/repairs.html#round-175).

<a id="ri-223"></a>

### RI-223 — Local inline-style writes retain unrelated relational dependencies

**Fixed, 2026-11-24.** An unrelated CSS `attr()` declaration could promote
an inline-style write into the generic attribute path. Unlike data/ARIA
writes, its journal entry was not marked local when no `:has()` selector
referenced the style attribute. Swag's carousel therefore recascaded the
whole document for transition, width and transform writes.

The existing attribute dependency query now classifies these writes as local,
while documents with shadow roots keep the broader path. Style-attribute
witness selectors, scope/column guards and container-query settling remain
active. This reuses the existing mutation protocol under **D9.1.1v4 /
D9.1.3v2 / D9.5.6v2**. A new failing control with an unrelated `attr()` rule
now passes; existing positive style-witness and size-query controls pass.
Together with [JS13-L65](jube/JS_Issue_Ledger.md#js13-l65), this removes the
observed unrelated recascades without suppressing mutations or timers.
The combined qualification is 65 focused passes and the unchanged milestone
baseline results. Evidence: `temp/online-visual-round197/` and
[comparison round 173](../test/online/comparison/repairs.html#round-173).

<a id="ri-222"></a>

### RI-222 — Auto-width SVGs ignore a definite height in flow and flex sizing

**Fixed, 2026-11-24.** A viewBox-only SVG with `height:1em; width:auto` in
a flex row took the whole available width. Maven Central's check icons
therefore squeezed card text into a narrow column. The automatic-axis helper
omitted the natural SVG ratio, and flex intrinsic sizing honored declared
axes only for bitmap images before applying its available-space fallback.

SVG flow uses the existing opposite-axis ratio transfer before the fallback;
SVG flex contributions use the existing content-box constraint solver with
the resolved axes. Zero CSS sizes remain valid. Explicit ratios, width limits,
border-box edges, column stretching and cyclic percentage viewport cases are
preserved. The general intrinsic ratio policy is unchanged. Ownership remains
**D9.1.1v4 / D9.1.3v2**.

Five of nine new rendered cases fail on the prior release. All nine pass in
Chromium and after repair, as does the new native flow/flex unit test. The
existing percentage-SVG regression caught an overly broad intermediate
attempt; that change was removed before qualification. All 65 combined
focused checks pass. Baselines run once after the fixes: 630/630 dynamic and
3851/3852 static, retaining the exact earlier `dom_view_role_nested` failure.
Maven's card text becomes readable; its missing consent panel keeps the page
high severity. Evidence: `temp/online-visual-round197/` and
[comparison round 173](../test/online/comparison/repairs.html#round-173).

<a id="ri-221"></a>

### RI-221 — Root-positioned children retain offsets from an earlier layout height

**Fixed, 2026-11-24.** After a geometry read laid out an initially short
document, script-driven content growth left bottom-positioned children of a
positioned root at the old viewport-height offset. NVD's absolute footer
covered the primary content. A script-free replay with the actual stylesheet
embedded placed it correctly; retained dynamic reflow reproduced the fault.

The root's separate layout path omitted the existing post-flow vertical
absolute-child resolution used by ordinary blocks. It now calls that helper
after the root's extent settles and before viewport scrolling clips its box.
The helper uses the shared absolute-containing-block resolver so fixed
children and unpositioned roots retain their viewport basis. No page-specific
branch, layout suppression or timing change is introduced. Native ownership
remains **D9.1.1v4 / D9.1.3v2**; capture timing remains **D10.1.7**. The behavior
follows [CSS 2.2 §10.1](https://www.w3.org/TR/CSS22/visudet.html#containing-block-details)
and §10.6.4.

Five of ten new rendered controls fail on the prior release; all ten pass
in Chromium and after repair. All **59 focused checks pass**: 45 rendered
positioning controls and 14 native positioning/containing-block tests. Layout
integer lint passes. Milestone baselines run once: **630/630 dynamic** and
**3851/3852 static**, with six static skips. The sole static failure,
`dom_view_role_nested` at 44.444% element match, exactly equals the previously
recorded result; it remains visible. NVD's mean image error falls from 38.50%
to 0.84%, and visual review confirms its content is no longer obscured.

Evidence: `temp/online-visual-round196/{root-positioned-before.log,
root-positioned-browser.log,positioned-focused.log,native-positioned-focused.log,
milestone-summary.json,selected/pairs.png}` and
[comparison round 172](../test/online/comparison/repairs.html#round-172).

<a id="ri-220"></a>

### RI-220 — GLB viewer crashes when a select popup extends below the window

**Fixed, 2026-11-23.** Open the Animation menu on the Homer bull GLB in a
native window with a 1200 × 717 CSS-pixel viewport on macOS. The popup grew
downward past the viewport, and the composited upper segment replayed glyph
rows beyond its destination buffer. The crash report and a native replay
both reached glyph coverage blending with an out-of-bounds destination.

Popup geometry now chooses the side with enough room and scrolls options
within the available height. The painter intersects option clips with the
viewport. Radiant clips grayscale and color glyph replay to the actual
surface, including strip offsets (D9.1.2v2, D9.5.6v2).

The guarded display-list test detected out-of-bounds writes before the fix.
It passes for both glyph types and both surface origins. The select replay
covers upward placement, constrained height and selection of the last row.
Three native runs of the original menu sequence closed normally. A fourth
run selected and played the bull's Walk animation with GPU compositing.
<a id="ri-219"></a>

### RI-219 — Transparent video frames blend over uninitialized decoder storage

**Fixed 2026-11-22.** Going's decoded hero played, but transparent regions
outside the capsule contained random pixels. The macOS backend drew each
CGImage with Core Graphics' default source-over blending into an allocated
or reused frame buffer. Transparent source pixels retained the old destination
bytes. The draw now uses copy blending so the complete decoded frame,
including transparency, replaces that storage. Buffer ownership and the
immutable paint snapshot remain unchanged
(**D5.5.1 / D9.1.1v4 / D9.1.3v2**).

The new native regression uses a generated 64x48px ProRes 4444 movie. It
poisons the returned frame buffer with opaque white, waits for a subsequent
decoded frame, then checks transparent RGBA corners and an opaque red center.
Before the repair all four transparent channels incorrectly remain 255;
afterward they are zero. The generator and 12.8KB fixture are native-owned;
the test downloads no media. All four focused video tests pass, including
the existing dimension, poster and following-foreground paint controls.
The checks require the macOS decoder service. The fresh shared-1000ms Going
pair restores its hero and clean transparency at 1.44% mean error, clearing
the major difference. The report retains 106 high-severity pages. Evidence is
under `temp/online-visual-round179/` and
[report160](../test/online/comparison/repairs.html#round-160).

A follow-up `make test-radiant-baseline` exits zero with 8,175 passed,
298 partial and 20 skipped of 8,493 cases. This milestone covers the paired
RI-218/RI-219 source state; the previously qualified release executables
were restored afterward. Existing expected results were retained. Evidence:
`temp/online-visual-round181/radiant-baseline.log`.

<a id="ri-218"></a>

### RI-218 — Definitely sized grid images have no paint surface

**Fixed 2026-11-22.** Getsafe's hero image had a valid 560x560px grid box
but no pixels. Both original and responsive AVIF files decoded and painted
as ordinary images; substituting local bytes in the full document retained
the failure. Grid finalization returned immediately for replaced content.
Definite sizing could skip intrinsic queries, leaving no EmbedStyle or image
surface for painting.

The final replaced-item branch now calls the existing image admission helper
before returning. Grid retains its resolved dimensions; the shared loader
retains resource ownership and revalidates source changes
(**D5.5.1 / D9.1.1v4 / D9.1.3v2**). No decoder, vendor, timing or site-specific
change is involved.

The new native test fails before the fix because the image has no embed
storage, then passes including a source mutation. All 17 focused native grid
tests pass. Four generic PNG/SVG rendering cases fail before and pass after;
nested-block and flex controls pass on both sides. Chromium agrees with all
six fixtures, and layout integer lint passes. Evidence is under
`temp/online-visual-round179/`. These focused checks establish the native
defect and repair; online-page clearance requires its paired full-page review.

<a id="ri-217"></a>

### RI-217 — Grid spans across fractional tracks lose a definite size contribution

**Fixed 2026-11-22.** The track-size input used the item's automatic
minimum of zero even when a definite preferred size supplied its minimum
contribution. In the 60px-wide item controls, two `0fr` tracks collapsed
to 0px and tracks sized `0.2fr 0.3fr` shrank to 6px and 9px.
The minimum contribution now uses the constrained min-content size for
an item with a definite, independent preferred size across flexible
tracks. The same preferred-size check serves intrinsic measurement.
See [CSS Grid §12.5](https://www.w3.org/TR/css-grid-2/#algo-content).
Native ownership **D9.1.1v4 / D9.1.3v2**.

All six stored fractional-span fixtures match fresh Chromium 148 grid and
child widths and positions exactly. Four previously failed, and both
non-regression controls still pass. The broad grid pattern passes 401 of
403, with the existing text and number input differences. The static and
script layout suites record 4,481 passes, six skips, and the same
`dom_view_role_nested` failure as the control.
Evidence: `temp/radiant-ledger-continuation/{grid-browser.log,grid-verification.txt,layout-baseline-final.log}`.

<a id="ri-216"></a>

### RI-216 — Nested flex measurement inflates percentage-sized SVGs

**Fixed 2026-11-22.** The intrinsic-height fallback for a definite-width row
flex item with a preferred aspect ratio measured its SVG child using an
outer ancestor's inline size. A 32px-wide item with `aspect-ratio:2` and a
100%-sized SVG with a square viewBox grew to 1200px tall. The parent item's
transferred height gives the SVG a 16px percentage basis, as Chromium 148
confirms. Fixed-height 40px children still contribute their natural extent.

The flex intrinsic height query now measures the row at its resolved
content width through the shared sizing entry point. That entry point
retains the transferred percentage basis while measuring natural height.
`NestedFlexRatioHeightUsesThePreferredWidth` and
`RatioFlexHeightSeparatesPercentageBasisFromNaturalExtent` both fail
before and pass after. Native ownership **D9.1.1v4 / D9.1.3v2**.
Evidence: `temp/radiant-ledger-continuation/{svg-query.log,final-dynamic.log,browser-probes.log}`.

<a id="ri-214"></a>

### RI-214 — Replays repeat ownership walks after a DOM retirement

**Fixed 2026-11-22.** The RI-213 build repair removed two DOM validation
optimizations. The Input revision in the current checkout provides both APIs
again. Radiant had continued to use expired proofs for surviving ViewState
owners after an unrelated DOM retirement. Optimized samples traced repeated
ownership walks to this path in the PDF, Slides, and Hamster replays.

StateStore now renews each surviving owner's proof. Pruning, graph validation
and Lambda custom-element traversal reuse the Input validation scope for
unproven addresses. The existing form-control lifetime test retires a
sibling and checks generation renewal and detached-control behavior
(**D5.5.1 / D9.1.3v2**).

The document editor also gives its file-tree scrollport size containment.
The containing panel determines the scrollport size, and expanded rows
determine its scroll range (**D5.5.3 / S12.1.3v6**).

The 39 state-store tests, three custom-element goldens, and all ten
requested replays passed in the debug build. Two isolated passes took
1.703–3.915 s per command. The full stock debug baseline had 8,175 passes,
298 partials, 20 skips, and zero failures. In that run text-to-PDF fell
from 37.519 s to 4.973 s. The release-profile build also passed the ten
replays. Evidence is under `temp/radiant-ten-under-five/`. RI-196 and
RI-198 track the remaining parallel timing gaps.

<a id="ri-213"></a>

### RI-213 — Release build used a removed DOM validation cache API

**Fixed 2026-11-22.** Radiant's state validation added five
`DomNodeValidationScope` guards and called `dom_node_ref_refresh`, but
lambda-input's `2178858` removed those APIs. The release build failed to
compile `state_machine.cpp` with an unknown type. Under **D5.5.1 / D9.1.3v2**,
the Radiant adapter now uses `dom_node_ref_validate` directly. It still checks
the document's current ownership and node identity when the cached lifetime
stamp is stale; the removed scope and stamp refresh only optimized repeated
checks. The split host also referenced a pending-load query absent from the
Radiant API, so it uses the existing resource-manager and document-pending-work
checks (**D10.1.7**) for the same completion condition.

Reproduction: `make release` on macOS. Verification: `make release` succeeds;
the Lambda runtime, adapters, Radiant archive and final executable all build.

The current Input revision provides the faster validation APIs. RI-214
records their restoration and verification in the paired checkout.

<a id="ri-212"></a>

### RI-212 — Unsubstituted CSS environment values poison layout coordinates

**Fixed 2026-11-22.** Shared computed-value substitution and scratch ownership
follow **D5.5.1 / D9.1.1v4 / D9.1.3v2**. Patreon's server-rendered primary
navigation had NaN positions and dimensions: its negative margins used
calc() over custom properties containing max(env(safe-area-inset-left), …).
The shared resolver traversed env() arguments but never substituted the
environment value, leaving math unresolved and poisoning the descendant boxes.

The existing bounded substitution path now handles env() alongside var(),
including preserved tokens, inherited custom properties, nested math, lazy
fallbacks, empty fallbacks and unknown names. The four safe-area insets resolve
to zero for Radiant's rectangular viewports, as required by
[CSS Environment Variables §2.1](https://www.w3.org/TR/css-env-1/#safe-area-insets).
Names retain case and decoded CSS escapes. Unknown environment values without
a fallback invalidate the computed declaration through the existing path.
No coordinate clamp, site rule or capture timing adjustment was added.

All nine new rendering cases fail before the repair and pass afterward;
Chromium 148 agrees on the same fixtures. All ten shared substitution unit
tests pass, including nine environment-name/fallback samples in the new test.
Integer lint passes. The final static layout milestone retains the exact
3852-case inventory and all previous outcomes: 3847 passes, five existing
failures, six reported skips and zero errors. Final dynamic layout also retains
all 630 passes and the previous inventory/outcomes. Evidence:
`temp/online-visual-round176/environment-{before,after}.log`,
`environment-browser/results.json`, `rdt-environment-tests.log`,
`baseline-static-comparison.json`, and
[comparison round157](../test/online/comparison/repairs.html#round-157).

Patreon's fresh 1000ms post-load pair restores the primary navigation and
video hero, leaving small attribution/typography differences. It clears the
major category (mean error 6.46%, changed area 13.09%). This is cumulative
page-level evidence for RI-210/RI-211/RI-212; the controlled video witness
isolates RI-210. The queue becomes 107 high / zero critical / 960 low.

<a id="ri-211"></a>

### RI-211 — Media commands before layout report an unavailable backend

**Fixed 2026-11-22.** Native document ownership follows **D5.5.1 /
D9.1.1v4 / D9.1.3v2**; the shared DOM host boundary follows **D9.5.6v2**.
A muted video calling play() from window.load rejected with NotSupportedError,
"Media backend is unavailable", although the same file decoded through the
declarative HTML path. radiant_media_prepare accessed doc->state before the
first layout created its StateStore and consequently returned no media state.

Preparation now uses the existing idempotent state_store_create before looking
up the media owner. Ownership and teardown stay with the document. Existing
audible-start checks under **D11.3.1** remain in their original path; no request
is authorized by this initialization guard. The scripted-play fixture fails
after RI-210 alone and passes after this correction.

All six shared video controls agree with Chromium 148, and all 54 related
resource/percentage checks pass. The dynamic layout milestone passes 630/630
with identical inventory and outcomes to round175. Integer lint passes.
The final three-fix milestone also retains all static baseline outcomes
(3847 passes, five existing failures). Evidence:
`temp/online-visual-round176/video-scripted-diagnostic.log`,
`video-first-frame-after-state.log`, `video-browser/results.json`,
`media-controls.log`, `baseline-js.json` and `milestone-summary.json`.

<a id="ri-210"></a>

### RI-210 — Opening playback retains a poster despite an available video frame

**Fixed 2026-11-22.** Native rendering and media ownership follow
**D5.5.1 / D9.1.1v4 / D9.1.3v2**, with capture timing under **D10.1.7**.
RI-134's first-frame path could decode a complete video while its asynchronous
player opened, but the poster selector admitted frames only in Playing/Paused.
Thus an accepted autoplay request with a poster never reached the available
frame path. The same clip without a poster rendered correctly.

The selector now also consults the existing MediaViewState.native_play_requested
flag while opening. A poster is replaced only after the existing frame getter
actually supplies pixels; idle, failed and unplayed media retain their poster.
No backend API, public data layout, transfer limit or clock changes.

The new autoplay/poster control fails before repair with zero landscape pixels
and passes afterward. Six shared controls cover paused/autoplay clips with and
without posters, scripted playback and an unavailable source. The original
landscape/poster checks are retained in a shared fixture module. Chromium
qualifies all six. RI-211 repairs the separately exposed early-play rejection.

Patreon's unchanged 12.5 MB clip and original poster provide a controlled
witness: with identical local video bytes, the earlier renderer paints the red
couch poster and the repaired renderer paints the podcast video. Compared with
Chromium playing the same bytes, mean image error drops from 15.22% to 2.82%
and changed area from 46.33% to 2.57%; the isolated clip leaves the major category.
This verifies source/frame selection without establishing exact video-time
agreement. The isolated remote source retained its poster at the existing
window; a later full-page capture displays the video. Its earlier full-page
attempt timed out at 150 seconds, and changed live resources prevent attributing
that timing difference to this fix. RI-212 repairs the separately observed
missing primary navigation and records the cumulative full-page clearance.
Convai's hero remains missing in its fresh capture. Evidence:
`temp/online-visual-round176/`:
`video-poster-before.log`, `video-first-frame-after.log`,
`video-first-frame-after-state.log`, `patreon-local-before/native.png`,
`patreon-local-after/native.png` and the corresponding source/result files.

<a id="ri-209"></a>

### RI-209 — Window load precedes required image/background completion

**Fixed 2026-11-22.** Native request ownership and the script-host boundary
follow **D5.5.1 / D9.1.1v4 / D9.1.3v2 / D9.5.6v2**; capture frames follow
**D10.1.7**. The loader waited for every modeled request for at most250ms,
then dispatched window.load while eager images and selected CSS backgrounds
were still pending. A loopback server holds each image until the load beacon
or a650ms fallback: frozen174 sends the beacon first, while Chromium148 waits
for eager images/backgrounds and excludes lazy/preload requests. The eager
image distinction also follows the [HTML image loading algorithm](https://html.spec.whatwg.org/multipage/images.html#updating-the-image-data).

The native boundary now checks current admitted eager image sources, including
detached Image requests, selected background/background-image URLs and actual
stylesheet links. It reuses existing CSS value/base-URL resolution and cascade
comparison. Hidden/contents backgrounds, unused before/after content and
overridden URLs do not acquire blockers; preloads, lazy images and post-load
fetches retain their separate completion paths. Image completion polling and
element events run before the boundary. Input's related polling starvation
repair is LI-102. The wait uses the existing navigation budget and observes
its elapsed deadline; transfer budgets, admission limits, snapshot resource
grace and the shared1000ms post-load frame are unchanged. The explicit
diagnostic load-timeout override remains available.

All17 new native cases pass with17 Chromium controls; frozen174 passes seven
and fails ten. All41 existing discovery and nine late-completion cases pass.
The dynamic layout milestone passes630/630 with identical inventory/outcomes
to174. Static layout geometry is unchanged; its preceding baseline was not
repeated. Complete font/media/pseudo-element load conformance is not established.

No whole-page clearance is claimed. Current WebKit article photos also start
after load in Chromium; Setpoint retains an authored loader overlay while its
239880-byte module is skipped by existing admission. Neither is forced visible.
Evidence: `temp/online-visual-round175/load-browser*/results.json`,
`load-boundary-frozen-http.log`, `load-boundary-frozen-extra.log`,
`resource-focused-final.log`, `baseline-js.json` and `milestone-summary.json`.

<a id="ri-208"></a>

### RI-208 — Nested column flex text can undersize a ratio-dependent cross minimum

**Fixed 2026-11-22.** An anonymous flex text item contributed its unwrapped
line height to the inner column's main sizing. The parent row therefore
accepted the 20px ratio-derived height even when the text wrapped into three
lines. When the column has a known inline width, its item collection now uses
the shared anonymous text formatter to measure wrapping before main sizing.
Trial text rectangles are restored by the formatter (**D5.5.1 / D9.1.3v2 /
D9.5.6v2**).

The reduced case moves from 40×20px to 40×60px, matching Chromium 148.
`DynamicLayoutTest.RowFlexKeepsTheWrappedHeightOfANestedRatioColumn` also
checks the single-line row cross-size contract from RI-206. All 55 selected
WPT flex aspect-ratio comparisons pass on both hosts. The original frozen173
observation is in `temp/online-visual-round173/`. New evidence is in
`temp/radiant-ordinary-fixes/`.

<a id="ri-207"></a>

### RI-207 — Column flex aspect-ratio items undersize their automatic cross axis

**Fixed for qualified horizontal-writing column contributions, 2026-11-22.**
Fast intrinsic text paths retain the single-line 10px height but omit the
fit-content automatic cross width needed by the preferred aspect ratio.
Both the flex base and automatic minimum therefore miss the 20px or 50px
ratio contribution. The two original column fixtures produce 20x10px instead
of 40x20px; a small explicit flex basis also bypasses the correct minimum.
This follows [Flexbox 9.2](https://www.w3.org/TR/css-flexbox-1/#algo-main-item).
Ownership follows **D5.5.1 / D9.1.1v4 / D9.1.3v2 / D9.5.6v2**.

A shared native helper reuses existing fit-content, min/max and box-aware ratio
transfer routines. Intrinsic storage publishes width inputs first, preventing
the shared width query from recursively re-entering the current measurement,
then retains the ratio floor in the column height contribution. Fixed content
keeps its larger contribution; preferred cross sizes and replaced/orthogonal
items keep their existing paths. No second size estimator or per-page branch
is introduced. The public header adds a function, with no data-layout change.

All 40 PNG cases pass with Chromium qualification. Frozen173 passes 33 and
fails seven new column cases. Controls include grow/shrink, explicit bases
and sizes, zero minima, stretch, wrap, scrolling and fixed/tall content. The
first permanent fixture draft accidentally inherited flex-shrink:0 from its
builder; the corrected fixtures retain the isolated source's flex-shrink:1.
That draft is diagnostic evidence only. All 22 existing aspect-ratio flex
fixtures now pass against unchanged references, including the two remaining
column cases. Layout integer lint passes.

At the milestone, the same 3852-case inventory improves from 3844 to 3847
passes. Only the three RI-206/RI-207 aspect cases change; all five remaining
failures preserve their prior outcomes. Six skips and zero errors remain.
The separate dynamic-layout inventory passes 630/630 with exact case agreement.
This does not establish any new high-severity online-page clearance. All
1067 canonical pairs and their records stay intact at 108 high. **D10.1.7**
timing, script/resource limits and vendor code are unchanged. The separate
nested-column observation remains RI-208. Evidence:
`temp/online-visual-round174/ratio-before.log`, `ratio-after.log`,
`ratio-browser-qualified.log`, `aspect-controls.log`, `milestone-summary.json`,
`baseline-layout.json` and `baseline-js.json`.

<a id="ri-206"></a>

### RI-206 — Single-line flex ratio minima use wrapped text height

**Fixed for qualified physical-height contributions, 2026-11-22.** The
aspect-ratio automatic minimum asks for a min-content cross contribution.
The shared helper instead measures height again at the flexed width. A
single-line row flex item with breakable text and a 40px maximum width gets
a 60px wrapped-height minimum, overriding its correct 20px ratio height.
The existing intrinsic path preserves the single-line contribution required
by [Flexbox 9.9.2](https://www.w3.org/TR/css-flexbox-1/#intrinsic-cross-sizes).
The ratio minimum now selects that path for a non-wrapping row flex container;
ordinary block and wrapping containers keep width-dependent measurement.
Existing direction/wrap helpers and intrinsic storage are reused. Ownership
follows **D5.5.1 / D9.1.1v4 / D9.1.3v2 / D9.5.6v2**.

All 23 focused PNG cases pass and have Chromium qualification. Frozen172
passes 20 and fails the three direct/nested single-line text cases. Controls
preserve block/wrapping minima, explicit and zero minima, scrollable overflow,
and a fixed child's actual 100px contribution. An initial reduction incorrectly
expected a fixed-child minimum to be capped by the other axis; Chromium
rejected that expectation, and no production version of that hypothesis was
built. Only the qualified single-line contribution repair is installed.

All 22 existing aspect-ratio flex fixtures are compared with frozen172. One
old row failure becomes a pass, the 19 prior passing cases remain passing,
and two original column failures remain unchanged. Fresh Chromium checks all
eight maximum-size fixtures against their original inputs and references.
Integer lint passes. The 630-case dynamic layout milestone passes with exact
inventory agreement on frozen172 before this increment; it is not repeated
after this focused change. No full 3852-case repeat, whole-page clearance or
exact-detail match is claimed. Existing column mismatches remain RI-207.
**D10.1.7** timing, limits and canonical page pairs are unchanged. Evidence:
`temp/online-visual-round173/ratio-before-qualified.log`, `ratio-after.log`,
`ratio-browser-qualified.log`, `frozen-aspect-controls.json`,
`repaired-aspect-controls.json`, `aspect-browser.json` and `baseline-js.json`.

<a id="ri-205"></a>

### RI-205 — Release captures accept rejected HTTP document bodies

**Fixed for HTTP response classification, 2026-11-22.** The Library of
Congress recheck receives a 403 challenge but reports both ok and loadOk as
true. Input correctly returns the response status and origin body. Radiant's
document-response marker uses log_info, which lib/log.h removes under NDEBUG;
the capture harness sees load completion without the rejection. Native
ownership and the host seam follow **D9.1.1v4 / D9.1.3v2 / D9.5.6v2**;
capture qualification follows **D10.1.7**.

The marker now survives release compilation. Loader configuration identifies
an embedded document before its DOM embedding edge exists, and derives that
state from the existing edge for inherited navigation. Child HTTP errors use
an embedded-response marker, so an optional rejected iframe cannot reject its
successful parent capture. Origin response bodies still render as diagnostic
PNGs. No site-specific error matching or blanket subresource rejection is
added. Input and runtime-state layouts are unchanged.

Frozen171 fails direct and redirected main-document 403 classification; its
original optional image/iframe control passes. The repair passes all 14
capture-harness checks. Three final native controls also pass after requiring
the optional iframe's actual rejected-response marker, ruling out an unloaded
child as the explanation for parent success. Layout integer lint passes.
The native host's header consumers rebuild; node-core dependency files do not
include the changed loader-config header. Full baselines are not repeated.

No challenge image replaces a canonical success, no page is cleared, and the
report remains at 108 high. FigJam and Frame still skip eight and ten large
startup tasks respectively, and NVD retains its recorded main-module barrier
(JS13-L1). These observations do not qualify admitting the full applications.
Script limits, capture windows and vendor code are unchanged. Evidence:
`temp/online-visual-round172/http-before.log`, `http-after.log`, `http-final.log`,
`admission-summary.json` and `review/live/loc/after/`.

<a id="ri-204"></a>

### RI-204 — Parsed eager images wait behind stylesheet completion

**Fixed for loader-stage HTTP discovery, 2026-11-22.** Initial dependency
prefetch queued stylesheets and waited for their completion before image
discovery. Parsed eager images could not overlap that wait or subsequent
script execution. The loader now queues them after the stylesheet requests
and before the wait, using the existing image-discovery callback. Responsive
source selection, the active base, admission, fetch priority, deduplication
and document ownership share their normal paths. Explicit lazy images stay
outside this early pass; inert templates and script-enabled noscript content
remain inert. Ownership follows **D5.5.1 / D9.1.1v4 / D9.1.3v2 / D9.5.6v2**;
see [HTML image loading](https://html.spec.whatwg.org/multipage/images.html#updating-the-image-data).

Two causal HTTP fixtures hold a stylesheet until its selected image arrives
or a fixture-only fallback releases it. Frozen170 requests neither image
before that release. The repair passes both ordinary and responsive-picture
cases, including active-base resolution, invalid/mixed-case loading states,
one request, inert/unselected/lazy controls, element completion before
window.load, natural size and PNG pixels. Chromium headless shell confirms
both final cases. All 41 resource-discovery tests, nine image-completion
tests and layout integer lint pass. Full baselines are not repeated.

Squarespace's fresh native pair now contains its large hero photograph and
headline and falls below the major threshold. Mean error against the same
fresh Chromium reference is 18.87% for the historical native image, 6.27%
for frozen170, and 4.14% for the repaired capture. Frozen170 also restores
the main content, so the page clearance reflects cumulative fixes and the
current recapture, not proof that this one repair restored the whole page.
Animated skew, small typography and a browser-only promotional strip remain.
The updated report has 108 high-severity pages. **D10.1.7** capture policy,
resource grace, script limits and vendor code are unchanged. Jetty's hero
is lazy and WebKit's photos are CSS backgrounds; neither is claimed fixed.
Evidence: `temp/online-visual-round171/discovery-before.log`,
`discovery-after.log`, `discovery-final.log`, `discovery-browser-final.log`,
`image-completion.log`, `same-reference-metrics.json`, `final/`, `frozen/`
and `browser-review/`.

<a id="ri-203"></a>

### RI-203 — Scalar height queries lose valid results on unrelated layout passes

**Fixed for qualified retained queries, 2026-11-22.** The scalar height cache
discarded every entry when the document mutation or measurement generation
changed, including changes in unrelated subtrees. Width queries already use
subtree, inherited-style, presentation, font and layout versions to retain
eligible results. Height entries now reuse that version comparison and the
existing resource/ordered-effect exclusions. Descendant restrictions propagate
through hits and computations. Provisional contexts cannot publish retained
results; current-pass queries keep their existing constraint behavior, while
old-pass history uses the existing nine-query retention bound. Ownership follows
**D5.5.1 / D9.1.1v4 / D9.1.3v2 / D9.5.6v2**.

The focused native test fails three unchanged-subtree reuse assertions against
frozen169 and passes with the repair. It also checks changed source height,
inherited em sizing and a source edit measured in a provisional context. An
initial provisional control incorrectly required discarding an older, still
valid normal result; the final control changes the source and requires fresh
normal geometry. All 49 IncrementalCssTest cases were run: 42 pass, including
the new case, and seven pre-existing failures retain the same assertion
outcomes as frozen169. Thirteen native PNG mutation/CSSOM groups, two existing
UI fixtures with 22 assertions, and layout integer lint pass. MIR/Chromium and
full baselines are not repeated for this native cache increment.

In bounded 20-batch Primer traces, final stable-pass height computations fall
from 2981 to 2759 and hits increase from 126 to 314. Both traces take about
11.1 seconds; neither is a completed page capture or evidence of a startup
speedup. The 120-second startup watchdog remains unresolved. No page is
cleared, and the report retains 109 high-severity pages. Capture policy under
**D10.1.7**, deadlines and source admission are unchanged. Evidence:
`temp/online-visual-round170/core-before-with-libraries.log`,
`core-before-controls.json`, `core-final.json`, `bounded-profile-comparison.json`,
`primer-symbolicated.txt`, `relational-ast.log`, `ui-regressions.log` and `lint.log`.

<a id="ri-202"></a>

### RI-202 — CSSOM dimensions expose the border box for content-box elements

**Fixed for qualified sizing cases, 2026-11-22.** Native CSSOM serialization
read the retained border-box dimensions for both box-sizing modes. A 90px
content-box width with 6px padding and 2px borders therefore serialized as
106px. Chromium reports 90px while retaining the 106px border rectangle.
The serializer now uses the existing boundary-to-content conversion for
committed content-box sizes. Border-box and pre-layout specified sizes retain
their respective sizing boxes; no box-metric calculation is duplicated.
Ownership follows **D5.5.1 / D9.1.1v4 / D9.1.3v2 / D9.5.6v2** and
[CSSOM resolved values](https://www.w3.org/TR/cssom-1/#resolved-values).

The focused `computed-size` group fails on frozen168 and passes with the
repair in AST, MIR and Chromium. It checks width/height, both box-sizing
modes, min/max constraints, independent border geometry and transforms that
must not alter resolved CSS sizes. The strengthened empty-text control also
checks the 90px CSS width against its 106px border rectangle. This is bounded
qualification, not complete CSSOM dimension conformance. Evidence:
`temp/online-visual-round169/box-browser-probe.json`,
`computed-size-before.log`, `final-ast.log`, `final-mir.log` and
`test/online/render_relational_mutations.test.cjs`.

<a id="ri-201"></a>

### RI-201 — Relational dependency checks rebuild unrelated measurement text

**Fixed for qualified dependency paths, 2026-11-22.** Primer repeatedly
measures a hidden text clone during startup. Full-document reconciliation
occurs even when the mutation cannot affect its relational selectors. Of 18
authored selector probes, the intermediate repair still falls back only for
`.w-richtext:is(.article-rich,.rich-text) :is(ul,ol):has(>li>span+span)`:
internal sibling combinators bypassed ancestor filtering, and static
functional type guards were ignored. Native ownership and the shared DOM
seam follow **D9.1.1v4 / D9.1.3v2 / D9.5.6v2 / D8.4.4**.

The dependency walk distinguishes leading sibling paths from internal
siblings, checks the existing selector-prefix predicate, and evaluates fully
static compound `:is()`/`:where()`/`:not()` guards with the existing matcher.
Descendant/child witnesses use stable type/class/id/attribute conditions;
positional and empty conditions also inspect the parent and removed subtree.
Text records retain local selector recascade and view-property reset, so
removed empty-state padding cannot survive. Column paths, recursive leading
siblings, unknown state/functions and batched attribute changes keep the
conservative dependency path. See
[Selectors 4 relational selectors](https://www.w3.org/TR/selectors-4/#relational).

Thirteen focused groups pass in AST and MIR and have Chromium qualification;
six groups fail on frozen168. Two existing DOM UI fixtures retain all 22
assertions, and 27 metadata/SVG mutation regressions pass. A new wrapped
leading-sibling control caught an intermediate width regression; that
candidate was never installed. The strengthened empty-text control caught
stale padding in another intermediate path before installation. Integer lint
passes. The repair does not qualify major missing features, change script
admission or alter **D10.1.7** capture timing. Evidence:
`temp/online-visual-round169/selector-probes/results.json`,
`frozen-final-fixtures.log`, `typed-after-first.log`, `empty-box-probe.html`,
`final-ast.log`, `final-mir.log`, `final-regressions.log` and `final-ui.log`.

<a id="ri-199"></a>

### RI-199 — Native test harnesses failed to link in the full test build

**Fixed, 2026-11-22.** `make build-test` failed on macOS arm64 because the
input-value test lacked stylesheet helpers and the custom-layout and
state-store tests linked Radiant's archive alongside conflicting host stubs.
The isolated tests now link their actual native sources, including shared
render-tree and effect queries; stylesheet tests link the real resource and
mutation helpers and resolve fixtures from the Radiant owner checkout. Native
and host ownership follows **D9.1.1v4 / D9.1.3v2**. A separate TOML test
compile error was fixed by supplying its required mutable input buffer.

The three affected Radiant test executables pass, as does the focused TOML
case. `make test-layout-baseline` passes with 5,632 passed, 292 partial and
12 skipped; `make test-radiant-baseline` passes with 8,176 passed, 296 partial
and 20 skipped. The targeted Radiant integer-cast lint also passes. Evidence:
`temp/layout-baseline-build/`.

<a id="ri-197"></a>

### RI-197 — Retained view state discards the DOM lifetime stamp

**Fixed 2026-11-22.** After Input replaced the per-node registry with
structural ownership, StateStore continued storing an address and ID.
Reconstructing a reference from that pair caused each cold validation to
walk the document. PDF navigation and scene replays multiplied those walks
across all retained entries during event validation.

StateStore stores and renews the full weak reference under D5.5.1. Input
shares one structural ownership snapshot across bulk checks (LI-098).
State transitions validate the settled interaction graph at the enclosing
batch boundary and still check the legality of each transition under
D1.10. This avoids resolving live interaction owners during unload,
after document weak proofs have been revoked.
[StateStore](../radiant/radiant/state_store.cpp) ·
[Transition validation](../radiant/radiant/state_machine.cpp)

The same replay profiles showed local recascades rediscovering selected CSS
resources for the entire tree. Discovery now follows the recascaded subtree
under D5.5.3. Full document loading still walks the document.
[CSS cascade](../radiant/radiant/css_cascade.cpp)

<a id="ri-195"></a>

### RI-195 — Headless Scene3D fixtures bypass the device mixer

**Fixed 2026-11-21.** The full host selects headless mode at runtime under
**D9.2.1v2**. Output and capture capabilities follow **D11.3.1** and **D11.3.2**.

After Radiant `4b0cdad`, headless documents use virtual submixes. Device
mixing and retirement skip them, so three Scene3D tests failed audio
assertions and a fourth crashed while reading a missing cue history status.
The GTest runner reported all 32 cases in that worker as failed because the
process exited before writing its JSON report. It had already passed 30.

The four tests now select a device-bound submix before creating any audio
state. They use a hidden graphics window and the offline audio bus. The cue
test requires a status before reading it. [Scene3D unit fixture](../test/test_scene3d_unit_gtest.cpp)

The macOS ARM64 debug build passes the four tests in three consecutive
runs, the headless document-audio control, and all 656 Scene3D native cases
through the baseline runner. `make run-radiant-baseline` records 8,172 passes,
298 partial results, 20 skips and zero failures. Evidence is under
`temp/scene3d-unit-regressions/`.

<a id="ri-194"></a>

### RI-194 — A flex-sized grid scroll container has no scroll range

**Fixed 2026-11-21.** Native layout and scrolling belong to Radiant under
**D9.1.1v4 / D9.1.3v2**; the range follows CSS Overflow 3 scrollable overflow.
Take an `overflow: auto` grid with `flex: 1; min-height: 0` in a fixed-height
column flexbox. It was laid out at the right size, and script reported Chromium's
`clientHeight/scrollHeight` (240/790), but the wheel never moved it. A block
list in the same position scrolled. A fixed-height grid outside flex also
scrolled. Homer Studio's Structures list (`.structure-list`) could not scroll.

`finalize_block_flow` publishes a scroll container's range from its flow
extent grown to its in-flow descendants' bounds. After line stretching,
`flex_update_item_scroller` refreshed every scrolling flex item's range from
`content_height` alone. For a block item that is its content extent. For a grid
it is the block cursor, which grid layout never advances (the log shows
`content_height:0.0`), so the refresh reset the vertical range to 0. The
descendant-bounds step is now `layout_scroll_container_extent` (declared in
`layout.hpp`), shared by `finalize_block_flow` and the flex refresh. The flex
path therefore keeps the range block flow measured and adds only the stretched
viewport.

Verification: new UI fixture `test/ui/layout_flex_grid_item_scroll`
(Chromium-captured 240/790 and 240/1600, plus a wheel step). It fails before
the repair (the grid's `scrollTop` stays 0) and passes after. The reduced
grid list scrolls from y 60 to −190, like the block list. The Homer Studio
Structures list scrolls (`tests/panels.json`).

<a id="ri-193"></a>

### RI-193 — Auto grid rows grow to scroll containers' content in a definite container

**Fixed 2026-11-21.** Native grid layout belongs to Radiant under
**D9.1.1v4 / D9.1.3v2**; the behaviour follows CSS Grid §6.6 and §11.5–§11.7.
Take a grid with `height: 300px` and one auto row whose items are
`overflow: auto` or `overflow: hidden` with 1000 px content. Radiant made the
row and every item 1000 px tall, so the scroll containers had nothing to scroll.
Chromium 148 (headless shell) gives 300 px for all three items. Homer Studio's
side panels (a `flex: 1` grid in a 100vh column) grew to 1682.8 px in a 772 px
row and could not scroll.

The grid algorithm sized the row correctly; the post-content correction pass
in `layout_grid_multipass.cpp` then broke it. That pass raises each row to its
items' laid-out heights. It applied the §6.6 automatic minimum (zero for a
scroll container) only to `fr` rows of a definite container, so `auto` rows took
the full content height. In a definite container, items raise a row's base size
only through their minimum contributions; growth toward max-content just fills
free space, which the algorithm pass already distributed. The pass now uses
`grid_item_minimum_contribution` for every row of a definite container. The
scroll test uses `layout_overflow_establishes_scroll_container`, so
`overflow: clip` (not a scroll container) keeps its content minimum. Indefinite
containers keep the max-content correction.

Verification: new baseline case `grid_121_auto_row_scroll_container_items`
covers a fixed-height grid and a `flex: 1` grid in a fixed column. Its reference
was captured from Chromium's headless shell, and the case passes 100%. The
reduced page gave 1000 px before the repair and 300 px after.
`make layout pattern=grid` gives 396 of 402 both with and without the repair;
the same six cases fail either way (four `grid_fr_span_2_proportion_*` cases
and two `grid-item-input-type-*` cases). The Homer Studio panels now measure
772 px and scroll on every tab.

<a id="ri-191"></a>

### RI-191 — Clips added by a scene3d re-render never receive host ticks

**Fixed 2026-11-21.** Scene animation ownership belongs to Radiant under
**D9.1.1v4 / D9.1.3v2**. No `S#`/`D#` ruling covers clock ownership on
regeneration. The working rule is Radiant_Design_Scene3D §3.3: prepare an edit
transactionally, and replace only the affected services.

`scene3d_animation_refresh` builds the edited scene as a quiet staged replay
(`scene3d_animation_create(..., replay = true)`), which always clears the
candidate's `automatic` flag. The refresh then publishes the old owner's flag.
A scene first rendered without clips (a still preview, or a character before a
motion is picked) has `automatic = false`. When an edit adds an autoplay clip
or a `<motion-controller>`, the new owner keeps that `false`. The clip samples
its first pose and never advances in a live window. The headless
`scene3d update` op only steps scenes that are not automatic, so fixtures that
stepped clips by hand hid the defect. Homer Studio's Abilities preview and
Character motions were frozen live.

`automatic` carried two meanings, so it is now split. `playable` records that
the template asks for the clock (an autoplay clip or a controller), captured
before replay clears `automatic`. `manual` records that a seek, an adopted
physics replay, or an explicit `scene3d_animation_automatic` host or script
command chose the clock. Rescheduling goes through a private
`scene3d_animation_schedule`, which leaves both flags unchanged. Refresh
publishes `automatic || (!manual && candidate->playable)` and carries `manual`
forward. A manually driven scene keeps its choice across edits, and a scene
that loses its clips keeps the previous flag, as before.

Verification: the new native case
`AnimationReconciliationLetsAddedAutoplayClipsClaimTheClockUntilTheHostSeeks`
covers both paths: an added clip advances on scheduler ticks, and after a seek
a later clip stays at its seeked time. Each of the 87
`Scene3dTest` animation, motion, controller and clip cases passes in its own
process; RI-192 prevents running them in one process. All 132 positive scene3d
view fixtures from `cases.inc` and `contracts.inc` pass headlessly. The 9
expected-failure fixtures fail as designed. A reduced headless probe advanced
with `advance_time` reproduced the defect before the repair: a clip added after
the first render, autoplay or one-shot controller, held its t = 0 pose. After
the repair it moves. `test_radiant_view_gtest.exe` itself was not run, because
its link fails in `make build-test` for unrelated reasons. Evidence: the Homer
Studio probes under `studio/temp/auto/`.
<a id="ri-190"></a>

### RI-190 — CSS resource discovery repeatedly scans the DOM for a base URL

**Fixed 2026-11-21.** Resource acquisition follows **D9.1.2v2** and uses the
existing admission policy under **D11.1.2**.

The first-resource discovery change in Radiant `952b770` enabled selected CSS
prefetch on local documents before a resource manager exists. Prefetch looked
up the document base for every selected declaration without a stylesheet
base. On documents without an active `<base>`, each lookup scanned the entire
DOM. That quadratic cost appeared in the PDF load and in recascades during
the document editor, spreadsheet and Hamster replays. A release stack sample
of the document-editor replay showed the DOM base lookup beneath the cascade
resource walk.

The URL visitor now resolves bases on demand and shares the document fallback
within each discovery pass. Each URL-bearing declaration resolves its own
stylesheet base. Requests that already have an absolute URL skip the
document-base lookup before they enter the resource queue.

The ten reported replays pass on the macOS ARM64 release build in
0.95–2.77 seconds each in serial and 2.02–5.99 seconds in the parallel Radiant
baseline. The baseline records 8,172 passes, 298 partial results, 20 skips and
zero failures. All 70 loopback resource checks pass, as does the local-file
control with a CSS image as its first remote dependency and a relative inline
image under an active `<base>`. Timing reports and the stack sample are under
`temp/radiant-perf-regression/`.

<a id="ri-189"></a>

### RI-189 — Expired no-fill effects retain undeclared opacity

**Fixed 2026-11-21.** Native style ownership follows **D9.1.1v4 /
D9.1.3v2**; the selected snapshot follows **D10.1.7**. No ruling changes.

The new network/fade controls exposed a second, independent defect. An
80ms linear fade-out with no fill and no authored opacity leaves the red
probe almost white at 250ms, instead of restoring its initial opacity 1.
A declared-opacity control restores correctly. The finish callback already
requests recascade, but reset_non_inherited_style_cache omits opacity, so
the retained InlineProp keeps the last active sample. Color restoration
already works and is not the root cause.

The existing reset now restores initial opacity through ensure_inline before
current declarations and animation samples apply. This also gives removed
opacity declarations a clean base without changing inheritance or forcing
visibility. Default and declared no-fill controls pass in native and Chromium;
all 15 block/flex/grid intrinsic-motion controls retain their implicit,
explicit, authored, variable and inherited endpoints. The network-created
fades now finish fully opaque as well. Layout integer-cast lint passes.

Evidence: `temp/online-visual-round155/opacity-before.log`,
`focused-qualified.log`, `lint.log`, and
`test/online/render_capture_settle.test.cjs`. Complete motion conformance and
automatic Web Animations playback remain outside this repair.

<a id="ri-188"></a>

### RI-188 — First script-created remote stylesheet is never requested

**Fixed 2026-11-21.** Native resource ownership follows **D9.1.1v4 /
D9.1.3v2**, with the retained export frame under **D10.1.7**.

A local document with only inline CSS has no native resource manager. After a
dynamic classic script creates a remote stylesheet link,
`radiant_discover_document_resources` returned at its no-manager guard before
visiting links. The link callback already called the shared
`queue_resolved_http_resource`, which can initialize that manager, but was
unreachable. The script and load event ran; the stylesheet was never requested.

Discovery now visits links before checking the manager for later resource
phases. It uses the existing queue, policy and transfer implementation; no
runtime dependency is added to Radiant. The focused stylesheet control fails
before repair and passes afterward, requests the CSS once and paints its blue
probe. The shared control also passes in Chromium and MIR. The enclosing
pre-Location batch passes 95 focused host checks. No source limit, deadline,
clock or reference tolerance changes. This is independent of a page's remaining
script/bootstrap failures.

Evidence: `test/online/{dynamic_script_fixtures.cjs,render_dynamic_scripts.test.cjs}`
(`stylesheet`), `temp/online-visual-round153/probe-stylesheet*`,
`focused-candidate.log`, `focused-mir.log` and browser stylesheet results.


<a id="ri-187"></a>

### RI-187 — Snapshot downloads finish without publishing image state or callbacks

**Fixed 2026-11-21.** Native resource ownership follows **D9.1.3v2 /
D5.5.1**, retained realm binding **D6.4.1**, and fixed-frame export
**D10.1.7**. No formal ruling changes.

A650ms HTTP image finishes after the loader's250ms completion phase but
within snapshot preparation's existing1000ms resource window. Preparation
flushed the downloaded bytes without polling the document's native pending
work. `DomImageSource` therefore retained an empty authoritative surface,
which overrode the decoded embed image. Its load event and decode promise
also remained queued after the initial JavaScript drain. The PNG was blank.
An80ms control painted correctly and ran both callbacks; tracing the failing
650ms case showed one completed resource and zero failures.

Snapshot preparation now binds the retained document through the existing
script-loop host, flushes resource processing, polls native pending work and
pumps current tasks without advancing virtual time. It repeats within the
same resource deadline and performs the final completion handoff under the
same binding. No runtime dependency is added to Radiant, and no timeout,
source limit, clock offset or forced visibility changes.

All56 focused image/resource tests pass, including nine new controls for
parsed, constructed, detached and picture images; failure and decode rejection;
chained sources; an unrelated slow sibling; a script-free control; and a
zero-time capture. Five initial cases fail before repair. All nine controls
agree with Chromium on their completion pixels. The clock invariant compares
completion against the last ordinary timer/rendering frame; both bootstrap
and post-load completions are covered without requiring a later frame.
Integer-cast lint and all three repository diff checks pass. No broad baseline
is repeated. Evidence: `temp/online-visual-round151/`, `late-before.log`,
`focused-final.log`, `browser-late-results.json`, and `late-clock-probe.log`.

Current Jetty/Pelago pairs are inspected, but native pixels are identical with
the prior renderer on those current pages. No live improvement or page
clearance is attributed to this repair. Jetty's media/navigation and Pelago's
consent state remain major differences. The repaired late-completion path has
no remaining blocker; the full high-severity goal remains open.

<a id="ri-186"></a>

### RI-186 — Linear-gradient resolution discards a sole double-position color

**Fixed 2026-11-21.** Native selected-style consumption belongs to
**D9.1.3v2**, shared CSS providers to **D9.1.1v4**, and retained stop storage
to **D5.5.1**. The linear-gradient resolver allocated stops only for two
comma-separated color groups, despite its existing stop builder expanding a
double-position color into two stops. It now permits one group and reuses
that builder and its ownership path. Input's corresponding admission defect
is repaired as LI-096. No formal ruling changes.

Hera's saved DOM contains a dark `linear-gradient(#010a14 0% 100%)` underlay.
Dropping it exposes white beneath a partially transparent radial layer. The
same-source1200×800 comparison improves from13.6926% mean error /30.8646%
changed area to2.0472% /2.4167%. The large light band disappears. This is
diagnostic saved-source evidence: Hera's independent live text-reveal gap
remains and no canonical row or major flag is cleared.

All12 new shared PNG cases fail before repair; all12 pass afterward and
agree with Chromium. The124 native CSS parser tests and25 existing PNG/color
controls pass. Captures, admission limits and clocks are unchanged. No vendor
edit or broad baseline run. Evidence: `temp/online-visual-round150/`,
`saas_landing_463-{lambda,browser}.png`, `hera-after.{png,json}`,
`gradient-{before,after}.log`, and `browser-gradient-results.json`.
The repaired stop form has no remaining blocker; full-page clearance is open.

<a id="ri-185"></a>

### RI-185 — Sparse GIF ticks and completion select the wrong frame

**Fixed 2026-11-21.** Native playback belongs to **D9.1.3v2**, finite
capture selection to **D10.1.7**, and decoded-buffer ownership to **D5.5.1**.
No formal ruling changes.

The player advanced only one frame per scheduler tick. A two-frame GIF with
100ms/200ms delays still displayed its second frame after a single 650ms tick,
although the selected time is in the first frame of its third loop. Pauses,
sparse UI ticks and timed exports could therefore display the wrong image.
The renderer also treated the encoded repeat count as total plays, ignored
the absence of a loop extension, and freed frame pixels on natural completion,
restoring the initial still instead of holding the final frame.

The player now samples the active loop and its frame deadlines directly in
O(frame count), without replaying elapsed loops. Its diagnostic loop counter
saturates before integer conversion. Encoded positive counts add the initial
play, zero with an extension remains infinite, and no extension means one
play. Forward fill retains decoded final pixels until the existing cancellation
path releases the player and restores the cache-owned still. Completion frees
the other frame buffers.

All 26 native GIF tests pass. Five sparse-tick variants and two new
repeat/completion tests fail before repair. Three of seven shared PNG controls
fail with the previous qualified renderer; all seven pass after repair and
agree with Chromium. Ten existing SVG image-motion controls and the layout
integer-cast lint pass. No broad baseline or online sweep is repeated.

The mixed GIF and Lottie replay now checks the second GIF frame at both 150ms
and 1050ms, then the first at 1250ms under D10.1.7. All six pixel assertions pass.

This was found while reviewing Maple's apparent headline-font difference.
The fresh asset is actually animated WebP, whose later-frame playback remains
unsupported under LI-030. The GIF repair does not clear Maple or another
high-severity page. Capture clocks, admission limits and comparison rows are
unchanged. Evidence: `temp/online-visual-round149/`,
`radiant/test/test_gif_player_gtest.cpp`, and
`test/online/render_gif_playback.test.cjs`.

<a id="ri-184"></a>

### RI-184 — External script success never dispatches the element load event

**Fixed 2026-11-21.** Host ownership follows **D9.1.1v4 / D9.1.3v2**;
native event entry and precise roots follow **D6.4.1 / D6.3.3**. Youtrust's
46,464-byte deferred consent SDK executes within the existing admission limit,
but its script's onload callback never runs. The scheduler reported fetch
errors without reporting successful external completion, leaving both the
home and pricing pages without their large consent dialog.

The scheduler now dispatches load after evaluation and its microtask checkpoint,
including empty sources and runtime throws. Failed graph fetches retain error;
inline classics/modules do not receive resource load events. currentScript is
restored before the handler, while the parser's suspended head tail and write
cursor remain suspended. A rooted original script target survives removal and
forced layout. Shared completion dispatch enters the document's execution
scope so native handler entry preserves bootstrap's pending mutations and
stylesheet owners. A minimal external-script fixture otherwise loses its
injected stylesheet unless an earlier CSSOM read publishes it.

Ten focused HTTP cases pass for blocking, defer, async, module, runtime throw,
empty source, self-removal, fetch failure and both inline kinds. They check
event count/shape, execution and callback microtasks, currentScript, parser
visibility, and pixels from evaluation and earlier bootstrap stylesheet writes.
Seven lifecycle cases fail against the preceding release; five stylesheet
checks fail against the initial unscoped completion implementation. Chromium
controls confirm the lifecycle distinctions. Six existing inline-script and
stylesheet tests pass, and the milestone baseline-js gate passes **630/630**.

With the existing 1000ms settling interval and current matched browser references,
home mean error falls **30.49% → 1.02%** and pricing **31.18% → 2.74%**.
Both pages also clear against the older references. Their old animation origins
were unrecorded, so the current matched pairs are published and the older pairs
archived. Minor typography, focus and background layout differences remain.
All other1065 comparison rows are preserved;
inventory becomes **111 high, zero critical,956 low**. No source/transfer
budget, capture clock or page condition changed (**D10.1.7**).
Evidence: `temp/online-visual-round145/`,
[report131](../test/online/comparison/repairs.html#round-131),
[HTML script execution](https://html.spec.whatwg.org/multipage/scripting.html#execute-the-script-element).

<a id="ri-183"></a>

### RI-183 — image-set string sources never enter the resource queue

**Fixed 2026-11-21.** Ownership follows **D9.1.1v4 / D9.1.3v2**.
The existing image consumer accepts string sources in image-set, but resource
discovery only visited URL-tagged values. Generic recursion reached the source
string and ignored it, leaving a network-managed background permanently without
bytes. The native HTTP image-set-string regression paints white before repair;
an ordinary quoted url() control passes. This is an existing discovery defect,
not a missing decoder or image-set selector.

The shared visitor now uses the existing parsed image-set option helper, visits
only each option's image and normalizes its string source to a borrowed URL
value for the callback. type() strings remain metadata; declaration-relative
spelling and the existing density/type selection are unchanged. Other URL,
function, list and custom fallback traversal remains in place.

After repair, three native GTests pass, including the new cached image-set
export in the10-case image export matrix, the18-case priority matrix and the
stylesheet-token guard. Six HTTP controls pass for image-set strings, ordinary
quoted URLs, punctuation/encoded inline URLs, variables and inert templates.
The image-set test checks green pixels and the stylesheet-relative request,
and rejects a request for the MIME string. Integer lint passes. No full
baseline, capture clock, source/transfer budget or page condition changed.
The syntax follows [CSS Images4 §2.4](https://drafts.csswg.org/css-images-4/#image-set-notation).
Evidence: `temp/online-visual-round144/resource-spelling-before.log`,
`focused-native.log` and `focused-http.log`.

A saved-style scan finds one high-page exposure in Conductor's webinar poster
rule, not proof that this below-fold image causes its major difference.
Overpass's portrait is an independent readiness observation: a read-only
saved-source capture with the existing1000ms settling interval has the same
pixels as the self-assignment diagnostic (0.78098% mean error). Its downloaded
PNG paints correctly locally; no CSS publication or decoder patch is justified.
No page clearance is claimed. All1067 comparison rows and browser references
are preserved in [report130](../test/online/comparison/repairs.html#round-130).

<a id="ri-182"></a>

### RI-182 — CSS-only local documents cannot start their first network resource

**Fixed 2026-11-21.** Native/input ownership follows **D9.1.1v4 /
D9.1.3v2**. The selected-style traversal and URL callback required an existing
resource manager. RI-181 let img/poster discovery create it, but a document
whose first dependency was a CSS background still could not queue that
resource. Its synchronous fallback did not consume the shared durable cache.

The URL callback now uses the existing lazy queue helper with the absolute URL
resolved against the declaration's stylesheet base. Selected-style traversal
no longer requires a manager. Admission, active/inert selection, variable
resolution and existing priority/transfer/wait budgets are preserved. The
queue helper reports success only for a created or reused resource.

Four new export cases fail before repair and pass afterward: inline background,
stylesheet background, custom-property URL and pseudo-element background.
The unchanged five img/poster cases, the resource-priority matrix and the
stylesheet-token guard still pass (three GTests total). Evidence:
`temp/online-visual-round143/background-before.log` and `focused-after.log`.
Three focused HTTP base/template/variable-resource controls and integer lint
also pass. The release paint control changes from0 to2993 red pixels for the
same cached73×41 CSS image. Current matched1000ms Overpass still lacks its
live hero (24.49% mean error); its saved-source portrait is independently
unresolved (2.51% diagnostic error). No whole-page clearance or attribution
of its live startup difference is claimed. No broad baseline was repeated.
See [report129](../test/online/comparison/repairs.html#round-129).

<a id="ri-181"></a>

### RI-181 — Local HTML image/poster exports never initialize network discovery

**Fixed 2026-11-21.** Under **D9.1.1v4 / D9.1.3v2**, native snapshot
preparation returned when a local document had no resource manager. Only
playable media could initialize one, leaving ordinary remote images and
posters on a synchronous path that did not consume the shared durable cache.
The unchanged ExportQueuesOrdinaryNetworkImagesBeforeInitialLayout test
fails independently before repair; the starting host also fails the seeded
SVG control. Resource queuing now initializes a manager lazily for an
admitted HTTP dependency, and initial snapshot discovery includes images and
posters. The existing wait limit and resource policy are unchanged.

The unchanged five-case export regression now passes: natural SVG dimensions,
explicit video dimensions, poster-only sizing, max-height and size containment.
The priority-discovery matrix and stylesheet-token guard also pass. Integer
lint passes. No full baseline was repeated. Evidence:
`temp/online-visual-round141/export-isolated.log`, `export-cache-before.log`
and `focused-final.log`. This local export repair alone clears no online page.

<a id="ri-180"></a>

### RI-180 — Authored fetchpriority never reaches resource scheduling

**Fixed 2026-11-21.** Ownership follows **D9.1.1v4 / D9.1.3v2**.
Discovery assigned fixed priorities to images, preload links, stylesheets and
scripts; the early source prefetch path also ignored fetchpriority. A saved
DOM scan finds hints on61 remaining high pages; this is exposure, not evidence
that scheduling caused their full-page differences. Module scripts also
lost their lower default when rediscovered through the DOM.

One native helper parses high/low as ASCII case-insensitive enumerated
keywords, shifting the existing destination default by one bounded scheduler
rank. Missing, auto and invalid values retain the default. Both discovery
and early prefetch use it. Dynamic image source requests pass their owner;
passive decoding borrows an existing request without promoting a low hint
back to normal. Shared URL identities retain the existing promotion logic.
Input supplies the borrowed manager lookup and an atomics-free priority
header; no runtime dependency or scheduler values change.

The cache-backed native integration checks18 discovery cases plus missing
lookup, passive reads, dynamic hints, shared identity and promotion. It and
two existing loading controls pass (three GTests total); integer lint passes.
No source/transfer budget, capture clock, site condition or vendor code changed.
IDL fetchPriority reflection and complete fetch API priority support are not
claimed. The keyword semantics follow the
[HTML fetch priority definition](https://html.spec.whatwg.org/multipage/urls-and-fetching.html#fetch-priority-attributes).
Evidence: `temp/online-visual-round140/priority-hints.json` and
`temp/online-visual-round141/focused-final.log`. Three focused HTTP controls also
pass. Current matched1000ms Aircall and Dayos pairs both load, with unchanged
major differences (37.89% and62.33% mean error). No online clearance is claimed;
see [report128](../test/online/comparison/repairs.html#round-128).

<a id="ri-179"></a>

### RI-179 — Font inheritance overwrites local shorthand and re-evaluates ancestor declarations

**Fixed 2026-11-21.** Native computation belongs to Radiant under
**D9.1.1v4 / D9.1.3v2**. Coda's zero-specificity font shorthand specifies
700, but actual layout reports and paints weight400. The inheritance guard
only checks direct longhands, overlooking the local font shorthand. It then
recascades an ancestor's weight400 with its original specificity. Children
also search ancestor syntax instead of inheriting the computed parent scalar.

For font components with a local shorthand node, inheritance now uses Input's
existing property-source winner lookup. Other inherited properties retain
their cached direct lookup. Missing weight/style/variant components copy the
parent's computed scalars. Native UA weight/style defaults remain local via
the shared HTML metadata; explicit all:inherit/unset still inherits.
No site-specific rule, new font parser or capture policy change.

Two flushed zero-specificity regressions fail before repair. Focused controls
also cover computed-parent variables, small-caps, native heading/emphasis
and explicit unset. The first candidate exposed lost UA defaults in the
text baseline; reduced heading/emphasis controls reproduce the defect and
the final metadata guard repairs it. All23 focused native cases and15
matching Chromium controls pass; integer lint passes. Coda's saved-source
post-layout heading changes from400 to700, matching Chromium. Its remote
background readiness is separate; no paint defect or live clearance is
inferred from that diagnostic. Complete font reset/relative-weight
conformance is not claimed. Evidence: `temp/online-visual-round137/`,
`font-where-flush-before.log`, `font-ua-before.log`,
`focused-ua-final.log`, `browser-font.json` and final baseline results.
Final targeted categories record **1526 passes /84 partials /zero failures**.
The current matched live Coda pair remains blank after startup exceptions;
no page clearance is claimed. The missing IndexedDB observation is tracked
without assigning blank-frame causality in JS13-L36.
See [report125](../test/online/comparison/repairs.html#round-125).

<a id="ri-178"></a>

### RI-178 — Loader computed-style queries ignore font shorthand components

**Fixed 2026-11-21.** Shared cascade ownership follows
**D9.1.1v4 / D9.1.3v2**. Before a layout flush, getComputedStyle serialized
font weight/style/family from the generic declaration fallback, which omitted
font from its component selection and projection. Valid700/italic/Arial
shorthands therefore reported defaults while the separate size path correctly
reported38px. Native formatter tracing confirms the initial font assignment
itself was correct; RI-179 covers the independent later inheritance defect.

Normal and pseudo fallback reads now select the winning font declaration by
full cascade priority and use Input's shared projector (LI-092). Variable
substitution uses the source shorthand grammar before component extraction,
including virtual longhands that identify the shorthand itself. Existing
pseudo overflow behavior remains intact. No duplicate font grammar.

Seven of the original eight reduced cases fail before repair; the explicit
later-component guard already passes. All15 final font cases and eight
existing font/button controls pass, alongside15 independent Chromium cases.
Numeric/keyword weights, stronger/important/variable shorthands, descendants,
pseudo fonts and later explicit components are covered. Broader system-font,
CSS-wide reset and pseudo variable contexts remain outside qualification.
Evidence: `temp/online-visual-round137/font-before.log`,
`font-extra-before.log`, `focused-ua-final.log` and `browser-font.json`.


<a id="ri-171"></a>

### RI-171 — Layout harness aborts inheritance setup before display cleanup

**Fixed 2026-11-21.** The failure reported at earlier milestone checkpoints
was a fixture-execution defect, not incorrect display inheritance. The
layout-only builtin WPT harness lacked `assert_not_equals`. Upstream
`inheritance-testcommon.js` temporarily sets the container to `initial` and
the target to `unset`, then invokes that missing function. The resulting
ReferenceError aborts before cleanup, leaving both divs inline and zero-width.
Chromium's full harness completes cleanup and retains their block defaults.

The existing native layout shim now provides a truthful inequality assertion
using SameValue (`Object.is`), throwing on equal values rather than ignoring
failed assertions. No layout/cascade code, browser reference, fixture or
required-pass entry changed. Ownership follows **D9.1.1v4 / D10.1.7**.

The new native regression fails before repair with the missing-function
ReferenceError. After repair it checks cleanup, equal primitive/NaN rejection
and signed-zero/distinct-object acceptance. It and both existing HTML script
tests pass. The unchanged original `wpt-css-display/inheritance` case improves
from 2/4 to **4/4 matched elements (100%)**. The full
17-category milestone was last run before this repair (5609/294/1); it was
not repeated. This is no online-page clearance. Evidence:
`temp/online-visual-round132/harness-before.log`, `focused-after.log`,
`display-inheritance/`, `inheritance-after.json` and `inheritance-result.json`.

<a id="ri-177"></a>

### RI-177 — Grid alignment replaces final flex height with an intrinsic estimate

**Status:** FIXED, 2026-11-21. **Lambda blocker:** no. Native formatting and
size ownership follow **D9.1.1v4 / D9.1.3v2**. Snapshot timing is unchanged
under **D10.1.7**.

After formatting a nested flex container, grid called a separate intrinsic
height estimator and stored its result for alignment. It could differ from
the completed formatter's border box. A script-free Payload reduction has
547.2px recorded height but 483.2px final in-flow content. Start, center and
end alignment all reproduce the mismatch; Payload itself moves its main
heading and actions downward and exposes translated hover content.

The grid content pass now publishes the completed flex formatter height,
matching its existing nested-grid handoff. The obsolete estimator helper is
removed; no repeated layout, special row/column inference, tolerance, forced
height or site condition is added.

All three new regressions fail before repair and pass afterward; Chromium
confirms all three. Thirty-six focused native grid tests pass, covering the
new regressions and spanning minima, percentage item sizing, row refinement,
nested grids and flex/grid constraints. Integer lint passes. The same saved
Payload document improves from 9.33% mean error / 14.17% changed area to
3.28% / 6.01% after RI-176/177. Its major vertical shift and leaked hover
content are restored; native button appearance remains a separate defect.
No live-page clearance is inferred from a frozen-source repair. Evidence:
`temp/online-visual-round136/`, original and repaired source captures,
`grid-flex-before.log`, `grid-height-after.log`, `browser-height.json` and
`payload-metrics.json`. The 17-category milestone records 5608 passes / 294 partials / two form
failures / zero skips. Both form failures (`option-text-setter` and
`input-type-button`) reproduce with the previous qualified round134 renderer;
no new baseline regression is observed. References and assertions are retained.
See [report124](../test/online/comparison/repairs.html#round-124).

<a id="ri-176"></a>

### RI-176 — Flexible spanning grid items retain a content-based automatic minimum

**Status:** FIXED, 2026-11-21. **Lambda blocker:** no. Track sizing belongs to
Radiant under **D9.1.1v4 / D9.1.3v2**.

The automatic minimum contribution was min-content for every non-scroll item,
including items spanning multiple flexible tracks. A 600px descendant therefore
expands a two-fr-track item in a 400px grid to 600px. Payload's four-of-sixteen
column span becomes 291.23px instead of 268px, changing text wrapping and
centering. [CSS Grid §6.6](https://www.w3.org/TR/css-grid-1/#min-size-auto)
requires a zero automatic minimum for this flexible spanning case.

Contribution collection now considers the selected axis tracks before sizing.
Only the automatic minimum is zeroed; explicit item minima and intrinsic track
minima remain effective. The existing flexible-track predicate has an opt-in
zero-fr mode for this rule, preserving its distribution callers' behavior.
No new pixel threshold or track sizing shortcut is introduced.

Three native cases fail before repair and pass afterward: columns, rows and
zero-fr spans. Three guards retain single-track, explicit item and intrinsic
track minima. Five Chromium column controls and one row control agree. The
combined RI-176/177 qualification passes all 36 focused native cases and layout
integer lint; source/capture clocks and budgets remain unchanged. Evidence:
`temp/online-visual-round136/grid-before-final.log`, `grid-row-before.log`,
`grid-height-after.log`, `browser-grid.json` and `browser-height.json`.

<a id="ri-175"></a>

### RI-175 — CSSOM reports internal table rows as block

**Fixed 2026-11-21.** Shared computed display follows **D9.1.3v2 /
D9.5.6v2**. The display model stores table-row as an internal role with a block
outer box. CSSOM serialized only the outer enum and returned block, while
layout output had separate table-role mappings. The existing shared
css_display_special_name helper now uses is_table_internal_display and the
registered CSS enum name. CSSOM and layout output reuse that helper, with no
new tag map or geometry change. The rollback matrix verifies table, table-row
and table-cell names, and all 78 focused native render controls pass.
Evidence: Lambda `temp/online-visual-round131/`.

<a id="ri-174"></a>

### RI-174 — Display rollback reapplies the discarded all reset and shows metadata text

**Fixed 2026-11-21.** Radiant's used display and shared CSSOM follow
**D9.1.3v2 / D9.5.6v2**. Mode uses all:unset with display:revert. The cascade
correctly removes author declarations for display, including all, and returns
no surviving author winner. The display resolver then independently reapplied
the global all reset's initial inline display. Head, style and script text
became visible instead of receiving their UA defaults.

The resolver now falls through to the existing css_default_display_for_element
when no cascaded display survives. Actual winning initial/unset declarations
continue through ordinary value resolution. No unconditional suppression of
script/style tags or page-specific rule is added. Six before controls fail
(all initial/unset/inherit crossed with revert/revert-layer); three guards
already pass (winning initial/unset and explicit visible style). All nine pass
after the fix and RI-175's shared table-role serialization correction.

All 78 focused native rendering controls and integer lint pass. Mode's current
capture again shows its landing page, with a large residual difference; the
intermediate source-text diagnostic is not imported. Dock's whole-page capture
is qualified separately under RI-031. No source admission, capture deadline,
clock or vendor change. Evidence: Lambda `temp/online-visual-round131/`.

<a id="ri-173"></a>

### RI-173 — SVG stroke-phase attributes force full HTML layout on every animation frame

**Fixed 2026-11-21.** Shared DOM/host ownership follows **D9.1.3v2 /
D9.5.6v2**; SVG paint-cache lifetime follows **D5.5.1**. FramerAuth's late
animation batches mix ordinary HTML transform writes with SVG path
`stroke-dashoffset` updates. The attribute record discarded property identity,
so one SVG record rejected the entire retained paint batch. Repeated whole-page
flow reached the 150-second capture deadline.

The native mutation record now retains the SVG paint-property hint. Its
attribute kind, observer delivery and ordinary document/style epochs remain
unchanged. The SVG paint path requires a connected element, rendered SVG
viewport, no attribute-selector/attr() dependency and no transition or relevant
animation sampling requirement. It refreshes SVG CSS and repaints without HTML
flow. SVG batches request full repaint; no local ink-bound assumption is added.
Existing global SVG cache epochs still invalidate inherited values and shared
references. The motion guard is shared with HTML admission instead of copied.

Four reduced cases fail before repair with eleven layouts instead of one:
ordinary attributes, null-namespace writes, inherited group attributes and
CSSOM property writes. All preserve observer records, geometry and final pixels
after repair while retaining one layout. Attribute-selector and active
transition controls keep ordinary cascade/layout; the transition fixture keeps
its initial zero-time phase. All **30 focused native checks** pass, along with
seven comparison-tool controls and integer lint. The 17-category layout
checkpoint reports **5,609 passed, 294 partial and the existing RI-171 failure**.
No reference or baseline entry is weakened.

The unchanged live FramerAuth request now loads and captures in **70.04s**,
within the existing 150-second deadline at the same 1000ms post-load frame.
The main hero remains absent, so its manual high review is retained. Current
Chromium records Web Animations opacity effects from an authored 0.001 to 1 on
its heading, copy and actions; automatic native playback remains a documented
feature gap. No opacity, timing, URL or admission exception is introduced.

Evidence: `temp/online-visual-round124/framerauth-attributes.log`,
`temp/online-visual-round125/`, and
`test/online/render_svg_dash_mutation.test.cjs`.

<a id="ri-172"></a>

### RI-172 — Retained transform resets leave fixed descendants in the old containing block

**Fixed 2026-11-21.** Retained geometry ownership follows **D9.1.3v2**;
effect/geometry admission follows **CI-18**. The paint-only guard rejected the
literal `none` keyword but admitted CSS-wide resets and pending substitutions.
These can compute to `none`, removing the transform's containing block. The
property resolved correctly while fixed descendants retained their old layout.

The guard now requires a non-keyword value with no pending substitution before
retaining an existing transform's geometry. Other values use ordinary CSS
resolution and layout. This covers `transform`, `translate`, `rotate` and
`scale` without changing parsing, inheritance or CSS variable semantics.

All sixteen reduced before cases fail: `var(--clear)` (defined as `none`),
`initial`, `unset` and `inherit` leave the fixed child at y75 instead of the
viewport-relative y5. All pass after repair. The full focused selection passes
**24 checks** (11 inline-style and 13 metadata); concrete transform writes still
retain one layout while moving geometry/pixels. Integer lint passes. The
round122 layout checkpoint is not repeated for this guard correction.

Evidence: `temp/online-visual-round124/transform-reset-before-expanded.log`,
`focused-after.log` and `test/online/render_inline_style_mutation.test.cjs`.
No live page clearance is established by these controls.

<a id="ri-170"></a>

### RI-170 — Retained paint publication leaves mutation layout dirtiness pending

**Fixed 2026-11-21.** Geometry publication and selected-frame ownership follow
**D9.1.3v2 / D10.1.7**. The retained paint reconciler resolved an eligible
property and published its input versions, but mutation recording had dirtied
the target, descendants and ancestors. The next geometry checkpoint therefore
performed full flow despite the successful paint commit.

When a proven paint or detached batch publishes against the same viewport,
font, cascade and layout environment, publication now consumes dirty flags via
the existing layout visitor. Pending reflow, scheduler work, selector-dependent
writes, transitions and transform containing-block changes retain their
existing guards. No paint eligibility is broadened here.

The CSSOM transform control changes geometry and pixels correctly with one
layout rather than eleven. All six inline-style and thirteen metadata controls
pass; the integer-cast lint passes. Before this fix the transform count still
fails after property notification is repaired (JS13-L30). Evidence:
`temp/online-visual-round121/` and
`test/online/render_inline_style_mutation.test.cjs`. Live performance and page
clearance require separate capture evidence.

<a id="ri-138"></a>

### RI-138 — Trial flow resets the scroll position of an EPUB frame

**Fixed 2026-11-18.** Trial flow and committed scroll state have
separate ownership under **D5.5.1**.

In `test/ui/ebook_paged.json`, a page turn sets the iframe's horizontal
position to 798 px. A subsequent intrinsic layout temporarily sizes the frame
with an indefinite height and publishes a zero scroll range. That clamps
the scroll position to zero before the committed layout restores the range.
A size-only pass uses the iframe's outer box and leaves its embedded
viewport and scroll state with the committed layout. The UI replay passes all
five assertions, including the second page turn after changing typography.

<a id="ri-137"></a>

### RI-137 — Inline flex exports a baseline before its children have line geometry

**Fixed 2026-11-18.** Final flex flow owns the selected baseline
geometry under **D5.5.1**.

WPT `baseline-source-first-001` places the span beside an inline column flex
box at y=701 px instead of y=656 px. The flex baseline cache contained zero
because it was populated before the children had completed their layout.
The final-content pass now publishes the same baseline calculation after
child layout and baseline alignment. The focused test matches all 125 element
boxes and 51 spans in the reference.

<a id="ri-136"></a>

### RI-136 — Boxless DOM ancestors erase flex decoration offsets

**Status:** FIXED, 2026-11-18. **Lambda blocker:** cleared for Affinity's
navigation positioning; the page remains high for missing hero artwork.
Native ownership follows **D9.1.1v4 / D9.1.3v2**, retained geometry ownership
**D5.5.1**, and selected capture time **D10.1.7**. No formal ruling changes.

The saved isolated Affinity header has 20px top padding on its flex container.
Items inside display:contents wrappers receive y0, while a direct item receives
y20; Chromium places all at y20. flex_set_axis_position reads decoration
from the item's DOM parent, which has no principal box. Its companion cross
position getter reads that same incorrect border owner.

Both lookups now reuse layout_nearest_block_ancestor, preserving the DOM/style
ancestry while using the actual formatting box's decoration offsets. Reverse
flow, alignment and existing baseline coordinate contracts are retained. No
new ancestor walker, generated wrapper, authored CSS edit or page condition is
introduced. No public header, layout-state shape or scripting ABI changes.

Eight new real PNG controls compare otherwise identical plain and boxless
wrapper layouts across row/column/reverse, wrap-reverse, RTL, vertical writing
and ignored wrapper decorations. All eight fail on the saved renderer and
match in independent Chromium controls. All eight pass with the repair, along
with three existing contents/ratio cases and nine recent shadow-inheritance
regressions: **20 focused native checks**. Integer-cast lint checks89 casts
with zero unmarked; both repository diff checks pass. No broad baseline is
repeated; round83 remains the recent broad gate.

The original header reduction places all three labels at y20 after repair.
One unmodified live Affinity capture completes in1.90 seconds at the existing
1000ms request, with readable aligned navigation. Four hero artwork panels
remain absent. Their stored files are complete, but initial markup has an
opacity-zero wrapper and required deferred bundles exceed current admission;
that separate host barrier remains LR12-57. No unhide or budget increase is
adopted. The report retains high severity and review history, preserves its
Chromium reference and all1066 unselected rows. Evidence is in Lambda's
`temp/online-visual-round89/` and
`test/online/comparison/repairs.html#round-89`. Release SHA-256:
`ba5f2b7dab87c82a48b6507962951c893cefa7d577a9987f44d93dd97cd66c2d`.

<a id="ri-135"></a>

### RI-135 — Retained descendants lose inherited text shadows

**Status:** FIXED, 2026-11-18. **Lambda blocker:** cleared for Boldare's
missing major headline words. Native ownership follows **D9.1.1v4 / D9.1.3v2**;
computed-list ownership follows **D5.5.1**. No formal ruling, script admission,
resource budget or selected capture time changes (**D10.1.7**).

Boldare paints white headline glyphs on white boxes with black text shadows.
The first layout can precede the application's shadow-variable assignment.
After that assignment, direct text recovers its shadows but nested text loses
them: text-shadow was absent from the runtime inherited-property refresh list.
An initial FontProp copy happened to inherit the list, hiding the missing
retained-layout path. Explicit inherit/unset also discarded the shadow list.

The runtime inheritance list now includes text-shadow. Implicit inheritance and
explicit inherit/unset share one helper that clones the nearest parent's
computed shadow list into element-owned storage. Parent-relative offsets and
colors are preserved instead of substituting declarations against the child's
font or custom properties. Replacements release the previous owned list;
none/initial and local overrides retain their reset/precedence behavior.
No foreground-color fallback, authored-page edit or site condition is added.

Nine real PNG regressions in Lambda's
`test/online/render_text_shadow_inheritance.test.cjs` cover defining a missing
variable after layout, removal, changed parent font-relative offsets, a child's
conflicting variable, explicit inherit/unset, local none, nested transforms and
initial. The saved renderer fails six and passes three controls. All nine pass
after repair, and independent Chromium controls agree with all nine. Eight
existing ancestor text-background cases also pass: **17 focused native checks**.
The layout integer-cast check and both repository diff checks pass. The prior
round83 broad baseline remains the milestone gate; it is not repeated here.

One unmodified live Boldare capture completes in 7.12 seconds at its existing
1000 ms request. ONLINE and FOR REMOTE TEAMS are fully readable; the remaining
small animated decorations and raster details are outside this major-only
pass. Its original matched-window Chromium image is preserved. The manual
major review is resolved with history retained; all other page rows and browser
metadata are unchanged. Report qualification is in Lambda's
`test/online/comparison/repairs.html#round-88` and evidence in
`temp/online-visual-round88/`. Release renderer SHA-256:
`18507bc770defa4ab16928b2183fff3a6ffb0fbd68734d131a5ffec1dd9bc483`.

<a id="ri-134"></a>

### RI-134 — Video snapshots reject decodable frames while playback opens

**Status:** FIXED, 2026-11-18. Native snapshot ownership follows
**D9.1.1v4 / D9.1.3v2** and immutable frame storage follows **D5.5.1**.
Capture does not advance media or virtual time (**D10.1.7**).

Mymind's missing large halo is an autoplay H.264 video, not a CSS gradient.
Its complete 3,091,800-byte source is cached. Both that local file and the
repository's 640x360 H.264 fixture expose native dimensions but paint a gray
placeholder. Independent Chromium captures show both real first frames at
time zero, excluding transfer, URL, codec and animation-phase explanations.

render_video_snapshot excluded RDT_VIDEO_STATE_LOADING before asking the
existing frame decoder. AVPlayer's asynchronous opening state can lag a valid
AVAssetImageGenerator frame from a complete file. The non-poster path now uses
the decoder result to establish frame availability while opening. Error/idle
guards, poster precedence, failed-decode placeholders, immutable pixel ownership
and the existing time getter remain unchanged. No source exception, new codec,
run-loop wait, clock advance or transfer/admission-budget change is introduced.

Three focused first-frame/poster controls pass; the saved renderer fails both
paused and autoplay landscape cases and passes the poster control. Thirteen
existing video sizing cases and five selected media-discovery cases also pass
(21 total). Backend qualification is macOS only. Larger incomplete HTTP
transfers remain RAD22-3; other platform decoders remain RAD22-2.

Mymind's unchanged one-second browser reference now agrees in major composition:
the halo, heading, copy, navigation and actions are present. Coarse error falls
to 7.18% with 16.36% changed area; the recorded missing-halo review is resolved.
Small spacing and native playback-overlay differences remain. No broader video
time synchronization or exact pixel parity is claimed. Evidence:
../../lambda/temp/online-visual-round87/ and
../../lambda/test/online/comparison/repairs.html#round-87.

<a id="ri-133"></a>

### RI-133 — Valid SVG images with leading XML comments are rejected

**Status:** FIXED, 2026-11-18. Native image admission belongs to Radiant
(**D9.1.1v4 / D9.1.3v2**).

WebKit's logo, search and menu SVG responses start with a copyright XML comment.
The shared image signature detector skipped BOM/whitespace but inspected the
comment as if it were the document root, rejecting valid SVG bytes before the
existing XML/SVG loader could run. HTTP images, data URLs and extensionless
image probes share this detector.

The detector now skips complete leading comments using the existing bounded
str_find helper, returning false for unterminated comments. It preserves the
existing XML-declaration and local-root-name paths; namespace validation remains
with the loader. No URL, file extension or response-header exception is added.

Six focused data-URL PNG checks cover ordinary SVG, XML declaration, copyright
comment, BOM/multiple comments with embedded markup, unterminated comment and
comment-only input. The saved round83 binary fails the two valid comment cases;
all six pass after repair. Twelve existing SVG percentage/intrinsic controls
also pass. Live WebKit no longer emits those three unknown-format rejections.
The two major missing article photos remain: their valid PNG requests are
queued, but independent downloads take 2.45/2.71 seconds, beyond the existing
one-second resource window. This fix does not claim a high-severity clearance
or change capture/resource budgets. The fresh round83 milestone gate is retained;
this narrow non-layout repair uses focused checks. Evidence:
../../lambda/temp/online-visual-round84/.

<a id="ri-132"></a>

### RI-132 — Assigned inline measures are resolved as containing widths again

**Status:** FIXED, 2026-11-18. Intrinsic query contracts belong to native Radiant
(**D9.1.1v4 / D9.1.3v2**).

The RI-129 formatter repair correctly uses the element's resolved content width
when its caller supplies a containing-block width. Some callers already choose
the content width: balance's candidate line-count queries and block intrinsic
axis/containment queries. They left the existing width_is_used_size flag false,
so authored width/min/max values could replace their selected candidate.

The round83 milestone exposed three regressions: text-wrap-balance-overflow-002,
text-wrap-balance-003, and the vertical min/max-content block-size fixture.
A/B checks match all three references on round79 and reproduce identical bad
scores on both the frame-only and capture-blocker round83 binaries. Their
reference scores include 0%/9.1% element matching for the balance cases and
64.7% elements / 2.0% text for vertical sizing.

The three callers now use the existing assigned-content-width contract. No
underlying formatter change is reverted, no special writing-mode/site branch
is added, and no reference, baseline score or threshold is changed. All three
original cases again match their frozen element, span and text references at
100%. Evidence and preserved pre-repair binaries are under
../../lambda/temp/online-visual-round83/.

<a id="ri-131"></a>

### RI-131 — Text balancing recursively measures its own atomic inline flow

**Status:** FIXED, 2026-11-18. Intrinsic measurement and layout belong to native
Radiant (**D9.1.1v4 / D9.1.3v2**); transient probes keep their scoped lifetime
under **D5.5.1**.

Vooma crashes with native stack exhaustion when the selected window commits its
page layout. The saved pre-repair renderer reproduces the same failure, and a
small static balanced paragraph containing an inline-block with a block child
crashes without any JavaScript. The release link map identifies the cycle:
balance line count -> max-content height -> atomic inline-flow height -> inline
setup -> balance line count for the same owner. The height fallback needs
ordinary wrapping to count candidate lines, not another balancing search.

The existing balance-probe scope now tracks active owners and restores the
previous probe on destruction. Only a repeated owner is excluded from starting
another search while its line-count probe runs; nested and sibling paragraphs
retain their own balancing. No depth cutoff, all-measurement disabling, heap or
layout-context ABI change is introduced.

Four focused PNG regressions cover one atom, repeated atoms, nested balanced
owners and plain text. Three crash before the fix and all four pass afterward,
with visible text and atomic content. Integer lint passes. Exact shaping and
broader clamped-prefix balancing remain outside this repair. This unblocks the
shared crash path found during Lambda's online comparison goal. Evidence:
../../lambda/temp/online-visual-round83/.

<a id="ri-130"></a>

### RI-130 — Text-clipped backgrounds ignore descendant glyphs

**Status:** FIXED, 2026-11-18. Native paint belongs to Radiant
(**D9.1.1v4 / D9.1.3v2**). The existing glyph masks and owned paint resources
retain their lifetime contract under **D5.5.1**.

Wiza's saved heading and description have transparent foreground fills. Their
radial/color text background belongs to an ancestor wrapper, while the actual
text nodes belong to child headings/paragraphs. The painter checked only the
immediate text parent and returned before recording the ancestor background.
The text disappeared even though the existing masked background path already
handles the required gradient/color layers.

One shared owner traversal now feeds native raster and vector recording. It
visits outer owners first so inner clipped layers stay above them, and reuses
the existing block-coordinate ancestor-offset helper to keep each gradient in
its owner's box. Direct-owner fragment geometry is unchanged. The same glyph
mask and layer compositor provide coverage; no fallback text color or page
selector is added. Fragmented inline owners, transformed descendants and wider
image/effect interactions remain partial rather than implied by this repair.

Eight reduced cases cover direct text, block and nested inline descendants,
radial paint, a translated owner, nested clipping owners, an offset hard-stop
gradient, and ordinary foreground text. The old renderer fails five cases and
passes three controls. Chromium confirms all eight. After the repair, 31
focused raster clipping/inline-decoration cases pass, along with integer lint.
Three SVG descendant/coordinate controls and a separate nested-owner SVG check
cover vector recording and reusable masks. PDF is not independently qualified.
No full baseline is repeated; the preceding round79 milestone remains the broad
checkpoint.

Wiza's identical saved source restores its large heading and description;
coarse error falls from 6.23% to 4.77%. Navigation alignment and the decorative
background still differ. Its normal live capture remains blank/high despite a
successful document load, so no live-page clearance is claimed. The browser
reference, capture clock, deadlines and thresholds are unchanged (**D10.1.7**).
Evidence is under Lambda's `temp/online-visual-round82/`.

<a id="ri-129"></a>

### RI-129 — Intrinsic inline flow loses the measured box's containing sizes

**Status:** FIXED, 2026-11-18. Native sizing follows **D9.1.1v4 /
D9.1.3v2**. Trial geometry retains the selected flow under **D5.5.1**.

Clay's saved pricing source places percentage-sized inline images inside
112px-wide, 16:9 logo covers. Its native logo strip grows to 1494.8px rather
than Chromium's 95px, pushing all pricing cards below the viewport. The final
logo cover itself is 112x63; its erroneous intrinsic contribution inflates the
outer flex row. Removing tooltips or changing inline-flex to flex does not
repair it. Replacing images with fixed boxes does.

The intrinsic height query already resolves the cover's content width and
ratio-derived height basis. Its inline-flow fallback passed the outer query
width and created an assigned block context with an indefinite height. The
image consequently used an unrelated percentage width and natural-height
fallback. The shared formatter now receives the resolved content width and
height basis. No early ratio-height return, image exception or fixed clamp is
introduced; automatic heights and overflowing content retain their behavior.

Six reduced cases cover two ratio sizes, fixed-height and block-image controls,
indefinite automatic height, and ratio content growth. Chromium confirms all
six. Before: three fail and three controls pass. After: all 50 focused inline,
button, nested-ratio and flattened-flex cases pass; integer lint passes. The
preceding round79 broad gate remains the milestone; it is not repeated for
this focused repair.

The identical saved source restores the logos and four pricing cards. Coarse
error falls from 12.54% to 10.37%; heading line-height and smaller details still
differ. The normal live capture remains blank and high severity. Its authored
anti-flicker script sets a hidden class and removes it after 4000ms, while the
unchanged export policy selects virtual frame zero (**D10.1.7**). Separate
inline/deferred script TypeErrors are observed but not attributed to this
sizing bug. No delay, visibility override or reference refresh is added and
no live-page clearance is claimed. Evidence is in Lambda's
`temp/online-visual-round80/`, including both same-source PNGs and focused logs.

<a id="ri-128"></a>

### RI-128 — Background keyframe shorthands discard their color component

**Status:** FIXED, 2026-11-18. Native animation ownership follows
**D9.1.1v4 / D9.1.3v2**, and parsed keyframe storage follows **D5.5.1**.

The keyframe parser chose the animation value type from the declaration's
property ID. The background shorthand has no direct animation track, so even
`to { background: lime }` was dropped while its background-color equivalent
worked. A longhand CSS control disproved an initially suspected clock fault.

The shared parser normalizes the existing color track but retains the original
shorthand value and grammar. After variable substitution, the existing
background component helper supplies the color. An omitted color follows the
initial-value path. Normalized targets also preserve shorthand/longhand source
order and exclude important keyframe declarations. No image/layer animation
subsystem or duplicated shorthand parser is introduced. Lambda's Web Animations
admission uses the same normalized target (LR12-56, **D9.5.6v2**).

Six native/Chromium CSS cases cover color, a full-grammar variable, omitted-color
reset, both declaration orders and important exclusion. All pass. Explicit
Web Animations shorthand and longhand seeks also pass. Automatic Web playback
is a separately documented unsupported surface: both longhand and shorthand
stay at their initial sample without an explicit seek. This repair does not
claim playback support. Before evidence and the 29-case focused pass are under
Lambda's `temp/online-visual-round79/`.
The release Radiant milestone gate passes 8,169 / 8,488, with 299 partial,
20 skipped and zero failures; the curated 201 document cases are unchanged.

<a id="ri-127"></a>

### RI-127 — Render exports ignore the host-selected settling frame

**Status:** FIXED, 2026-11-18. Native export ownership follows **D9.1.1v4 /
D9.1.3v2**. The selected frame is prepared before encoding; encoding does not
move virtual time (**D10.1.7**).

Layout honors LAMBDA_POST_LOAD_SETTLE_MS, but the common render-export host
configuration silently ignored it. Python's authored first-slide fade therefore
remained at opacity zero despite an explicitly selected later frame. A second
control showed script-free CSS had no JS timer loop to prepare that frame.

Layout and export now share validated host-option selection. Explicit CLI zero
still overrides a host default. Export honors the selected interval; a
script-free document samples its existing CSS scheduler after initial layout.
Unspecified exports still use zero. Malformed/negative windows are refused.
No default delay, site exception or timeout extension is introduced.

The focused loopback tests cover an 80ms task versus a later 3000ms task, explicit
zero, unchanged defaults, script-free CSS, option precedence and browser parity.
The old release fails the selected-window task control; all 29 focused capture,
comparison and animation checks pass after both repairs. Capture methodology is
recorded separately: Python's paired 1000ms capture restores the carousel,
but this is not an unchanged-policy engine-driven clearance. WebKit's article
grid becomes visible; missing large article images keep manual high severity.
Evidence: Lambda `temp/online-visual-round79/` and the separate
`test/online/comparison/capture_frame_updates.json` audit.

<a id="ri-126"></a>

### RI-126 — Intrinsic inline-height trials skip atomic placement and reset frame scrolling

**Status:** FIXED, 2026-11-18. Native layout ownership follows
**D9.1.1v4 / D9.1.3v2**. Trial geometry and retained document state follow
**D5.5.1**.

RI-123 extended intrinsic height measurement to inline runs containing atomic
boxes. The scalar cache and definite-size shortcuts returned child dimensions
before joining the parent line. A fixed-size child contributed zero to the
intrinsic height, and `baseline-source-first-001` placed the sibling of an
inline flexbox 45px below the reference. Trial layout also visited the EPUB
reader's positioned frames and refreshed scroll ranges from provisional geometry.
Both page-next actions left the chapter on page 1.

An inline-height trial now formats its lines and atomic boxes in a measurement
mode that bypasses scalar shortcuts. It omits out-of-flow placement and embedded
document layout. It restores the selected flow without refreshing the live
scroll ranges from the trial replay.

The broader height trial also exposed a cyclic percentage on a range control.
The height query refreshed the control's CSS while the auto-height parent had
34px of provisional inline flow. Ordinary layout had already cleared the 50%
height. The query put back the resulting 17px before the final control sizing.
The block height query now restores the caller's selected preferred height and
its type before the caller applies the intrinsic constraint.

The rebuilt debug host passes `IntrinsicQueriesRestoreCallerSizingContext`,
all 169 `wpt-css-inline` baseline entries, all five `ebook_paged` assertions,
and all 28 inline-height and line-break rendering checks. The Radiant integer
cast lint passes. The full `make test-radiant-baseline` exits 0 with 8,169 passed,
299 partial, 20 skipped, and zero failures. Reproduction and verification logs
are in the Lambda checkout under `temp/radiant-test-fixes/`.

<a id="ri-125"></a>

### RI-125 — Absolute-child layout unwinds a foreign scratch allocation

**Status:** FIXED, 2026-11-18. Scratch lifetime follows **D5.1.4 / D5.5.1**
and native ownership follows **D9.1.1v4 / D9.1.3v2**.

The flattened child list in `layout_absolute_children_in_context` was
allocated with plain `scratch_calloc` inside an active `ScratchScope`.
The buffer survived until scope exit, where the scope detected a foreign
allocation and asserted. Allocate the list through the scope's
overflow-checked `array_zero` so it remains live through child layout and
is reclaimed by its owner.

The rebuilt debug host passes `flex_grid_placement_ignored`,
`chart_timeline_export`, `mermaid_state_diagram`, `proc_timeline_export`,
and `archive_pkg_render`. The first four aborted before producing test
results. In the archive case the export worker aborted, so its parent
reported failed PDF and thumbnail checks. The original
`Scene3dTest.OutdoorGardenFogWindWaterAndCascades` replay also passes, as
does the Radiant integer-cast lint.

The before/after logs are in the Lambda checkout under
`temp/failed-tests-20261118/`. The original outdoor backtrace is in
`temp/outdoor-lldb.log`.

<a id="ri-124"></a>

### RI-124 — Float roundoff falsely wraps an exactly fitting atomic inline

**Status:** FIXED, 2026-11-18. Native ownership follows **D9.1.1v4 /
D9.1.3v2**. No capture clock, threshold, budget or browser reference changes
(**D10.1.7**).

Python's search form is 348.8812866px wide and its adjacent social menu is
98.03125px. Their containing float measures 446.9125061px. Separate float sums
differ by about 0.0000305px, so a strict greater-than fit test puts the menu on
another line and shifts the following page by about 46px. This is distinct from
round44's already repaired internal-button whitespace isolation. Integral-width
controls fit with and without internal whitespace; fractional edge controls
reproduce the failure without text, fonts or network resources.

Promote the existing float exclusion edge comparator to layout.hpp as
layout_edge_at_or_before and reuse it at the four atomic inline fit/float-band
decisions. Its existing scale-relative FLT_EPSILON policy absorbs one float
roundoff; no fixed pixel allowance or second comparator is introduced. The
reverse float-edge comparison delegates to the same helper. This preserves the
project's float geometry convention while avoiding spurious soft line breaks.

Six new PNG cases cover inline-block, inline-flex and inline-grid at the
rounding boundary and with genuine 0.1125px overflow. All three fitting cases
fail before; all three real-overflow controls pass before. Chromium confirms
all six. Afterward all **32 focused inline-height/break/button-isolation
checks** pass, as does integer lint. The same-source Python comparison improves
from 10.8% to 0.27%; its header and following content align. The live capture
loads and scores 4.20%, but the carousel code and explanatory content remain
absent, so the pre-existing manual high-severity annotation is retained.
No page clearance is claimed. The preceding round74 broad checkpoint remains
the aggregate evidence; no broad baseline is repeated for this focused repair.

The preceding media diagnosis used a temporary optimized trace, now removed.
It verified that Jetty's correct 1200w hero URL was queued but still pending at
the existing wait: 53 total resources, 15 completed, zero failed. Squarespace
had 50/12/0 and Pelago 20/16/0. Those observations do not establish an image
decoder or sizing bug. No priority exception or larger wait was added.

Evidence in Lambda: temp/online-visual-round78/fractional-before.log,
fractional-browser.json, pixels-qualified.log, final-metrics.json,
python-children-probe native/browser logs, before/ paired PNGs and the final
Python PNGs; resource-readiness-summary.json retains the separate media trace.
All diagnostic source instrumentation was removed before the final build.

<a id="ri-123"></a>

### RI-123 — Intrinsic atomic-inline height loses the enclosing line strut

**Status:** FIXED, 2026-11-17. Native ownership follows **D9.1.1v4 /
D9.1.3v2**; selected layout fragments retain their measurement ownership
(**D5.5.1**). Capture clocks and budgets remain unchanged (**D10.1.7**).

Optimizely's script-free heading lays out at 222.3px, but its containing flex
column inside a grid receives only 117px. This shifts the hero copy and the
following section upward by 105.3px and changes background-image placement.
The heading contains an atomic inline box with a block measurement descendant
and positioned per-letter boxes. The intrinsic inline-text collector cannot
flatten that structure. Its fallback takes the maximum child height, losing
the enclosing parent's line strut, although normal layout already includes it.

The failed-flattening path now reuses intrinsic_inline_flow_content_height,
which already invokes the real inline formatter and restores selected fragments.
It preserves baseline alignment and the enclosing strut without a second
height estimator or a site condition. The obsolete second qualification
collection is removed. This implements
[CSS 2.2 §10.8](https://www.w3.org/TR/CSS22/visudet.html#line-height).

Six small cases cover 24px/80px ancestor line height with an atomic inline
containing a block child, under block, flex and grid parents. Chromium confirms
all six. The 80px grid case fails on the preceding release; the other five
remain controls. The repaired trial passes all 22 focused inline-break/height
checks. Identical saved Optimizely HTML improves from 13.56% to 1.4% mean
difference; the major vertical shift disappears. Text strokes and small font
details remain outside the major-difference scope. The final simplified build
also passes all 22 checks and retains the 1.44% same-source result. Its normal
live capture improves from 14.43% to 2.53%; Optimizely leaves high severity.
The sorted inventory is 148 high, one critical and 918 low. Other report rows,
browser references, annotations, catalog, clocks and budgets are preserved.
Integer lint passes. The preceding passing round74 broad checkpoint is retained
without another baseline run for this focused follow-up.

Evidence: Lambda temp/online-visual-round77/strut-before.log,
strut-browser.json, pixels-qualified.log, optimizely-parity.json and
static-parity.json, the before/ image archive and qualified same-source PNGs.
No browser reference, annotation, URL, threshold, timeout or clock is changed.

<a id="ri-121"></a>

### RI-121 — HTTP media sources enter the scheduler after the snapshot wait

**Status:** FIXED, 2026-11-17, for media request ordering. Native ownership
follows **D9.1.1v4 / D9.1.3v2**; resource acquisition stays with the input
service (**D9.1.2v2**). Existing audio authorization (**D11.3.1**) and capture
clocks/budgets (**D10.1.7**) are unchanged.

Going's exact hero MP4 decodes correctly from a local file, but an HTTP source
without a script-triggered layout returns a blank frame almost immediately.
Resource discovery queued video posters only. Media source requests arose
later in style/layout through `radiant_media_prepare`, after the snapshot's
ready-byte wait. Local HTML could have no manager at that earlier boundary,
so the wait returned before the source initialized its manager.

Discovery now reuses `radiant_media_prepare` for video and audio. It queues
eligible bytes before the existing wait while retaining the same source
iteration, preload:none behavior, autoplay requests, audio permission checks,
resource identity and cache/decoder ownership. A shared media discovery helper
also lets local HTML acquire its first manager before the wait guard. Poster
requests retain their independent owner. No source iterator, policy branch,
time budget or site condition is duplicated.

Four regressions fail before: direct HTTP video, a nested source, autoplay
with preload:none, and a local document's remote source. A non-autoplay
preload:none control passes and continues making zero source requests.
The existing 640×360 repository MP4 must produce a 320×180 used box before
paint; the fast loopback reproductions eliminate large-file transfer time
as the ordering cause. All **34 focused CSS-resource/video-sizing checks**
and integer lint pass afterward. These media-decoder cases use the macOS
backend; other-platform video backends remain RAD22-2.

Going's 7 MB source remains absent in the current capture, and several
live pages retain separate startup/content omissions. This fix does not
claim progressive playback or a resolved large-file transfer; see RAD22-3.
Holocene's verified design change requires separate browser-reference
maintenance. The passing round74 curated milestone precedes this focused
follow-up and is not repeated. Evidence: Lambda
`temp/online-visual-round76/`; report
`test/online/comparison/repairs.html#round-76`.

<a id="ri-120"></a>

### RI-120 — SVG auto dimensions acquire a definite fallback natural size

**Status:** FIXED, 2026-11-17. **Lambda blocker:** resolved for SVG natural
metadata and percentage flex sizing. Ownership follows **D9.1.1v4 /
D9.1.3v2**. Capture clocks and resource budgets remain unchanged (**D10.1.7**).

Wispr's hero has width:100% SVG attributes and height="auto" with viewBox
1048×594. The SVG occupies approximately 265×150 inside a 600px flex column,
compressing the large text paths. With height omitted, a minimal flex SVG
correctly occupies 600×340.08; height="auto" produces 264.65×150. Ordinary
block layout instead keeps width600 but the same incorrect height150.

`calculate_svg_intrinsic_size` marked every nonpercentage attribute string
as a natural dimension even when `parse_svg_length` failed and returned the
300/150 default. It now reuses the existing `read_svg_number_attr` success
contract for both numeric and string attributes. Only parsed nonnegative
finite lengths set natural-axis flags; percentage guards and viewBox ratio
handling are retained. This follows
[SVG 2 §8.12](https://www.w3.org/TR/SVG2/coords.html#IntrinsicSizing): auto and
percentage dimensions do not supply natural dimensions. No alternate sizing
path, hardcoded keyword exception or new feature is introduced.

Three new auto/invalid-height cases fail before; omitted-height and numeric
controls pass. Chromium confirms the five controls and the 320px card height.
All **34 focused SVG/ratio/flex PNG cases** and integer lint pass afterward.
Wispr's saved-page error improves **15.30% to 2.43%**, restoring the large
path regions. Remaining font/animation details are not claimed fixed.
The passing round71 broad gate precedes this focused follow-up and is not
repeated. Evidence and final capture qualification: Lambda
`temp/online-visual-round73/`; report
`test/online/comparison/repairs.html#round-73`.

<a id="ri-119"></a>

### RI-119 — Positioned decorations make inline intrinsic heights stack

**Status:** FIXED, 2026-11-17. **Lambda blocker:** resolved for native inline
height measurement. Ownership follows **D9.1.1v4 / D9.1.3v2**; selected flow
restoration remains under **D5.5.1**. Capture clocks and resource budgets are
unchanged (**D10.1.7**).

Overpass's saved first heading line contains eight inline-block letters and
an absolute underline span. Its native height is 195.55px versus Chromium's
32.89px, and the hero grows to 1131.55px versus 550.53px, displacing its photo
and following section. A minimal eight-letter line is 24px tall normally but
192px with an empty absolute decoration. Fixed positioning also reproduces.

The intrinsic height classifier checked display:none but allowed out-of-flow
children to decide whether content was inline. The absolute span has a block
outer role, so it disabled inline measurement; the later loop skipped the
span yet summed the letters as separate blocks. Classification now reuses
`intrinsic_child_is_out_of_flow` before deciding the formatting mode. An
independent flow-root had a related omission: inline-child classification
was enabled only for line clamping. Ordinary flow roots now use that existing
classification too. The child-level checks and existing height paths remain
shared; no new formatter or site condition is added. This follows
[CSS 2.2 §9.2.1 / §9.4](https://www.w3.org/TR/CSS22/visuren.html#normal-flow).

The flow-root and absolute/fixed decoration reductions fail before; plain
block and inline-block controls pass. Chromium confirms all five controls.
All **42 focused flex/inline PNG cases** and integer lint pass after the fix.
The flow-root-only counterfactual does not repair Overpass, distinguishing it
from the confirmed decoration cause. With both corrections, exact-source
Overpass error improves **18.22% to 0.47%** and the hero/next section recover.
Minor heading details are not claimed fixed. The round71 full Radiant gate
passed 8,169 of 8,488 with 299 partial and 20 skipped before this focused
follow-up and is not repeated.

Evidence, counterfactual renderer and final qualification: Lambda
`temp/online-visual-round72/`; report
`test/online/comparison/repairs.html#round-72`.

<a id="ri-118"></a>

### RI-118 — Preferred widths leak into flex content-only intrinsic minima

**Status:** FIXED, 2026-11-17. **Lambda blocker:** resolved for native flex
minimum sizing. Ownership follows **D9.1.1v4 / D9.1.3v2**. Capture clocks and
resource budgets remain unchanged (**D10.1.7**).

Orum's saved hero is a 1120px row. Its left block flex item has a 600px flex
basis and width:100%; native allocates the entire row to that item and pushes
the right image outside the viewport. A reduced 360px row with a 200px fixed
sibling reproduces the inflated minimum using either percentage preferred
widths or a large fixed preferred width. A smaller fixed-width control passes.

The existing content-only intrinsic query omits the item's preferred width,
but descendant percentages still resolved against that preferred width.
[CSS Flexbox §4.5](https://www.w3.org/TR/css-flexbox-1/#min-size-auto) makes
this intrinsic percentage basis indefinite even when the preferred width is
definite. The percentage-basis helper now reads the existing active intrinsic
query stack. It recognizes content-only mode before descendant cache lookup,
so ordinary and cyclic percentage measurements receive distinct cache keys.
No new layout state, copied measurement path or site condition is introduced.

Three reduced cases fail before and the small-width control passes. All
**55 focused flex/intrinsic PNG cases** pass afterward. The new native cache
unit alternates normal/content-only queries with warm descendant caches and
passes only after the correction. Two older selected cache tests fail with
byte-identical assertions before and after this change; their existing
104/204-versus-4 failures are retained, not masked. Counterfactual binaries
use the same current test objects and differ only in the intrinsic object.
Integer-cast lint passes. The accumulated follow-ups pass the second broad
Radiant milestone: **8,169 passed, 299 partial, 20 skipped, no failures out
of 8,488 checks**.

The saved Orum source regains its right image and column allocation. Both
qualified renderers produce an entirely white current live capture, so this
repair does not clear Orum from high severity or claim a resolved startup
cause. Evidence and qualification: Lambda `temp/online-visual-round71/`;
report `test/online/comparison/repairs.html#round-71`.

<a id="ri-116"></a>

### RI-116 — Lifted layers retain an intermediate absolute ancestor's DOM offset

**Status:** FIXED, 2026-11-17. **Lambda blocker:** resolved for native paint
coordinate replay. Readymag's independent live startup remains unresolved.
Ownership follows **D9.1.1v4 / D9.1.3v2**; **D10.1.7** clocks and resource
budgets are unchanged.

Readymag's saved document has an in-flow consent box translated above the
viewport; its normal wrapper remains at y529. Its absolute main, absolute
page container and nested absolute content all report y0 in Lambda and
Chromium, but Lambda paints the page below y529. The page container is a
positive stacking layer lifted across an auto-level absolute main ancestor.
A simple absolute box alone paints correctly; the additional lifted layer
is required to reproduce the defect.

Raster and shared vector stacking-path replay subtracted
`layout_positioned_parent_offset` at the target only. Intermediate absolute
ancestors were added in DOM-parent coordinates, retaining the wrapper's y529
even though their used positions belong to an earlier containing block. Both
walkers now apply the existing helper at intermediate positioned ancestors;
raster CSS/legacy clip replay receives that corrected parent origin too.
No new coordinate helper, site condition or conservative fallback is added.

The reduced absolute case fails before the correction; a fixed ancestor is
a separate stacking context and correctly keeps its child below a later outer
layer. Chromium independently confirms both cases. All **38 focused paint
fixtures** pass afterward. The SVG witness moves the blue rectangle from y200
to y0. Exact-source Readymag error improves **41.50% to 9.87%**; mosaic and
text return to the viewport. Remaining font, white-card and widget details
are not claimed fixed. The current live page still yields a blank startup
capture, so it remains high; inventory **151 high, 1 critical, 915 low**.

The round66 broad baseline passed 8,169 of 8,488 with 299 partial and 20
skipped before the two later local fixes; it was not repeated for this
focused coordinate correction. Lambda evidence/pin:
`temp/online-visual-round68/`; report:
`test/online/comparison/repairs.html#round-68`.

<a id="ri-115"></a>

### RI-115 — Flex refinement forgets heights of items exposed through contents

**Status:** FIXED, 2026-11-17. **Lambda blocker:** resolved; Superlist's major
missing app image now paints in the actual current source and live capture.
Native ownership follows **D9.1.1v4 / D9.1.3v2**; **D10.1.7** clocks, resource
budgets and browser capture procedure are unchanged.

After RI-114 restores navigation, Superlist's current app image remains absent.
Its flex-item container has aspect-ratio 1.63 and width 1120px but zero used
height, while its row parent retains 687.12px. A 400px-wide minimal ratio box
with positioned content paints through a direct parent and disappears when
one or two display:contents wrappers are inserted. Following flow retains the
expected y200 position, distinguishing child height loss from parent collapse.

`layout_final_flex_content` captures original heights through the flattened,
ordered `flex_for_each_final_content_item` list. Its restoration helper walked
direct DOM children instead, omitting flattened descendants and potentially
misassociating reordered items. The restoration helper now takes the active
flex layout and reuses the same existing iterator. Both ratio restoration and
cross-size refinement consume the matching height snapshot; existing final
absolute-child percentage resolution updates the restored image viewport.
No new parent walk, sizing fallback or site-specific logic is introduced.

Before the fix, the direct control passes and both contents cases fail.
All **25 focused ratio and positioned/grid fixtures** pass after the fix;
integer-cast lint passes. Superlist's app image returns in its current saved
source and normal live capture. Its temporary missing-image annotation is
removed after visual review; headline wrapping and small details remain.
The inventory becomes **151 high, 1 critical, 915 low**. Browser references,
other rows and all previous annotations are preserved. The round66 audited
reference refresh remains separate from this engine repair.

The preceding three-fix Radiant baseline passed **8,169 / 8,488**, with
299 partial and 20 skipped, before this follow-up. It was not repeated for
this local iterator correction. Evidence and the pinned release:
Lambda `temp/online-visual-round67/`; report:
`test/online/comparison/repairs.html#round-67`.

<a id="ri-114"></a>

### RI-114 — Positioned children beneath contents wrappers never receive layout

**Status:** FIXED, 2026-11-17. **Lambda blocker:** resolved for existing
flex/grid positioned-child collection; Superlist retains a distinct ratio-box
sizing omission. Ownership follows **D9.1.1v4 / D9.1.3v2**; **D10.1.7** clocks
and resource budgets are unchanged.

Superlist's saved navigation is visible/fixed but has zero geometry. It sits
below a display:contents wrapper in the outer flex container. Flex/grid item
collection flattens such wrappers and excludes out-of-flow boxes from normal
items; `layout_absolute_children_in_context` then visited only direct DOM
children and never reached the initialized positioned box.

The shared pass now reuses the existing flattened-node count/collection
helpers with scoped scratch storage. Existing containing-block, static-position
and grid-area callbacks remain in use. No new flattening implementation,
parent reparenting, site condition or sizing fallback is introduced.

Four new fixtures fail before the fix: grid placement under one/two contents
wrappers, flex absolute and nested flex fixed boxes. All **29 focused paint
fixtures** pass afterward; integer-cast lint passes. The fixed navigation
returns in the exact saved source. A milestone gate for RI-112/RI-113/RI-114
passes **8,169 tests, 299 partial, 20 skipped, no failures out of 8,488**.
Lambda's native baseline runners were rebuilt in release and the documented
run-radiant-baseline command used the immutable round66 host. No references,
expected failures or tolerances were changed to pass the engine baseline.

The live site has independently changed from a dark “Tasks, notes, and plans”
page to a light “A little less on your mind” design. Archived/current HTTP
sources justify an audited browser-reference refresh. A missing large app
image still has a zero-height 1120px-wide ratio box; a manual annotation
retains high severity. This is a separate unresolved sizing defect, not a
claimed RI-114 clearance. Inventory: **152 high, 1 critical, 914 low**.
Evidence: Lambda `temp/online-visual-round66/`; report:
`test/online/comparison/repairs.html#round-66`.

<a id="ri-113"></a>

### RI-113 — Calculated preferred aspect ratios collapse auto-sized boxes

**Status:** FIXED, 2026-11-17. **Lambda blocker:** resolved for dimensionless
preferred-ratio math; Remote's live client startup remains unresolved.
Native ownership follows **D9.1.1v4 / D9.1.3v2**. **D10.1.7** capture clocks,
references and budgets are unchanged.

Remote's large SVG logo paints in isolation but disappears in the exact saved
full document. Its relative flex-item container is 1093.94px wide and zero
height in Lambda, versus 199.61px high in Chromium. Selectors, opacity and
animation-none resolve correctly. The preferred ratio is supplied through a
custom property containing `calc(1096 / 200)`.

`layout_aspect_ratio_value` read literal numeric tokens but ignored CSS math
functions, returning no preferred ratio. A shared numeric-component reader
now invokes the existing CSS math evaluator with an empty evaluation context;
both a single ratio number and numerator/denominator components use it.
Dimensional or unresolved function components reject the ratio instead of
silently being skipped. No flex-specific or SVG-specific sizing override is
added.

Two new pixel cases fail on the previous release. The final build passes all
nine related sizing fixtures and two focused native tests, including a ten-row
ratio table covering literal, calc, min, auto, calculated components, zero and
invalid dimensions. Integer-cast lint passes. Focused tests caught an initial
null-context call (type checking only), which was corrected before publishing.
The exact-source comparison improves **11.27% to 0.75%** mean image error.
The live capture still lacks the hydrated logo and headline in the preserved
browser reference, so Remote remains high severity; the inventory remains
**152 high, 1 critical, 914 low**. Only Remote's native attempt is refreshed.
No broad baseline was repeated. Lambda evidence and pin:
`temp/online-visual-round65/`; report:
`test/online/comparison/repairs.html#round-65`.

<a id="ri-112"></a>

### RI-112 — A positioned document root disappears from raster output

**Status:** FIXED, 2026-11-17. **Lambda blocker:** resolved for root painting;
Dayos retains a separate live startup overlay. Native ownership follows
**D9.1.1v4 / D9.1.3v2**; **D10.1.7** capture clocks and budgets are unchanged.

Dayos's saved script-free document renders entirely white in Lambda despite
valid visible content geometry; Chromium paints its black page and content.
A minimal `html {position:fixed;inset:0}` reproduces the loss independently of
BODY positioning or overflow clipping. An absolute root also fails; a fixed
BODY beneath an ordinary root is a passing control.

`render_raster_view_tree` passed the root to `render_children`, whose shared
normal-flow walker defers positioned boxes to a parent's stacking pass. The
document root has no such parent pass. The tree entry now uses the existing
`render_raster_stacking_view` dispatcher directly, painting the root exactly
once without changing descendant positioning, stacking or clipping rules.

Four new root fixtures fail before the correction, while the BODY control
passes. The final release passes all **24 focused opacity/position and
root/body overflow fixtures**. Chromium independently confirms the five new
cases. The exact saved Dayos source improves from **99.01% to 0.77%** mean
image error. Its normal live capture still has a full-page yellow `.site-in`
overlay (authored fixed, z-index 100); no overlay removal or clock advance is
introduced. The Nuxt bootstrap needed to remove it is not qualified here.
Dayos therefore remains high severity; the inventory stays **152 high,
1 critical, 914 low**. Full baselines were not repeated for this single
entry-point change. Evidence and the pinned release are under Lambda's
`temp/online-visual-round64/`; the repair report is
`test/online/comparison/repairs.html#round-64`.

<a id="ri-111"></a>

### RI-111 — Inline-property sharing erases isolation and other semantic differences

**Status:** FIXED, 2026-11-17. **Lambda blocker:** resolved for the existing
canonical inline-style path. Native ownership follows **D9.1.1v4 / D9.1.3v2**;
**D10.1.7** capture clocks and resource budgets are unchanged.

Operate's large gray foreground panels were painted beneath the main green
background. A script-free replay preserves the heading geometry exactly in
Chromium and Lambda, but loses the gray fill. DOM/declaration minimization and
SVG command order show a negative layer escaping its isolated ancestor.

`inline_prop_equal` and `inline_prop_hash` omitted isolation. An explicit auto
ancestor therefore allowed an isolated child to share its default inline
storage, discarding the child's stacking context during canonical promotion.
The InlineProp field audit also found ruby_position and the explicit line-strut
flag missing from both functions, plus image_rendering absent from hashing
(its equality check was already correct). One shared scalar/float field list
now drives both comparisons and hashes, preserving normalized float hashing
and deep cursor-image equality. No allocation or lifetime fallback is added.

Before linking the correction, two new pixel cases fail (green over red) and an
auto-isolation control passes. The final release passes all eleven shared
opacity/position fixtures, five focused canonical-style unit tests and three
independent Chromium isolation cases. The new unit tests exercise actual
parent/child promotion for isolation, ruby position and line-strut state, plus
existing exact-value reuse, copy-on-write and capacity fallbacks. The final
same-source and live Operate captures restore the main interface and gray
panels. Remaining widget/font/decorative details are not claimed exact matches.
Full baselines were not repeated. Evidence: `../../lambda/temp/online-visual-round63/`.


<a id="ri-109"></a>

### RI-109 — Relative HTML base URLs are ignored and absolute bases overwrite document identity

**Status:** FIXED, 2026-11-17. **Lambda blocker:** resolved for initial resource
resolution. Ownership follows **D9.1.1v4 / D9.1.3v2 / D9.5.6v2**. Fixed-frame
capture rules under **D10.1.7** are unchanged.

Unreal declares `<base href="/documentation/assets/">`. The initial loader parsed
this without its document URL, rejecting it and requesting stylesheet/script
siblings under the documentation route instead. Absolute bases replaced the
actual loader URL, also changing `document.URL` and `location.href` incorrectly.

Input now supplies one owned document-relative URL resolver. It selects the
first HTML base with href, excludes template/shadow contents, resolves against
the actual document URL and falls back for invalid/data/javascript bases. The
loader attaches the DOM root before dependency discovery and retains its actual
URL. Initial CSS/scripts, module base, images, font/inline resource fallbacks,
baseURI and XHR initialization reuse the resolver. External stylesheets keep
their own URL origins. Input LI-074 separately repairs head-template closure.

Final release qualification: nine focused native document tests pass, including
six relative/absolute/empty/prohibited/invalid-base cases with CSS, classic and
module scripts, image geometry and URL identity. Two persistent HTTP regression
tests pass. Chromium agrees on five base controls; malformed `http://[invalid`
produces about:blank in Chromium, whereas the current HTML Standard's frozen
base URL algorithm specifies document fallback after parse failure. This
reference discrepancy remains explicit; no Chromium-specific exception is added.
See https://html.spec.whatwg.org/multipage/semantics.html#frozen-base-url.
The qualified live Unreal capture restores styled documentation; visual review
remains open because its preserved browser reference is a client-side 403.
Evidence: `../../lambda/temp/online-visual-round62/`.

<a id="ri-110"></a>

### RI-110 — Resource and stylesheet discovery activates inert HTML template contents

**Status:** FIXED, 2026-11-17. **Lambda blocker:** resolved for the existing
initial discovery paths. Ownership: **D9.1.1v4 / D9.1.3v2 / D9.5.6v2**.

Raw stylesheet collection, prefetch and the live native resource scans walked
parser-backed HTML template descendants as ordinary document content. A reduced
page acquired inert CSS/scripts and turned its active green box red. The Lambda
script adapter also executed those scripts (JS13-L10).

Shared namespace-aware source checks stop initial CSS/prefetch and script
collection at HTML templates; live CSS recollection excludes their descendants.
Native resource selector, selected-style and inline-style callbacks share one
ancestor check, preventing inert sheet/script/image/background downloads.
Foreign SVG template elements remain active. No vendor code, capture clock,
resource cap or site-specific condition changes.

Nine focused document tests and both persistent HTTP regressions pass against
the final release archives. The HTTP inert fixture asserts no sheet, script,
image or background request; native and Chromium controls agree on six style,
script, stylesheet-count and foreign-SVG states. Input's three focused parser
tests and Radiant integer lint pass. Full baselines were not repeated.
Evidence: `../../lambda/temp/online-visual-round62/`.



<a id="ri-108"></a>

### RI-108 — Final grid percentage heights and intrinsic image ratios lose their current constraints

**Status:** FIXED, 2026-11-17. **Lambda blocker:** resolved for this existing
layout path; the live Cape page still has a separate startup/entrance omission.
Radiant owns these algorithms under **D9.1.1v4 / D9.1.3v2**. **D10.1.7** capture
clocks, resource budgets, references and severity thresholds are unchanged.

The Cape's saved hydrated document has a large SVG illustration inside an
auto-height grid, a percentage-height block, a flex container and an automatic
flex item. Native paint loses the illustration. A reduced viewBox-only image
measures 100×150 instead of Chromium's 220×330; definite-height controls agree.

Final grid-area percentage resolution stored the used number but discarded its
definiteness when descendants queried the original cyclic percentage. Grid
positioning now retains the existing late-percentage flag and the next intrinsic
grid pass clears it before measuring. No additional layout-state field is added.

Two intrinsic-ratio shortcuts then reused old constraints during retained
resizing: an image's previous resolved percentage height and its previous
resolved max-width:100%. Traces show a 450px current containing height alongside
a stale 330px image height; later, a correct 300px ratio width is capped at the
old 220px percentage maximum. Re-resolve percentage heights from the current
query and reuse the shared border-box constraint helper to defer cyclic maxima.
That helper also recognizes calculated and substituted percentage maxima.
Replaced cyclic contributions retain their zero min-content rule and authored
floors. Non-replaced preferred-ratio minima and definite-bound clamp behavior
remain with their existing paths; this distinction protects grid ratio and
border-box/max-content cases.

**Verification:** five focused native document tests pass. The new test has
**54 retained states**, covering percentage/calc heights, percentage/calc/var
maxima, height changes and narrowing/restoring the containing grid column.
Chromium headless shell independently confirms all 54 states. Integer-cast lint
and all three repository diff checks pass. Same-source saved-page PNGs restore
the Cape illustration; the live capture still lacks hero content and remains
high severity. The pre-resource inline geometry probe is not a final-paint
measurement: it runs before the external SVG's final admission.

One configured layout sweep reports **5,626 passed, 295 partial, 12 skipped and
three sizing regressions**. Correcting the scope of the replaced minimum and
definite-bound paths restores **all 776 sizing baseline cases** on a sizing-only
rerun, including grid-aspect-ratio-016/025 and border-box-and-max-content-002.
The other categories passed the first sweep. The full sweep was not repeated;
its input-event section used the existing native runner. No full native Radiant
or Lambda baseline was rebuilt or claimed for this repair.

<a id="ri-107"></a>

### RI-107 — Reuse initially parsed stylesheets when delivering prefetched link completion

**Status:** FIXED, 2026-11-17. **Lambda blocker:** resolved for initial
stylesheet completion alongside Input **LI-073**. Ownership remains under
**D9.1.1v4 / D9.1.3v2**; the host observer continues using its existing pinned
event queue and **D6.3** lifetime contract. Capture clocks are unchanged.

The initial loader parses authored stylesheet links synchronously after a
byte-only prefetch. Once Input correctly associates later discovery with the
link, ordinary CSS processing must deliver completion without appending the
same stylesheet again or discarding its CSSOM changes. The CSS resource handler
recognizes an existing non-import sheet by owner and resolved origin URL and
uses its existing completion guard to publish the load event without reparsing.
New and empty sheets continue through the existing parsing path.

The four initial-link HTTP tests cover print-to-all activation, already-active
CSS, empty CSS and HTTP failure; all fail before the handoff fix and pass on the
final host. Successful sheets remain present exactly once, one HTTP transfer is
made, and both inline handlers and listeners run before window.load. The ten
existing CSS resource tests also pass on the first candidate; six native Input
resource-manager tests pass on the final archive. Chromium confirms the four
fixture event/rendering outcomes. Failed-sheet CSSOM representation remains a
small known difference. No broad baseline was rerun.

<a id="ri-106"></a>

### RI-106 — Lifted positioned children overpaint exported ancestor outlines

**Status:** FIXED, 2026-11-17. **Task blocker:** resolved for Lambda's
`PdfFileOutput.LambdaPdfOffsetsAndImageStreamsAreValidInBothTiers`.
Radiant owns the shared paint walk under D9.1.1v4 / D9.1.3v2; the Lambda
PDF adapter consumes it through D9.5.6v2. No formal ruling changes.

**Reproduction and root cause:** exporting Lambda's
`test/html/pdf_lambda_phase6.html` moved the blue `#outlineorder` child
after its parent's red outline in both interpreter and JIT execution.
The stacking collector correctly lifted zero-level positioned descendants,
but the shared vector walk finished the parent's outline before visiting
those lifted descendants. The PDF stream differed from its accepted golden
only in this rectangle/outline ordering.

**Repair:** the shared positioned walk defers an outlined ancestor's final
paint until its contiguous lifted descendants finish, then replays only the
outline under the original box and ancestry scopes. Content and semantic
visits are not repeated. Stacking collection, z-index ownership and raster
dispatch retain their existing behavior.

**Verification:** the unchanged PDF goldens pass in both tiers. All 122 native
dynamic-layout tests pass, including a new order/coordinate/visit-count/hit
regression and the existing zero-layer ownership and clipping cases. Logs:
`../../lambda/temp/failed_tests_fix/pdf_walker.log` and
`../../lambda/temp/failed_tests_fix/native_dynamic.log`.
The full Lambda baseline passes 7,571/7,571. The full Radiant baseline exits
0 with 8,169 passed, 299 partial and 20 skipped, with no failures. Independent
Poppler rendering passes all 16 phase-6 pixel checks, including the red outline
over its blue descendant. Aggregate logs are `lambda_baseline.log` and
`radiant_baseline.log` in the same evidence directory. These runs use Lambda
`c5c8d95cf` and Radiant `f6a2cef` with this repair and its regression uncommitted.

<a id="ri-105"></a>

### RI-105 — CSS Shapes WPT contour and inline wrapping failures

**Status:** FIXED, 2026-11-16. **Task blocker:** resolved for
`make layout suite=wpt-css-shapes`. Native ownership is D9.1.1v4 /
D9.1.3v2; copied image rows retain D5.5.1 pass-local ownership. No formal
ruling changes.

**Reproduction and repair:** the current-source control passed 194/294.
Atomic inlines incorrectly entered block-level float avoidance; taller pending
boxes contaminated the preceding line band; explicitly zero struts became
16px; SVG resources lacked alpha rasterization; and line searches skipped
interior contour openings. Fixing these causes reached 242/294. The user then
requested saving those 242 passing names in the suite's `baseline.txt` and
continuing the remaining 52.

The continuation maps physical contours and alpha rows into vertical/sideways
flow axes, uses the parent's block-axis atomic extent, preserves all four
elliptical inset corners, rasterizes generated gradients through shared image
resolution and sampling, releases stale provisional line advances, and restores
the font box of float-only inline wrappers beside visible atomic content.
Input's independent gradient admission repair is
[LI-066](Lib_Input_Issue_Ledger.md#li-066).
Float refresh now uses the same local query origin as prescan; the negative
origin regression covers ordinary rectangular floats as well. Font-box repair
requires out-of-flow descendants, preserving empty WBR span positions.

**Reference evidence and approval:** 47 of the remaining cases were repaired
in the engine. The user separately approved replacing only gradient references
010 and 016 after independent Chromium captures proved stale float positions
and overlapping text in the saved captures. The user also approved three
`.standards.json` controls for border/padding/content-box-003; their ordinary
captures remain intact. Chromium's `ExcludedSegment` clamps a 20px query to the
10px reference height. Analytic full-band geometry and equivalent inset-outline
browser captures establish the standards expectations. Each control records its
stylesheet, CSS specification, reason, source hash and browser. Comparisons and
tolerances are unchanged.

**Verification:** the exact Shapes command passes 294/294 and the original
242 baseline entries pass. Geometry passes 16/16; integration/query tests pass
30/30. Shared unscored-gate tests pass 6/6; reference-control tests and layout
integer-cast lint pass. The 18 configured Lambda layout suites pass 5,631 cases
plus 293 accepted partials, with 12 skips and no failures. The separate static
corpus has 3,850 passes, six skips and only RI-092's existing flex mismatch.
The owner aggregate retains RI-091's duplicate-provider test link failure;
scripted layout passes 630/630.

Evidence: `../Lambda-opus/temp/wpt-css-shapes/completed-suite-final.log`,
`completed-layout-baseline-results.txt`, `completed-static-baseline.log`,
`completed-owner-baseline.log`, `native-integration-final.log`,
`native-geometry-final.log`, `reference-repair-provenance.json`,
`reference-repair-proof.json`, `margin-control-proof.json`, the independent
capture directories and `completion-provenance.json`. See the
[implementation qualification](impl/Radiant_Impl_CSS_Shapes.md#wpt-follow-up-2026-11-16).

<a id="ri-104"></a>

### RI-104 — Intrinsic margins use the measuring parent's variable and font scope

**Status:** FIXED, 2026-11-17. Layout ownership follows **D9.1.1v4 /
D9.1.3v2** and query-local state **D5.5.1**. Capture time is unchanged
(**D10.1.7**); no formal ruling changes.

Astra269's hero grid collapses to height zero even in script-free replay.
Its left item has a NaN height although its visible heading and children have
finite boxes. CSS reduction retains just grid display and Tailwind-style
sibling margins using calc(1rem * calc(1 - var(--reverse))). Intrinsic vertical
margin measurement calls the length resolver with its measuring parent still
selected. The child's own variable is consequently missing; NaN poisons the
height contribution. An independent finite reduction measures a row at70px
instead of80px because the same path reads the parent's10px font rather than
the child's20px font.

The margin queries now select the owning child and its font through existing
view/font scopes; vertical percentage math uses the supplied containing inline
basis. Horizontal intrinsic margin queries use the same owner context. The
existing LayoutViewScope accepts an optional target, also reused by padding and
IntrinsicMeasureScope, preserving automatic restoration without duplicate owner
selection sequences. No NaN clamp, site condition or CSS override is added.

Five focused native cases pass, including the two new live variable/font/
percentage and shrink-to-fit cases plus three adjacent margin regressions.
Chromium independently confirms all seven new geometry states. The original
minimal grid changes from0px/NaN to finite79.88px, and Astra's live heading,
copy, actions and payment illustration reappear. Integer-cast lint passes.
No broad baseline is rerun: the user cleared the active goal during the build,
and this already-built repair is wrapped up with focused qualification only.

Evidence: sibling Lambda temp/online-visual-round56/ holds the original saved
page, valid reduced DOM/CSS, before/after geometry logs, browser controls,
qualified release pin and selected capture/report. The first diagnostic DOM
reducer used Unicode splitlines offsets inconsistent with HTMLParser and was
corrected to LF-only offsets before the retained valid reduction. That temporary
artifact was never used as a production fix or final control.

<a id="ri-092"></a>

### RI-092 — Intrinsic column height omits definite child flex bases

**Status:** FIXED, 2026-11-17. The milestone release Radiant baseline passes:
8,169 passed, zero unexpected failures, 299 partial and 20 skipped (8,488 total). Native layout ownership follows **D9.1.1v4 /
D9.1.3v2**; **D10.1.7** frame selection is unchanged. No design ruling changes.

The existing flex_grow_root_minimized fixture and a standalone reduction shrink
a nested automatic column to its 100px minimum, with children 66.7px/33.3px.
Its empty first child has flex-basis:200px; the second has height:100px.
Chromium uses 300px for both columns and 200px/100px for the children.
The previously unconfirmed observation affected the CSS Shapes aggregate even
though this fixture contains no Shapes or floats. Original evidence remains in
Lambda temp/css-shapes/{flex-original-host.log,flex-current-host.log,
layout-current-host-baseline.log}.

Both generic height traversal and specialized flex-item intrinsic measurement
used an empty child's zero content height instead of its definite basis. They
now share one promoted contribution helper, consuming computed flex-basis and
retaining automatic/explicit minima, maxima, scroll behavior and box sizing.
Indefinite percentage bases remain content based. Tracing two additional states
showed that final row refinement discarded a non-growing column-item basis,
and initialization treated an allocated zero basis as unassigned. Both size
handoffs now respect the existing parent-basis constraint protocol. The flag
that records actual flex redistribution keeps its original meaning.

All thirteen new states match independently sampled Chromium geometry: natural,
explicit/min/max height, restored and unconstrained cases; content minima and
min-height:0; non-growing and scroll containers; 0px versus 0%; content-box
padding/borders. All 36 focused grid/flex/absolute-height native tests pass,
and layout integer lint passes. The unchanged original fixture now reports
100% element and text agreement. No references or tolerances are modified.
Temporary trace logging and an ineffective min-height flag hypothesis were
removed before final build. Evidence: Lambda temp/online-visual-round51;
release pin a0f10c812c027d0ba021726df6d1491c1bcd5a08107fe61d2afad88cabd616e4.

<a id="ri-103"></a>

### RI-103 — Final flex content discards its grid-assigned cross size

**Status:** FIXED, 2026-11-17. **Lambda blocker:** no.
Native ownership follows **D9.1.1v4 / D9.1.3v2**; capture time stays at the
selected virtual frame under **D10.1.7**. No design ruling changes.

Bonsai reviews uses a two-column grid whose cards are padded, start-aligned
flex containers with aspect-ratio:16/9. Each absolute portrait has height:100%.
A 396px-wide reduced card finishes at 222.75px, but its child receives only
66px (18px text plus 48px padding). Stretch alignment masks the defect.
The final-content cross-axis recalculation checked only authored height,
shrinking the grid-assigned card to its text. Positioned layout then consumed
that provisional box; subsequent grid alignment restored only the card.

The recalculation now uses the existing flex_container_axis_is_fixed helper,
promoted through layout.hpp rather than copied. The flex algorithm and final
content therefore agree about parent-owned sizes. No site condition, new sizing
policy, repeated child layout or image-specific adjustment is introduced.

The new native regression fails before repair for start, center, end and restored
start, with stretch as a passing control. All 35 focused grid/flex/absolute-height
tests pass after repair; layout integer lint passes. The local raster reproducer
fills both cards. The unchanged saved hydrated Bonsai source, served over HTTP,
improves from 12.7% to 0.7% image error and restores both full-height portraits.
Its live capture independently omits the main content and remains high severity
by explicit review despite falling below the coarse threshold. No visual clearance
or root cause for that live omission is claimed. No broad baseline is repeated
after the round47 milestone. Evidence: Lambda temp/online-visual-round49;
release pin f3c85566115f811ea97ff7d74981978da855adb690aece3ec4c7ccad9a015247.

<a id="ri-098"></a>

### RI-098 — Rounded inset glyph and hit clips use unnormalized radii

**Status:** FIXED for admitted uniform circular inset radii, 2026-11-17.
**Lambda blocker:** no. Native ownership D9.1.1v4 / D9.1.3v2; scratch-owned
computed shape geometry follows D5.5.1. No design ruling changes.

AndOpen's saved hydrated navigation buttons use `clip-path:inset(0% -1% 0% 0%
round 500px)`. Their text geometry, opacity and color are visible, but glyphs
are absent. A 180x40 flex button with white MMMM reproduces this: round20px
shows 336 white samples; round500px shows none, while a vector green child
survives. The vector path clamps the radius, but `css_basic_shape_resolve`
published raw500px values to CPU glyph scanlines and native point containment.

The producer now creates a Corner with `render_path_uniform_corner` and applies
existing `constrain_corner_radii` before publishing shape radii. Vector painting,
glyph clipping and hits therefore consume the same used geometry. No new
normalizer, site condition, hidden content or capture-clock adjustment.

Two exact PNG equivalence controls fail before repair and pass afterward,
including a translated/scaled label. Eighteen existing clipping controls also
pass. Three native tests pass; the new test covers large/small/transform/restored
radius hit states. Chromium independently confirms both equivalences; integer
lint passes. This fixes labels in the unchanged saved hydrated AndOpen source.
Its live canonical capture still contains independent initial hidden content
and is not cleared or attributed to this clipping defect. No broad baseline
was repeated: the substantial round41 milestone already ran that gate.
Evidence: Lambda `temp/online-visual-round42/`; qualified release pin
`f3858f73a3402185fefe9ab0cd5375b4330c1e8b09e06082666945946e59f436`.

<a id="ri-097"></a>

### RI-097 — Nested zero-level foreground paints below lifted absolute background

**Status:** FIXED, 2026-11-16.
**Lambda blocker:** no. Native ownership D9.1.1v4 / D9.1.3v2; borrowed view
paths follow D5.5.1. No design ruling changes.

In a relative container, place an absolute red background under an ordinary
wrapper and a later relative green foreground under that same wrapper. Native
paints red over green; Chromium paints green over red. The absolute layer was
painted by its containing block after the wrapper's local relative layer.
PostHog's entire main window was hidden by that same wallpaper ordering.

The candidate collects zero-level descendants at their real block stacking
context, preserving ancestry scopes, tree order and reverse hits. Normal local
collections defer to that owner; custom-layout and existing inline-effect paths
retain their separate traversal. New controls distinguish explicit zero from
auto for positive descendants and detect duplicated opacity painting. Four of
seven controls failed before; all seven plus fifteen related PNG tests now pass.
Four native tests and seven Chromium colors pass. The former internal local-list
assertion now checks real-context paint ownership while retaining the relative
positioning containing block and adding an actual local hit witness.

The unchanged saved PostHog source restores its full main window, heading,
signup panel and tabs. Saved Primer, Opus and Armadillo are not assigned this
cause. The standard Lambda Radiant baseline ran 8,488 cases: 8,167 passed,
299 partial, 20 skipped and two CSS 3D pixel tests failed. The broader collection
exposed a missing context boundary: computed `preserve-3d` creates a context even
without a transform list. Reusing `transform_establishes_containing_block` keeps
those planes in their depth compositor and includes participating hidden backfaces.
Both failed tests pass with the final candidate; they also pass with the prior
qualified host, confirming the regression boundary. No expected pixels or
reference baselines changed. All22 focused PNG and four native tests still pass.
The full baseline was not repeated after that correction.

Final pin `35dc8bf8b95d952a6732c5fd293d89804ee6d5876108a29270b997d38b594a7f`.
Evidence: Lambda `temp/online-visual-round41/`, including initial baseline summary,
focused post-fix results, immutable pre-fix hosts and source state. No capture
clock, admission, watchdog or feature-budget change (D10.1.7). Painting order:
https://www.w3.org/TR/CSS22/zindex.html#painting-order .


<a id="ri-096"></a>

### RI-096 — Static flex/grid items ignore z-index

**Status:** FIXED, 2026-11-16. **Lambda blocker:** no; PostHog's primary
navigation disappeared behind its wallpaper. Native ownership D9.1.1v4 /
D9.1.3v2; view/style ownership D5.5.1. No formal ruling changes.

The positive/negative layer predicates and context predicate restricted stack
levels to non-static positioned boxes. CSS Flexbox §4.3 and Grid item painting
also apply z-index to static items. All four initial positive/negative PNG
controls failed; the saved PostHog header is a static flex item with z-index50.

The shared predicates now admit actual flex/grid items, including explicit zero
in the positioned paint phase. Item-style storage alone is insufficient:
`flex:none` allocates such storage on ordinary blocks. The existing membership
helper now checks the formatting parent across display:contents and excludes
out-of-flow boxes. This preserves ordinary static blocks' ignored stack levels.
The first intermediate failed that safeguard and was held. No CSS position is
rewritten. Raster/export and native hit testing reuse these predicates.

Seven PNG controls and eight related opacity/positioned regressions pass;
Chromium independently confirms all seven expected colors. Three native tests
pass, including hit order through flex/grid/block/flex restyles and unchanged
static positioning. The two previous native hit setups now share one fixture
helper. Integer lint passes. The saved PostHog header reappears; its missing main
window remains a separate unisolated defect. Primer's saved hero is also still
absent and is not attributed to this repair. Evidence in Lambda
`temp/online-visual-round40/`; no broad baseline or capture-clock change.
Reference: https://www.w3.org/TR/css-flexbox-1/#painting .

<a id="ri-095"></a>

### RI-095 — Flex-assigned grid height is replaced by intrinsic row height

**Status:** FIXED, 2026-11-16. **Lambda blocker:** no; Draftbit's hero copy
was under its header and its phone preview was absent. Native ownership
D9.1.1v4 / D9.1.3v2; pass-local size ownership D5.5.1.

A 400px column flex container with a 40px footer assigns 360px to its growing
grid. The native grid instead ended at 200px, with the centered 80px child at
60px instead of 140px and its 50% child at 100px instead of 180px. The flex
content dispatch established `used_size_owner` for ordinary flow only; grid
initialization read an ancestor context and treated its own height as auto.

Assigned grids now use `layout_setup_assigned_block_context`, guarded by the
parent's definite block axis or actual column redistribution and the existing
`flex_height_is_parent_constrained` predicate. Flex-grow alone does not make
an auto-height provisional allocation definite. The existing parent-flex local
is reused. No authored height is changed.

`DynamicLayoutTest.FlexAssignedGridHeightRetainsTracksAndPercentageChildren`
passes five live states: 400px, 600px, auto, min-height400px and restoration;
Chromium independently confirms all expected geometry. Three neighboring grid,
ratio and deferred-gap tests pass. Before, 15 geometry assertions failed in the
fixed-height states. The first intermediate fix broke the auto control and was
held; the refined fix preserves it. The unchanged saved Draftbit source now
shows centered hero content and its phone preview. RI-041 retains the separate
unsupported composite-mask tint. Evidence: Lambda `temp/online-visual-round39/`.

<a id="ri-094"></a>

### RI-094 — Individual translate loses affine percentage calculations

**Status:** FIXED for affine length/percentage calculations, 2026-11-16.
**Lambda blocker:** no. Native ownership D9.1.1v4 / D9.1.3v2. No new allocation
or lifetime model is introduced; D5.5.1 remains unchanged.

Draftbit's fixed 980px navigation uses nested `calc()` to translate -50%.
After LI-066 admits that syntax, the transform decoder still returned zero for
calculations containing percentages; only transform lists retained deferred
expressions. `resolve_transform_translate_arg` now uses the existing typed math
evaluator with percentage preservation to retain affine pixel and percentage
coefficients against the eventual reference box. Transform-list resolution
retains its expression only when those coefficients could not be resolved, so
a resolved calculation is not applied twice. Reference-dependent non-affine
expressions keep the existing transform-list fallback; broader individual math
support remains partial.

Seven exact PNG controls pass: nested arithmetic, font-relative mixed units,
variable substitution with script-driven resizing, composition, equivalent
transform-list single application, nonlinear list fallback and invalid third-axis
rejection. Four existing transform animation/snapshot tests and two input
validation tests pass; Chromium confirms all seven fixture positions. Integer
lint passes. Saved Draftbit header x changes from 600px to 110px, restoring all
navigation/login/signup actions. Evidence: Lambda `temp/online-visual-round39/`.
The initial direct host link used stale runtime archives and aborted during
script compilation; refreshing its dependency chain resolved that build issue.
No vendor, capture clock, admission or watchdog change was made (D10.1.7).

<a id="ri-093"></a>

### RI-093 — Assigned grid/flex float contexts misplaced or omitted exclusions

**Status:** FIXED, 2026-11-16. **Task blocker:** blocked delivery of the CSS
Shapes visual page until corrected. Native layout ownership is D9.1.1v4 /
D9.1.3v2; pass-local contours retain D5.5.1 lifetime. No design ruling changes.

**Reproduction and cause:** render `test/html/css_shapes_visual.html` at
1120 by 1500 pixels. Shaped floats overlapped text inside offset grid cards.
In the reduced grid case, first text x was 98.2804 instead of Chromium's 153.
`block_context_offset_to_bfc` already returned BFC-border-relative positions;
`block_context_update_float_geometry` subtracted the assigned context's
parent-space origin again. Rectangular floats shared the incorrect frame.
A corresponding horizontal flex item produced x=63 rather than 153 because
its ordinary flow content inherited a float context instead of initializing
the item's independent formatting context.

**Repair and verification:** float margin boxes now retain the BFC border
frame used by exclusion queries. Ordinary horizontal flex items initialize
the existing assigned block context, as vertical items already did. Five
native regressions cover offset ordinary flow, grid and flex plus rectangular
controls. The test's text-position helper uses the existing canonical
block-document conversion; Chromium captures independently confirm the grid
and flex expectations. All 21 integration cases and 14 geometry cases pass.
All 51 runnable native suites pass 1,063 tests with one skip; RI-091's two
failed test links and RI-092's existing static layout mismatch remain visible.

Evidence under `temp/css-shapes/`: `visual-before-offset-fix.png`, `visual.png`,
`offset-before.log`, `offset-diag.log`, `offset-after.log`,
`flex-offset-final.log`, `browser-offset/offset-grid.json`,
`browser-offset-flex/offset-flex.json`, `native-suites/results.json` and
`layout-final-visual-baseline.log`. No reference or tolerance was changed.

### RI-092 — Earlier independent repair and qualification

**Status:** FIXED, 2026-11-17. **Aggregate blocker:** resolved. Native layout
ownership is D9.1.1v4 / D9.1.3v2; no formal ruling changes.

**Reproduction and cause:** `flex_grow_root_minimized.html` produced root and
inner heights of 100px instead of 300px, with children 66.7/33.3px instead of
200/100px. An independent headless Chromium capture confirms the original
reference. The fixture has no floats or Shapes declarations. The manual
intrinsic-height accumulation in `calculate_item_intrinsic_sizes` read child
CSS heights/content but omitted a definite `flex-basis`. The enclosing auto
column therefore received an undersized 100px contribution and shrank the
200px-basis child together with the 100px-height child.

**Repair:** horizontal-writing column contributions with a definite,
non-percentage flex basis reuse `calculate_hypothetical_main_size` in an
indefinite column query context. This shares the existing basis and min/max
constraint logic. Indefinite percentages retain their content fallback. The
native regression covers both initial layout and retained relayout; existing
max-height shrinking, zero-basis overflow, aspect-ratio and minimum-height
controls remain green. The focused fixture now matches 100% of elements.

**Verification:** eight focused flex controls pass. Owner native tests pass
1,154 cases across 53 executables, with one skip. The complete owner baseline
exits zero: static 3,851/3,851 plus six skips; scripted 630/630. The 18 configured
Lambda layout suites pass 5,631 cases and 293 accepted partials, with 12 skips
and no failures. Shapes remains 294/294 and the layout integer-cast lint passes.
No fixture, reference, baseline or tolerance changed.

Evidence under `../Lambda-opus/temp/radiant-aggregate-repairs/`:
`flex-before.log`, `browser/flex_grow_root_minimized.json`,
`flex-regression-before.log`, `flex-regression-after.log`, `flex-after.log`,
`owner-baseline.log`, `layout-baseline-results.txt`, `shapes-regression.log`
and `provenance.json`. CSS authority: [Flexbox §9.9](https://www.w3.org/TR/css-flexbox-1/#intrinsic-sizes).

<a id="ri-091"></a>

### RI-091 — Isolated native test links import conflicting full-host providers

**Status:** FIXED, 2026-11-17. **Aggregate blocker:** resolved for the owner
native baseline and the three original Lambda consumer test links. Native
ownership is D9.1.1v4 / D9.1.3v2; consumer build ownership is D9.1.5. No formal
ruling or dependency direction changes.

**Reproduction and cause:** custom-layout reported nine duplicate definitions;
retained display-list reported twelve. The linker `-why_load` trace establishes
two missing isolated services. `stacking_order.cpp` referenced
`find_containing_block`, admitting `layout_positioned.cpp` and then the full
layout host. `view_geometry.cpp` referenced
`css_prop_compute_declared_overflow`, admitting CSSOM/state-store services and
then the renderer and layout host. Their real implementations collided with
the isolated tests' existing layout, form, path and picture providers.

**Repair:** the shared `test_unlinked_layout_stubs.cpp` declares exactly these
two absent host services through its existing fail-fast logger/abort helper.
Unexpected service use remains observable; no production implementation,
assertion, library selection or linker duplicate-symbol policy was weakened.
The trace now shows zero collisions and no full layout, positioned-layout,
state-store or ThorVG renderer providers entering either isolated link.

The earlier StateStore repair remains in place: overflow and view-node-state
queries are fail-fast services in its own host. StateStore passes 39 tests,
custom layout 13 and retained display-list 66 in the Lambda consumer binaries.
The exact consumer build command succeeds:

```sh
make -C build/premake config=debug_native -j8 test_state_store_gtest test_layout_custom_gtest test_retained_display_list_gtest
```

**Verification:** the full owner `make -j8 run-radiant-baseline`, using the
current Lambda-opus host, exits zero. All 53 native executables pass 1,154 tests
with one skip; the static corpus passes 3,851 cases with six skips and scripted
layout passes 630/630. RI-092 supplies the separate flex layout repair.

Evidence under `../Lambda-opus/temp/radiant-aggregate-repairs/`:
`link-before.log`, `link-why-load.log`, `link-boundary-final.log`,
`link-proof.json`, `custom-final.log`, `retained-final.log`,
`consumer-links.log`, the three `*-consumer.log` files and `owner-baseline.log`.
The original investigation remains in `temp/css-shapes/link-comparison/` and
Lambda-opus `temp/js-parity-work/targeted-make-fixed.log`; the earlier StateStore
before/after proof is under Lambda-opus `temp/edit-viewer/ri090/`.

<a id="ri-090"></a>

### RI-090 — Home and End stop at text fragments beside inline atoms

**Status:** FIXED, 2026-11-15. The user explicitly authorized the owner-side
repairs from the Lambda editing task.

**Repair and verification:** Home/End now resolves the full laid-out line
using block-local line numbers, including superscripts, wrapped inline spans,
retained images and inline blocks. Ordinary spans use descendant fragments
rather than their opening cursor. Atomic formatting contexts use the parent's
line identity and retain canonical parent/child boundaries outside the atom.
StateStore extends the canonical selection while preserving its anchor; the
Lambda controller no longer repeats that extension through a text projection
(D10.2.1).

The original footnote replay fails 1/3 assertions before repair and passes
afterward. New native image/inline-block controls also fail before their repair
and pass afterward; all 39 StateStore tests pass. Evidence under
`Lambda-opus/temp/edit-viewer/ri090/`: `before.log`, `box-before.log`,
`native-final.log` and `focused.log`. The complete Lambda editor selection
passes 209/209 UI fixtures, including Shift+Home/End around retained references.
Three existing fixtures assumed fragment-local navigation; their setup now
uses character movement to reach the same intended edit positions, retaining
their formatting/delete/undo assertions. No layout reference or tolerance was
changed. Final aggregate gates are recorded in the Lambda implementation plan.

**Original report:**

**Original status:** OPEN, reproduced 2026-11-15 against Lambda `e55450ec8` plus
editor work and Radiant `4ec1e8c` plus the owner's worktree. **Lambda task
blocker:** yes for reliable keyboard navigation at retained inline content in
`lambda edit`. Radiant ownership is D9.1.1v4 / D9.1.3v2; editor selection and
document editing are D10.2.1 / D9.3.4.

From the Lambda checkout run:

```
./lambda.exe view temp/edit-viewer/caret-edge-repro.html --headless --event-file temp/edit-viewer/caret-edge-repro.json
```

The plain contenteditable page has a non-editable inline `REF` before the
first text run of one paragraph and after the last text run of another. After
clicking `begins.`, Home then typing `Start ` yields `REFStart  begins.`
instead of `Start REF begins.`. After clicking `Ends`, End then typing
` tail` yields `Ends  tailREF` instead of `Ends REF tail`. Both assertions
fail independently of `lambda.editor`; see `caret-edge-repro.log` in the same
directory. The editor regression `test/ui/edit/edit_md_footnote_edges.json`
reproduces the same placement around retained Markdown footnote references.

`state_store_caret_move_to_boundary` in `radiant/state_store.cpp` handles
line start/end using only the current `ViewText`'s `TextRect` offsets. This
explains why an adjacent inline element and other text views on the same
visual line are not reached. Resolve the boundary across the laid-out visual
line while respecting non-editable content and wrapped lines; retain the
normal selection-extension semantics. No Radiant implementation or reference
was changed by this report.


<a id="ri-089"></a>

### RI-089 — Min-content minimum widens floated intrinsic contribution boxes

**Status:** FIXED, 2026-11-14. **Lambda blocker:** cleared. Native ownership
is D9.1.1v4 / D9.1.3v2; measured layout state retains D5.5.1 ownership. The user
explicitly authorized the Radiant source update from the Lambda task.

The unchanged `keyword-sizes-for-intrinsic-contributions` case had two floated
`.test.min-width` boxes at 90px/150px instead of 70px. Their children used
`width:0;min-width:min-content` and correctly laid out at 60px, but contributed
the larger natural max-content width to the containing float. A definite
preferred width skipped the early shortcut when intrinsic limits required
content measurement and was never restored after that measurement.

Intrinsic measurement now retains the definite border-box preferred width
separately from the natural min/max-content bounds. Shared minimum and maximum
clamps constrain its contribution, with a minimum taking precedence over a
smaller maximum. `width:auto` retains the RI-074 repair. `fit-content` limits
keep negotiating available space through their existing contribution path;
the first candidate's fit-content regressions were repaired before delivery.

The new native definite-contribution regression fails before and passes after
repair. It checks nine min/max combinations in three containing widths with
repeated recascade/layout; the additional fit-content control checks both
minimum and maximum limits. Independent Chromium agrees with all nine width
combinations and all four fit-content box measurements. The complete owner
dynamic suite passes **110/110**; layout lint checks **89 casts, zero unmarked**.

The refreshed debug Lambda host passes sizing **776/776**. The standard
`make test-radiant-baseline` exits 0 with **8,170 passed / zero failed /
297 partial / 20 skipped**. The previously repaired Lambda-owned paint and
cascade integration executables remain green at **250/250** and **186/186**.
No external fixture, reference, tolerance or scored baseline was changed.

Source identity: Lambda `09a40dfe2`, Radiant `4ec1e8c` plus this implementation
and test, Input `3e14a05`. Original and final integration evidence is in
`../../Lambda-opus/temp/radiant-three-failures/`, especially
`ri089-radiant-baseline.log`, `sizing-final-results.json`,
`browser-control-final.json` and `ri089-verification-identity.json`.
Owner evidence is in `../temp/ri-089-before.log`,
`../temp/ri-089-dynamic-final.log` and `../temp/ri-089-lint-final.log`.


<a id="ri-087"></a>

### RI-087 — Propagated body overflow clips its own zero-height box

**Status:** FIXED for the selected clipping/coordinate milestone, 2026-11-14.
**Lambda blocker:** cleared for the saved Mymind main-content omission.
Native ownership D9.1.1v4 / D9.1.3v2; declaration reads follow D5.5.1.

A zero-height body with overflow scroll/hidden/auto clips its absolutely
positioned content to an empty box. Its overflow instead belongs to the
viewport when the HTML root has visible overflow on both axes. The source
body then uses visible overflow locally ([CSS Overflow §3.3](https://www.w3.org/TR/css-overflow-3/#overflow-propagation)).
Root/body containment disables this transfer; ordinary boxes keep their clips.
The existing iframe viewport selector also missed those containment guards.

One shared `view_viewport_overflow_source` selects the root or first displayed
body with those guards. Viewport overflow resolution reuses it. Shared clip
geometry/axes, raster scroll scopes and scrollbar paint, native hit testing
and coordinate scroll scopes skip the source body's local overflow. Declared
CSS remains intact. No page condition, stylesheet modification, changed frame,
threshold or budget is used; no public structure layout changes.

The new native propagation case fails before repair; five focused native
cases and the existing clip/hit regression pass afterward. Exact PNG controls
change from5/8 to8/8, with all eight independently confirmed by Chromium.
Cases include scroll/hidden/auto transfer, explicit root overflow, root style
containment, body layout/paint containment and ordinary-box clipping. The
unchanged saved Mymind source now displays its heading, copy and actions.
RI-086 alone repaired root geometry but left the pixel control blank; that
intermediate host was held. RI-088 remains an independent unresolved query error.
Evidence: Lambda `temp/online-visual-round38/`, propagation native/PNG before
and final logs, browser-body-overflow.json and source freezes. Integer lint
passes. The later live canonical capture remains blank and is not cleared;
its initialization state is a separate unresolved cause. Full baselines and
catalog capture are deferred under focused checking. The user cleared the goal
while the completed repair was being wrapped up.

<a id="ri-086"></a>

### RI-086 — Empty-child root collapse erases resolved minimum height

**Status:** FIXED for the selected final root calculation, 2026-11-14.
**Lambda blocker:** cleared for root geometry; RI-087 separately fixes painting.
Native ownership D9.1.1v4 / D9.1.3v2; scalar sizing follows D5.5.1.

Mymind's min-height100% resolves to800px, but the late all-self-collapsing-child
shortcut replaces its already-constrained root height with0. A border-box
control similarly falls from800 to14px. The shortcut now reuses the existing
`layout_apply_min_max_axis` resolver and retains the larger content overflow
extent. It does not replace collapsed-margin handling or make auto height definite.

The native regression fails five height assertions before repair. Six live
states then agree with independent Chromium geometry: percentage and fixed
minima, conflicting calculated minimum/maximum, zero minimum, border-box
padding/border and restoration. The empty-body margin control retains20px
root height and10px body Y. Both source styles and capture clocks are unchanged.
Evidence: Lambda `temp/online-visual-round38/`, native-before/native-focused,
browser-root-min.json and the minimum-only held host/source pin. The final
combined qualification includes the additional RI-087 viewport controls.


<a id="ri-085"></a>

### RI-085 — Wide SVG Gaussian kernels exhaust the work budget

**Status:** FIXED for the existing wide Gaussian path, 2026-11-14.
**Lambda task blocker:** cleared for Overflow's missing colored SVG background;
other page residuals are reported separately. Native ownership D9.1.1v4 /
D9.1.3v2; temporary window storage and work admission follow D5.5.1.

Overflow's two single-node Gaussian graphs use standard deviations80 and130.
Their5285×3322 and6229×2896 raster grids exhaust the unchanged805,306,368-unit
work limit while convolving each varying pixel over every kernel tap. RI-082's
exact flat-run reuse cannot remove that radius-dependent work on gradients.
Failure provenance now records the node, grid, density and used/allowed work.

For physical sigma>=2, the existing SVG path now uses the three sliding box
passes permitted by [Filter Effects §9.14](https://www.w3.org/TR/filter-effects-1/#feGaussianBlurElement).
Odd windows remain centered; the first two even windows balance their half-pixel
offsets. Virtual line halos extend the original none/duplicate/wrap input once
and survive all three passes. X and Y deviations remain independent. Smaller
kernels and the public exact Gaussian reference routine retain their previous
math. Fully transparent input has an admitted scan and returns its existing
zero surface. No budget, capture frame, resource admission or page condition
changes; this is an algorithm change within an existing filter feature.

One shared strided box-window span now serves both SVG and the existing CSS
shadow passes. The original CSS edge renormalization and integer truncation
are preserved, with widened sums. Window scratch is line-sized and memory/work
admitted before evaluation. SVG accounting includes extension, initial windows,
sliding updates, output writes and line copies.

The new native regression fails before repair at sigma40 under a linear work
budget. After repair it covers18 combinations of deviation, edge and axis,
compares against the independent exact Gaussian within the specified3% envelope,
and retains low-budget rejection. Literal span values verify asymmetric windows,
strides, truncation, renormalization and untouched output. Nine initial focused
tests pass; the milestone extends this once to21 filter, shadow and budget tests,
all passing. Four independent Chromium controls (including even windows and
anisotropic blur) differ by at most3 channel values, with mean errors0.027%–0.098%.
Layout integer lint passes. No public structure layout changes.

The identical saved Overflow source restores its colored hero with no filter
failure and completes in1.65s versus the diagnostic failing2.64s run. These are
capture observations, not a broad performance benchmark. Evidence: Lambda
`temp/online-visual-round37/`, native-before/native-first/native-milestone logs,
Chromium control comparison, diagnostic and repaired source pins, and selected
capture audit. Full native/layout/JS/Input baselines and catalog recapture remain
deferred; original Chromium references and D10.1.7 clocks are retained.


<a id="ri-084"></a>

### RI-084 — Static effect contexts lose positioned descendants and empty-box backdrops

**Status:** FIXED for the selected block-context traversal, 2026-11-14.
**Lambda task blocker:** the reduced opacity defect is cleared; no online page
clearance is inferred. Native ownership D9.1.1v4 / D9.1.3v2; display-list resource
copies and temporary borrows follow D5.5.1.

An absolute child beneath a static opacity parent uses an external containing
block. The zero-level paint list followed only that block's `first_abs_child`
chain, painting the child after the opacity group closed. SVG export confirms
the colored rectangle outside the empty opacity group. Paint ownership now
chooses the nearest block stacking context before the containing block. The
original containing block still supplies coordinates. Nested contexts and
positive/negative layers retain their existing traversal. Inline effect-context
ownership is outside this repair's acceptance scope.

Raster/export/hit consumers share the sorted positioned visitor and reuse their
existing ancestry replay. Raster's former bridge ignored the walker's coordinates;
using its existing direct stacking path preserves the containing-block basis.
Overflow and scrolling below an external absolute/fixed containing block do not
apply, while containing-block and outer clips remain. Chromium independently
confirms that escape rule; the initial local clip expectation was corrected
before promotion, without weakening an existing tolerance.

A separate zero-height group had no backdrop admitted at begin, so its descendant
ink composited at full opacity. Completed ink bounds now admit the required
backdrop prefix before replay. Shared `dl_prepend_tail` reuses existing resource
copy/replacement helpers and also replaces PaintIR's duplicated staging shape.
No public structure layout, page-specific condition, capture frame, budget or
admission change is introduced.

The previous qualified binary passes two and fails six of the final eight PNG
controls. The final binary passes all eight, including transparent and half
opacity, overlapping children composited once, fixed/positive-z children, a local
relative containing block and ordinary overflow escape. Four native ownership,
paint-order, clip and hit tests pass; three focused display-list resource/mask
checks and all eight independent Chromium controls pass. Layout integer lint
has no violations. Two intermediate candidates were held after focused pixel
failures and were not promoted.

Identical saved Metronome and Highnote sources have unchanged pixels before/after;
their visible residuals are not assigned to this repair. No canonical captures,
scores or references were changed. Evidence: Lambda `temp/online-visual-round36/`,
source hash freeze, native-before/final, png-before-final/png-final, browser-opacity,
rdt-focused and lint-final logs. Full baselines and catalog capture are deferred.

<a id="ri-083"></a>

### RI-083 — Early flex intrinsic widths omit deferred scalar gaps

**Status:** FIXED for the selected declared-length path, 2026-11-14.
**Lambda task blocker:** cleared for Affinity button-row/nav sizing; independent
card animation/font differences remain. Native ownership D9.1.1v4 / D9.1.3v2;
bounded owner-aware declaration queries follow D5.5.1.

A wrapping row inside an automatic-width flex column sized two100px items at
200px, omitting its16px variable gap. Final layout resolves that gap and wraps
the second item, producing96px height instead of40px. A literal16px control
already uses216px and stays on one line. Affinity has the same root: its parent
uses344px (the sum of its buttons), omits16px, and stacks the actions.

`layout_flex_declared_length` accepted only a raw CSS length and silently
ignored `var()` and math functions. Its font-relative lengths also used the
caller's font. It now reuses `resolve_element_definite_length_value` to consume
the declaring element's substituted value and font. The existing definite-only
contract excludes cyclic percentages. One nested measurement call now retains
its available context rather than passing null. No duplicate variable/math
resolver, page override, public ABI or structure change is introduced.

The native regression fails20 assertions before the fix. Four focused native
cases pass afterward, including existing nested-wrap, ratio-height and margin
controls. Chromium confirms all five cold/live states:16px variable,24px variable,
calc(6px +10px),.8em at20px, and restoration. Layout integer lint has no violations.
The identical saved Affinity HTML/CSS now lays out the actions in one row and
stops clipping the navigation labels at the top. Additional media loaded from
cache during the later run is not attributed to this sizing fix. Font and card
motion differences remain; no broad page-fidelity completion is claimed.
Evidence: Lambda `temp/online-visual-round35/`, gap-control/fixed views,
native-before.log, native-focused.log, browser-gap.json and affinity-static/final
PNGs. Full baselines and catalog recapture are deferred under focused verification.


<a id="ri-082"></a>

### RI-082 — Gaussian work accounting charges skipped flat-run convolutions

**Status:** FIXED for accounting and transparent runs, 2026-11-14.
**Lambda task blocker:** Overflow's complete filter graph remains unresolved.
Native ownership D9.1.1v4 / D9.1.3v2; filter-run borrows and scratch D5.5.1.

The Gaussian kernel already shares an exact convolution across flat pixel
runs, but its caller precharged every output pixel times every kernel tap.
A constant 128×128 image with sigma1.5 fails under16 work units per pixel even
though the exact optimized kernel fits. The private budgeted kernel now charges
linear channel/run scans, output writes and kernel setup separately, then each
convolution immediately before executing it. The public API remains unchanged.
No cap, admission profile, image bounds, frame clock or approximation changes.

A related redundant-work path treated transparent image edges as boundaries
that always require convolution, even when zero extension makes every sample
zero. The existing flat-run optimization now joins transparent boundary runs
with zero extension and skips the exactly-zero kernel. Nonzero and duplicate/
wrap edge math retains its prior behavior. This also handles a transparent
128×128 image with sigma80, wider than the image, under the same linear budget.

Five focused native tests pass. The new test compares unlimited and budgeted
pixels byte for byte across none/duplicate/wrap modes, checks the wide zero
kernel, and confirms that noisy input still rejects the same budget with an
existing shared charge. Existing independent Gaussian pixels, channel/pitch
and edge-extension checks, shared graph accounting, and memory reclamation/
oversized-grid rejection pass. Evidence: Lambda `temp/online-visual-round34/`,
native-before.log, native-focused.log, native-final.log and source hash freezes.

The saved Overflow page still reports two generic filter memory/work failures
with both candidates, and its PNG is unchanged between them. The remaining
rejection phase has not been isolated; this repair does not clear the page.
Stored logs show filter failures on Overflow, Calendly, FramerAuth and Pitch,
which are candidates rather than claimed beneficiaries. No canonical capture
or comparison score is refreshed without demonstrated page improvement. Full
baselines and catalog captures are deferred under focused verification.


<a id="ri-081"></a>

### RI-081 — Wrapped flex height queries omit item cross margins

**Status:** FIXED for the selected intrinsic query, 2026-11-14.
**Lambda task blocker:** cleared for Betterway hero placement. Native ownership
D9.1.1v4 / D9.1.3v2; scalar queries retain geometry under D5.5.1.

Betterway's heading started 80px too high, partially hidden behind its header.
A local reduction has three 40px full-width flex items with 20px bottom margins
inside a wrapping row with 10px padding, nested in a stretched parent. The
container eventually reaches the correct 200px height, but its items retain a
30px upward offset. Their individual hypothetical border/outer cross sizes
are already correct (40/60px). The scalar wrapping-height query omitted the
margins, causing the parent to allocate only 140px and center the lines in too
little space before late content growth.

The wrapping query now includes each child's non-collapsing vertical margins.
It reuses the existing signed/percentage/auto resolver; the existing fieldset
and ordinary-flow fallback are folded into that resolver to avoid a third
copy. No alternate alignment rule, site CSS, source mutation, horizontal-size
change, budget or capture-clock adjustment is introduced.

The new native regression fails three positions before the fix and passes
afterward across live 20px, 10%, -10px and restored 20px margins. It also checks
read-only intrinsic queries and the unchanged 40px item height. Chromium
independently agrees with all four states. Four focused native cases pass,
including existing nested wrapping, flexed-width and RI-080 controls; layout
integer lint has zero violations. The identical saved Betterway HTML renders
its complete hero heading at Chromium's position. Evidence: Lambda
`temp/online-visual-round33/`, native-before.log, native-focused.log,
browser-control.json and betterway-current/final.png. No ABI/structure change;
full baselines and catalog recapture are deferred under the user's focused
verification instruction.


<a id="ri-080"></a>

### RI-080 — Nested ratio height queries consume parent free space and stale SVG heights

**Status:** FIXED for the selected preferred-size queries, 2026-11-14.
**Lambda task blocker:** no after repair. Native ownership is D9.1.1v4 /
D9.1.3v2; scalar queries preserve selected geometry and scratch lifetime under
D5.5.1.

Isomeet's 16px ratio icon made a nested flex button approximately 176px high,
versus Chromium's 48px reduction. Height queries applied preferred/natural ratios
before resolving the child's declared width, using the parent's free space.
The existing width-expression/min/max resolution now runs before ratio and
replaced-size contributions, after the definite-height fast return. Assigned
used widths retain their existing priority.

The non-square control exposed two connected SVG paths: a percentage geometry
height could be replaced by cached used height or a natural-ratio fallback,
and a ratio-transferred parent height was still treated as cyclic. The shared
SVG percentage-axis helper now consumes a definite height before those
fallbacks, retains automatic sizing for an indefinite basis, and preserves CSS
precedence and existing box/min/max handling. Cyclic fallback applies only when
no definite child-height basis exists. No structure or public ABI change.

Qualification: seven new exported PNG controls improve from 3/7 to 7/7; all
seven independently pass Chromium. Seven existing SVG percentage controls also
pass. The first 6/7 renderer was held for the non-square failure. Four selected
native cases pass, including live 16/24/16px mutations, a 2:1 ratio, alternate
query widths and geometry preservation, plus the existing width-expression,
computed-inheritance and SVG percentage regressions. Integer lint checks 89
casts with zero unmarked. The saved Isomeet page restores the launch badge,
normal form control size and hero placement. Evidence: Lambda's
`temp/online-visual-round32/`; tracked controls are
`test/online/nested_ratio_height_fixtures.cjs` in Lambda. Broad layout/paint/JS/
Input baselines were deferred under the user's focused-verification instruction.


<a id="ri-079"></a>

### RI-079 — Unmatched stylesheet icons exhaust resource discovery

**Status:** FIXED for selected-cascade discovery, 2026-11-14.
**Lambda task blocker:** no after repair. Native ownership is D9.1.1v4 /
D9.1.3v2; temporary substitution values retain D5.5.1 lifetimes.

Conductor's 11 stylesheets contain over 700 URL declarations, mostly icon
classes absent from its 1,124-element document (36 icon classes). The loader
prefetched every raw stylesheet declaration before cascade, including unmatched
and inactive rules. It reached the unchanged 500-resource limit before the
hero, logo images and script consumers loaded. Storetasker also has recorded
capacity rejections among the remaining high-severity pages.

Discovery now reads selected element and pseudo-element style trees after
cascade publication, including live selector/CSSOM changes. It retains source-file
URL bases and the existing variable, fallback, inline-edit and image-set paths.
The URL walker is read-only and substitution scratch dies after discovery.
Premature raw-sheet discovery is removed from stylesheet completion and loader
setup. Resource limits, network waits and D10.1.7 snapshot clocks are unchanged;
Input LI-036 separately preserves reuse of existing resources at capacity.

Qualification: five new loopback HTTP PNG/request controls improve from 0/5 to
5/5, covering 600 unmatched icon rules, inactive conditions, stylesheet-relative
variables, pseudo backgrounds and live selector activation. Five existing inline
CSS image controls also pass, including video and style mutations; three input
capacity/cache lifetime units pass. Focused integer lint checks 89 casts with zero
unmarked. The exact saved Conductor source now displays the large hero and brand
marks, without a capacity warning. Evidence: Lambda's
`temp/online-visual-round31/`; tracked integration controls are
`test/online/render_css_resource_discovery.test.cjs` in Lambda.

This remains speculative discovery of URLs in selected cascade declarations;
visibility pruning and exhaustive resource selection are not claimed. Full
layout/paint/JS/Input baselines and catalog recapture were deferred.


<a id="ri-078"></a>

### RI-078 — Absolute percentage math keeps a provisional containing height

**Status:** FIXED for late preferred-height resolution, 2026-11-14.
**Lambda task blocker:** no after repair. Native ownership is D9.1.1v4 /
D9.1.3v2; scalar declaration queries preserve D5.5.1 owner/scratch lifetime.

Statamic's script-free hero contains an absolute image with
`height:calc(100% + 3rem)`. It retained 48px after its auto-height parent reached
952px, omitting the landscape. The late absolute-child pass revisited only the
bare percentage scalar, ignoring calculated percentage dependencies.

The existing grid percentage-preferred-size resolver is now shared through
`layout_resolve_percentage_preferred_size`. It classifies after substitution,
borrows the final containing axis through `LayoutContainingBlockScope`, resolves
with the child's own units and variables, and returns only a scalar. Grid keeps
its existing behavior; the absolute-height pass reuses the helper and its
existing min/max, box-size and ratio handling. Fixed/automatic heights are not
replaced by percentage guesses. No public structure layout changes.

Qualification: new native `AbsolutePercentageMathUsesTheFinalContainingHeight`
checks images, blocks, own font/variables, a maximum, fixed and bare-percentage
controls through live 160/240/160px parent changes, plus query/geometry isolation.
It and three existing grid/absolute sizing cases pass. Six PNG controls improve
from 2/6 to 6/6 and independently pass Chromium. The focused integer-cast lint
checks 89 casts with zero unmarked. Evidence: Lambda's
`temp/online-visual-round30/`; tracked reductions are
`test/online/abs_percentage_height_fixtures.cjs` in Lambda. Full layout/paint
baselines and catalog captures were deferred under the user's focused budget.


<a id="ri-077"></a>

### RI-077 — SVG inherited reads repeat shared ancestor declaration queries

**Status:** FIXED for the selected cache path, 2026-11-14. Native ownership
**D9.1.1v4 / D9.1.3v2**, cache-pool lifetime **D5.5.1**, unchanged snapshot frame
**D10.1.7**. This is a partial cost repair; Dock's capture blocker is not cleared.

A release CPU sample of Dock localizes repeated full-sheet declaration queries
inside SVG transition baselines. The existing per-document computed-property
cache retained the requesting child's result, but inheritance walked common
ancestors again for each sibling. The resolver now stops at an existing parent
snapshot and fills the visited ancestry with that same computed value. The
walk stays iterative, and direct hits allocate nothing. Existing document,
selector-state, stylesheet/query, font, layout, animation-generation, base-value
and inheritance guards are retained; scoped use-instance and incomplete SMIL
reads remain uncached. Values stay owned by the existing SVG cache pool, with
no borrowed declaration payload or native/public structure-layout addition.

The new native regression covers sibling inheritance, variables, source owners,
initial/unset, explicit inheritance of non-inherited opacity and ancestor class
and attribute changes. It and the neighboring SVG percentage case pass. Five
existing SVG inheritance, animation-base/retargeting, text-hit invalidation,
paint invalidation and cache-teardown tests pass. The same saved script-free
Dock page produces a byte-identical PNG before/after. Single release controls
observe 9.83 CPU seconds before and 9.12 after; these are limited observations,
not a stable performance guarantee or page-wide improvement claim.

Matched live release profiles both reach the unchanged 150-second cutoff:
23 completed layouts before and 25 after. Repeated script-turn full recascade
and layout remains the dominating capture cost (RI-031). No canonical page
capture or reference is replaced from these diagnostic profiles. No URL, site
condition, admission, time/grace budget, clock or broad verification gate changes.
Only the two native and five existing SVG cases were selected; full baseline
and catalog capture were deferred under the user's instruction.

Evidence: Lambda `temp/online-visual-round29/`, `qualified-source-sha256.json`,
`native-final.log`, `rdt-focused.log`, `dock-{static-profile,static-final,live-profile,live-final}.json`,
identical static PNGs and `dock-live.symbolicated.txt`. The linker-map image's
text-section hash was checked equal to the sampled pre-change release image
before resolving its addresses. Final release pin SHA256
`c82dd7955d760cbb7e101bc062f9d049c4164bd3e2e2a7fe51fc3dbae3412f49`.



<a id="ri-076"></a>

### RI-076 — SVG percentage viewport attributes inflate intrinsic height

**Status:** FIXED for the selected height-at-width path, 2026-11-14. Native
ownership **D9.1.1v4 / D9.1.3v2**, measurement rollback **D5.5.1** and snapshot
frame **D10.1.7**. User authorization permits sibling changes.

Contentsquare's saved script-free blog gives the lead card 757.65px native
height versus Chromium 501.61px. The media's final dimensions agree; removing
the tiny Read More SVG removes 256px of excess card height. A network-free
reduction gives a 10×10 final SVG but a 300px intrinsic contribution, inflating
its card to 761.65px versus Chromium 481.64px. Equivalent CSS percentages work.

SVG percentage geometry attributes were applied only in final viewport layout.
Intrinsic width queried them as unresolved and selected the 300px default;
the containing height query reused that width even with a definite narrow
containing block. The existing percentage-attribute getter is now shared with
final layout. Height-at-width queries resolve the attribute against their own
containing width, apply existing width constraints and transfer the ratio.
Authored CSS precedence and cyclic/indefinite width handling are preserved.
The child containing-block scope also retains definite zero instead of silently
borrowing the outer width. No authored style, site condition, admission limit,
resource grace or capture time is changed.

Seven PNG controls improve from 2/7 to 7/7; all independently pass Chromium.
They cover partial/oversized percentages, nonsquare ratios, definite zero,
min/max clamps and CSS precedence. The first candidate's 6/7 result was held
until zero propagation was repaired. The native regression additionally checks
alternate-width queries preserve selected geometry and a live width-attribute
edit changes the card's size. All 98 selected layout/cache unit cases pass;
integer lint reports 89 casts and zero unmarked. Same saved full-page geometry
now gives a 502.05px card, agreeing within 0.5px with Chromium. Small text/baseline
residue and cookie/interactive differences are outside this bounded repair.

A public read-only helper was added; runtime/native structure layouts and Node
state ABI are unchanged. Broader layout/paint baselines and catalog recapture
are deferred under the user's focused-verification instruction. Canonical page
qualification is recorded in Lambda's round28 online comparison audit/report.
Evidence: Lambda `temp/online-visual-round28/`, original/repaired card captures,
`svg-percentage-{before,final}.log`, `svg-percentage-browser.json`,
`native-final.log`, `lint.log`; native
`DynamicLayoutTest.SvgPercentageViewportDimensionsDoNotInflateIntrinsicRows` and
Lambda `test/online/render_svg_percentage_intrinsics.test.cjs`.



<a id="ri-075"></a>

### RI-075 — Canvas general matrix operations are unavailable to the DOM host

**Status:** FIXED, 2026-11-14. Native provider **D9.5.6**, ownership
**D9.1.1v4 / D9.1.3v2**, synchronous matrix borrow **D5.5.1**. The selected
Origin callback failure is cleared; page-wide hero parity remains unresolved.

Origin's DOMContentLoaded particle initializer throws at `context.setTransform`.
An unchanged extracted local script reproduces this exactly. Canvas already
retains a CTM and supports scale/rotate/translate, but has no general set/compose
provider operation and the DOM bindings publish none of these matrix methods.
The native provider now accepts six affine coefficients and set/compose mode.
Existing matrix application is shared with scale/rotate/translate; the setter
preserves clip, path and other drawing state. Runtime bindings supply transform,
setTransform (six numbers or DOMMatrix2DInit aliases/defaults) and resetTransform.
Receiver, arity, conversion, alias and nonfinite checks run before native state
changes. No site condition, alternate source, animation time or budget changes.

All 10 new PNG controls fail before and pass after; 10 independent Chromium
controls agree. They cover composition/order, off-diagonal coefficients,
replacement, reset, clip/style preservation, dictionary/error behavior,
save/restore, resize and actual load-callback initialization. Existing Canvas
context and drawImage goldens pass; the rebuilt DOM graphics-provider contract
case passes and layout integer lint reports zero unmarked casts. The saved
Origin page callback completes after repair. Other visibility/Worker gaps remain.
The provider table gains an operation; affected providers/clients are rebuilt.
Runtime/DocState layout and Node state ABI are unchanged. No playback, WebGL,
getTransform or complete Canvas conformance claim is made.

Evidence: Lambda `temp/online-visual-round27/`, new
`test/online/canvas_transform_fixtures.cjs` / `render_canvas_transform.test.cjs`,
`canvas-before.log`, `canvas-after.log`, `canvas-browser.json`,
`canvas-app*-output.log`, `origin-*-output.log` and `provider-test.log`.
The selected matrix contracts follow HTML Canvas transformations and Geometry
Interfaces DOMMatrix2DInit validation; no Lambda formal ruling is changed.

<a id="ri-070"></a>

### RI-070 — Inherited percentage width retains the parent's used width

**Status:** FIXED, 2026-11-14. Native ownership **D9.1.1v4 / D9.1.3v2**,
computed-value lifetime **D5.5.1**. No full online page is attributed to this
adjacent defect without a fresh page-specific comparison.

`resolve_css_axis_size` copied the parent's mutable given size for `inherit`,
losing percentages, keywords and the difference between a preferred size and
its clamped used size. A 600px card with 20px padding gave its width-inheriting
child 600px instead of 560px; the image-bearing reduction could enlarge the
card to 640px. The resolver now follows the parent's winning physical/logical
size declaration and inheritance chain. Existing variable and owner-context
helpers substitute the parent's value; lengths are normalized in the parent's
font context, percentages remain for the child's containing box, and the
existing axis resolver consumes the temporary owned value before it retires.
No persistent field, API or native layout structure is added.

One focused native regression fails before and passes after. Seven width cases
cover percentages, parent max-width clamping, auto, em, percentage calc with
different parent/child fonts and variables. A percentage-sized image and live
style edits are included. Height and grandchild inheritance additionally check
percentage and calc values. The neighboring min-width regression also passes.
Broader verification is deferred under the user's focused-check instruction.

Evidence: Lambda `temp/online-visual-round26/inherit-before.log`,
`inherit-after.log`, `inherit-controls.log`; native
`DynamicLayoutTest.InheritedAxisSizesKeepComputedPercentagesAndLengths`.
The original image-bearing reduction remains in Lambda
`temp/online-visual-round24/width-inherit-before.html`.

<a id="ri-074"></a>

### RI-074 — A min-content minimum incorrectly caps the preferred width

**Status:** FIXED, 2026-11-14. Native ownership **D9.1.1v4 / D9.1.3v2**,
geometry ownership **D5.5.1**. The selected sizing blocker is cleared; Python
page-wide comparison remains unresolved.

`measure_element_intrinsic_widths` set max-content equal to min-content for
`min-width:min-content`. A minimum is not a cap on preferred width. This also
breaks fieldsets, whose UA minimum is min-content. Python.org's search fieldset
loses the combined width of its inline controls and wraps them vertically.
A reduced 30px span + 220px input + 50px button becomes 220x120 native versus
300x40 Chromium; replacing fieldset with an ordinary block without that minimum
already gives 300x40. The repair removes the incorrect min-width cap while
preserving max-width:min-content and min-width:max-content behavior.

One focused native regression fails before and passes after. It covers fieldset
and ordinary block wrappers, zero/min-content/max-content live transitions,
a definite preferred width and a max-width:min-content control. On identical
saved Python HTML, form width improves from 239.4px to 303.381px and height from
114.6px to 98.6px; Chromium is 348.859px by 57.375px. Separate icon visibility
and slider initial-frame differences remain; full-page clearance is not claimed.
Aggregate verification is deferred under the user's focused-check instruction.
No capture clocks, admission rules, references or thresholds change.

Evidence: Lambda `temp/online-visual-round26/minwidth-before.log`,
`minwidth-after.log`, `fieldset-before*`, `fieldset-browser*`,
`python-before*` / `python-after*`; native
`DynamicLayoutTest.MinContentMinimumPreservesPreferredInlineWidth`.

<a id="ri-073"></a>

### RI-073 — Cyclic percentage invalidation discards replaced media height

**Status:** FIXED, 2026-11-13. **Lambda blocker:** cleared for Pivot271's selected
zero-height media defect. Native ownership **D9.1.1v4 / D9.1.3v2**, region/state
ownership **D5.5.1**, unchanged capture frame **D10.1.7**. User authorized edits.

The same saved script-free Pivot source loads its MP4 and reports natural
506x720 with all 180 resources complete. The enclosing column is about 619.94px
high in both engines, but native video/immediate wrapper height is zero versus
619.72px in Chromium. Authored CSS is `width:100%;height:100%` in an auto parent.
A network-free 200x100 poster video in a 320px auto parent reproduces native 0
versus Chromium 160, while its natural ratio remains correctly available.
Posterless video also loses its 150px default height.

The default/natural replaced-size and ratio stage runs before cyclic percentage
height handling, which clears its result. The stage now follows invalidation;
only the invalidated height percentage is treated as automatic. Existing type
conditions and helpers supply dimensions/constraints, the obsolete iframe
fallback exception is removed, and the repeated ratio call is shared. A natural
ratio also describes content when specified sizing uses border-box; the common
ratio helper now preserves that distinction. No page condition, geometry
constant, CSS rewrite, new decoder or capture/readiness change is added.

The native live-style test fails before and passes after with correct intrinsic
facts, auto/definite/zero parent transitions, 50%/100% percentages, fallback
retention and decorated border-box media. Thirteen PNG controls improve from
6/13 to 13/13; independent Chromium passes all 13. Author versus natural ratios,
content/border-box decorations and the image neighbor are included. All 97
native dynamic and 13 refreshed custom-layout cases, 206 online cases, 247
rebuilt paint/ownership cases, 117 UI assertions (10 runner cases), 26 independent
SVG/PDF rasters, and integer lint (89 casts, zero unmarked) pass. All 17 layout
categories retain 5609 full passes and 295 recorded partials, zero regressions,
skips or errors. No API/header/structure/state layout changes. Input source
and Node ABI are unchanged; prior independent LI-001/002/012 aggregate failures
remain recorded. Release performance and broader replaced-size conformance
remain unqualified.

The unchanged saved Pivot video/wrapper is now 619.72888px high (Chromium
619.71875px), while the overall hero stays 984.699px. The final canonical
full-page capture shows the large animated artwork, resolving the major
missing-artwork annotation. The remaining shape difference reflects movie
phase, alongside smaller background/header/spacing residue. The quick
script-free diagnostic captures the blank initial poster/frame, while native
wall-clock playback advances during full-page loading; D10.1.7 does not freeze
that playback. The actual H.264/avc1 asset decodes through AVFoundation. An
initial sandboxed Swift probe fails but the same probe outside it succeeds;
this is not attributed to the engine or an unsupported codec. Capture clocks
are unchanged, no diagnostic frame is substituted, and playback synchronization
remains unqualified.

Evidence: Lambda `temp/online-visual-round25/`, root
`test/online/video_percentage_fixtures.cjs`, `render_video_percentage.test.cjs`,
`VideoCyclicPercentageHeightRetainsNaturalAndFallbackSize`, same-source
`pivot-media*` / `pivot-fixed*`, original poster/MP4 headers and SHA256 records,
and independent time0/time1 decoded frames. Final pin SHA256:
3fb7114ad21abb7a6c90ce36ee6bfb3573d94370694c7817df60d2a20643e882.


<a id="ri-072"></a>

### RI-072 — Atomic inline alignment loses enclosing relative Y offsets

**Status:** FIXED, 2026-11-13. **Lambda blocker:** cleared for RI-071's inline
control. Selected geometry **D5.5.1**, native ownership **D9.1.1v4 / D9.1.3v2**.

An inline `span{position:relative;left:40px;top:40px}` containing a 100px
inline-block marker places it at (40,0) in the preserved host; Chromium gives
(40,40). Removing the overlapping background reproduces before RI-071. Final
line alignment overwrites atomic Y without retaining enclosing inline offsets,
although the existing text and BR branches already share
`layout_applied_inline_relative_y`. The atomic branch now uses the same helper
before applying its own relative offset. No alternate geometry path is added.

The unchanged positioned-inline PNG control passes both witnesses. Native
`InlineAtomicAlignmentPreservesRelativeAncestors` covers nested offsets and
alternating child relative/static styles without accumulation. The joint final
qualification below covers all 96 dynamic, 13 custom-layout, 193 online and
247 raster/ownership tests, 117 UI assertions, 32 SVG/PDF controls and unchanged
17-category layout inventory. Evidence: Lambda
`temp/online-visual-round24/inline-flow*`, `inline-order*`, `inline-after.log`
and the native regression. No expected pixel or tolerance is weakened.


<a id="ri-071"></a>

### RI-071 — Absolute and relative zero-level siblings paint in separate passes

**Status:** FIXED, 2026-11-13. **Lambda blocker:** cleared for the selected
block-owned sibling paint/hit sequence. Native ownership **D9.1.1v4 / D9.1.3v2**;
geometry **D5.5.1**, unchanged capture frame **D10.1.7**. User authorized edits.

Pivot's saved script-free hero has an early absolute background followed by a
relative foreground container. Both prior hosts paint that background over the
headline/actions, while Chromium shows them. A 600x400 relative parent, absolute
navy background and later lime relative marker reproduce without network,
scripting, fonts or media. The shared flow walk paints relative zero-level
children before the separate absolute containing-block chain. Reverse hit
traversal follows those split passes and also targets the background.

The block-owned collector now combines its in-flow relative/sticky/containment
children with its absolute chain and sorts equal stack levels in DOM order.
The existing selection order helper is promoted to the shared stacking module;
a deep-DOM control exposes its old 64-ancestor cap, now replaced by an unbounded
ancestor walk. The render walk dispatches generic Views so inline boxes retain
their producer. Hits consume exactly the same list in reverse. Explicit phase
ownership on child walkers preserves standalone and inline calls; normal block
callers defer their relative siblings to the combined sequence. Existing signed
layers and custom-layout sequencing keep their owners. Two child-walk signatures
extend with this flag; consumers and all four Node module targets are refreshed.
Public structure/Node state layout is unchanged.

Sixteen PNG controls improve from 7/16 before to 16/16 after. They cover both
source orders, auto/explicit zero, positive/negative layers, sticky and inline
foreground, absolute descendants under static/deep wrappers, overflow clipping,
static owners and a once-only opacity group. All 16 independent Chromium
controls and all 32 independent SVG/PDF rasters pass. The native paint/hit test
fails before and passes after under live z-index changes; all 96 dynamic and
13 custom-layout cases pass. Joint final qualification: 193 online cases,
247 rebuilt raster/ownership cases, 117 UI assertions (10 runner cases), and
integer-cast lint (89 casts, zero unmarked). All 17 layout categories retain
5609 full passes and 295 recorded partials with zero regressions/skips/errors.
The final layout/UI gates run against the exact restored pin after the RDT
client target incidentally relinks the working host. Input source is unchanged;
prior independent LI-001/002/012 aggregate failures remain recorded.

The unchanged saved Missive customer page also restores its large right-card
quotation and Canny logo. The preserved host and RI-069-only host hide both;
the final host and Chromium show them. The final canonical capture agrees,
resolving its manual major omission while retaining smaller font/logo-size and
spacing residue. Same-source evidence is `missive-static-before*`,
`missive-static-after*` and `missive-width-only*` under the round24 directory.

The unchanged saved Pivot source now shows its headline, copy, actions and
customer logos. RI-069's closer hero height remains 984.70px versus Chromium
991.58px. Background/media/header residue remains; exact parity is not claimed.
This finite repair does not implement zero-level descendant lifting through
arbitrary contexts, negative inline stacking or full flex/grid isolation rules.
Evidence: Lambda `temp/online-visual-round24/`, root
`test/online/positioned_order_fixtures.cjs`, `render_positioned_order.test.cjs`,
and native regressions. Final pin SHA256
4648e3a3abaec9cd3eb443e4962ddf164bd43c37d39cdf8d65272e6dfe6a69eb.
Release performance remains unqualified. Canonical review is recorded separately
in online comparison round 24.


<a id="ri-069"></a>

### RI-069 — Height queries omit width expressions and constraints

**Status:** FIXED, 2026-11-13. **Lambda task blocker:** cleared for the selected
Maple static sizing defect; its independent animated-WebP title gap remains
LI-030. Ownership **D9.1.1v4 / D9.1.3v2**, scalar query/selected geometry
**D5.5.1**, unchanged capture frame **D10.1.7**. User authorized sibling edits.

The same saved script-free Maple hero is 843.4px high instead of about 600px,
while the title image and text have correctly sized selected boxes. A reduced
600px column flex, a border-box card with 20px padding and a child
`width:var(--size)` or `calc(100% - 360px)` produces a 600px card instead of
240px. The selected image is correctly 200px square. Literal width controls
agree, isolating the measurement path rather than final layout or media.

The generic height query recognizes only literal lengths/percentages when
resolving its root content width. It omits functions, variables and width
constraints, and conflates the percentage containing block with the supplied
content width and border-box edges. Descendants contribute a height at the
outer width, which the enclosing flex retains as its basis.

The repair reuses element-owned CSS resolution and size classification, existing
content/border-box conversion and min/max clamps. Automatic/intrinsic keywords
and unresolved cyclic percentages retain their existing policy. Assigned
flex/grid query mode still consumes the supplied content width directly. No
second CSS evaluator, style rewrite, page condition, capture-clock change or
public API/structure-layout change is added.

`IntrinsicHeightResolvesWidthExpressionsAndLimits` fails before and passes
after, covering live variable/style/constraint changes, alternate available and
assigned width queries, and preservation of selected image geometry. Sixteen
PNG controls improve from 3/16 to 16/16; independent Chromium passes all 16.
They cover variable fallbacks, calc/min/max/clamp, width limits, percentage and
content/border-box edges, and measured-element font units. All 94 native dynamic
cases, 177 online cases, 247 refreshed raster/ownership cases, 117 UI assertions
(10 runner cases), and integer-cast lint pass. All 17 layout categories retain
5609 full passes plus 295 recorded partials, with zero regressions/skips/errors.
Input source was unchanged; its aggregate was not repeated and prior independent
LI-001/002/012 failures remain recorded. Node module ABI is unchanged.

The unchanged static Maple hero shrinks from 843.4px to 605.77px (Chromium about
600px), restoring the following statistics section to the viewport. Smaller
button/font/spacing residue remains outside the major-gap milestone. The title
still fails because the actual response is animated WebP, which the input codec
explicitly rejects (LI-030); no title repair or exact page parity is claimed.
Canonical refresh/review is recorded separately in round 24's comparison.
Evidence: Lambda `temp/online-visual-round24/`, root
`test/online/intrinsic_width_value_fixtures.cjs` and
`render_intrinsic_width_value.test.cjs`, plus the native regression. The host
SHA256 is a377e8444c548c0a37be721e1967c377402f9835ec78c8e05e1439a29d37db26.
Release performance and broader intrinsic-sizing conformance remain unqualified.


<a id="ri-068"></a>

### RI-068 — Grid row contributions reuse provisional whole-grid widths

**Status:** FIXED, 2026-11-13. **Lambda task blocker:** cleared for the selected
horizontal grid/flex sizing defect. Ownership **D9.1.1v4 / D9.1.3v2**, query
rollback **D5.5.1**, unchanged capture frame **D10.1.7**. The user authorized
sibling edits.

Monese's saved script-free home page has a 1392px native hero versus Chromium's
800px. Its side grid item is finally 280px wide but contributes 1136px of
height. A reduction with a 600px three-column grid, a central 280px minimum and
200px content, and a square image in a percentage-width side item produces a
600px row instead of 200px, independently of scripts and external resources.

Grid row contributions select the provisional given width before the solved
144px column area. That percentage was resolved against the whole grid, and
the height query then applies preferred width again. The generic grid-height
estimator independently guesses equal-width columns, promotes a percentage
minimum using the whole grid, and leaves the inflated size in the enclosing
flex measurement. Fixing only final row contributions exposes this second path.

The repair promotes the existing grid-placement border-size resolver, adding a
read-only mode for measurement, and uses the solved area as the percentage
containing block. Height queries receive the assigned content-width contract.
Explicit zero preferred sizes retain zero. Horizontal grid height-at-width
queries now reuse the existing placement and track-sizing algorithm under
`LayoutMeasureScope`, replacing the guessed column calculation. The existing
width-resolution block is shared by grid and ordinary-flow queries. No page
condition, authored-style rewrite, second track algorithm or clock change is
introduced. Public functions were added; there is no structure-layout change.

The native regression fails before and passes after, including live
600/900/600px resizing and alternate-width scalar queries that preserve selected
grid/image/wrapper geometry. Twelve PNG reductions improve from 3/12 to 12/12;
all independently pass Chromium. The first candidate's 11/12 result remains
recorded and was rejected until the enclosing-query path was fixed. All 93
native dynamic tests, 161 online cases, 247 refreshed raster/ownership cases,
117 UI assertions (10 runner cases), and integer-cast lint pass. Four Node DSOs
were refreshed. All 17 layout categories retain 5609 full passes plus 295
recorded partials, with zero regressions/skips/errors. Input aggregate was not
repeated; prior independent LI-001/002/012 failures remain recorded.

The unchanged static Monese hero shrinks from 1392px to 800px; its heading moves
from y533.90 to y237.90, agreeing with Chromium's y237.90625. Smaller grid image
height/spacing differences remain (385.8px grid versus 324.19px). The live page's
initial hidden-state script is a separate unresolved cause; this fix does not
claim to clear its blank canonical capture or establish exact page parity.
Vertical intrinsic measurement, full grid conformance and release performance
remain outside the milestone. Evidence: Lambda `temp/online-visual-round23/`,
`test/online/grid_row_width_fixtures.cjs`, `render_grid_row_width.test.cjs`, and
`DynamicLayoutTest.GridRowHeightUsesTheSolvedPercentageWidth`. The qualified
host SHA256 is d0aae7d46c8943d80bc05f1445aee9bd13d01b2c78380560297193b0b88aef2c.
Canonical refresh/review is recorded separately in round 23's report.


<a id="ri-067"></a>

### RI-067 — Row-flex cross heights reuse the preferred width after flexing

**Status:** FIXED, 2026-11-13. **Lambda task blocker:** repaired for the selected
horizontal row-flex cross-height defect. Native ownership **D9.1.1v4 /
D9.1.3v2**, query ownership **D5.5.1**, selected capture frame **D10.1.7**.
The user authorized sibling edits.

Panxo 481's headline/copy/actions sit below a large empty hero, pushing its
product panel outside the viewport. Identical saved script-free HTML reproduces
the gap: the native top row is 508px high instead of Chromium's 163.19px. The
right item occupies its correct 405px flexed width but retains a 508px height;
the correctly sized left item aligns to that inflated row's bottom. A reduced
600px row with a 200px fixed neighbor assigns the other item 400px, yet uses
124px of height measured at its preferred 100px width instead of 52px.

The cross-height fallback passes the assigned content width into the generic
height query, whose descendant branch reapplies the root's preferred CSS width.
Intrinsic preferred cross sizes also read the earlier unflexed height cache;
intrinsic min/max clamps can retain that value after main-axis allocation.
Thus growing and shrinking items both wrap against the wrong width during
cross sizing, even though final text layout uses the assigned width.

Height queries now carry an explicit used-width mode, defaulting to the existing
available-width contract. Only that query root skips preferred-width resolution;
descendant width/percentage resolution keeps its own containing block. The mode
is included in the immutable height-cache key. Row-flex intrinsic block sizes
and keyword clamps resolve against the allocated main content width, and the
ordinary cross fallback forwards the same contract. Existing sizing helpers
perform measurement; no authored CSS rewrite or second formatter is added.
The C++ query signatures changed and native/header consumers were rebuilt;
no public structure layout changes were needed.

The focused native RowCrossHeightUsesTheFlexedWidth test fails before and passes
after. It covers width/style changes, shrink/restore, distinct available/used
query results for equal numeric widths, and preservation of selected geometry.
Twenty-two real PNG controls improve from 4/22 to 22/22 and independently pass
Chromium: auto/min/max/fit-content cross sizes, intrinsic min/max clamps,
padding/border, percentage descendants, growth/shrinkage, fixed/zero height and
a row inside a column. All 92 native dynamic cases, 149 online cases, 247
refreshed raster/ownership cases, 117 existing UI assertions (10 runner cases)
and layout integer-cast lint pass. Four Node DSOs were refreshed. The 17 recorded
layout categories retain 5609 full passes plus 295 recorded partials, with zero
baseline regressions, skips or runner errors. Input's aggregate was not repeated.

The same static Panxo row shrinks from 508px to 172px, restoring the headline
and product panel to the viewport. Chromium measures 163.19px; the small residual
text/spacing difference is outside this major-gap milestone. Live canonical
qualification and manual-review resolution are recorded in round 22's report.
Evidence: Lambda temp/online-visual-round22/ preserves the saved document,
failing native/PNG controls, repaired captures, pin and gate logs. Browser
references, URLs, admission, finite resource grace, deadline and virtual time
remain unchanged. Vertical/orthogonal, broader intrinsic-sizing conformance and
release performance are not qualified by this repair.


<a id="ri-066"></a>

### RI-066 — Auto flex columns measure empty wrappers and unlaid-out child boxes

**Status:** FIXED, 2026-11-13. **Lambda task blocker:** repaired for the selected
centered auto-column regression exposed during RI-065 qualification. Native
ownership **D9.1.1v4 / D9.1.3v2**, measurement ownership **D5.5.1** and capture
frame **D10.1.7**. The user authorized sibling edits.

The first RI-065 pin restores Comand but shifts Exactly 471's heading upward
88px inside a clipped centered flex column. Identical saved script-free HTML
confirms the regression: Chromium and the previous pin position the headline
at y298 with height 120; the first repair pin uses y210 with height 120. Trial
line formatting itself returns the correct 120px. The first capture/report
candidate was rejected during visual review and is preserved as evidence.

Two auto-height approximations lose the same collected content. The multipass
extent walk visits source display:contents wrappers rather than collected flex
items. Its auto-basis fallback then initializes descendant boxes to zero and
recurses over those boxes without measuring text. Exactly's heading and action
therefore contribute zero; only 32px gap plus 80px padding gives a provisional
112px column. Centering 176px of actual items plus 32px gap in 32px content space
moves them upward 88px. The previous wrong heading estimate happened to trigger
a later corrective content pass; accurate inline measurement removed that
incidental second pass. An experimental cache-key guard did not fix the cause
and was removed.

Auto extents now reuse the existing collected-item walker. A live column's auto
and content bases both use calculate_flex_basis, which already owns constrained
text measurement and preferred-size rules. No page condition, alternate line
algorithm, cache bypass or clock change is introduced. Exactly's unchanged
static headline returns to y298, and Comand's RI-065 restoration remains intact.

The new CenteredAutoColumnMeasuresFlattenedTextItemsBeforeAlignment native test
covers center/start/center recascade, full heading height, outer auto height and
action placement. The new centered PNG control fails on the first pin and passes
with the companion repair; all 11 PNG controls independently pass Chromium.
Final qualification passes 91 native dynamic cases, 127 online cases, 247 refreshed
raster/ownership cases and layout integer-cast lint. The 17 layout categories
retain 5609 full passes plus 295 recorded partials, with 0 baseline regressions,
skips or runner errors. Live capture refresh and visual qualification are in
the round 21 online report. No release performance claim is made.

Evidence: Lambda temp/online-visual-round21/ keeps failing first captures,
script-free reductions, generic traces, rejected v2/v3 experiments, final v4 pin,
source records and gate logs. No diagnostic logs or experimental cache changes
remain in engine source. Original browser references, authored inputs, admission,
finite resource grace and deadlines are unchanged.


<a id="ri-065"></a>

### RI-065 — Structured inline breaks understate nested flex height

**Status:** FIXED, 2026-11-13. **Lambda task blocker:** repaired for the selected
horizontal text/BR intrinsic-height defect. Native ownership **D9.1.1v4 /
D9.1.3v2**; selected fragment ownership **D5.5.1** and capture frame **D10.1.7**.
The user authorized sibling edits.

Comand 524 places its headline 129.6px too low and clips hero copy/action. The
same saved script-free HTML/CSS reproduces it in native20 and agrees with the
original discrepancy against independent Chromium. Its headline finishes at
194.4px, but the outer flex column measures 355.6px rather than 420.4px. Replacing
fonts, resolving typography variables, removing hidden variants and flattening
contents wrappers do not remove the difference. A reduced monospace fixture
with text/span + BR + text/span measures the column 298px instead of 334px.

intrinsic_text_br_content_height admitted direct text/BR only (except balance).
The complex-inline fallback maxed independently measured inline children,
losing forced-line boundaries when a heading also contained spans. Eligible
horizontal text/BR descendants now reuse the normal inline formatter in an
isolated measurement scope. Counter state, caller context, selected scalar
boxes and text fragments are restored. Mixed fonts are not flattened into one
font and no separate line-layout algorithm is introduced. Direct text keeps
its existing fast path; vertical/paged/atomic estimators are not expanded.

The new DynamicLayoutTest.NestedColumnIntrinsicHeightRetainsInlineForcedBreaks
fails before and passes after, including hide/show recascade, repeated 120/240px
queries and selected text-geometry preservation. All 91 native dynamic cases
pass. Ten root PNG cases improve 2/10 to 10/10 and independently pass Chromium:
direct/nested breaks, normal/contents wrapper, mixed font sizes, preserved and
nonwrapping whitespace, consecutive breaks, direct-text and hidden-break
controls. The complete online suite passes 127 cases; layout integer-cast lint
passes. The unchanged saved Comand hero now measures 420.4px and positions its
headline at 333.6px, within 0.02px of Chromium. Its missing live background video
remains a separate unresolved difference; no complete Comand repair is claimed.

Evidence: Lambda temp/online-visual-round21/ retains reductions, failing tests,
qualified host, logs and comparison refresh. Capture admission, finite resource
grace, 150s deadline, authored pages, browser references and virtual clock stay
unchanged. Broader gate and canonical refresh qualification is recorded with
the round 21 online report.


The first pin was held after Exactly visual review exposed RI-066. Its companion
repair and the eleventh centered PNG control are required for final qualification.

<a id="ri-064"></a>

### RI-064 — Inline CSS image URLs never enter snapshot resource discovery

**Status:** FIXED, 2026-11-13. **Lambda task blocker:** repaired for inline URL
discovery. Native ownership **D9.1.1v4 / D9.1.3v2**, declaration scratch lifetime
**D5.5.1**, selected-frame capture **D10.1.7**. The user authorized sibling edits.

Maki222's white hero omits its authored video CSS background, leaving white
heading/copy unreadable. The inline URL survives parsing and the image paints
in isolated controls; literal local pixels also paint in the complete page.
Network-managed paint reads only ready resource bytes. The loader prefetches
stylesheet URLs before inline attributes are parsed, and document discovery
previously queued img/use/video-poster resources but no inline CSS URLs. Thus
all77 tracked requests could complete while the required background was never
requested. This is distinct from RI-061's finite readiness grace.

Document image discovery now walks parsed inline declarations and reuses the
existing recursive CSS URL visitor, promoted through resource_loaders.h. CSSOM
edits borrow their owned declaration blocks; untouched HTML attributes parse
through the existing declaration parser in one traversal-owned scratch pool.
The scheduler copies URL ownership before that pool retires. The shared walker
retains custom-property/function/list behavior without rewriting consumer URL
spelling. Requests use the existing scheduler, priorities, admission and bounded
snapshot wait. Paint still does not start a blocking HTTP fetch.

Five reduced HTTP PNG/request cases in Lambda's existing render_session.test.cjs
fail0/5 before and pass5/5 after: div/video background, inline variable,
image-set and script style mutation. The complete online suite passes116 cases.
Fresh raster/paint/ownership247 cases,117 existing media/CSSOM UI assertions
(10 runner cases), and17 layout categories pass with5609 full passes/295
recorded partials and0 regressions/0 skips. Both unchanged saved Maki script-free
and full-script controls regain the hero media; full external source/time
freezing and exact sizing/video frame parity are not claimed.

Evidence: Lambda temp/online-visual-round20/ retains failing controls, final
pin, source patches, logs and the canonical comparison refresh. The existing
1000ms optional-resource grace,150s parent deadline, original browser references,
URLs, authored content and virtual clock are unchanged. No broader media
readiness, mask consumer, platform decoder or SPA support is claimed.


<a id="ri-063"></a>

### RI-063 — Decoded video overwrites foreground page content

**Status:** FIXED, 2026-11-13. **Lambda task blocker:** repaired for the selected
video paint-order defect. Native ownership **D9.1.1v4 / D9.1.3v2**, immutable
render ownership **D5.5.1**; the selected-frame contract **D10.1.7** is unchanged.
The user granted sibling implementation updates.

Bizzabo656's full saved HTML renders its primary heading/copy/action and header
under the hero video. At capture the video is playing with a decoded1920x1080
frame, while heading/ancestors have visible styles, opacity1 and valid geometry.
The absolute video has z-index:-1. `render_video_frames` and its cached update
path nevertheless blit the video over the completed page. This violates the
existing [CSS painting order](https://www.w3.org/TR/CSS22/zindex.html#painting-order).

The native `DecodedFramePreservesFollowingForegroundPaint` regression records
a decoded video then a red foreground rectangle. Before repair the post-paint
blit replaces the red4278190335 pixel with decoded4281808966. The repair captures
decoded pixels and controls once in an immutable owned ImageSurface, then
records it through the ordinary image painter in CSS paint order. Shared
object-fit/object-position, content clips, transformations and effect scopes
apply. The video marker retains decoder-epoch invalidation. Playback wakes
repaint the page through the same ordered stream; the out-of-order cached blit
and unused placement cache are removed. Worker replay cannot borrow a mutable
decoder buffer. This does not add a decoder/backend or force a later frame.

Qualification:34 native video cases,247 raster/paint/ownership cases,
111 online CLI/PNG/capture/comparator cases and117 assertions in seven existing
media/CSSOM UI replays pass. Seventeen layout categories retain630 JS and4979
static passes plus295 recorded partials, with0 regressions/0 skips. The native
regression also checks contain/cover and retained pixels after a decoder seek.
The complete saved Bizzabo HTML restores the foreground; external resources
remain live, so that screenshot is not a fully frozen-source or video-time
comparison. Full-catalog canonical refresh/report evidence is recorded in
Lambda `temp/online-visual-round19/` and `test/online/`.

Limits: live video now replays ordered page content rather than the previous
placement-only optimization. No release playback performance qualification or
new GPU video composition claim is made. SVG/PDF video frames and platform
backends remain separately qualified; browser references, URLs, admission,
deadlines, comparison thresholds and virtual clocks are unchanged.


<a id="ri-057"></a>

### RI-057 — Sizing and noscript snapshot regressions in the Lambda layout gate

**Status:** RESOLVED, 2026-11-13. **Lambda task blocker:** repaired. Native
ownership: D9.1.3v2. Initial reproduction: Lambda `e5d5e848e`, Radiant `ea6ec14`.
The user explicitly approved Radiant edits and overriding the sibling boundary.

The recorded `wpt-css-sizing` baseline had two regressions and `wpt-css-lists`
had one. All reproduced individually; no fixture, reference, tolerance or scored
baseline was changed.

- `svg-intrinsic-size-001`: the 40x40 viewport with a 4:1 viewBox was measured
  correctly, then treated as an automatic cross size because its attribute had
  no numeric BlockProp slot. The shared automatic-size predicate now recognizes
  SVG viewport hints when CSS does not override them. Cross sizing reuses the
  existing explicit-size helper and publishes measured column width before
  alignment. Late line reconciliation reuses the existing stretch predicate;
  it no longer overwrites attribute dimensions through a second numeric check.
- `keyword-sizes-on-abspos`: intrinsic min/max keywords did not make the parent
  query cyclic. Descendant percentage canvas widths therefore contributed the
  provisional containing width instead of zero/20px min/max-content values.
  A shared preferred/min/max predicate now governs contribution measurement,
  cyclic percentage/cache basis and ratio transfer.
- `counter-set-002`: all geometry already matched. The unrendered snapshot
  confused the suppressed layout role with computed `display:none`, shifting
  comparison child paths. It now uses the existing computed-display serializer
  while retaining zero geometry.

Final qualification, debug split host at the commits above plus these changes:

- `make test-radiant-baseline`: exit 0, **8,165 passed / 299 recorded partial /
  20 skipped**, zero failures out of 8,484 cases.
- Sizing: 776/776. Lists: 52 passing plus eight recorded partial results.
- Selected UI baseline, including the independent Lambda-owned LR12-54 iframe
  rooting fix: 329/329.
- Owner `test_layout_dynamic_gtest`: 87/87, including two new regressions for
  all six intrinsic canvas limits and independent SVG axes through row/column
  flex, CSS auto and retained attribute mutation.
- Layout integer-cast lint: 89 casts checked, zero unmarked exceptions.

Evidence is under `../../lambda/temp/failed-radiant-tests/`, especially
`radiant-baseline.log`, `native-dynamic-final.log`, `radiant-lint.log` and the
original focused mismatch logs. [Implementation record](impl/Lambda_Impl_Radiant_Baseline_Repairs.md).
The extra Lambda sweep remains incomplete after its missing optional WebRTC
prerequisite; ASan host linkage is a separate Lambda LR01-25 limitation.


<a id="ri-056"></a>

### RI-056 — Native input-value test target does not link its CSS resource call

**Fixed and reverified 2026-11-22.** The isolated Lambda test target originally
omitted the implementation of `process_css_resource` and failed to link its
downloaded stylesheet tests. Radiant `01e0111` extracted that implementation
into `resource_css.cpp`. Lambda `f31a61fe80` added the owner file to the test
target's sources under **D9.1.1v4 / D9.1.3v2**. The original failing call
was in `RadiantInputValueTest.DownloadedStylesheetsUseOwnerOrderAcrossCompletionOrder`
in the aggregate `make build-test` run.

At Lambda `3bed8e86ad` with Radiant `7ca2432`, a fresh debug build of
`test_radiant_input_value_gtest` links and passes all 12 cases, including
downloaded CSS owner order, media and disabled state. The original link
failure is recorded in `temp/sync-demo/build-test.log`. Current evidence:
`temp/radiant-ordinary-fixes/ri-056.log`.

<a id="ri-055"></a>

### RI-055 — Paint engine lacks the M3 wet-media kernels

**Status:** FIXED, 2026-11-12. Native ownership: **D9.1.1v4 / D9.1.3v2**; engine contract: PE9.

Initially the paint engine had no `paint_wet_layout`, `paint_wet_step`, or
`paint_wet_deposit` implementation. PE9 calls for a caller-owned planar sheet,
in-place Curtis steps, activity bounds, and transfer into pigment tiles. At the initial report Lambda could not connect the native watercolor path or
establish parity and the 512-by-512 step budget without those kernels and their API.
This is an unimplemented milestone, not a diagnosed defect in existing kernels.

Reproduce from Lambda-opus: inspect `../radiant/radiant/paint/` and run
`rg -n 'wet_step|wet_deposit|PaintWet|wet_layout' ../radiant/radiant/paint`;
at the initial report there were no matches. `lmd/package/procart/watercolor.ls` supplies the 21-plane
reference and `vibe/Lambda_Pkg_Paint.md` section 6 gives M3 acceptance: parity,
the wet-step budget, and the unchanged procart watercolor demo on the native
step. Implement and test the kernels in Radiant, then expose the owner-defined
interface through Lambda's adapter and validate those acceptance cases.

Continuation: the user authorized Radiant changes. `paint_wet_step` now has a
three-pigment, caller-owned 21-plane implementation with validation and focused
native tests. A ten-step, 40-by-32 two-pigment comparison against the Lambda
reference differed by at most 1.2e-7 across the planes; this is a focused
check, not full M3 qualification. The continuation now adds dry-to-pigment
transfer and Lambda adapters for both f32 and float64 sheets. An 80-step
reference comparison passes on the three Lambda tiers with forced GC, and five
30-step runs against a pinned release archive give medians of 6.04 ms (f32)
and 6.98 ms (float64) for a fully active 512 x 512 sheet. The watercolor replay
passes 64/65; its one mobile layout assertion fails identically with the original
Lambda step. At that checkpoint the wet layout was still fixed to three pigments; the
continuation below resolves that engine limitation. Evidence: Lambda-opus `temp/paint_m3/`.

Resolution: `paint_wet_layout` now describes 1–4,096 pigments with `12 + 3M`
planes, and the shared f32/float64 kernels step, track activity, dry and transfer
every pigment. The bridge checks table/shape agreement and charges work by count.
Native tests cover one, four and seven pigments, the last pigment's activity and
drying, span rejection and transfer; a 4,096-pigment Lambda probe passes. The
three-pigment outputs remain byte-identical to the prior implementation in six
checked cases. A source-matched isolated release archive passes 36 native paint
tests and the 512 x 512 wet budget (6.79 ms f32, 7.47 ms float64 locally).
Lambda's isolated baseline passes 7,457/7,457 and Test262 passes 40,261/40,261;
five wet fixtures pass on three tiers under forced GC. Both the native-step and
reference-step watercolor replays still fail the same one mobile layout assertion
(64/65 each); its ownership is unconfirmed and it is not claimed as fixed here.
Evidence: Lambda-opus `temp/paint_m3_general/`.

<a id="ri-054"></a>

### RI-054 — Owner-held font fixture lookup under Lambda integration

**Status:** FIXED, 2026-11-12. Ownership: **D9.1.1v4 / D9.1.3v2**.
`radiant_test_fixture_path` chose Lambda's `test/fixtures` merely because the
directory existed, even when the requested `fonts/cmunrm.woff2` belonged to
Radiant. This made `FontMetricTest.RasterPreservesNativeGrayscaleCoverage` fail
with a null loaded font. The shared helper now tests the requested local file
before resolving the owner-held fixture; isolated runs use `RADIANT_ROOT`.
The same font test fails before and passes after correction. Native metric
expectations and grayscale comparisons are unchanged. Evidence in Lambda-opus:
`temp/paged-p7-integration/{paged.xml,updated-focused.xml,paged-final.xml}`.


<a id="ri-053"></a>

### RI-053 — Historical P7 comparison artifacts are unavailable in the current checkouts

**Status:** CLOSED by explicit user waiver, 2026-11-12. Native ownership:
D9.1.1v4 / D9.1.3v2. No layout ruling changes.

The historical P7 record in Lambda's `vibe/impl/Radiant_Impl_Paged_Integration.md`
requires the archived 113-input source inventory, release executables/receipts,
PNG controls and source-size snapshot under `temp/paged_release/`. That directory
and its qualification/source-size/raster scripts or archives are absent from the
available Radiant, Lambda and Lambda-opus temp trees. The source commit
`1a9921fe9` still exists in Lambda history, but the generated input hashes and
exact captured artifact set are unavailable. A source reconstruction alone must
not be reported as a replay of the original measured corpus.

Reproduction: from Radiant, inspect `temp/paged_release/`,
`../Lambda/temp/paged_release/` and `../Lambda-opus/temp/paged_release/`, then
search the three temp inventories for `paged*qualification.json`,
`paged*source_sizes.json`, `qualify.py`, `raster_controls.py` and paged archives.
The required historical set is not present. Cause of its loss is **unconfirmed**.

On 2026-11-12 the user confirmed that the archive location is unknown and
instructed us to assume it is deleted/lost and ignore its replay. This closes
the missing-artifact blocker by waiver; it does not make the old raster,
performance or source-reduction comparisons pass. Fresh source/core-fidelity
receipts under Radiant's `temp/paged-core-*` establish the current scope.
`scripts/qualify_paged.py` records the waiver separately from its current gates.
No reference results or historical measurements are reset.


<a id="online-visual-review-20261112"></a>

### Online visual qualification — 2026-11-12

RI-022 and RI-024–RI-027: 68 native dynamic layout tests, the document activation
regression, 38 StyleEpoch tests and 11 owner input-value tests pass (118 focused
cases). Online tools pass 14 checks. The aggregate Radiant gate retains the
existing JS03-L2 forced-GC iframe failure: 8,163 pass, 299 partial, 20 skip,
one fail. The final CSS images suite passes all 56 cases, including size
containment of video posters. No references, URLs, deadlines or comparison
thresholds changed. Evidence and the full comparison are under Lambda
`temp/online-visual-round2/` (`radiant-baseline-qualified.log`).

<a id="ri-027"></a>

### RI-027 — Mobile viewport metadata shrinks desktop layout independently of its host

**Status:** FIXED, 2026-11-12.
**Lambda task blocker:** no. Ownership: **D9.1.1v4 / D9.1.3v2**; scale axes
remain distinct under the scale design's **RSC6/RSC7**.

Cap'n Proto declares `width=480`. A 1200×800 desktop capture keeps a1200-pixel
surface and JS window but uses 480 pixels for the native root and CSS media
queries, leaving a large dark region. Chromium's desktop root is 1200 pixels.
The cause is the direct use of raw mobile meta dimensions in `layout_init`,
layout publication inputs and CSS media setup.

A shared host-viewport resolver now supplies all three stages. Parsed metadata
remains available, while desktop layout follows the host's CSS viewport.
Registered viewport-unit lengths also use that active CSS environment instead
of raw meta hints. Native regression covers width480/height320 metadata,
1200→800 host resize, CSS media queries, vw/vh, registered vw lengths and
oversized2000×2000 metadata. It fails before and passes after repair. Mobile
viewport adaptation/zoom remains outside this correction. Evidence: Lambda
`temp/online-visual-round2/capnproto-tree.json`, `test-viewport-{before,after}.log`.

Verification: see the [online visual qualification](#online-visual-review-20261112).


<a id="ri-026"></a>

### RI-026 — Background color resets paint opaque black

**Status:** FIXED, 2026-11-12.
**Lambda task blocker:** no. Ownership: **D9.1.1v4 / D9.1.3v2**.
OCaml's documentation hero declares `background-color:initial` above its SVG
background. Native layout sends that keyword to generic color conversion, whose
invalid-color fallback is opaque black. `unset` fails in the same way. The
background is non-inherited and its initial color is transparent:
[CSS Backgrounds §2.2](https://drafts.csswg.org/css-backgrounds-3/#background-color),
[CSS Cascade §7.3](https://drafts.csswg.org/css-cascade-5/#initial).

A shared background-color resolver now handles wide reset keywords before
generic color conversion in direct declarations and ordered background
composition. Explicit inheritance remains intact, and a color reset retains
the image layer. The reduced `background-initial.html` shows initial/unset alpha
255 before the fix, versus alpha0 for authored transparent. Native regression
`BackgroundColorWideKeywordsUseTransparentInitialValue` covers these cases,
shorthand reset and inheritance. Evidence: Lambda `temp/online-visual-round2/`.

Verification: see the [online visual qualification](#online-visual-review-20261112).


<a id="ri-025"></a>

### RI-025 — Headless exports never queue ordinary network image dependencies

**Status:** FIXED, 2026-11-12.
**Lambda task blocker:** no. Native ownership: **D9.1.1v4 / D9.1.3v2**;
finite snapshot frame selection: **D10.1.7**.

Loader-stage HTTP documents have a resource manager. `load_image_resource`
correctly consumes only ready bytes during layout/paint, but export preparation
never discovers ordinary `img` requests. Curl's main SVG logo is absent and
laid out with fallback dimensions; DEBUG logs say `Network image not ready
without blocking` with no corresponding request. The same logo and NOAA's photo
decode/render correctly from local files. This is a missing integration step,
not an unsupported image codec.

`radiant_prepare_snapshot_resources` now queues selected image sources and
external SVG uses before layout, binds the existing native resource manager,
and shares the layout CLI's existing bounded wait with export. It delivers
native completions without executing JS timers or advancing the virtual frame.
No budget increases or synchronous HTTP fetches were added to layout/paint.
The existing wait defaults to1000ms, with the existing 0–5000ms environment
override. Slow/pending resources can still be absent; this is not a claim of
full lazy-media/video support.

Offline regression `ExportQueuesOrdinaryNetworkImagesBeforeInitialLayout`
starts from a cached73×41 SVG available only through its HTTP request identity.
Before repair it has fallback height16 and no image; after repair it has the
correct natural geometry/image and virtual time0. The shared native image/use
discovery walker is also reused by interactive resource discovery. Evidence:
Lambda `temp/online-visual-round2/test-image-{before,after}.log`, `curl-debug.log`,
`assets-test.png` and live probes.

The followup also queues `video[poster]` as an opaque image dependency, without
binding it to the video as an `img` owner. Video width/height hints now survive
style resolution. When video metadata is unavailable, the existing replaced
intrinsic facts use a ready poster's natural dimensions before assigning the
generic 300×150 fallback. Automatic axes use the existing replaced-size
constraint solver so a maximum height scales the width with it. Size containment
keeps its substituted dimensions and suppresses the poster's natural ratio.
The cached-SVG export regression covers ordinary images, authored video sizes,
automatic poster sizes, max-height and containment. Automatic video size fails
before with 300×150 and passes after with 73×41. This is the existing poster
and intrinsic-sizing path; it does not implement video timeline parity.


Verification: see the [online visual qualification](#online-visual-review-20261112).


<a id="ri-024"></a>

### RI-024 — Multi-keyword link relations omit entire stylesheets

**Status:** FIXED, 2026-11-12.
**Lambda task blocker:** no. Native ownership: **D9.1.1v4 / D9.1.3v2**.
`vite_docs` and `rollup_docs` use `rel="preload stylesheet"`. Initial CSS
prefetch, linked-sheet loading and dynamic resource discovery compared `rel`
against the complete string `stylesheet`, omitting these sheets. Vite's banner
image consequently expands across the unstyled document. The HTML link relation
is a set of case-insensitive ASCII-whitespace-separated keywords:
[HTML links](https://html.spec.whatwg.org/multipage/links.html#linkTypes).

Promoted the existing sandbox token walker to `radiant_html_has_token` and
reused it in all three paths. The newly recognized titled alternate sheets stay
loaded but initially disabled; existing CSSOM can enable them. Native focused
regressions pass for keyword boundaries, case, HTML whitespace (excluding VT),
actual stylesheet application, and alternate activation. Live Vite/Rollup
captures now retain their document CSS. Qualification and residual details are
under Lambda `temp/online-visual-round2/`.

Verification: see the [online visual qualification](#online-visual-review-20261112).


<a id="ri-022"></a>

### RI-022 — Downloaded dynamic stylesheets lose their link media condition

**Status:** FIXED for the confirmed sheet activation defects, 2026-11-12. **Lambda task blocker:** no; this affects one
remaining large-difference class. Native ownership: **D9.1.1v4 / D9.1.3v2**.

`lambda.exe render https://webassembly.org/ -o temp/online-visual/wasm.png
-vw 1200 -vh 800` can export the dark stylesheet despite light media preferences.
The site writes both theme links from a `noscript` template. Their `media`
attributes select mutually exclusive color schemes. `process_css_resource` in
`radiant/resource_loaders.cpp` parses completed dynamic CSS and assigns its owner,
but leaves `CssStylesheet::media` null. Both sheets can enter the cascade, making
the result depend on loading. The initial linked-sheet collector already checks
media; the asynchronous completion path does not. The native cause is confirmed;
additional custom-element behavior on this site is not qualified.

Evidence in Lambda: `temp/online-visual/webassembly.html`, the preserved
`refreshed-before/webassembly-lambda.png`, and
`probes/webassembly.png` plus its log. Expected: retain the link's media
condition on the associated sheet and evaluate it with the shared CSS engine.
Do not force a particular theme or choose sheets by their filenames.
The selection requirement is also stated in the
[HTML link media processing rules](https://html.spec.whatwg.org/multipage/semantics.html#processing-the-media-attribute).

Implemented the completion-path correction: copy the owner's media query into
the document pool and initialize its disabled flag before appending the sheet.
The native `DownloadedStylesheetsRespectLinkMediaAndDisabledState` regression
covers completed/cached downloads and active/inactive/disabled sheets; the full
11-case input-value suite passes. Three final WebAssembly.org captures still
select dark, so this site is not claimed fixed. Its custom element changes
media and disabled state during loading; the remaining association/mutation
cause is unconfirmed. Evidence: `temp/online-visual/wasm-final-{0,1,2}/`.
The separately built pristine-current-source control produces the identical
61.45% mean error against the browser image (`pristine-wasm/webassembly.png`),
so this remaining mismatch is not introduced by the three repairs.

**Confirmed followup and repair (second review):** `link.media` and `style.media`
were absent from IDL reflection; `link.disabled` used the style element's
sheet-only setter and lost pending-link state. Associated sheets did not receive
owner attribute edits. Lambda's shared CSSOM owner-attribute hook now updates
the retained sheet condition/disabled state, invalidates it, and preserves
unrelated CSSOM state. Initial and dynamically parsed inactive sheets remain
associated with their media conditions instead of being discarded.

The native fast, reference, layer-rank and single-element cascade paths also
ignored root-sheet media; they now share an active-sheet predicate. The new
`StylesheetOwnerMediaAndPendingDisabledRetainTheirAttributes` integration test
fails before and passes after (pending-link booleans, media on/off, attribute
set/remove, immediate computed style and sheet identity). The final
WebAssembly.org capture selects light: mean color error 61.4%→0.6%. Its shadow
control rendering remains partial under Lambda JS13-L2. The earlier unconfirmed
association/mutation cause above is now resolved; WASM execution was not the
cause of this theme mismatch. HTMLLinkElement's disabled IDL is reflected,
whereas HTMLStyleElement's disabled IDL controls its associated sheet:
[HTML link/style interfaces](https://html.spec.whatwg.org/multipage/semantics.html#the-link-element).


Verification: see the [online visual qualification](#online-visual-review-20261112).


<a id="ri-021"></a>

### RI-021 — Grid placement declarations suppress actual flex items

**Status:** FIXED, 2026-11-12.
**Lambda task blocker:** no. Computed/view storage follows **D5.5.1**;
native ownership follows **D9.1.1v4 / D9.1.3v2**.

Chrome, Firebase, Google Cloud and web.dev documentation pages wrap ordinary
description text into tiny columns or give it zero-sized boxes. A local
reduction requires a column flex parent and a child with `grid-area: content`.
Resolving the declaration provisionally allocates the parent-item union's grid
arm. Flex initialization refuses to overwrite that arm, so the flex algorithm
cannot size the item. The property's declaration order incorrectly determines
participation in its parent's formatting context.

`layout_set_parent_item_kind` now releases the provisional owning prop, adopts
the actual parent role and invalidates the computed storage before applying
its CSS again. Flex and grid initialization share this transition. It also
replaces grid's previous discriminant-only conversion, which abandoned the
old owning prop. The authored CSS remains in the cascade; no grid declarations
are removed to match a page. Native regressions cover text geometry and four
flex/grid parent switches. All 63 dynamic-layout checks and integer-cast lint
pass. The reduced Chrome description grows from zero to the browser's 564px.
Both aggregate integration runs have 8,163 passes, 299 partials, 20 skips and
one existing forced-GC iframe failure (Lambda JS03-L2). A separate pristine
current-source host reproduces that failure 3/3, as does the repaired host.
The gate remains red; its failure is not suppressed. Broader live recapture
is recorded in the Lambda online-visual checkpoint.

Evidence in Lambda: `temp/online-visual/min-rules.json`, `prop-no_area.json`,
`chrome-static-fixed.json`, `test-root-causes.log`, and `test-dynamic-all.log`.

<a id="ri-020"></a>

### RI-020 — An initially disabled linked theme participates in the cascade

**Status:** FIXED, 2026-11-12.
**Lambda task blocker:** no. Native ownership: **D9.1.1v4 / D9.1.3v2**.

Chrome documentation links its dark theme with the boolean `disabled`
attribute. `collect_stylesheets_in_document_order` loaded the sheet but did not
initialize its existing `disabled` flag, so its dark declarations replaced the
active light theme. Firebase, Cloud and web.dev share this failure.

The collector now retains the sheet for CSSOM access while marking the root
sheet disabled. Its imports stay inactive through the existing sheet-tree
traversal. Native tests cover the empty boolean attribute, `disabled='false'`,
imported rules and enabling/disabling the retained sheet across recascades.
All focused and neighboring native tests pass. Aggregate qualification has
the same existing iframe failure documented above under RI-021.
The minimal before fixture incorrectly gives a light page a dark
background and changes a 600px block to 8px. Evidence in Lambda:
`temp/online-visual/disabled-before.json` and `test-root-causes.log`.

Live before/after coarse mean image error against the unchanged Chromium image:
Chrome 79.5%→7.3%, Firebase 75.8%→1.5%, Cloud 67.1%→4.1%, web.dev 57.0%→6.0%.
These improvements combine RI-020 and RI-021; remaining icon/image details are
outside the major-difference milestone.

Verification evidence: `../../lambda/temp/online-visual/radiant-baseline-final.log`,
`test-dynamic-all.log`, `test-media-final-all.log`, `layout-lint.log` and
`pristine-build.py`. The online comparison scripts and saved before/after
images retain the remaining differences; none are filtered out to qualify these repairs.

<a id="ri-019"></a>

### RI-019 — Reflow aborts when the focused target becomes non-focusable

**Fixed for the reproduced mutation path, 2026-11-22.** Native mutation
publication pruned disconnected owners and then requested reflow. A connected
owner could keep focus after its eligibility changed, so the unchanged state
invariant rejected the next transition. Mutation publication now checks the
connected owner through the host's existing eligibility query and uses the
normal focus transition to clear it (**D1.10 / D5.5.1 / D9.1.3v2**).

Five local debug controls abort before the change. They set `display:none`
or `visibility:hidden`, change an ancestor's class, insert a stylesheet rule,
or remove a focused anchor's `href`. All five and the existing disabled and
inert controls clear focus and complete the geometry read after the repair.
See `temp/radiant-ordinary-fixes/focus-*-after.log`. Historical failures on
Designmodo, Pulsetic and the Eden ecosystem replay supplied the original
reports. Their original logs are under `test/online/snapshots/` for
`saas_landing_046` and `saas_landing_138`, and
`temp/eden-load/smoke-all-final.log` for Eden. Those replays were outside
this local verification.

**Focus event follow-up, 2026-11-23.** `css_focus_fixup` passed 18 of 24
assertions after the early eligibility check was added. The state-only clear
left the post-reflow transition without a target for `blur` and `focusout`.
Mutation publication now calls the existing document focus transition through
the event-host provider before reflow validates state (**D1.10 / D5.5.1 /
D9.1.3v2**). The fixture passes all 24 assertions at 1x and 2x. A timer-driven
ancestor hide also preserves a blur listener's refocus and layout read. A
keyboard edit followed by hiding the input delivers `change`, `blur`, and
`focusout` in order. Reproduction and verification are in
`temp/css-focus-fixup/`.

**Detached-target follow-up, 2026-11-28.** Invalid editor navigation detached
the focused textarea while its blur sequence was still being delivered. The
event scope now retains the owner document and resolves the pinned node identity
for `blur`/`focusout` even after disconnection (**D1.10 / D5.5.1 / D9.1.3v2**).
`doc_viewer_edit_navigation_invalid` passes all 7 assertions.

<a id="ri-018"></a>

### RI-018 — Online exports exhaust memory before viewport PNG encoding

**Status:** RESOLVED for the four original reproductions, 2026-11-11.
**Lambda task blocker:** cleared for APR, Azure, F# and Leaflet captures.
Ownership **D9.1.1v4 / D9.1.2v2 / D9.1.3v2**; precise carrier ownership
**D6.3**, map shape mutation **D3.4.3**. Coordinated sibling edits authorized
by the user. The original observations had separate allocation causes.

- Leaflet's collapsed-cell borders and fragmented-simple backgrounds were
  lowered without clearing the pending PaintIR list. Later cells replayed
  every earlier border. Both raster boundary paths now use the existing
  `paint_record_lower_pending` gateway. Leaflet's display list fell from
  4,452,044 commands to 8,862 and the 1200x800 PNG exports successfully.
- Azure/F# exposed Lambda String boxing that reflagged every indexed property
  through copied shape prefixes. The Azure allocation snapshot showed
  1.77GB/30M allocations in the transition arena; a native allocation stack
  identified `mvp_lmd_box_primitive` / `type_tree_retype_target`. Lambda now
  constructs the complete unpublished index/length shape once, preserving
  subclass identity, UTF16 units and immutable descriptors. Azure post-script
  allocator reservation fell from 1,925,873,344 to 160,050,837 bytes. APR,
  Azure and F# now have canonical loaded-document PNGs.
- Follow-up Studio capture exposed input's legacy all-frame GIF loader
  bypassing the process memory policy. It now admits its existing preflight
  estimate through `memtrack_allow_allocation` before giflib expansion. GIFs
  that exceed the shared budget retain the independent still-frame path;
  the configured budget is unchanged. Studio's image category fell from
  1,059,694,508 to 711,756,076 bytes and PNG encoding succeeds. Its selected
  initial frame is mostly blank; this is load/export qualification, not a
  claim of full visual fidelity.

Verification: `RadiantDocumentTest.BoundaryFragmentsAreConsumedOncePerBox`
checks exact linear command counts and empty pending lists for 32 collapsed
border and fragmented-background flushes; it and collapsed-table geometry
pass 2/2. Required Radiant integer-cast lint passes. The full Lambda JavaScript
native executable passes 859/859 tests, including long-wrapper memory and
UTF16/descriptor checks. Input image I/O passes 35/35, including budget
rejection and retained still decoding. The final Radiant baseline passes 8,164 with
299 partial / 20 skipped of 8,483 and no failed gate. The final Lambda gate
passes 7,415/7,415, including 859 JS and 368 GC stress cases; Lambda
LR03-46 records the later private-object fallback repair and Ko-fi PNG.

Evidence in Lambda `temp/online-recovery/`: `azure-memory.json`,
`azure-string-fixed-memory.json`, `azure-arena-trace-output.log`,
`leaflet-dl-trace-output.log`, `leaflet-border-fixed.log`,
`border-fragment-tests.log`, `js-mvp-suite.log`, `radiant-baseline.log`,
`studio-image-trace3-output.log`, `studio-budget-fixed-output.log`;
input `temp/online-gif-tests.log`. Canonical PNGs/logs are under
Lambda `test/online/snapshots/`. Temporary allocator instrumentation was removed.


<a id="ri-017"></a>

### RI-017 — One-shot exports drain recurring animation before capture

**Status:** RESOLVED, 2026-11-11. **Lambda task blocker:** cleared for finite
HTML file/source exports. Ownership **D9.1.1v4 / D9.1.3v2**; snapshot contract
**D10.1.7**. The user authorized coordinated sibling source updates.

`render_export_load_html_document` and `render_export_load_html_source`
left `auto_close_event_loop` and `virtual_clock_enabled` false. Recurring rAF
and timers therefore kept the host in its long-lived drain loop before PNG
encoding. Native layout suspends the script CPU watchdog, so this can also
outlive its wall-time allowance. A stack sample on Boldare located the loop
under `js_event_loop_drain_impl` / `js_animation_frame_drain` / layout checkpoints.

Both loaders now use one shared export configuration helper with finite
virtual-clock capture. The selected frame runs load handlers and queued rAF,
without advancing a future timer. Scripts and assertions remain enabled.
`RadiantViewTest.FileExportsCaptureRecurringFramesWithoutAdvancingDelayedTimers`
verifies PNG, SVG and PDF output, load/rAF execution and exclusion of a delayed
1000ms timer. It and `StaticSnapshotDoesNotAdvanceTimersDuringNativeLayout`
and `FailsUnsettledSnapshotAndContinuesLayoutBatch` pass (3/3). Boldare now
loads and exports its canonical Lambda PNG; a full failed-side corpus retry
is recorded in Lambda's ignored capture manifest. Not every former timeout
is attributed to this cause.

Evidence: Lambda `temp/online-recovery/ri017-tests.log`, original
`saas_landing_003/sample.txt`, and
`test/online/snapshots/saas_landing_003-lambda.png` with its capture logs.


<a id="ri-013"></a>

### RI-013 — Downloaded CSS overrides later document styles

**Status:** RESOLVED, 2026-11-11. **Lambda blocker:** cleared for the public
Kian Hup login rendering task. Ownership: **D9.1.1v4 / D9.1.3v2**.

`process_css_resource()` appended a downloaded sheet without its link owner
and bypassed inline styles' document-order sorting. A dynamically inserted
Bootstrap link could therefore override the template and page styles after
its download completed. The handler now sets `owner_element` and uses the
promoted `radiant_append_document_stylesheet()` helper; the duplicate append
and array-growth implementation is removed.

The owner regression
`RadiantInputValueTest.DownloadedStylesheetsUseOwnerOrderAcrossCompletionOrder`
loads completed/cached resources in reverse order around an inline style and
checks all three owners. It and the existing inline mutation/order test pass.
The Lambda host separately repairs `link.rel` reflection and parser-blocking
head insertion order under **D8.4.4 / D6.3**.

<a id="ri-014"></a>

### RI-014 — A zero-height body loses its canvas background image

**Status:** RESOLVED, 2026-11-11. **Lambda blocker:** cleared for login rendering.
Ownership: **D9.1.1v4 / D9.1.3v2**.

`lambda.exe view https://kianhup.com/login` loaded the 6x6 tile, but all body
content is absolutely positioned and the body's height is zero. Native canvas
propagation handled only color, leaving the image's painting area empty.
The shared background recorder now extends the selected canvas image layer's
painting area to the viewport. Canvas source selection also checks root
images before propagating a body background, rather than checking color alone.
Image positioning remains based on the existing box geometry; transformed,
contained and fragmented canvas backgrounds are outside this repair's checks.
The relevant CSS rule is [CSS Backgrounds 3 §2.11](https://www.w3.org/TR/css-backgrounds-3/#special-backgrounds).

Lambda integration fixture `canvas_body_background` passes its viewport pixel
assertions beyond a zero-height body and verifies that a root background color
suppresses body propagation. PNG and SVG exports retain the tile through the
shared recorder. Live comparison evidence is in
`../../lambda/temp/kianhup-style/native-final.png` and `browser.png`.

<a id="ri-015"></a>

### RI-015 — Authored inline buttons bypass atomic button layout

**Status:** RESOLVED, 2026-11-11. **Lambda blocker:** cleared for login rendering.
Ownership: **D9.1.1v4 / D9.1.3v2**.

With `button { display:inline; font-size:14px; height:2.4em }`, inline dispatch
bypassed the existing HTML button used-display transformation in
`layout_block.cpp`. Inline decoration supplied a 30px box instead of the
authored 33.6px height; the surrounding login panel was 11.68px too short.
The helper is now shared through `layout.hpp` and applied before dispatch,
so inline buttons reach their existing atomic layout and paint code.

The `canvas_body_background` integration fixture checks the 33.6px button
height. The live panel measures 231.800px versus Chromium's 231.78125px and
the button 33.600px versus 33.59375px. A 0.574px button text width difference
remains as font-metric residue; heading and input widths match. Integer-cast
layout lint passes. Geometry and replay logs are under
`../../lambda/temp/kianhup-style/`.

<a id="ri-011"></a>

### RI-011 — Cloned multicol text trim has a pre-existing capacity mismatch

**Fixed for auto-height horizontal inline flow, 2026-11-22.** The cloned text
block used a
full line advance for the final line in each column, even though its cloned
block edges trim leading again. Line projection now fits the trimmed extent.
Fragment boxes and following siblings use the consumed flow through the last
trimmed line (**D9.1.1v4 / D9.1.3v2**).

`wpt-css-inline/text-box-trim-multicol-003` and the owner regression
`ClonedTextBoxTrimFitsFourLinesInEachFullColumn` place the text in groups of
four, four and two, with the following block at the Chromium coordinates.
Seven line-count controls match Chromium 148 for text positions and the
cloned box width. Of these, three retain the existing difference in how an
independent following block splits across columns. No layout reference is
updated. The 13 stored multicol trim comparisons pass on both hosts.
The original comparison is in `temp/radiant-layout-20261111/`. New evidence
is in `temp/radiant-ordinary-fixes/`.

<a id="ri-010"></a>

### RI-010 — Layout baseline mismatches after the host-path repair

**Status:** RESOLVED, 2026-11-11, macOS arm64. **Lambda blocker:** cleared.
The normal integration entry point passes all 18 categories: **5,631 passed /
293 partial / 0 failed / 12 skipped**, all 5,936 cases present. Authority:
**D9.1.1v4 / D9.1.3v2**, preserving **D5.5 / D6.3**. The user explicitly
approved the scoped 16-file macOS reference correction as an exception to
AGENTS.md rule 12. Original captures, fixture source, scored baselines and
tolerances remain intact; the native and reference repairs are described below.

**Original report and intermediate qualification:**

The reported 5,904 layout startup failures are caused by Lambda adapters using
the shared runner's default `radiant.exe` in workspaces containing `lambda.exe`.
A two-site explicit-host patch, tested in an isolated review checkout, executes
all 5,936 selected cases: 5,621 pass, 293 retain partial fidelity, 10 fail and
12 input-event cases retain their skips. The user explicitly approved the two-file ownership exception, and that
integration repair is applied; it is not a layout workaround.

Initial ten cases:

- `wpt-css-inline/text-box-trim-multicol-{002,004,006,007,008}`.
- `wpt-css-text/text-spacing-trim-end-001` and `writing-system-line-break-001`.
- `wpt-css-writing-modes/available-size-{022,023}`.
- `wpt-css-lists/list-marker-position-crash`.

Reproduce from this checkout with `node test/layout/test_radiant_layout.js --exe
../Lambda/lambda.exe --host-cwd ../Lambda -c <category> -t <case> --json`.
The exact original reports, isolated integrated host, ten retained view trees,
full comparisons and fresh Chrome captures are under
`temp/radiant-failed-20261111/`. `remaining-layout-comparisons.json` compares the
original references; `fresh-comparisons.json` compares temporary fresh captures.

Fresh capture repairs fragment ordering in the 006/008 observations without a
native change; those two references therefore need an owner-side capture audit.
For 002/004/007, remaining continuation text positions differ by 20px despite
matching element boxes. The orthogonal 022/023 cases also require standards
review: the native constrained height follows their WPT reftest's scrollport
constraint, whereas current Chrome selects the larger viewport fallback under
containment. See [CSS Writing Modes §7.3.1](https://www.w3.org/TR/css-writing-modes-3/#orthogonal-auto)
and the fixtures' `available-size-022-ref.html` target. The CJK/font and list-marker
causes are not yet confirmed.
No reference, baseline score, fixture selection or tolerance was changed.

The native replay-summary repair separately passes 326/326 reported UI cases,
278 view cases with one existing speech skip, and the owner-native/layout gate.

**Native layout follow-up, 2026-11-11:** `text-spacing-trim-end-001` now fits
closing punctuation using its line-end half-space before wrapping, and
`list-marker-position-crash` now preserves float source order across an empty
list-item block. Both reach 100% element/text agreement with the stored and
fresh browser captures. Three multicol trim/flow corrections bring all five
reported multicol cases to 100% element/text agreement with fresh captures.
Japanese hyphen-like punctuation now respects the content writing system,
including explicit script subtags and inline/intrinsic boundaries.

The unchanged full integration contract now reports **5,623 passed / 293 partial /
8 failed / 12 skipped**, all 5,936 cases present. The dependencies at that checkpoint were:

- Five multicol cases still fail their stored text-fragment references; their
  fresh captures fully match the repaired native geometry. Reference ownership
  and capture audit block further work on these comparisons in Radiant scope.
- `writing-system-line-break-001` retains stored font/line differences. The
  repaired Japanese box matches fresh Chrome; Chrome's `ja-Hang` box still breaks
  before U+301C, contrary to the fixture assertion and the writing-system rule
  in [CSS Text 3 §5.2](https://drafts.csswg.org/css-text-3/#line-break-property).
  Native Hangul-script text retains the prohibited boundary. Reference/font and
  browser behavior need owner review; no incorrect native break is introduced.
- `available-size-{022,023}` retain the standards/browser disagreement. Both
  native outputs have aside heights `[280,280,280,280,280,680,680,680,680]`, exactly
  matching a fresh capture of their shared WPT reftest target. Chrome captures of
  the tests instead use 680px for all nine boxes. The scrollport sizing rule is
  preserved pending resolution of that disagreement.

Qualification: **901 native tests plus static smoke**, all **4,481 owner geometry
comparisons**, seven runner contracts and layout lint pass. Seven new native
regressions cover trim geometry, flow height, punctuation, script tagging,
inline/intrinsic breaks and float ordering. See the
[implementation record](impl/Radiant_Impl_Remaining_Layout_Repairs.md) and
[pinned receipt](../radiant/temp/radiant-layout-20261111/verification-summary.json).
No sibling source, corpus reference, scored baseline, selection or tolerance
changes are included (D9.1.1v4 / D9.1.3v2).

**Reference audit follow-up, 2026-11-11:** the remaining eight comparisons have
independently generated macOS reference corrections prepared under
`temp/radiant-reference-20261111/proposal/`. Five use ordinary browser recaptures;
three use explicit, source-hashed standards controls through the existing corpus
mechanism. The two orthogonal controls reproduce all nine aside geometries and
line contents from a separate capture of the fixtures' shared WPT reference.
The CJK control applies strict breaking only to the Hangul-script string, giving
the required boundary without changing source text or prescribing geometry.
Unmodified Chrome outputs are rejected by all three corrected standards oracles.

Full/compact parity and the unchanged 5,936-case integration inventory pass in
a temporary overlay: **5,631 passed / 293 partial / 0 failed / 12 skipped**.
The 16-file additive patch preserves every existing reference and affects only
macOS selection. At that checkpoint, applying it awaited an explicit exception
to AGENTS.md rule 12, and the real corpus still had eight failures. The user then
approved the reviewed scope; exactly those 16 additions were applied and their
hashes verified without altering other dirty corpus files. See the
[review and patch](../radiant/temp/radiant-reference-20261111/REVIEW.md).

The standards review additionally completed native Hanb/Bopo/Hira/Kana script
recognition and added an orthogonal containment sizing regression. The focused
script checks fail before and pass after the mapping repair. The final owner
gate passes **902 native tests plus smoke, all 4,481 geometry comparisons and
seven runner contracts**; layout lint and its five contracts pass. This native
follow-up needs no reference exception (D9.1.1v4 / D9.1.3v2).

**Final applied qualification:** the real Lambda `test/test_run.js` layout
integration group now exits zero with the same **5,631 passed / 293 partial /
0 failed / 12 skipped** result as the isolated proposal. This run uses ordinary
reference resolution, with no temporary overlay. Every newly added file matches
its reviewed SHA-256; the corpus status delta contains exactly those 16 paths.
The eight corrected cases have 100% element/text agreement in both full and
compact formats. Existing partial cases and skips remain visible. Native source
and executable hashes still match the passing owner gate described above.

Final reports, explicit authorization and applied-reference identities are in
`temp/radiant-reference-20261111/applied-integration/`, `authorization.json` and
[verification-summary.json](../radiant/temp/radiant-reference-20261111/verification-summary.json).
The unrelated pre-existing clone-capacity mismatch remains open as RI-011.
Changes remain unstaged and uncommitted.

<a id="ri-004-pointer-event-protocol"></a>

### RI-004 — Lambda pointer-event adapter expects an absent native event protocol

**Status:** RESOLVED, 2026-11-10, macOS arm64. **Lambda blocker:** the original
release adapter and complete host now build and link from current sources. Authority:
**D9.1.1v4 / D9.1.3v2**. This report retains its original ID separately from
the older layout RI-004 below; its descriptive anchor preserves unambiguous links.

**Original reproduction:** Lambda starting HEAD `aee8f3242`, adapter last changed
in `41dc5ccc5`, Radiant `558eb3e`. From Lambda, after normal Premake generation:
`make -C build/premake config=release_native -f lambda-radiant-adapter-cpp.make LDDEPS= -j2`.
`lambda/module/radiant/event.cpp:8594–8624` expected `pointer_type`, pressure,
pen axes, `samples`, `RdtPointerSample`, `rdt_pointer_sample_of`,
`RDT_POINTER_MOUSE` and `RDT_POINTER_SAMPLE_CAPACITY`, but the owner header's
`MousePositionEvent` carried only position/movement. Compilation stopped after
20 missing-field/type/constant errors. The runtime archive built, while host
linking and execution qualification stopped. Original evidence remains in
Lambda's `temp/mvp_full_mir_retirement/adapters.log` and
`vibe/impl/Lambda_Impl_MIR_Full_Retirement.md`.

**Confirmed cause and repair:** the owner checkout lagged its native pen
protocol. Radiant commit `2a86f24` adds the complete protocol, platform capture,
bounded coalescing and replay; merge `7106a48` brings it into this checkout.
The existing Lambda consumer agrees with those definitions and its original
release build now succeeds. No further ABI edit or adapter fallback is needed.
Samples remain borrowed only during synchronous dispatch; Radiant's archive
continues to depend on lambda-input, with scripting supplied by the host
(D5.5.1, D6.3.3, D9.1.1v4, D9.1.3v2).

**Regression coverage:** five native cases in
[`test_pointer_input_gtest.cpp`](../radiant/test/test_pointer_input_gtest.cpp) verify
mouse initialization after pen input, pen axes and chronological samples,
separate device/pointer batches, bounded overflow/reset and borrowed samples
during reentrant submission. The owner Makefile discovers the suite with the
other native tests. All five pass; a fresh debug host links and its HTML replay
passes eight pen/eraser/mouse axis assertions through the unchanged adapter.
The canonical full release build also succeeds against Lambda `fd07a893f`:
`make -C ../lambda build-lambda-split SPLIT_PROFILE=release SPLIT_OUTPUT=../radiant/temp/ri004/lambda-release.exe JOBS=4`.
That release host passes the same eight assertions with each of
`JS_EXEC_BACKEND=ast` and `JS_EXEC_BACKEND=mir` (16/16). Both hosts were built
under `temp/ri004/`; the public Lambda executable was not replaced.

**Qualification limits:** `make -j4 PROFILE=debug run-radiant-baseline` passes
823 native tests plus the static corpus smoke test, with 45 existing skips.
The aggregate exits 2 because the shared corpus checkout `a64ed85dc` lacks the
`--host-cwd` option and related runner contracts expected by this Radiant tree:
six of seven runner tests fail, and geometry comparisons do not execute.
The existing Lambda-script pen fixture separately exits 255 on parser E100 at
`test/demo/paint/tests/_pen_input.ls:48`, before event delivery. Both failures
are retained; no fixture, assertion or reference was weakened. Broader Lambda
MIR retirement correctness and performance qualification remain separate.

Evidence: [`temp/ri004/`](../radiant/temp/ri004), including `adapter-release.log`,
`pointer-tests.xml`, `host-debug.log`, `pointer-adapter-result.json`,
`build-release.log`, `release-link.log`, `release-replay-summary.json`,
`pen-replay.log`, `radiant-baseline.log`, `baseline-summary.json` and
`verification.json` (source and artifact identities).

<a id="ri-003"></a>

### RI-003 — Startup fragment navigation is followed as a document redirect

**Status:** FIXED in Radiant, 2026-11-09; full Lambda source rebuild remains
blocked independently. **Observed:** 2026-11-09, macOS, debug Lambda host.
**Lambda blocker:** the fragment loop is resolved for the two same-document
timer replays (D9.1.1v4, D9.1.3v2); retain the original Lambda unit cases.

From the Lambda checkout, create an HTML file under `temp/` containing
`<div id="result"></div><script>setTimeout(function(){document.getElementById('result').setAttribute('data-timer','yes')},0);location.replace('#result');</script>`.
Run `./lambda.exe view <file> --headless --event-file <events.json> --event-result <result.json>`
with an event that asserts `#result` has `data-timer` equal to `yes`. The host
exits 255 without a result; it logs `load_html_doc: too many document redirects`.
The equivalent in-process Lambda tests for relative and absolute same-document
navigation pass. The retained reproduction and log are under
`Lambda/temp/radiant-adapter-test-migration/fragment.{html,json,log}`.

**Confirmed cause and repair:** both redirect loops in
[`cmd_layout.cpp`](../radiant/radiant/cmd_layout.cpp) treated startup fragment navigation
as a replacement document. They now share `document_resolve_navigation_redirect`,
which uses input's `url_equals_without_fragment` and updates the existing URL
carrier in place. It clears the consumed request so the next viewer turn cannot
reload it. Document, DOM, pool and host-owned script/timer state remain retained
(D5.5.1, D6.3.3). Query/path changes and same-URL requests without a fragment
still redirect or reload.

**Verification:** the two original relative/absolute CLI replays failed with
exit 255 before the repair. All eight
[`test_navigation_cli.py`](../radiant/test/test_navigation_cli.py) cases pass afterward:
relative/absolute/repeated/empty fragments, cross-document and query redirects,
and relative/absolute `layout` output retaining timer-produced text. The six
`view` replays pass 12 assertions. Eight native loader regressions verify retained
ownership and redirect decisions. `make test-radiant-baseline` passes **770**
tests across **45** runners with **45 existing skips** (RI-001, RI-002 and the
hardware-dependent MIDI case); `make test-native-closure` also passes without
Lambda runtime or DOM libraries. Input facade and diff checks pass.

**Host qualification limit:** `make -C ../Lambda build` still fails on ambiguous
`direct_call`/`expression` overloads in Lambda's `mvp_mir.cpp`. Before/after CLI
checks used separately linked debug executables with current Radiant/input
archives and the same last-successful Lambda runtime/adapter archives. A full
current-source host rebuild remains pending. Reproduce the integration checks
with `make test-navigation-cli LAMBDA_HOST=../Lambda/lambda.exe` after that
independent build issue is repaired. Logs, binary/archive hashes and the
isolated executables are under `temp/ri-003/`; replay artifacts are under
`temp/navigation-cli/`.

<a id="ri-004"></a>

### RI-004 — Dynamic baseline exposes native anonymous-table and match-parent mismatches

**Status:** FIXED in Radiant, 2026-11-09; the three match-parent comparisons
are confirmed unsupported-browser references. **Observed:** 2026-11-09, macOS
debug Lambda integration, shared corpus `lambda-test` at `a64ed85dc`.
**Lambda blocker:** the native defects in this entry are resolved; the complete
integration suite still has independent failures, including those references.
Radiant retains native layout and generated-content ownership; Lambda remains
the optional scripting host (D9.1.1v4, D9.1.3v2, D5.5.1).

**Original reproduction:** from Lambda, run `LAMBDA_ROOT="$PWD" node
test/layout/test_radiant_layout.js -c baseline-js --baseline-only -j 3
--global-concurrency 3 --batch-size 100 --json`. Initially it reported
598/630 passing; the separate Lambda `classList` fix raised this to 599/630.
The native cases were `table-anonymous-objects-117`–`120`, `169`–`170`,
`185`–`188`, `193`–`196`, and `205`–`208`. Case `117` matched only 8/11
elements; `text-align-match-parent-02`–`04` matched only 13/17 text nodes.
The fixtures had moved from `baseline` to `baseline-js` without content changes.
Retained initial evidence: [full reproduction](../radiant/temp/radiant-failed-tests/baseline-js-reproduction.json),
[fresh-reference comparison](../radiant/temp/radiant-failed-tests/fresh-reference-comparison.json),
and [previous shared-runner results](../radiant/temp/radiant-failed-tests/final-reported-gates/test_radiant_layout_baseline_results.json).

**Confirmed causes and repairs:**

- [`intrinsic_sizing.cpp`](../radiant/radiant/intrinsic_sizing.cpp) did not recognize
  an inline outer display with table inner display as atomic before its first
  layout. Text lookahead descended into its cells, interpreted them as a forced
  line boundary, and removed the preceding space. Treating this display pair
  as atomic preserves the separator and adjacent-node geometry.
- A generated table-cell pseudo is reparented under anonymous table fixups.
  On retained reflow, [`layout_block.cpp`](../radiant/radiant/layout_block.cpp) looked
  up its content on that wrapper and replaced it with empty text. It now uses
  the existing `dom_source_parent()` helper to find its originating element.
  [`layout_list.cpp`](../radiant/radiant/layout_list.cpp) uses the same source ancestry
  for the pseudo's counter operations. Retained nodes and property ownership
  remain in their existing view-tree epoch (D5.5.1).
- [`view_pool.cpp`](../radiant/radiant/view_pool.cpp) now omits anonymous table wrappers
  below inline parents as it already did below block parents, preserving
  authored nodes and zero-size table markers in exported snapshots.

**Match-parent classification:** Headless Chrome 148.0.7778.97 reports
`CSS.supports('text-align', 'match-parent') === false`. Its original captures
therefore inherit logical start/end alignment and use the child's direction.
Radiant correctly resolves against the parent's direction, as required by
[CSS Text 3 §6.1](https://drafts.csswg.org/css-text-3/#valdef-text-align-match-parent).
With Chrome's supported `-webkit-match-parent` spelling applied only in a
temporary browser control, all 16 text positions in each of the three fixtures
agree with Radiant within 0.2 CSS pixels. Native tests cover parent-direction,
child-class and parent-width mutations. No alignment implementation, corpus,
reference, threshold or baseline expectation was changed. The three original
browser-reference comparisons remain failed pending corpus-owner correction.

**Verification:**

- All 18 named table fixtures now match **100% elements and text** against
  unchanged references. Native `207` rendering was visually checked against
  Chrome: the overlaid text is green, including the retained generated cell.
- All six [`DynamicLayoutTest`](../radiant/test/test_layout_dynamic_gtest.cpp) cases
  pass, including whitespace geometry, marker/snapshot structure, generated
  content and counter ownership across reflow, and the alignment mutations.
  Build with `make build/test/test_layout_dynamic_gtest`, then run
  `./build/test/test_layout_dynamic_gtest`.
- The native GTest and static-layout smoke portions pass: **794 tests,
  45 skips, no native failures**. The standard aggregate now also runs the
  geometry comparisons below, whose remaining failures keep it red.
- Full browser-geometry comparisons, using three host processes and batches
  of up to 100, improve `baseline-js` from **599/630 to 617/630** and static
  `baseline` from **3,786/3,851 to 3,800/3,851** (six static skips). No case's
  element or text score regressed. The remaining **13 dynamic / 51 static**
  failures are retained; these complete comparison suites are not green.
- Input-facade and diff checks pass. Direct `no-int-layout-decl` scanning has
  zero findings, and the changes add no integer casts. The documented
  `no-int-cast-radiant` rule is absent from the current Lambda lint configuration;
  its wrapper was also rejected by automatic approval review for sibling-temp
  writes. The optional type-aware checker lacks Python `clang` bindings.

Detailed results, unchanged-host comparisons, binary hashes and browser controls
are in [`temp/ri-004/qualification.json`](../radiant/temp/ri-004/qualification.json),
[`browser-alignment.json`](../radiant/temp/ri-004/browser-alignment.json),
[`radiant-baseline.log`](../radiant/temp/ri-004/radiant-baseline.log), and
[`lint-result.json`](../radiant/temp/ri-004/lint-result.json). Integration used an isolated
debug host relinked with the repaired Radiant archive and existing Lambda
runtime/adapter archives; a full Lambda source rebuild was not part of this
native repair. Initial invocations from Radiant lacked Lambda's package/WPT
roots; final comparisons launch from Lambda with all scratch/cache/log output
redirected into Radiant's `temp/ri-004/`.

**Standard test integration:** `make test-layout-baseline` now invokes the
existing shared `test/layout/test_radiant_layout.js` directly for both suites;
`make test-radiant-baseline` includes it. The shared runner's `--host-cwd` option
lets the external host resolve its package/WPT resources while all scratch files
stay in Radiant's `temp/`. Combined-suite reporting now propagates unscored
geometry failures and missing suites to the CLI exit code. Five maintained
runner-contract tests pass. The [standard report](../radiant/temp/layout-baseline/results.log)
retains the **51 static / 13 dynamic** failures and the aggregate exits nonzero;
the [full log](../radiant/temp/layout-integration/standard-radiant.log) also records the
passing native portions. The temporary RI-004 wrapper is no longer needed.

**Follow-up:** the historical 51 static / 13 dynamic failures above are now
resolved and the standard aggregate is green; see [RI-005](#ri-005).


<a id="ri-005"></a>

### RI-005 — Native static layout baseline needs execution qualification

**Status:** FIXED, 2026-11-09, macOS arm64 debug. The original **51 static /
13 dynamic** failures now pass through the existing standard layout suites.
**Lambda blocker:** none for these comparisons; the external host remains a
separate integration dependency. RI-006 remains independent.

The original qualification ran the static smoke corpus successfully but left
`baseline` at **3,800/3,851** and `baseline-js` at **617/630**. The standard
aggregate correctly returned failure. This follow-up repairs the underlying
layout and reference defects without changing fixtures, thresholds or coverage.

**Native repairs:** preserve aspect-ratio transfers and assigned sizes through
late absolute/flex/grid passes; retain fractional grid tracks and use the grid
area as the percentage containing block. Correct intrinsic percentage padding,
negative-indent forced breaks, emergency wrapping during balanced text probes,
float fit and clearance, and the multicolumn leading-margin simulation and
balanced-height floor. Table row minima exclude cyclic percentage cell heights;
empty-cell baselines and auto-inset static positions follow content alignment.
Inline bounds include decorated empty children, broken-image fallback text
preserves following spaces, bidi preserves visible interior spaces, and emoji
clusters retain their variation-selected font metrics. Counter operations ignore
boxless elements; reversed counters include their originating element and
following sibling scope, replacing same-name counters across intervening
sibling scopes.

The existing [`DynamicLayoutTest`](../radiant/test/test_layout_dynamic_gtest.cpp) suite
now contains **31 passing cases**, including 25 new focused regressions. No new
runner was introduced. `make test-layout-baseline` continues to run the shared
`test/layout/test_radiant_layout.js` for `baseline` and `baseline-js`, and
`make test-radiant-baseline` combines those comparisons with native tests and
the runtime-independent static smoke pass (D9.1.1v4 / D9.1.3v2).

**Reference qualification:** 23 full/compact reference pairs were recaptured
with the existing extractor after checking fixture fonts, WPT resources,
post-mutation geometry, source-order multicolumn fragments and bidi ranges.
Each was compared against native output before installation. Ten additional
full/compact standards-control pairs retain the original fixtures and browser
captures while explicitly compensating for Chrome 148 gaps: unprefixed
`text-align:match-parent`, prohibited atomic GL/WJ/ZWJ wrapping, and reversed
custom-counter initialization across sibling scopes. Controls record the source
SHA-256, specification, transformation and reason; the existing runner rejects
stale controls. Full/compact parity passes for all **33 pairs**. Comparison
normalization now preserves every fragment when differently wrapped source runs
cannot be partitioned, reporting differences instead of throwing. Browser
capture scratch honors `LAMBDA_TEMP_DIR`.

**Verification:**

- `caffeinate -is make PYTHON=/usr/bin/python3 test-radiant-baseline -j8
  LAMBDA_HOST=<qualified external host> LAYOUT_BASELINE_CONCURRENCY=3` exits 0:
  **819 native/smoke tests passed, 45 existing native skips; 3,851 static and
  630 dynamic geometry comparisons passed, zero failures, six existing static
  skips**. Coverage and element/text thresholds are retained; tolerance is 5px.
- All seven maintained runner/reference contract tests pass. Input-facade and
  both repository diff checks pass. Direct `no-int-layout-decl` scanning and
  the existing libclang `int-cast-type-aware` checker report zero findings
  across the changed layout files. Existing multiline `INT_CAST_OK` markers
  were moved onto the cast lines so the checker recognizes them. The legacy
  `make lint --rule ^no-int-cast-radiant$` wrapper is not a qualification gate:
  that rule is absent and its unconditional fixture helper tries to write into
  sibling scratch; the relevant existing checks were run directly with local
  scratch instead.

The [standard report](../radiant/temp/layout-baseline/results.log),
[full aggregate log](../radiant/temp/layout-repairs/standard-radiant.log), and
[qualification receipt](../radiant/temp/layout-repairs/qualification.json) retain the
counts, fixture identities, browser controls and executable provenance. The
isolated debug host was relinked with Radiant and rebuilt existing Lambda
runtime/adapter archives; no Lambda runtime source changes were required.
All scratch, logs and browser profiles used Radiant's `temp/`.

**Current workflow, 2026-11-10:** the standard target is now
`make run-radiant-baseline`. It builds and runs `radiant.exe` for both geometry
suites; that executable links the existing Lambda scripting adapters, while the
native Radiant archive remains runtime-independent. The full target again
passes 3,851 static and 630 dynamic comparisons with six existing static skips;
the native/smoke portion passes 819 tests with 45 existing skips. See
[`temp/radiant-standalone-baseline-final.log`](../radiant/temp/radiant-standalone-baseline-final.log).


<a id="ri-001"></a>

### RI-001 — HTML/CSS integration cases skip missing fixtures

**Status:** OPEN — test coverage gap. **Observed:** 2026-11-09, macOS,
`make run-radiant-baseline` (debug). **Lambda blocker:** not established.

The owner-local `test_html_css_gtest` run skipped 19 cases because fixture files
could not be loaded. For example, `LoadSimpleBoxTestHTML` could not load
`test/html/simple_box_test.html`, and `LayoutData_Flex_NestedContent` reported
a missing layout file. The remaining 30 cases passed. Reproduce with
`./build/test/test_html_css_gtest` after building the baseline. Determine
whether the fixtures should be supplied or the test paths corrected, and verify
the affected cases execute; this report does not establish a rendering defect.

**Resolution (2026-11-11):** FIXED under D9.1.1v4 / D9.1.3v2.
The 19 skips reproduce before the change. Most named fixtures had moved into
`baseline` (one into `page`); their test paths now follow the actual corpus.
The absent `simple_box_test.html` is supplied in Radiant's `test/html/`, and
the missing float/clear page now uses the existing static clear-float fixture.
Required single-file loads now assert rather than skip. No corpus file or
reference was changed. `test_html_css_gtest` passes **49/49, zero skips**.
Before/after receipts: `temp/ledger-html-{before,after}.{log,json}`.


<a id="ri-002"></a>

### RI-002 — DOM integration cases skip unsupported standalone node creation

**Status:** OPEN — test coverage gap. **Observed:** 2026-11-09, macOS,
`make run-radiant-baseline` (debug). **Lambda blocker:** not established.

`test/css/test_css_dom_integration.cpp` contains 25 existing skipped cases
that report standalone node creation is no longer supported. Reproduce with
`./build/test/css/test_css_dom_integration` after building the baseline.
Review the supported document-owned creation path and restore equivalent
coverage where applicable, without removing or hiding the skipped cases.
The separate MIDI live-device skip is conditional on hardware and is not
classified as a defect here.

**Resolution (2026-11-11):** FIXED under D5.5.3, D9.1.1v4 and
D9.1.4v2. All 25 skips reproduce before the change. The same named cases now
create native-backed text/comment/doctype nodes in the fixture's document,
using the document-owned detached factories or backed parent append helpers.
Text updates and insertion use the supported backed mutation APIs, preserving
both the Mark list and sibling chain. All original coverage families execute;
additional assertions check ownership, backward links, tails and backing order.
`test_css_dom_integration` passes **139/139, zero skips**. No input-owned source
or test changed. Before/after receipts: `temp/ledger-dom-{before,after}.{log,json}`.


<a id="ri-007"></a>

### RI-007 — Column flex measures a nested wrapping row too short

**Status:** OPEN — reproduced in static HTML; native cause unconfirmed.
**Observed:** 2026-11-10, macOS debug `lambda.exe view --headless`.
**Lambda blocker:** no. The paint demo (`test/demo/paint/`) gives its toolbar
rows that do not depend on this case.

A `flex-direction: column` container holds a wrapping row (`flex-wrap: wrap`).
One child of that row is itself a wrapping row limited by `max-width`. The
column sizes the flexible item after it as if the inner row did not wrap.
The outer row is then laid out taller, so the items after it overflow the
column by the difference.

Reproduce from the Lambda checkout with
`./lambda.exe view temp/radiant-issues/flex_col4.html --headless --event-file temp/radiant-issues/flex_rects.json`
(the rect assertions are expected to fail; their messages print the rectangles).
In a 1400 × 960 viewport, the toolbar `#bar` is laid out at 102 px. The
`flex: 1` stage gets 858 px, which is 960 − 74 − 28: the column used a 74 px
toolbar, the height without the inner wrap. The 28 px footer starts at y = 960,
outside the container. Expected: a 830 px stage and the footer at y = 932.
`flex_col6.html`, which is the same page with the outer row set to `nowrap`,
lays out correctly (96 + 836 + 28 = 960). So does the page without the inner
wrapping row (74 + 858 + 28). The hypothetical main size of the outer row
probably comes from a measurement pass that does not wrap the inner row at its
`max-width`. That cause is unconfirmed. No Radiant source was changed.

**Resolution (2026-11-11):** FIXED under D9.1.1v4 / D9.1.3v2
(native layout ownership). Confirmed cause: the wrapping-row intrinsic-height
walk measured each child using the parent's full width, even after computing
a narrower hypothetical child width from `max-width`. The inner row therefore
reported a single-line height during column flex sizing. The query now receives
the child's content width derived from its hypothetical border box; percentage
resolution retains the parent's containing-block basis.

`DynamicLayoutTest.ColumnFlexMeasuresNestedWrappingRows` first reproduces the
reported **102 / 858 / footer y=960** mismatch, then verifies **102 / 830 /
footer y=932** after repair. It also checks retained recascade/reflow after
narrowing the inner maximum. The nowrap-toolbar and nowrap-child controls pass.
The complete dynamic native suite passes **35/35**. Receipts:
`temp/ledger-flex-before.{log,json}`, `temp/ledger-layout-after.{log,json}`.
The restored owner-local layout-cast gate passes (see RI-009). Aggregate
qualification is recorded below.


<a id="rad22-7"></a>

#### RAD22-7

**GIF finish leaves a dangling `surface->pixels` briefly.** `gif_animation_finish` frees the frames, after which `surface->pixels` points at freed memory until it is nulled two lines later (`gif_player.cpp:55-64`) — a narrow window, but any interleaved read during teardown would be a use-after-free.

**Resolution (2026-11-11):** FIXED under D5.5.1 / D9.1.1v4.
The current `GifAnimation::finish` still freed `GifFrames` before switching
`surface->pixels` away from the borrowed frame. Teardown now restores the
cache-owned still (including pitch, dimensions, alpha mode and generation),
or detaches pixels, before releasing frames. This closes the transient dangling
reference; no concurrent-reader failure was reproduced or claimed.
`test_gif_player_gtest` passes **23/23**, including finish/cancellation controls.
Receipt: `temp/ledger-gif-after.{log,json}`.


<a id="rad01-5"></a>

#### RAD01-5

**Class-name handling is still a single-token stub.** The deprecated `RDT_VIEW_MATH` enum span has been removed, but the class-name path in `view_pool.cpp` still assumes a single class ("TODO: split on whitespace for multiple classes").

**Reverified (2026-11-11):** RESOLVED by existing code under D9.1.1v4.
`view_pool.cpp` now shares `append_class_list_json` across dump paths and walks
whitespace-separated tokens; selector-name output also splits the class list.
The reported single-token TODO no longer exists. This is a source audit of the
reported stub, not a claim about every DOM class mutation path.


<a id="rad02-4"></a>

#### RAD02-4

**No named colors in the HTML-attr parser.** `parse_html_color` (`resolve_htm_style.cpp:90`) handles hex/`rgb()` only, with a TODO for named colors (`resolve_htm_style.cpp:100`). Because presentational attributes use this parser and CSS uses `resolve_color_value`, a named color works in a stylesheet but not in a `bgcolor` attribute.

**Reverified (2026-11-11):** RESOLVED by existing code under D9.1.1v4.
`parse_html_color` delegates to input's `css_parse_color`, including named
colors. The added native `HtmlColorAttributesUseNamedCssColors` control passes
for body/table-cell backgrounds and mixed-case font color attributes.
Receipt: `temp/ledger-layout-after.{log,json}`.


<a id="rad02-6"></a>

#### RAD02-6

**Two color parsers.** `parse_html_color` (htm) vs `resolve_color_value` (css) — see item 4. Consolidating would remove a class of "works one place, not the other" bugs.

**Reverified (2026-11-11):** RESOLVED by existing code under D9.1.1v4.
The HTML attribute adapter uses the shared CSS color parser; it no longer
contains an independent hex/rgb parser. The RAD02-4 native color control passes.
This closes parser duplication, not full legacy HTML color-algorithm coverage.


<a id="rad05-1"></a>

#### RAD05-1

**Dead `IntrinsicSizeCache` struct.** The header struct (`layout.hpp`), including its four-slot per-width `HeightCacheEntry` height cache, has zero references anywhere. The real cache is the four `DomElement` fields (`dom_element.hpp:434-437`) and caches **width only**. *Improvement:* delete the struct, or wire up its height cache to fix issue 2.

**Reverified (2026-11-11):** RESOLVED by existing code under D9.1.1v4.
`IntrinsicSizeCache` and its unused `HeightCacheEntry` are absent from current
Radiant sources. The live intrinsic-height query has its own owner-scoped cache;
its scope/remaining measurement correctness is separate from this dead-struct
report. No production edit was needed.


<a id="rad06-8"></a>

#### RAD06-8

**Legacy `@font-face` cruft in the font bridge.** `font_face.cpp:341` `parse_font_face_rule_OLD` is a dead lexbor path (fonts are RAD07's domain, cross-linked here only because it is reached from the inline text path via `setup_font`).

**Reverified (2026-11-11):** RESOLVED by existing code under D9.1.1v4.
`parse_font_face_rule_OLD` and its commented lexbor implementation are absent
from `font_face.cpp`. This duplicate report and RAD07-2 are closed together;
no new font capability is claimed.


<a id="rad07-2"></a>

#### RAD07-2

**Legacy dead code in `font_face.cpp`.** `parse_font_face_rule_OLD` (`font_face.cpp:341-385`) is an entire lexbor-dependent function, including a hardcoded "Liberation Sans" registration and a `rule == NULL` "hardcoded @font-face" path, left inside a block comment. It is dead but clutters the file and encodes a stale design. *Improvement:* delete it.

**Reverified (2026-11-11):** RESOLVED by existing code under D9.1.1v4.
The reported dead legacy function and hardcoded registration block are absent
from current `font_face.cpp`; the native archive builds without them.
See the duplicate RAD06-8 record.


<a id="rad08-1"></a>

#### RAD08-1

**Fixed-size global measurement cache.** `static MeasurementCacheEntry measurement_cache[1000]` with `static int cache_count` (`layout_flex_measurement.cpp:555`) is a process-global, capacity-capped array ("Fixed size for simplicity") — not per-document. Overflow logs an error and drops the entry (`:571`); a document with more than 1000 measured nodes silently loses caching, and there is a theoretical cross-document bleed risk mitigated only by the generation counter. *Improvement:* a per-`ViewTree` hash-map cache with real eviction.

**Reverified (2026-11-11):** RESOLVED by existing code under D5.5.1 /
D9.1.1v4. Flex measurement entries, capacity and generation now belong to
`ViewTree`. `ensure_measurement_cache_capacity` uses `lam::mem_grow_array`
and logs allocation failure; the global 1000-entry array is absent.
This source audit closes the fixed-global-cap report, not every retained-cache
invalidation concern (RAD04-4R remains open).


<a id="rad08-4"></a>

#### RAD08-4

**`has_flex_item_prop` duplicated across all three files.** It is re-defined as a `static` in `layout_flex.cpp:39`, `layout_flex_measurement.cpp:438`, and `layout_flex_multipass.cpp:268`, and the measurement file additionally carries its own `flex_measurement_direction_is_row` (`:68`) parallel to `is_main_axis_horizontal`. Divergence between copies is a live risk. *Improvement:* hoist the shared predicates into `layout.hpp`.

**Reverified (2026-11-11):** RESOLVED by existing code under D9.1.1v4.
`has_flex_item_prop` has one shared inline definition in `layout.hpp`, consumed
by all three flex translation units. The duplicate direction predicate is also
absent. The archive and all three nested-flex controls build and pass.


<a id="rad14-4"></a>

#### RAD14-4

**Fixed clip-stack depth in the ThorVG backend.** `RDT_MAX_CLIP_DEPTH` is hard-coded to 8 (`rdt_vector_tvg.cpp:1461`); overflow is logged and the clip dropped (`:1476`). Deeply nested SVG clip paths beyond depth 8 silently lose clipping.

**Reverified (2026-11-11):** RESOLVED by existing code under D9.1.1v4.
The ThorVG backend uses a thread-local inline clip stack that grows on demand
via `ensure_clip_capacity`; `RDT_MAX_CLIP_DEPTH` and the depth-eight clip-drop
branch are absent. Allocation failure is logged. This is source verification of
the reported fixed cap, not qualification of every nested SVG clip combination.


<a id="rad16-2"></a>

#### RAD16-2

**Hard parser caps silently truncate.** `parse_keyframes_content` caps at 64 stops and 32 declarations per stop (`css_animation.cpp:343`/`347`), with 64-byte name and 256-byte value buffers (`css_animation.cpp:413`/`434`). Overflow is dropped without a warning. *Improvement:* at least `log_warn` on overflow, or grow dynamically.

**Reverified (2026-11-11):** RESOLVED by existing code under D9.1.1v4.
`parse_keyframes_content` grows stop/property/offset arrays with
`lam::pool_grow_array` and duplicates names/values at their actual lengths.
The 64-stop, 32-declaration and fixed-buffer truncation paths are absent.
This source audit closes those specific hard-cap reports; it does not qualify
all CSS animation syntax or property coverage (RAD16-3 remains separate).


<a id="rad17-3"></a>

#### RAD17-3

**`s_in_batch` is a file-static global**, not a `DocState` field. It gates whether mutation-time asserts fire, so it is not per-document and is unsafe if multiple documents or threads mutate state concurrently. *Improvement:* move it into `DocState` alongside `active_cascade_depth`/`transition_depth`.

**Reverified (2026-11-11):** RESOLVED by existing code under D9.1.1v4.
The batch guard is `DocState::state_batch_depth`; `state_begin_batch` increments
it and `state_end_batch` publishes only at the outermost boundary. The added
`BatchesNestIndependentlyAcrossDocuments` control verifies nested/interleaved
batches on two documents and an unmatched end, without cross-document changes.
The state-store suite passes **38/38**. Receipt: `temp/ledger-state-after.{log,json}`.


<a id="ri-009"></a>

### RI-009 — Required standalone layout lint target is absent

**Status:** OPEN — verification tooling gap. **Observed:** 2026-11-11,
macOS arm64. **Lambda blocker:** no. Authority: D9.1.1v4 / D9.1.3v2.

Reproduce from Radiant with
`make lint ARGS='--rule ^no-int-cast-radiant$'`: Make exits 2 because the owner
Makefile has no `lint` target. The sibling Lambda pattern-rule inventory also
lacks that exact rule. An attempted type-aware fallback through
`../Lambda/utils/lint/tidy/explicit_cast_check.py radiant/intrinsic_sizing.cpp`
exits before scanning because this Python environment lacks `clang.cindex`.
Expected: an owner-local, runnable layout-cast gate rather than a silent empty
rule selection. This blocks lint qualification of RI-007, not its focused
geometry checks. No integer geometry or integer casts were added by that fix.
Evidence: `temp/ledger-layout-lint.log`, `temp/ledger-layout-type-lint.log`.

**Resolution (2026-11-11):** FIXED under D9.1.1v4 / D9.1.3v2.
Radiant's Makefile now owns `lint` and `test-lint`. The owner-local ast-grep
rule restores the same `no-int-cast-radiant` pattern and file scope formerly
in Lambda (`59ebad8066^:utils/lint/rules/c-cpp/no-int-cast-radiant.yml`),
without depending on sibling scripts or `clang.cindex`. The runner fails on
an empty rule selection, missing scanner, scanner errors or unmarked casts.
Same-line `INT_CAST_OK` exceptions require a reason.

The audit adds reasons to seven existing byte/count casts and the type-erased
integer node-budget payload, and moves the existing grid-count explanation
onto its cast line. No layout arithmetic changed. The required command passes:
**89 cast matches checked, zero unmarked**. Five gate contract tests pass,
including rejection of an unmarked geometry cast, a reasonless marker, a
marker on another line and an unknown rule selector.
Receipts: `temp/ledger-layout-lint-after.log`, `temp/ledger-lint-contracts.log`.


<a id="ri-006"></a>

### RI-006 — Property providers still live outside the unified Radiant archive

**Status:** RESOLVED — verified 2026-11-09 against Radiant `558eb3e`.
**Observed:** 2026-11-09, macOS arm64. **Lambda blocker:** cleared for the
current-source MVP release build and validation.
Authority: **D9.1.1v4 / D9.1.3v2**; the current Lambda
[split implementation checkpoint](impl/Lambda_Impl_Radiant_Split.md#current-checkpoint)
requires both property providers in `libradiant.a`, with no separate property
archive target.

Lambda HEAD `1540d9228429f3385c1526d3bfabc85d715a423e`; Radiant HEAD `0c664052c753b66016e34267e65fe59bd11ce6bd`.
From the Lambda checkout, reproduce with:
`make build-lambda-split SPLIT_PROFILE=release SPLIT_OUTPUT=temp/mvp_untyped_tuning_next/starting-release.exe JOBS=2`.
The native archives build, but the host link fails with undefined
`BLOCK_PROP_DEFAULT`, `BOUNDARY_PROP_DEFAULT`, `EMBED_PROP_DEFAULT`,
`GRID_ITEM_PROP_DEFAULT`, and numerous property-provider symbols.

The cause is confirmed in the owning build: `Makefile`'s `libradiant.a` rule
archives only `NATIVE_OBJECTS`; `PROPERTY_OBJECTS` remain in the separate
`libradiant-properties.a` rule. `build_targets.json` still declares the separate
`radiant-properties` target. `ar t build/lib/release/libradiant.a` contains neither
`view_prop_defaults.cpp.o` nor `view_prop_ensure.cpp.o`, while the property
archive contains both. The merged Lambda integration deliberately removed the
separate provider link and its checks require each provider once in Radiant.
Expected: the owner manifest/build produce the unified archive containing both
providers once, so the existing Lambda consumer links without restoring the
retired separate dependency.

Evidence in the Lambda checkout:
`temp/mvp_untyped_tuning_next/starting-tree.json`, `starting-build.log`,
`libradiant.a.provider-members.txt`, `libradiant-properties.a.provider-members.txt`;
full link diagnostics: `temp/build_lambda-exe-split.log`.
No Radiant source/build edits or Lambda link workarounds were made. MVP
implementation/timing was stopped under the project-boundary rule pending the
owner repair. No baseline suite was run.

**Resolution verification:** the updated owner manifest removes the separate
property target and includes both providers in the native source list. Rebuilt
`build/lib/release/libradiant.a` contains `view_prop_defaults.cpp.o` and
`view_prop_ensure.cpp.o` exactly once; the four default symbols listed above
are defined. The original Lambda split release build/link now exits zero with
no dependency workaround. Typed and untyped Fibonacci goldens pass under MVP
and Full MIR (4/4 smoke runs). This closes the archive/link blocker, not broader
language or performance qualification (D9.1.1v4, D9.1.3v2).

Verification: `Lambda/temp/mvp_untyped_tuning_next/ri003-verification.json`,
`ri003-rebuild.log`, `ri003-successful-link.log`, and `ri003-recheck-tree.json`.
The private host SHA256 is
`cda6b0f4985ac9e440494d8b86badbbbe2c7d386e86e6483e1e45b7a4fb14363`.
The original failing link log is preserved as `ri003-failed-link.log`.


<a id="ri-008"></a>

### RI-008 — font face source includes a missing alloca header

**Status:** RESOLVED — Radiant now includes the selected input header at
`lib/alloca.h`.
**Observed:** 2026-11-10, macOS arm64 debug `make build`.
**Lambda blocker:** yes — the split build cannot compile `libradiant.a`
(D9.1.1v4, D9.1.3v2).

Reproduce from the Lambda checkout with `make build`. The build reaches
Radiant's `radiant/font_face.cpp` and fails at line 11 with
`fatal error: 'lib/lambda_alloca.h' file not found`. The command supplies
`-I/Users/henryluo/Projects/lambda-input`, but that checkout has
`lib/alloca.h` and no `lib/lambda_alloca.h`; a search across the Lambda, input
and Radiant checkouts found no file with the requested name. The current input
header defines `LAMBDA_ALLOCA`. Expected: Radiant compiles against the current
selected input headers and the Lambda split build proceeds.

Radiant's three consumers now include `lib/alloca.h`, preserving the existing
`LAMBDA_ALLOCA` call sites and behavior. This updates `font_face.cpp`, the
reported blocker, and the same stale include in `render_border.cpp` and
`rdt_vector_tvg.cpp`. A focused debug compilation of all three translation
units passed.


## 2026-11-11 owner qualification

Under D9.1.1v4 / D9.1.3v2, `make -j8 run-radiant-baseline` passes:
**875 native tests plus one static corpus smoke test**, with one hardware-only
MIDI skip; **7/7 runner contracts**; **3,851 static and 630 dynamic geometry
comparisons**, with six pre-existing static corpus skips and no failures.
The 44 formerly skipped integration cases now execute. No baseline tolerance,
reference, corpus file or sibling implementation was changed.

`make lint ARGS='--rule ^no-int-cast-radiant$'` passes with 89 checked casts;
`make test-lint` passes 5/5; the input-facade and diff whitespace checks pass.
The final follow-up to the aggregate changes only cast-reason comments,
owner lint tooling/tests, build target/documentation wiring and ledger records;
production behavior remains that of the qualified aggregate. Broader imported
feature/refactoring work and source-audit-only limitations remain explicit.

Receipts: `temp/ledger-radiant-baseline.log`,
`temp/layout-baseline/results.log`, `temp/ledger-layout-lint-after.log`,
`temp/ledger-lint-contracts.log`, and the focused before/after logs above.


## 2026-11-11 continued owner repairs and re-verification

<a id="rad03-2"></a>

#### RAD03-2

**Hard-coded float-width estimate.** `layout_block.cpp:4123` uses `float float_width = 100.0f; // Conservative estimate` in place of a real float measurement, so float space reservation can be wrong for floats whose actual width differs materially from 100px.

**Resolution (2026-11-11):** removed the obsolete 100px float-width and
8px-per-character prescan gate from `prescan_and_layout_floats`. The placement
walk already stops at preceding in-flow content and lays each admitted float
out through `layout_block`; a guessed combined width cannot improve that
decision. The preliminary scan still rejects floats behind a preceding block
and avoids work when no float exists. This is removal of redundant estimation,
not a claimed newly reproduced pixel mismatch. The unchanged original controls
and repaired source both pass `FloatPrescanUsesActualGeometryAndFlowOrder`,
covering leading floats, empty preceding inlines, preceding blocks and retained
width changes through 180px, 40px and 220px. Duplicate report: RAD04-2.
Authority: **D9.1.1v4 / D9.1.3v2**.

<a id="rad04-2"></a>

#### RAD04-2

**Hard-coded float-width estimate.** `layout_block.cpp:4123` uses `float float_width = 100.0f; // Conservative estimate` instead of measuring the float, so a float's effect on available width is guessed. *Improvement:* run `layout_measure_intrinsic` for the float and use its max-content width.

**Resolution (2026-11-11):** the same obsolete prescan gate as
[RAD03-2](#rad03-2) was removed. Actual float layout remains the sizing authority;
no replacement estimate or hard-coded measurement was added. The original
geometry controls pass before and after the cleanup. Authority:
**D9.1.1v4 / D9.1.3v2**.

<a id="rad04-7"></a>

#### RAD04-7

**`KnownDimensions` counts only positive givens as known.** `layout_known_dimensions_from_block` (`layout_pass.cpp:185`) treats a `given_width` of exactly 0 as *unknown*, so a legitimately zero-width element cannot key slot 0. This is usually benign but is an implicit contract worth documenting at the call sites.

**Resolution (2026-11-11):** reproduced a real cache-key collision:
`layout_known_dimensions_from_block` returned an unknown axis for a specified
zero width or height, and the resulting entry was incorrectly reusable for an
`auto` query. It now uses the same nonnegative-given contract as the context
helper and reads properties without mutable access. Negative givens remain
unknown. `LayoutCacheTest.ZeroDimensionsRemainDistinctFromAutoCacheKeys`
fails on the original source for both axes and passes after the repair,
including actual cache store/query isolation. Receipts:
`temp/ledger-next-layout-before.{json,log}` and
`temp/ledger-next-layout-final.{json,log}`. Authority:
**D9.1.1v4 / D9.1.3v2**.

<a id="rad07-1"></a>

#### RAD07-1

**Duplicated CSS→`FontStyleDesc` mapping.** The weight/slant mapping exists twice: inline in `setup_font` (`font.cpp:109-129`) and in `font_style_desc_from_prop` (`view.hpp:437`), and a third variant maps CssEnum→`FontWeight` in `register_font_face` (`font_face.cpp:438-447`). They can drift (e.g. `font.cpp` handles `bolder`/`lighter` keywords; `register_font_face` handles only `bold`/`normal` + numeric range). *Improvement:* have `setup_font` and `register_font_face` both route through one shared mapping helper.

**Resolution (2026-11-11):** `setup_font` already called
`font_style_desc_from_prop`, but that helper still duplicated the conversion
used by font-face registration. More seriously, the font-face helper interpreted
`CSS_VALUE_NORMAL` (318) as numeric weight 318 because CSS keyword IDs overlap
the numeric interval. The shared weight helper now decodes keywords before
numeric weights; `font_style_desc_from_prop` delegates both weight and slant
to the same helpers as `register_font_face`. Explicit numeric-property weights,
including 950, retain precedence. `FontStyleTest` reproduces the original
normal/bolder/lighter and encoded-numeric discrepancies; the native
`NormalFontFaceRegistersTheNormalWeight` case checks the real registry entry is
400. Receipts: `temp/ledger-next-font-before.{json,log}`,
`temp/ledger-next-font-after.{json,log}` and
`temp/ledger-next-layout-final.{json,log}`. Authority:
**D9.1.1v4 / D9.1.3v2**.

<a id="rad15-5"></a>

#### RAD15-5

**`event_sim_print_results` uses `fprintf(stderr,...)`.** Scoped to the test harness, but a deviation from the project no-`fprintf` guidance.

**Resolution (2026-11-11):** replaced the result banner's direct
stderr writes and the utility `SIM_EVENT_LOG` write with project logging,
each prefixed `event_sim:`. Passing summaries use `log_info`; a failing summary
uses `log_error`. The result JSON schema, counts and file-writing path are
unchanged. A source check finds no remaining `fprintf` or direct console writes
in `event_sim.cpp`; string-buffer `snprintf` calls remain. `log.conf` was not
changed. Authority: **D9.1.1v4 / D9.1.3v2**.

**Correction (2026-11-11):** that cleanup incorrectly treated the human replay
result as debug output. With filtered/disabled logging, the seven view-oriented
UI fixtures and many Radiant view controls lost their required `Assertions:`
and `Result:` lines despite passing machine results. `event_sim_print_results`
now formats its existing user-facing banner into a project string buffer and
writes it independently of logging. Diagnostics still use the logging API;
the JSON result schema and counts remain unchanged. The native
`EventSimResults.HumanAndMachineResultsRemainAvailableWithLoggingDisabled`
regression fails before the repair and passes afterward for success, failure
and no-assertion cases. Evidence lives under
`temp/radiant-failed-20261111/result-contract-{before,after}.{json,log}`;
integrated-host qualification is recorded separately when complete.

<a id="rad01-3"></a>

#### RAD01-3

**Incremental relayout correctness is implicit.** The skip-clean optimization depends on `layout_height_contribution` being exactly recomputed on every non-skipped child (`layout_block.cpp`). There is no assertion tying the cached contribution to the actual advance, so a bug there manifests as silently mis-placed siblings rather than a crash.

**Reverified (2026-11-11):** the current
`layout_can_shift_clean_block` rejects reuse unless the cached height
contribution matches the finite used height within its existing 0.1px check.
It also excludes margins/collapse, floats, vertical flow and unstable subtrees.
The normal path recomputes the contribution from the actual advance delta.
The historical implicit-only precondition is therefore already explicit in
the reuse gate; an assertion is not needed to reject that invalid reuse.
This source audit does not establish every incremental-layout combination.
No production change was needed. Authority: **D9.1.1v4 / D9.1.3v2**.

<a id="rad03-4"></a>

#### RAD03-4

**BFC line-break detection uses a hard-coded inline tag whitelist.** `layout_block.cpp:7562-7578` decides whether a block should suppress the parent line break by matching a long list of inline/inline-block tag ids (`HTM_TAG_SPAN`, `HTM_TAG_A`, …) *before* styles are resolved. This is fragile: a new inline-level element, or an unusual tag defaulting to inline via CSS, will be mis-classified. *Improvement:* defer the decision until after `dom_node_resolve_style`, or drive it from the UA stylesheet's default `display` rather than a tag list.

**Reverified (2026-11-11):** the tag whitelist no longer exists.
The current `layout_block` line-break decision uses its resolved display,
inline-atomic/table classification, float state and out-of-flow state, rather
than a span/a/etc. tag inventory. Animated `display:none` also restores the
saved parent context. This closes the named obsolete whitelist report, not
all possible block/inline interactions. No production change was needed.
Authority: **D9.1.1v4 / D9.1.3v2**.

<a id="rad04-5"></a>

#### RAD04-5

**`max_iterations` caps truncate silently.** The float-avoidance / find-y loops in the block driver and `block_context_find_y_for_width` cap at 100 iterations with no error logged when the cap is hit (see RAD03); a pathological float stack silently stops converging. Mentioned here because the sizing services (`compute_shrink_to_fit_width`, percentage resolution) feed those loops.

**Reverified (2026-11-11):** all four current capped float searches
already log exhaustion: `RAD_CAP_FLOAT_FIND_Y` in `block_context.cpp`,
`RAD_CAP_FLOAT_PLACE` in `layout_positioned.cpp`, and `RAD_CAP_FLOAT_LINE_Y` /
`RAD_CAP_BFC_FLOAT_AVOID` in `layout_block.cpp`. This closes the report that
the caps truncate silently; the limits themselves remain. Source verification
only; no cap was weakened and no pathological convergence claim is made.
Authority: **D9.1.1v4 / D9.1.3v2**.

<a id="rad08-6"></a>

#### RAD08-6

**space-evenly workaround.** `space-evenly` is routed through a custom `x-justify-content` property in style resolution rather than a first-class CSS value (`layout_flex_multipass.cpp:831` note). This is a resolution-layer patch that should be normalized.

**Reverified (2026-11-11):** standard `justify-content:space-evenly`
is parsed by the shared content-alignment parser, stored as
`CSS_VALUE_SPACE_EVENLY` and consumed by native space distribution. The old
`x-justify-content` route is absent. The native
`StandardSpaceEvenlyDistributesEdgeAndInteriorGaps` control verifies equal
edge/interior gaps at widths 400px and, after retained mutation, 200px.
No production change was needed. Authority: **D9.1.1v4 / D9.1.3v2**.

<a id="rad13-2"></a>

#### RAD13-2

**PDF success paths emit `log_error` noise.** Multiple non-error PDF outcomes log at error level — e.g. "fallback effect group rendered as passthrough content" (`render_pdf.cpp:1033`) and the raster-fallback effect-group notice (`:598`) fire on the *success* path where a fallback is the intended behavior. This makes real PDF errors hard to spot. *Improvement:* demote intended fallbacks to `log_debug`.

**Reverified (2026-11-11):** `render_pdf.cpp` now delegates PDF
writing through `radiant_pdf_export_write` and has no `log_error` calls or
the reported effect-fallback success notices. The former native implementation
is gone. This source-level result concerns the Radiant wrapper only; it does
not qualify diagnostics in an optional external PDF writer. No production
change was needed. Authority: **S2.6.6v3 / D9.1.1v4 / D9.1.3v2**.

<a id="rad22-9"></a>

#### RAD22-9

**`WebViewProp.src`/`srcdoc` are borrowed pointers.** They are documented as borrowed DOM-attribute storage in `radiant.hpp`; their lifetime is tied to the DOM node and is not retained through a `view_pool_reset_retained` (RAD01) — a relayout that frees attribute storage while a webview holds `src` would dangle. No `PersistentFieldRef` guards them.

**Reverified (2026-11-11):** source admission already uses
`radiant_retain_webview_src` / `radiant_retain_webview_srcdoc`, which store
pool-domain pointers through `PersistentFieldRef`. The historical claim that
those guards are absent is obsolete. The owning document/pool lifetime remains
the contract; this source audit is not a platform-webview mutation/teardown
stress test. No production change was needed. Authority:
**D5.5 / D9.1.1v4 / D9.1.3v2**.

## 2026-11-11 continued owner qualification

Under **D9.1.1v4 / D9.1.3v2**, `make -j8 run-radiant-baseline` passes on the
complete repaired source: **882 native tests plus one static corpus smoke
test**, with one existing hardware MIDI skip; **7/7 runner contracts**;
**3,851 static and 630 dynamic geometry comparisons**, with six existing static
fixture skips and no failures. The focused native layout suite passes **41/41**
and the font suite **4/4**. No baseline tolerance, reference, fixture corpus or
sibling implementation was changed.

`make lint ARGS='--rule ^no-int-cast-radiant$'` passes with **89 checked casts
and zero unmarked casts**; `make test-lint` passes **5/5**. The input-facade
and whitespace checks pass. The grid baseline entries remain explicitly
PARTIAL, including flex, spanning-row and orthogonal qualification limits.
The six source-reverified historical reports above retain their narrower
verification limits.

Receipts: [verification summary](../radiant/temp/ledger-next-verification-summary.json),
[aggregate log](../radiant/temp/ledger-next-radiant-baseline.log),
[geometry results](../radiant/temp/ledger-next-layout-baseline.log),
`temp/ledger-next-layout-final.{json,log}`, `temp/ledger-next-lint.log`, and
`temp/ledger-next-lint-contracts.log`. The verification summary pins the host
and changed source/header/test hashes. Only ledger documentation changed after
the passing aggregate.


## 2026-11-11 flex, media and layout-cache delivery

<a id="rad22-6"></a>

#### RAD22-6

**Poster image is not plumbed.** The idle/loading branch of `render_video_frames` (`render_video.cpp:452-457`) comments that a poster "would need to be passed through" and falls back to the play button; `poster` attribute support is missing.

**Resolution (2026-11-11):** poster images are admitted independently of a
playable source and rendered through the normal image path before playback.
Object-fit/position, ancestor/content clips and retained image ownership share
existing image mechanisms. Removing, replacing or failing a poster load clears
its old borrow. Display-list and PaintIR placeholders carry a poster-presence
bit, without another resource pointer. The compositor rechecks playback so a
live frame wins over a retained pre-play recording; ready/idle cached media
keeps the poster. Missing/broken images preserve the existing fallback.

**Evidence:** the original CLI captures in `temp/ledger-impl/` show only the
video background. Native pixel tests now cover no source, preload:none,
contain letterboxing, cover/ancestor clipping, replacement/removal and a broken
poster. The macOS decoder test also reproduces the retained-placeholder bug
before its compositor repair and verifies live-frame pixels afterward. Its
sandboxed audio-decoder start failure is retained separately from the passing
native rerun. Receipts: `temp/ledger-impl-layout-final.{json,log}` and
`temp/ledger-impl-retained-poster-{before,after}.{json,log}`. The
[implementation record](impl/Radiant_Impl_Ledger_Repairs.md) states the delivered
scope. Authority: **D5.5 / D9.1.1v4 / D9.1.2v2 / D9.1.3v2**.

### Core delivery qualification

Under **D9.1.1v4 / D9.1.3v2**, the final `make -j8 run-radiant-baseline`
passes **891 native tests plus one static corpus smoke test**, with one
existing hardware MIDI skip; **7/7 runner contracts**; **3,851 static and 630
dynamic geometry comparisons**, with six existing static skips and no failures.
The final focused layout suite is **49/49**. The earlier aggregate's one
`align_baseline_nested_column` failure was repaired and retained as a regression;
fresh Chrome and the unchanged corpus reference agree. Its failed aggregate
remains in `temp/ledger-impl-radiant-baseline-before-nested.log`.

Layout-cast lint passes **89 checked / zero unmarked**, lint contracts **5/5**,
and input-facade/diff checks pass. Native poster pixels, the macOS playback
transition and PNG/SVG visual controls pass. No tolerance/reference/corpus or
sibling implementation changed. Broader baseline, whitespace and decomposition
reports remain PARTIAL as described in the implementation record.

Receipts: [verification summary](../radiant/temp/ledger-impl-verification-summary.json),
[aggregate log](../radiant/temp/ledger-impl-radiant-baseline.log),
[geometry results](../radiant/temp/ledger-impl-layout-baseline.log), and
[implementation record](impl/Radiant_Impl_Ledger_Repairs.md). The summary pins the
host and changed source/header/test identities; only documentation changed
after the passing aggregate.

## Continued native clip and default-size repairs — 2026-11-11

**Final owner qualification:** `make -j8 run-radiant-baseline test-native-closure`
passes 893 native tests plus static smoke, 7 runner contracts, all 3,851 static
and 630 dynamic geometry comparisons and native archive closure. One hardware
MIDI test and six static fixtures retain their existing skips. Layout-cast lint
checks 89 casts with zero unmarked; its five contracts, the input facade and
diff checks pass. The [receipt](../radiant/temp/ledger-next-verification-summary.json)
pins source and executable/archive hashes; only documentation changed afterward.
Six PNG/SVG clip states independently match fresh Chrome pixels. The original
failing native clip report remains in `temp/ledger-next-clip-before.{json,log}`.

<a id="rad02-2"></a>

#### RAD02-2 — Generated content keeps counters, images and text in order

**Fixed 2026-11-22.** The original report listed `counter()`, `counters()`,
`attr()`, `url()` and concatenation as stubs. The current engine already
evaluates these values. A mixed list still failed because the ordered counter
pass reevaluated the pseudo-element as a text string, dropping the image and
the following entries. It now uses the existing ordered content materializer
for mixed lists containing images, preserving any list-item marker.

The new native regression checks the counter, 12×12 image, and attribute
suffix across retained updates. The focused PNG changes from zero red
pixels to a visible red image. Authority **D5.5.3 / D9.1.2v2 / D9.1.3v2**.
Evidence: `temp/radiant-ledger-continuation/{mixed-test-before.log,final-dynamic.log,mixed-content-before.png,mixed-content-after.png}`.

<a id="rad02-3"></a>

#### RAD02-3

**`clip: rect()` not parsed.** `resolve_css_style.cpp:9908` has a TODO to parse `rect()` values into scroller clip bounds; currently ignored.

**Resolved (2026-11-11):** the native dispatcher now resolves rectangle lengths and auto edges into view-owned position data. Shared border-box geometry clips the positioned HTML owner, descendant paint, vector output and native hits, including retained updates. Two native controls pass; six raster/vector states match fresh Chrome pixels. See the [implementation and scoped limits](impl/Radiant_Impl_Ledger_Clip_Defaults.md#rad02-3--legacy-rectangle-clipping) and [qualification receipt](../radiant/temp/ledger-next-verification-summary.json). Authority: **D5.5 / D9.1.1v4 / D9.1.3v2**.

<a id="rad03-3"></a>

#### RAD03-3

**Replaced-element 300×150 defaults are duplicated.** The HTML default replaced size appears as scattered literals — e.g. `layout_block.cpp:5849` (`... : 300.0f`) and `5864` (`given_height = 150.0f`), plus the `layout_measure.cpp` intrinsic path (RAD05). Correct per spec but duplicated rather than centralized in one constant, so a change must be made in several places.

**Resolved (2026-11-11):** replaced width/height and audio height now have one native constant definition. Inline SVG, HTML hints, sizing, SVG metadata and picture fallbacks share those constants or the existing `layout_replaced_default_size` provider. Full geometry qualification preserves existing sizes; see the [implementation record](impl/Radiant_Impl_Ledger_Clip_Defaults.md#rad03-3--rad04-3--replaced-defaults). Authority: **D9.1.1v4 / D9.1.3v2**.

<a id="rad04-3"></a>

#### RAD04-3

**Hard-coded replaced defaults are duplicated.** The 300×150 (and 300×54 audio) intrinsic defaults live both in `layout_measure.cpp:105`+ and scattered through `layout_block.cpp` (SVG/iframe/hr paths). Centralizing them into one table would remove the drift risk.

**Resolved (2026-11-11):** the duplicate replaced defaults are consolidated with [RAD03-3](#rad03-3); the existing tag-based sizing provider remains authoritative. See [implementation and qualification](impl/Radiant_Impl_Ledger_Clip_Defaults.md#rad03-3--rad04-3--replaced-defaults). Authority: **D9.1.1v4 / D9.1.3v2**.

<a id="rad16-5"></a>

#### RAD16-5

**`radiant_frame_clock_start` has dead trailing statements.** The `#else` fallthrough stores `running = 0` and returns `false`, but the two statements after the `#endif` (`frame_clock.cpp:369`-`370`) are unreachable on every platform — harmless but confusing.

**Resolved (2026-11-11):** removed the shared trailing store/return after the platform selection. The macOS defensive mode rejection remains inside its own branch, and all branches now return explicitly. Native animation/scheduler regressions remain in the aggregate gate. See the [implementation record](impl/Radiant_Impl_Ledger_Clip_Defaults.md#rad16-5--frame-clock-control-flow). Authority: **D9.1.1v4 / D9.1.3v2**.

<a id="ri-028"></a>

### RI-028 — Auto-height flex columns shrink natural content to their minimum

**Status:** FIXED, 2026-11-12. **Lambda blocker:** cleared for the reduced
ImageMagick header. Ownership **D9.1.1v4 / D9.1.3v2**. ImageMagick uses a column
body with `min-height:100vh` and an overflow-hidden hero. The initialization
used that minimum as available main size before natural line sizing. Flex
shrink then reduced the hero to its padding and clipped its heading/actions.
A reduced 800px-minimum column shrank a 340px header to 40px and a 100px footer
to 0px despite 1000px of main content; the auto container should grow to 1440px.

The existing natural column-size pass now selects the larger of the minimum
used size and natural hypothetical lines when the main size is not definite.
A new regression fails before and passes after, including short-content
minimum/growth controls. All 69 native dynamic tests pass; all 3851 recorded
static layout baselines pass with 6 skips and no changed tolerances. The layout
integer-cast lint passes. The repaired offline full ImageMagick document's
hero grows from 105px to 604.4px; exact font/spacing parity is not claimed.
Evidence: Lambda `temp/online-visual-round3/{flex-before.log,dynamic-after.log,
baseline-layout.log,imagemagick-local.json,imagemagick-after.json}`.


<a id="ri-029"></a>

### RI-029 — Modal dialogs and backdrops paint in the document top layer

**Fixed 2026-11-19 in Radiant `494680e`, reverified 2026-11-22.** The
original Leaflet capture allowed ordinary page content over its modal and
lacked the dim backdrop. Radiant lays out and paints the document's
ordered top layer independently of ancestor clips and stacking contexts.
Native hit testing visits it in reverse paint order and applies modal
inertness (**D9.1.1v4 / D9.1.3v2 / D9.5.6v2**).

The original qualification passed 88 pinned modal and top-layer checks.
A fresh offline capture puts a blue dialog and green backdrop above a
maximum-`z-index` red layer in PNG, SVG and flat PDF. It also places the
dialog outside its 1×1 clipped ancestor. Evidence:
[HTML implementation record](impl/Lambda_Impl_HTML_Support.md) and
`temp/radiant-ledger-continuation/modal.{html,png,svg,pdf}`.

<a id="ri-030"></a>

### RI-030 — Grid row growth leaves stale scroll clips that hide item contents

**Status:** FIXED for the isolated defect, 2026-11-12. **Lambda blocker:**
cleared for the reduced paint case; full DigitalOcean content remains under
investigation. Ownership **D9.1.1v4 / D9.1.3v2**. DigitalOcean's missing content
led to a small two-row grid reproduction: an overflow-auto main item has final
height 1400px, but `setup_scroller` clips its children at a cached height 0px.
An adjacent sidebar's cached clip is 18px while its final used height is 800px.
`layout_final_grid_content` published scroll bounds before content-driven row
growth and final alignment changed those used boxes.

The existing publication now runs after row growth/alignment, before sticky
positioning. No new clip implementation or page-specific rule was added.
`GridScrollBoundsUseFinalContentGrownRows` fails before on both stale-state
assertions and passes after; the reduced PNG's interior pixel changes from
parent yellow to child red. All 70 native dynamic tests and all 3851 recorded
static layout baselines pass (6 skips); integer-cast lint passes. The full
DigitalOcean offline page still loses main content after this repair, so this
entry does not claim its entire visual mismatch is solved.
Evidence: Lambda `temp/online-visual-round3/{grid-before.log,dynamic-grid-after.log,
baseline-grid.log,grid-scroll.html,grid-scroll.png,grid-scroll-after.png,
do-after-debug.log}`. The final 142 grid-using candidates are refreshed separately.

<a id="ri-032"></a>

### RI-032 — Flex SVG automatic cross sizes ignore the flexed main size

**Status:** FIXED, 2026-11-12. **Lambda blocker:** cleared for the reduced
Opus announcement. Native layout ownership follows **D9.1.1v4 / D9.1.3v2**.
An SVG with `viewBox="0 0 20 20"` and CSS `width:14px` inside a flex row
receives a provisional intrinsic height of 1,184px from available space.
Cross-size finalization used only authored aspect ratios, so it never
transferred the SVG's natural ratio from the final 14px main size. The icon
made the announcement 1,200px tall and pushed the hero below the viewport.

Finalization now uses the existing preferred/natural ratio query and existing
border/content-box transfer helper. The regression fails before and passes
after for physical/logical widths, non-square viewBoxes, padding/borders,
explicit independent height and width mutation. The full static Opus document
now has a 32.8px announcement and its main begins at 101.8px, rather than an
announcement above 1,100px. This does not add a new SVG feature or page rule.

Qualification: all 73 dynamic-layout tests pass; all 3,851 recorded static and
630 JavaScript layout baselines pass (six static skips). Evidence: Lambda
`temp/online-visual-round4/{svg-before.log,svg-after.log,dynamic-final.log,
svg-physical.html,saas_landing_254-static-after.json,radiant-baseline.log}`.
Online capture qualification is recorded separately in the round-four audit.

<a id="ri-033"></a>

### RI-033 — Propagated body backgrounds cover negative stacking layers

**Status:** FIXED for the isolated ordering defect, 2026-11-12.
**Lambda blocker:** cleared for the reduced Vogelhaus sky. Ownership
**D9.1.1v4 / D9.1.3v2**. The renderer selected the body background as the canvas
background, then painted it again during the ordinary body boundary visit.
Root negative-z descendants were already painted, so the repeated body fill
covered the hero's gradient and made white text disappear. CSS Backgrounds 3
§2.11.2 gives a propagated body's background its transparent initial used value.

The shared background-source walk now records that selected source during the
root boundary visit and suppresses its later body visit. Both paint drivers
admit canvas recording when the root has no own background, and optimized
boundary paths use the same source selection. Existing layer accessors and
canvas selection moved to the lightweight boundary module to preserve native
recorder test linkage; they were not copied or replaced by test stubs.
Existing image positioning and the canvas viewport extension remain shared.

The native pixel regression fails before and passes after for solid and
gradient body sources and an explicit root-background control. It samples both
a narrow negative layer and exposed canvas. All 73 dynamic tests pass, local
PNG/SVG controls agree after headless-shell rasterization, and the unchanged
`canvas_body_background` integration fixture passes all six assertions,
including zero-height tiled body images and a later explicit root color.
A local extension also passes all 12 assertions while changing body colors
and restoring the image, checking retained canvas-source freshness.
Containment and fragmented canvas backgrounds remain outside these checks.
Evidence: Lambda `temp/online-visual-round4/{canvas-before.log,dynamic-final.log,
export-controls.json,ui-canvas-final.log,canvas-body.html}`.
Spec: https://www.w3.org/TR/css-backgrounds-3/#special-backgrounds

<a id="ri-034"></a>

### RI-034 — Positioned intrinsic sizing leaks its query mode to following blocks

**Status:** FIXED, 2026-11-12. **Lambda blocker:** cleared for the reduced
Vogelhaus width. Ownership **D9.1.1v4 / D9.1.3v2**. `layout_abs_block_internal`
selects min/max-content available space for an intrinsically sized positioned
box. Its public wrapper returned without restoring that state. A fixed
`width:max-content` header therefore made a following ordinary block choose
max-content width: 3,000px instead of its 1,200px containing width. Vogelhaus's
archived main grew to 4,482px and centered the hero outside the viewport.

The positioned wrapper now saves and restores the parent's available space.
The regression fails before and passes after with fixed max-content,
absolute min-content, a definite-size control and relayout. Overflowing child
content keeps its 3,000px width, while both normal-flow siblings stay 1,200px.
The archived full Vogelhaus main now stays 1,200px. All 73 native dynamic,
3,851 recorded static and 630 JavaScript layout baselines pass (six skips).
Evidence: Lambda `temp/online-visual-round4/{space-before.log,abs-space.html,
abs-space-after.json,reduce-width-2.log,saas_landing_476-static-after.json,
dynamic-final.log,radiant-baseline.log}`.


<a id="ri-036"></a>

### RI-036 — Nested column flex intrinsic heights omit main-axis gaps

**Status:** FIXED, 2026-11-12. **Lambda blocker:** no. Ownership
**D9.1.1v4 / D9.1.3v2**. `FlexIntrinsicAccumulator::add_width` counted only row
children; the subsequent intrinsic contribution added only column gaps to row
widths. Nested auto columns therefore omitted every row gap from their height,
then an enclosing column shrank fixed-height contents to fit that underestimated
budget. A reduced 736px hero, 6000px following section, 120px gap and 40px padding
produced a 6776px page instead of 6896px and compressed the hero to 722.9px.

Count contributions for either orientation and add the main-axis gap through
`layout_flex_style_info`, reusing the existing style resolver. The regression
`NestedColumnIntrinsicHeightIncludesMainAxisGaps` fails before repair and passes
with both 120px and 40px gaps across recascade/relayout. All 77 dynamic-layout
checks, 3851 static and 630 JavaScript layout cases pass (six static skips), as
does layout integer-cast lint. Evidence: Lambda
`temp/online-visual-round5/{gap-before.log,dynamic-final.log,craft-min.html}`.
Craft's full-page residual is separately open as RI-039.

<a id="ri-037"></a>

### RI-037 — Authored display bypasses scripting-enabled noscript suppression

**Status:** FIXED, 2026-11-12. **Lambda blocker:** no. Ownership
**D9.1.1v4 / D9.1.3v2**. The existing scripting-mode guard operated in inline
layout, so explicit `display:contents`, block, flex or grid roles could bypass
it. Raw noscript markup became text or a 400px box could enter a flex/grid.
`layout_resolved_display` now applies the existing
`layout_noscript_content_suppressed` predicate before selecting a used layout
role. CSSOM still reads `resolve_display_value`, preserving authored computed
CSS and DOM contents. No public style is rewritten.

`ScriptingEnabledNoscriptCannotCreateBoxesThroughCssDisplay` covers native
scripting-disabled fallback followed by enabled-mode recascade and relayout.
Lambda `test/ui/dom/noscript_used_display` verifies computed CSS, zero DOMRects,
retained DOM text and normal following flow through the real script host (five
assertions). Its old-host control fails. Qualification is the same complete
layout gates as RI-036. Evidence: Lambda
`temp/online-visual-round5/{noscript-before.log,noscript-before-ui-result.json,noscript-final-ui-result.json}`.

<a id="ri-038"></a>

### RI-038 — Intrinsic ratio heights ignore inline min/max constraints

**Status:** FIXED, 2026-11-12. **Lambda blocker:** no. Ownership
**D9.1.1v4 / D9.1.3v2**. Intrinsic height transferred its provisional inline
query width through `aspect-ratio` before applying inline min/max constraints.
A grid item with `width:100%;max-width:56px;aspect-ratio:1` reserved an 856px auto
row; its percentage-height child remained 205px despite its final 56px square.
Both an empty item and an image-containing flex item reproduce this defect.

Before ratio transfer, constrain the candidate using the existing border-box
conversion and `layout_apply_min_max_border_box_axis` helpers. This implements
CSS Sizing 4 §4.2/§4.4's constrained ratio-determining size without duplicating
constraint logic. `GridRatioHeightUsesConstrainedInlineSize` fails before and
passes after; it checks empty/image items, 56px→112px mutation and preserved
explicit 90px height. All 77 native dynamic checks and both recorded layout
gates pass, with integer-cast lint. Evidence: Lambda
`temp/online-visual-round5/{ratio-before.log,ratio-after.log,grid-ratio.html}`.
The reduced Craft grid/image now measure 56px; RI-039 retains the distinct outer
flex-height residual. Do not claim this repair alone resolves Craft's hero.


<a id="ri-039"></a>

### RI-039 — Intrinsic height recursion borrows an outer percentage-height basis

**Status:** FIXED, 2026-11-12. **Lambda blocker:** no. Ownership
**D9.1.1v4 / D9.1.3v2**. Recursive intrinsic height queries installed each
child's inline basis but retained an outer block's definite height through
auto-height ancestors. In a script-free nested flex/grid reduction, a 100%
image borrowed viewport/outer query heights (800px, then 1152px and 1392px in
LLDB) instead of its own containing box's auto/ratio size. A 136px following
card acquired a 1392px wrapper and squeezed an earlier 736px hero to 540px.

Install the current element's own explicit or ratio-derived content height,
or an indefinite basis for auto height, through the existing
`LayoutContainingBlockScope`. `IntrinsicDescendantPercentHeightUsesItsOwnContainingBlock`
fails before and passes after, including 56px→112px square mutations and
relayout. All 77 native dynamic tests, 3851 static and 630 JavaScript layout
cases pass (six static skips); integer-cast lint passes. The full frozen
Craft hero now measures 736px. The separate nested-height failure in RI-040
still obscures live hero content and reproduces on the previous renderer
when the hero is fixed at 736px. This is an isolated sizing repair, not a
claim that the entire page matches.

Evidence: Lambda `temp/online-visual-round5/{percent-before.log,dynamic-qualified-final.log,craft-fivefix.json,lldb-height.log,craft-thin.html}`. Captures use
`lambda-fivefix.exe` SHA-256
`6993045526db37b5a4b9b1b4ce504ede4e9c6eaafa582105ac4d52572425c0a4`.


<a id="ri-040"></a>

### RI-040 — Maximum constraints erase definite flex main sizes

**Status:** FIXED, 2026-11-12. **Lambda blocker:** no. Ownership
**D9.1.1v4 / D9.1.3v2**; size/percentage behavior follows
[CSS Flexbox §9.8](https://www.w3.org/TR/css-flexbox-1/#definite-sizes).
In init_flex_container, the maximum-size branch replaced has_definite_main
with a comparison against the maximum. An explicitly 736px-high container
with max-height:1100px therefore became indefinite, even though no clamping
occurred. Percentage-height descendants then used their content minima:
Craft's nested wrapper became600px and its padded inner flex box120px
instead of736px. The product image overpainted the heading. The code handles
either main axis.

Preserve an already definite main size while retaining the previous rule
for an automatic size that reaches its maximum. The standalone reduction
temp/online-visual-round6/percent-max.html reproduces without scripts or
site CSS. MaximumConstraintPreservesDefiniteFlexMainAxis fails before and
passes after repair: inactive1100px maximum, clamped600px maximum and removal
of the maximum, with nested percentages, positioned bottom child and a
neighboring percentage-basis row across recascade/relayout.

All78 native dynamic tests,3851 static and630 JavaScript layout cases pass
(six static skips), and layout integer-cast lint passes. Full frozen Craft
keeps both outer and inner content736px; the live heading/button/product
image are restored. URL-masked decorative geometry remains RI-041, so the
page is not declared a complete visual match. Prior fixed-height controls
remain valid evidence that this defect predates RI-039.

Evidence: Lambda temp/online-visual-round6/{max-before.log,dynamic-max.log,
percent-matrix.log,percent-ancestors.log,craft-max.json,craft-live.png}.
Qualified host: lambda-max.exe, SHA-256
34795256482fbda34c9e677bc079475ae48963d43fb5733c9748e8694fc24891.


<a id="ri-042"></a>

### RI-042 — CSS math gaps are dropped before layout

**Status:** FIXED for length-valued math, 2026-11-12. **Lambda blocker:** no.
Ownership **D9.1.1v4 / D9.1.3v2**, document style contract **D5.5.1**;
[CSS Box Alignment §8](https://www.w3.org/TR/css-align-3/#gaps) defines
nonnegative gap lengths. Craft uses gap:calc(var(--spacing) * 10) with
--spacing:.25rem. The variable and computed serialization are correct, but
the gap shorthand excludes FUNCTION values and resolve_gap_property handles
only literal lengths/numbers/percentages. Every corresponding layout gap
therefore becomes zero. The root cause is unrelated to the unsupported URL
masks (RI-041) and is not a missing Tailwind theme variable.

Admit math-valued shorthand components and resolve their substituted definite
lengths through the existing length/math resolver. Clamp negative computed
lengths to zero. The shared consumer feeds flex, grid and multicolumn geometry;
no page-specific stylesheet or spacing is substituted. Pure percentages retain
their existing deferred flags. Percentage-containing math expressions still
lack a retained gap expression and used-container basis; this limited repair
does not claim that separate incomplete consumer is supported.

MathGapsUseOwnerUnitsAcrossLayoutModes fails before the repair in all three
layout modes and passes afterward. It checks calc/min-max-family math,
custom-property substitution, rem/em owner units, two-value shorthand,
nonnegative clamping, pure-percentage controls and 16px to20px root-font
mutation with recascade/relayout. Chromium headless-shell independently
returns the same16 expected relative coordinates. All79 native dynamic
layout tests pass. Final integration qualification and online capture counts
are recorded in Lambda test/online/README.md.

Evidence in Lambda temp/online-visual-round6/: gap-before.log,
dynamic-gap.log, gap-browser.json, craft-spacing-probe-output.log.
Final host lambda-final.exe SHA-256
60f757cadefdbc438344b5826696c6b1536e5c81f4cd777ff2d531a9529b1c60.


<a id="ri-043"></a>

### RI-043 — Unsubstituted variables incorrectly trigger cyclic intrinsic sizing

**Status:** FIXED, 2026-11-12. **Lambda blocker:** no. Ownership
**D9.1.1v4 / D9.1.3v2**, document contract **D5.5.1**;
[CSS Sizing §5.2](https://www.w3.org/TR/css-sizing-3/#intrinsic-contribution)
requires the appropriate min/max constraints in intrinsic contributions.
measure_element_intrinsic_widths classifies authored width and max-width with
layout_css_value_has_percentage before variable substitution. That predicate
conservatively treats var() as potentially percentage-valued. A definite
max-width:var(--cap), with --cap:800px, therefore enters the cyclic-percentage
policy and loses its cap when a flex parent measures an auto-width child.
This enlarges Maven Central's text column and shrinks its promo image.

Use the existing layout_css_size_facts query to classify the substituted
owner value. No new resolver or page-specific geometry is introduced. A
literal 800px override on the frozen page already restores the correct
contribution, proving that font noise and script readiness are not this cause.
The first reduced promo has browser text/image widths600.64/327.36 versus
old native678.8/249.2; this repair gives600.6/327.4. That reduction uses a
436x347 natural image to separate the independent image-hint defect RI-044.

IntrinsicVariableMaximumConstrainsNestedFlexContributions checks literal,
local-variable, owner-em and variable-math maxima, recascade after cap300->200,
and a zero-percentage math neighbor against an unconstrained control. Chromium
independently confirms that calc(300px + 0%) retains a cyclic dependency and
must not be forced to behave like a pure length. All81 native dynamic tests
pass with RI-044. Final recorded-layout and online qualification is in Lambda
test/online/README.md. No percentage basis or tolerance is substituted.

Evidence in Lambda temp/online-visual-round7/: maven-matrix.json,
constraint-before.log, constraint-browser-before.json, dynamic-twofix.log.

<a id="ri-044"></a>

### RI-044 — Intrinsic images restore HTML width hints overridden by CSS

**Status:** FIXED, 2026-11-12. **Lambda blocker:** no. Ownership
**D9.1.1v4 / D9.1.3v2**, document contract **D5.5.1**;
[CSS2 §6.4.4](https://www.w3.org/TR/CSS2/cascade.html#preshint) puts presentation
hints below author CSS. The intrinsic IMG branch correctly avoids a provisional
resolved percentage width, then falls back to the raw HTML width attribute.
That fallback revives a hint which authored width:auto or a cyclic percentage
already overrode. Maven's image has natural972x613 dimensions but HTML436x347
hints; width:100%;height:auto must keep the natural intrinsic contribution.

Only use the raw HTML fallback when no winning physical size declaration
exists. Resolved definite CSS sizing remains authoritative; bare HTML hints
remain available. The shared physical declaration selector includes logical
inline-size, so it follows the same cascade without a copied special case.
IntrinsicImageCssOverrideDoesNotRestoreHtmlWidthHint fails before repair and
passes after: percentage, automatic, variable percentage and logical widths,
plus untouched bare-hint/fixed-CSS controls. Row width400->500 and HTML hint
100->80 mutations verify that overridden hints stay overridden. Chromium's
independent six-case geometry control includes an automatic image retaining
its natural300px width while its flex wrapper shrinks to200px.

All81 native dynamic tests pass. With both repairs, the frozen real-dimension
Maven promo has text/image widths419/509px and height327px; Chromium returns
418.97/509.03px and327.02px. The missing client-created search input remains
unresolved; this is not a claim of full application execution support.
Evidence in Lambda temp/online-visual-round7/: image-before.log,
image-browser.json, dynamic-twofix.log, maven-natural-browser.json,
maven-facts.json, maven-final-summary.json. Final pinned host lambda-final.exe
SHA-256 76e79482164f6192fee0b148cfa929e56b4d8cd129730ef36518b369bca67c9c.


<a id="ri-045"></a>

### RI-045 — Headless document hosts omit the browsing profile

**Status:** FIXED, 2026-11-12. **Lambda blocker:** cleared for cookie startup;
remaining visual omissions require their own evidence. Ownership
**D9.1.1v4 / D9.1.3v2**, headless parity **D9.2.1v2**, snapshot policy **D10.1.7**.
Twenty-six remaining high-severity captures report document.cookie setter
SecurityError. Export initializes a native UI context but no BrowsingSession;
view and js --document already have one. The loader therefore receives no
cookie jar, loses initial response cookies, reports cookieEnabled=false and
throws on script cookie writes. Layout and legacy PNG batches share this gap.

UiContext now owns an ephemeral session only when its caller supplied none.
Exports ensure it before loading; the common hosted loader also ensures it
from explicit/TLS UI configuration before HTTP and passes the same jar to
subresources. Existing window profiles remain borrowed. UI retirement releases
its owned session after the document and loader tasks retire. A shared batch UI
retains the profile across jobs; independent exports do not persist it.
No synthetic cookie success value or setter exception suppression is used.

The real local HTTP renderer regressions in Lambda
 test/online/render_session.test.cjs cover response/DOM cookies, HttpOnly
visibility, initial and script-created image requests, redirects, independent
exports, layout and both batch paths. The initial renderer fails all three
export cases; the intermediate export-only repair leaves layout/legacy batches
failing; all six pass with the common-loader repair and input LI-018/LI-019.
OwnedEphemeralBrowsingSessionEndsWithUi and
BorrowedBrowsingSessionSurvivesUiCleanup verify profile lifetime and isolation.
All83 focused native cases and574 AST baseline tests pass. A restricted-sandbox
media failure reproduces on old/new renderers and passes on both with native
macOS services; no media implementation or expectation was changed.

Input transport fixes LI-018/LI-019 are necessary companions: enabling a jar
exposed corrupt terminal cookie attributes and port-bearing cookie domains.
Final aggregate/capture counts and limitations are recorded in Lambda
 test/online/README.md. These exception repairs do not establish that every
cookie-using page now visually matches. Evidence in Lambda
 temp/online-visual-round8/: cookie-before.log, session-before.log,
 session-frontends-before.log, session-qualified.log, native-qualified.log,
 js-baseline-ast-qualified.log, media-controls.json, media-native-services.json.
Final host lambda-final.exe SHA-256
ff06c819f1cfa2a8e3bc1d1a7e20ae6e2bc9e6f153e9f10b8b1b2f5cbc8ea209.


<a id="ri-046"></a>

### RI-046 — Pending style text reparses discard later CSSOM edits

**Status:** FIXED, 2026-11-12. **Lambda blocker:** cleared for dynamic sheet
retention; unrelated omissions remain explicit. Native/adapter ownership
**D9.1.1v4 / D9.1.3v2**; finite frame capture remains **D10.1.7**.

A script connects an empty style node and calls sheet.insertRule. Its CSSOM
rule list reports the insertion, but the post-script native rescan reparses the
queued DOM text update and replaces the sheet with its empty text source.
Computed color can report the CSSOM edit before the final image loses it;
geometry reads can trigger the stale rescan sooner. Libraries that insert
rules without serializing them into text children lose their entire stylesheet.
The element.sheet getter also returned the old sheet after textContent changed.

radiant_sync_mutated_inline_stylesheets now consumes each successfully synced
owner once, retaining the batch count for reconciliation. The Lambda CSSOM
adapter flushes pending text changes before returning an inline sheet, so later
CSSOM edits survive the final rescan. An actual later text replacement still
creates its new sheet. Explicit native radiant_sync_inline_stylesheet calls
retain their force-parse contract. CSSOM rules are not copied back into text,
and no site-specific selectors, opacity overrides or timer advances are used.

Lambda test/online/render_stylesheet.test.cjs covers new empty-sheet insertion,
existing rule declaration edits, text replacement followed by a sheet read and
CSSOM insertion, and later text superseding CSSOM. Two of four cases fail with
the prior qualified renderer; all four pass with the repair, including exported
solid-color pixels. Chromium independently confirms the reduced insertion and
replacement behavior. The new native input-value regression checks repeated
synchronization retains CSSOM state and a new text update still replaces it.
The real window/event fixture dom_stylesheet_cssom_retention fails3 of4
assertions with the previous renderer and passes4 of4 with the repair. All3
recent DOM integration fixtures pass15 assertions.
All12 native input-value cases,574 AST baseline cases,3851 recorded static layout
cases (6 skips),630 JavaScript layout cases,6 HTTP/session cases and5 comparator
tests pass. The native aggregate has928 passes,1 skip and the existing RI-035
unavailable-voice failure. Initial AST worktree failures use stale/missing
fixture/module links; the qualified current fixture/module environment passes
all574 without crypto/media source or expectation changes.

Diagnostic captures restore Northstar home/about layout and Greenlist styling;
Greenlist's hero remains absent and must remain high even below the metric
threshold. Final per-page results and qualification limits are in Lambda
 test/online/README.md and temp/online-visual-round9/. Reduced source, PNGs,
stylesheet-before.log,render-qualified.log,native-input-value.log,
cssom-browser.json,js-baseline-ast-qualified.log and layout/native logs retain
the controls. Final host SHA-256:
044ca5945f52565491a82c67ad85437473eb8489ad981f4bc51b69f75875f71b.


<a id="ri-047"></a>

### RI-047 — Raw basic-shape parsing drops computed clip geometry

**Status:** FIXED, 2026-11-12. **Lambda blocker:** cleared for the confirmed
menu overlay; unrelated visual differences remain. Native ownership follows
**D9.1.1v4 / D9.1.3v2**, transient value ownership **D5.5.1**, selected-frame
snapshot policy **D10.1.7**.

Hive Streaming uses a fixed menu at1200px and closes it with
clip-path:inset(0 0 calc((1 - var(--menu-open,0)) * 100%) 0).
render_clip.cpp read raw declaration text with strtof, so math did not advance
and the shape became an unclipped rectangle. A whole-property variable also
failed. The open menu covered the main hero although HTTP loading succeeded.
Separately, one/two-value inset percentages were resolved once on the vertical
axis and copied to horizontal edges; circle percentages used the shorter side
instead of the normalized diagonal. Relative lengths were read as raw numbers.

Basic shapes now consume parsed, substituted CssValue trees. Their shared
length sink uses the existing native CSS numeric/font/unit/math resolver with
an explicit reference basis; its original three-argument API retains its prior
behavior. Inset shorthand expands syntax before each physical-axis resolution.
Circle/ellipse center positions reuse the existing CSS position grammar;
closest/farthest-side radii and a default circle use their box distances.
Only resolved coordinates enter the clip scratch scope, including polygon
vertices. Empty/overconstrained insets stay empty. Raster and vector consumers
share this geometry, including CSS zoom. No math parser is copied, no vendor
code edited, and no site selector, clipping override or timer advancement added.

The frozen original Hive HTML/CSS and a control differing only by the equivalent
literal inset(0 0 100% 0) reproduce the menu/hero difference with the previous
renderer. The repaired original matches the old literal control exactly in
pixels, as well as its repaired literal control. A live diagnostic capture
restores the hero and changes coarse mean error from59.7% to1.6%; full-round
measurements retain consent/content/readiness caveats.

Eighteen real PNG cases cover closed/partial clips, variable fallback and whole
values, shorthand axes, nonlinear math, font units, circle/ellipse/polygon,
script mutation, zoom, empty shapes and invalid dimensions. The previous pin
fails15 of18; the repaired pin passes18. Independent Chromium agrees with all18
expected pixel sets, and18 exported SVGs rasterized by Chromium pass the same
checks. The window/event regression fails4 of6 assertions before and passes6
of6 after variable changes and a used-box resize. All4 recent DOM fixtures pass
21 assertions. All83 focused native cases,574 full AST cases,3851 recorded
static layout cases(6 skips),630 JS layout cases,28 real CLI regressions,8
existing clip-render corpus cases,5 comparator cases and geometry lint pass.
The native aggregate has928 passes,1 skip and the same RI-035 voice failure.
Input implementation is unchanged in this round.

Full inset corner/slash rounding, polygon fill rules, complete shape() grammar,
URL clips and geometry boxes remain incomplete. Existing bounded path()/shape()
command parsing is retained. The HTML/CSS support matrix records these limits.
No high-severity page is cleared solely from a metric threshold.

Evidence in Lambda temp/online-visual-round10/: clip-probe.html,
clip-before.png,clip-browser.json,clip-tests-before.log,clip-tests-after.log,
clip-browser-cases.json,vector-controls.json,hive-frozen.html,hive-literal.html,
hive-*-before.png,hive-*-after.png,ui-before.log,ui-qualified.log,
render-qualified.log,render-clips.log and layout/native qualification logs.
Pinned final host SHA-256:
bbea00e3ff61c45ba59b981ee38781c0bdb46c700d4fcb1d351cdda770965fb5.


<a id="ri-048"></a>

### RI-048 — Grid-item percentage sizes use the whole grid instead of their area

**Status:** FIXED, 2026-11-12. **Lambda blocker:** no. Native ownership
**D9.1.1v4 / D9.1.3v2**, temporary query scope **D5.5.1**.
[CSS Sizing §5.2.1](https://www.w3.org/TR/css-sizing-3/#cyclic-percentage-contribution)
and [CSS Grid track sizing](https://www.w3.org/TR/css-grid-1/#algo-content)
require cyclic percentage contributions to be separated from used item sizing.

Nova Benefits' hero is a 1160px, two-column `1fr 1fr` grid with a 64px gap.
Its first item is a flex column with `width:100%`. Intrinsic measurement used
1160px as that percentage's containing block, producing tracks of 1160px/380px.
The loaded, visible 560px hero image consequently began at x1244, outside the
viewport. Chromium instead gives 548px/548px tracks and image x632. Separately,
percentage-containing math bypassed the explicit-size exclusion and retained
its provisional whole-grid preferred size after track placement.

`calculate_grid_item_intrinsic_sizes` now uses the existing containing-block
scope for the unsolved contribution axis and the shared substituted CSS size
facts for percentage classification. Solved row-axis column geometry remains
available. Placement reevaluates percentage math with the existing owner-aware
length resolver against the solved area; border-box conversion remains shared.
No page selector, guessed size, min-width override or alternate CSS is added.

The same frozen original HTML/CSS restores 548px/548px tracks and the hero.
A diagnostic control changes only the first item's width:100% to auto; it works
on the old renderer, and the repaired original matches that control exactly
in pixels. The live page also restores the illustration. A frozen Zapier customer-story
page reproduces its displaced narrow hero on the previous engine and restores
the two-column heading/photo after the same native repair, with identical
HTML/CSS and no scripts. These controls isolate native layout; live score
movement can additionally include consent/content.

`GridPercentageContributionsUseTheUnsolvedArea` covers nine width/constraint
variants at 1160px and 1080px, with independent Chromium geometry. The previous
engine fails 16 assertions; all 18 case/phase geometry sets pass after repair.
Ten real PNG fixtures add owner-em and substituted mixed math: old 5 pass/5 fail,
new 10 pass, and independent Chromium confirms all 10 pixel/geometry sets.
`grid_percentage_area` tests live resize, font and variable changes: old 2/8,
new 8/8 assertions. Current milestone gates pass 84 native dynamic cases,
42 real CLI cases, eight DOM UI fixtures with 58 assertions, 574 AST cases,
3,851 recorded static layout cases (six skips), 630 JS layout cases and geometry
lint. The native aggregate retains 929 passes, one device skip and the known
RI-035 unavailable-voice failure. Input implementation is unchanged.

Evidence in Lambda `temp/online-visual-round12/`: `grid-before.log`,
`grid-after.log` (intermediate scope-only repair; final is native-dynamic.log),
`grid-browser.json`, `grid-png-before/after.log`, `grid-browser-png/results.json`,
`ui-before/final.log`, `nova-frozen-comparison.json`, `nova-browser-boxes.json`,
`nova-original-after-lambda-output.log`, and milestone logs. Final online
qualification and remaining severity counts are in Lambda `test/online/README.md`.

<a id="ri-049"></a>

### RI-049 — Root font-size evaluation leaves descendants on the initial rem basis

**Status:** FIXED, 2026-11-12. **Lambda blocker:** no. Native style/layout
ownership **D9.1.1v4 / D9.1.3v2**, query scratch **D5.5.1**; capture virtual time
remains **D10.1.7**. [CSS Values 4 font-relative lengths](https://www.w3.org/TR/css-values-4/#font-relative-lengths)
uses the initial font for the root's own font-relative font-size and the
computed root font for descendant rem lengths.

Zammad's company page sets the root to
`clamp(.875rem,.25rem + .8333vw,1rem)` at 1200px. While computing that value,
the general rem resolver primed root_font_size with the initial 16px. The
actual root font subsequently became 14px, but the cache was only published
when negative. Descendant layout retained 16px: the 4rem heading used 64px and
wrapped to two lines, while CSSOM correctly reported 56px. Root restyles could
also reuse the previous root basis for their own rem font-size.

The existing owner-aware length resolver now evaluates the root's own rem
font-size against the initial font without priming the descendant cache. The
font prepass publishes the computed root size before other root geometry;
root layout also publishes it on cached-style paths. The stored basis stays
unzoomed, so zoom is applied once by existing consumers. No clamp expression,
page selector or input stylesheet is rewritten by the repair.

The same frozen HTML/CSS, with scripts removed, now matches Chromium's 14px
root, one-line 56px heading at y252 and 52.5px paragraph at y336. The repaired
original and both old/new renderers of a diagnostic one-declaration 14px
control match exactly in pixels. The control is not a catalog or source edit.

`RootRelativeFontSizesPublishTheComputedRemBasis` covers nine font/variable/math/
shorthand/viewport/zoom variants with repeated forced recascades. The native
dynamic suite passes 85 cases. Eight real PNG cases change from one pass/seven
failures to eight passes; independent Chromium validates their geometry and
pixels, plus the native fixture's nine geometries. The live DOM UI fixture
changes from two of six pixels to six of six after root-variable mutations.
All 50 real CLI cases, nine UI fixtures/64 assertions, 574 AST cases, 3851 static
layout cases (six skips), 630 JS layout cases, five comparator tests and integer
cast lint pass. The native aggregate has 930 passes, one device skip and the
independent known RI-035 voice failure. Input implementation is unchanged.

Evidence: Lambda `temp/online-visual-round13/` contains the frozen original,
literal control, before/after PNGs, rem-controls.log, rem-png-before/after.log,
root-rem-browser.json, native-dynamic-final.log and qualification logs. The
initial native probe included an incorrect global-coordinate assertion against
parent-relative positions; its failure count is not used as repair evidence.
The corrected source checks root boundary padding directly. The compatible
integration host uses exact repaired root sources and current sibling archives;
the independent main-checkout paint API mismatch remains outside this repair.

<a id="ri-058"></a>

### RI-058 — Absolute grid placement sizes contents against the whole container

**Status:** FIXED, 2026-11-13. **Lambda blocker:** cleared for the selected
absolute-grid sizing cases. Native ownership **D9.1.1v4 / D9.1.3v2**, pass
scratch **D5.5.1**. [CSS Grid §9.1](https://www.w3.org/TR/css-grid-1/#abspos)
requires the resolved grid area to establish the positioned containing block;
out-of-flow placement does not create tracks.

Stripe's blog hero assigns its absolute figure `grid-area:image`. Native grid
placement only inspected numeric fields, whereas the current CSS projection
stores named references. Numeric diagnostic placement moved the outer box to
the right column, but its descendants had already been laid out against the
whole card. The later grid-specific constraint equation also overwrote authored
sizes and min/max constraints. A static grid or ordinary grid with a fixed child
could wrongly replace the actual containing block. Empty grids did not register
their named line/area aliases before resolving their positioned children.

The existing authored-edge resolution is extracted into `grid_resolve_item_edges`
and reused by in-flow and positioned consumers, retaining their distinct auto
edge rules. Named aliases are registered before the empty-grid branch. The
resolved area is borrowed through `AbsChildLayoutState` and `LayoutContextScope`
while the ordinary positioned solver computes style, size and descendants.
Late percentage passes reuse committed track geometry. The duplicate post-layout
grid constraint equation is removed; the actual containing-block check preserves
static-grid and viewport-fixed behavior. Inset-solved automatic heights now
provide a definite percentage basis through the existing percentage helper.

Nine independent Chromium geometry/pixel cases cover named/numeric placement,
percentage insets, explicit and maximum sizes, child box edges, mixed math,
static grids and viewport-fixed controls. Real PNG results change from 0/9 to
9/9. The live track/area/size fixture changes from 3/9 to 9/9 pixels. The native
case loads the same nine fresh inputs and repeats forced recascades; it passes.
No existing assertion, reference, admission cap or capture time is weakened.

The exact saved Stripe original now restores its 524px-wide image beside the
heading, author, copy and action; RI-059 separately repairs the card height.
Combined qualification: 89 native dynamic cases, 64 real CLI cases, eleven
DOM UI fixtures/81 assertions, 574 AST cases, 3851 static layout cases (six
skips), 630 JS layout cases, five comparator tests and integer-cast lint pass.
Native aggregate:1083 passes, one device skip and the existing RI-035 voice
failure. The current main host builds successfully; the older paint API build
mismatch no longer applies to this revision.

Evidence: Lambda `temp/online-visual-round14/` contains immutable before,
absolute-only and combined hosts; grid-absolute-before/after.log,
grid-absolute-browser.json, native-after-inset-basis.log, ui-before/final.log,
the identical stripe-frozen.html and before/after PNGs. An early native test
mutation sequence and a pre-link stale-binary invocation are explicitly marked
diagnostic/invalid in the checkpoint and are not final before/after counts.

<a id="ri-060"></a>

### RI-060 — Native style replacement retains removed padding in a grid child

**Fixed 2026-11-22.** Retained recascade reset border and margin storage but
kept the previous used padding. Replacing `grid-area:photo;padding:5px;
border:2px solid` with `grid-area:photo;width:calc(100% - 20px);height:50%`
therefore left the native grid item at 190px instead of 180px. The native
boundary reset now restores padding to its initial value before applying the
current cascade (**D5.5.3 / D9.1.1v4 / D9.1.3v2**).

`DynamicLayoutTest.GridStyleReplacementClearsRemovedPadding` reproduces the
original mutation with `DomElement::set_attribute`, native recascade and
forced layout. It fails against the pinned source and passes with the repair.
All 13 grid checks pass. The original investigation is in
`temp/online-visual-round14/`. New evidence is in
`temp/radiant-ordinary-fixes/`.

<a id="ri-059"></a>

### RI-059 — Nested grid publication replaces formatted height with a flow cursor

**Status:** FIXED, 2026-11-13. **Lambda blocker:** cleared for the selected
Stripe card and nested-grid sizing cases. Native ownership
**D9.1.1v4 / D9.1.3v2**, formatter scratch **D5.5.1**.

After RI-058 restores the image column, Stripe's white card still has only its
64px vertical padding while its text extends beneath it. In
`layout_grid_item_final_content_multipass`, the nested grid formatter computes
its own geometry, then the common footer overwrites its content height using
the parent's unadvanced flow cursor. Non-stretch alignment subsequently uses
that padding-only height. Independently, every nested grid is marked as having
an assigned block size, even when start/center alignment requires natural size;
this also stretches the card's automatic rows incorrectly.

The nested grid now keeps its formatter-owned dimensions and baselines. The
existing detailed self-alignment resolver is promoted through the module header,
not copied, to distinguish an authored or stretched block extent from natural
layout. The native card height becomes460px and its rows24/96/48/140/24px; the
image becomes524×428px. These sizes match Chromium for the same saved HTML/CSS;
the remaining9px global vertical offset belongs to other page geometry. Live
capture also restores the full card. No site CSS or input document is changed.

Five independent Chromium geometry/pixel cases cover automatic parent size,
start/center alignment, stretch and authored height. Real PNG cases change
from2/5 to5/5; the focused native case changes from10 assertion failures to a
pass across repeated recascades. The live alignment/size fixture changes from
6/8 to8/8 pixels. Combined native/CLI/UI/layout qualification and known failure
are recorded in RI-058. Evidence: Lambda `temp/online-visual-round14/`
grid-nested-before.log, grid-nested-browser.json, native-nested-before/after.log,
ui-nested-before.log, ui-final.log and stripe-grid-after PNG/geometry logs.
Broader orthogonal, intrinsic and rare alignment combinations remain governed
by the existing partial-support matrix; this is a bounded common-page repair.


<a id="ri-062"></a>

### RI-062 — Cyclic percentage flex children inflate the content minimum

**Status:** FIXED, 2026-11-13. **Lambda blocker:** clipped text in online
Azure cards is repaired; the page retains independent header and capture
omissions. Native ownership **D9.1.1v4 / D9.1.3v2**; bounded substituted facts
follow **D5.5.1**; selected capture timing **D10.1.7** is unchanged.

A 270px outer flex box with 16px padding and a nested row containing a fixed
64px icon, 16px gap and `width:100%` text area expanded the row to452.7px.
Chromium uses236px and wraps the text in156px. The intrinsic-child loop in
`layout_flex_measurement.cpp` overwrote the measured min-content contribution
with max-content times the authored percentage. This made a cyclic100%
preferred width act as a nowrap minimum. CSS Sizing3§5.2.1 and Flexbox§4.5
require the cyclic preferred percentage to behave as auto during this query.

The loop now retains the existing content-only measurement. The shared
`layout_axis_size_is_percentage` in `intrinsic_sizing.cpp` consumes existing
`layout_css_size_facts`, so substituted variables, math and logical dimensions
retain their percentage dependency rather than using provisional pixels.
Definite substituted lengths and authored nowrap minima remain effective.
No page-specific rule or alternate intrinsic measurement is added.

Thirteen real PNG controls in Lambda's
`test/online/render_flex_percentage.test.cjs` share inputs from
`flex_percentage_fixtures.cjs`: before8/13, repaired13/13. They cover percentages,
math, variables, logical size, auto/zero-minimum controls, fixed and nowrap
minima, and mutation after a layout read. Independent Chromium agrees with all
13 pixel witnesses. Initial witnesses intersected text ink in both engines;
final witnesses sample the background below the glyphs.

The identical saved Azure HTML/CSS with scripts removed isolates the repair:
its four media rows change from308.9/370.3/451.9/541.5px to236px each; titles
wrap instead of clipping at card edges. A small auto cross-height discrepancy
remains in the reduction and is not claimed repaired. The main navigation is
separate; the page is not declared visually cleared from this control alone.

Qualification at Lambda0c5c0ba0d / Radiant eb39862 / Input4d1ad20 with these
two Radiant source edits:89 native dynamic cases,91 online CLI/capture/comparator
cases,630 recorded JS layouts and16 static suites with4979 passes and295
recorded partials,0 regressions and0 skips. Layout integer-cast lint passes.
A mistakenly named focused `wpt-css-flexbox` category does not exist and is not
counted; the subsequent17 existing-category run is the accepted baseline.
Evidence is Lambda `temp/online-visual-round17/`, including before/final hosts,
`flex-before-final.log`, `flex-percentage.log`, `flex-pixel-browser.json`,
`azure-*-layout.json`, saved PNGs and `layout-results.json`. Qualified host SHA256
`10fae5cc038c9f09a06c06b189b5f604bb648b6e30a628e9f2c52edc96c3da25`.

<a id="ri-099"></a>

## RI-099 — Inline background curves exceeded their fragment bounds

**Status:** FIXED, 2026-11-17. Native paint ownership is D9.1.1v4/D9.1.3v2;
this is an implementation repair, without a formal ruling change.
`render_text_inline_background` generated a separate cubic path from the raw
top-left radius treated as uniform. A150px-wide text fragment with100px radius
already formed inverted curves; Tailwind's9999px pills generated huge paths
that covered unrelated viewport content. Cleanup.pictures' saved hydrated
hero had lime highlights covering its page, and raster differed from the
existing constrained SVG/PDF boundary path. LI-067 independently fixes its
collapsed auto-column geometry. Live client initialization is a separate issue.

The native inline fill now uses existing `render_path_uniform_corner`,
`constrain_corner_radii` and `render_path_create_rounded_rect` instead of the
handwritten curve. It preserves the existing current-transform recording and
inline-border pass. No new CSS radius grammar, per-corner/elliptical inline
support, site condition, capture policy or replacement content is introduced.
The common used-radius rule is [CSS Backgrounds3 §4.5](https://www.w3.org/TR/css-backgrounds-3/#corner-overlap).

Lambda `test/online/render_inline_rounded_background.test.cjs` checks exact
1000px/9999px equivalence, bounded painted area, transforms, and an offscreen
control. Before repair2of3cases fail; Chromium independently confirms all3
radius equivalences. Final focused checks and the saved/live page outcomes
are in Lambda `temp/online-visual-round43/checkpoint.md` and the comparison
report; no live clearance follows from saved-source evidence alone.

<a id="ri-100"></a>

## RI-100 — Line-fill lookahead entered inline button labels

**Status:** FIXED, 2026-11-17. Native layout ownership is D9.1.1v4/D9.1.3v2;
no formal ruling changes. Python.org's search form had the correct341.9px
intrinsic width, but its GO button wrapped, making the form98.6px instead
of57.4px high and moving the navigation/hero down. Trimming only the button's
internal indentation prevented the wrap; its width was unchanged.

An independent Arial fixture reproduced the fault with `display:inline` and
an indented button label; `inline-block` and `inline-flex` controls passed.
`layout_button_used_display` already converted authored inline buttons into
atomic boxes at layout. Intrinsic boundary classification still saw the raw
display, and `node_has_line_filled` independently entered raw inline children.
The outer source probe retained spaces from inside the button's own context,
reported overflow, and prematurely broke the preceding line. A temporary
geometry probe confirmed that the actual atomic margin box fit the row; the
probe was removed. No width, margin or wrapping tolerance was changed.

`layout_intrinsic_element_is_atomic_inline` now shares the existing button
used-display normalizer, and line-fill lookahead stops at the shared atomic
predicate. This implements [HTML Rendering §15.5.3](https://html.spec.whatwg.org/multipage/rendering.html#button-layout)
without changing ordinary inline whitespace propagation or flex/grid routing.

Evidence: Lambda `temp/online-visual-round44`, including minimal sources,
`png-before.log` (2genuine failures/2controls), four independent Chromium
compact/indented equivalences, and `native-final.log` (5passed). Native
`InlineButtonWhitespaceStaysInsideItsAtomicContext` checks four live display
transitions, row placement, height and an ordinary inline negative control.
Final PNG and saved/live page evidence are in the round44 checkpoint/report.
Capture admission, reference pixels, clocks and thresholds are unchanged.

Round45 milestone follow-up: the existing text-overflow atomic fixture exposed
a regression when the predicate re-resolved a constructed inline-block as its
synthetic tag's inline default. The button normalizer now consumes the retained
`element->display` role, as the previous atomic classifier did. The existing
projection test stays unchanged; live button transitions and PNG controls are
rechecked alongside it. This retains generated box roles without changing
ellipsis tolerances or source geometry (D9.1.1v4/D9.1.3v2).

<a id="ri-101"></a>

## RI-101 — Unknown custom-layout names disabled ordered generated effects

**Status:** FIXED, 2026-11-17. Native ownership is D9.1.1v4/D9.1.3v2;
selected-flow preservation is D5.5.1. No design ruling changes.
`layout_prepare_generated_effects` rejected an entire document when any node
had a custom-layout name, including unregistered generic `data-layout` values.
Those nodes already fall back to ordinary flow and have no callback that
could generate deferred effects. Mymind's unrelated `data-layout="grid"`
therefore sent its navigation through the legacy generated-content path.

Preparation now checks the existing callback registry. Only a registered
callback requires that custom-layout deferral; shadow/relevance deferrals
remain intact. Native controls distinguish missing and registered callbacks.
Three exact generated-content pixel controls compare an empty unregistered
layout sibling with an ordinary empty sibling. All fail before the combined
repair; after Input LI-069 and this change they pass, including text and both
pseudo sides. A local-invalidation-only candidate exposed the separate
pixel difference and was held rather than promoted.

Evidence: Lambda `temp/online-visual-round46`. Seven PNG and four focused
native tests, three independent Chromium equivalences and integer lint pass.
Two existing flow/pseudo-font ownership controls pass during implementation.
The saved Mymind source no longer emits its three restoration failures;
its live capture remains blank. No page clearance, exact live image match or
clean new full baseline is claimed. No URL condition, budget, admission or
renderer clock changed. The final replay guard is byte-identical to its
pre-investigation source.

<a id="ri-088"></a>

## RI-088 — Selected navigation flow failed restoration during measurement

**Status:** FIXED for the reported restoration failures, 2026-11-17.
D5.5.1 governs preservation; D9.1.1v4/D9.1.3v2 govern owner boundaries.
A release-visible rejection trace isolates host input revision changes with
unchanged style/mutation epochs. Input LI-069 repairs local derived-cascade
invalidation; RI-101 restores ordered generation for unregistered layout
aliases. The original saved source changes from three errors to zero.
The live Mymind page remains blank for a separate unisolated cause.

The earlier malformed CoreText-name observation is not used as evidence of
a dangling pointer. In Stellar's independent reproduction, LLDB finds those
same bytes in the font's name table; extracting the original WOFF2 name
stream independently through Node Brotli matches all982bytes exactly.
That confirms authored metadata for that reproduction. It does not prove
Mymind's separate font source or explain its live blank state.

<a id="ri-102"></a>

## RI-102 — Downloaded stylesheet completion never reached DOM listeners

**Status:** FIXED for network-managed stylesheets, 2026-11-17.
Ownership follows D9.1.1v4 / D9.1.3v2; precise callback pins and execution
ownership follow D5.5.1 / D6.4.1. No design ruling changes.

Vite's CSS preload promises remained pending although Radiant downloaded and
parsed their links. `process_css_resource` never published load/error through
the existing resource observer. Empty CSS also returned before publication.
CSS now uses the same pinned completion queue as images, retaining the old
image API and one-pointer observer ABI. Empty stylesheets publish an empty
sheet; read/parse/download failures publish error.

Lambda's load-readiness loop now drains completion and promise jobs before
window.load and discovers resources added by those callbacks. Its existing
250ms bound and renderer clock are unchanged. The callback drain shares the
bootstrap RuntimeExecutionScope: entering a fresh synthetic-event turn here
cleared pending bootstrap mutation records and lost the post-script cascade.

Evidence: Lambda `temp/online-visual-round47`. Five HTTP completion cases
cover listeners before/after insertion, empty/error CSS, and chained sheets;
they check exactly-once events, receiver/target, CSSOM, load order and pixels.
The original five resource-discovery controls remain unchanged. All ten pass;
the native observer compatibility test passes. The combined candidate also
passes ten DOM comment/insertion controls and three Input tests. The milestone release baseline initially reports 10 failures. Restoring owned
fragment-text occurrences in Lambda clears all nine persistent cases in focused
checks, and the initial jQuery failure passes directly. The full gate was not
repeated; the round47 report records that qualification limit.
Local files without a network manager are outside this reproduction. No URL
condition, admission-budget change or unsupported large feature is added.
The event requirement is [HTML linked stylesheets](https://html.spec.whatwg.org/multipage/semantics.html#link-type-stylesheet).

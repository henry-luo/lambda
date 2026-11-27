# Radiant Issue Ledger

Working issue record for Radiant's native layout, rendering, event-host and
state-store code under D9.1.1v4 and D9.1.3v2. This ledger records issues and
evidence; design rulings remain in the formal specifications.

Use stable IDs `RI-001`, `RI-002`, and so on. For each new issue record its
status, affected code or test, reproduction, observed and expected behavior,
and whether it blocks work in Lambda. Cite the applicable `S#` or `D#` ruling
when one covers the issue. Record uncertain causes as unconfirmed, link any
owner-side fix and verification. Move resolved entries to the
[fixed issue ledger](<Radiant_Issue_Ledger (fixed).md>), preserving their IDs
and leaving an anchor link here for existing references.

The [historical Lambda-hosted Radiant ledger](radiant/Radiant_Issue_Ledger.md)
retains existing `RAD` IDs and mixed issue history.
Native Radiant entries have been moved below with their original IDs; mixed,
Lambda-owned, input-owned and resolved records remain there. The imported
observations still require re-verification. Add new Radiant issues here.

## Open issues

<a id="ri-265"></a>

### RI-265 — Viewer mode changes retain rebuilt presentation storage

**Open, observed 2026-11-27.** Run `make test-viewer-navigation-memory` to
replay 14 documents in one release viewer. The replay changes modes, scrolls
highlighted source, and performs edits. Under the document lifetime and
incremental presentation rules (D5.5.1–D5.5.3), retired occurrences should
release their storage once live references are gone. Release allocator
snapshots between rounds 2 and 4 showed about 29 MiB per round in the
mixed replay before the validator fix, compared with about 6 MiB per round
when replaying previews alone. The viewer's layout property pool,
layout Input pool, and DOM document pool accounted for about 15 MiB per
round.

**Follow-up, 2026-11-27.** Editing event data now uses GC-owned values
(D6.3.3). Reactive HTML insertion releases its temporary parse tree after
publishing the copied occurrences. Copied element types build their field
index once in the destination owner (D5.2.5). Together with RI-260, these
changes reduced tracked live storage at the end of mixed round 5 from
439 MiB to 405 MiB. The revised run still added 71.5 MiB between rounds 2
and 5 after explicit collection. The active view-property pool gained
17.2 MiB, the active DOM document pool 12.8 MiB, and the layout Input pool
12.9 MiB. These are live allocation bytes. Layout retention remains open.

The rich Markdown control traced most of the live view-property growth to
obsolete editor subtrees pinned by template state. With model-keyed collection
(S12.1.3v6, D6.3.3), the control adds about 5 KiB of view properties per round,
down from 4 MiB. The style trace also found old generations referenced only
by retired synthetic markers. View teardown now drops these bindings and
releases the marker text. The mixed replay holds four stylesheet pools and
adds about 0.1 MiB per round in the view-property pool. After the scoped
Markdown parser change, tracked live growth from rounds 2 through 5 fell from
25.1 MiB per round to 9.2 MiB, with 334 MiB live at round 5. The layout Input
pool still adds about 4.3 MiB per round for copied type and field backing, and
the main document's Input arena adds about 1.7 MiB. Their storage stays with
the window after its replaced occurrences retire (D5.5.1–D5.5.3).
The cascade scratch index now uses the subtree being styled instead of the
window's lifetime allocation count. A focused test retires 512 elements and
checks that styling the same retained subtree uses the same scratch space.
The latest full release GTest reached round 21 of 24 before its 900-second
viewer deadline. Its last process footprint was 202 MiB above the median of
rounds 4–6. The 64 MiB growth budget remains in effect.

<a id="ri-264"></a>

### RI-264 — Nested row subgrids do not share their parent tracks

**Open, 2026-11-27.** Figma Pricing uses three nested `subgrid` row axes.
Its original overlap also exposed the independent flexible-track defect
fixed by RI-263. With that defect repaired, both ordinary captures have
legible first-row pricing panels and clear high severity (**D10.1.7**).
The reduced authored structure still lays out as standalone grids in
Radiant. Chromium shares eight row tracks and sizes the fixture to 580 px.
The standalone control measures 472 px in both engines. Parser support,
projection of intrinsic contributions to the parent, and shared used tracks
belong to Input and Radiant (**D9.1.1v4 / D9.1.3v2**).
[CSS Grid Level 2 §9](https://www.w3.org/TR/css-grid-2/#subgrids) ·
[Round 223](../test/online/comparison/repairs.html#round-223).

<a id="ri-263"></a>

- [RI-263 — Indefinite grid tracks collapse or absorb their gutters](<Radiant_Issue_Ledger (fixed).md#ri-263>).

<a id="ri-262"></a>

- [RI-262 — Windowless file export skips native WebGL host acquisition](<Radiant_Issue_Ledger (fixed).md#ri-262>).

<a id="ri-261"></a>

- [RI-261 — A ratio-sized flex badge loses its descendant percentage height](<Radiant_Issue_Ledger (fixed).md#ri-261>).

<a id="ri-260"></a>

### RI-260 — Detached frame documents accumulate during viewer navigation

**Open, observed 2026-11-26.** Repeatedly switch between a local HTML page
and a text file in `lambda view`. The allocator report retains a separate
set of document, view and Input pools for each retired frame. The frame
teardown path transfers ownership to the parent's resources, which preserve
storage for exposed wrappers and adopted nodes until parent close. Timely
retirement needs to track those lifetime edges and wait until the relevant
evaluators are quiescent (D5.5.1, D6.3.3). This affects the mixed-document
memory gate. Transformed inline previews have a separate lifetime.

**Partial fix, 2026-11-27.** Reclaim detached frames between evaluator turns
when their storage has no external wrapper, adopted node, or shared script
owner. Foreign wrappers provide a weakly tracked hold on their native
document. Renderer-owned child documents no longer receive a second strong
wrapper root in the parent evaluator. In a release replay of the same HTML
and EPUB fixtures, the old viewer added two document pools each round. The
revised viewer held three live document pools across all 24 rounds. A
detached frame remained readable through a live wrapper, and the iframe
retirement replay passed under forced garbage collection and freed-storage
poisoning (D6.3.3).

<a id="ri-259"></a>

- [RI-259 — Cached previews repeat stylesheet, font and layout allocation](<Radiant_Issue_Ledger (fixed).md#ri-259>).

<a id="ri-258"></a>

- [RI-258 — A broad rebuild loses the input measurements of its completed container cascade](<Radiant_Issue_Ledger (fixed).md#ri-258>).

<a id="ri-257"></a>

- [RI-257 — SVG auto-height measurement ignores its resolved width](<Radiant_Issue_Ledger (fixed).md#ri-257>).

<a id="ri-256"></a>

- [RI-256 — Flex layout loses ratio-transferred height](<Radiant_Issue_Ledger (fixed).md#ri-256>).

<a id="ri-255"></a>

- [RI-255 — Shadow stylesheets skip their own host](<Radiant_Issue_Ledger (fixed).md#ri-255>).

<a id="ri-254"></a>

- [RI-254 — Nested flex height queries lose the percentage containing width](<Radiant_Issue_Ledger (fixed).md#ri-254>).

<a id="ri-253"></a>

- [RI-253 — Loading animation frames silently age held CSS effects](<Radiant_Issue_Ledger (fixed).md#ri-253>).

<a id="ri-252"></a>

- [RI-252 — Demo tile hover triggers a full catalog layout](<Radiant_Issue_Ledger (fixed).md#ri-252>).

<a id="ri-251"></a>

- [RI-251 — Widening a window retains narrow flex and grid geometry](<Radiant_Issue_Ledger (fixed).md#ri-251>).

<a id="ri-250"></a>

- [RI-250 — Demo window resizing repeats shadow and cascade work](<Radiant_Issue_Ledger (fixed).md#ri-250>).

<a id="ri-249"></a>

- [RI-249 — macOS clears the visible drawable during native resize](<Radiant_Issue_Ledger (fixed).md#ri-249>).

<a id="ri-248"></a>

- [RI-248 — Native resize presents a stale frame before repaint](<Radiant_Issue_Ledger (fixed).md#ri-248>).

<a id="ri-247"></a>

- [RI-247 — Collapsed table borders spill out of scroll panes](<Radiant_Issue_Ledger (fixed).md#ri-247>).

<a id="ri-246"></a>

- [RI-246 — Rounded inline backgrounds spill past rectangular scroll clips](<Radiant_Issue_Ledger (fixed).md#ri-246>).

<a id="ri-245"></a>

### RI-245 — Textarea caret needs affinity at a soft-wrap boundary

**Status:** Open, observed 2026-11-25. **Owner:** Radiant text-control
selection and painting (D9.1.3v2). This limits wrapped textarea navigation.

In `test/ui/test_form_input.html`, enter enough text to wrap across three
lines, click the start of the second line, and press End. The selection offset
reaches the end of that visual line, but the caret paints at the start of the
third line. The shared byte offset identifies both visual positions, and the
selection state currently has no affinity to distinguish them. A fix needs
to preserve visual affinity through pointer and keyboard selection, redraw,
and text-control state transitions. Reproduction and capture are in
`temp/textarea-end-boundary.json` and `temp/textarea-end-boundary.png`.

<a id="ri-244"></a>

- [RI-244 — Textarea caret geometry ignores soft-wrapped lines](<Radiant_Issue_Ledger (fixed).md#ri-244>).

<a id="ri-243"></a>

- [RI-243 — Default focus outline misses dialogs and sits away from text fields](<Radiant_Issue_Ledger (fixed).md#ri-243>).

<a id="ri-241"></a>

### RI-241 — Automatic SVG export clips diagrams to the initial viewport

**Status:** Open, observed 2026-11-25. **Owner:** Radiant continuous export
(D9.1.3v2, D9.5.6v2).

Run `./lambda.exe render test/lambda/graph/upstream/d2/flow.d2 -o temp/graph-flow.svg`
using Lambda `9d907361f3` with the upstream graph fixes. The output SVG viewBox is
1250 × 850 CSS px and contains a clip at the initial 1200 × 800 viewport.
The graph starts at y = 20 and its measured height is 940.8 px.
The final nodes and their links fall past the clip. Automatic export should
include the graph's visible extent. The reference test supplies explicit
dimensions that fit its fixtures, so this issue does not block that test.
The extent and root clip behavior in continuous export need owner-side tracing.

<a id="ri-239"></a>

- [RI-239 — Layout redirects leak state created by the source document](<Radiant_Issue_Ledger (fixed).md#ri-239>).

<a id="ri-238"></a>

- [RI-238 — Layout format detection reads the URL query and fragment](<Radiant_Issue_Ledger (fixed).md#ri-238>).

<a id="ri-237"></a>

- [RI-237 — Native Canvas pixel readback is missing](<Radiant_Issue_Ledger (fixed).md#ri-237>).

<a id="ri-236"></a>

- [RI-236 — Wrapping flex children lose fitted cross sizes](<Radiant_Issue_Ledger (fixed).md#ri-236>).

<a id="ri-235"></a>

- [RI-235 — Late positioning stretches intrinsic popovers](<Radiant_Issue_Ledger (fixed).md#ri-235>).

<a id="ri-234"></a>

- [RI-234 — Native edge scrolling retains a retired text row](<Radiant_Issue_Ledger (fixed).md#ri-234>).

<a id="ri-233"></a>

- [RI-233 — Retained paint reuses an old selection highlight](<Radiant_Issue_Ledger (fixed).md#ri-233>).

<a id="ri-232"></a>

- [RI-232 — Pointer selection collapses on indented source lines](<Radiant_Issue_Ledger (fixed).md#ri-232>).

<a id="ri-231"></a>

- [RI-231 — Source editor loses bytes after NUL on paste and drag](<Radiant_Issue_Ledger (fixed).md#ri-231>).

<a id="ri-230"></a>

- [RI-230 — Form control labels truncate at embedded NUL](<Radiant_Issue_Ledger (fixed).md#ri-230>).

<a id="ri-229"></a>

- [RI-229 — Generated content from attribute values truncates at embedded NUL](<Radiant_Issue_Ledger (fixed).md#ri-229>).

<a id="ri-228"></a>

### RI-228 — FIXED

See [plain-text viewing and layout across embedded NUL](<Radiant_Issue_Ledger (fixed).md#ri-228>).

<a id="ri-227"></a>

- [RI-227 — Shadow fragments truncate positioned containing-block and paint walks](<Radiant_Issue_Ledger (fixed).md#ri-227>).

<a id="ri-226"></a>

- [RI-226 — SVG transition snapshots recompute context for resolved absolute lengths](<Radiant_Issue_Ledger (fixed).md#ri-226>).

<a id="ri-225"></a>

- [RI-225 — A stacking owner's overflow scope clips escaping positioned layers](<Radiant_Issue_Ledger (fixed).md#ri-225>).

<a id="ri-224"></a>

- [RI-224 — Empty stacking contexts cull visible positioned descendants](<Radiant_Issue_Ledger (fixed).md#ri-224>).

<a id="ri-223"></a>

- [RI-223 — Local inline-style writes retain unrelated relational dependencies](<Radiant_Issue_Ledger (fixed).md#ri-223>).

<a id="ri-222"></a>

- [RI-222 — Auto-width SVGs ignore a definite height in flow and flex sizing](<Radiant_Issue_Ledger (fixed).md#ri-222>).

<a id="ri-221"></a>

- [RI-221 — Root-positioned children retain offsets from an earlier layout height](<Radiant_Issue_Ledger (fixed).md#ri-221>).

<a id="ri-220"></a>

### RI-220 — FIXED

Native select popups choose a side with enough room and glyph replay clips
to the destination surface. See the
[GLB viewer dropdown crash](<Radiant_Issue_Ledger (fixed).md#ri-220>).
<a id="ri-219"></a>

- [RI-219 — Transparent video frames blend over uninitialized decoder storage](<Radiant_Issue_Ledger (fixed).md#ri-219>).

<a id="ri-218"></a>

- [RI-218 — Definitely sized grid images have no paint surface](<Radiant_Issue_Ledger (fixed).md#ri-218>).

<a id="ri-217"></a>

- [RI-217 — Grid spans across fractional tracks lose a definite size contribution](<Radiant_Issue_Ledger (fixed).md#ri-217>).

<a id="ri-216"></a>

- [RI-216 — Nested flex measurement inflates percentage-sized SVGs](<Radiant_Issue_Ledger (fixed).md#ri-216>).

<a id="ri-215"></a>

### RI-215 — Two expanded document-editor replays fail on the current checkout

**Open, 2026-11-22.** Run `test_ui_automation_gtest.exe --suite baseline
--test 'doc_editor_*'`. The tree-alignment replay looks for
`test/test_graph_parser_gtest.cpp`. Its source belongs to lambda-input at
`lambda-input/test/test_graph_parser_gtest.cpp` (**D9.1.1v4**).
The markup-formats replay expects the AsciiDoc title in the first preview
heading and receives “Section 1”. The heading mismatch remains unassigned
between input and the preview (**D10.1.2 / S12.1.3v6**).

Both assertion outcomes reproduce with the original editor source and the
pre-fix viewer. Evidence is under
`temp/radiant-ten-under-five/doc-regression-control/`.

<a id="ri-214"></a>

- [RI-214 — Replays repeat ownership walks after a DOM retirement](<Radiant_Issue_Ledger (fixed).md#ri-214>).

<a id="ri-213"></a>

- [RI-213 — Release build used a removed DOM validation cache API](<Radiant_Issue_Ledger (fixed).md#ri-213>).

<a id="ri-212"></a>

- [RI-212 — Unsubstituted CSS environment values poison layout coordinates](<Radiant_Issue_Ledger (fixed).md#ri-212>).

<a id="ri-211"></a>

- [RI-211 — Media commands before layout report an unavailable backend](<Radiant_Issue_Ledger (fixed).md#ri-211>).

<a id="ri-210"></a>

- [RI-210 — Opening playback retains a poster despite an available video frame](<Radiant_Issue_Ledger (fixed).md#ri-210>).

<a id="ri-209"></a>

- [RI-209 — Window load precedes required image/background completion](<Radiant_Issue_Ledger (fixed).md#ri-209>).

<a id="ri-208"></a>

- [RI-208 — Nested column flex text can undersize a ratio-dependent cross minimum](<Radiant_Issue_Ledger (fixed).md#ri-208>).

<a id="ri-207"></a>

- [RI-207 — Column flex aspect-ratio items undersize their automatic cross axis](<Radiant_Issue_Ledger (fixed).md#ri-207>).

<a id="ri-206"></a>

- [RI-206 — Single-line flex ratio minima use wrapped text height](<Radiant_Issue_Ledger (fixed).md#ri-206>).

<a id="ri-205"></a>

- [RI-205 — Release captures accept rejected HTTP document bodies](<Radiant_Issue_Ledger (fixed).md#ri-205>).

<a id="ri-204"></a>

- [RI-204 — Parsed eager images wait behind stylesheet completion](<Radiant_Issue_Ledger (fixed).md#ri-204>).

<a id="ri-203"></a>

- [RI-203 — Scalar height queries lose valid results on unrelated layout passes](<Radiant_Issue_Ledger (fixed).md#ri-203>).

<a id="ri-202"></a>

- [RI-202 — CSSOM dimensions expose the border box for content-box elements](<Radiant_Issue_Ledger (fixed).md#ri-202>).

<a id="ri-201"></a>

- [RI-201 — Relational dependency checks rebuild unrelated measurement text](<Radiant_Issue_Ledger (fixed).md#ri-201>).

<a id="ri-200"></a>

### RI-200 — Legacy frameset documents have no frame embedding/layout path — **FEATURE GAP**

**2026-11-22.** Native document rendering belongs to Radiant under
**D9.1.1v4 / D9.1.3v2 / D9.5.6v2**. Eclipse Help is a real HTML4 frameset:
its outer rows are 55,24,* and its three frame children load the banner,
toolbar and navigation/content documents. Chromium displays those panes;
the retained native capture is blank. The source is preserved at
temp/online-visual-round2/documents/eclipse_help.html and the current triage
contact is temp/online-visual-round165/triage-contact.png.

The existing frameset branch in layout_block.cpp supplies viewport dimensions
only. The native layout/render code has no frame-child embedding path; its
iframe support does not provide frameset track sizing or load frame sources.
This is a legacy HTML feature gap, not evidence that an ordinary CSS box
collapsed. A complete implementation needs nested row/column track layout,
frame document ownership and embedding, and is deferred under the online
review's instruction to avoid major new features. Eclipse remains high;
no synthetic pane, document rewrite or visual clearance is introduced.

<a id="ri-199"></a>

- [RI-199 — Native test harnesses failed to link in the full test build](<Radiant_Issue_Ledger (fixed).md#ri-199>).

<a id="ri-198"></a>

### RI-198 — Four named replays exceed five seconds in the full debug baseline

**Open, 2026-11-22.** After RI-197 and LI-098, the six reported cases pass
together in 2.171–3.880 s each on the macOS debug build. Running
`make run-radiant-baseline` with the original timing history and ordinary
worker settings passes the suite. The text-to-PDF replay takes 5.778 s,
Hamster editor 5.294 s, direct table 6.950 s and slide animations 5.681 s.
The Type 1 PDF and essay cases meet the five-second budget in that run.

The remaining profiles include live layout, PDF transformation and
background compilation under load. The event and frame contracts follow
D8.3.4 and D10.1.7. The underlying Scene3D budget also remains open in
RI-196. Results are under `temp/radiant-debug-regression/`.

After RI-214, the stock debug baseline passes with seven UI workers.
Text-to-PDF takes 4.973 s and essay toggle takes 4.531 s. The source scroll
reset takes 5.631 s, direct table 7.575 s and slide animations 6.089 s.
Two isolated debug passes meet the five-second target. See
`temp/radiant-ten-under-five/final-baseline/test_summary.json`.

**Release recheck, 2026-11-22.** The source scroll reset, text-to-PDF, slide
animations and direct table replays all pass with the runner configured for
seven workers. Their times are 2.409, 2.886, 3.036 and 3.454 s. This is a
four-case release run and the full debug baseline observation stays open
(**D8.3.4 / D10.1.7**). A rebuilt release host passes the same replays in
2.647–3.381 s. See `temp/radiant-ordinary-fixes/ri-198-release*.log`.

<a id="ri-197"></a>

- [RI-197 — Retained view state discards the DOM lifetime stamp](<Radiant_Issue_Ledger (fixed).md#ri-197>).

<a id="ri-196"></a>

### RI-196 — Interactive Scene3D View cases exceed the parallel debug budget

**Open, 2026-11-22.** Run the existing Radiant View baseline with the
`debug_native` executable and its normal nine-worker batch. The target is
under five seconds per case and under 38.3 seconds for the batch.
Collection spends most of its time loading, laying out and painting linked
documents. Midtown spends time building the cold document, drawing the
first frame and processing drive frames. Hamster spends most of its time
in live edits and frames. Meadow meets the case budget after retaining
seed-owned geometry.

The engine retains native resources and updates the requested frame under
**D5.5.2** and **D10.1.7**. The remaining timing gap affects the performance
target. The replay checks and timing artifacts are recorded in the
[UI tuning checkpoint](impl/Radiant_Impl_UI_Test_Tuning.md#current-checkpoint).

After RI-214, the five requested Scene3D cases each complete within 3.915 s
in two isolated debug passes. In the nine-worker baseline their times range
from 5.600 to 8.717 s. One of the largest sample costs is now loading and
painting the linked Collection documents or Midtown's first scene
(**D9.1.3v2 / D10.1.7**). See
`temp/radiant-ten-under-five/final-baseline/test_summary.json`.

**Release recheck, 2026-11-22.** Both Collection replays, Midtown desktop,
Hamster editor and Meadow pass in 1.031–3.793 s per case on the selected
release host. The five-case run does not qualify the full parallel debug
baseline (**D5.5.2 / D10.1.7**). A rebuilt release host passes the same
replays in 1.038–3.100 s. See
`temp/radiant-ordinary-fixes/ri-196-release*.log`.

<a id="ri-195"></a>

- [RI-195 — Headless Scene3D fixtures bypass the device mixer](<Radiant_Issue_Ledger (fixed).md#ri-195>).

<a id="ri-194"></a>

- [RI-194 — A flex-sized grid scroll container has no scroll range](<Radiant_Issue_Ledger (fixed).md#ri-194>).

<a id="ri-193"></a>

- [RI-193 — Auto grid rows grow to scroll containers' content in a definite container](<Radiant_Issue_Ledger (fixed).md#ri-193>).

<a id="ri-192"></a>

### RI-192 — Two Scene3dTest units in one process crash the scene3d unit binary

**Open, 2026-11-21.** `test/test_scene3d_unit_gtest.exe` exits with SIGSEGV
(139) when any two `Scene3dTest` cases run in one process. Examples are
`ParticlesMotionLifetimeCurvesAndNearZeroDragMatchAnalyticalControls` followed by
`ParticlesCpuBillboardsFaceRotatedCamerasWithoutAdvancingMotion`, and the two
`AnimationNumericStream*` cases. Each case passes alone. The crash reproduces
without the RI-180 repair, so it predates it. The cause is unconfirmed:
suspected fixture teardown or shared native state between cases. It does not
block Lambda work. Workaround: run each case in its own process with
`--gtest_filter`. RI-191 was verified that way (87 animation, motion and clip
cases).

**Recheck, 2026-11-22.** A fresh debug build at Lambda `3bed8e86ad` with
Radiant `7ca2432` passes both original pairs together in one process, repeated
ten times with native macOS graphics access. The historical cause remains
unconfirmed. See `temp/radiant-ordinary-fixes/ri-192-native-repeat.log`
(**D5.5.1 / D9.1.3v2**).

<a id="ri-191"></a>

- [RI-191 — Clips added by a scene3d re-render never receive host ticks](<Radiant_Issue_Ledger (fixed).md#ri-191>).

<a id="ri-190"></a>

- [RI-190 — CSS resource discovery repeatedly scans the DOM for a base URL](<Radiant_Issue_Ledger (fixed).md#ri-190>).

<a id="ri-189"></a>

- [RI-189 — Expired no-fill effects retain undeclared opacity](<Radiant_Issue_Ledger (fixed).md#ri-189>).

<a id="ri-188"></a>

- [RI-188 — First script-created remote stylesheet is never requested](<Radiant_Issue_Ledger (fixed).md#ri-188>).

<a id="ri-187"></a>

- [RI-187 — Snapshot downloads finish without publishing image state or callbacks](<Radiant_Issue_Ledger (fixed).md#ri-187>).

<a id="ri-186"></a>

- [RI-186 — Linear-gradient resolution discards a sole double-position color](<Radiant_Issue_Ledger (fixed).md#ri-186>).

<a id="ri-185"></a>

- [RI-185 — Sparse GIF ticks and completion select the wrong frame](<Radiant_Issue_Ledger (fixed).md#ri-185>).

<a id="ri-184"></a>

- [RI-184 — External script success never dispatches the element load event](<Radiant_Issue_Ledger (fixed).md#ri-184>).

<a id="ri-183"></a>

- [RI-183 — image-set string sources never enter the resource queue](<Radiant_Issue_Ledger (fixed).md#ri-183>).

<a id="ri-182"></a>

- [RI-182 — CSS-only local documents cannot start their first network resource](<Radiant_Issue_Ledger (fixed).md#ri-182>).

<a id="ri-181"></a>

- [RI-181 — Local HTML image/poster exports never initialize network discovery](<Radiant_Issue_Ledger (fixed).md#ri-181>).

<a id="ri-180"></a>

- [RI-180 — Authored fetchpriority never reaches resource scheduling](<Radiant_Issue_Ledger (fixed).md#ri-180>).

<a id="ri-179"></a>

- [RI-179 — Font inheritance overwrites local shorthand and re-evaluates ancestor declarations](<Radiant_Issue_Ledger (fixed).md#ri-179>).

<a id="ri-178"></a>

- [RI-178 — Loader computed-style queries ignore font shorthand components](<Radiant_Issue_Ledger (fixed).md#ri-178>).

<a id="ri-177"></a>

- [RI-177 — Grid alignment replaces final flex height with an intrinsic estimate](<Radiant_Issue_Ledger (fixed).md#ri-177>).

<a id="ri-176"></a>

- [RI-176 — Flexible spanning grid items retain a content-based automatic minimum](<Radiant_Issue_Ledger (fixed).md#ri-176>).

<a id="ri-175"></a>

- [RI-175 — CSSOM reports internal table rows as block](<Radiant_Issue_Ledger (fixed).md#ri-175>).

<a id="ri-174"></a>

- [RI-174 — Display rollback reapplies the discarded all reset and shows metadata text](<Radiant_Issue_Ledger (fixed).md#ri-174>).

<a id="ri-138"></a>

- [RI-138 — Trial flow resets the scroll position of an EPUB frame](<Radiant_Issue_Ledger (fixed).md#ri-138>).

<a id="ri-137"></a>

- [RI-137 — Inline flex exports a baseline before its children have line geometry](<Radiant_Issue_Ledger (fixed).md#ri-137>).

<a id="ri-136"></a>

- [RI-136 — Boxless DOM ancestors erase flex decoration offsets](<Radiant_Issue_Ledger (fixed).md#ri-136>).

<a id="ri-135"></a>

- [RI-135 — Retained descendants lose inherited text shadows](<Radiant_Issue_Ledger (fixed).md#ri-135>).

<a id="ri-134"></a>

- [RI-134 — Video snapshots reject decodable frames while playback opens](<Radiant_Issue_Ledger (fixed).md#ri-134>).

<a id="ri-133"></a>

- [RI-133 — Valid SVG images with leading XML comments are rejected](<Radiant_Issue_Ledger (fixed).md#ri-133>).

<a id="ri-132"></a>

- [RI-132 — Assigned inline measures are resolved as containing widths again](<Radiant_Issue_Ledger (fixed).md#ri-132>).

<a id="ri-131"></a>

- [RI-131 — Text balancing recursively measures its own atomic inline flow](<Radiant_Issue_Ledger (fixed).md#ri-131>).

<a id="ri-130"></a>

- [RI-130 — Text-clipped backgrounds ignore descendant glyphs](<Radiant_Issue_Ledger (fixed).md#ri-130>).

<a id="ri-129"></a>

- [RI-129 — Intrinsic inline flow loses the measured box's containing sizes](<Radiant_Issue_Ledger (fixed).md#ri-129>).

<a id="ri-128"></a>

- [RI-128 — Background keyframe shorthands discard their color component](<Radiant_Issue_Ledger (fixed).md#ri-128>).

<a id="ri-127"></a>

- [RI-127 — Render exports ignore the host-selected settling frame](<Radiant_Issue_Ledger (fixed).md#ri-127>).

<a id="ri-126"></a>

- [RI-126 — Intrinsic inline-height trials skip atomic placement and reset frame scrolling](<Radiant_Issue_Ledger (fixed).md#ri-126>).

<a id="ri-125"></a>

- [RI-125 — Absolute-child layout unwinds a foreign scratch allocation](<Radiant_Issue_Ledger (fixed).md#ri-125>).

<a id="ri-124"></a>

- [RI-124 — Float roundoff falsely wraps an exactly fitting atomic inline](<Radiant_Issue_Ledger (fixed).md#ri-124>).

<a id="ri-123"></a>

- [RI-123 — Intrinsic atomic-inline height loses the enclosing line strut](<Radiant_Issue_Ledger (fixed).md#ri-123>).

<a id="ri-122"></a>

### RI-122 — Named view-progress animation timelines are unsupported

**Status:** OPEN major feature gap, 2026-11-17. Blocks Nolla's visual parity;
independent native repairs can continue. Ownership follows **D9.1.1v4 /
D9.1.3v2**; initial capture clocks remain unchanged (**D10.1.7**).

Nolla's script-free hero and following panels declare a named view timeline,
animation-timeline and a containment/exit animation range. The style registry
and native animation subsystem do not implement those properties. The ordinary
animation path samples the parallax end values: the hero wrapper is at opacity
zero and translated 156.244px while its heading and other geometry exist.
Chromium's range-bound animation leaves the hero visible at initial scroll.
This explains the major blank main region on the same HTTP document
(29.85% mean error). See
[Scroll-driven Animations §3–§4](https://www.w3.org/TR/scroll-animations-1/#view-timelines).

The earlier comparison was unqualified because its temporary browser driver
assigned an absolute numeric currentTime to a progress-based timeline. The
driver now pauses progress timelines at their natural initial scroll position
and assigns numeric zero only to numeric times; the comparison completes.
No canonical capture clock, scroll position, style or reference is changed.
Named timeline lookup, progress/range calculation and sampling require a new
subsystem, outside this round's bug-fix scope. No fade suppression or native
timing exception is added.

Evidence in Lambda: temp/online-visual-round77/nolla-comparison.json,
the exact-source native/browser PNGs and nolla-probe-lambda-output.log.
The page remains high severity.

<a id="ri-121"></a>

- [RI-121 — HTTP media sources enter the scheduler after the snapshot wait](<Radiant_Issue_Ledger (fixed).md#ri-121>).

<a id="ri-120"></a>

- [RI-120 — SVG auto dimensions acquire a definite fallback natural size](<Radiant_Issue_Ledger (fixed).md#ri-120>).

<a id="ri-119"></a>

- [RI-119 — Positioned decorations make inline intrinsic heights stack](<Radiant_Issue_Ledger (fixed).md#ri-119>).

<a id="ri-118"></a>

- [RI-118 — Preferred widths leak into flex content-only intrinsic minima](<Radiant_Issue_Ledger (fixed).md#ri-118>).

<a id="ri-117"></a>

### RI-117 — Chromium ignores Readymag's missing SVG mask reference

**Status:** OPEN interoperability discrepancy, 2026-11-17; no native defect
confirmed. **Lambda blocker:** no. Preserve the existing native rule rather
than introducing a browser-matching exception. Ownership follows
**D9.1.1v4 / D9.1.3v2**; effects ownership **D5.5.1**.

Readymag's saved white card has a path mask URL naming an ID absent from the
entire document (zero mask elements). Four isolated controls retain the real
SVG and vary only that attribute and the CSS drop-shadow. Native output is
transparent with the missing mask, and white without it, independently of
the drop-shadow. Chromium paints white even with the missing reference.

[CSS Masking Level 1 §7.1](https://www.w3.org/TR/css-masking-1/#the-mask-image)
specifies a transparent-black layer for a nonexistent mask reference and
applies to SVG graphics elements. The existing native effect path implements
that rule intentionally. This is not evidence to suppress the mask or render
a replacement white shape. No renderer change or reference adjustment is
made. It explains this saved-source card discrepancy only; Readymag's blank
live startup remains independently unresolved.

Evidence: Lambda `temp/online-visual-round69/card-results.json`, the four
minimal documents and native PNGs, and `operate-static-browser.png`.
The existing qualified round68 renderer is unchanged. No additional baseline
is needed for this read-only diagnosis.

<a id="ri-116"></a>

- [RI-116 — Lifted layers retain an intermediate absolute ancestor's DOM offset](<Radiant_Issue_Ledger (fixed).md#ri-116>).

<a id="ri-115"></a>

- [RI-115 — Flex refinement forgets heights of items exposed through contents](<Radiant_Issue_Ledger (fixed).md#ri-115>).

<a id="ri-114"></a>

- [RI-114 — Positioned children beneath contents wrappers never receive layout](<Radiant_Issue_Ledger (fixed).md#ri-114>).

<a id="ri-113"></a>

- [RI-113 — Calculated preferred aspect ratios collapse auto-sized boxes](<Radiant_Issue_Ledger (fixed).md#ri-113>).

<a id="ri-112"></a>

- [RI-112 — A positioned document root disappears from raster output](<Radiant_Issue_Ledger (fixed).md#ri-112>).

<a id="ri-111"></a>

- [RI-111 — Inline-property sharing erases isolation and other semantic differences](<Radiant_Issue_Ledger (fixed).md#ri-111>).

<a id="ri-109"></a>

- [RI-109 — Relative HTML base URLs are ignored and absolute bases overwrite document identity](<Radiant_Issue_Ledger (fixed).md#ri-109>).

<a id="ri-110"></a>

- [RI-110 — Resource and stylesheet discovery activates inert HTML template contents](<Radiant_Issue_Ledger (fixed).md#ri-110>).

<a id="ri-108"></a>

- [RI-108 — Final grid percentage heights and intrinsic image ratios lose their current constraints](<Radiant_Issue_Ledger (fixed).md#ri-108>).

<a id="ri-107"></a>

- [RI-107 — Reuse initially parsed stylesheets when delivering prefetched link completion](<Radiant_Issue_Ledger (fixed).md#ri-107>).

<a id="ri-106"></a>

- [RI-106 — Lifted positioned children overpaint exported ancestor outlines](<Radiant_Issue_Ledger (fixed).md#ri-106>).

<a id="ri-105"></a>

- [RI-105 — CSS Shapes WPT contour and inline wrapping failures](<Radiant_Issue_Ledger (fixed).md#ri-105>).

<a id="ri-104"></a>

- [RI-104 — Intrinsic margins use the measuring parent's variable and font scope](<Radiant_Issue_Ledger (fixed).md#ri-104>).

<a id="ri-103"></a>

- [RI-103 — Final flex content discards its grid-assigned cross size](<Radiant_Issue_Ledger (fixed).md#ri-103>).

<a id="ri-102"></a>

- [RI-102 — Downloaded stylesheet completion never reached DOM listeners](<Radiant_Issue_Ledger (fixed).md#ri-102>).

<a id="ri-101"></a>

- [RI-101 — Unknown custom-layout names disabled ordered generated effects](<Radiant_Issue_Ledger (fixed).md#ri-101>).

<a id="ri-100"></a>

- [RI-100 — Line-fill lookahead entered inline button labels](<Radiant_Issue_Ledger (fixed).md#ri-100>).

<a id="ri-099"></a>

- [RI-099 — Inline background curves exceeded their fragment bounds](<Radiant_Issue_Ledger (fixed).md#ri-099>).

<a id="ri-098"></a>

- [RI-098 — Rounded inset glyph and hit clips use unnormalized radii](<Radiant_Issue_Ledger (fixed).md#ri-098>).

<a id="ri-097"></a>

- [RI-097 — Nested zero-level foreground paints below lifted absolute background](<Radiant_Issue_Ledger (fixed).md#ri-097>).

<a id="ri-096"></a>

- [RI-096 — Static flex/grid items ignore z-index](<Radiant_Issue_Ledger (fixed).md#ri-096>).

<a id="ri-095"></a>

- [RI-095 — Flex-assigned grid height is replaced by intrinsic row height](<Radiant_Issue_Ledger (fixed).md#ri-095>).

<a id="ri-094"></a>

- [RI-094 — Individual translate loses affine percentage calculations](<Radiant_Issue_Ledger (fixed).md#ri-094>).

<a id="ri-093"></a>

- [RI-093 — Assigned grid/flex float contexts misplaced or omitted exclusions](<Radiant_Issue_Ledger (fixed).md#ri-093>).

<a id="ri-092"></a>

- [RI-092 — Intrinsic column height omits definite child flex bases](<Radiant_Issue_Ledger (fixed).md#ri-092>).

<a id="ri-091"></a>

- [RI-091 — Isolated native test links import conflicting full-host providers](<Radiant_Issue_Ledger (fixed).md#ri-091>).

<a id="ri-090"></a>

- [RI-090 — Home and End stop at text fragments beside inline atoms](<Radiant_Issue_Ledger (fixed).md#ri-090>).

<a id="ri-089"></a>

- [RI-089 — Min-content minimum widens floated intrinsic contribution boxes](<Radiant_Issue_Ledger (fixed).md#ri-089>).

<a id="ri-088"></a>

- [RI-088 — Selected navigation flow failed restoration during measurement](<Radiant_Issue_Ledger (fixed).md#ri-088>).

<a id="ri-087"></a>

- [RI-087 — Propagated body overflow clips its own zero-height box](<Radiant_Issue_Ledger (fixed).md#ri-087>).

<a id="ri-086"></a>

- [RI-086 — Empty-child root collapse erases resolved minimum height](<Radiant_Issue_Ledger (fixed).md#ri-086>).

<a id="ri-085"></a>

- [RI-085 — Wide SVG Gaussian kernels exhaust the work budget](<Radiant_Issue_Ledger (fixed).md#ri-085>).

<a id="ri-084"></a>

- [RI-084 — Static effect contexts lose positioned descendants and empty-box backdrops](<Radiant_Issue_Ledger (fixed).md#ri-084>).

<a id="ri-083"></a>

- [RI-083 — Early flex intrinsic widths omit deferred scalar gaps](<Radiant_Issue_Ledger (fixed).md#ri-083>).

<a id="ri-082"></a>

- [RI-082 — Gaussian work accounting charges skipped flat-run convolutions](<Radiant_Issue_Ledger (fixed).md#ri-082>).

<a id="ri-081"></a>

- [RI-081 — Wrapped flex height queries omit item cross margins](<Radiant_Issue_Ledger (fixed).md#ri-081>).

<a id="ri-080"></a>

- [RI-080 — Nested ratio height queries consume parent free space and stale SVG heights](<Radiant_Issue_Ledger (fixed).md#ri-080>).

<a id="ri-079"></a>

- [RI-079 — Unmatched stylesheet icons exhaust resource discovery](<Radiant_Issue_Ledger (fixed).md#ri-079>).

<a id="ri-078"></a>

- [RI-078 — Absolute percentage math keeps a provisional containing height](<Radiant_Issue_Ledger (fixed).md#ri-078>).

<a id="ri-077"></a>

- [RI-077 — SVG inherited reads repeat shared ancestor declaration queries](<Radiant_Issue_Ledger (fixed).md#ri-077>).

<a id="ri-076"></a>

- [RI-076 — SVG percentage viewport attributes inflate intrinsic height](<Radiant_Issue_Ledger (fixed).md#ri-076>).

<a id="ri-075"></a>

- [RI-075 — Canvas general matrix operations are unavailable to the DOM host](<Radiant_Issue_Ledger (fixed).md#ri-075>).

<a id="ri-074"></a>

- [RI-074 — A min-content minimum incorrectly caps the preferred width](<Radiant_Issue_Ledger (fixed).md#ri-074>).

<a id="ri-073"></a>

- [RI-073 — Cyclic percentage invalidation discards replaced media height](<Radiant_Issue_Ledger (fixed).md#ri-073>).

<a id="ri-072"></a>

- [RI-072 — Atomic inline alignment loses enclosing relative Y offsets](<Radiant_Issue_Ledger (fixed).md#ri-072>).

<a id="ri-071"></a>

- [RI-071 — Absolute and relative zero-level siblings paint in separate passes](<Radiant_Issue_Ledger (fixed).md#ri-071>).

<a id="ri-070"></a>

- [RI-070 — Inherited percentage width retains the parent's used width](<Radiant_Issue_Ledger (fixed).md#ri-070>).

<a id="ri-069"></a>

- [RI-069 — Height queries omit width expressions and constraints](<Radiant_Issue_Ledger (fixed).md#ri-069>).

<a id="ri-068"></a>

- [RI-068 — Grid row contributions reuse provisional whole-grid widths](<Radiant_Issue_Ledger (fixed).md#ri-068>).

<a id="ri-067"></a>

- [RI-067 — Row-flex cross heights reuse the preferred width after flexing](<Radiant_Issue_Ledger (fixed).md#ri-067>).

<a id="ri-066"></a>

- [RI-066 — Auto flex columns measure empty wrappers and unlaid-out child boxes](<Radiant_Issue_Ledger (fixed).md#ri-066>).

<a id="ri-065"></a>

- [RI-065 — Structured inline breaks understate nested flex height](<Radiant_Issue_Ledger (fixed).md#ri-065>).

<a id="ri-064"></a>

- [RI-064 — Inline CSS image URLs never enter snapshot resource discovery](<Radiant_Issue_Ledger (fixed).md#ri-064>).

<a id="ri-063"></a>

- [RI-063 — Decoded video overwrites foreground page content](<Radiant_Issue_Ledger (fixed).md#ri-063>).

<a id="ri-062"></a>

- [RI-062 — Cyclic percentage flex children inflate the content minimum](<Radiant_Issue_Ledger (fixed).md#ri-062>).

<a id="ri-061"></a>

### RI-061 — Bounded snapshot resource grace can leave major media unavailable

**Status:** OPEN capture qualification limitation, 2026-11-13. **Lambda
blocker:** no for the selected grid repairs; limits interpretation of some
fresh media differences. Native ownership **D9.1.1v4 / D9.1.3v2**, finite
capture frame contract **D10.1.7**. The resource scheduler/priority contribution
beyond the observed grace expiry remains **unconfirmed**.

The first round14 captures omit large images on Cofounder505, Banky121,
Amperos565 and Pelago234. Their resource summaries respectively report71/85,
33/37,63/81 and37/39 completed at about1000ms, with no resource failures.
`snapshot_resource_wait_timeout_ms` in `network_integration.cpp` defaults to
1000ms; snapshot preparation then flushes updates and proceeds even with
outstanding optional resources. This observation does not identify every pending
request or establish one missing-image cause for all pages.

Alternating current-head before/repaired hosts under identical capture settings
later report85/85 for Cofounder,37/37 for Banky and81/81 for Amperos. Both hosts
restore those images. Pelago remains38/39 at the same grace. Thus a shared
regression from RI-058/RI-059 is not established by the initial score movement;
cache/network readiness remains a plausible contributor. These diagnostics do
not replace the canonical captures or claim stable initial readiness.

No grace, parent deadline, admission rule or virtual time is changed, and no
later media frame is substituted. Further work must isolate the required
dependency/priority or capture-readiness policy before changing behavior;
raising a per-site timeout is not a root-cause repair. Evidence: Lambda
`temp/online-visual-round14/media-readiness-controls.json/log`, alternating
before/final PNGs, and canonical `test/online/snapshots/` logs for these labels.

<a id="ri-060"></a>

- [RI-060 — Native style replacement retains removed padding in a grid child](<Radiant_Issue_Ledger (fixed).md#ri-060>).

<a id="ri-059"></a>

- [RI-059 — Nested grid publication replaces formatted height with a flow cursor](<Radiant_Issue_Ledger (fixed).md#ri-059>).

<a id="ri-058"></a>

- [RI-058 — Absolute grid placement sizes contents against the whole container](<Radiant_Issue_Ledger (fixed).md#ri-058>).

<a id="ri-057"></a>

- [RI-057 — Sizing and noscript snapshot regressions in the Lambda layout gate](<Radiant_Issue_Ledger (fixed).md#ri-057>).

<a id="ri-056"></a>

- [RI-056 — Native input-value test target does not link its CSS resource call](<Radiant_Issue_Ledger (fixed).md#ri-056>).

<a id="ri-055"></a>

- [RI-055 — Paint engine lacks the M3 wet-media kernels](<Radiant_Issue_Ledger (fixed).md#ri-055>).

<a id="ri-054"></a>

- [RI-054 — Owner-held font fixture lookup under Lambda integration](<Radiant_Issue_Ledger (fixed).md#ri-054>).

<a id="ri-053"></a>

- [RI-053 — Historical P7 comparison artifacts are unavailable in the current checkouts](<Radiant_Issue_Ledger (fixed).md#ri-053>).

<a id="ri-052"></a>

### RI-052 — Existing empty-cell table baseline assertion differs by 10px

**Status:** open, qualification limitation, 2026-11-12. Native ownership:
**D9.1.1v4 / D9.1.3v2**. Cause is **unconfirmed**.

`SecondaryViewTest.TableBaselineUsesFirstNestedLineOrTheEmptyCellContentEdge`
reports glyph baseline 33 versus empty-cell y 10 + expected 33 = 43, for both fixed
and auto table layout. It reproduces identically with pagination source from
`340ad59` and the current source, with all remaining linked objects and flags
held constant. The full current Lambda paged inventory passes 504/505 after the
new nested-width acceptance and owner font lookup correction.

Evidence in Lambda-opus: `temp/paged-p7-integration/paged-final.xml`,
`control/table.xml`, `control/receipt.json` and `updated-focused.xml`.
This blocks an all-green legacy paged integration claim; it does not block the
19 focused native fragmentation cases or the 947-pass owner baseline. Leave the
assertion visible until its layout or coordinate-oracle cause is established.

Continuation evidence: Lambda-opus `temp/paged-advanced-integration/paged.xml`
still passes 504/505, with the same failure messages as `paged-final.xml`.
The expanded Radiant core now passes 60 focused cases, 988 owner-native cases
(one disabled) and 4,481 layout cases (six skips). RI-052 stays open.

Reservation/nested-row continuation: Lambda-opus
`temp/paged-reservations-integration-final/paged.xml` remains 504/505 with the
same assertion messages. The final owner increment passes 82 focused cases,
1,010 native cases (one disabled) and 4,481 layout cases (six skips).

**2026-11-22 recheck.** Chromium 148 and the native paged test put the
nested line and the image at a baseline 23px below the row top for fixed and
auto table layout. Varying the empty cell's height from 0px to 60px increases
the row height when needed while leaving that baseline at 23px in Chromium.
Radiant's existing empty-cell change (`b17a3c0`) captures the content edge
before the cell's height contributes to the row minimum. The Lambda
integration assertion still derives its expected baseline from the empty
cell's specified 30px height plus its top border and padding. Native
`EmptyTableCellDoesNotRaiseTextBaselineToSpecifiedHeight` passes.
The integration assertion remains open under **D9.1.1v4 / D9.1.3v2**.
Evidence: `temp/radiant-ledger-continuation/{browser-probes.log,table-native-before.log}`.

<a id="ri-051"></a>

### RI-051 — Current full PDF/export qualification retains six unchanged failures

**Status:** open, qualification limitation, 2026-11-12. Ownership of native
qualification: **D9.1.1v4 / D9.1.3v2**; underlying causes are **unconfirmed**
except the Lambda-owned archive sentinel recorded as LR12-50.

The current 472-case PDF/export inventory completes with 466 passes and six
failures: inside image marker raster parity, background shorthand SVG resource
text, grid font-unit SVG geometry, RTL two-line clamp raster parity, four Ghent
ReadMes import baseline rows, and the archive sentinel rejected by the independent
reference reader. Each exact failure message reproduces with pagination source
from `340ad59` and otherwise identical current objects, headers and flags. The
four WebGL cases pass with normal macOS graphics access after their first sandbox
run stalled creating its hidden context. All 472 case names are accounted for;
the interrupted raw run is preserved rather than rewritten as passing XML.

Reproduction and evidence in Lambda-opus: `temp/paged-p7-integration/pdf.log`,
`pdf-gpu.xml`, `pdf-control.xml`, `pdf-summary.json`, `receipt.json` and
`control/receipt.json`. Both original and isolated-copy PDF baseline hashes are
unchanged. Current native core controls under Radiant's `temp/paged-p7-final/`
pass separately. This blocks full P7 PDF/export closure, not the delivered native
float/width increments. Do not reset references or change tolerances to close it.

Continuation evidence: Lambda-opus `temp/paged-advanced-integration/pdf.xml`
reruns 90 affected PDF/FO/native cases including these six failure controls:
84 pass and the six exact failure-message profiles still match `pdf-control.xml`.
Both baseline copies remain unchanged. Radiant
`temp/paged-advanced-final/report.json` passes ten independent export controls.
This subset does not replace the earlier 472-case inventory.

Reservation/nested-row continuation: Lambda-opus
`temp/paged-reservations-integration-final/pdf.xml` remains 84/90 with exactly the
previous six failure messages and unchanged baseline copies. Radiant
`temp/paged-reservations-final/report.json` passes sixteen independent exported
controls. RI-051 remains open; no protected references were changed.

<a id="ri-050"></a>

### RI-050 — Reservation/nested-row pagination increment increases composition cost

**Status:** open, performance qualification limitation, 2026-11-12. Native
ownership: **D9.1.1v4 / D9.1.3v2**. The cause within `layout_paged.cpp` is
**unconfirmed**; no timing workaround or workload change was made.

On unchanged optimized 21/300-page ordinary-flow workloads, four alternating
before/after pairs (28 samples per host/workload after two warmups per run) show
about 6% more composition time. A control rebuild replaces only
`layout_paged.cpp` with source from `ead8d2a`; every other current archive object,
benchmark client and link input is held constant. The archive symbol index
necessarily changes with its replaced object. Exact published geometry/glyph
FNV digests agree in every sample.

Median ms/page: 21 pages 0.901476 before / 0.955524 after (1.05995 ratio);
300 pages 0.781150 / 0.827102 (1.05883). Parsing, export, hashing and edition
retirement are excluded. A separate archived-host comparison also observed
about 5%; the held-object control is the stronger attribution evidence.

Reproduce from Radiant: run each of
`temp/paged-reservations-causal-control/{before,after}` with
`temp/paged-reservations-final/composition-{21,300}.html` and sample count `7`,
alternating order. `temp/paged-reservations-causal-control/` holds the original
source, compiler/link arguments, object/archive checks, executables, raw samples,
`receipt.json` and `report.json`. Current correctness is separately qualified by
82 focused cases, 1,010 native passes, 4,481 layout passes and sixteen exact
export controls. This blocks a current performance no-regression claim, not the
verified reservation/row behavior. The lost historical P7 archive waiver does
not waive this new measured difference.

**Partial repair, 2026-11-22.** Release profiles found that ordinary lines
queried fragment ancestry even when they had no width override. The line
start now checks for the override first. Consecutive width queries reuse the
last matching record's array index, and an open inline run keeps the
admission result for the rest of that run (**D5.5.1 / D9.1.3v2**).

Four alternating release batches of seven samples per host put the current
repair at 0.741 and 0.592 ms/page for 21 and 300 pages, down from 0.756 and
0.618 ms/page before this repair. Both workloads retain their exact page and
glyph digests. The reconstructed pre-reservation control is at 0.726 and
0.618 ms/page in that batch, with greater variance on the 300-page workload.
The 21-page cost still exceeds the old control by about 2%. The older source
needed mechanical field-access updates for today's headers. An adaptive-track
entry point absent from the old source aborts if called by either control.
All 126 owner fragmentation tests pass. See
`temp/radiant-ordinary-fixes/composition-admission-summary.json` and
`final-paged.log` in the same directory.

<a id="ri-048"></a>

- [RI-048 — Grid-item percentage sizes use the whole grid instead of their area](<Radiant_Issue_Ledger (fixed).md#ri-048>).

<a id="ri-049"></a>

- [RI-049 — Root font-size evaluation leaves descendants on the initial rem basis](<Radiant_Issue_Ledger (fixed).md#ri-049>).

<a id="ri-047"></a>

- [RI-047 — Raw basic-shape parsing drops computed clip geometry](<Radiant_Issue_Ledger (fixed).md#ri-047>).

<a id="ri-046"></a>

- [RI-046 — Pending style text reparses discard later CSSOM edits](<Radiant_Issue_Ledger (fixed).md#ri-046>).

<a id="ri-045"></a>

- [RI-045 — Headless document hosts omit the browsing profile](<Radiant_Issue_Ledger (fixed).md#ri-045>).

<a id="ri-043"></a>

- [RI-043 — Unsubstituted variables incorrectly trigger cyclic intrinsic sizing](<Radiant_Issue_Ledger (fixed).md#ri-043>).

<a id="ri-044"></a>

- [RI-044 — Intrinsic images restore HTML width hints overridden by CSS](<Radiant_Issue_Ledger (fixed).md#ri-044>).

<a id="ri-038"></a>

- [RI-038 — Intrinsic ratio heights ignore inline min/max constraints](<Radiant_Issue_Ledger (fixed).md#ri-038>).

<a id="ri-037"></a>

- [RI-037 — Authored display bypasses scripting-enabled noscript suppression](<Radiant_Issue_Ledger (fixed).md#ri-037>).

<a id="ri-036"></a>

- [RI-036 — Nested column flex intrinsic heights omit main-axis gaps](<Radiant_Issue_Ledger (fixed).md#ri-036>).

<a id="ri-039"></a>

- [RI-039 — Intrinsic height recursion borrows an outer percentage-height basis](<Radiant_Issue_Ledger (fixed).md#ri-039>).

<a id="ri-040"></a>

- [RI-040 — Maximum constraints erase definite flex main sizes](<Radiant_Issue_Ledger (fixed).md#ri-040>).

<a id="ri-042"></a>

- [RI-042 — CSS math gaps are dropped before layout](<Radiant_Issue_Ledger (fixed).md#ri-042>).

<a id="ri-041"></a>

### RI-041 — URL-based CSS image masks have no paint consumer

**Status:** PARTIAL, 2026-11-26. Ownership **D9.1.1v4 / D9.1.3v2**.
Effects contract **D5.5.1 / CI-18**.

**2026-11-26.** A single URL image mask now uses the stylesheet’s resource
base, the existing image cache and computed tile geometry. The renderer
flattens the subtree, then samples mask alpha or luminance in the owner’s
coordinate space. An unavailable URL has zero coverage. The paint record
uses a frame-local image handle, and the existing retained-list check
rejects live mask payloads. Input shorthand and longhand admission is
recorded in LI-126 (**D5.5.1 / D9.1.1v4 / D9.1.2v2 / D9.1.3v2**).

The callback fixture covers standalone SVG, WebP, repeat, rotation,
stylesheet-relative resolution, shorthand priority, a failed request, and
a live reset. The radial mask UI fixture passes all 30 assertions. Two
ordinary Craft pairs restore the torn edges at 1.53% mean error. The same
paint consumer reveals the Acme AE Onboarding panel and photograph in Dock’s
saved authored DOM. Dock stays high pending a qualifying live frame.
This iteration covers one URL or radial layer (**D10.1.7 / CI-18**).
[Regression](../test/online/render_css_image_masks.test.cjs) ·
[CSS Masking](https://www.w3.org/TR/css-masking-1/#the-mask-image) ·
[Round 218](../test/online/comparison/repairs.html#round-218).

**2026-11-26, layered image follow-up.** Standalone image and CSS gradient
layers now use the shared tile and gradient geometry. Each layer computes
alpha or luminance, and the compositor applies `add`, `subtract`,
`intersect` or `exclude` from the bottom layer up. A `none` slot supplies
zero coverage in a list. The layer properties retain their computed
periods for inheritance (CI-18, **D5.5.1 / D9.1.3v2**).

Both ordinary Draftbit pairs show the gradient confined to its border.
Mean error falls from 14.36% to 0.75%. Local checks cover the four operators,
mixed SVG and WebP resources, generated gradients, inheritance, and the
borrowed display-list and transformed render paths. Chromium omits a leading
`none` in the reduced intersection and drops an unused inherited mask
position. Radiant follows the CSS Masking used-value
rules. Inline and paged clips, and URLs selecting an SVG mask element,
remain open (**D10.1.7 / CI-18**).
[CSS Masking](https://www.w3.org/TR/css-masking-1/#layering) ·
[Round 219](../test/online/comparison/repairs.html#round-219).

**Original report, 2026-11-12.** `MaskProp` retained only a radial gradient.
Craft's authored CSS used `mask-image:url(/images/decorations/paperSheetMask.svg)`
and variants for the torn edges. Radiant painted solid paper geometry where
Chromium used the masks. The inline wordmark outline also used a URL mask.
RI-040 had restored the heading and product placement, isolating the
decoration difference from the repaired flex sizing. Evidence includes
temp/online-visual-round5/craft-static.html, round-six live capture
temp/online-visual-round6/craft-live.png, and the paired browser reference.

**Additional evidence, 2026-11-21:** Dock's `.hero-scene-foreground` repeats
the full workspace photograph above its product panel (`z-index:1` on the
parent layer). An authored URL WebP mask cuts out the center in Chromium.
The missing image-mask consumer instead paints the photograph across the
product panel. After LI-091 repairs the panel dimensions, hiding only this
foreground in a diagnostic copy reveals the real Acme AE Onboarding panel;
no panel markup or dimensions are replaced. This intervention is evidence,
not a product fix or canonical capture. Saved DOM, original CSS, the qualified
renderer image and the diagnostic image are under
`temp/online-visual-round128/dock-static.html`,
`temp/online-visual-round127/hero.css`,
`temp/online-visual-round131/dock-static-fixed/` and
`temp/online-visual-round132/dock-without-foreground/`. URL image masking
remains outside this existing-bug milestone (D9.1.1v4 / D5.5.1).

**Additional evidence, 2026-11-16:** Draftbit's border overlay uses two linear
mask layers, `content-box` and `mask-composite:exclude` (prefixed `xor`). The
radial-only consumer leaves its full gradient rectangle painted over the hero.
After RI-094/RI-095, navigation, heading and phone preview render, while this
large tint remains. Saved unchanged source and before/after PNGs are in Lambda
`temp/online-visual-round39/`; no mask layer was hidden or replaced. Linear,
multilayer and composite mask implementation remains outside this bug milestone.


<a id="ri-035"></a>

### RI-035 — Unavailable-voice native provider test stays pending on this macOS session

**Status:** OPEN, cause unconfirmed, 2026-11-12. **Lambda blocker:** no;
this limits the native aggregate qualification, not the online page captures.
Ownership **D9.1.1v4 / D9.1.3v2**. Running
`./build/test/test_radiant_media_backend_gtest --gtest_filter=RadiantNativeMedia.SpeechProviderReportsAnUnavailableVoice`
in the owner checkout returns a non-null speech job for
`invalid.lambda.test.voice`, but `rdt_speech_job_state` remains pending (0),
not failed (2), and the error is null. It fails in the native gate and three
isolated repeats. No speech implementation or test was changed by the online
rendering repairs. The cause and any dependency on the macOS provider/service
state remain unisolated; no assertion or skip was added. Evidence: Lambda
`temp/online-visual-round4/{native-gate-final.log,speech-1.log,speech-2.log,speech-3.log}`.

The current macOS replay and its platform lookup control are recorded at
[RI-016](#ri-016). Both reports describe the same native test.

<a id="ri-034"></a>

- [RI-034 — Positioned intrinsic sizing leaks its query mode to following blocks](<Radiant_Issue_Ledger (fixed).md#ri-034>).

<a id="ri-033"></a>

- [RI-033 — Propagated body backgrounds cover negative stacking layers](<Radiant_Issue_Ledger (fixed).md#ri-033>).

<a id="ri-032"></a>

- [RI-032 — Flex SVG automatic cross sizes ignore the flexed main size](<Radiant_Issue_Ledger (fixed).md#ri-032>).

<a id="ri-031"></a>

### RI-031 — Dock captures after redundant work is removed; layout cost remains

**2026-11-26.** The current release still reaches the 150-second native
deadline. RI-258 fixes a redundant full container-query probe. Stable broad
rebuilds in the profiling run take about 8.5–8.8 seconds, down from
14.5–15.5 seconds, while the saved geometry and live stylesheet controls
retain correct query results. Subsequent startup turns still repeatedly
reach broad relational fallback. In one sequence each of five CSS custom
property writes on the same editor container requests a full rebuild.
The same 1000 ms capture window applies. Dock stays high
(**D5.5.1 / D9.1.3v2 / D10.1.7**). See
`temp/online-visual-round244/dock-candidate-profile/`.

**Latest qualification129 — PARTIAL:** LI-089 evaluates the authored relative
Oklch background; LI-090 reuses the existing all-only cascade cache for absent
properties. Dock saves a PNG in140.11s at the unchanged150s/1000ms policy.
Its blue hero, main heading and copy are restored. Against the preserved
reference, mean error improves44.87%→12.12%, changed area70.07%→26.53%.
Product screen/side photographs and header differences retain a manual high
review. All66 native rendering and47 style-node controls pass; the milestone
remains5609 passed,294 partial,one existing RI-171 failure. An intervening
color-only timeout is excluded. No budget change. Report119 and Lambda
`temp/online-visual-round129/` preserve the qualified pair and source pin.


**Latest qualification 2026-11-21 — PARTIAL:** LI-087 / JS13-L32 remove
repeated stable-inert menu layouts; LI-088 stops cascade sources after the
highest eligible candidate. The combined release now saves Dock's PNG in
144.78s within the unchanged150-second/1000ms policy. All46 style-node and38
native rendering checks pass; the17-category milestone remains5609 passed,
294 partial,one existing RI-171 failure. The current matched browser/native
comparison remains high at44.87% mean error /70.07% changed area. Large nested
grid/flex measurement cost remains; no general performance-completeness claim.
Background/text/media/header differences need separate diagnosis. Evidence:
Lambda `temp/online-visual-round127/`, report118.

**2026-11-21 update:** LI-087 / JS13-L32 remove repeated stable-inert menu
layouts. All38 focused controls and integer lint pass. The release now reaches
the completed-load marker at about145 seconds, but the unchanged150-second
capture deadline still kills it before a PNG. A cache-enabled trace confirms
the remaining layouts come from distinct stylesheet/content/geometry writes.
A two-second CPU sample identifies nested grid/flex intrinsic measurement and
CSS cascade candidate selection as significant costs; these samples alone do
not establish a safe cache extension. No canonical pair is replaced. Evidence:
Lambda `temp/online-visual-round126/`. RI-031 remains OPEN.


Latest release qualification (2026-11-14): a saved script-free control completes
in 11.58s wall / 9.83s CPU. The scripted page instead reaches 150.04s with 149.13s
CPU and 23 completed full layout passes. A short CPU sample places most sampled
work in post-script-turn full rebuilds; repeated SVG ancestor declaration reads
are one cost contributor. RI-077 repairs that reuse path with identical static
pixels, but the final matched live profile still times out at150.04s /149.91CPU,
with25 completed layouts. The repeated full-rebuild triggers remain incompletely
isolated. This confirms a native workload/cascade cost, not a transport wait;
no deadline, admission or virtual frame is changed. These profiles are diagnostic
and do not replace the canonical failed capture. Evidence: Lambda
`temp/online-visual-round29/`; the release build and source pins are recorded
there. RI-031 remains OPEN; other online work can continue.


**Status:** OPEN, root cause unconfirmed, 2026-11-12. **Lambda blocker:** yes
for a new canonical Dock snapshot; other online cases continue. Native layout
ownership **D9.1.1v4 / D9.1.3v2**. Two fresh repaired-host attempts at
`https://www.dock.us/` were killed by the existing 150s parent deadline during
deep nested grid/flex content layout. A control using the prior qualified
renderer (`8f3326ea42be5041d44bc776888dad4b4c1f2a7a008eb42611aa2666a88044a2`)
also times out at 150s. The failure therefore also exists without these changes; its cost change is unmeasured;
current document complexity, layout repetition and origin-dependent input
still need isolation. This is a capture correctness observation, not a release
performance benchmark. No deadline was raised or prior PNG accepted as fresh.
The round-fourteen current-main host with RI-058/RI-059 also times out at
the unchanged150s deadline (2026-11-13T07:00:39.276Z); see Lambda
`temp/online-visual-round14/dock-result.json`. No timing or layout-cost repair
is inferred from the other pages' visual improvements.
The round-thirteen root-font repair retry also times out at the unchanged
150s deadline (2026-11-12T13:34:03.960Z); see Lambda
`temp/online-visual-round13/dock-result.json`. The round-twelve result is
retained in its corresponding evidence directory.
The round-eleven DOM interface/initial-observer retry also times out at the
unchanged 150s deadline (2026-11-12T12:11:34.567Z); see Lambda
`temp/online-visual-round11/dock-result.json`. No partial PNG is promoted.
The round-ten computed-clip repair retry also times out at the unchanged
150s deadline; see Lambda temp/online-visual-round10/dock-result.json.
The round-nine CSSOM synchronization repair retry also times out at the
unchanged150s deadline; see Lambda temp/online-visual-round9/capture.log.
The round-eight session/cookie repair retry also times out at the unchanged
150s deadline; see Lambda temp/online-visual-round8/capture.log.
The round-seven intrinsic-sizing repair retry also times out at150s; see
Lambda temp/online-visual-round7/capture.log.
The round-six three-fix retry also times out at the unchanged 150s deadline;
see Lambda temp/online-visual-round6/capture-threefix.log.
The round-five five-fix retry also times out at the same 150s deadline; see
Lambda `temp/online-visual-round5/capture-fivefix.log`.
The round-four repaired-host retry also times out at the same 150s deadline;
see Lambda `temp/online-visual-round4/capture.log`.
Evidence: Lambda `temp/online-visual-round3/{capture-all.log,capture-grid.log,
dock-control/result.json,dock-control/dock-control-lambda.log}`, plus canonical
`test/online/snapshots/saas_landing_310-lambda.log` and manifest failure status.

<a id="ri-030"></a>

- [RI-030 — Grid row growth leaves stale scroll clips that hide item contents](<Radiant_Issue_Ledger (fixed).md#ri-030>).

<a id="ri-029"></a>

- [RI-029 — Modal dialogs and backdrops paint in the document top layer](<Radiant_Issue_Ledger (fixed).md#ri-029>).

<a id="ri-028"></a>

- [RI-028 — Auto-height flex columns shrink natural content to their minimum](<Radiant_Issue_Ledger (fixed).md#ri-028>).

<a id="ri-027"></a>

- [RI-027 — Mobile viewport metadata shrinks desktop layout independently of its host](<Radiant_Issue_Ledger (fixed).md#ri-027>).

<a id="ri-026"></a>

- [RI-026 — Background color resets paint opaque black](<Radiant_Issue_Ledger (fixed).md#ri-026>).

<a id="ri-025"></a>

- [RI-025 — Headless exports never queue ordinary network image dependencies](<Radiant_Issue_Ledger (fixed).md#ri-025>).

<a id="ri-024"></a>

- [RI-024 — Multi-keyword link relations omit entire stylesheets](<Radiant_Issue_Ledger (fixed).md#ri-024>).

<a id="ri-023"></a>

### RI-023 — Online snapshots omit major image/video regions

**Status:** OPEN, 2026-11-12. **Lambda task blocker:** no; further asset and
snapshot-lifecycle diagnosis is needed. Native ownership:
**D9.1.1v4 / D9.1.3v2**; finite frame selection follows **D10.1.7**.
The original observations did not establish one shared defect. RI-025 now
repairs ordinary image/poster request discovery and poster intrinsic sizing;
RI-026 repairs the black reset-color region. Final Curl, NOAA and OCaml captures
restore their missing media. Python's active carousel has authored opacity0 at
the selected frame; its timing follows D10.1.7. Other remaining lazy-media,
video-frame, CSS and application-state differences still require individual
evidence and are not all assigned to one loader bug.

The major online visual review finds missing hero regions on Chrome docs
(`chrome_developers`), CMake (`cmake`), Python (`python`), NOAA (`noaa`) and
multiple marketing pages. After RI-020/RI-021 the Chrome text/theme is repaired,
but its authored `img` for `images/docs/docs-hero.webp` is still absent. Other
cases may use CSS backgrounds, lazy content, animations or video. Do not force
asset sizes, replace images or remove differences from scoring. Reproduce with
the catalog URLs and 1200×800 render options.

Evidence in Lambda: `temp/online-visual/review-{0..28}.png` covers all 226
pre-fix review candidates; the final comparison report retains every catalog
row. `probes/chrome_developers.png` and `pristine-probes/` distinguish the fixed
layout/theme issues from remaining media. Per-page capture logs are under
`test/online/snapshots/`. Other remaining classes include hidden application
content (Lambda JS13-L1), differing consent state and origin/application errors.
Each needs a reduced reproducer before a source repair.

Second-review residual: CMake's video geometry now matches the browser at
1232×528 after RI-025, but its selected snapshot still has a blank media region.
The poster URL returns HTTP200 (144,119 bytes) in a separate direct request;
that alone does not establish that the native manager delivered it before the
selected frame. Native readiness logs retain one pending resource. The remaining
resource/paint/playback cause is unconfirmed; the page is retained for visual
review even though its coarse error falls below the automated threshold.

<a id="ri-022"></a>

- [RI-022 — Downloaded dynamic stylesheets lose their link media condition](<Radiant_Issue_Ledger (fixed).md#ri-022>).

<a id="ri-021"></a>

- [RI-021 — Grid placement declarations suppress actual flex items](<Radiant_Issue_Ledger (fixed).md#ri-021>).

<a id="ri-020"></a>

- [RI-020 — An initially disabled linked theme participates in the cascade](<Radiant_Issue_Ledger (fixed).md#ri-020>).

<a id="ri-019"></a>

- [RI-019 — Reflow aborts when the focused target becomes non-focusable](<Radiant_Issue_Ledger (fixed).md#ri-019>).

<a id="ri-018"></a>

- [RI-018 — Online exports exhaust memory before viewport PNG encoding](<Radiant_Issue_Ledger (fixed).md#ri-018>).

<a id="ri-017"></a>

- [RI-017 — One-shot exports drain recurring animation before capture](<Radiant_Issue_Ledger (fixed).md#ri-017>).

<a id="ri-016"></a>

### RI-016 — Native macOS speech rejects an invalid voice too late

**Status:** OPEN, 2026-11-11. **Lambda task blocker:** no; speech is outside
the Kian Hup rendering path. Native ownership: **D9.1.1v4 / D9.1.3v2**.
The root cause is **unconfirmed**.

`make test-native INPUT_ROOT=/Users/henry/Projects/lambda-input -j6` completes
902 passing tests but fails
`RadiantNativeMedia.SpeechProviderReportsAnUnavailableVoice`. A focused repeat
also fails: a request for `invalid.lambda.test.voice` returns a job in state 0
with a null error instead of immediate `RDT_SPEECH_FAILED` and a `no voice`
error. No audio playback is requested by this check.

Both `radiant/rdt_speech_avf.mm` and
`test/test_radiant_media_backend_gtest.cpp` are unchanged from HEAD. The
provider uses `AVSpeechSynthesisVoice voiceWithIdentifier:` and rejects the
request immediately only if that lookup returns nil. Whether this platform
returns a fallback voice or another provider state prevents rejection has
not been isolated. No source or test changes were made for this separate
failure. Evidence in the sibling Lambda checkout:
`temp/kianhup-style/baseline-owner-native.log` and `test-owner-speech.log`.

**2026-11-22 recheck.** The freshly built native media executable passed
both tests on 20 repeats. Direct AVFoundation lookup returned nil for the
invalid identifier and returned the expected identifier for a listed voice.
The earlier platform failure remains unexplained. See
`temp/radiant-ledger-continuation/{media-repeats.log,voice-probe-sandbox.txt}`.

<a id="ri-012"></a>

### RI-012 — Forced window capture repaint omits login fragment foreground

**Status:** OPEN, 2026-11-11. **Task blocker:** no; Lambda's blank-login
startup failure is fixed, and the normally presented window surface contains
the complete login form. This report concerns native capture/repaint behavior
owned by Radiant under D9.1.1v4 / D9.1.3v2. The cause is **unconfirmed**.

With the current Lambda debug host and its pending `navigator.cookieEnabled`
fix, `lambda.exe view https://kianhup.com/login` initializes the public Acro
template and Angular controller successfully. DOM assertions confirm visible
username/password fields and Login text. A `capture_surface` event with the
default `render:true` produces only the background and panel, omitting the
logo, heading, labels, inputs and button. A capture with `render:false` after
the same load reads the complete presented surface. Waiting 1500 ms before
the forced capture does not change its result. A local static/late-revealed
`fragment` control did not reproduce the omission.

Reproduce from the sibling Lambda checkout after building its pending fix:

```sh
./lambda.exe view https://kianhup.com/login --event-file temp/kianhup-blank/login-window-delayed-events.json
./lambda.exe view https://kianhup.com/login --event-file temp/kianhup-blank/login-window-readback-events.json
```

Evidence under `../../lambda/temp/kianhup-blank/`: `login-window.png` (forced
repaint, foreground absent), `login-window-readback.png` (presented surface,
foreground present), corresponding event JSON and stdout files. The delayed
stdout records both fragments as `display:inline`, `visibility:visible`,
`opacity:1` with nonzero bounds. `login.png` and `login-result.json` record the
passing headless replay. Direct OS window capture was unavailable; the
comparison uses Radiant's own surface readback.

Relevant entry points: `radiant/event_sim_render.cpp` `read_surface()` calls
`event_sim_render_surface()` by default, whereas `render:false` calls
`ui_context_materialize_surface()`; `radiant/event_sim.cpp`
`event_sim_render_surface()` suspends the GPU compositor around
`render_html_doc()`. Whether compositor state, retained paint, or another
native invalidation path causes the omission remains unconfirmed. No native
source workaround or repair was attempted for this capture issue. Recheck
after the RI-013/RI-014/RI-015 login repairs: both presented-surface and forced
repaint captures now retain all foreground at 793x620. The exact cause of the
earlier omission was not isolated, so this observation does not attribute it
to a particular fix. Evidence: `../../lambda/temp/kianhup-style/native-final.png`,
`native-forced-final.png`, and corresponding stdout/event files. Lambda verification:
`make test-radiant-baseline` passes, with 8,160 passed, 299 partial and 20
skipped of 8,479 tests; this observation is outside that gate's scored cases.


<a id="ri-010"></a>

RI-010 is [resolved in the fixed issue ledger](<Radiant_Issue_Ledger (fixed).md#ri-010>):
all eight remaining layout comparisons pass after the approved reference repair.
The normal 5,936-case integration gate has zero failures on macOS.

<a id="ri-011"></a>

- [RI-011 — Cloned multicol text trim has a pre-existing capacity mismatch](<Radiant_Issue_Ledger (fixed).md#ri-011>).

Continued owner-local repairs (2026-11-11): RAD02-3 now clips positioned HTML
paint, vector output and native hits; RAD03-3 / RAD04-3 share replaced-size
defaults; RAD16-5 no longer has an unreachable frame-clock tail. See the
[implementation and qualification](impl/Radiant_Impl_Ledger_Clip_Defaults.md).
The original reports are retained in the fixed ledger with scoped limits.
Final gate: **893 native tests plus static smoke, all 4,481 geometry comparisons,
7 runner contracts and native archive closure pass**. Existing hardware/static
skips remain; layout lint checks 89 casts with zero unmarked and its five
contracts pass. See the [pinned receipt](../radiant/temp/ledger-next-verification-summary.json).

Latest bounded implementation (2026-11-11): horizontal flex baseline groups,
video posters, a pass-local whitespace cache, and alignment/batch module
extractions are implemented under D9.1.1v4 / D9.1.3v2. RAD22-6 is archived;
broader baseline, whitespace and monolith entries retain explicit PARTIAL
scope. See [the implementation record](impl/Radiant_Impl_Ledger_Repairs.md) and the
final qualification receipt in the fixed ledger.

Final gate: **891 native tests plus static smoke, 7 runner contracts and all
4,481 geometry comparisons pass**, with the existing hardware/static skips.
Layout-cast lint and its five contracts pass. See the
[pinned verification receipt](../radiant/temp/ledger-impl-verification-summary.json).

Continued review on 2026-11-11 repairs RAD04-7, RAD07-1 and RAD15-5 and removes
the redundant float prescan estimates reported as RAD03-2 / RAD04-2. Six more
historical reports are already resolved in current source and have scoped
re-verification records in the fixed ledger. RAD03-1 / RAD04-1 / RAD08-5 remain
PARTIAL: native grid first/last baseline groups are repaired, while broader
baseline behavior remains open. The completed verification receipt is linked
in the continued owner qualification entry in the fixed ledger.

Reviewed 2026-11-11 under D9.1.1v4 / D9.1.3v2: RI-001, RI-002, RI-007, RI-009
and RAD22-7 have owner-local repairs and verification. Eleven imported
reports were already resolved in current source; their original observations
and the precise scope of re-verification are retained in the fixed ledger.
The full native and static/dynamic layout aggregate passes; layout-cast lint
and its five contract checks also pass.
The remaining imported feature/refactoring reports are not claimed fixed.

<a id="ri-004"></a>

### RI-004 — Lambda pointer-event adapter expects an absent native event protocol

**Status:** RESOLVED — the merged native pointer protocol clears the original
release adapter compile failure (D9.1.1v4, D9.1.3v2). See the
[cause, regression coverage and verification limits](<Radiant_Issue_Ledger (fixed).md#ri-004-pointer-event-protocol>).
The descriptive anchor distinguishes this report from the
[older resolved layout issue](<Radiant_Issue_Ledger (fixed).md#ri-004>) that was
independently assigned RI-004; both original IDs are preserved.

The native static-layout qualification and unified-archive provider entries
were independently recorded as `RI-003`. They are now `RI-005` and `RI-006`,
respectively; `RI-003` remains the fixed startup-fragment navigation issue.

## Imported native Radiant backlog

These entries retain their original `RAD` IDs, evidence and historical wording
from the Lambda-hosted ledger. Ownership was reviewed against the split on
2026-11-09 (D9.1.1v4, D9.1.3v2). The 2026-11-11 resolved-entry links below
identify the reports rechecked in current source. Remaining claims and old line
numbers still need re-verification; duplicate or related open entries retain
their original IDs.

<a id="rad13-l1"></a>

### RAD13-L1 — Scene3D gaming profiles remain incomplete

**Updated:** 2026-10-28. The [gaming design](radiant/Radiant_Design_Scene3D.md)
stages native gameplay support while preserving pure authored values and
document-owned live services (**S12.1.1v3 / S12.1.5v2, D9.4.4–D9.4.5**).
Source relocation and the shared scene coordinator are implemented. The first
collision extensions add discrete capsule/convex contacts, mass/inertia,
point/ray tests and named-pair closest-point queries through the shared C
provider. The convex delivery also preserves owned live worlds on compatible
geometry rejection; full M2 remains partial.

Outstanding native work is the design's remaining collision/CCD profiles and
M3–M12: articulated/character movement, navigation, animation graphs, emitters,
game audio, genuine 2D, depth effects, baked/dynamic indirect lighting, fog and
soft bodies. The four Lambda Script gameplay-algorithm scopes remain KIV.
Acceptance and receipts live in the
[implementation record](impl/Radiant_Impl_Scene3D_Gaming.md); no future
profile is advertised as implemented. Its M1 core baseline is green; Radiant
qualification retains six text/inline/sizing layout failures, the existing
header cap failure and the module-boundary import debt described there.
The M2 aggregate also records an unreproduced render-adapter idle timeout;
direct and shared-harness component reruns pass with unchanged checks. The
failed aggregate is retained in the implementation receipts.
Convex qualification exposed the live async sanitizer limit
[LR12-43](<Lambda_Issue_Ledger (fixed).md#lr12-43>), fixed 2026-10-29 under
**D6.1.1 / D6.3.3 / D6.3.6** by reusable-stack lifetime and ASan fiber
bookkeeping. The original runtime and mesh event fixtures now pass **22/22**
and **17/17** assertions with forced/poisoned GC and stack-use-after-return
checking. The standing layout/header/boundary debt above is unchanged.

### RAD01 — View & DOM Model

<a id="rad01-3"></a>

- [RAD01-3](<Radiant_Issue_Ledger (fixed).md#rad01-3>).

<a id="rad01-5"></a>

- [RAD01-5](<Radiant_Issue_Ledger (fixed).md#rad01-5>).

### RAD02 — CSS Style Resolution & Computed Style

<a id="rad02-1"></a>

#### RAD02-1

**Status (2026-11-11): PARTIAL — first bounded extraction delivered.**
`css_alignment.cpp` / `css_alignment.hpp` own container/self alignment resolution
and the stores shared with the dispatcher. Native alignment behavior is covered
by existing and added geometry controls; the remaining property groups still
need decomposition. See [implementation record](impl/Radiant_Impl_Ledger_Repairs.md).
Authority: **D9.1.1v4 / D9.1.3v2**.

**The 726k monolith.** `resolve_css_style.cpp` (~13.9k lines) is the largest file in the repo, and the ~248-case `resolve_css_property` switch (`resolve_css_style.cpp:4632`) is navigable only through comment GROUP banners. *Improvement:* split by property group into separate translation units keyed off a shared dispatch table.

<a id="rad02-2"></a>

- [RAD02-2 — Generated content keeps counters, images and text in order](<Radiant_Issue_Ledger (fixed).md#rad02-2>).

<a id="rad02-3"></a>

- [RAD02-3](<Radiant_Issue_Ledger (fixed).md#rad02-3>).

<a id="rad02-4"></a>

- [RAD02-4](<Radiant_Issue_Ledger (fixed).md#rad02-4>).

<a id="rad02-6"></a>

- [RAD02-6](<Radiant_Issue_Ledger (fixed).md#rad02-6>).

<a id="rad02-7"></a>

#### RAD02-7

**Silent `NAN` on var()/calc overflow.** The depth-64 guard in `resolve_length_value` (`resolve_css_style.cpp:2457`) returns `NAN` and logs a warning, but deep-but-legal chains simply drop out with no user-visible diagnostic; downstream layout must tolerate `NAN`.

<a id="rad02-8"></a>

#### RAD02-8

**Presence-flag fragility.** The many per-struct `has_*` flags (e.g. `InlineProp::has_color`, `has_svg_*`) must be set by hand in each relevant case; adding a new property case and forgetting its flag silently mis-reports "not set". *Improvement:* generate the flag/set pair from a property table.

### RAD03 — Layout Driver, Block Layout & Block Formatting Context

<a id="rad03-1"></a>

#### RAD03-1

**Status (2026-11-11): PARTIAL — horizontal core delivered.** Grid first/last
sharing groups and horizontal flex container/self alignment now preserve the
compound value and select the correct line baseline. Flex keeps both groups
independent, refreshes natural cross sizes after content layout and redistributes
wrapped-line space before final alignment. Controls cover auto/fixed heights,
mixed groups, row reversal, retained changes, wrapped stretch, auto margins
and explicit start overrides. Local Chrome confirms the wrapped geometry.

Orthogonal baselines, spanning grid items and exhaustive nested-container
baseline export remain unqualified. The original failing flex controls and
passing-after evidence are retained under `temp/ledger-impl-*`; see the
[bounded implementation record](impl/Radiant_Impl_Ledger_Repairs.md). Related:
RAD04-1 and RAD08-5. Authority: **D9.1.1v4 / D9.1.3v2**.

The historical stub description below predates the current implementation.

**Last-baseline alignment is unimplemented.** `compute_element_last_baseline` (`layout_alignment.cpp:242`) is a `// TODO: Implement proper last baseline calculation` returning `-1`. `LayoutOutput::last_baseline` (`layout.hpp`) therefore never carries a real value, so `align-items: last baseline` in flex/grid degrades to first-baseline/edge alignment.

<a id="rad03-2"></a>

- [RAD03-2](<Radiant_Issue_Ledger (fixed).md#rad03-2>).

<a id="rad03-3"></a>

- [RAD03-3](<Radiant_Issue_Ledger (fixed).md#rad03-3>).

<a id="rad03-4"></a>

- [RAD03-4](<Radiant_Issue_Ledger (fixed).md#rad03-4>).

<a id="rad03-5"></a>

#### RAD03-5

**`layout_block` is ~1600 lines mixing many concerns.** The single function (`layout_block.cpp:7486`) handles float detection, context save/restore, view tagging, style resolution, cache/measurement bailout, counters, list-items, and the deep inline-atomic float/wrap flow-into-parent logic (`layout_block.cpp:7849+`, `8600+`). It is hard to test in isolation and predates the RAII scopes it could use. *Improvement:* extract the flow-into-parent margin/collapse tail and the inline-atomic handling into named helpers.

<a id="rad03-6"></a>

#### RAD03-6

**`LayoutOutput` return-struct abstraction is inconsistently threaded**: block layout writes `block->width`/`height` directly, so the measure-vs-layout separation is not carried by a uniform return type.

### RAD04 — Box Model, Containing Blocks & the Layout Cache

<a id="rad04-4r"></a>

#### RAD04-4R — Native layout-cache invalidation

**Status:** UNVERIFIED — native portion of historical RAD04-4; ownership
reviewed 2026-11-09. The original report describes coarse cache clearing in
Radiant's `layout.cpp` and `view_pool.cpp` and raises a possible stale
descendant measurement if invalidation does not propagate. Recheck those
claims against current native invalidation before deciding on a fix. The
Lambda event-dispatch portion remains in the [mixed historical record](Radiant_Issue_Ledger.md#rad04-4).

<a id="rad04-1"></a>

#### RAD04-1

**Status (2026-11-11): PARTIAL.** See [RAD03-1](#rad03-1) for the fresh native
flex/grid repairs, failing-before/passing-after controls and remaining scope. The
historical stub description below predates the current helper implementation.

**`compute_element_last_baseline` is a stub.** `layout_alignment.cpp:237` has `// TODO: Implement proper last baseline calculation` and returns `-1`, so *last*-baseline alignment (`align-items: last baseline`) silently degrades to no-baseline for flex and grid items. *Improvement:* mirror the recursive first-baseline logic over the last in-flow child.

<a id="rad04-2"></a>

- [RAD04-2](<Radiant_Issue_Ledger (fixed).md#rad04-2>).

<a id="rad04-3"></a>

- [RAD04-3](<Radiant_Issue_Ledger (fixed).md#rad04-3>).

<a id="rad04-5"></a>

- [RAD04-5](<Radiant_Issue_Ledger (fixed).md#rad04-5>).

<a id="rad04-6"></a>

#### RAD04-6

**`LayoutOutput` is under-threaded.** `layout.hpp` defines a clean width/height + baseline return struct, but block layout mostly writes `block->width`/`height` in place rather than returning `LayoutOutput`, so the abstraction is not consistently plumbed and the baseline fields are largely unused by the block path.

<a id="rad04-7"></a>

- [RAD04-7](<Radiant_Issue_Ledger (fixed).md#rad04-7>).

### RAD05 — Intrinsic Sizing

<a id="rad05-1"></a>

- [RAD05-1](<Radiant_Issue_Ledger (fixed).md#rad05-1>).

<a id="rad05-2"></a>

#### RAD05-2

**Height is an admitted estimation, and never cached.** `calculate_max_content_height` does not run a real layout for elements (`intrinsic_sizing.cpp:5264-5265`, "for now, use a simplified estimation"), so nested flex/grid/table height can mismatch committed layout; and because there is no height cache, the expensive width-dependent walk repeats on every call.

<a id="rad05-3"></a>

#### RAD05-3

**The monolith is ~3100 lines with heavy duplication.** `measure_element_intrinsic_widths` (`:1939-5060`) inlines table, grid, flex, block/inline, replaced, and pseudo logic. Box-extra/border/padding resolution is duplicated across the table block (`:3433-3465`), the generic tail (`:4900-4948`), the pseudo path (`:4751-4840`), and the grid early-return (`:3703-3714`); font setup is mirrored again in `calculate_max_content_height` (`:5290`). *Improvement:* extract a shared box-extras helper and a per-mode dispatch table.

<a id="rad05-4"></a>

#### RAD05-4

**Grid intrinsic width is a fixed-track approximation with an early return.** The grid branch (`:3606-3744`) sums only fixed-length tracks and gaps, ignoring percentage/`fr` tracks, and early-returns past the generic constraint tail (duplicating the max-width clamp at `:3719`). Real track sizing is in RAD09; the two can disagree.

<a id="rad05-5"></a>

#### RAD05-5

**Per-call timing and the `SLOW MEASURE` beacon signal a known hotspot.** Every element call runs a `std::chrono` timer and warns above 100ms (`:5053-5057`); text measurement loads a glyph per codepoint. The absence of height memoization (issue 2) makes repeated measurement passes (flex, grid multipass) quadratic-ish on deep subtrees.

<a id="rad05-6"></a>

#### RAD05-6

**Debt is expressed as `CRITICAL FIX` / `fallback` / "for now", not `TODO`.** There are no literal TODO/FIXME/HACK tokens; instead accreted special-cases carry `CRITICAL FIX` comments (button UA styling `:1973`, monospace font-before-children `:1973`), form-control border-box flags, and anonymous-table repair — harder to grep and easy to overlook when refactoring.

<a id="rad05-7"></a>

#### RAD05-7

**The re-entrancy guard returns `{0,0}` silently on a cycle.** Breaking a percentage-height ↔ aspect-ratio ↔ width cycle by returning zero (`:1951`) is safe against infinite recursion but can under-measure a legitimately complex box with no diagnostic beyond a `log_debug`.

### RAD06 — Inline & Text Layout

<a id="rad06-1"></a>

#### RAD06-1

**Bidi and complex-script paint remain separate.** `layout_bidi.cpp` resolves visual order for established line fragments, using optional FriBidi or its local fallback, and publishes advance-based ranges. That geometry does not provide Arabic joining, Indic shaping or full bidi glyph substitution; the existing paint and fallback limitations still apply. This scoped refresh does not qualify the complete UAX #9 surface.

<a id="rad06-2"></a>

#### RAD06-2

**No general complex-script shaping.** Only the ASCII-Latin fast path calls `font_measure_text` (`layout_text.cpp:976`); every other script is measured per-codepoint with no GSUB. Arabic joining, Indic reordering, and ligatures beyond the font's simple advance are not handled. The comment at `layout_text.cpp:884` notes the ZWJ-emoji handling is a heuristic "without HarfBuzz text shaping". *Improvement:* integrate a shaper (HarfBuzz) and persist a shaped buffer (see item 5).

<a id="rad06-3"></a>

#### RAD06-3

**Emoji ZWJ sequences are heuristic.** `is_emoji_for_zwj` (`layout_text.cpp:877`) and `is_zwj_composition_base` use hardcoded codepoint ranges to zero post-ZWJ advances, rather than proper UAX #29 grapheme clustering; non-standard emoji sequences will mis-measure.

<a id="rad06-4"></a>

#### RAD06-4

**`<br>` reported height is an approximation.** `layout_inline.cpp:1180` admits the rule "is not fully understood" and uses the font cell height, whereas Chrome's reported `<br>` height varies by context.

<a id="rad06-5"></a>

#### RAD06-5

**Layout and render re-measure independently from byte ranges.** `TextRect` stores only `start_index`/`length`/`width`, so `render_text.cpp` re-decodes and re-loads every glyph. This duplicates work and risks layout/paint divergence. *Improvement:* a shared shaped-glyph buffer keyed by the rect's byte range.

<a id="rad06-6"></a>

#### RAD06-6

**Status (2026-11-11): PARTIAL — layout hot-path cache delivered.** Text
layout, wrapping and lookahead share a pass-local result by parent, invalidated
by style/mutation epochs, layout/measurement generations and owner/view changes.
Inline overrides, UA defaults and retained style changes have native controls.
Context-free paint/query helpers retain their resolver, and the related
collapsed-whitespace bounds observation remains unqualified. No performance
claim is made. See [implementation and receipts](impl/Radiant_Impl_Ledger_Repairs.md).
Authority: **D5.5 / D9.1.1v4 / D9.1.3v2**.

**`white-space` is re-walked per text node.** `layout_text.cpp:2642` carries `todo: white-space should be put in BlockContext`; `get_white_space_value` climbs the ancestor chain on every text node. Related: `:2700` `todo: probably should still set it bounds` for fully-collapsed whitespace nodes.

<a id="rad06-7"></a>

#### RAD06-7

**Hardcoded fallbacks.** tab-size default `8` (`layout_text.cpp:2983`), small-caps lowercase scale `0.7f` (multiple sites, `:3094`/`:3133`/`:3235`), and C1-control line-height synthesis ratios. These are correct for common cases but not configurable.

<a id="rad06-8"></a>

- [RAD06-8](<Radiant_Issue_Ledger (fixed).md#rad06-8>).

<a id="rad06-9"></a>

#### RAD06-9

**Infinite-`goto` guard.** `layout_text` caps its `goto LAYOUT_TEXT` re-entries at `layout_text_iterations > 500` (`layout_text.cpp:2733`) — an explicit admission that the goto-driven control flow can loop under degenerate layouts (extreme negative margins), aborting text layout rather than hanging.

<a id="rad06-10"></a>

#### RAD06-10

**`layout_text` is a single ~1370-line function.** The overflow/wrap cascade and break-recording tail are long and deeply nested; correctness depends on many interacting `Linebox` flags. There is no unit-level test of individual branches — regressions surface only through the layout suite.

### RAD07 — Fonts

<a id="rad07-1"></a>

- [RAD07-1](<Radiant_Issue_Ledger (fixed).md#rad07-1>).

<a id="rad07-2"></a>

- [RAD07-2](<Radiant_Issue_Ledger (fixed).md#rad07-2>).

<a id="rad07-3"></a>

#### RAD07-3

**Platform coupling leaks into the bridge.** The macOS `"System Font"` reuse quirk (`font.cpp:64-73`, guarded `__APPLE__`) is a bridge-level compensation for how CoreText reports `system-ui`. Any second platform whose backend renames the resolved family would need the same treatment, ad hoc. *Improvement:* have the engine normalize the reported family for generic system families so the bridge's cache-match test can stay platform-agnostic.

<a id="rad07-4"></a>

#### RAD07-4

**Cache-match test relies on exact float equality.** `font_handle_matches_prop` compares `handle_size == fprop->font_size` (`font.cpp:75`) with `==` on floats. This is currently sound because both come from the same resolution path, but it is a fragile invariant — any sub-pixel rounding difference silently forces a re-resolve.

<a id="rad07-5"></a>

#### RAD07-5

**Hard-coded caps.** `@font-face` array initial capacity `10` (`font_face.cpp:399`); `max_cached_faces = 64` at context creation (`ui_context.cpp:161`). These are reasonable defaults but are not configurable from CSS or document scale.

<a id="rad07-6"></a>

#### RAD07-6

**No variable-font or feature-control surface.** Radiant's `FontProp`/`FontStyleDesc` expose only family/size/weight/slant plus `font-kerning`. There is no bridge for `font-variation-settings` (variable-font axes) or `font-feature-settings` (arbitrary OpenType features) — only the specific `halt` adjustment (`font_get_halt_adjustment`) is surfaced. *Improvement:* extend `FontStyleDesc` and the bridge to carry axis/feature maps if variable-font support is desired.

<a id="rad07-7"></a>

#### RAD07-7

**Fallback handling is fragile at the seam.** Radiant delegates per-codepoint fallback entirely to `font_load_glyph`, but special-cases only emoji (`font_load_glyph_emoji`); the fallback-metric blend for line height depends on `LoadedGlyph::font_normal_*` being populated correctly by whichever fallback face the engine picks — a dependency that is implicit and untested at the bridge level.

### RAD08 — Flexbox Layout

<a id="rad08-1"></a>

- [RAD08-1](<Radiant_Issue_Ledger (fixed).md#rad08-1>).

<a id="rad08-2"></a>

#### RAD08-2

**Hard-coded measurement fallbacks.** The default container width `366` in `measure_flex_child_content` (`:661`), the `line_init(..., 10000)` sentinel width (`:641`), the scattered `10000.0f` "unconstrained" widths (`layout_flex_multipass.cpp:2123` and elsewhere), and the **tag-based height estimates** (e.g. h4 = 20px at `:756`, and the "p=36px, h1=32px" heuristics noted at `layout_flex_multipass.cpp:2121`) are magic numbers that can mis-measure content in edge cases. *Improvement:* derive these from font metrics / actual available space.

<a id="rad08-3"></a>

#### RAD08-3

**Correctness caps.** `MAX_ITERATIONS = 10` (`layout_flex.cpp:4194`), `MAX_FLEX_DEPTH = 16` (`layout.hpp`), and `MAX_MEASURE_DEPTH = 32` (`layout_flex_measurement.cpp:436`) are hard caps added for fuzzer-found blowups. They protect against pathological trees but silently truncate genuinely deep or genuinely slow-converging layouts rather than reporting a limit hit.

<a id="rad08-4"></a>

- [RAD08-4](<Radiant_Issue_Ledger (fixed).md#rad08-4>).

<a id="rad08-5"></a>

#### RAD08-5

**Status (2026-11-11): PARTIAL.** See [RAD03-1](#rad03-1) for the fresh native
flex/grid repairs, failing-before/passing-after controls and remaining scope. The
historical stub description below predates the current helper implementation.

**Incomplete last-baseline alignment.** `layout_alignment.cpp:242` carries `// TODO: Implement proper last baseline calculation`; `align-items:last baseline` is not fully spec-correct.

<a id="rad08-6"></a>

- [RAD08-6](<Radiant_Issue_Ledger (fixed).md#rad08-6>).

<a id="rad08-7"></a>

#### RAD08-7

**Heavy per-phase logging in the hot path.** Every phase and many per-item steps emit `log_info`/`log_debug` (e.g. the "Phase N" and "ITERATIVE_FLEX" lines throughout `layout_flex.cpp`). On large trees this is a real log-volume and throughput concern; the logging is not compiled out in release. *Improvement:* gate the per-item lines behind a debug-only channel.

<a id="rad08-8"></a>

#### RAD08-8

**Monolithic functions mixing spec logic with fixups.** `layout_flex_container` (~1400 lines) and `layout_flex_container_with_nested_content` (~375 lines) interleave the §9 algorithm with the 4b/5b/5c/7b/7c/8b/9a/9.5 fixup branches, making the spec logic hard to test in isolation.

### RAD09 — CSS Grid Layout

<a id="rad09-1"></a>

#### RAD09-1

**Broad flow, ordering and logical-axis qualification remains open.** The focused dense/sparse and both-flow capacity controls do not establish every placement combination.

<a id="rad09-3"></a>

#### RAD09-3

**Pass-3 row-height reconciliation remains heuristic.** It repairs underestimated intrinsic heights from laid-out content and applies only to non-fixed tracks.

<a id="rad09-4"></a>

#### RAD09-4

**Placement counts still have a preservation guard.** Post-placement item endpoints cannot reconstruct the entire negative-implicit extent, so recomputation must preserve placement-established counts.

<a id="rad09-5"></a>

#### RAD09-5

**Independent paged grid remains unqualified.** Browsing and PNG/SVG/PDF controls do not establish fragmented-grid behavior.

Focused evidence: `css_grid_auto_fit`, `css_grid_named_lines`, `css_grid_capacity`, `css_grid_extent_policy`, `GridStorage.*`, retained-clone retirement and `RenderOutputParity.Grid*` cases. The active [CSS support plan](impl/Radiant_Impl_CSS_Support.md) records dated qualification and remaining P0–P2 work.

### RAD10 — Table Layout

<a id="rad10-1"></a>

#### RAD10-1

**The 501k single-file mega-function.** `layout_table.cpp` is ~9945 LOC and dominated by `table_auto_layout` (`layout_table.cpp:6142`), a single ~2900-line function whose phases are delimited only by "Step N" comments and share a large flat set of locals. This is the strongest extraction candidate in Radiant: caption handling, structure analysis (already partly factored), border-collapse resolution, auto width distribution, fixed distribution, and cell positioning could each become their own function/file. Cognitive load and merge-conflict surface are both high. *Improvement:* split each Step into a named function taking the `TableMetadata` explicitly.

<a id="rad10-2"></a>

#### RAD10-2

**Debt is structural, not marked.** A grep for `TODO`/`FIXME`/`HACK`/`XXX` across `layout_table.cpp`, `layout_table_metadata.cpp`, and `layout_table_caption.cpp` finds **none** — the tech-debt here is size and coupling, not tagged intent, so it will not surface in a marker sweep. The implementation comments themselves flag "simplified" building (`build_table_tree` at `layout_table.cpp:4463`) and a "Phase 3 optimization" without a corresponding removal plan.

<a id="rad10-3"></a>

#### RAD10-3

**Caption logic is duplicated across placement and inline handling.** Caption sizing/placement appears both in the dedicated `layout_table_caption.cpp` helpers (declared in `layout.hpp`) and inline within `table_auto_layout` (top at `layout_table.cpp:7964`/`8021`, bottom at `9175`), plus the caption-only fast path at `6203`. Keeping these in sync is manual.

<a id="rad10-4"></a>

#### RAD10-4

**Wide parallel-array `TableMetadata`.** The grid carries ~15 parallel arrays (`layout.hpp`) indexed by bare column/row integers; a bug that indexes one array with the wrong axis or an off-by-one against the `cols+1` edge array (`col_edge_max_border`) is silent. *Improvement:* a small accessor layer or a per-column struct-of-arrays would make misuse harder.

### RAD11 — Positioned, Floats, Multi-column, Lists & Counters

<a id="rad11-1"></a>

#### RAD11-1

**Multicol is explicitly simplified.** The header comment at `layout_multicol.cpp:2292` states it uses block-level distribution and does not fully fragment within block elements. Spanner top-margin collapsing is `max(prev_margin_bottom, spanner_margin_top)` and marked "simplified" in-code (`:2716`). *Improvement:* a real fragmentation cursor that can break inside a block, not only project inline lines after the fact.

<a id="rad11-2"></a>

#### RAD11-2

**Broader fixed-position contexts remain partial.** Initial viewport and transformed/contained ancestor selection have bounded sizing coverage, including percentage math and retained changes. General fixed positioning across scrolling, independent views and fragmented contexts is not fully qualified by those controls.

<a id="rad11-3"></a>

#### RAD11-3

**Fragile float coordinate math guarded by `#ifndef NDEBUG`.** `layout_float_element` keeps several debug-only coordinate variables (`layout_positioned.cpp:2162`+, and `cb_x`/`cb_y` in `calculate_absolute_position` at `:704`), indicating the BFC↔local coordinate conversion has been bug-prone. The 100-iteration cap (`:2268`) and the `0.001f` fit epsilon (`:2337`) are defensive constants, not principled bounds.

<a id="rad11-4"></a>

#### RAD11-4

**`layout_abs_block` is a 700-line mega-function with heavy re-correction.** The abspos path in `layout_positioned.cpp:1242`+ re-solves margins, x, and y multiple times (after image load, after shrink-to-fit, after auto height) with many mode guards (flex/grid/form/replaced). Correctness depends on these fix-ups running in exactly the right order; there is no assertion tying the final geometry to the constraint equation.

<a id="rad11-5"></a>

#### RAD11-5

**Image-load failure uses a placeholder box.** Failed abspos `<img>` loads fall back to a hard-coded 40×30 placeholder (`layout_positioned.cpp:1464`); marker image-load failure silently falls through to the numeric/bullet render (`render_list.cpp:70`).

<a id="rad11-6"></a>

#### RAD11-6

**Very few explicit TODO/FIXME markers.** The structural debt in this area shows up as `simplified`/`placeholder`/"Phase N" narration rather than tagged TODOs, so grep-based debt tracking under-counts it (notably the mega-functions and multicol).

### RAD12 — Paint IR, Display List & Backend Abstraction

<a id="rad12-1"></a>

#### RAD12-1

**Eager per-command lowering yields no batching benefit.** The gateway records one `PaintCmd`, lowers it, then clears the `PaintList` on every op (`render.hpp`, and every `paint_record_*` inline). PaintIR is a "thin layer above DisplayList" (`render.hpp`) that today adds a copy-and-switch per op with no cross-op optimisation. The overhead is unmeasured. *Improvement:* record whole subtrees into the `PaintList` and lower in bulk, enabling coalescing/culling before raster.

<a id="rad12-2"></a>

#### RAD12-2

**Triple-mirrored op and payload definitions.** `PaintOp` ↔ `DisplayOp` ↔ the `rdt_*` calls, and `Paint*` ↔ `Dl*` payload structs, mirror each other by hand (`render.hpp` vs `render.hpp`). Adding one primitive means edits in ~3 struct sites plus the lowering switch (`paint_ir.cpp:944`) and the replay switch (`display_list_replay.cpp:85`) — roughly 3 edit sites per primitive before it renders. *Improvement:* generate the mirrored trio from one op table.

<a id="rad12-3"></a>

#### RAD12-3

**~18 fragmented `display_list_*` files.** Recording, replay, storage, bounds, and the per-effect replay families are split across many small files (`display_list_replay_state.cpp` ~60 lines, `..._raster.cpp` ~50, `..._shadow.cpp` ~74, several 5–24-line headers). The split is by effect family, not by a clear layering rule, so navigation cost is high. *Improvement:* consolidate the tiny replay/record units by pipeline stage.

<a id="rad12-4"></a>

#### RAD12-4

**Semantic ops are not fully lowered.** Raster lowering ignores `PAINT_GLYPH_RUN` semantic runs and richer effects (`render.hpp`); SVG handles only text runs and opacity-only groups; `PAINT_SVG_SUBSCENE` is a Phase-F stub (`render.hpp`, `330-331`). Most effect groups round-trip through raster fallback rather than native vector constructs, so the "one paint algorithm for all backends" promise is only partly realised.

<a id="rad12-5"></a>

#### RAD12-5

**Tiling disabled whenever glyphs are present.** `render_output_replay_display_list` forces the serial path if `dl_contains_glyphs` is true (`render_output.cpp:308-309`). Because most real pages have text, the parallel tiled path rarely engages even though tile-local glyph rendering already exists (`tile_pool.cpp:385`). *Improvement:* let glyph ops render tile-locally in the parallel path and drop the page-level gate.

<a id="rad12-6"></a>

#### RAD12-6

**No `TODO/FIXME/HACK/XXX` markers, but the debt is structural.** The core files grep clean of inline debt markers; the migration is incremental-by-design (Phase C/D/F), so the incompleteness above is not flagged in-line and is easy to mistake for finished work.

### RAD13 — Render Walk, Painters & Export

<a id="rad13-1"></a>

#### RAD13-1

**CSS stacking contexts are still partial.** Signed positioned block layers belong to their nearest context, including `isolation:isolate`, with shared paint/hit order. Zero-level descendant lifting, floats, negative inline spans, blend-group isolation and complete flex/grid context rules still need the full CSS stacking-context algorithm.

<a id="rad13-2"></a>

- [RAD13-2](<Radiant_Issue_Ledger (fixed).md#rad13-2>).

<a id="rad13-3"></a>

#### RAD13-3

**Duplicated body-background propagation.** `render_output_canvas_background` (`render_output.cpp:205`) and the iframe branch of `render_embed_doc` (`render.cpp:122-156`) reimplement the same `<html>`-then-`<body>` background walk. *Improvement:* extract one shared helper.

<a id="rad13-4"></a>

#### RAD13-4

**File sprawl (~50 `render_*` files, ~26.8k LOC).** Painter logic, low-level pixel ops, and IO decode are interleaved; the largest are `render_svg_inline.cpp` (5633), `render_pdf.cpp` (2384), `render_background.cpp` (2107), `render_svg.cpp` (1978). No TODO/FIXME/HACK markers exist — the debt is structural (sprawl + duplication), not annotated.

<a id="rad13-5"></a>

#### RAD13-5

**The raster path is only partway onto the shared abstraction.** The header comments admit it: `render.hpp` describes richer raster block state being "migrated in staged slices," and `render.hpp:15-37` labels the PaintIR routing "Phase C." The raster backend deliberately leaves `render_bound`/effect/transform callbacks null and does that work inside `RenderContext` instead of the generic walker, so the two paths are not yet fully unified.

<a id="rad13-6"></a>

#### RAD13-6

**PDF/SVG re-do layout headlessly.** `render_html_to_pdf` (`render_pdf.cpp:2262`) reloads and re-lays-out the HTML rather than reusing an existing view tree, and cannot be driven from the in-memory `render_output_render_view_tree_to_target` (`render_output.cpp:497`). Two parallel entry conventions (`render_html_to_*` vs `render_output_*`) coexist. *Improvement:* allow vector export off a pre-laid view tree.

<a id="rad13-7"></a>

#### RAD13-7

**Effect-group need vs. capability is decided in two places.** The walker's `render_walk_block_effect_group` (`render_walk.cpp:67`) decides an effect group is *needed*; the backend caps decide it is *supported*. A backend that opens a group it cannot honor relies on flatten/fallback correctness elsewhere — there is no single assertion tying the two together.

### RAD14 — SVG, Vector Graphics & Diagram Layout

<a id="rad14-1"></a>

#### RAD14-1

**`render_svg_inline.cpp` is a 5633-line monolith.** It mixes SVG parsing, CSS style resolution, path/arc geometry, gradients/patterns, filters, clips/masks, and text glyph rendering in one file. *Improvement:* split along the natural seams — path parsing, filters/masks, gradients/patterns, and text — into separate TUs sharing `SvgInlineRenderContext`.

<a id="rad14-3"></a>

#### RAD14-3

**Backend caps parity gaps** (`rdt_vector_tvg.cpp:774` vs `rdt_vector_cg.mm:273`). Both leave `opacity_group`, `blend_modes`, `color_matrix_filters`, and `native_text_runs` = false, so those effects silently no-op or fall back to raster. The ThorVG native `gaussian_blur` cap is `__APPLE__`-only (`rdt_vector_tvg.cpp:787-791`) — the CSS-filter blur path degrades on Linux/Windows ThorVG builds (but inline-SVG `<feGaussianBlur>` does not).

<a id="rad14-4"></a>

- [RAD14-4](<Radiant_Issue_Ledger (fixed).md#rad14-4>).

<a id="rad14-5"></a>

#### RAD14-5

**Two directions named "SVG" are easy to confuse.** `render_svg_inline.cpp` is SVG *input* (element tree → pixels); `render_svg.cpp` is SVG *output* (view tree → SVG text). They share only the `PaintSvgSubscene` builder. *Improvement:* rename `render_svg.cpp` to something like `render_svg_output.cpp` to make the direction unmistakable.

<a id="rad14-6"></a>

#### RAD14-6

**Sparse explicit debt markers.** The only "unsupported" log in the inline renderer is the SVG path parser's unknown-command branch (`render_svg_inline.cpp:2263`); `rdt_picture_load` logs unsupported formats rather than falling back. Most debt here is structural (file size, algorithm scope) rather than tagged with TODO/FIXME.

<a id="rad14-7"></a>

#### RAD14-7

**Layer cache has no eviction.** `svg_layer_registry` keeps a captured layer until the document is torn down or the same element changes; an `<svg>` removed from the tree keeps its raster (bounded by the 256 MB per-document cap). A rotated, skewed, flipped or non-uniformly scaled `<svg>` never caches. *Improvement:* evict entries not painted for N frames; draw transformed layers through `DL_DRAW_IMAGE` with a premultiplied upload.

<a id="rad14-8"></a>

#### RAD14-8

**Manual lifetime bookkeeping around `RdtPicture`.** The path-keyed picture cache plus its mutex (`rdt_vector_tvg.cpp:101,286`) pair with hand-managed `Pool`/`Element` ownership on `KIND_SVG_DOM` pictures — a place to watch for leaks or races under concurrent load.

### RAD15 — Events & Input Dispatch

<a id="rad15-4r"></a>

#### RAD15-4R — Native event-simulation complexity

**Status:** UNVERIFIED — native portion of historical RAD15-4; ownership
reviewed 2026-11-09. The historical report calls out the size and complexity
of `radiant/event_sim.cpp`. Recheck the current implementation and whether
a native decomposition is justified. Lambda's `event.cpp` dispatcher remains
Lambda-owned and is retained in the [mixed historical record](Radiant_Issue_Ledger.md#rad15-4).

<a id="rad15-2"></a>

#### RAD15-2

**Zero-level stacking remains partial.** Signed positioned block descendants use their enclosing real stacking context and replay intervening ancestor clips. Absolute/fixed layers translate into the containing-block coordinate frame before hit testing, including their own border box. Zero-level descendant lifting and interleaving absolute/fixed children with in-flow step-8 boxes by tree order remain incomplete (RAD13). Ordinary block background/foreground and float phases share paint and reverse hit order. View-backed DOM `elementFromPoint` first delegates to the native engine target picker, with shadow and geometry fallbacks in `dom_document_element_from_point_native` for cases without an engine hit.

<a id="rad15-5"></a>

- [RAD15-5](<Radiant_Issue_Ledger (fixed).md#rad15-5>).

### RAD16 — Animation & Frame Scheduling

<a id="rad16-1"></a>

#### RAD16-1

**Transition support requires checking the supported property subset.** Rechecked
   2026-10-06: `css_transition_capture_before_change`,
   `css_transition_resolve_config` and `css_transition_resolve` now capture changes,
   read transition longhands and create scheduler instances. The earlier claim
   that transitions were unwired is obsolete. This does not establish support
   for every CSS property or PowerPoint effect; consult the animation value
   parser and the used-value consumer for the requested property.

<a id="rad16-2"></a>

- [RAD16-2](<Radiant_Issue_Ledger (fixed).md#rad16-2>).

<a id="rad16-3"></a>

#### RAD16-3

**`property_name_to_id` is a fixed strcmp table.** Only ~28 properties are animatable (`css_animation.cpp:122`); anything else in a keyframe is silently ignored. New animatable properties require editing three coupled tables (`property_name_to_id`, `property_value_type`, `apply_animated_value`).

<a id="rad16-4"></a>

#### RAD16-4

**Heavy platform `#ifdef` in `frame_clock.cpp`.** Every function branches on `__APPLE__`/`_WIN32`/`__linux__` (`frame_clock.cpp:27`, `226`, `316`, `384`) rather than dispatching through a small vtable; each new clock operation must re-branch in several places. *Improvement:* a `RadiantFrameClockOps` function-pointer table per platform.

<a id="rad16-5"></a>

- [RAD16-5](<Radiant_Issue_Ledger (fixed).md#rad16-5>).

<a id="rad16-6"></a>

#### RAD16-6

**Registry lookup and dedup are linear.** `keyframe_registry_find` (`css_animation.cpp:561`) and the "already running?" scan in `css_animation_resolve` (`css_animation.cpp:1022`) are O(n) over all keyframes / all active animations, run per element during layout. Fine at current scale, a hazard for animation-heavy pages.

<a id="rad16-7"></a>

#### RAD16-7

**Transform-dirty bounds are approximate.** `css_animation_tick` inflates bounds by a generous `max(w,h)` margin for scale/rotate (`css_animation.cpp:859`) rather than computing the true transformed AABB, over-painting to stay correct. Acceptable, but wasteful for large elements.

### RAD17 — Interaction State: DocState, State Machine & Schema

<a id="rad17-1"></a>

#### RAD17-1

**Status (2026-11-11): PARTIAL — batch module extracted.** `state_batch.cpp`
owns begin/end batching and presentation/dirty synchronization. The existing
internal header exposes shared synchronization helpers instead of copying them.
The state suite covers interleaved document batches and native state invariants.
Other state-store families and deferred callbacks remain open. See the
[implementation record](impl/Radiant_Impl_Ledger_Repairs.md). Authority:
**D5.5 / D9.1.1v4 / D9.1.3v2**.

**`state_store.cpp` remains a large monolith** spanning interned names, dump, lifecycle, generic state, constraints, dropdowns, immutable mode, batching, dirty/reflow, caret/selection presentation, focus, and clipboard. It is the prime split candidate in this subsystem — a natural decomposition follows the function-prefix families (`doc_state_*`, `form_*`, `view_state_*`, `selection_*`, `caret_*`, `state_dump_*`).

<a id="rad17-2"></a>

#### RAD17-2

**Immutable / COW mode is an unimplemented stub.** `state_set_immutable` shallow-copies and re-hashes rather than structurally sharing — `// TODO: implement proper HAMT for structural sharing` (`state_store.cpp:5142`). Related dangling TODOs: `state_end_batch` never fires deferred callbacks (`// TODO: trigger deferred callbacks`, `:5284`), and the dump element builder has a `// TODO: add attributes if needed` (`:8338`). Undo/time-travel cannot rely on this mode today.

<a id="rad17-3"></a>

- [RAD17-3](<Radiant_Issue_Ledger (fixed).md#rad17-3>).

<a id="rad17-5"></a>

#### RAD17-5

**Cached `View*`/`DomElement*` pointers require prune-after-reflow.** Numerous `DocState` fields (`hover_target`, `active_target`, `cursor->view`, drag/drop views, editing surfaces) hold raw pointers that go stale on relayout and depend on `state_store_prune_after_reflow` being called at the right time.

### RAD18 — Editing, Selection & DOM Ranges

<a id="rad18-1"></a>

#### RAD18-1

**`dom_range.cpp` is a 4551-line monolith.** It mixes lifecycle, boundary comparison, mutation envelopes, extract/clone/surround, selectAll/triple-click, `Selection.modify`, and stringification. It is the single largest file in the area and the prime split candidate — a natural fission is (a) range/selection core + comparison, (b) mutation envelopes + mutators, (c) modify/selectAll/word-breaking, (d) stringification.

<a id="rad18-2"></a>

#### RAD18-2

**Text-control target range is a StaticRange hack.** `compute_text_control_target_ranges` synthesizes a boundary over the control *element* carrying UTF-16 `selectionStart`/`End`, until form values can be promoted to concrete DOM text nodes. The document-vs-text-control distinction remains intentionally unified by the `EditingSelection` facade.

<a id="rad18-3"></a>

#### RAD18-3

**Incomplete OS clipboard backends.** macOS uses NSPasteboard for text, HTML, images and single image-file copies. Win32/X11 rich-MIME adapters remain unimplemented; the fallback GLFW backend is plain-text-only, and `clipboard_store_sanitize` is a near-no-op.

<a id="rad18-4"></a>

#### RAD18-4

**Pattern-regex validation TODO (F5).** Form constraint validation `te_validate` cannot enforce `pattern="..."` without a lazy-compiled regex. (Detail belongs to RAD19, noted here because it is a spec-coverage gap in the editing surface.)

<a id="rad18-5"></a>

#### RAD18-5

**`Selection.modify` fidelity.** All accepted argument families are implemented, including bidi-aware visual left/right character and word movement. Word segmentation remains a simple alphanumeric classifier, while sentence and some layout-free line fallbacks are structural approximations; locale-sensitive and complex-script results can diverge from a browser. This is a bounded Selection-API conformance limit, not a general contenteditable-support gap.

### RAD19 — Form Controls & Text-Control Editing

<a id="rad19-1"></a>

#### RAD19-1

**Text-control target range is a StaticRange hack.** `editing_compute_target_ranges` builds a synthetic boundary over the control element with UTF-16 offsets instead of real DOM text nodes (`event.hpp`). It stays until form values become concrete DOM text — the same "E0 promotion" blocker referenced in RAD18. *Improvement:* promote `<input>`/`<textarea>` values to a shadow text node so DOM Range and form offsets unify.

<a id="rad19-2"></a>

#### RAD19-2

**`TODO(F5)`: `pattern="…"` validation is unimplemented.** `te_validate` (`text_edit.cpp:641`) checks number/email/url but has an explicit TODO for `pattern` because it "needs lazy-compiled regex". Until then a `pattern` attribute is silently ignored — a control with only a `pattern` constraint always reports `:valid`.

<a id="rad19-3"></a>

#### RAD19-3

**Projection-cache consumers remain.** Canonical `EditingSelection` (StateStore), the HTML-facing `form->selection_start/end` mirror, and caret/anchor projection cache are reconciled by hand in `tc_sync_selection_to_form` (`text_control.cpp:404`). The remaining cleanup is to port projection consumers directly to `EditingSelection` / DomRange geometry.

<a id="rad19-4"></a>

#### RAD19-4

**IME edge cases.** `te_ime_commit_prepare` (`text_edit.cpp:822`) accepts a composition *session* on readonly/disabled controls but rejects the commit, and orphaned compositions are silently dropped in `te_ime_begin` (`text_edit.cpp:775`). There is no reconversion / candidate-window geometry reported back to the OS, and the preedit caret is a single codepoint offset with no segment/clause underlining.

<a id="rad19-5"></a>

#### RAD19-5

**Paste sanitization is minimal.** `te_prepare_paste_replacement` handles newline normalization and `maxlength` truncation only (`text_edit.cpp:649`); it does not strip control characters, apply `inputmode`/type-specific filtering (e.g. non-numeric into `type=number`), or coordinate with the rich clipboard MIME store.

<a id="rad19-6"></a>

#### RAD19-6

**Constraint validation is a v1 subset.** Beyond the missing `pattern` (item 2), `te_validate` (`text_edit.cpp:602`) does not implement `min`/`max`/`step` for number/range, `minlength`, or the full email/url grammar — the `te_value_is_*` helpers are approximate.

<a id="rad19-7"></a>

#### RAD19-7

**Chrome-matched magic numbers.** `FormDefaults` (`view.hpp`) hard-codes dozens of pixel constants (153×21 text input, 57px select, per-type date/time widths in `layout_form.cpp:24-`) reverse-engineered from Chrome. These drift as Chrome's UA stylesheet changes and are unverified across platforms/DPI beyond the tuned cases.

<a id="rad19-8"></a>

#### RAD19-8

**Legacy dual dispatch surface.** `te_replace_byte_range` vs `te_replace_byte_range_no_events`, and `te_ime_commit` vs the `_prepare`/`_finish` split, exist purely to straddle "legacy caller without EventContext" and "unified dispatcher". The legacy variants (`text_edit.cpp:278`, `806`) are latent duplication once all callers own an `EventContext`.

<a id="rad19-9"></a>

#### RAD19-9

**Flat SVG and PDF omit textarea values.** Open, 2026-11-18. PNG paint reads
initial or current control text, including a script-assigned empty value.
The flat vector walk has no form-content callback. In the focused probe,
SVG's textarea group contained no text paint and the rasterized PDF was
identical for empty and populated textareas. Shared paint ownership follows
**D5.5.1** and control policy follows **D10.2.1**. The missing vector consumer
does not block independent HTML work.

### RAD20 — Application Shell, Browsing & Document Loading

<a id="rad20-1"></a>

#### RAD20-1

**Networking is borrowed, not owned, by the session — a lifetime coupling.** `BrowsingSession` holds `thread_pool`/`file_cache` pointers it explicitly does not own (`browsing_session.cpp:98`), and per-navigation `radiant_cleanup_network_support(old_doc)` is called from `session_navigate`/`go_back`/`go_forward` (`browsing_session.cpp:156`, `207`, `254`). The window creates network support only for the *initial* document if it is HTTP (`window.cpp:1100`); a later same-window navigation to an HTTP page relies on `show_html_doc`→`load_html_doc` re-initializing it, so ownership of network lifetime is split across three files. *Improvement:* give the session an explicit owned network context, or document the ownership contract in one place.

<a id="rad20-2"></a>

#### RAD20-2

**History cap is a hard-coded 100 with silent eviction.** `BROWSE_HISTORY_MAX` (`radiant.hpp`) drops the oldest entry by `memmove` (`browsing_session.cpp:131`) with no user signal. `history_ensure_capacity` also silently returns on `mem_realloc` failure (`browsing_session.cpp:39`), after which the push can fail and `session_navigate` returns `NULL` (`browsing_session.cpp:141`). *Improvement:* make the cap configurable and surface allocation failure distinctly from a load failure.

<a id="rad20-3"></a>

#### RAD20-3

**Windowless-by-default is a platform workaround, not a clean abstraction.** The headless branch avoids creating a window because GLFW's hidden-window path can deadlock in macOS LaunchServices (`ui_context.cpp:76-108`), reachable only via `LAMBDA_HEADLESS_GLFW_WINDOW`. Downstream code must therefore tolerate `uicon->window == NULL` everywhere (e.g. `update_window_title` early-returns, `ui_context_cleanup` guards `glfwTerminate`). A missed guard crashes only on the windowless path. *Improvement:* a null-object window wrapper would remove the scattered `if (window)` checks.

<a id="rad20-4"></a>

#### RAD20-4

**Single global `ui_context` blocks multi-document/tabbed browsing.** GLFW callbacks reference the global directly, so there is no per-window state; the shell cannot host more than one document at a time.

<a id="rad20-5"></a>

#### RAD20-5

**Dispatch responsibility is split and the old doc was stale.** Extension dispatch is in `load_html_doc_no_redirect` (`cmd_layout.cpp:3522`) while `load_html_doc` (`cmd_layout.cpp:3597`) only handles redirects — the former `Radiant_View_Design.md` claimed dispatch happened in `load_html_doc()`. `detect_doc_format` in `window.cpp` keeps a second extension list, now used only to name the format and to refuse unsupported extensions; every supported format loads through `load_html_doc`. *Improvement:* one extension→loader table shared by both.

<a id="rad20-6"></a>

#### RAD20-6

**`render()` does network draining and title management inline.** `render` (`window.cpp:685`) both flushes network completions and owns the loading-progress/partial-load window title (`window.cpp:704-757`), overlapping with the main loop's own `resource_manager_flush_layout_updates`. The double-flush is harmless but the responsibility split (loop vs. render) makes the load-completion path hard to follow.

### RAD22 — Media & Embedded Webview

<a id="rad22-2"></a>

#### RAD22-2

**No Linux or Windows video.** `rdt_video_stub.cpp:3-4` documents that FFmpeg and Media Foundation backends are "not yet implemented, using stubs"; every getter returns 0/false and playback never leaves `RDT_VIDEO_STATE_IDLE`. Video works on macOS only; elsewhere the page shows a permanent play button.

<a id="rad22-3"></a>

#### RAD22-3

**Remote media requires a complete downloaded file; progressive/range playback is unavailable.** Reverified 2026-11-17: `radiant_media_prepare` requests HTTP media through the input-owned resource manager, then copies ready cached bytes to an immutable local decoder file. Fast local-server MP4 controls load successfully, so HTTP media is no longer categorically unsupported. `rdt_video_open_file` still receives a complete local file; it does not consume a partially available HTTP stream. RI-121 fixes source-request ordering before the unchanged snapshot wait, while larger transfers may remain unavailable at capture. Going's 7 MB source decodes locally but its normal snapshot still lacks the video. Progressive/range playback requires a larger input/media integration and is not implemented here (**D9.1.2v2 / D10.1.7**).

2026-11-21 Slack follow-up: its hero MP4 is40,038,408bytes (HTTP200). The
saved-source native frame has no video metadata and uses300×150. A diagnostic
substitution of the existing local16:9 control restores500.16×281.34 and
brings the heading fromy64 toy113.17 (Chromium116.16). No separate grid defect
is established; no substitution, larger wait or admission change is shipped.
The first scratch browser pair was invalid because disabling scripting activated
a noscript redirect; the corrected helper asserts its saved file URL.
Evidence: `temp/online-visual-round138/`. Canonical Slack remains unchanged.

2026-11-21 Going follow-up: the selected H.265 MP4 returns HTTP200 with
Content-Length7,036,404, exceeding the unchanged per-resource source limit.
The missing hero video is therefore still within this recorded complete-file
loading gap. Response metadata is retained in
`temp/online-visual-round143/going-video-headers.txt`; no limit or source
substitution is changed (**D10.1.7 / D9.1.1v4 / D9.1.3v2**).

<a id="rad22-4"></a>

#### RAD22-4

**Layer-mode snapshot is forced synchronous by spinning the run loop.** `webview_layer_mac.mm:379-385` and `webview_layer_linux.cpp:453` block up to 500 ms pumping the platform event loop to turn an async snapshot into a synchronous call for the render pipeline. This is fragile (nested run-loop reentrancy, and a hard 500 ms stall on a slow snapshot) and is the main reason layer mode is still "Stage 2".

<a id="rad22-5"></a>

#### RAD22-5

**AVFoundation frame path is per-frame `AVAssetImageGenerator`, not a video output.** The workaround for error -11821 (`rdt_video_avf.mm:265-271`) copies a `CGImage` every frame via `CGBitmapContextCreate` — noticeably less efficient than a zero-copy `AVPlayerItemVideoOutput`/`IOSurface` path.

<a id="rad22-6"></a>

- [RAD22-6](<Radiant_Issue_Ledger (fixed).md#rad22-6>).

<a id="rad22-7"></a>

- [RAD22-7](<Radiant_Issue_Ledger (fixed).md#rad22-7>).

<a id="rad22-8"></a>

#### RAD22-8

**No macOS child-mode compositing / z-order.** Child-window webviews are always-on-top overlays with no relationship to document z-order; only layer mode composites correctly. Mixing a child webview under other content is impossible.

<a id="rad22-9"></a>

- [RAD22-9](<Radiant_Issue_Ledger (fixed).md#rad22-9>).

## Fixed issues

<a id="ri-013"></a>

- [RI-013 — Downloaded CSS overrides later document styles](<Radiant_Issue_Ledger (fixed).md#ri-013>).

<a id="ri-014"></a>

- [RI-014 — A zero-height body loses its canvas background image](<Radiant_Issue_Ledger (fixed).md#ri-014>).

<a id="ri-015"></a>

- [RI-015 — Authored inline buttons bypass atomic button layout](<Radiant_Issue_Ledger (fixed).md#ri-015>).

<a id="ri-003"></a>

- [RI-003 — Startup fragment navigation is followed as a document redirect](<Radiant_Issue_Ledger (fixed).md#ri-003>).

<a id="ri-004-layout"></a>

- [RI-004 — Dynamic baseline exposes native anonymous-table and match-parent mismatches](<Radiant_Issue_Ledger (fixed).md#ri-004>).

<a id="ri-001"></a>

- [RI-001 — HTML/CSS integration cases skip missing fixtures](<Radiant_Issue_Ledger (fixed).md#ri-001>).

<a id="ri-002"></a>

- [RI-002 — DOM integration cases skip unsupported standalone node creation](<Radiant_Issue_Ledger (fixed).md#ri-002>).

<a id="ri-005"></a>

- [RI-005 — Native static layout baseline needs execution qualification](<Radiant_Issue_Ledger (fixed).md#ri-005>).

<a id="ri-006"></a>

- [RI-006 — Property providers still live outside the unified Radiant archive](<Radiant_Issue_Ledger (fixed).md#ri-006>).

<a id="ri-007"></a>

- [RI-007 — Column flex measures a nested wrapping row too short](<Radiant_Issue_Ledger (fixed).md#ri-007>).

<a id="ri-008"></a>

- [RI-008 — font face source includes a missing alloca header](<Radiant_Issue_Ledger (fixed).md#ri-008>).

<a id="ri-009"></a>

- [RI-009 — Required standalone layout lint target is absent](<Radiant_Issue_Ledger (fixed).md#ri-009>).

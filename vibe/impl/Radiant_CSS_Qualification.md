# CSS qualification register — 2026-10-15

Phase 0 intake of merge `dffad2ab1`, runtime SHA-256 `0bcfb7f601707ca5766484990c266c82fb0b26de963ab086bb0c5376b440db2b`.
The saved merged Radiant gate reports 18 layout failures and one crashed paged
runner. A bounded capture of 38 readiness-bearing inputs reproduces exactly
17 failed, empty result frames; the other failure has a retained geometry tree.
No engine change, assertion relaxation or reference update was made.

## Layout failures

Paths below are relative to `test/layout/data/`. All seventeen capture failures
retain `reftest-wait` at the existing bounded deadline. The dependency column
records the input requirement, not a claim that every downstream runtime cause
has been debugged. They belong to the independent readiness/integration backlog;
none blocks the selected theme-color consumers.

| Case | Failed result / input dependency | Owner and disposition |
|---|---|---|
| `form/customizable-combobox/customizable-combobox-appearance.html` | empty failed capture; WPT testdriver pointer actions | WPT driver / native select integration; independent backlog |
| `form/the-option-element/dynamic-content-change-rendering.html` | empty failed capture; `TestRendered` event | WPT capture lifecycle integration; independent backlog |
| `form/the-select-element/list-box-important-colors.html` | empty failed capture; WPT testdriver pointer actions | WPT driver / native select integration; independent backlog |
| `wpt-css-images/image-orientation/image-orientation-none-cross-origin-svg.html` | empty failed capture; image/fetch/blob load completion | image resource and web API lifecycle; independent backlog |
| `wpt-css-images/image-orientation/image-orientation-none-cross-origin.html` | empty failed capture; image/fetch/blob load completion | image resource and web API lifecycle; independent backlog |
| `wpt-css-images/object-fit-containcontainintrinsicsize-png-001c.tentative.html` | empty failed capture; image load/frame completion | image resource lifecycle; independent backlog |
| `wpt-css-images/object-view-box-property-changed.html` | empty failed capture; image load/frame completion | image resource lifecycle; independent backlog |
| `wpt-css-tables/paint/col-change-span-bg-invalidation-002.html` | empty failed capture; WPT rendering helper/frame completion | WPT helper / snapshot script integration; independent backlog |
| `wpt-css-position/overlay/overlay-transition-dialog.html` | empty failed capture; dialog/frame script completion | dialog/script integration; independent backlog |
| `wpt-css-position/position-fixed-scroll-overlap.html` | empty failed capture; WPT rendering helper/frame completion | WPT helper / snapshot script integration; independent backlog |
| `wpt-css-position/sticky/position-sticky-change-top.html` | empty failed capture; WPT rendering helper/frame completion | WPT helper / snapshot script integration; independent backlog |
| `wpt-css-position/sticky/position-sticky-fixed-ancestor-iframe.html` | empty failed capture; embedded-document load callback | embedded-document/resource lifecycle; independent backlog |
| `wpt-css-position/sticky/position-sticky-fixed-ancestor.html` | empty failed capture; WPT rendering helper/frame completion | WPT helper / snapshot script integration; independent backlog |
| `wpt-css-display/display-change-iframe.html` | empty failed capture; embedded-document load callback | embedded-document/resource lifecycle; independent backlog |
| `wpt-css-display/display-change-object-iframe.html` | empty failed capture; embedded-document load callback | embedded-document/resource lifecycle; independent backlog |
| `wpt-css-display/display-flow-root-dynamic.html` | empty failed capture; WPT rendering helper/frame completion | WPT helper / snapshot script integration; independent backlog |
| `wpt-css-display/order-dynamic.html` | empty failed capture; WPT rendering helper/frame completion | WPT helper / snapshot script integration; independent backlog |
| `markdown/md_ky-readme.html` | retained layout mismatch: e.g. first divergent div height 1074.9 versus stored 492.91 in the saved diagnosis | Markdown/browser-reference owner; diagnosed stale reference, correction remains separate pending reproducible capture provenance |

## Snapshot controls (Q0.2)

The merged runtime was checked once with the ordinary ticker, 12-frame ready,
24-frame ready, cancelled-frame, unresolved-ready and following-ready documents.
Widths are respectively 14, 22, 34, 34, no payload and 22px. Exit status is 1
because the unresolved document is rejected; its frame has zero payload and the
following document succeeds. Existing merged native text cases also verify the
cancellation marker. This preserves SN1 in `vibe/radiant/Radiant_Design_Event.md`.
Raw frames and assertions: `temp/css_plan_delivery/snapshot_controls.json`;
readiness candidate results: `temp/css_plan_delivery/intake_frames.json`.

## Other required-gate debt (Q0.3–Q0.4)

- `test_view_reuse_gtest` crashes at
  `SecondaryViewTest.CaptionInheritanceAndGeneratedCountersPreserveSourceOrderWhenReordered`.
  This prevents an all-green Radiant claim and leaves native cases unexecuted.
  Owner: [paged integration §9](Radiant_Impl_Paged_Integration.md#9-progress-record).
  Its legacy migration records 306 failures /15 crashes of 498 cases after P4;
  the CSS schedule does not acquire that separate migration.
- `chart_chart_dashboard`: the preserved core checkpoint has six word-cloud
  text baselines shifted by 0.5px. Its saved comparison says current Chromium
  agrees with all six corrected positions. Owner: chart reference/core golden.
  Keep the raw failure and existing golden until a separately reviewable
  source/reference capture is available; do not insert font offsets.
- `md_ky-readme`: the same large content-height discrepancy exists before the
  recent leading repair (1056.9 versus stored 492.91) as after it
  (1074.9 versus 492.91). Existing evidence diagnoses stale content/reference;
  do not claim fresh browser parity or regenerate the entire layout corpus.

Evidence: `temp/merge_css_conflicts/resolution_summary.json`, the saved Radiant
reports, `temp/css_support_p0_p1_resume/{marker_baseline_md_ky_report.json,
nested_font2_md_ky_report.json,nested_br_checkpoint_summary.json,
nested_br_core_failed/}` and the dated CSS plan appendix. New intake source/input
digests are in `temp/css_plan_delivery/intake_provenance.json`.

## Phase 0 disposition

Q0.1–Q0.4 are classified. There are no selected Phase 1 blockers. Qualification
debt remains failed and owned above; classification does not repair it or imply
a passing aggregate. Proceed to Phase 1. Preserve style ownership
(**D4.5.1v4 / D4.1.4v5**) and module boundaries (**D7.3.3–D7.3.4**).

## Phase 1 required-gate update

The valid native-graphics Radiant run records **5,333 passed /155 partial /
19 failed /one skipped out of 5,508**. Its failures are the same eighteen
layout inputs above and the same paged runner SIGSEGV. Computation 137/137,
text 123/123, vector 164/164 and UI 602/602 pass. Evidence:
`temp/css_plan_delivery/radiant_graphics/test_summary.json`.

The initial restricted run was stopped after macOS XPC/graphics hangs; a native
control passes immediately with graphics access. It is not qualifying evidence.
Input projects were then rebuilt from the runner's actual 71-project inventory:
`build-input-baseline` alone does not refresh the legacy CSS executables.

Seven input executables retain these independent failures. Saved pre-rebuild
binary controls have identical failed case IDs and assertions, ignoring only
memory addresses; no assertion or golden was changed. The rebuilt full input
run reports 4,164 passed /34 failed over 4,198 reported entries. Eight failed
entries are startup-crash sentinels and do not count their unexecuted native
cases; the remaining failures are fifteen native cases and eleven exporter
groups.

| Owner | Failing native cases |
|---|---|
| CSS framework round-trip | `CssAllFilesTest.RoundTripFormattingTest` (modern-normalize/tailwind); `AllCSSFiles/CssFileRoundtripTest.FileRoundtrip/{bootstrap_css,bulma_css,foundation_css,materialize_css,stylesheet_3_0_css,tailwind_css,uikit_css}` |
| CSS parser legacy expectations | `CssParserUnitTest.Declaration_LineHeight_RejectsUnknownDimension`; `CssParserUnitTest.Declaration_Display_CustomLayoutDeclarationParser` |
| DOM occurrence/relink identity | `DomIntegrationTest.UiRepeatedValuesHaveIndependentDomOccurrences`; `DomIntegrationTest.UiChildRelinkRetainsCommentIdentity` |
| HTML/CSS test storage assumptions | `HtmlCssIntegrationTest.AVLTreePerformance_MultipleProperties` |
| Markdown source positions | `SourcePosTest.MarkdownSpanTextIsTheConstruct` |
| Graph parser LOC budget | `GraphParserTest.ParserLocBudget` |

Comparison: `temp/css_plan_delivery/input_control_comparison.json`; raw controls
and current reports are under `input/`, `input_controls/` and `input_current/`
in that directory. These failures prevent an all-green input claim and remain
outside the selected theme-color task list.

The eight validator-related executables (`test_validator_input_gtest`,
`test_null_vs_missing_gtest`, `test_validator_features_gtest`,
`test_enhanced_errors`, `test_validation_options`, `test_format_validation`,
`test_validator_integration`, `test_type_references_simple`) abort at startup
with dyld's missing `_LIT_BOOL`. The same diagnostic exists in all eight
pre-rebuild controls. Owner: validator runner linkage; this is not a color
consumer regression and prevents full input execution.

The exporter passes **284/295 groups**, retaining **216 raw assertions in eleven
groups**. The five historical gradient/layer/skip-ink/opacity/PDF-import groups
remain. The other six belong to the paged integration owner's recorded
caption, body-column, explicit-column, FO-control and size-container migration:

- `PagedCaptionsMatchBlockWrappersAcrossTableAndCaptionContinuations`
- `BodyColumnRulesSharePreviewSvgAndPhysicalPdfGeometry`
- `PatternedBodyColumnRulesRemainVectorAndMatchIndependentPdfRasters`
- `FoAndNativePageControlsSharePreviewAndPhysicalPdfPages`
- `ExplicitNativeColumnsRetainUnequalGeometryInPreviewSvgAndPhysicalPdf`
- `SizeContainersMatchExplicitGeometryInFlatAndFragmentedExports`

See [paged integration §9](Radiant_Impl_Paged_Integration.md#9-progress-record),
which records the independent failures on its control build. Current raw group
names/assertions remain in `input_current/test_pdf_render_visual_gtest_results.json`.
The new modern-color output group and both focused neighbors pass.

**Phase 1 disposition:** C1.1–C1.3's feature acceptance is complete. Required
gates were run and remain failed for the independently owned debt above; this
is not an all-green qualification. Module boundaries and layout lint pass.
No owned operand representation was added: deferred mixes reuse existing
immutable trees and owned cloning, with source-pool destruction tested. No
fresh ASan claim is made. Receipt: `temp/css_plan_delivery/phase1_result.json`.

## Phase 2 required-gate update

The new source records **5,335 passed /155 partial /19 failed /one skipped over
5,510 Radiant entries**. The same eighteen layout inputs and paged runner crash
remain; computation 138/138, text 123/123, vector 164/164 and UI 603/603 pass.
The scoped CSS input gate runs all nineteen CSS/HTML-CSS executables and records
**950/963**, retaining the same thirteen failed cases and normalized assertions
as the Phase 1 controls. No failures were filtered inside those executables.

The new fixture passes **35/35 per AST/MIR**. Both selected-view/native query
controls pass, as do the existing resize and media-query GC fixtures per backend
and the modern-color/border-width output neighbors. Module boundaries retain
137 deferred imports with zero unapproved; layout integer lint and inventory
freshness pass. No owned operand or target storage changed, and no fresh ASan
claim is made.

Scope: this phase changes CSS query environment/native invalidation. The full
input alias's unrelated PDF-import/export and validator runs were not repeated;
Phase 1's full-input evidence is not transferred to this new runtime. See
`temp/css_plan_delivery/{phase2_result.json,media_css_scope.json,
media_css_failure_comparison.json,media_radiant/test_summary.json}`.

**Phase 2 disposition:** M2.1–M2.3 are delivered. Independent gate debt remains
failed and owned above. The next selected work is Phase 3.

## Phase 3 implementation controls

The new interaction fixtures currently pass 20 resnap assertions, 28 keyboard
assertions (including nested textarea selection) and 15 resize assertions. The
native no-realm CSSOM write exposed an absent observer capsule; notification now
returns before reading that capsule when no observer realm exists. The same
native setter replaces inline priority while retaining other authored properties.

An additional editing neighbor, `test_form_input_caret_navigation`, records
8/9: all caret assertions pass, but its stored Mark dump omits the current
document-parent HTML `index: 1`. The saved pre-change executable
`temp/css_support_p0_p1_resume/snapshot_reject_export_frozen/lambda.exe`
(SHA-256 `f77ce37219bd5a907632fda5e12fa1ae7c4604af98b43527c02a7a30351c1dd9`)
reproduces the identical expected/actual mismatch. This additional control is
outside the configured Radiant baseline selection; its golden remains unchanged.
See `temp/css_plan_delivery/keyboard_neighbor_{probe,control}.{json,log}`.
The completed Phase 3 gates are recorded below; these controls do not claim
an all-green aggregate.

The Phase 3 Lambda gate also reports two MVP admission-diagnostic assertions:
`JsMvpLmd.OptionalIntegerElementsPreserveUndefined` and
`JsMvpLmd.PropertyExpansionOptionalNumericKeys`. Both expect a fallback
diagnostic for typed-array indexing but receive none. Their source owner is
`lambda/js/mvp-lmd/`; no JS admission source changed in this CSS delivery.
Historical saved MVP runs contain 227 cases, versus the current 647, so they do
not establish the status of these newer cases. Keep these failures explicit;
do not alter the JS gate or claim an exact pre-change comparison. Raw assertions:
`temp/css_plan_delivery/phase3_mvp_diagnostic_failures.json`.

Phase 3's required Radiant gate records **5,339 passed /155 partial /19 failed /
one skipped over 5,514**. Its UI selection is 606/606. The same six layout
categories retain eighteen failed entries and the legacy paged runner crashes.
The scoped CSS gate records **951/964** with exactly the same thirteen case IDs
and address-normalized assertions as Phase 2.

The Lambda alias records **5,311/5,314**: the two MVP cases above and
`chart_chart_dashboard`, whose first difference is the already recorded
word-cloud text y=9 versus stored 9.5. Forced-GC stress passes 313/313. The alias
stops before its explicit tail commands; the DOM-node tail was run directly and
passes 40/40. No success is claimed for the skipped tail commands.

AST/MIR acceptance totals are 20 resnap, 28 keyboard and 15 resize assertions.
The supported ASan CLI passes 20+15 resnap/resize assertions, with
`detect_leaks=0`; its native parent runners are not instrumented. Boundary
validation retains 137 deferred imports with zero unapproved, and layout lint,
inventory freshness and diff checks pass. Full input/export was not repeated.
Receipt: `temp/css_plan_delivery/phase3_result.json`.

## Phase 4 focused qualification

Atomic-inline ellipsis passes all six independently browser-qualified shape
references through PNG/SVG/PDF. The live fixture passes 33/33 per AST/MIR,
including width changes, unchanged atomic source bounds, pixel/hit suppression,
plain Latin/Hebrew RTL lines, font/direction inheritance and clamp-count changes.
The native projection/teardown control preserves logical UTF-8 and source
rectangles while selecting the Hebrew prefix and positioning glyph clusters.

`RenderOutputParity.RtlTwoLineClampMatchesExplicitVisualLinesAcrossOutputs`
passes raster comparisons but retains four strict vector-raster assertions:
the clamped and fitting Hebrew references each differ in PDF and SVG at glyph
antialias fringes. First differences are 58 versus 59 (PDF) and 135 versus 137
(SVG). Glyph order, marker side and raster pixels agree. Keep the assertions and
literal visual-order references unchanged; no threshold or golden was loosened.
Owner: glyph/vector path positioning/serialization. This precision residue is
deferred under the plan's minor-AA exclusion, rather than extending the core
feature milestone. Raw evidence: `temp/css_plan_delivery/phase4_outputs_final.log`.
The Phase 4 Radiant milestone gate records 5,340 passed /155 partial /19 failed /
one skipped (5,515 total), including 607/607 UI cases. Its 18 layout failures and
legacy paged runner SIGSEGV retain the Q0 dispositions. Runtime SHA-256:
`d4a30c3753020bbdc36c8c9ec296257590577a2ae6f682a15faedcc3792cc1a9`.
Receipt: `temp/css_plan_delivery/phase4_result.json`. Full input/core and a fresh
ASan matrix were not repeated for this scoped paint projection increment.


## Phase 5 motion delivery — aggregate failures retained

Typed seconds/milliseconds and absolute time math now share one CSS evaluator
path. Calculated durations clamp at computation; signed delays survive, negative
literal durations are rejected and wrong dimensions default after substitution.
Numeric `linear()` uses normalized percentage inputs, distribution, paired stops
and repeated-position jumps through the shared sampler. Retained scheduler/effect
owners copy the points and release replaceable scheduler payloads
(**D4.5.1v4**); native controls exercise parser-pool retirement and live frees.

The native motion runner passes 145/145. The animation/transition fixtures pass
22/9 assertions per AST/MIR, including opacity/translateX pixels/hits, live
variables/curve changes, retargeting and removal. CLI ASan passes all 31 assertions
(AST, `detect_leaks=0`); parent runners remain uninstrumented. Fixed-clock fixtures
explicitly set `input_turn_ms: 0`, rather than assuming the harness has no input
latency. No assertion tolerance was broadened to hide time drift.

Radiant records 5,348 passed /155 partial /19 failed /one skipped (5,523 total),
with 609/609 UI cases passing. The existing eighteen layout failures and legacy
paged runner crash retain their owners. Scoped CSS records 951/964 and the same
thirteen failure IDs/assertions; one diagnostic contains different native pointer
addresses, normalized only in the comparison receipt, not in the tests.
`temp/css_plan_delivery/phase5_css_failure_comparison.json` preserves both raw
and normalized comparisons. Layout lint, inventory and module boundaries pass
(137 deferred imports, zero unapproved).

Runtime: `d3692e2b385d9b6ac03624f32facd69b3d7610f3f72d23f6a68d5c68cc293749`.
Receipt: `temp/css_plan_delivery/phase5_result.json`. The matching Lambda baseline
selection records 5,311/5,314, including five freshly run input corpora
(2,112/2,112). The same two MVP diagnostic cases and chart dashboard golden fail;
forced-GC passes 313/313 and native JS passes 504/504. The failure IDs retain the
Phase 3 owners. Native runners were rebuilt before the baseline selection ran
directly alongside the exporter on the unchanged frozen host. Input corpora ran
separately; no previous Lambda/input result was transferred. Raw results:
`temp/css_plan_delivery/phase5_lambda/test_summary.json` and
`temp/css_plan_delivery/phase5_input_baseline_results.json`.

## Phase 6 gradient qualification — strict perimeter failures retained

The existing shared linear/radial range normalization already preserves the
selected non-repeating interval colors. The complete named range test passes
native raster and SVG pairs at both densities, and all PDF pairs at 1×. Eleven
strict 2× PDF comparisons remain failed. Pixel analysis finds zero differing
interior pixels; differences are confined to the box perimeter (161 pixels,
or 201 for the transformed case). Channel deltas can reach 255: this is thin
boundary coverage, not a claim of uniformly small channel error.

Owner: PDF gradient image/clip-edge lowering and reader rasterization. Keep the
strict failures under Phase 6's explicit reader-edge exclusion, without changing
comparators, goldens or masks. The earlier opaque-mask trial already recorded
marker regressions in Appendix B; it is not repeated. No new paint producer
change is warranted by the bounded color requirement.

Raw evidence: `temp/css_plan_delivery/phase6_range_before.log`; location analysis:
`temp/css_plan_delivery/phase6_range_difference_locations.json`. The full exporter
gate records 285/297 groups, retaining 220 assertions in twelve failed groups.
The prior eleven group IDs/assertion counts are unchanged; the additional group
contains Phase 4's four Hebrew SVG/PDF antialias assertions. The 19 range shapes
produce 114 exact image comparisons: 103 pass, eleven PDF perimeter comparisons
remain failed. Neighboring center/radius/stop controls pass; ordered-layer and
opacity edge assertions retain their prior dispositions. No new production
paint change was required. Receipt: `temp/css_plan_delivery/phase6_result.json`;
comparison: `temp/css_plan_delivery/phase6_exporter_comparison.json`. This is
bounded color qualification with explicit failures, not an all-green exporter.

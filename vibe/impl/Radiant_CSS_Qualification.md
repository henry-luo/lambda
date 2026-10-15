# CSS qualification register — 2026-10-15

Phase 0 intake of merge `dffad2ab1`, runtime SHA-256 `0bcfb7f601707ca5766484990c266c82fb0b26de963ab086bb0c5376b440db2b`.
The saved merged Radiant gate reports 18 layout failures and one crashed paged
runner. A bounded capture of 38 readiness-bearing inputs reproduces exactly
17 failed, empty result frames; the other failure has a retained geometry tree.
No engine change, assertion relaxation or reference update was made.


## Active repair request — 2026-10-15

The user's follow-up explicitly promotes this register's precision residue and
regression debt into implementation scope. Earlier “independent backlog” and
“deferred” dispositions below describe the completed feature schedule, not the
current repair request. The active goal covers the named failures; it does not
silently acquire all 498 cases of the separate legacy paged migration.

Repair order and finite acceptance:

1. Remove the registered paged crash, retaining caption-side ordering, inherited
   alignment and all three source-order generated-counter assertions. Preserve
   selected-view ownership (**D4.5.1v4**) and UI copy boundaries (**D4.5.3v2**).
2. Close all thirteen registered CSS failures: seven framework rule-count
   roundtrips, the two framework input comparisons, two parser expectations,
   two DOM occurrence cases and the HTML/CSS storage case (the two framework
   input mismatches are assertions within one native case).
3. Close the four Hebrew and eleven gradient-edge comparisons with their exact
   existing pixel assertions and literal references; check shared glyph,
   gradient-marker, transform and alpha neighbors once the fixes stabilize.
4. Capture all seventeen readiness inputs after their intended mutations;
   investigate missing methods/resources and event delivery by family. Validate
   the Markdown and chart references against independently captured Chromium
   output before changing either golden.
5. Repair the remaining named paged/export groups and input startup failures;
   run the required Radiant, scoped CSS/input and affected Lambda gates on the
   final frozen source. Report any remaining failure explicitly.

### Wrap-up checkpoint — work stopped at the user's request

Changes are uncommitted. The latest CLI build succeeds. The four original Hebrew
and eleven gradient-edge assertions, all thirteen registered CSS failures, all
eighteen original layout rows and all three named Lambda failures have focused
passing evidence. This does **not** make the broader gates green.

| Gate | Last full run | Follow-up evidence |
|---|---|---|
| CSS scope | **965/965** | nineteen executables, `css_scope_result.json` |
| Lambda baseline | **5,319/5,321** | object-URL fixture passes after its origin-contract correction; isolated LaTeX timeout replay passes in 37.3 seconds; `js_dom_final.json`, `latex_corpus_isolated.json` |
| Input baseline | **4,308/4,318** | all 4,033 non-export cases pass; exporter **287/297**, with three subsequently repaired groups passing in `wrapup_paged.json` |
| Radiant baseline | **5,348 pass /24 fail /155 partial /one skip** | UI **609/609**; two DOM subprocess terminations pass isolated replay; kitchen-sink teardown and fetch admission have focused repairs; nineteen additional layout failures remain in the raw aggregate |

The last paged repair admits native traits only on source-backed boxes: generated
anonymous table fixups have no DOM identity and were incorrectly reported as an
allocation failure. Fragment edge policy reads the source's computed style,
rather than optional occurrence metadata. Paged Markdown and both split-cell
border/alignment groups now pass **3/3** (`wrapup_paged.json`). Parent fetches that
complete while a child pumps the shared loop retain their existing precise roots
until the parent's checkpoint (**D5.3.3 / D5.4.1**); the unchanged cold/warm
admission control passes (`wrapup_fetch.json`). These latest changes have not had
another full aggregate or sanitizer run.

Remaining work is bounded and recorded:

- **Seven exporter groups:** five text/decoration-shadow groups have strict
  2× PDF fringe differences; `TransformedDecorationsMatchPlacedGeometry` has
  one SVG wavy-stroke difference; the imported-PDF group retains the Prince
  cover mismatch below. Raw full result:
  `input_final/test_pdf_render_visual_gtest_results.json`. Keep thresholds and
  baselines unchanged.
- **Nineteen additional layout rows:** fresh independent Chromium captures
  reveal a symlink/canonical-path mismatch in the browser resource resolver.
  With its prepared correction, six object-view-box and five sticky cases pass;
  eight sticky-table cases retain real geometry differences. The corrected
  resolver and these new references are **temporary artifacts, not installed**.
  Receipts: `browser_layout_realpaths_capture.log`,
  `new_capture_realpath_comparisons.log`, `new_capture_comparisons.json`.
- **Lifetime qualification:** kitchen-sink no longer crashes, but memory tracking
  still reports **9,447,755 bytes /14,723 allocations** after cleanup
  (`kitchen_sink_cleanup_rebound.log`). Retaining its custom-paint evaluator
  through document-resource teardown fixes the stale precise-root owner; the
  retained allocations remain unresolved. Do not describe this as leak-free.
- **Legacy paged runner:** the original source-order caption case passes. The
  last full runner reaches another old caption-minimum crash. After replacing
  its obsolete fragment-child dereference, focused replay exits normally with
  two assertions still failing: fixed-grid width is 60 rather than 134, and the
  source-specific row occurrence is absent (`wrapup_caption.json`). The original
  caption case passes in the same rebuilt runner. The full legacy runner has
  not been replayed; its separate 498-case migration stays open.
- **Final qualification:** preserve the raw aggregate results above; focused
  replays are separate evidence. Do not rerun the whole core/forced-GC gate
  alongside render sweeps. The two terminated DOM cases passed isolated
  (`dom_terminated_replay.json`); the full Lambda timeout likewise passed alone.

All evidence paths in this checkpoint are under `temp/css_debt_repair/`.
Layout integer-cast lint and the regenerated 411-record CSS inventory pass;
all four qualification columns are unchanged. Diff whitespace checks pass
excluding the inventory's existing trailing empty TSV fields. The last boundary
audit passed at 137 deferred imports against 165; it predates the final fetch
checkpoint change and was not rerun for this wrap-up.
The already-installed Chromium/reference changes also require the sibling
`lambda-test` checkout; the main repository alone does not contain those files.

Current focused evidence is under `temp/css_debt_repair/`. Both newer MVP cases
already pass on rebuilt HEAD without JS changes (`mvp_current.log`). The paged
caption regression now passes (`caption_final.log`): its old computed-style
pointer assertion was obsolete, and caption layout also omitted its counter
scope/generated-content materialization. Width refinement now reuses the
source-order generated text. Both UI occurrence regressions pass under
**D4.5.3v2** (`dom_migrated_final.log`).

The selector formatter preserves pseudo-element identity and nth-child `of`
filters. Native CSS roundtrip passes 55/55, parser 90/90 and formatter 45/45.
The framework input runner passes 11/11 using accepted-rule preservation and
exact canonical stability rather than a raw-source percentage that counted
rejected vendor selectors. This stronger check also exposed and fixed accumulating
at-rule whitespace, unescaped CSS strings and charset whitespace consumption.
See `css_{roundtrip,input,parser,formatter}_qualified.log`. The complete nineteen
CSS executables now pass **965/965**, with zero failed runners
(`css_scope_result.json`). Required broader aggregate gates remain pending.

The original Hebrew and gradient comparisons plus marker/transform neighbors
pass in `precision_final.json` (four groups; the marker group covers its full
existing matrix). The broader frozen exporter run records **280/297** with
17 failed groups (`export_current.json`): the global per-glyph strategy also
changes shadow and text-background fringes. That strategy was removed. Combining
the existing uniform-RTL projection into visual source-line runs now passes the
original Hebrew group and text-background neighbor (`rtl_grouped_final.log`);
shadow, emphasis and hard-gradient text controls also pass. Keep both receipts
rather than transferring the earlier four-group result to the full suite.

The remaining Lambda chart golden changes exactly six SVG `y` attributes.
Current Chromium 152.0.7977.42 independently confirms all six font-cell metrics
(`chart_browser_metrics.json`, `chart_golden_receipt.json`), and the real golden
runner passes (`chart_final.log`). No font offset was added to production code.
Together with the two current MVP controls, all three named Lambda failures
are repaired or confirmed passing; the final required Lambda gate is pending.

Replacing the never-settling local WPT frame-helper stub, delivering the committed
`TestRendered` frame, implementing dialog close, dispatching WPT actions through
native input, adding canvas bitmap drawing, and completing embedded navigation
before parent load now produce fifteen of the seventeen formerly empty captures
(`readiness_families2.log`, `iframe_cache_final.log`, `readiness_current.log`).
Dialog and canvas behavioral controls pass in AST and MIR. The two remaining
image-orientation cases require HTTP origin/CORS setup; a pinned local wptserve
probe exposes missing Blob URL resource loading and FileReader, rather than a
frame-settlement defect. A successful capture alone is not an asserted browser
geometry pass. Fresh Chromium capture of the Markdown input removes the large
stale-reference discrepancy. Preserving relative inline offsets through final
line alignment and retained reflow clears the superscript differences
(`relative_inline_final.json`). One code element and two text records still
wrap differently because of accumulated font advances
(`md_ky_relative_comparison.json`); the stored reference is unchanged.

Fixed-PDF export passes all three named groups (`fixed_pdf_final.json`): the
flow hooks had created a spurious unlabeled shell page before the fixed sheets.
Publishing occupied body columns restores patterned rules; the solid-rule
fixture now scopes `white-space:pre` to its paragraphs, preserving its intended
line counts without turning XML indentation into content. Both rule groups
pass (`column_rules_final.json`, `body_columns_qualified.json`). Definite-height
box tails now allocate their remaining fragmentainers, clearing the size-container
export (`paged_tail.json`), and PDF opaque rectangle unions retain one edge
coverage before group alpha (`pdf_union_extent.json`). Opaque constant background
composites use the solid-fill contour and the layered-background group passes
(`background_solid_final.json`). Broader final exporter qualification is pending.

The caption exporter now passes fixed/automatic grids and table/caption
continuations (`caption_grid.json`). Percentage table sizing retains its already
resolved containing-block width, border-box padding is counted once, and the
transparent caption wrapper publishes a separate grid decoration box. Bottom
captions begin after the grid's bottom border.

All eight formerly aborting validation runners now pass **124/124**
(`{validator,null_missing,enhanced_errors,format_validation,validator_features,
validation_options,validator_integration,type_references}_fixed.json`). The
internal validation library now links statically with its runtime; a dynamic
library had referenced hundreds of private host symbols and aborted on
`LIT_BOOL`. This preserves the module boundary (**D7.3.3–D7.3.4**) without
exporting runtime internals. The source-position suite passes **5/5**
(`sourcepos_fixed.json`): its exact source-range matcher now recognizes the
[GFM §6.9 email autolink grammar](https://github.github.io/gfm/#autolinks-extension-),
including numeric domain labels. The parser output was already correct.

The graph parser suite passes **20/20** (`graph_audited.json`). The footprint
review in `vibe/Lambda_Pkg_Graph.md` §19.3.5 accounts for the existing sequence
family, shares repeated line-label normalization across families and tightens
all six source-unit ceilings to their audited sizes. The rebuilt
Mermaid corpus also passes **40/40** (`graph_corpus_qualified.json`).

The final narrow RTL strategy and gradient contour changes pass all eight
selected exporter groups (`precision_scoped_qualified.json`), including the
original four Hebrew and eleven 2× gradient-edge assertions, the complete
gradient-marker matrix, transformed overflow, shadow, emphasis, hard-gradient
text and text-background controls. Wavy skip-ink now tessellates its cubic once
before applying interval clips, removing backend clip-dependent subdivision;
both raster and SVG/PDF pattern groups pass (`skip_ink_tessellated.json`).
The full final exporter gate remains pending.

Markdown now matches the independently captured current Chromium tree at the
existing tolerances: **1,100/1,100 elements, 2,153/2,153 text records and 2/2
spans** (`md_context_comparison.json`). The remaining cause was outgoing
punctuation kerning across inline boundaries: `r` before the link's following
comma lost its contextual advance under per-character wrapping. Context
measurement includes ASCII punctuation and records advance ownership so the
next glyph does not apply the same pair twice. Native controls retain padding
boundaries, contextual script shaping and retained reflow
(`punctuation_context_native.json`). The sibling `lambda-test` corpus gains
macOS-specific full/compact references, independently captured by Chromium
152.0.7977.42; generic/Linux references stay intact. Source hashes, original and
new reference digests are in `md_ky_reference_receipt.json`.

The rebuilt native text/layout target now passes **126/126**
(`text_qualification.json`). Current captures of the fifteen repaired readiness
inputs plus Markdown complete **16/16**, but only eight initially match their
stored browser references (`readiness_qualification.json`). Several remaining
references visibly describe the pre-mutation state (for example block versus
inline flow-root, original flex order and the old column span). They require
independent browser captures that honor `reftest-wait` and `TestRendered`, not
further native changes made to match the wrong state. Fresh comparisons are
being prepared under `browser_readiness/`; no readiness reference has been
replaced yet.

All **16 local layout cases now pass** through the ordinary reference resolver
(`local_readiness_qualified.json`): the fifteen non-HTTP readiness fixtures plus
Markdown. Eight macOS full/compact reference pairs now capture the intended
mutation, with input hashes and Chromium version preserved. The sibling
`lambda-test/layout/extract_browser_references.js` dispatches `TestRendered`
for pending explicit reftests and rejects a readiness timeout before writing a
reference. The two HTTP image-orientation fixtures remain outstanding.

The final local sticky case required real native repairs in addition to its
reference refresh: fixed-ancestor constraints use viewport coordinates; initial
script geometry acquires its document state owner; scroll-dependent offsets
synchronize on CSSOM reads without advancing pending DOM commits; window scroll
getters use the active document before loader publication; and `window.scrollTo`
writes the single `document.scrollingElement` instead of scrolling root and body
twice. The original case matches all eleven element and two text records
(`sticky_qualified_comparison.json`). A causal native frame test checks the
immediate post-scroll offset, single viewport translation, fixed positioning
and reset, alongside existing scheduling/cancellation controls
(`sticky_snapshot_native.json`). Ownership and commit boundaries preserve
**D4.5.1v4 / D5.4.1**.

## Layout failures

**Repair checkpoint:** all eighteen original rows pass focused comparisons.
The sixteen local cases use the ordinary platform reference resolver; the two
HTTP cases pass all **43 element and seven text** records against independently
captured Chromium 152.0.7977.42 references. The sibling corpus now launches the
pinned WPT HTTP server for mirrored fixtures declaring `get-host-info.sub.js`,
verifies identical pinned HTML, retains real localhost/127.0.0.1 origins and
uses the same environment for Chromium, single-file and streamed native runs.
Receipts: `http_qualification/receipt.json`, `http_reference_manifest.json` and
`local_readiness_qualified.json` under `temp/css_debt_repair/`.

Image writes retain pending network requests until native load/error completion.
Blob URL bytes have a document resource owner (**D4.5.1v4**); revoke removes that
resource. This bounded implementation does not claim cross-document Blob URL
sharing. FileReader supports asynchronous `readAsDataURL`, state/result/error,
load/progress/end events and abort cancellation; other read methods and the
ProgressEvent class remain outside this repair. FileReader and bitmap-source
controls pass in AST/MIR, and the rebuilt native JS runner passes all three new
FileReader/canvas/dialog fixtures (`js_image_readiness.json`). The text/scroll
native checkpoint passes **127/127** (`text_scroll_qualified.json`).

Unequal native columns now reflow their containing widths and line widths over
the existing bounded convergence passes; finished boxes are not stretched to
fake wrapping. The original unequal-column export group passes with its exact
PNG/SVG/PDF geometry and color assertions (`paged_half_open.json`). Undoing an
internal text strut preserves the block's definite used height. Half-open page
lookup no longer applies the closing-edge epsilon twice, removing an empty
trailing page. Terminal extent updates happen once per sequence and reserve a
real body tail when a narrower last master would otherwise oscillate. The
paired FO/native exporter now passes all **53 fixture pairs**, including exact preview
pixels and every physical PDF page (`fo_computed_edges.json`). Tall notes retain
legal same-width continuation pieces before the next sequence; changed-width
note continuations and nested named-page width changes remain unsupported.
Selected native property bindings reach the live CSS workspace and its final
background pass, and independently retained padding/border edges drive both
break reservations and paint. FO root defaults no longer mask authored whitespace.
The bounded flow, color, inherited-whitespace and original caption controls pass
**22/22** (`flow_fo_final.json`). Native trait admission rejects invalid values
before the CSS workspace can approximate them, preserving original FO diagnostics.

The transformed-image sampler now shares the existing area filter used by
ordinary CPU minification, including premultiplied/straight alpha and flips.
All six image-paint controls pass (`image_projection_native.json`). This clears
three of six imported-PDF regressions. Calibrated SVG text keeps vector coverage,
and the PDF comparison retains its natural aspect ratio instead of centering
within Poppler's rounded bitmap height. The latter is independently checked with
a literal PDF bar (`pdf_scale_control.pdf`). **43/44 imported pages** now meet
the unchanged baselines; `PrinceCatalogue_page_01` remains at **2.968104%**
against **2.30256065%** (`pdf_natural_calibration.json`). Its JPEG payload is
byte-identical to the independently extracted source. An accurate-DCT experiment
changed the result only to 2.927224% and was reverted. Neither PDF baselines
nor pixel thresholds changed.

The first aggregate replay exposed an outer-turn mutation loss after the JS
realm became available during load: nested lifecycle/synthetic dispatch cleared
records belonging to the loading script. Nested dispatch now preserves that
ledger and defers its rebuild until the outer turn (**D5.4.1 / D5.3.3**). The
unchanged load-cascade, dynamic auto-fit grid, jQuery tabs and noUiSlider drag
controls pass (`{cascade,grid,tabs,slider}_nested_fixed.json`). The object-URL compatibility fixture now checks the document origin rather than
the retired `blob:lambda/` mock prefix; its expected output is unchanged. Aggregate replay
is required; the interrupted layout batch and earlier failing UI totals are
not transferred to the repaired source. The full legacy paged runner also
reaches a different, previously recorded caption-minimum crash; the named
source-order caption crash remains passing. Its separate migration stays open.

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

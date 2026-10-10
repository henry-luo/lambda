# Lambda UI dtna implementation

**Started:** 2026-10-09. **Product target:** native Radiant.
**Design:** [Lambda_Pkg_UI.md](../Lambda_Pkg_UI.md).
**Public guide:** [Lambda_UI.md](../../doc/Lambda_UI.md).

The implementation provides reusable component sources, a default-light
theme, native controls, flat navigation, disclosure and collection components,
and rendered component explorers.
The complete 73-entry Phase 1 remains the objective. The inventory currently
records **0 complete, 38 partial, 35 planned**; this increment does not close
M0 or the full Phase 1 gate. Browser interaction and additional themes remain
deferred by the user's scope decision.

## Contracts and ownership

- **D7.2.4/D7.2.6:** `lambda.ui.dtna` resolves to the ordinary shipped
  `lmd/package/ui/dtna.ls`. There is no resolver special case or native widget
  hierarchy. Shared utilities live in `ui/core/`; component recipes live in
  `ui/dtna/`.
- **S12.1.1v2–S12.1.3:** constructors and presentation are functional;
  procedural author handlers perform DOM reads, focus changes and emission.
  Component state is per view instance. Application data remains with the
  enclosing author view, communicated through typed action envelopes.
- **S9.1.4/S9.1.7:** there is no mutable captured or process-global theme store.
  Visual configuration inherits through scoped CSS variables; native fieldset
  rules provide disabled inheritance. Nonvisual provider context remains open.
- **S10.1.1v3:** singleton text unions in template attribute predicates must
  match their branches; they must not become presence-only constraints.
- **D8.1.1v17:** package model/contract fixtures run on `interp`, `auto`, and
  `jit`. Native interaction is tested separately on forced `interp` and `jit`;
  pure tier parity alone does not certify handler behavior.
- **D4.5.2/D5.3.3:** no package-owned native registry retains raw GC items.
  Existing template and DOM lifetime mechanisms retain component sources.

## Implemented modules

| Source | Responsibility |
|---|---|
| `lmd/package/ui/dtna.ls` | Public constructors, explicit prop contracts, page/render helpers |
| `ui/core/component.ls` | Descriptor/slot construction, attribute filtering, boolean omission, action records |
| `ui/core/collection.ls` | Unique keys, disabled-item navigation, sorting and pagination helpers |
| `ui/core/interaction.ls` | Scoped delegated event targets and keyboard focus |
| `ui/dtna/contract.ls` | Component-specific input validation |
| `ui/dtna/tokens.ls` | Validated seeds, derived palette, complete/scoped CSS variables |
| `ui/dtna/style.ls` | Scoped default-light recipes and native interaction states |
| `ui/dtna/alert.ls` | Status/banners, content/action/icon slots, native close and controlled retention |
| `ui/dtna/spin.ls` | Delayed and automatic loading frames, indicators, semantic parts and nested/fullscreen presentation |
| `ui/dtna/button.ls` | Button appearances, semantic parts, delayed loading, links and native activation |
| `ui/dtna/input.ls` | Native text/select/check/radio/switch views and typed events |
| `ui/dtna/navigation.ls` | Tabs, Segmented, flat Menu, Pagination, selectable Steps and roving focus |
| `ui/dtna/layout.ls` | Responsive Grid, Space/Flex slots, compact/button groups and labelled Divider |
| `ui/dtna/locale.ls` | Explicit immutable en-US/zh-CN Pagination messages |
| `ui/core/responsive.ls` | Shared breakpoint validation, inheritance, class/variable/rule generation |
| `ui/dtna/icons.ls` | Pinned licensed 848-variant SVG catalog, themes/colors/rotation and semantics |
| `ui/dtna/display.ls` | Badge/Ribbon and responsive Descriptions packing |
| `ui/dtna/progress.ls` | Line/steps/circle/dashboard progress and success segments |
| `ui/dtna/timeline.ls` | Direction, placement, labelled/reversed/pending items and markers |
| `ui/dtna/statistic.ls` | Exact decimal-text grouping and precision, formatter and loading |
| `ui/dtna/skeleton.ls` | Loading/content transition, text/avatar options and standalone shapes |
| `ui/dtna/surfaces.ls` | Card/meta/grid, shared Tabs, localized Empty and illustrated Result |
| `ui/dtna/typography.ls` | Semantic heading hierarchy, inline decorations and disabled/external links |
| `ui/dtna/general.ls` | Shell/display/feedback recipes and providers |
| `ui/dtna/rate.ls` | Full/half rating, hover/clear, controlled values, RTL/keyboard, form text and symbols |
| `ui/dtna/form.ls` | Native Form submission/reset and labelled Form Item |
| `ui/dtna/collapse.ls` | Multiple/accordion disclosure, retained content and keyboard headers |
| `ui/dtna/tree.ls` | Recursive model, disabled check barriers, selection and visible-node keyboard navigation |
| `ui/dtna/table.ls` | Semantic rich cells, local sort/filter/page, selection, details and summaries |

The package exports implemented constructors only. Placeholder exports are not
used to count planned components as implemented. The JSON-formatted inventory
is `test/ui/dtna_reference/catalog.manifest`; `.manifest` avoids the native UI
runner's rule that every JSON below `test/ui/` is a fixture with one owner.

The reference is AntD **6.6.5**, resolved commit
`4a39f54842eade4e565ab336ef6097cd7e723cdd`. The tag/commit and component catalog
were verified against upstream. The locked local application now captures nineteen
reference cases with source/lock/font/browser/PNG hashes and geometry. Native
correspondence remains an open gate. The HSV palette follows the published
parameter schedule; blue and purple shades have exact goldens. Icons now come
from the locked MIT-licensed `@ant-design/icons-svg` 4.6.0 catalog; the license,
all definition hashes and generated-asset hash are retained in the repository.

## Engine regressions found by package fixtures

1. **Text edits disappeared after a parent update.** `StateStore` excluded all
   regenerated text controls from migration. It now preserves canonical native
   state when tag/type/id and authored default are unchanged, while a changed
   default or identity resets it. Tests cover both cases and real typed text
   across parent updates. Native editing remains the sole text-state owner.
2. **Roving focus failed on `tabindex=-1` and lost keyboard focus indication.**
   The public programmatic-focus adapter routed through sequential-focus
   eligibility. It now uses the programmatic path, and reactive focus restore
   preserves the keyboard flag. State-store and native keyboard fixtures cover
   negative tabindex, focus restoration and selection.
3. **Union patterns intercepted unrelated dtna components.** Template matching
   treated singleton unions as attribute-presence checks. It now unwraps type
   declarations and recursively compares text-union branches. A focused golden
   covers matching values, unrelated values, symbols and absent attributes on
   all three execution tiers (**S10.1.1v3**).
4. **Native Form never reached a Lambda submit handler without a JS realm.**
   Submission returned early before the existing author event dispatcher.
   The no-JS branch now dispatches the ordinary cancellable author event.
   Native required-field validation, successful entries, retained edits and
   reset are exercised through actual button clicks.
5. **An unhandled document error crashed during runtime cleanup.** Both T0 and
   MIR reproduce SIGSEGV when rendering `test/ui/dtna_error_document.ls` with
   the pre-fix release binary. `Context::last_error` still pointed into the
   destroyed GC heap when final context cleanup inspected it. Teardown now
   clears this diagnostic mirror while the heap is alive, as heap reset already
   does. A permanent subprocess regression requires the render command to exit
   with load-rejection status 1 on both tiers (**D4.5.2/D5.3.3**).
6. **Named views lost all event handlers under forced MIR.** The function
   prepass named handlers from the authored view name while registration looked
   for the generated `_view_N_hM` name. Compilation now uses the generated name
   consistently; authored names continue to identify state. All four native
   fixtures are permanent forced-interpreter/forced-JIT checks in the focused
   target (**D8.1.1v17**).

Boolean HTML attributes must be absent when false: emitting a null-valued
`disabled`/`checked` attribute still has presence semantics in the Lambda DOM.
The shared descriptor helper centralizes that rule. Nested providers emit only
explicit overrides; emitting a full default token map incorrectly reset the
parent's primary color.

No vendored code, generated parsers, or formal rulings were changed.

## Collection and disclosure increment

Collapse adds controlled/default active keys, multiple or accordion expansion,
disabled headings, extra slots, eager retained content and keyboard focus.
Tabs adds opt-in `keep_mounted:true`; native input edits survive hiding and
reopening, and input arrows no longer select another tab. Shared event helpers
reject another same-kind component's bubbling targets.

Tree validates globally unique typed keys, flattens hierarchy with explicit
parent/level/sibling metadata and renders visible nodes. Check conduction stops
at disabled or checkbox-disabled nodes; eligible ancestors derive checked and
mixed states. Strict checking is independent. Selection, expansion, arrow
navigation, Home/End, Enter/Space and cyclic first-letter search have real
native fixtures. Table uses the shared key and pagination helpers for typed
cross-page selection and local filter/sort/page operations. Semantic table
cells accept pure render functions; caption, summary, row detail, empty/loading
and controlled requests are included. This is partial M2/M5 progress; overlays,
virtualization, asynchronous data and editable enterprise tables remain open.

Three engine regressions were reduced before fixing their causes:

1. **Regenerated native constraints stayed stale.** StateStore migrated old
   `disabled`/`readonly`/`required` bits over new markup, so a pagination button
   remained disabled after leaving page 1. Migration now reapplies authored
   constraints while retaining native edits. `dtna/constraints` fails on the
   pre-fix pinned host; a unit regression verifies both constraint directions
   and preservation of the native text buffer.
2. **Imported scalar propagation unboxed twice in MIR.** The cross-module
   boxed-call path unboxed a string before the `^` consumer checked its Item
   error tag, then the consumer unboxed the raw pointer again. It produced null
   where T0 returned `"multiple"`, hiding Table's select-page control. The import
   path now retains an Item for propagation/handlers, records its actual
   representation and roots that representation. `dtna_imported_outcomes`
   covers success, comparison and error handling across all three tiers
   (**S7.6.1v4/S7.6.3v2; D2.4.1–D2.4.3; D8.1.1v17**).
3. **Shrink-to-fit alignment moved flex children twice.** Deferred inline-line
   alignment treated the direct text and the sort-arrow span in an inline-flex
   button as separate flow lines, moving the arrow over its label. The shared
   deferred-line helper now excludes flex/grid containers, whose item algorithms
   already own those positions. The reduced native Ahem geometry fixture fails
   on the pre-fix host and passes after the fix; the gallery was rendered again.

Controlled null is recognized through `at` key membership (**S8.2.2v4**),
instead of the current `contains` implementation's non-null value test. Native
inspection also exposed blank numeric cells and wrapped 12/12 grid columns:
shared slot rendering now converts numeric/bool/symbol scalars to text, and
Row gutters use column padding rather than adding gaps to 100% of column
widths. Native text and geometry assertions cover both defects.

The new explorer is `test/ui/dtna_data_gallery.ls`. Its native PNG is
`temp/ui_dtna/collections_validation/temp/data_gallery_final.png` (1100 × 1000),
inspected for numeric/rich cells, selection, indentation, disabled appearance,
disclosure and two-column layout. This remains native inspection evidence;
the locked AntD reference application and pixel comparison are still open.

### Validation — collection and disclosure increment

The final focused sweep passes **11 package GTests**, including nine golden
scripts across interpreter/automatic/JIT modes (27 combinations) and native
render-error rejection; **15 StateStore tests**; and all nine native fixtures
on forced interpreter and JIT. Each native tier passes **128 assertions**:
constraints 5, controlled collections 13, controls 25, disclosure 13, form 5,
navigation 11, styles 16, Table 19 and Tree 21. Pointer, native keyboard
activation, focus, edits, controlled requests, disabled behavior and layout
geometry are exercised. Catalog validation and Radiant dimension lint pass.

The commands are the test stages of `make test-ui-dtna ARGS='--jobs 1'`, run
from `temp/ui_dtna/collections_validation/` against an isolated debug host.
`provenance.json` records the executable and engine/package source hashes;
`temp/collections_final_{pure,state,native_interp,native_jit}.log` and
`temp/collections_final_exits.txt` retain their results. Debug and release
hosts were linked directly into separate validation directories through the
generated Makefile's `TARGETDIR` override, without editing generated files.

`make test-lambda-baseline` passes **6546/6548**, including the dtna cases,
and fails `edit_view_only` (the existing math round-trip mismatch) and
`map_model` (an expected/actual boolean mismatch). The focused rerun on the
pinned host reproduces `edit_view_only`; `map_model` passes after the concurrent
map source changes. The original aggregate remains failed and its causes are
not assigned here.
The aggregate log is `temp/ui_dtna/collections_lambda_baseline_retry.log`;
the rerun is `collections_validation/temp/collections_aggregate_failures_rerun.log`.
An earlier attempt stopped at a concurrent `geomap.cpp` duplicate declaration.

`make test-radiant-baseline` stops during compilation of the concurrently
edited `test_map_gtest.cpp`: a `DomNode*` is used to access `first_child`.
The log is `temp/ui_dtna/collections_radiant_baseline.log`. This is not a
passing aggregate; unrelated sources, expectations and harnesses were not
changed to bypass it.

The isolated layout checks pass the recorded form baseline **378/378**
(365 fully passing and 13 recorded partial matches) and CSS display baseline
**145/145 fully passing**. Logs are
`temp/ui_dtna/collections_layout_validation/{form,display}.log`.

The final Test262 run is clean: **40261/40261 fully passing**, zero
non-fully-passing cases, lost/crashed batches, retries or regressions, exit 0.
It uses an isolated `release_native` host and the standard baseline/async
population with `--jobs=3` to reduce contention. The executable SHA-256 and
engine/harness/baseline source hashes are recorded in
`temp/ui_dtna/collections_release_validation/provenance.json`; the result is
`temp/collections_test262_final.log` in that directory. No tests, thresholds
or baseline expectations were changed.

The earlier `make test262-baseline` invocation suffered spawn failures while
concurrent builds replaced the shared host. An isolated attempt launched
before its release link finished is also invalid evidence. The first complete
isolated run used seven workers and recovered one slow Unicode-identifier
case only on retry; its exit 0 is not a clean gate. These attempt logs remain
separate from the final clean result.

## Validation and artifacts — initial increment

```bash
CCACHE_DIR="$PWD/temp/ccache" make test-ui-dtna ARGS='--jobs 1'
make test-lambda-baseline
make test-radiant-baseline
make test262-baseline
make lint ARGS='--rule ^no-int-cast-radiant$'
```

- `test/lambda/ui_dtna/`: five golden-driven contract/model scripts, including
  the singleton-union regression; permanent `UiDtnaTests` runs their 15 tier
  combinations.
- `test/ui/dtna/`: manifest-owned native controls, form, navigation and scoped
  style fixtures. The final focused run passes **51 assertions on each of
  forced interpreter and JIT**: controls 25, form 5, navigation 11, styles 10.
  This includes Select popup open/commit/Escape and typed values, Button
  Space/Enter activation, retained edits, inherited direction, and
  inherited/explicit icon sizes.
- `test/test_state_store_gtest.cpp`: authored-default migration and keyboard
  programmatic-focus regressions; the standalone runner uses explicit stubs
  for paths excluded from its link.
- `test/ui/dtna_gallery.ls`: broad native explorer. Captures and logs are under
  `temp/ui_dtna/`; screenshots are inspection evidence, not an AntD baseline.

The focused package runner passes all seven GTests, including the 15 tier
combinations and two render-error rejection cases. StateStore passes 14/14;
the Radiant dimension lint and catalog checker pass. All three public-guide
examples execute, and an absolute `LAMBDA_HOME` import from a foreign working
directory matches the token golden. The 1100 × 2400 native gallery was rendered
and visually inspected.

The final complete `make test-ui-dtna ARGS='--jobs 1'` invocation also passes,
exit 0; see `temp/ui_dtna/focused_target_final.log`. It runs the catalog check,
seven package GTests, 14 StateStore GTests and all four native fixtures on both
forced tiers.

Focused validation uses a pinned executable under
`temp/ui_dtna/validation/`; `binary.sha256` and `provenance.json` record its
identity and the source hashes. An initial test attempt was invalidated by a
concurrent build removing the shared `lambda.exe`; its missing-executable
failures are not package regressions. The rerun uses the pinned executable.

The resumed Lambda aggregate passes **6539/6541**, including the dtna cases,
and fails these two scripts; both failures reproduce in a focused rerun:

- `latex_test_latex_phase3_corpus`: exceeds the runner's 60-second timeout.
- `edit_view_only`: source export is unchanged, but the math round-trip check
  reports that block 1 would change. The failure also reproduces with the older
  release host and a copy of the HEAD math package; this does not establish its
  root cause. A smaller standalone round trip passes on all three tiers.

Logs are `temp/ui_dtna/lambda_baseline.log` and
`temp/ui_dtna/lambda_failures_rerun.log`. The checkout contains concurrent map
and math work; these failures remain unresolved and are not attributed to it.
A focused green result does not certify the aggregate gates.

The resumed Radiant gate exits 2. Native Scene3d passes 59/59, map tests 11/11,
and the selected view-reuse tests 368/368. Its later aggregate prints
1073/1139, but concurrent runs overwrite shared `temp/_radiant_*.log` and
result files: the printed WPT/DOM failures disagree with the subsequently
preserved passing logs, and Bootstrap's missing machine result passes in an
isolated rerun. This run is not authoritative aggregate evidence.

Other reported failures remain visible: math-intensive view/scroll cases,
indexed math-arrow SVG assertions, CSS cascade memory, and Doom audio/gameplay
probes (including timed-out processes). The indexed-arrow fixture reproduces
5/7 on the pinned host; it specifies exact SVG viewBox values. No unrelated
fixtures, memory budgets or expected results were changed to make the gate
green. Preserved logs are under `temp/ui_dtna/aggregate_evidence/`.

The first Test262 run exits 0 with 40259 fully passing baseline cases and two
Unicode-identifier cases recovered only in Phase 4. It is not a clean gate.
The standard baseline rerun is clean: **40261/40261 fully passing, zero
non-fully-passing cases, retries or regressions**, exit 0. The harness and test
sources were unchanged. The final log is `temp/ui_dtna/test262_final.log`; the
release binary hash is recorded in the validation provenance.

## Foundation and navigation increment — 2026-10-10

This increment implements outstanding M1 features without changing language
contracts (**D7.2.4**, **S12.1.3**). Grid now supports validated spans,
offsets/order, zero-span hiding, two-axis gutters and breakpoint maps through
`xxxl` (1920px). Viewport changes update the existing DOM. Space preserves
original child slots, omits nulls and inserts optional separators; slot wrappers
are necessary because adjacent strings otherwise merge (**S2.6.3/S2.6.4**).
Space.Compact and Button groups join borders horizontally or vertically,
support block sizing, and inherit disabled state through native fieldsets.
Divider adds start/center/end labels and dashed/plain variants.

Pagination adds controlled/default page size, a native size changer, Enter
quick jump with range clamping, bounded page controls and clickable ellipses.
Rendering inspects at most seven page candidates regardless of the total.
Size changes keep the old first visible item's containing page. Explicit
`en-US`/`zh-CN` messages are immutable component data, with no ambient locale
store (**S9.1.7**). Steps adds controlled/default current, selectable native
buttons, disabled items, four statuses, icons/subtitles, vertical/small layouts
and narrow-screen adaptation. Actions request controlled changes rather than
overriding authored props.

Three root causes in Radiant were exposed and fixed:

- Resize reflow changed layout geometry but retained the old CSS-engine
  viewport and winning media declarations. Layout initialization now updates
  the viewport and uses the shared full-document recascade, preserving
  current pseudo state, native edits and focus.
- Reversed flex positioning used physical left/top margins as main-start.
  The shared axis-reversal calculation now also chooses the correct physical
  margin and auto-margin sides for alignment, including RTL.
- Disabled fieldsets suppressed the immediate native click dispatcher but
  still entered deferred Lambda author dispatch through click defaults. The
  physical pointer path now skips both paths for disabled controls; the
  first-legend exemption remains intact. Script-created event dispatch is
  unchanged.

`test/ui/dtna/engine_contracts.ls` isolates these contracts using ordinary HTML,
without importing the package. Its assertions improve from **10/14** on the
pre-fix host to **14/14** on the updated host. It checks media rules in both
resize directions, retained input edits/focus, reversed-axis margins, disabled
descendant clicks and first-legend activation.

`CCACHE_DIR="$PWD/temp/ccache" make test-ui-dtna ARGS='--jobs 1'` passes:
**12 package GTests**, including ten golden scripts on all three tiers;
**15 StateStore tests**; and **201 native assertions on each of interpreter
and JIT**, across twelve fixtures (**D8.1.1v17**). New package goldens include
twenty foundation contract cases; new layout/navigation fixtures add 59 native
assertions. Catalog validation and Radiant dimension lint pass. Layout and
navigation screenshots were visually inspected; these are native inspection
artifacts, not AntD pixel comparisons.
The final quick-jump regression also rejects decimal, nonnumeric and zero
input without emitting a change; conversion alone can produce a decimal value
from a string, so the handler explicitly requires an integer page.
The final complete focused invocation is `focused_final.log`, exit 0.

Focused logs, the before/after engine result records and captures are under
`temp/ui_dtna/foundation_increment/`. All five public-guide examples execute
successfully. The inventory remains **0 complete, 37 partial, 36 planned**;
remaining feature requirements stay explicit.

The Radiant aggregate exits 2, reporting **4176 fully passing, 350 recorded
partial matches and three failures** across 4529 cases. Its preliminary
Scene3D, map and filtered view-reuse gates pass 59/59, 20/20 and 431/431.
All 3294 recorded layout baselines pass their existing thresholds; CSS cascade
memory, DOM integration, page snapshots and render baselines also pass.
The three failures are `doc_editor_indexed_math_arrows`,
`radiant_view_math_intensive_scroll`, and the view-command test of the same
LaTeX scroll fixture. Both underlying fixtures reproduce on the pre-change
host with the same results: **5/7** and **14/16** assertions. Their expectations
were not changed. The first sandboxed aggregate stalled in Scene3D after
macOS graphics-service errors and was interrupted; the completed retry ran
with system-service access. Aggregate logs and fixture results are preserved
in `foundation_increment/aggregate_evidence/` and the adjacent before/after
records. This is a failed aggregate, not a certified full gate.

The Lambda aggregate exits 2 with **6566/6569 passing**, including all dtna
cases. `math_test_math_html_output` and `edit_view_only` disagree with their
goldens; their outputs are byte-identical on the pre-change and updated hosts,
as recorded in `lambda_failures_comparison.json`. The third failure,
`latex_test_latex_phase3_corpus`, is absent from the aggregate batch results;
it passes the isolated GTest rerun, which takes 54.4 seconds for the script.
The original aggregate remains failed. Input baselines pass 2112/2112,
JS runtime tests 502/502 and MIR GC stress 301/301. No unrelated expectations,
timeouts or harness behavior were changed. Full Test262 was not rerun for this
increment; the previous clean result above is historical evidence only.

## Outstanding completion gates

M0 still needs a stable reconstructed/reordered-child identity contract,
nonvisual provider propagation, overlay logical ownership, native reference
correspondence, and agreement/implementation of native accessibility coverage. Existing HTML ids
do not constitute a language-level keyed reconciliation API. Retained source
items are the supported pattern in this increment.

Native text controls keep the sole edit buffer. A controlled package field
requests a change, then restores the latest committed value on the next
document frame; accepted edits retain selection. Unmount/remount state
preservation and collection identity remain open. Tabs mounts only its active panel unless eager retention is
requested; visited-only lazy retention remains open. Collapse eagerly retains
panels, while Tree children and Table row details unmount when hidden. Menu is
flat. Select is the
native single-select control. Page locale sets HTML `lang`; Pagination alone
has explicit en-US/zh-CN messages. Provider locale/size/motion defaults are
unimplemented.

Remaining M1–M7 features are listed individually in the inventory, including
advanced table/tree features, pickers/uploads, overlays and focus management, virtualization,
localization, motion, responsive fidelity, accessibility, static export
coverage and stress/performance gates. A name or idle-state screenshot is not
feature completion.

### Foundation/display continuation — 2026-10-10

This increment follows **D7.2.4/D7.2.5** (ordinary package imports and general
engine mechanisms), **S12.1.1v2–S12.1.3** (pure projections and procedural
interaction) and **D8.1.1v17** (tier parity). The §11.1 identity, lifecycle,
provider, overlay, async and native-accessibility proposals still await user
consultation; none is ratified by these implementation changes.

The pinned reference now has ten cases and records regular/bold/italic/bold
italic font faces, computed styles, source/lock/browser hashes, per-case
viewports and image hashes. `reference_capture_ten.log` records successful
capture and the composition interactions. Matching font files removed a
browser-synthesis mismatch; reference/native correspondence remains partial.
The five Empty/HTTP Result illustrations retain upstream MIT notices and
source/generated hashes, alongside the 848-variant icon catalog.

New package modules provide Skeleton shapes/content transitions, Card/meta/grid
and shared Tabs, localized Empty/illustrated Result, and semantic Typography.
Typography emits actual h1–h5 headings (default h1), semantic inline decorations
and disabled/external links. Absent link attributes are omitted: a present
Lambda null does not mean an absent HTML attribute. Progress supports sorted
linear gradients and masked conic ring gradients. A shared arc constructor
keeps rails, progress, masks and success segments aligned. Native pixels check
three ring colors rather than only the resource structure.

Pagination adds editable/read-only simple mode, small controls, localized or
custom total ranges and hide-on-single-page. Its native input owns the draft;
Enter, blur and Up/Down commit through the existing page request path. Controlled
values remain authoritative. Pure tests cover empty/last-page bounds and invalid
options; native tests cover clamp, arrow/blur commits, controlled rejection,
read-only presentation, sizing and changing totals.

Engine root causes found by the fixtures:

- Intrinsic measurement reset `PositionProp` wholesale and discarded already
  laid-out absolute-child links. ComputeSize now resets the CSS prefix while
  retaining the runtime links/static/sticky suffix and authored z-index cache.
  The stretched pseudo fixture improved from 7/9 to 9/9 pixel checks; Timeline
  rails have vertical/horizontal pixel checks.
- Mouse targeting correctly chose negative-tabindex tabs, but focus assignment
  incorrectly applied sequential eligibility. It now calls the existing
  programmatic-focus helper; clicking Card tab B and then Home preserves focus
  and changes its panel (12/12 surface assertions).
- Zero-blur outer shadows filled transparent square interiors. Hard shadows now
  use the existing isolated shadow compositor; square/rounded/transformed
  checks improved from 4/6 to 6/6.
- Automation parsed only the first selector before a comma. It now uses the DOM
  selector-list parser and group matcher, so grouped assertions count the whole
  list without duplicate matches.
- Retained Lambda SVG gradients lost numeric coordinates/stops through a
  string-only read. Resource inheritance and offsets now reuse the existing
  numeric SVG reader. `svg_numeric_retained_before.log` has 2/6 passing checks
  on the saved pre-fix binary; `svg_numeric_retained_after.log` has 6/6. The
  non-retained HTML serialization path already passed and was insufficient to
  reproduce this defect.
- Flex and grid final-content passes bypassed foreignObject HTML layout.
  Both now invoke the existing SVG formatting-context helper; grid avoids
  treating SVG children as normal flow. The independent flex/grid fixture
  improved from 0/4 to 4/4 geometry/paint checks. Conic Progress then painted
  correctly through the ordinary foreignObject/mask path.

`pagination_modes_focused.log` records the complete focused gate: 20 package
GTests (including 18 scripts across interp/auto/jit), 15 StateStore tests, and
28 native fixtures with 321 assertions per tier, on interp and MIR. All pass.
The dimension lint and `git diff --check` pass. These are correctness checks,
not debug-build performance measurements.

The last completed Radiant aggregate before the numeric SVG/foreignObject
changes is `selector_shadow_radiant_baseline.log`: exit 2, 4,189 passing,
350 recorded partials and three failures in two previously identified math
fixtures (indexed arrows and intensive-scroll, the latter reused by a command
check). All 3,294 recorded layout thresholds and the snapshot gate pass;
render visual records 206/212 passing plus the expected dispositions. This
remains a failed aggregate. A fresh aggregate is required for the later SVG
changes. The catalog remains 0 complete, 37 partial and 36 planned; M0–M3
have not been certified.

### Responsive Grid and controlled controls — 2026-10-10

Grid now supports logical push/pull, numeric/fixed/shorthand flex, responsive
flex/alignment/justification and CSS-length gutters. Space, Flex, Skeleton and
Icon reuse one validated length helper; invalid declarations and nonfinite
numeric text are diagnosed. A base Col flex takes precedence over responsive
flex, matching the pinned reference. The eleventh reference case records
500/800/1100/500px viewport states. `check_grid_geometry.py` compares all 32
native geometry checks with the reference within 0.2px, including RTL and
nested gutters. The Grid focused gate passes 21 package GTests, 15 StateStore
tests and 353 assertions per native tier; logs are in
`temp/ui_dtna/grid_increment/`.

Input, Text Area, Checkbox, Radio, Switch and native Select now treat supplied
value/checked props as committed state. Their procedural handlers request a
change and schedule the existing document frame mechanism; after the author
handles that request, the frame synchronizes the native control with the
latest prop. The native control remains the sole edit owner. Matching accepted
text is left intact, preserving caret/selection and composition. Named radio
peers restore from their projected committed checked props. This uses existing
DOM/frame facilities (**D7.2.5; S12.1.1v2–S12.1.3**), without settling the pending
general identity/provider/async proposals. The 40-assertion native fixture
covers accepted/rejected changes, typed Select values, radio arrows, Space,
middle edits and IME commit/cancel.

Two native root causes were reduced independently:

- A blurred text control could remain the canonical selection owner after its
  subtree was retired. The stale-owner pruning path now releases that selection
  before presentation refresh can dereference it; focus is checked before
  inspecting the selection owner's control type. The new StateStore regression
  fails before the fix and all 16 StateStore tests pass after it.
- Reactive focus capture omitted text-control IDs and textarea selections.
  When its render-map match disappeared, restoration selected the first input
  with the same class. Capture now retains the authored ID for all focused
  elements and selection for every text control; restoration uses the ID before
  anonymous class fallback. The package-independent `regenerated_focus` fixture
  improves from 3/15 to 15/15 assertions on the saved pre-fix/current hosts.

The latest completed Radiant aggregate before these two fixes is
`temp/ui_dtna/foreign_grid_radiant_baseline.log`: exit 2, 4,191 passes,
350 recorded partials and four failures. All 3,294 layout thresholds and the
page snapshot gate pass. Three failures are the previously recorded math
checks; `superlambda_smoke` fails on a syntax error in concurrently edited demo
code. Those independent demo changes are not modified by this work. A new
aggregate is required for the selection/focus changes. M0–M3 remain open.

`temp/ui_dtna/controlled_increment/focused.log` records the complete focused
gate after these fixes: exit 0, 21 package GTests (19 golden scripts on
interp/auto/jit plus two host checks), 16 StateStore tests, and all 31 native
fixtures with 408 assertions per tier on forced interpreter and JIT. The
catalog still records 0 complete, 37 partial and 36 planned entries.

### Scoped typography and aggregate follow-up — 2026-10-10

Heading sizes and line-box heights now derive from the pinned reference's
exponential, even-pixel font scale. Typography consumes inherited CSS variables;
changing only radius leaves the parent's heading scale intact. Body line height
derives from font size unless explicitly supplied. Nonfinite derived dimensions
are rejected. Constructors remain pure (**S12.1.1v2**); this extends existing
visual CSS inheritance and does not settle nonvisual provider context.

The typography golden now covers 20 contracts. The native scoped-token fixture
improves from 14/30 to 30/30 assertions, including nested inheritance, sibling
isolation, live font changes and retained native input value/focus/caret. All
30 expected assertions agree with measured AntD data through
`check_typography_tokens.py`. Twelve reference cases now capture actual edit
values/focus/caret as well as geometry. Native/reference heading margins,
glyph pixels and input widths are still separate open correspondence work.

The controlled-controls Radiant aggregate completed with exit 2: 4,194 passes,
350 recorded partials and six failures. Besides the three previously recorded
math failures, it exposed two Todo editing crashes and a percentage
letter-spacing regression. Their causes were investigated before continuing:

- Selection retirement cleared the owner, but presentation refresh returned
  early when no document selection existed, leaving an old caret visible with
  no target. The shared empty-selection path now clears that presentation and
  requests repaint only when presentation changed. The reduced StateStore test
  fails before the correction; all 16 tests and both original Todo fixtures pass
  after it. The existing idle-document repaint test also passes.
- A concurrently added typed CSS validator rejected percentage letter-spacing
  that the renderer already supports. Three current runs each failed
  `letter-spacing-percent-001`; three saved-host runs all passed. Validation now
  retains percentage and length-percentage tracking while rejecting invalid
  types, as required by [CSS Text 4 §8.2](https://www.w3.org/TR/css-text-4/#letter-spacing-property).
  All 103 CSS parser tests and the 980-case text baseline pass after the fix
  (751 fully passing and 229 recorded partials). FO-specific admission rules
  and the independent paged-media implementation are unchanged.

Evidence is retained under `temp/ui_dtna/controlled_increment/` and
`temp/ui_dtna/typography_tokens_increment/`. These correctness checks do not
certify release performance, native accessibility, or the still-open M0–M3
contracts and features.

`typography_tokens_increment/focused.log` records exit 0: 21 package GTests,
16 StateStore tests and all 32 native fixtures on forced interpreter and JIT,
with 438 assertions per tier (**D8.1.1v17**). Catalog validation, both reference
comparison scripts and `git diff --check` also pass. The fresh full Radiant run
in `typography_tokens_increment/radiant_baseline.log` completes with exit 2:
4,553 checks, 4,200 passes, 350 recorded partials and three failures. Only the
previously recorded indexed-math-arrows and math-intensive-scroll cases fail
(the latter is also exercised by a command test). All 3,294 layout thresholds,
both Todo editing cases, the Superlambda smoke case and the page snapshot gate
pass. This remains a failed aggregate and does not certify M0–M3.

### Segmented variants and repeated markup — 2026-10-10

Segmented now projects small/middle/large metrics, full-width and vertical
groups, round shape, icon/label slots and named native form entries. Native
buttons retain disabled and activation behavior; procedural choice handlers
skip disabled options, support arrows/Home/End and suppress an unchanged
selection (**S12.1.1v2–S12.1.3**). Controlled props remain committed values.
The golden covers 16 contracts on interpreter and JIT. Fourteen geometry
oracles agree with the pinned browser reference within 0.2px.

The native fixture records 41/42 assertions passing. Reusing one ordinary SVG
value in eighteen icon slots leaves only its final native occurrence. The
package-independent two-slot reproducer also fails, with 1/2 passing. The
embedded-node reuse/detach path in DOM rebuilding is the cause; **UI-1** in the
central ledger tracks it. The repeated-presentation consultation remains
pending under **RS7/RSO1/RSO2/RSO10** and proposal §11. No component cloning
workaround or unreviewed identity rule is introduced.

The pinned reference's radios submit `choice:on` because they omit native value
attributes. dtna's explicit hidden field submits `choice:3`, while `ui_change`
retains the integer `3`. That native-form translation is recorded separately
from the matching selection/controlled/disabled interactions. Thirteen reference
cases passed before the Avatar increment; logs are in
`temp/ui_dtna/segmented_increment/`.

### Avatar variants and computed corner radii — 2026-10-10

Avatar now accepts preset, numeric and responsive sizes; circle/square shapes;
text/icon/custom-source slots; and native image attributes. Missing responsive
entries restore default metrics, as independently observed in pinned AntD.
Twenty constructor contracts pass on both tiers; the native fixture passes all
26 assertions, including six viewport states. The browser checker verifies 23
size/font/corner oracles. The reference now contains fifteen captured cases.
The Avatar pixel checker validates source/lock/font/PNG hashes and runs the
existing native `assert_snapshot` gate on interpreter and JIT, with a 0.1%
mismatch bound, identical 500×600 dimensions and no masks. Both pass, including
a rerun after the subsequent flex fix. Generated artifacts stay under `temp/`.
This is fixture correspondence on macOS, not complete Avatar coverage.

The initial native failures traced to missing CSSOM corner accessors. The
shared property table now serializes physical/logical corners and compressed
elliptical shorthands. A computed corner snapshot retains percentages and
length expressions independently of paint overlap constraints
(**D4.5.1v4**); temporary serialization storage is freed after each query.
The standalone CSSOM fixture passes 22 assertions, and both new focused unit
tests pass. The rebuilt CSS animation suite passes 96 tests; vector passes
112 and StateStore passes 16. Logs are in `temp/ui_dtna/avatar_increment/`.

The full test build exposed missing dependencies in four deliberately isolated
harnesses. A shared test-only seam supplies fail-fast native layout/editing
hooks rather than fabricated geometry or values. Custom layout passes 12
and retained display lists pass 30. DOM-range synthetic nodes now receive
canonical IDs/registry records and release registry roots before external
storage; all 72 tests pass. Source-position bridge still has one existing
ownership failure, `ReusedResultRefreshesDirectOwner` (29/30 pass), which remains
visible. These focused results do not close the required aggregate gates.

### Tag variants, selection and flex line metrics — 2026-10-10

Tag now renders inline filled/outlined/solid variants, thirteen preset colors,
semantic token colors, custom hex backgrounds, icons, links and disabled close
controls. `checkable_tag` exposes boolean controlled/uncontrolled selection.
Native buttons supply Enter/Space activation and inherited fieldset disabling
(**D7.2.5**). A controlled `visible:true` owner can decline a close request;
custom notifications request changes without mutating application props
(**S12.1.1v2–S12.1.3; S8.2.2v5**). No synchronous custom-event return contract
is assumed. Non-hex custom filled/outlined colors are diagnosed, not ignored.

The golden covers 22 contracts on interpreter and JIT. All 38 native assertions
pass on each tier; 16 geometry/color oracles and actual selection/close
observations agree with pinned AntD. The reference uses `componentDisabled`
for its provider and records cancellation through upstream `onClose`;
dtna uses controlled visibility for that purpose. Pixel coverage, semantic
parts, group selection and motion remain outstanding.

Icon/close variants exposed stale parent font metrics in final flex-item
content layout. Flex now calls the existing block-font-metric helper already
used by Grid before initializing its line boxes, and records its own container
font. No widget-specific engine rule was added. The standalone
`flex_strut_contracts` fixture improves from 2/8 to 4/8 assertions passing with
the saved before/after binaries. Its remaining two small-font items retain a
42px measured height despite one-line final content; pinned Chromium measures
22px at both viewports. **UI-2** tracks that distinct sizing defect. The
fixture's expected values remain unchanged. The fresh aggregate run and logs
are under `temp/ui_dtna/tag_increment/`; this increment does not certify M0–M3.

### Rate, hover boundaries and fractional line metrics — 2026-10-10

Rate now supports finite full/half values, count, sizes, custom string/element/
function symbols, tooltips as native titles, clearing, readonly/disabled state,
RTL arrows and named form entries. Controlled values remain committed props;
`ui_change` requests retain numeric values and hover previews are separate
notifications (**S12.1.1v2–S12.1.3; S8.2.2v5**). Native buttons supply Enter/Space
activation (**D7.2.5**). `keyboard:false` disables arrow adjustment while retaining
button activation, matching the pinned reference. Shared Tooltip integration,
semantic parts, motion, native accessibility and full pixel coverage remain open.

The golden passes 22 contracts on interpreter and JIT, and the native fixture
passes 70 assertions per tier (**D8.1.1v17**). Twenty-two geometry/font oracles
and thirteen committed interactions agree with pinned AntD. The reference
contains sixteen cases at this increment; readonly is translated to upstream
disabled behavior. Reusing an authored immutable symbol still depends on the
unresolved repeated-presentation contract and **UI-1**; default symbols are
constructed separately rather than cloning authored markup.

Rate exposed general engine defects that are fixed at their shared sources:

- Native hover synthesis now snapshots and pins ancestor paths, sends boundary
  events in ancestor order, and roots `relatedTarget` before event allocation
  (**D4.5.2; D5.3.3**). Mouse enter/leave events have non-bubbling,
  non-cancelable, non-composed flags. A settled template rebuild re-hits the
  host viewport to restore the physical hover source; it does not invent a
  logical identity rule. The standalone boundary fixture passes six assertions
  and a forced-GC event-factory regression passes. Generation follows the
  [UI Events native mouse-move algorithm](https://w3c.github.io/uievents/event-algo.html#handle-native-mouse-move).
- Shifted inline atomic boxes no longer contribute their unshifted ascent a
  second time. Flex flow-label buttons use content layout for cross sizing,
  retain fractional changes, and measure outer boxes instead of glyph-ink
  overflow. The shared explicit-line-height helper follows Blink's floor of
  the ascent half of leading, with the remainder below the baseline
  ([Blink `CalculateLeadingSpace`](https://raw.githubusercontent.com/chromium/chromium/main/third_party/blink/renderer/core/layout/inline/line_utils.cc)).
  Independent 15px/20px/25px, raised-icon and glyph-overflow fixtures pass all
  sixteen assertions across two viewport sizes; the browser probe records
  matching measurements with pinned font/browser hashes.
- Column flex measurement now converts measured content width into a border-box
  budget before the existing decoration subtraction. This closes **UI-2-R**
  in the fixed ledger: the unchanged mixed-font fixture passes all eight
  assertions, including both formerly wrapped small-font cases.

The full test build passes. The fresh package gate passes 25 GTests, CSS
animation 96, vector 112, StateStore 16 and JavaScript script tests 199. Native
dtna passes 41/42 GTests on each tier; the sole failure remains **UI-1** in
Segmented. Avatar's pinned pixel gate still passes both tiers after these
layout changes. Logs are under `temp/ui_dtna/rate_increment/`; earlier aggregate
runs under `tag_increment/` are historical and do not validate the final
engine changes. The render runner in that earlier run printed its complete
accepted baseline report but hung during Node/V8 shutdown; its sampled stack
and explicit termination are retained there. Required aggregate reruns and
the remaining M0–M3 acceptance work stay open.

### Alert variants, semantic slots and live colors — 2026-10-10

Alert now has four statuses, outlined/filled variants, banner defaults,
native/custom icons, title/message/description/action/child slots, custom native
close controls and controlled/uncontrolled visibility. Explicit title presence
wins over the compatibility message, including null (**S8.2.2v5**). Close requests
carry `value:false`; a controlled owner may retain `visible:true`
(**S12.1.1v2–S12.1.3**). Native buttons provide Enter/Space (**D7.2.5**).
Validated `class_names`/`styles` maps address the seven upstream semantic parts.
Close motion, after-close notification, full pixel correspondence and the
separately gated M7 error boundary remain outstanding.

Nested alerts exposed reliance on HTML IDs in the shared delegation helper.
It now compares nearest component roots at the target and native dispatch
position. Anonymous nested alerts dismiss independently; this scopes physical
DOM events and does not settle the pending logical identity contract. Existing
Menu/Tabs/Pagination/Steps/Collapse/Tree/Table delegation fixtures still pass.

The CSSOM property table lacked the `border-color` shorthand. It now serializes
its metadata-defined physical longhands through their existing accessors and
shares the corner-radius compression helper. A synchronous style mutation also
exposed a stale `currentColor` read from the committed paint snapshot. Declaration
serialization now resolves live color without consuming pending geometry;
`color:currentColor` follows inheritance. Computed/paint separation remains
**D4.5.1v4**. The new unit regression initializes the same property metadata as a
loaded document; all 97 CSS animation/CSSOM tests pass. The standalone native
fixture passes nine assertions, including inheritance and a handler-time read.

Alert's golden passes 29 contracts on both tiers, and its native fixture passes
43 assertions per tier. Twenty-one geometry/color oracles and six real upstream
action/close observations pass against the seventeenth pinned reference case.
Controlled retention is recorded separately because upstream Alert closes
unconditionally. The full test build, 26 package GTests, 199 JavaScript tests,
112 vector tests and 16 StateStore tests pass. Native dtna passes 43/44 GTests
on both tiers; **UI-1** remains the sole failure. All seven browser comparison
scripts, catalog validation, dimension lint and `git diff --check` pass. Logs
are under `temp/ui_dtna/alert_increment/`. Full aggregate results are recorded
after their runs; these focused results do not certify M0–M3 completion.

The Alert aggregate rerun finished: Lambda/input **6582/6585**, with the same
`latex_phase3_corpus`, `math_html_output` and `edit_view_only` failures. Radiant
recorded **4210 passing, 350 partial and 4 failing of 4564**: **UI-1** and three
math-intensive editor/view cases. All 3294 layout thresholds and all 211 accepted
render baselines passed; this render runner exited normally. Facatology's batch
snapshot used narrower text and fell to 17.1% element/8.2% text correspondence.
Paired saved/current runs with the same font cache did not reproduce that width
change: both loaded Itim; current correspondence was 92.1%/95.9%. The saved
aggregate anomaly stays visible as an unresolved batch/font investigation,
rather than being removed from the report. Logs are in `alert_increment/` and
`facatology_probe/`. These counts precede the Spin cascade fixes below.

### Spin loading frames and generated pseudo cascades — 2026-10-10

Spin now supports three indicator sizes, explicit spinning/delay, descriptions,
nested content, a fixed fullscreen mask, custom content/functions, manual/auto
percentages and five semantic parts. Size affects the indicator alone. Pure
projection reads explicit state (**S12.1.1v2–S12.1.3**); document-owned named
frames implement delay, staged automatic progress, cancellation and removed-owner
cleanup (**D7.5.3**). The fixture retains action models explicitly, consistent
with the M0 prototype limit; it does not prove general keyed composition.
Fullscreens block pointers and retain the reference's keyboard policy without
inventing focus containment or dismissal. Native accessibility remains open.

The golden passes 24 contracts on T0 and MIR Direct (**D8.1.1v17**), and the
native fixture passes 41 assertions per tier. Ten initial geometry/style oracles
and real delay/content/removal/fullscreen observations agree with the eighteenth
pinned reference. Spin keyframes are paused at 0ms explicitly in the browser
capture. Automatic progress remains a live observation, so its PNG is not a
pixel-parity gate. Provider defaults, repeated authored indicators, full motion/
pixel/static-output correspondence and native assistive technology remain open.

Native clicks exposed two general cascade defects, recorded as **UI-3-R**.
Active-state recascade cleared generated boxes' borrowed pseudo styles and
matched those boxes as authored DOM. An inactive loading veil consequently lost
`pointer-events:none` between down/up and intercepted its own content. Both
stylesheet traversal paths and clear visitors now preserve generated boxes;
layout rebinds their declarations from the originating element. Per-element
CSSOM recascade also replaces pseudo trees so vanished selector branches cannot
survive an ancestor class change. Existing retirement/borrow ownership keeps
the committed snapshot alive until rebind (**D4.5.1v4**).

The property table now exposes inherited/live `pointer-events` through the
shared declaration serializer. The independent HTML fixture passes ten
assertions, including synchronous pseudo reads and reversed loading owners.
The reduced complete-stylesheet probe changed from 0/2 to 2/2 click assertions;
adding an unrelated `:active` rule was sufficient to reproduce the defect.
Focused and aggregate checks after these engine changes are recorded when
finished under `temp/ui_dtna/spin_increment/`; prior aggregate counts do not
validate the new cascade changes. M0–M3 remain incomplete.

The rebuilt focused gates pass: 27 package GTests (25 discovered goldens plus
two host/tier contracts), 97 CSS animation/CSSOM, 199 JavaScript, 112 vector
and 16 StateStore tests. Native dtna remains 45/46 GTests on both tiers, with
only **UI-1** failing. The pointer-event fixture passes all ten assertions on
both tiers. Avatar's unmasked 0.1% pixel bound still passes on both tiers.
Catalog validation, dimension lint and diff whitespace checks pass.

### Template invocation provenance — 2026-10-10

The existing source-position regression incorrectly retained an earlier owner
when an independent template invocation returned the same result. Wrapper
composition requires a nested application, as established by reactive UI §7.8
and DOM dispatch **ES27**; equal result identity alone cannot prove nesting.
The context now numbers invocation boundaries and reverse records. Initial
`apply` and retransformation link wrappers only when the inner result was
recorded during that body. Direct records refresh the root owner. Both `apply`
forms share the invocation/record helper; no new presentation-key contract is
introduced (**S12.1.3**).

All 33 source-position tests pass, replacing the previous 29/30 result. New
regressions distinguish independent calls, nested three-owner chains (including
one recursively selected template reference), and stale versus current inner
ownership during retransformation. The rebuilt 27 package tests pass, and the
native interpreter suite retains the same sole **UI-1** failure. Required
reactive/Lambda/Radiant/Test262 aggregate gates are running; their results belong
below when complete. Logs remain under `temp/ui_dtna/spin_increment/`.

Those aggregate gates finished before the following Button increment. Reactive
UI passed its six GTests. Lambda/input passed **6583/6586**; its failures remain
`latex_phase3_corpus`, `math_html_output` and `edit_view_only`. Radiant recorded
**4212 passing, 350 partial and four failing of 4566**, retaining **UI-1** and
the same three math-intensive editor/view failures. All 3294 layout thresholds
and all 211 accepted render baselines passed. The page snapshot averaged
92.1% element/92.5% text correspondence; the earlier Facatology batch anomaly
did not recur. Test262's release gate passed all **40,261** baseline entries
(2652 skipped), with no regressions. These runs validate the cascade and
invocation-provenance changes; they do not cover the subsequent Button edits.

### Button loading, semantic slots and native form continuations — 2026-10-10

Button now owns its constructors, view, handlers and appearance rules in
`ui/dtna/button.ls`. Five appearances, three sizes, circle/round/square shapes,
start/end icons, ghost/danger/block, anchors and root/icon/content overrides
have explicit contracts. Literal two-character Chinese labels insert spacing
unless disabled by the prop, an icon or text/link appearance. Full nested-label
parity remains open. Shared palette derivation supplies semantic hover/active
colors and scoped seed overrides without copied color algorithms.

Loading accepts a boolean or `{delay,icon}`. Pure projection observes explicit
state (**S12.1.1v2–S12.1.3**); document-owned named frames handle delay changes,
cancellation and removed owners (**D7.5.3**). Activation remains available
during the delay, matching the pinned reference, and stops when loading is
shown. Native `type` enums are serialized as text for both Button and Input;
symbol-valued HTML attributes had bypassed native submit/reset/input dispatch.

Button's native fixture passes all 64 assertions on T0 and MIR Direct
(**D8.1.1v17**). The nineteenth pinned reference supplies 34 initial
geometry/style oracles, semantic-part observations and actual keyboard,
loading, removal and form actions. All nine reference comparison scripts pass
against the fresh capture. Full pixels, loading/wave motion, modern color and
variant props, component tokens, provider/group defaults and native assistive
technology remain open; Button remains partial.

The independent eight-assertion `form_activation_rebind` fixture exposed two
event-continuation defects, recorded as **UI-4-R**. Author-only dispatch settled
a parent redraw without moving the physical default-action target onto the
replacement tree. A reset handler could also settle a nested redraw while the
outer UA handler still needed its form pointer. The event seam now propagates
the replacement target (including removal), and retransformation waits for the
active procedural handler to unwind. Existing reactive dispatch **ES25/ES26**
governs this ordering; no new presentation-identity contract is introduced.
The independent fixture passes all eight assertions on both tiers, including
typed edits, nested text hits, submit cancellation, reset and keyboard activation.

Final package/native and aggregate validation limits are recorded below.
Logs are under `temp/ui_dtna/button_increment/`. M0–M3 remain incomplete;
the consultation items in proposal §11.1 and the repeated authored SVG issue
**UI-1** remain open. The user's wrap-up request ends work after this increment.

The final focused run passes 28 package GTests, including all 36 Button golden
contracts across interpreter/auto/JIT, all 33 source-position tests and all
16 StateStore tests. Native dtna passes **47/48 GTests on each tier**; its sole
failure is still **UI-1** (Segmented 41/42 assertions). Avatar's unmasked 0.1%
pixel bound passes on both tiers. All nine reference comparison scripts,
catalog validation, dimension lint and `git diff --check` pass.

The main debug build succeeds. The final full `make build-test` fails to link
several runners on `image_jpeg_exif_orientation_from_memory`, from the shared
checkout's separate JPEG-orientation changes in `radiant/surface.cpp` and
`lib/image.c`. Those changes were left untouched. Required aggregate gates
were therefore not rerun after the Button/event-continuation edits; the earlier
Spin/provenance aggregate results above remain dated evidence, not validation
of this final increment. Work is paused at the user's wrap-up request, with
M0–M3 and the listed contracts/features still open.

### Gallery Select/Checkbox overlap — 2026-10-10

**UI-5-R** fixes a shared Radiant sizing defect exposed by the gallery. Select's
120px border box sat in a 57px Space item because nested-flex intrinsic sizing
read raw native control metrics, bypassing CSS minimum/maximum constraints.
The wrapper now uses the existing constrained intrinsic-width contribution.
Native flex constraints use the shared border-box clamp as well, retaining
content-box padding/borders and capping the automatic minimum by the maximum
(CSS Flexbox §4.5/§9.9.3). No dtna stylesheet or spacing workaround is needed.

The reduced HTML fixture improves from 3/18 to **18/18 assertions**; the actual
gallery improves from 0/2 to **2/2**, including a real checkbox click. Both pass
on interpreter and MIR Direct (**D8.1.1v17**). Independent Chromium geometry
agrees with all 18 reduced oracles. Gallery Select remains at x=75, width=120;
the next label moves from x=140 to x=203, leaving the authored 8px gap.
The rebuilt executable, dimension lint, catalog and whitespace checks pass.
Logs, geometry and browser evidence are under `temp/ui_dtna/select_overlap/`.
This bug fix does not resume milestone work.
Isolated native dtna runs pass **47/48 fixtures (49/50 GTests)** on each tier;
their sole failure remains **UI-1**.

The required Radiant aggregate completed: **4219 passing, 350 partial, five
failing of 4574**. All **3294 layout thresholds** and **211 accepted render
baselines** passed. The retained failures are **UI-1**,
`doc_editor_indexed_math_arrows`,
`RadiantViewTest.LoadsMathIntensiveLatexAsHeadlessView` and
`radiant_view_math_intensive_scroll`. The fifth report was a runner artifact:
overlapping interpreter/JIT suite runs shared Rate's machine-result path.
Its original stdout passed 70/70 assertions, and an isolated interpreter rerun
with a unique result file passed **70/70**, including the machine result.
The aggregate's raw result remains a failure; the rerun does not rewrite it.

The additional Radiant unit sweep passed **927/931**. Four `StyleEpochTest`
cases fail CSS assertions before any layout call:
`OverlappingMutationRootsCascadeTheFinalTreeOnce`,
`ChildListOutsideStructuralAnchorsPreservesExistingCascade`,
`PositionAndEmptySelectorsRecascadeChangedParent` and
`SiblingRemovalRecascadesWhenPreviousAnchorIsDetached`. Their fixture creates
synthetic nodes through null backing and expects authored stylesheet matching;
these paths do not call the changed flex code. Those tests remain unchanged.

### Gallery pagination text alignment — 2026-10-10

**UI-6-R** fixes pagination numbers sitting above the center of their buttons.
The flex-item content path bypassed block finalization and its native button
label-centering step. That existing helper is now shared with flex-item layout,
after the used height settles; authored flex/grid alignment and the existing
vertical-writing/text-box-trim exclusions remain intact. No package CSS changes
or new formal rulings are needed.

The reduced HTML fixture passes **16/16 assertions** on interpreter and MIR
Direct (**D8.1.1v17**), matching independent Chromium observations. It covers
flow/row/column layout, small/tall/stretched buttons, authored flex/grid alignment
and resize relayout. The gallery PNG shows centered pagination labels. The
debug executable, dimension lint, catalog and whitespace checks pass. Evidence
is under `temp/ui_dtna/pagination_alignment/`. This bug fix does not resume
milestone work.

Gallery geometry confirms that all seven pagination labels move from a
**−3.5px** vertical center offset to **0px**, retaining 32px button heights.
The additional Radiant unit sweep again passes **927/931**, with the same four
pre-layout `StyleEpochTest` failures listed under the Select/Checkbox fix above.
Isolated native dtna runs pass **48/49 fixtures (50/51 GTests)** on each tier;
the sole retained failure is **UI-1**. Both the 64-assertion Button fixture and
the 18-assertion pagination interaction fixture pass on interpreter and MIR
Direct.

The required Radiant aggregate completes with **4221 passing, 350 partial and
four failing of 4575**. All **3294 layout thresholds** and **211 accepted render
baselines** pass, as do the page snapshot, DOM integration, vector, page-load,
CSS memory, fuzzy-crash and WPT gates. Its four retained failures are **UI-1**,
`doc_editor_indexed_math_arrows`,
`RadiantViewTest.LoadsMathIntensiveLatexAsHeadlessView` and
`radiant_view_math_intensive_scroll`. These match the genuine failures recorded
before this fix; the earlier overlapping-run Rate artifact does not recur.

### Gallery description text overlap — 2026-10-10

**UI-7-R** fixes the overlap between “Radiant” and “Theme” in the gallery.
Intrinsic grid sizing counted an unresolved variable template as one column
and used a separate approximation that omitted spans and flexible fractions.
It assigned 150.9px to the three-column grid and zero width to its first value.

Intrinsic width queries now activate the grid's block view before computed
style resolution, then reuse its existing placement and track-sizing pipeline.
The existing measurement scope restores geometry and the grid scope releases
scratch allocations. This implements the existing
[CSS Grid §12.1/§12.7](https://www.w3.org/TR/css-grid-2/#algo-flex-tracks)
algorithms; package styles and formal rulings are unchanged.

The shared measurement pass retains the existing grid-item minimum contribution
helper, including an authored zero minimum for a compressible native control.
Resolving child styles earlier also exposed an opposite-axis ratio bug: a
resolved zero `min-height` replaced an image's natural width. Transferred
minimum heights now floor the natural contribution, preserving natural size
when the minimum is smaller. The existing 776-case sizing baseline passes.

The reduced fixture passes **40/40 assertions**, matching Chromium, and the
actual gallery passes **4/4 assertions** on interpreter and MIR Direct
(**D8.1.1v17**), including resizing. Its description grid expands to 353.6px;
the first value receives its 48.9px text width, with the authored 16px space
before the next label. Three existing intrinsic/grid ownership unit contracts,
the debug build, dimension lint, catalog and whitespace checks pass. Evidence
is under `temp/ui_dtna/description_overlap/`. This fix does not resume milestone
work.

Six additional browser-derived assertions cover a range control in a nested
grid and image minima of zero or less than the natural height, before and
after resizing. All **50 geometry assertions** pass on interpreter and MIR
Direct. Isolated dtna runs pass **51/52 fixtures (53/54 GTests)** on each tier;
the sole retained failure is **UI-1**. The extended exploratory probe retains
two pre-existing final layout gaps: an explicit `width:min-content` control
grid and an image minimum larger than its natural height. Both reproduce with
the saved pre-fix binary; their evidence is retained as
`replaced_extended_probe.*` and
`replaced_before_*` in the evidence directory.

The additional Radiant unit sweep passes **927/931**, retaining the same four
pre-layout `StyleEpochTest` failures listed above. No unit expectations were
changed.

The render runner once stalled in Node/V8 shutdown after reporting all 211
accepted baselines passed. A process sample captured the main thread joining
a background baseline-compiler worker waiting for GC. The completed process
was terminated so the aggregate could continue; an unmodified standalone
rerun exits normally with all 211 baselines passing. The stack sample and
rerun log are retained in the evidence directory.

The final Radiant aggregate records **4224 passing, 350 partial and four
failing of 4578**. All **3294 recorded layout thresholds**, the page snapshot,
**211 accepted render baselines**, DOM integration and WPT gates pass. The four
failures remain **UI-1**, `doc_editor_indexed_math_arrows`,
`RadiantViewTest.LoadsMathIntensiveLatexAsHeadlessView` and
`radiant_view_math_intensive_scroll`, matching the pre-fix aggregate. The
final native gallery preview is `gallery_final.png` in the evidence directory.

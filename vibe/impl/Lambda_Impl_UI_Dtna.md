# Lambda UI dtna implementation

**Started:** 2026-10-09. **Product target:** native Radiant.
**Design:** [Lambda_Pkg_UI.md](../Lambda_Pkg_UI.md).
**Public guide:** [Lambda_UI.md](../../doc/Lambda_UI.md).

The implementation provides reusable component sources, a default-light
theme, native controls, flat navigation, disclosure and collection components,
and rendered component explorers.
The complete 73-entry Phase 1 remains the objective. The inventory currently
records **0 complete, 37 partial, 36 planned**; this increment does not close
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
| `ui/dtna/input.ls` | Native button/text/select/check/radio/switch views and typed events |
| `ui/dtna/navigation.ls` | Tabs, Segmented, flat Menu, Pagination and roving focus |
| `ui/dtna/general.ls` | Original SVG icons, layout/display/feedback recipes, providers |
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
were verified against upstream. A locked local reference application,
per-example source hashes, font environment and reference pixel captures are
still required before visual parity can be evaluated. The HSV palette follows
the published parameter schedule; blue and purple shades have exact goldens.
Icons are original minimal SVG paths, not a copied AntD icon distribution.

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

## Outstanding completion gates

M0 still needs a stable reconstructed/reordered-child identity contract,
nonvisual provider propagation, overlay logical ownership, a locked reference
application and captures, and a native accessibility audit. Existing HTML ids
do not constitute a language-level keyed reconciliation API. Retained source
items are the supported pattern in this increment.

Native text `value` is applied when the authored value changes; an unchanged
value does not force user edits back to its previous value. Full controlled
text semantics, unmount/remount state preservation and collection identity
remain open. Tabs mounts only its active panel unless eager retention is
requested; visited-only lazy retention remains open. Collapse eagerly retains
panels, while Tree children and Table row details unmount when hidden. Menu is
flat. Select is the
native single-select control. Locale currently sets HTML `lang` without
translating strings. Provider size/motion defaults are unimplemented.

Remaining M1–M7 features are listed individually in the inventory, including
advanced table/tree features, pickers/uploads, overlays and focus management, virtualization,
localization, motion, responsive fidelity, accessibility, static export
coverage and stress/performance gates. A name or idle-state screenshot is not
feature completion.

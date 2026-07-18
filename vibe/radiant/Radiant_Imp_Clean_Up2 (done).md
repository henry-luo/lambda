# Radiant Implementation Clean-Up 2 — Dead APIs and Legacy-State Convergence

**Status:** Implemented  
**Date:** 2026-07-14  
**Baseline:** `26aff3257`  
**Current maintained-source size:** 191,440 LOC under `radiant/` (`.c/.cpp/.h/.hpp/.mm`)

**Implementation completed:** 2026-07-15. All seven phases were implemented. The
owner resolved Phase 3 by retaining `rdt_vector_cg.mm` as excluded source for
future exploration and retiring WebDriver completely. The CoreGraphics decision
is an explicit exception to the proposal's compile-target recommendation: it
remains absent from generated builds, release binaries, and tests by design.

## 1. Purpose

The first Radiant clean-up campaign removed the large, ordinary duplication
families and recorded the major false positives. This follow-up targets what is
left in the current tree:

1. externally linked functions that have no in-tree caller and therefore evade
   `-Werror=unused-function`;
2. whole subsystems that are excluded from the shipping build;
3. legacy state representations that remain synchronized with a newer canonical
   model; and
4. the few residual clone families for which sharing still produces a net LOC
   reduction.

This is a source-maintenance proposal. Release binary size and release runtime
overhead are tracked separately from source LOC so diagnostic source is not
deleted merely to improve a line-count metric.

## 2. Executive summary

| Opportunity | Estimated source LOC saved | Risk / decision |
|---|---:|---|
| Zero-call function sweep | 800–950 | low–medium; verify external reachability |
| Canonical selection/projection convergence | 350–600 | high |
| Grid legacy/enhanced representation convergence | 250–450 | high |
| Residual mechanical dedup | less than 50 | low, low payoff |
| Delete dormant CoreGraphics backend | 1,332 | owner decision |
| Delete the dormant WebDriver subsystem | 2,496 | owner decision |
| Keep diagnostics but compile them out of release | no source-LOC claim | low |

The realistic source reduction is **about 1,400–2,000 LOC** without deleting
dormant product areas, or **about 5,200–5,800 LOC** if both dormant subsystems
are retired. That is approximately 2.7–3.0% of the current Radiant tree at the
upper end.

The important negative finding is that another broad helper-extraction sweep is
not worthwhile. The filtered Lizard scan reports only 6 unreviewed clone
families and 129 union duplicate lines. Their combined net saving is likely
below 50 LOC.

## 3. Diagnostics policy

Diagnostic source is allowed and should remain when it is useful, provided it
does not compile into the release build and does not perform diagnostic-only
work in release.

### 3.1 Current logging contract is correct

`lib/log.h` defines all of these as `((void)0)` when `NDEBUG` is defined:

- `log_debug(...)`
- `log_info(...)`
- `clog_debug(...)`
- `clog_info(...)`
- their `va_list` variants

The release configuration in `build_lambda_config.json` contains the
`DNDEBUG` build flag, which defines the `NDEBUG` preprocessor symbol. Because
stripping happens in the preprocessor, the call arguments are removed too;
they are not evaluated at runtime. The 764 `log_debug()` call sites in
`resolve_css_style.cpp` are therefore **not clean-up targets** and contribute
no release runtime or binary logging code.

`log_warn`, `log_error`, `log_notice`, and `log_fatal` intentionally remain in
release.

### 3.2 Diagnostic work outside the logging call must be guarded

`view_pool.cpp` currently constructs the complete human-readable view-tree
buffer before calling `log_debug()`. Although the logging calls disappear in a
release build, the traversal and string construction are outside those calls.

The text-dumper family (`print_inline_props`, `print_block_props`,
`print_view_block`, and `print_view_group`, roughly lines 821–1138) should be
enclosed in `#ifndef NDEBUG`, and its invocation in `print_view_tree()` should be
guarded by the same condition. The JSON dump must remain available because it
is the regression-test artifact.

This phase is a release-code reduction, not a source-LOC reduction. If the
human-readable dump is no longer useful even in debug builds, deleting it saves
roughly another 320–340 source LOC, but that is an optional owner decision.

### 3.3 Release diagnostic gate

After changing diagnostic code:

1. run `make release`;
2. verify diagnostic-only dumper symbols are absent from the release objects;
3. verify a representative diagnostic prefix is absent from the release binary;
4. run the normal debug build to confirm the diagnostic path remains usable.

No logging expression may own a side effect needed for correctness. Release
macro stripping would remove that side effect.

## 4. Caret and selection state: what is duplicated today?

Radiant does have multiple caret/selection representations, but they are not
simply two independent canonical selection engines.

### 4.1 Canonical document selection

`DomSelection` owns zero or one `DomRange`. A collapsed `DomSelection` is the
canonical document caret boundary. Its offsets use DOM semantics: UTF-16 code
units for text and child indexes for elements.

This is the model exposed to the JS Selection/Range APIs and used for rich or
document editing.

### 4.2 Canonical active-editing facade

`DocState::sel` is an `EditingSelection`. It describes the selection belonging
to the currently active editing surface:

- `EDIT_SEL_DOM_RANGE`: points at the canonical document `DomRange`;
- `EDIT_SEL_TEXT_CONTROL`: stores the active input/textarea plus UTF-16 start,
  end, and direction.

This facade is necessary because browser semantics allow a focused text control
to have its own selection while a separate document `DomSelection` still
exists. It does not represent a second document selection.

### 4.3 Legacy render/event projections

`DocState` also owns two private compatibility caches from
`state_store_internal.hpp`:

- `CaretState* caret`: view pointer, byte offset, line/column, geometry,
  visibility, blink state, iframe offset, and previous paint rectangle;
- `SelectionState* selection`: anchor/focus view pointers and byte offsets,
  pointer-drag state, geometry, and iframe offsets.

These are the legacy structures referred to by the first survey. They are
rebuilt by `state_store_refresh_caret_projection()` from `EditingSelection` and
`DomSelection`. They remain live because rendering, dirty-rectangle tracking,
event code, and debug accessors still consume view-space/byte-space projections.

### 4.4 Text-control mirrors

Text controls additionally mirror selection start/end/direction in
`FormControlProp` and `ViewState::data.form`. These mirrors serve control state,
relayout, and reactive-state persistence; they are not another general document
selection object.

The current shape is therefore:

```text
document/rich selection:  DomSelection -> DomRange
                                  |
active editing facade:       EditingSelection
                                  |
legacy presentation:       CaretState + SelectionState

text control selection:    EditingSelection
                              |          |
                       FormControlProp  ViewState mirror
```

There are two legitimate canonical selection domains—document and focused text
control—but there is also a duplicated legacy presentation layer. The proposed
LOC reduction removes the latter as independent storage, not the browser-required
distinction between document and text-control selection.

## 5. Root cause: exported dead functions evade the compiler gate

The build treats unused file-local functions as errors with
`-Werror=unused-function`. That does not diagnose non-`static` functions with
external linkage. Radiant accumulated declarations in coherent headers plus
matching definitions after their last caller was removed.

A current-tree lexical survey found **68 zero-call candidates totaling 757 body
LOC**. Removing their declarations and now-stale API comments raises the likely
net source saving to 800–950 LOC.

“Zero-call candidate” means the identifier occurs only in its definition, or in
its definition plus declaration, across `lambda/`, `radiant/`, and `test/` C/C++
sources. Before deletion, each candidate must still be checked for:

- generated call tables or macro-generated names;
- string-based lookup, `dlsym`, or host/JIT symbol exposure;
- externally supported library ABI; and
- platform-specific call sites not present in the active build.

### 5.1 Layout and intrinsic candidates

High-value bodies include:

| Function | Body LOC | Location |
|---|---:|---|
| `layout_measure_intrinsic` | 42 | `layout_measure.cpp` |
| `layout_flow_node_for_flex` | 38 | `layout_flex_measurement.cpp` |
| `block_context_position_float` | 35 | `block_context.cpp` |
| `compute_stretched_cross_size` | 28 | `layout_alignment.cpp` |
| `requires_content_measurement` | 25 | `layout_flex_measurement.cpp` |
| `measure_table_cell_intrinsic_widths` | 24 | `intrinsic_sizing.cpp` |
| `setup_flex_item_properties` | 16 | `layout_flex_measurement.cpp` |
| `measure_all_flex_children_content` | 13 | `layout_flex_measurement.cpp` |
| `get_item_flex_basis` + `_is_percent` | 20 | `layout_flex.cpp` |
| `estimate_text_width` | 10 | `layout_flex_measurement.cpp` |
| `compute_element_last_baseline` | 8 | `layout_alignment.cpp` |
| `form_control_{min,max}_content_width` | 16 | `layout_form.cpp` |

Smaller candidates include `cleanup_temporary_view`, `element_has_positioning`,
`layout_clamp_min_max_axis`, `grid_item_is_nested_container`,
`counter_pop_scope`, `layout_padding_border_axis`,
`get_measurement_cache_generation`, and `get_combine_text_nodes`.

Unused header-inline candidates include `intrinsic_sizes_width`,
`intrinsic_sizes_height`, `space_distribution_none`, `layout_axis_border_end`,
the three `layout_context_*` predicates, and `OriginZeroLine::as_i16`.

### 5.2 Event, state, and editing candidates

| Function/family | Body LOC | Location |
|---|---:|---|
| `form_control_set_value` | 45 | `state_store.cpp` |
| projection-to-DOM sync pair | 62 | `state_store.cpp` |
| `radiant_dispatch_form_text_ime_cancel` | 32 | `event.cpp` |
| `state_on_change` | 24 | `state_store.cpp` |
| `radiant_dispatch_event_sim_simple_event` | 21 | `event.cpp` |
| `state_has` | 18 | `state_store.cpp` |
| `radiant_state_dump_to_file` | 16 | `state_store.cpp` |
| `focus_within` | 15 | `state_store.cpp` |
| `event_state_log_warning` | 15 | `event_state_log.cpp` |
| unused DOM Selection aliases/methods | about 49 | `dom_range.cpp`, resolver |
| `te_select_word_at`, `te_select_line_at`, `te_select_all` | 18 | `text_edit.cpp` |

Other small candidates are `event_state_log_emit_raw`,
`clipboard_store_set_backend`, `visited_links_check`, `node_is_editable`, and
`radiant_dispatch_rich_composition_event`.

The projection-to-DOM sync pair is distinct from the larger projection
convergence campaign: it has no caller today and can be deleted immediately.

### 5.3 Render, shell, and ownership candidates

- `render_composite_copy_backdrop` and `render_composite_apply_blend`: 35 body
  LOC plus declarations;
- unused image-source retain/clear inline helpers;
- unused font-loading diagnostic wrapper functions;
- `keyframe_registry_destroy`;
- `free_view`;
- `window_main`, superseded by the Lambda CLI entry;
- `collect_inline_styles`, superseded by `collect_inline_styles_impl` and the
  live list wrapper;
- unused `is_text_input_type` and image-source ownership helpers.

## 6. Dead-code checker must be repaired first

`utils/lint/dead-code/run_unused_function.sh` currently returns a false clean
result when the custom Lambda Tree-sitter dylib referenced by `sgconfig.yml` is
absent:

1. `ast-grep` fails while loading the custom language;
2. stderr is redirected away;
3. the background pipeline produces an empty definition file; and
4. the script continues because it does not abort on the failed background
   job.

This is why the 68 externally linked candidates were not reported.

Fix the root cause before deleting functions:

- give the C/C++ dead-code scan a config that does not require the Lambda custom
  grammar, or make the grammar dependency explicit and mandatory;
- propagate a failed `ast-grep`/`jq` background job as a script failure;
- never interpret an empty definition set as success unless the scan explicitly
  completed; and
- add a fixture containing one dead external function plus one live external
  function so CI checks both detection and non-detection.

The checker remains lexical until USR verification is added, so findings stay
warnings requiring review rather than automatic deletion.

## 7. Dormant subsystem decisions

### 7.1 CoreGraphics vector backend — 1,332 LOC

`radiant/rdt_vector_cg.mm` is excluded in the global build configuration and in
the relevant target overlays. ThorVG is the documented current backend on all
platforms.

**Owner decision: retain as excluded exploration source.** The implementation
remains in the tree, its global and Jube exclusions remain in
`build_lambda_config.json`, and generated build files contain no reference to
it. Maintained design documentation identifies ThorVG as the sole active
backend and makes no support claim for the excluded CoreGraphics source.

### 7.2 WebDriver subsystem — 2,496 LOC

The previous clean-up listed only the excluded server and CLI. The current
reachability audit shows that all WebDriver implementation calls remain inside
`radiant/webdriver/`; no source outside that directory calls its session,
locator, action, or error APIs. The real server and command are excluded, while
the shipping command resolves to a stub.

| File | LOC |
|---|---:|
| `webdriver.hpp` | 348 |
| `webdriver_session.cpp` | 282 |
| `webdriver_locator.cpp` | 428 |
| `webdriver_actions.cpp` | 509 |
| `webdriver_errors.cpp` | 77 |
| `webdriver_server.cpp` | 723 |
| `cmd_webdriver.cpp` | 129 |
| **Total** | **2,496** |

**Owner decision: retire.** The complete implementation directory, command and
headless stubs, build configuration, tests, lint/dedup exceptions, RAD_23, and
its diagrams were deleted. Maintained indexes and architecture diagrams now end
at RAD_22 and expose no WebDriver command or component.

## 8. Structural convergence campaigns

### 8.1 Remove `CaretState` and `SelectionState` as independent storage

Target: make render/event/debug projection APIs derive their snapshots from
`EditingSelection`, `DomSelection`, `DomRange` layout cache, and the shared
editing animation fields without retaining a second mutable selection record.

The current projection-related functions and accessors span roughly 800 LOC,
but many APIs still need to exist. The expected net deletion is 350–600 LOC.

Sequence:

1. classify every `state->caret` and `state->selection` field as logical state,
   presentation geometry, animation state, or pointer-gesture state;
2. move presentation-only geometry/blink data into one narrowly named
   `SelectionPresentation` cache if recomputing it is impractical;
3. convert snapshot/accessor functions to read canonical boundaries directly;
4. migrate renderer, dirty tracking, event simulation, and invariant checks;
5. delete `state_store_refresh_caret_projection`, projection sequence state,
   the two structs, and their arena allocation; and
6. retain the document-vs-text-control distinction in `EditingSelection`.

Do not replace the two legacy structs with a third full copy. Any retained cache
must contain presentation data only and must not own anchor/focus truth.

Required tests include the full editor suites, Selection WPTs, text-control
selection tests, iframe caret geometry, drag selection, selection repaint, and
state-dump/event-sim snapshots.

### 8.2 Make enhanced grid tracks canonical

`GridContainerLayout` still owns legacy `GridTrack` arrays. Each pass converts
them into `EnhancedGridTrack`, performs placement/sizing, and copies sizes back.
Both representations carry base size and growth limit and can disagree during a
pass.

Target:

- store enhanced tracks directly in the grid layout pass;
- make legacy consumers read the enhanced representation or small read-only
  accessors;
- delete old-to-new sizing conversion, new-to-old largest-remainder copy, and
  duplicate computed-size state; and
- keep parsed `GridTrackSize` only as CSS input syntax, not as a second computed
  track model.

Expected net deletion: 250–450 LOC. Risk is high because placement, intrinsic
contribution, rounding, gaps, negative implicit tracks, and auto-fit masks all
cross the adapter seam.

## 9. Residual dedup: only take net-negative slices

The remaining unreviewed clone families are small:

1. SVG/PDF effect-raster fallback initialization;
2. repeated `RdtPath` append-command setup;
3. GIF/Lottie surface pixel detachment and generation bump;
4. bounded environment-integer parsing in `cmd_layout` and `script_runner`;
5. short inherited-text-property ancestor walks; and
6. small capacity-growth/signature shapes in `cmd_layout`.

Only implement a slice if the aggregate LOC gate is negative and the helper
expresses one real invariant. In particular:

- centralizing surface detachment is justified by the generation-bump
  invariant even if the LOC saving is small;
- a path-entry append helper is reasonable if it removes repeated capacity and
  count mutation;
- generic callback walkers for the text-property cases are likely negative ROI;
- do not couple unrelated modules merely to share a three-line `strtol` shape.

Expected combined saving: less than 50 LOC.

## 10. Phased implementation plan

### Phase 0 — Repair the reachability gate

- fix `run_unused_function.sh` failure propagation and C/C++ configuration;
- add live/dead external-function fixtures;
- record the reviewed zero-call candidate report.

No Radiant LOC target; this phase prevents recurrence.

### Phase 1 — Pure zero-call deletions

Delete candidates in small domain batches:

1. layout/intrinsic;
2. event/state/editing;
3. render/shell/header inline helpers.

Each batch must re-run repository-wide identifier search before deletion. If a
symbol is part of an intentional external ABI, mark it with a specific
`UNUSED_FUNCTION_OK` reason instead of inventing an internal caller.

Target: 800–950 LOC.

### Phase 2 — Release diagnostics contract

- compile the human view-tree dumper and its invocation only in debug builds;
- audit other diagnostic paths for expensive work outside stripped log calls;
- retain `resolve_css_style.cpp` debug logging;
- verify debug diagnostics and release symbol/string absence.

Source LOC target: none unless the owner elects to delete the text dumper.

### Phase 3 — Product decisions

Resolved independently: CoreGraphics source is deliberately retained but
excluded from compilation for future exploration, while the complete dormant
WebDriver surface is deleted.

Potential target: 3,828 LOC.

### Phase 4 — Selection projection convergence

Migrate one consumer class at a time, keeping canonical state stable. Delete
legacy projection storage only after renderer, event, debug, and validation
consumers are gone.

Target: 350–600 LOC.

### Phase 5 — Grid representation convergence

Make enhanced tracks canonical for one axis-neutral implementation, preserve
largest-remainder output behavior, then remove adapter conversions.

Target: 250–450 LOC.

### Phase 6 — Residual clone polish

Take only measured, low-risk, net-negative helper extractions.

Target: less than 50 LOC.

## 11. Verification gates

Every implementation phase must pass:

1. **LOC gate:** aggregate production LOC across all touched and newly added
   files strictly decreases for deletion/dedup phases;
2. **build gate:** `make build`;
3. **Radiant gate:** `make test-radiant-baseline`;
4. **layout gate:** `make layout suite=baseline` for layout/grid/render changes;
5. **editing gate:** the editor and Selection WPT suites for selection changes;
6. **lint gate:** `make lint`, including
   `make lint ARGS='--rule ^no-int-cast-radiant$'`; and
7. **release gate:** `make release` for diagnostic or reachability changes.

Deletion phases should also run focused tests for the owning domain. The build's
`-Werror=unused-function` remains useful for file-local fallout, while the
repaired dead-code scanner covers exported orphan APIs.

## 12. Non-targets

- Do not delete useful `log_debug`/`log_info` source merely to lower source LOC;
  they are already stripped from release.
- Do not merge backend-specific PaintIR/display-list switches that unpack
  different payloads or call different sinks.
- Do not merge policy-distinct flex baseline, hit-test, or text-extraction
  walkers already recorded as reviewed non-findings.
- Do not remove the document-selection versus text-control-selection distinction;
  it is required browser behavior.
- Do not remove `DomElement::native_element` as a line-count exercise. Its
  reallocation and anonymous-element semantics require a separate DOM ownership
  design and are unlikely to produce material net LOC savings by themselves.

## 13. Implementation conclusion

Phases 0–6 are complete. The repaired reachability gate reports no Radiant
candidate after the reviewed deletions and WebDriver retirement. Selection uses
canonical DOM/text-control boundaries plus geometry-only presentation state;
Grid uses one canonical computed-track representation; and the two measured
residual clone slices share their ownership/capacity invariants. Production
changes under `lambda/` and `radiant/` total 623 additions and 4,768 deletions,
for a net reduction of 4,145 lines while preserving the excluded CoreGraphics
source.

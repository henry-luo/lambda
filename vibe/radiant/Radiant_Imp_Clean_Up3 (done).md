# Radiant Implementation Clean-Up 3 — Structural LOC Reduction

**Status:** IMPLEMENTED 2026-07-19 — all Phase-R-validated, owner-approved slices are complete. Conditional spikes that failed their stated gates are closed without landing machinery; CU3-T5 and Class C remain excluded exactly as decided below.
**Date:** 2026-07-19
**Implementation baseline:** master @ `f2930eed7` — `radiant/` C/C+ sources = **188,310 physical lines across 162 files**. The original survey baseline was `d76b90f5e` (**187,503 physical lines / 146,782 NLOC / 160 files**). Between them, 30 scoped files changed by +1,108/−273, including `view_pool.cpp`, `event.cpp`, `layout_block.cpp`, `resolve_css_style.cpp`, `view.hpp`, and `css_prop_table.cpp`; therefore all call counts, line ranges, estimates, and test baselines below are hypotheses until Phase R refreshes them.
**Method:** four parallel code-survey agents (CSS resolution, layout, event/state/editing, render/tooling/shell) + a quantitative sweep (Lizard function census, logging/comment counts, subsystem grouping), followed by a 2026-07-19 implementation-plan review against current HEAD. That review corrected several overstatements (“dead,” “redundant,” and additive totals); Phase R is the final source-level revalidation before edits.
**Prior campaigns honored:** `Radiant_Impl_Clean_Up (done).md` (mechanical dedup, ~3.3–4.6k), `Radiant_Imp_Code_Dedup (done).md` (140→19 headers, ~2–2.5k), `Radiant_Imp_Clean_Up2 (done).md` (dead exports + WebDriver + selection/grid convergence, −4,145). All their §1.1 verified negatives were given to the survey agents as exclusions; none are intentionally re-proposed here.

### Implementation closure (2026-07-19)

Phase R pinned `f2930eed7` and measured **188,310 physical lines / 147,501
Lizard NLOC across 162 Radiant C/C+ files**. The duplicate scan reported 120
raw blocks, 27 after exclusions, 24 first-party clone families, and 471 union
duplicate lines. There were no `#if 0` blocks. `make check-radiant-dup` exposed
one stale exclusion for a previously deleted WebView helper; removing that
obsolete region repaired the ratchet without suppressing a live clone.

The zero-call refresh found four additional current-HEAD candidates omitted by
the survey: `clipboard_store_init`, `clipboard_store_write_mime`,
`css_font_family_is_available` (plus its exclusive helper subtree), and
`position_prop_init_defaults`. All four were deleted and the scanner is clean.

| Slice | Closure |
|---|---|
| CU3-T1 | **Implemented.** Deleted the human-readable view-tree formatter and side writes; `print_view_tree()` now delegates only to JSON. Current documentation was updated; the caret dump remains a separate simulator diagnostic. |
| CU3-T2 | **Implemented.** Removed 158 plain case-entry logs, added one ordinary-debug property-name/value-kind entry after logical remapping, and retained enriched logs. A focused debug run produced 50 centralized entries and retained enriched background-color context. |
| CU3-T3 / T4 | **Implemented.** Deleted `extract_inline_css`; removed only the truly unused disabled-selection total/casts. Selection variables still read by the dormant branch were retained. |
| CU3-E4 | **Implemented.** Deleted `te_ime_commit` and its declaration; `_prepare`/`_finish` remain separate around event injection. |
| CU3-L2 | **Partially implemented after revalidation.** Inlined and unexported sole-call `measure_text_run`. The traversal predicates are intentionally distinct (stopping and visibility policy), and the proposed grid wrapper was absent at this HEAD, so both were dropped. |
| CU3-L1 | **Implemented to the proven common boundary.** Promoted one aspect-ratio value normalizer and preferred-ratio resolver used by block, positioned, and absolute-child paths. Replaced-size constraint ordering remains context-specific, so no broad used-size core was forced. |
| CU3-E2 | **Dropped by its gate.** Untouched-baseline `make editor-4c` had 45 parity failures (25 `tier_0_drawing`, 20 `view`); no keymap change or rebaseline was made. |
| CU3-E3 | **Dropped after call-path inspection.** The alleged stubs are live: the non-rich X path copies and clears selection, while Backspace/Delete request repaint. |
| CU3-E1 + terseness follow-ons | **Registry spike rejected by the net-LOC gate.** Representative bodies move unchanged but require parse and execute callbacks plus a row per command. Current HEAD already has typed pointer/assertion parsers and `resolve_assert_element`; remaining lookalikes have different accepted-view/failure semantics. A registry or generic field-offset table is net-positive before behavior tests, so none was landed. |
| CU3-C1 | **Dropped by Phase R.** Current HEAD already centralizes regular keyword stores with `resolve_keyword_slot`; the remaining direct-looking rows have shorthand, inheritance, COW auxiliary flags, union allocation, clamp, remap, or allow-list policy. A write descriptor has too few pure consumers to offset its initializer/callback and tests, so it fails the plan's metadata/LOC gate. |
| Class B | **Spike rejected at the internal-equivalence gate.** A representative complex-flex trace showed preliminary cache records (`0×132`, content `0×100`) being applied while `calculate_item_intrinsic_sizes` was never invoked for those items. The old path is therefore not redundant with a second record stream; deleting it would introduce a new sizing policy rather than retire duplicate work. Fixture/performance deletion gates were not run after this prerequisite failed. |
| CU3-T5 / Class C | **Unchanged and excluded.** No owner reconfirmation or scope expansion was inferred. |

The retained Radiant diff is **−761 physical lines** (92 additions, 853
deletions) across the changed source/header set. `make build` and
`make test-radiant-baseline` pass; the latter reports 6,829 total tests, 6,286
passed, 543 partially passing, and no failures. The focused no-int-cast lint,
duplicate ratchet, Radiant unused-function scan, and diff check pass. Full lint
passes. A follow-up absorbed the pre-existing per-file
`radiant/resource_resolver.hpp` declaration into the consolidated `radiant.hpp`
shell API and removed that final header-allow-list violation.

---

## 1. Starting point: this tree is already tight

Three campaigns in five days removed ~10k LOC. At the survey baseline, the filtered Lizard duplicate rate was **1.27%** (was 9.12% on 2026-07-13). There were no `#if 0` blocks, no dormant subsystems left (WebDriver deleted; CoreGraphics deliberately retained as excluded source; graph layout already migrated to Lambda script), and the zero-call scanner reported no candidates. **Another broad dedup campaign would find scraps.** Phase R reruns these measurements at `f2930eed7`; this plan does not silently carry quantitative claims across the intervening view-reuse/lifecycle implementation.

What remains is a different problem. The mass is *concentrated*, not smeared:

- The **top 12 functions hold ~24k lines** — 13% of the tree. `resolve_css_property` alone is **5,974 lines** (one function = 3.2% of radiant), `measure_element_intrinsic_widths` 2,717, `handle_event` 2,661, `process_sim_event` 2,122, `layout_block_content` 1,946.
- **~9,000 LOC of test/QA infrastructure ships in the release binary** (event simulator, state-machine validation layer, state-dump band, view-tree text dumper).
- Physical-line composition: **18,819 blank (10.0%) + 22,003 comment-only (11.7%) + ~4,500–5,500 logging** (3,745 single-line `log_debug/log_info` starts + continuations) + ~141k executable.

So the levers are (a) a bounded set of deletion/refactoring candidates, (b) one spike-gated architectural retirement, and (c) **scope/relocation/migration decisions** — which is where the large numbers are, and which only the owner can green-light.

### Survey-baseline subsystem shape (physical lines; refresh in Phase R)

| Subsystem | LOC | Files | Share |
|---|---:|---:|---:|
| Layout (block/inline/flex/grid/table/intrinsic/multicol/positioned) | 66,207 | 38 | 35% |
| Event / state / editing | 45,501 | 24 | 24% |
| Render / paint / display-list / media | 36,721 | 63 | 20% |
| CSS style resolution | 17,257 | 9 | 9% |
| View tooling (cmd_layout, view_pool, view.hpp) | 13,775 | 6 | 7% |
| Shell (window, surface, webview, script_runner…) | 8,042 | 20 | 4% |

---

## 2. The honest headline

| Class | What | Net radiant LOC | Confidence |
|---|---|---:|---|
| A — sanctioned work | approved rows total **~1,230–1,740** before Phase R recount; CU3-T5's additional ~20–30 is excluded pending D-8 | **~1,230–1,740** | medium-high; includes deletion and behavior-preserving refactoring |
| B — spike-gated retirement | flex heuristic estimator subtree onto the intrinsic API | **~500–800** (long-term hypothesis ~1,200–1,400) | medium — needs internal differential, fixture, and release-performance gates first |
| C — scope & migration decisions | event_sim relocation, validation-layer gating, Lambda-script migration of serialization bands, logging policy | **non-additive alternatives; do not total** | mechanically plausible; **resolved 2026-07-18: declined/deferred — info only (§7)** |

**Approved working range before Phase R: roughly 1,900–2,900 lines (about 1.0–1.5%)**, including Class A, the C-1 terseness follow-ons, and Class B. This is a planning range, not a commitment. Class C figures overlap: state-dump gating overlaps state-dump migration, and simulator relocation overlaps later simulator migration, so there is no valid “total potential” obtained by adding its rows. There is no honest path to a 20%-class reduction: the four surveys independently confirmed that the big files are big because their content is bespoke, load-bearing policy (85% of the CSS switch is irreducible shorthand parsing; the block/inline/table/text cores are the CSS algorithms themselves).

Accounting rule (carried from Clean-Up 1): **deleted ≠ refactored ≠ moved ≠ migrated.** The §9 summary labels the nature of each bucket. Relocation to `test/` reduces `radiant/` LOC and the release binary but not repo LOC; Lambda-script migration deletes C+ LOC and adds ~⅓ as much script elsewhere.

---

## 3. Findings catalog — Class A: sanctioned work (~1,250–1,770 before Phase R)

Each: **what / where / save / risk**. IDs continue the campaign convention (CU3-*). “Deletion” is reserved for unreachable or deliberately retired behavior; inlining, table-driving, and API consolidation are recorded as refactoring even when they reduce physical lines.

### Tooling & diagnostics

| ID | Finding | Where | Save | Risk |
|---|---|---|---:|---|
| CU3-T1 | **Delete only the debug text-serialization branch of `print_view_tree()`**. The public wrapper is live (`cmd_layout.cpp` calls it) and its JSON path stays. The deletable subtree is `print_inline_props`/`print_block_props`/`print_view_*` plus the `#ifndef NDEBUG` text branch and `.txt` side-channel writes. Update `Lambda_Jube.md` and any profiling/design notes that still promise `view_tree.txt`; keep the shared file writer because JSON uses it. | current `view_pool.cpp`:951–1291; caller `cmd_layout.cpp`:6738; declaration stays | **~328**, Phase R recount | LOW after scope correction |
| CU3-T2 | **Replace plain per-case CSS entry logs with one ordinary-debug generic entry log; do not simply delete them.** The existing generic property-name log is behind `RADIANT_TRACE_CSS_PROPERTIES`, while the case logs are active in normal debug builds. At current HEAD there are ~158 plain entry logs and ~26 enriched entry logs. Emit one unconditional property-name/value-kind log after logical→physical remapping, remove only the plain duplicates, and retain enriched logs that carry element/value context. If the owner instead wants quieter default logging, record that as a diagnostic-policy change rather than “redundancy.” | `resolve_css_style.cpp`:5777 onward | **~150–165**, Phase R recount | LOW for generic replacement; MED for diagnostic-policy reduction |
| CU3-T3 | Dead `extract_inline_css` (body is `return nullptr` under a TODO; header says DEPRECATED pointing at a function that no longer exists; zero callers) | cmd_layout.cpp:1979–1985 | ~11 | LOW |
| CU3-T4 | Vestigial disabled-selection locals (computed then `(void)`'d; "legacy glyph-by-glyph selection" comment) | render_text.cpp:165–174 | ~10 | LOW |
| CU3-T5 | **Unresolved policy item:** retire `RADIANT_SCRIPT_BEFORE_CASCADE` only after an explicit owner reconfirmation. It remains a documented baseline-triage compatibility seam in RAD-20/RAD-21 and the design review. Removal includes those doc updates and tests for load-time `getComputedStyle`, script DOM/class/style mutation, injected/disabled `<style>`, and post-script recascade. It is not part of Phase 0. | `cmd_layout.cpp`:2630–2633 + branch sites; RAD-20:95; RAD-21:75 | ~20–30 | MED |

### Event / editing / simulator

| ID | Finding | Where | Save | Risk |
|---|---|---|---:|---|
| CU3-E1 | **Spike a command registry before approving the full rewrite.** Survey shape: ~70 `strcmp` parse branches, ~22 trivial parser cases, and ~70 execute cases. Start with 8–12 representative commands (trivial pointer/key, bespoke drag, assertion, retry-sensitive command) and measure net LOC and log behavior. Keep simulator JSON parsing separate from a transport-independent executor if WebDriver reuse is the goal. Prefer a linear scan for this cold ~70-row registry unless lookup cost is measured; if `bsearch` is retained, add a sortedness/uniqueness test. Only expand when the spike is net-negative and retry/lifetime semantics remain identical. See §5 C-1 for follow-ons. | current `event_sim.cpp`:1163–1994, 3624–5749 | **hypothesis ~250–400** | MED |
| CU3-E2 | **Form keymap table, blocked on a green baseline**: `KEY_DOWN` contains near-parallel key→`dispatch_form_*` paths for TEXT/TEXTAREA, but caret math, UTF-8 movement, selection collapse, beforeinput cancellation, read-only/disabled behavior, and Lambda-handler side effects differ. Extract only a common action after a semantic matrix proves the branches equivalent. Do not drive mouse caret placement through the same table merely because it shares helpers. | `event.cpp`, Phase R line refresh | **hypothesis ~150–300** | MED-HIGH; editor-4c must be green first |
| CU3-E3 | DP5 residue: dead compatibility stubs in the superseded non-rich key path (X-cut/BACKSPACE/DELETE/insert TODOs that do nothing) — ~30 now; the full legacy branch (~55) only after document-selection copy is routed through a modern handler | event.cpp:9505–9556, 9705–9717 | ~30 (→55) | LOW (MED for copy branch) |
| CU3-E4 | DP6 residue: delete dead `te_ime_commit` and its header declaration (live IME goes through `_prepare`/`_finish` with event injection between). **Do not** collapse `te_replace_byte_range`/`_no_events` into a boolean-control API: the named wrappers encode an important event-dispatch policy and the live wrapper already delegates to the shared implementation. | `text_edit.cpp`:778–792; `event.hpp`:1678–1679 | ~15 | LOW |

### Layout

| ID | Finding | Where | Save | Risk |
|---|---|---|---:|---|
| CU3-L1 | **Shared `resolve_replaced_used_size()`**: the CSS replaced-element used-size algorithm (natural size → explicit dims → aspect-ratio fill → min/max clamp) is re-implemented in four contexts; plus `extract_aspect_ratio_number` is a verbatim cross-file clone whose `_for_positioned` suffix defeated token scanning for three campaigns | layout_block.cpp:446–473/514, layout_positioned.cpp:10–37/38, layout_flex_measurement.cpp:1750–1791, intrinsic_sizing.cpp img/video/svg blocks | **~155–205** | MED — constraint order differs subtly per context; gate on replaced-element fixtures |
| CU3-L2 | **Small layout refactors, not dead-code deletion.** `measure_text_run` is live through `measure_text_content_accurate` and is declared in `layout.hpp`; it may be inlined into its sole caller and unexported after Phase R confirms the call graph. For the before/after inline-content predicate quartet, extract a shared traversal only if named wrappers preserve the semantic distinction; do not expose a vague `require_content` boolean at call sites. Revalidate the grid-wrapper claim against current HEAD before including it. | current `layout_flex_measurement.cpp`:1573–1609; `layout.hpp`:1657–1660; `layout_block.cpp`:1925–2035 | **hypothesis ~50–90** | LOW-MED |

### CSS resolution

| ID | Finding | Where | Save | Risk |
|---|---|---|---:|---|
| CU3-C1 | **Write-safe descriptor applier for the cleanly-pure cases.** The existing `CssPropAccessor` is a read/serialization descriptor; its base accessor can point at absent, default, or immutable canonical storage and therefore must not be reused for mutation directly. Add an explicit write-side group initializer/callback that calls the correct `ensure_*()` gate, preserves Block/Font inherited initialization, triggers InlineProp copy-on-write, and handles auxiliary flags such as `has_color`. Fold only proven-pure keyword/length/color stores; keep OPACITY clamp, BOX_SIZING allow-list, VISIBILITY remap, FLOAT inherit-walk, and other quirks bespoke. | `resolve_css_style.cpp`; `css_prop_table.cpp`:422; `view_prop_ensure.cpp`:24–72/138–176 | **hypothesis ~80–160** | MED-HIGH until retained-view/COW tests pass |

CU3-C1's real payoff is not LOC: the prop→{group, offset, value-kind} identity/storage knowledge is repeated across apply, serialization, animation, and Lambda/Jube metadata. The revised goal is to unify only metadata whose semantics are genuinely identical. Animation interpolation type, write allocation/COW, inheritance, and per-property quirks may remain separate callbacks or flags. Required tests cover absent groups, inherited Block/Font initialization, canonical InlineProp copy-on-write, retained relayout, incremental reconciliation, pseudo styles, and byte-identical computed-style serialization. The aggressive “absorb the quirks” variant remains rejected.

---

## 4. Class B: the flex estimator spike (~500–800, long-term ~1,200–1,400)

`layout_flex_measurement.cpp` (2,409) still contains **two parallel content estimators** that pre-date the intrinsic-sizing API. The 2026-07-14 closure rerouted the *text leaf* onto `measure_text_intrinsic_widths` (:1887) but **not the element-child traversal**:

- `measure_flex_child_content` (:1328–1562) has **exactly one live caller** (layout_flex.cpp:1978) and an **exclusive helper subtree** — `flex_measure_direct_child_heights`, `flex_measure_direct_element_height`, `flex_measure_nested_flex_height`, `flex_measure_nested_content_summary`, etc. — totaling **~780 LOC** reachable from that single call site. It populates a *preliminary* `item->width/height` via the measurement cache (applied at layout_flex.cpp:2011–2020).
- `calculate_item_intrinsic_sizes`' element path (:2180–2274) still hand-walks the DOM through the same helper family instead of calling `calculate_min/max_content_width` / `calculate_max_content_height`.

**The spike (do this before committing to the finding):** determine whether the preliminary sizes are redundant with the `fi->intrinsic_*` path. Instrument the old and intrinsic paths in one build and record, for every measured child, min/max intrinsic widths, preliminary width/height, containing width, cache key/hit result, and final flex base size. Run the 494 flex fixtures plus the full layout baseline and require the internal records to match within the engine's existing float tolerance—not only the final JSON pixels. Then run a representative release-build layout benchmark and reject a material regression in traversal count, cache hits, or wall time. A temporary flag may exist only in the spike branch/commit and must be removed from the merged deletion.

If the differential, fixture, and performance gates pass, replace `measure_flex_child_content` one slice at a time and delete only the helper subtree proven exclusive (~500–800 net after keeping cache infrastructure). If the second estimator can follow, the longer-term hypothesis is ~1,200–1,400 of the file's survey-baseline 2,409 lines. **Risk remains HIGH until all three gates pass** — cached values are consumed, not overwritten, and prior flex-baseline attempts broke 23 fixtures.

*(Related but weaker: sharing the width/height dispatch scaffolding in intrinsic_sizing.cpp would net only ~200–350 at HIGH risk — the 2,716-line width function and 848-line height function are asymmetric by policy (min/max pair vs scalar; SUM-vs-MAX accumulation; the ~550-line inline table-column algorithm has no height mirror). Defer unless the flex spike builds confidence; do not attempt "merge the mirrors" wholesale.)*

---

## 5. Class C: scope & migration alternatives (non-additive; information only)

This is where "substantial" lives, and every item is an owner call, not an engineering discovery.

### C-1. Event simulator — DECIDED 2026-07-18: stays in production; make it terse instead

`event_sim.cpp` (5,974) + `event_sim.hpp` (450) is a scenario driver: command parsing, action execution, assertions, snapshot pixel-diff. The survey's relocation option (move to `test/`, ~6,500 LOC out of radiant/ and the release binary, plus ~1,800 of follow-on debug-gating) is **declined by owner decision**: the simulator is intentionally a production component because **parts of it will be reused to support WebDriver on Radiant**. The relocation numbers are preserved above only as information.

The accepted direction is CU3-E1 plus terseness slices — restructure, don't relocate. The full rewrite is now contingent on the representative registry spike:

1. **Command-registry spike, then expansion (CU3-E1, hypothesis ~250–400).** One `SimCommand {name, type, parse_fn, exec_fn}` registry may replace the regular portion of both ladders, but first prove the shape with 8–12 representative commands. Simulator JSON parsing stays simulator-specific; reusable command execution sits below it so a future WebDriver transport can translate routes/payloads without depending on scenario-file conventions or assertion commands. A linear lookup is the default because parsing is cold and ~70 rows are small; a measured need for `bsearch` requires a sortedness/uniqueness invariant test. Bespoke commands remain named functions only when extraction is net-negative overall.
2. **Field-descriptor slice (~100–200, take only if measurably net-negative).** The bespoke parse functions repeat the `reader.has(X) ? reader.get(X).asT() : default` triple dozens of times; a per-command `{json_key, member, kind, default}` spec table can absorb the regular ones. Gate each sub-slice on the LOC script — this is the kind of machinery that can go net-positive if pushed past the regular cases.
3. **Assert-prologue slice (~50–100).** ~15 assert cases inline the same resolve-target → require-view → fail-with-message prologue; one `sim_resolve_required_target()` helper serves them (the policy split vs live hit-testing is already recorded as a negative and is untouched — this shares the *prologue*, not the walk).

If the spike validates the design, the working combined hypothesis is **event_sim.cpp ~5,974 → ~5,300–5,500** with identical behavior, retry results, ownership cleanup, and golden sim logs. If it does not, keep only independently net-negative helpers. The remaining mass is genuine assertion/action semantics — terser dispatch cannot remove it, and rewriting the semantics is the declined Class C migration territory.

Deferred with this decision: the **state-machine validation layer** gating (~1,145, state_machine.cpp:350–1495) and **state-dump band** (~683, state_store.cpp:202–885) — both are reachable from the production simulator, so their debug-gating is now a future call to make together with the WebDriver design (a WebDriver build may well want the validation layer live).

### C-2. Lambda-script migration of the serialization bands (alternative future scope; non-additive)

The project has already decided directionally that "most of the state_store C+ code is expected to eventually migrate to Lambda script" (Code-Dedup RQ decision record), and the graph-layout family (~2.4k) has **already completed exactly this journey** — its C+ files are gone from radiant/. The survey's band map of state_store.cpp (8,095) says: ~78% is hot-path C+ that must stay (get/set core, caret-nav geometry, selection writes, projection refresh, focus); the migratable bands are the **Mark state dump (:202–885)** and **text/HTML clipboard extraction (:7683–8040)** — pure read→serialize transforms, **~1,040 LOC**, plus interned-name/schema tables (~150).

Prerequisite (the real cost): a Lambda-script bridge exposing DocState + ViewState + FormControlProp + laid-out View geometry. The existing `radiant_dom_bridge.cpp` exposes the DOM only. This same bridge is what a later Lambda rewrite of the sim's scenario layer and further state_store bands would ride on — so the bridge could be an investment with three consumers, not a one-off. These figures must not be added to C-1 relocation/gating figures where the same source bands overlap.

### C-3. Logging policy (up to ~1,500–3,000, explicitly NOT recommended as a default)

At the survey baseline, tree-wide single-line `log_debug/log_info` starts numbered **3,745** (~4,500–5,500 physical lines with continuations); resolve_css_style.cpp alone carried ~900–950 log lines. Clean-Up 2 correctly ruled these are **not** generic cleanup targets — they preprocess away in release and are part of the project's log-based debugging practice. This proposal keeps that ruling. CU3-T2 now preserves ordinary-debug property-name visibility with one generic log and retains enriched case logs; it is a consolidation, not a broad logging purge. A broader "logging tier" purge remains declined unless the owner makes a separate diagnostic-policy decision.

### C-4. What was checked and is NOT a lever (important negatives)

- **cmd_layout.cpp (7,280) is not test-harness bloat.** Bands 2–11 (~5,000 LOC: loaders, cascade, charset, selector index) are the shared front-end the interactive browser uses (`load_html_doc` et al. called from window.cpp, event.cpp, render_output.cpp…). The compare/report harness already lives outside the repo tree (`lambda-test/layout/*.js` over `print_view_tree_json`). Reduction beyond CU3-T3/T5: none.
- **render_svg_inline.cpp (5,503) does not re-implement lambda parsers.** Markup parsing is already delegated (it consumes `Element*` from `html5_parse_svg_document`); the path-data/length/viewBox/points parsers (~770 LOC) are SVG micro-syntax with no counterpart anywhere. Only ~100–140 (color table, transform matrix-compose) could partially delegate, at MEDIUM coupling risk — marginal, take only if touching the file anyway.

---

## 6. New verified negatives (append to the campaign ledger; do not re-audit)

1. **No legacy float path in layout_block.cpp** — `block_context.cpp` is the single unified BFC+float system; `FloatContext`/`BlockFormattingContext` exist only in comments.
2. **`layout_block_content`/`layout_block` are procedurally irreducible** — sequential CSS block-layout phases that already call the intrinsic API; splitting is refactoring, not reduction.
3. **Grid family is structurally sound post-convergence** — legacy `GridTrack` type is gone; syntax→model conversion happens once at init (grid_sizing.cpp:42–44), no round-trip; `grid_enhanced_adapter.hpp` is the live algorithm (misnamed, not a shim); placement vs positioning are distinct phases.
4. **Intrinsic width/height mirrors do not fold** — return-shape (pair vs scalar), accumulation (SUM vs MAX per axis), and the ~550-line width-only table-column algorithm are policy asymmetries; only ~200–350 of dispatch scaffolding is shareable, at HIGH risk.
5. **The range/editing trio layers, it does not duplicate** — dom_range.cpp owns boundary primitives; resolver (geometry, deliberately GLFW-free) and editing_target_range (semantics) consume them via `dom_boundary_compare/move`. Merging would break testability seams (DI hooks exist so unit tests avoid linking event.cpp). Consolidation is a <100-LOC discoverability play at best.
6. **No dual event engine** — template/JS dispatch and editing transactions are complementary layers; only the ~55-LOC DP5 non-rich branch is superseded.
7. **DP6 API tiers are deliberate** — `te_ime_commit_prepare`/`_finish` must stay split (events are injected between phases); only the unified `te_ime_commit` wrapper is dead.
8. **DP7 walker merge is net-neutral** — the shared skeleton (`render_paint_block_run`) and traversal (`render_walk_*`) are already extracted; the residual raster vs SVG/PDF drivers differ by output model (record+replay vs direct emission) and stateful raster concerns (~10 vtable hooks' worth). Same shape as the closed paint-op-switch negative.
9. **The `resolve_css_property` switch cannot be table-replaced** — 137 bespoke bodies hold 85% of its 5,904 body-lines (BACKGROUND 583, FONT 284, TRANSFORM 223…); the descriptor table shaves the thin pure tail only (CU3-C1). The pre-helperization audit verdict stands.
10. **resolve_htm_style.cpp has no table win** — per-tag UA-style bodies are genuinely bespoke (BODY parses 6 legacy attributes); only an ~15-LOC H1–H6 pair exists. Below threshold.
11. **No dormant subsystems remain** — lottie/gif/video players are wired via surface.cpp; webview stubs are platform-gated; script_runner has no second engine; no `#if 0` anywhere; render record/replay split is the architecture, not drift.
12. **cmd_layout.cpp and render_svg_inline.cpp negatives** — see §5 C-4.

---

## 7. Owner decisions from 2026-07-18 plus one remaining confirmation

| # | Decision | Resolution | Unlocks |
|---|---|---|---:|
| D-1 | Delete the debug text branch of the view-pool dumper while retaining the live wrapper + JSON path | **APPROVED** (Class A, scope corrected) | ~328 deleted, docs updated |
| D-2 | Relocate event_sim to test/ | **DECLINED** — simulator is a production component; parts will back WebDriver on Radiant. Terseness track instead (§5 C-1). | 0 moved; ~400–700 deleted via E1+slices |
| D-3 | Gate validation layer + state-dump band debug-only | **DEFERRED** — revisit with the WebDriver design (both are reachable from the production sim) | — |
| D-4 | Lambda bridge + serialization-band migration | **DEFERRED** — info only, may consider in future | — |
| D-5 | Flex-estimator spike (Class B) | **APPROVED with revised gates** | ~500–800 only after internal differential + fixture + release-performance proof |
| D-6 | CU3-C1 shared property storage metadata | **APPROVED with write-ownership constraint** | ~80–160 hypothesis + drift reduction; must use `ensure_*`/COW gates |
| D-7 | Logging: consolidate plain CU3-T2 entry logs while retaining ordinary-debug visibility and enriched logs; broader purge declined | **APPROVED as corrected** | ~150–165 hypothesis |
| D-8 | Retire `RADIANT_SCRIPT_BEFORE_CASCADE` | **NEEDS OWNER RECONFIRMATION** because it remains a documented triage seam | ~20–30; excluded until resolved |

## 8. Phasing and verification

### Phase R — mandatory revalidation; no cleanup implementation

1. Pin `f2930eed7` (or the newer merge-base if master advances) as the implementation baseline and require a clean worktree for measurements.
2. Rerun physical/NLOC counts, Lizard duplication, the zero-call scanner, and `make check-radiant-dup`.
3. Refresh every target's caller count, exclusive-helper subtree, line range, and LOC estimate. Explicitly recheck all files changed since `d76b90f5e`.
4. Establish green pre-change gates: `make test-radiant-baseline` plus the relevant domain target. In particular, **CU3-E2 cannot start until `make editor-4c` is green on the baseline commit.** A harness repair or expectation correction is a separate prerequisite PR with reviewed semantic justification; never rebaseline merely to unblock cleanup.
5. Amend this document with measured Phase R results before implementation begins. Estimates that fail revalidation are dropped, not worked around.

### Per-PR gates

- Keep each PR to one subsystem and one independently reviewable claim. Do not combine unrelated tooling, editor, layout, and CSS changes to make aggregate LOC negative.
- For a true deletion, run `./utils/verify_loc_reduction.sh --ref <merge-base> <changed-files>` and require that individual slice to be net-negative. Refactors such as CU3-C1 may be accepted at LOC breakeven for a demonstrated invariant/drift win, but must not be reported as deleted LOC.
- All PRs: `make build`, `make test-radiant-baseline`, the named domain suite, `make lint`, and `make check-radiant-dup`.
- Layout PRs additionally run `make lint ARGS='--rule ^no-int-cast-radiant$'` and the focused layout suite/fixtures.
- Behavior-preserving dispatch/serialization changes require byte-identical golden outputs where applicable. Diagnostic consolidation requires a focused debug-log check rather than claiming byte identity after intentionally changing entry text.
- Performance claims use `make release`; debug builds are never performance evidence.

### Implementation sequence

1. **Pure removals, separate small PRs:** CU3-T3; CU3-T4; the currently inert CU3-E3 stubs only; CU3-E4's dead `te_ime_commit` body + declaration. CU3-L2 is excluded because it is refactoring, not dead code.
2. **Diagnostics, separate PRs:** CU3-T1 debug text subtree + documentation; CU3-T2 generic ordinary-debug entry log + removal of plain case logs. CU3-T5 waits for D-8.
3. **Event simulator:** CU3-E1 representative spike first. If net-negative and behavior-identical, expand the registry in slices, then consider the field-descriptor and assertion-prologue follow-ons independently. Preserve retry results, parse-failure cleanup, and golden logs.
4. **Editing keymap:** CU3-E2 only after Phase R has a green `editor-4c`; build and test the semantic matrix before table-driving any action. Mouse caret placement remains separate unless equivalence is independently proven.
5. **Small layout refactors:** Phase-R-validated portions of CU3-L2, with named semantic wrappers rather than boolean-control call sites.
6. **Replaced-size algorithm:** CU3-L1 starts by promoting the verbatim aspect-ratio parser. Extract a pure normalized used-size core only where constraint ordering is proven identical; retain thin context wrappers for block, positioned, flex, and intrinsic policies. Review replaced-element fixture diffs individually.
7. **CSS metadata:** CU3-C1 is its own PR after write-side ownership/COW tests exist. Apply only pure rows through `ensure_*` mutation gates; keep read serialization and animation-specific semantics explicit.
8. **Class B flex estimator:** instrumented dual-path spike → internal differential → 494 flex fixtures + full layout baseline → release performance/cache comparison → per-slice deletion. Remove the experimental flag before merge.
9. ~~Relocation/gating~~ remains cancelled/deferred by D-2/D-3. ~~Bridge + migration~~ remains deferred by D-4. Class C stays outside approved totals.

---

## 9. Estimate summary

### Approved scope (Class A + B + C-1 terseness slices)

| Bucket | Net radiant LOC | Nature |
|---|---:|---|
| Approved Class A rows after review, including refactors and CU3-E1 hypothesis; CU3-T5 excluded | **~1,230–1,740** | mixture of deletion + refactoring; Phase R recount required |
| C-1 terseness follow-on slices (field-descriptor + assert-prologue, each independently LOC-gated) | ~150–300 | deletion/refactoring only if individually net-negative |
| Class B flex estimator | ~500–800 (long-term ~1,200–1,400 remains a hypothesis) | deleted only after three spike gates |
| **Approved working range** | **~1,900–2,900 (≈1.0–1.5%)** | not all rows are yet proven; do not label all as deletion |

### Deferred for future consideration (Class C, info only; rows overlap and are not additive)

| Bucket | Net radiant LOC | Nature |
|---|---:|---|
| C-1 sim relocation | ~6,500 | declined — sim is production; overlaps any later sim migration |
| C-1 gating (validation + dump bands) | ~1,800 release-only | deferred; state-dump portion overlaps C-2 wave 1 |
| C-2 migration wave 1 (dump + clipboard bands) | ~1,000–1,200 | deferred; needs DocState/View bridge and overlaps gating |
| C-2 migration wave 2 (sim scenario layer) | ~1,500–2,500 | deferred; overlaps relocation |

The honest core stands: after three campaigns, further *large* reduction requires deferred scope decisions. The approved work is a ~1.9–2.9k pre-revalidation mixture of genuine deletion and structural refactoring, plus an attempt to make the production simulator a cleaner transport-independent command substrate. Phase R may lower that range. The ratchets (`make check-radiant-dup`, header cap ≤24, no-new-per-file-header, the repaired zero-call scanner) keep it from regrowing.

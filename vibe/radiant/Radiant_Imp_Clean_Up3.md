# Radiant Implementation Clean-Up 3 — Structural LOC Reduction

**Status:** DECIDED 2026-07-18 (owner) — **Class A + Class B approved for implementation. Class C is recorded for information only; may be considered in future.** The event simulator stays in production deliberately: parts of it will be reused to support WebDriver on Radiant (see §5 C-1). Implementation not yet started.
**Date:** 2026-07-18
**Baseline:** master @ `d76b90f5e` — `radiant/` = **187,503 physical lines** (146,782 NLOC) across **160 files**
**Method:** four parallel code-survey agents (CSS resolution, layout, event/state/editing, render/tooling/shell) + a quantitative sweep (Lizard function census, logging/comment counts, subsystem grouping). Every finding was verified by reading the cited source; every hypothesis that died on contact is recorded in §5 so future audits stay cheap.
**Prior campaigns honored:** `Radiant_Impl_Clean_Up (done).md` (mechanical dedup, ~3.3–4.6k), `Radiant_Imp_Code_Dedup (done).md` (140→19 headers, ~2–2.5k), `Radiant_Imp_Clean_Up2.md` (dead exports + WebDriver + selection/grid convergence, −4,145). All their §1.1 verified negatives were given to the survey agents as exclusions; none are re-proposed here.

---

## 1. Starting point: this tree is already tight

Three campaigns in five days removed ~10k LOC. The filtered Lizard duplicate rate is **1.27%** (was 9.12% on 2026-07-13). There are no `#if 0` blocks, no dormant subsystems left (WebDriver deleted; CoreGraphics deliberately retained as excluded source; graph layout already migrated to Lambda script), and the zero-call scanner reports no candidates. **Another dedup campaign would find scraps.**

What remains is a different problem. The mass is *concentrated*, not smeared:

- The **top 12 functions hold ~24k lines** — 13% of the tree. `resolve_css_property` alone is **5,974 lines** (one function = 3.2% of radiant), `measure_element_intrinsic_widths` 2,717, `handle_event` 2,661, `process_sim_event` 2,122, `layout_block_content` 1,946.
- **~9,000 LOC of test/QA infrastructure ships in the release binary** (event simulator, state-machine validation layer, state-dump band, view-tree text dumper).
- Physical-line composition: **18,819 blank (10.0%) + 22,003 comment-only (11.7%) + ~4,500–5,500 logging** (3,745 single-line `log_debug/log_info` starts + continuations) + ~141k executable.

So the levers are (a) a bounded set of genuine deletions the surveys verified, (b) one spike-gated architectural retirement, and (c) **scope/relocation/migration decisions** — which is where the large numbers are, and which only the owner can green-light.

### Subsystem shape (physical lines)

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
| A — sanctioned deletions | 14 verified findings, no policy blockers beyond one already-flagged owner item | **~1,450–2,050** | high (each finding cited below) |
| B — spike-gated retirement | flex heuristic estimator subtree onto the intrinsic API | **~500–800** (long-term ~1,200–1,400) | medium — needs a fixture-backed spike first |
| C — scope & migration decisions | event_sim relocation, validation-layer gating, Lambda-script migration of serialization bands, logging policy | **~6,500–10,500** | high mechanically; **resolved 2026-07-18: declined/deferred — info only (§7)** |

**Total potential: roughly 8,500–13,500 lines ≈ 5–7% of the tree** — with Class C contributing the bulk. Without the Class C decisions, the realistic ceiling is ~2,000–2,900 (~1.3%). There is no honest path to a 20%-class reduction: the four surveys independently confirmed that the big files are big because their content is bespoke, load-bearing policy (85% of the CSS switch is irreducible shorthand parsing; the block/inline/table/text cores are the CSS algorithms themselves).

Accounting rule (carried from Clean-Up 1): **deleted ≠ moved ≠ migrated.** Every §7 table row labels which one it is. Relocation to `test/` reduces `radiant/` LOC and the release binary but not repo LOC; Lambda-script migration deletes C+ LOC and adds ~⅓ as much script elsewhere.

---

## 3. Findings catalog — Class A: sanctioned deletions (~1,450–2,050)

Each: **what / where / save / risk**. IDs continue the campaign convention (CU3-*).

### Tooling & diagnostics

| ID | Finding | Where | Save | Risk |
|---|---|---|---:|---|
| CU3-T1 | **Delete the view-tree text dumper** (owner item D3, flagged twice before; survey re-verified: zero external callers, closed recursive subtree reachable only from the `#ifndef NDEBUG` block; JSON is the sole test artifact consumed by `lambda-test/layout/*.js`) | view_pool.cpp:742–1050 + NDEBUG block 1063–1082 + fwd decl :15 | **~328** | LOW |
| CU3-T2 | **Delete 182 redundant per-case entry logs** — every CSS case opens with `log_debug("[CSS] Processing <x> property")`; the property name is already logged once at function entry via the `RADIANT_TRACE_CSS_PROPERTIES` gate (resolve_css_style.cpp:5791) | resolve_css_style.cpp:5842 et al. (182 sites) | **~180** | LOW |
| CU3-T3 | Dead `extract_inline_css` (body is `return nullptr` under a TODO; header says DEPRECATED pointing at a function that no longer exists; zero callers) | cmd_layout.cpp:1979–1985 | ~11 | LOW |
| CU3-T4 | Vestigial disabled-selection locals (computed then `(void)`'d; "legacy glyph-by-glyph selection" comment) | render_text.cpp:165–174 | ~10 | LOW |
| CU3-T5 | *(policy)* Retire the `RADIANT_SCRIPT_BEFORE_CASCADE` P17 escape hatch if the old load order is no longer needed | cmd_layout.cpp:2625–2628 + 3 branch sites | ~20–30 | LOW |

### Event / editing / simulator

| ID | Finding | Where | Save | Risk |
|---|---|---|---:|---|
| CU3-E1 | **Command-registry rewrite of the event simulator's dispatch** (measured: 70 `strcmp` parse branches of which 22 are trivial {set type, call shared field parser}; 70 execute cases, many thin assert delegations). One `{name, type, parse_fn, exec_fn}` registry replaces both the parse chain (~812 LOC) and the execute switch scaffolding; trivial commands become single table rows. **Doubles as the WebDriver substrate** — a future WebDriver server dispatches HTTP routes onto the same registry instead of growing its own handler ladder (the deleted `webdriver_server.cpp`'s ~20 per-endpoint lookup prologues were exactly the duplication this avoids). See §5 C-1 for the follow-on terseness slices (+~150–300). | event_sim.cpp:1174–1986, 3624–5749 | **~250–400** | LOW |
| CU3-E2 | **Form keymap table**: `KEY_DOWN` hand-expands near-parallel key→`dispatch_form_*` chains for TEXT (8954–9155) and TEXTAREA (9157–9490), and the same parallelism recurs in `MOUSE_DOWN` (7923–8013). One `(control capability, key, mods) → action` table with per-entry caret-adjust callbacks drives all four | event.cpp | **~200–300** | MED — TEXT/TEXTAREA caret math differs; table entries need callbacks |
| CU3-E3 | DP5 residue: dead compatibility stubs in the superseded non-rich key path (X-cut/BACKSPACE/DELETE/insert TODOs that do nothing) — ~30 now; the full legacy branch (~55) only after document-selection copy is routed through a modern handler | event.cpp:9505–9556, 9705–9717 | ~30 (→55) | LOW (MED for copy branch) |
| CU3-E4 | DP6 residue: dead `te_ime_commit` (zero callers — live IME goes through `_prepare`/`_finish` with event injection between); optionally collapse `te_replace_byte_range`/`_no_events` into one `bool fire_input` param | text_edit.cpp:778–792; :237/:252 | ~15 (+13) | LOW |

### Layout

| ID | Finding | Where | Save | Risk |
|---|---|---|---:|---|
| CU3-L1 | **Shared `resolve_replaced_used_size()`**: the CSS replaced-element used-size algorithm (natural size → explicit dims → aspect-ratio fill → min/max clamp) is re-implemented in four contexts; plus `extract_aspect_ratio_number` is a verbatim cross-file clone whose `_for_positioned` suffix defeated token scanning for three campaigns | layout_block.cpp:446–473/514, layout_positioned.cpp:10–37/38, layout_flex_measurement.cpp:1750–1791, intrinsic_sizing.cpp img/video/svg blocks | **~155–205** | MED — constraint order differs subtly per context; gate on replaced-element fixtures |
| CU3-L2 | Dead `measure_text_run` (no external callers) + inline-content predicate quad → 2 functions with a `require_content` param + grid `convert_to_*_sizing` wrapper inlining | layout_flex_measurement.cpp:1564–1611, layout_block.cpp:1925–2035, grid_sizing.cpp:47–151 | ~80–100 | LOW |

### CSS resolution

| ID | Finding | Where | Save | Risk |
|---|---|---|---:|---|
| CU3-C1 | **Descriptor-table applier for the cleanly-pure cases** — fold the ~20–27 pure keyword/length/color stores through the *existing* `CssPropAccessor` row (view.hpp:59, already used by the serializer's `DIRECT_ROW` table); no keyword→enum maps needed (parser already emits shared `CssEnum`), and specificity is provably untouched by these cases | resolve_css_style.cpp switch tail; css_prop_table.cpp:416 | **~100–170** | MED — misclassifying a quirk case (OPACITY clamp, BOX_SIZING allow-list, VISIBILITY remap, FLOAT inherit-walk) as pure would be a silent bug; keep quirks bespoke |

CU3-C1's real payoff is not LOC: the prop→{group, offset, value-kind} knowledge is currently encoded **four times** (apply switch, serialize table, animation applier css_animation.cpp:122/653, lambda metadata table). Unifying on one row kills a live class of drift bugs and gives Jube's DOM dispatch the same descriptors. Recommended even at LOC-breakeven; the aggressive variant (~350–400 by absorbing quirk cases) is **not** recommended — it re-adds moved logic and degrades per-property logging.

---

## 4. Class B: the flex estimator spike (~500–800, long-term ~1,200–1,400)

`layout_flex_measurement.cpp` (2,409) still contains **two parallel content estimators** that pre-date the intrinsic-sizing API. The 2026-07-14 closure rerouted the *text leaf* onto `measure_text_intrinsic_widths` (:1887) but **not the element-child traversal**:

- `measure_flex_child_content` (:1328–1562) has **exactly one live caller** (layout_flex.cpp:1978) and an **exclusive helper subtree** — `flex_measure_direct_child_heights`, `flex_measure_direct_element_height`, `flex_measure_nested_flex_height`, `flex_measure_nested_content_summary`, etc. — totaling **~780 LOC** reachable from that single call site. It populates a *preliminary* `item->width/height` via the measurement cache (applied at layout_flex.cpp:2011–2020).
- `calculate_item_intrinsic_sizes`' element path (:2180–2274) still hand-walks the DOM through the same helper family instead of calling `calculate_min/max_content_width` / `calculate_max_content_height`.

**The spike (do this before committing to the finding):** determine whether the preliminary sizes are redundant with the `fi->intrinsic_*` path — i.e., replace `measure_flex_child_content` with direct intrinsic-API calls behind a flag, run the 494 flex fixtures + full layout baseline, and diff. If green, delete the exclusive subtree (~500–800 net after keeping the cache infra). If the second estimator can follow, the total re-implementation living in this file is ~1,200–1,400 of its 2,409 lines. **Risk: HIGH until the spike passes** — the cached values are consumed, not overwritten; prior flex-baseline attempts broke 23 fixtures, so this goes one slice at a time with per-slice fixture gates.

*(Related but weaker: sharing the width/height dispatch scaffolding in intrinsic_sizing.cpp would net only ~200–350 at HIGH risk — the 2,716-line width function and 848-line height function are asymmetric by policy (min/max pair vs scalar; SUM-vs-MAX accumulation; the ~550-line inline table-column algorithm has no height mirror). Defer unless the flex spike builds confidence; do not attempt "merge the mirrors" wholesale.)*

---

## 5. Class C: scope & migration decisions (~6,500–10,500)

This is where "substantial" lives, and every item is an owner call, not an engineering discovery.

### C-1. Event simulator — DECIDED 2026-07-18: stays in production; make it terse instead

`event_sim.cpp` (5,974) + `event_sim.hpp` (450) is a scenario driver: command parsing, action execution, assertions, snapshot pixel-diff. The survey's relocation option (move to `test/`, ~6,500 LOC out of radiant/ and the release binary, plus ~1,800 of follow-on debug-gating) is **declined by owner decision**: the simulator is intentionally a production component because **parts of it will be reused to support WebDriver on Radiant**. The relocation numbers are preserved above only as information.

The accepted direction is CU3-E1 plus terseness slices — restructure, don't relocate:

1. **Command registry (CU3-E1, ~250–400, approved in Class A).** One `SimCommand {name, type, parse_fn, exec_fn}` table, name-sorted for bsearch. The 22 trivial parse branches collapse to rows sharing `parse_pointer_fields`/`parse_key_fields`/`parse_target`; bespoke commands keep small named parse functions; the execute switch's 70-case scaffolding becomes direct `exec_fn` dispatch. This is also the WebDriver alignment: W3C WebDriver endpoints are name-keyed commands with JSON payloads — the future server maps routes onto this same registry (session/element plumbing stays server-side), so the terseness work is a down payment on the WebDriver plan, not competition with it.
2. **Field-descriptor slice (~100–200, take only if measurably net-negative).** The bespoke parse functions repeat the `reader.has(X) ? reader.get(X).asT() : default` triple dozens of times; a per-command `{json_key, member, kind, default}` spec table can absorb the regular ones. Gate each sub-slice on the LOC script — this is the kind of machinery that can go net-positive if pushed past the regular cases.
3. **Assert-prologue slice (~50–100).** ~15 assert cases inline the same resolve-target → require-view → fail-with-message prologue; one `sim_resolve_required_target()` helper serves them (the policy split vs live hit-testing is already recorded as a negative and is untouched — this shares the *prologue*, not the walk).

Realistic combined: **event_sim.cpp ~5,974 → ~5,300–5,500** with identical behavior (golden sim logs byte-identical). The remaining mass is genuine assertion/action semantics — terser dispatch cannot remove it, and rewriting the semantics is the (declined) Class C migration territory.

Deferred with this decision: the **state-machine validation layer** gating (~1,145, state_machine.cpp:350–1495) and **state-dump band** (~683, state_store.cpp:202–885) — both are reachable from the production simulator, so their debug-gating is now a future call to make together with the WebDriver design (a WebDriver build may well want the validation layer live).

### C-2. Lambda-script migration of the serialization bands (deletes ~1,000–1,200 C+; more later)

The project has already decided directionally that "most of the state_store C+ code is expected to eventually migrate to Lambda script" (Code-Dedup RQ decision record), and the graph-layout family (~2.4k) has **already completed exactly this journey** — its C+ files are gone from radiant/. The survey's band map of state_store.cpp (8,095) says: ~78% is hot-path C+ that must stay (get/set core, caret-nav geometry, selection writes, projection refresh, focus); the migratable bands are the **Mark state dump (:202–885)** and **text/HTML clipboard extraction (:7683–8040)** — pure read→serialize transforms, **~1,040 LOC**, plus interned-name/schema tables (~150).

Prerequisite (the real cost): a Lambda-script bridge exposing DocState + ViewState + FormControlProp + laid-out View geometry. The existing `radiant_dom_bridge.cpp` exposes the DOM only. This same bridge is what a later Lambda rewrite of the sim's scenario layer (−1,500–2,500 more C+) and further state_store bands would ride on — so the bridge is an investment with three consumers, not a one-off.

### C-3. Logging policy (up to ~1,500–3,000, explicitly NOT recommended as a default)

Tree-wide, single-line `log_debug/log_info` starts number **3,745** (~4,500–5,500 physical lines with continuations); resolve_css_style.cpp alone carries ~900–950 log lines (~8% of the file). Clean-Up 2 correctly ruled these are **not** cleanup targets — they preprocess away in release and are the project's debugging culture (CLAUDE.md mandates log-based debugging). This proposal keeps that ruling: only CU3-T2's 182 *redundant* entry logs (info already logged at function entry) are targeted. A broader "logging tier" purge is listed here solely so the option is priced; it trades real debuggability for lines and should stay declined unless the owner says otherwise.

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

## 7. Decision items — RESOLVED by owner, 2026-07-18

| # | Decision | Resolution | Unlocks |
|---|---|---|---:|
| D-1 | Delete the view-pool text dumper (re-confirmation of old D3) | **APPROVED** (Class A) | ~328 deleted |
| D-2 | Relocate event_sim to test/ | **DECLINED** — simulator is a production component; parts will back WebDriver on Radiant. Terseness track instead (§5 C-1). | 0 moved; ~400–700 deleted via E1+slices |
| D-3 | Gate validation layer + state-dump band debug-only | **DEFERRED** — revisit with the WebDriver design (both are reachable from the production sim) | — |
| D-4 | Lambda bridge + serialization-band migration | **DEFERRED** — info only, may consider in future | — |
| D-5 | Flex-estimator spike (Class B) | **APPROVED** | ~500–800 deleted, spike-gated |
| D-6 | CU3-C1 descriptor rows as single property-metadata source | **APPROVED** (Class A) | ~100–170 + drift-kill |
| D-7 | Logging: only CU3-T2's redundant 182 entry logs; broader purge declined | **APPROVED as scoped** | ~180 deleted |

## 8. Phasing (each phase = one PR, three gates: `verify_loc_reduction.sh` strictly negative for deletion phases · `make build && make test-radiant-baseline` 100% · domain suite)

1. **Phase 0 — pure deletions, no decisions needed:** CU3-T3, T4, E3-stubs, E4-dead, L2 (~150 LOC; layout+editor suites).
2. **Phase 1 — sanctioned diagnostics:** CU3-T1 (on D-1) + CU3-T2 (~510; layout baseline byte-identical for JSON dump).
3. **Phase 2 — table-driving:** CU3-E1 sim dispatch (~250–400; editor/sim suites byte-identical logs), then CU3-E2 keymap (~200–300; editor-4c suites — note these were red on harness issues at last campaign close; fix or re-baseline first).
4. **Phase 3 — layout slices:** CU3-L1 replaced-size unification (replaced-element fixtures reviewed diff-by-diff), CU3-C1 descriptor applier (CSS fixtures byte-identical).
5. **Phase 4 — Class B spike, then slices** (approved): flag-gated estimator replacement → fixture diff → per-slice deletion.
6. ~~Phase 5 — relocation/gating~~ **cancelled by D-2/D-3 resolution**; replaced by the §5 C-1 terseness slices, which fold into Phase 2.
7. ~~Phase 6 — bridge + migration~~ **deferred with D-4** (Class C, info only).

---

## 9. Estimate summary

### Approved scope (Class A + B + C-1 terseness slices)

| Bucket | Net radiant LOC | Nature |
|---|---:|---|
| Class A deletions (Phases 0–3, incl. CU3-E1 ~250–400) | ~1,450–2,050 | deleted |
| C-1 terseness follow-on slices (field-descriptor + assert-prologue, each LOC-gated) | ~150–300 | deleted |
| Class B flex estimator (Phase 4) | ~500–800 (→1,200–1,400 long-term) | deleted, spike-gated |
| **Approved total** | **~2,100–3,150 (≈1.1–1.7%)** | all genuinely deleted |

### Deferred for future consideration (Class C, info only)

| Bucket | Net radiant LOC | Nature |
|---|---:|---|
| C-1 sim relocation | ~6,500 | declined — sim is production (WebDriver substrate) |
| C-1 gating (validation + dump bands) | ~1,800 release-only | deferred to the WebDriver design |
| C-2 migration wave 1 (dump + clipboard bands) | ~1,000–1,200 | deferred (needs DocState/View bridge) |
| C-2 migration wave 2 (sim scenario layer) | ~1,500–2,500 | deferred |

The honest core stands: after three campaigns, further *large* reduction requires the deferred scope decisions; the approved work is the genuine-deletion tail (~2–3k) plus making the production simulator terse enough to be a clean WebDriver substrate. The ratchets (`make check-radiant-dup`, header cap ≤24, no-new-per-file-header, the repaired zero-call scanner) keep it from regrowing.

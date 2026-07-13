# Radiant Implementation Clean-Up — LOC Reduction Proposal

**Date:** 2026-07-13
**Baseline commit:** `028a75323`
**Method:** Five parallel code-survey agents, one per subsystem (layout, CSS resolution, render/paint, event/editing/state, shell/misc). Every finding below was verified by reading the cited source bodies and tracing callers — not inferred from file names. Line numbers are current-tree at the baseline commit.

**Relationship to `vibe/Lambda_Code_Clean_Up.md`:** that ledger's radiant items (§4.x layout, §5.x paint) are largely done or quantified-partial. This proposal picks up its open remainders (§4.5 baseline walkers, §4.6 flex percentages, §5.1 replay switches, §5.3 paint-op table, §5.7 audit slices) and adds a fresh sweep of subsystems the ledger never covered: CSS property resolution, event/state logging, view_pool dumpers, cmd_layout, and the form/background painters.

---

## 1. Current shape of the code

`radiant/` = **197,966 LOC** across 283 files (`.cpp/.hpp/.c/.h`), essentially flat (only `webdriver/` is a subdirectory).

| Subsystem | LOC | Files | Biggest files |
|---|---|---|---|
| Layout (block/inline/flex/grid/table/intrinsic) | 64,750 | 65 | layout_table 9,842 · layout_block 9,004 · intrinsic_sizing 6,294 · layout_flex 5,461 |
| Event / editing / state | 45,255 | 41 | event 9,644 · state_store 8,524 · event_sim 5,937 · dom_range 4,551 |
| Render / paint / display-list | 35,613 | 111 | render_svg_inline 5,596 · render_pdf 2,381 · render_background 2,108 · paint_ir 1,963 |
| Shell / misc / tooling | 24,784 | 33 | cmd_layout 7,200 · view_pool 3,459 · script_runner 2,574 |
| CSS resolution | 19,345 | 11 | **resolve_css_style 13,889** · resolve_htm_style 2,152 · css_animation 1,668 |
| Graph layout | 2,532 | 8 | |
| Other | 5,687 | 14 | |

Structural observations that drive the plan:

- **`resolve_css_style.cpp` is 7% of all of radiant in one file**, and 67% of that file (lines 4590–13889) is a *single function* — `resolve_css_property`, a ~180-case switch. The wins there are mechanical boilerplate, not replacing the switch.
- **The paint-op set is switched over in 5–6 places** (validate, lower-raster, lower-svg, plus two switches inside `render_pdf.cpp` — an undocumented fourth lowering backend the §5.3 audit missed), and the display-list op set in **11 switch sites across 6 files**. Adding one primitive touches all of them.
- **`intrinsic_sizing.cpp` re-implements grid, flex, and table measurement — twice each** (once for widths in a 3,050-line function, again for heights in a 1,040-line function). This is the single largest duplication mass in radiant, and also the most fragile.
- **Copy-paste-per-side / per-type families** are everywhere at small scale: 4-side border blocks, 9 state-transition loggers, 9 event-dispatch scaffolds, 7 form-control painter prologues, 5 table border getters, 4 cloned nth-of-type selector builders, 25 trivial teardown wrappers.

### 1.1 Verified non-findings (do NOT re-audit these)

Recording the negatives is as valuable as the positives — each was a plausible hypothesis that reading the code disproved:

- **Flex is layered, not parallel.** `layout_flex_multipass.cpp` wraps and drives `layout_flex_container` (layout_flex.cpp:575); it does not re-implement the line algorithm. Not a merge candidate.
- **There is no dead grid engine.** `grid_enhanced_adapter.hpp` *is* the live sizing path (layout_grid.cpp:282, 556), not legacy.
- **Multicol reuses block primitives** (`layout_flow_node`, `finalize_block_flow`, float prescan); its own code is genuinely fragmentation-specific.
- **`render_svg.cpp` vs `render_svg_inline.cpp` are opposite directions**, not clones: inline-SVG *parses* SVG → PaintIR; render_svg *serializes* PaintIR → SVG text, and already delegates to `paint_ir_lower_svg_stream`. Only residual dup: color-string helper (§P3) and the legacy border/text direct-emission layer (§D4).
- **`graph_to_svg.cpp` shares nothing with `render_svg.cpp`** — it builds a Lambda `Element` tree via `ElementBuilder`, not SVG text.
- **`state_store.cpp` is not a read/write serializer pair** — `state_dump_*` is write-only (consumed by event_sim text-diff asserts); there is no read-back walker to unify against. `state_schema.cpp` is a transition-rule table, not a deserializer.
- **calc()/unit conversion is already centralized** (`resolve_length_value`:2454, `evaluate_calc_expression`:2348) — no per-property calc duplication.
- **`resolve_htm_style.cpp` overlap is resolved** — it is now tag-dispatch over shared `apply_html_*` helpers; the ledger's §4.8 verdict still holds.
- **The §5.5 "rect zoo" is negative ROI** — `Rect`/`Bound`/`TextRect`/`DirtyRect`/`Point2D` are ~4 lines each and genuinely differ; consolidation touches hundreds of call sites to save ~a dozen lines. Close §5.5 as won't-fix.
- **Platform shells are properly abstracted** — window/surface/webview platform variance lives in separate per-OS files over different OS APIs, not near-identical `#ifdef` blocks.
- **`css_animation.cpp` interpolation does not duplicate resolver logic** (only 4 properties, own code path). Its *name→id map* does duplicate the parser's, though (§C4).
- **Dead code is rare.** No `.bak` files remain in radiant/; the only `#if 0` block is resolve_css_style.cpp:6065 (46 lines); event/editing/state has essentially zero dead functions.

---

## 2. Verification harness (prerequisite for every phase)

**Rule: a phase is only "done" when all three gates pass.**

1. **LOC gate** — `utils/verify_loc_reduction.sh` (new, committed with this proposal) compares the phase's file set between a git ref and the working tree and **fails unless total LOC strictly decreased**. New helper files created by the phase must be listed too, so moving code doesn't masquerade as deleting it:

   ```bash
   # example: phase C1, run before committing
   ./utils/verify_loc_reduction.sh --ref master \
       radiant/resolve_css_style.cpp radiant/resolve_style_helpers.hpp
   # prints per-file before/after/delta table; exit 1 if total didn't shrink
   ```

2. **Behavior gate** — the phase's listed test suites pass 100%:
   - always: `make build && make test-radiant-baseline`
   - layout-touching phases: `make layout suite=baseline`
   - editing/event/state phases: `make editor-4c-js && make editor-4c-view`
   - render phases: SVG-export / PDF fixtures per the phase notes
3. **Whole-tree sanity** — `make lint` stays clean; `./utils/count_loc.sh` recorded in the phase's commit message (running total).

Byte-identical output is the target for all "mechanical" phases: dumpers, loggers, and painters must produce identical snapshots/golden files, which the suites diff automatically.

---

## 3. Findings catalog

Grouped by subsystem; each entry: **what / where / how to unify / est. net LOC saved / risk**.

### CSS resolution (resolve_css_style.cpp and friends) — est. ~790–840

| ID | Finding | Where | Save | Risk |
|---|---|---|---|---|
| C1 | Missing `ensure_bound`/`ensure_border` helpers: the 3-line `if (!span->bound) … alloc_prop` block repeats ×85, border variant ×40, `if (!span->blk)` ×19. resolve_htm_style.cpp already has exactly these helpers (`ensure_html_border_prop`:156) — the pattern is sanctioned, this file never got them. | resolve_css_style.cpp, throughout the switch | ~250 | very low |
| C2 | 4-side border expansion: `BORDER_{TOP,RIGHT,BOTTOM,LEFT}_WIDTH` are four ~56-line blocks identical except the side suffix (8120–8344); same for `*_STYLE` (8345–8431) and `*_COLOR` (8433–8488). One `resolve_border_side_{width,style,color}(span, side, value, specificity)` each. | resolve_css_style.cpp:8120–8488 | ~235 | low |
| C3 | Margin/padding/inset per-side clones + logical-property fan-out: 4 margin sides (5918–5965) + 6 logical margin cases re-expanding to the same fields (5966–6056); padding mirrors; ~10 inset/top/left/right/bottom clones (9609+). Drive via `{prop_id → side}` over shared `resolve_box_side()`. Also extract the 15× repeated keyword/percent type-ternary into `css_value_axis_type()`. **Bonus: fixes live drift — `TOP` (9629) has isnan/calc handling that `LEFT`/`RIGHT`/`BOTTOM` lack.** Preserve the hard-wired LTR/horizontal-tb logical mapping (comments at 6011, 6023). | resolve_css_style.cpp:5918–6056, 9609+ | ~200 | low-med |
| C4 | `property_name_to_id` in css_animation.cpp (123–152) reimplements the parser's `css_property_id_from_name()` (css_properties.cpp:448) as a 26-entry strcmp chain. Delegate. | css_animation.cpp:123–152 | ~28 | low |
| C5 | 5 no-op cases that only `log_debug` and store nothing — `TABLE_LAYOUT` (12114), `BORDER_COLLAPSE` (12130), `BORDER_SPACING` (12145), `CAPTION_SIDE` (12156), `EMPTY_CELLS` (12172); consumers read raw declarations directly (layout_table.cpp:2876). Fold into `default:`. Plus the tree's only `#if 0` block (6065, 46 lines). | resolve_css_style.cpp | ~120 | low |
| C6 | *(optional, policy)* logging macro: `info ? info->name : "unknown"` idiom ×42, 957 `log_debug` lines total. `LOG_ENUM_PROP` macro slice only. | resolve_css_style.cpp | ~100–150 | med (debug-value tradeoff) |

### Layout — est. ~480–740 safe, +800–1500 structural

| ID | Finding | Where | Save | Risk |
|---|---|---|---|---|
| L1 | Dead computation: `calculate_initial_grid_extent` is pure and its only call discards the result (`(void)…`); the next step recomputes independently. | grid_enhanced_adapter.hpp:520–558, 635 | ~40 | very low |
| L2 | 5 near-identical border-source getters (`get_cell_border`, `get_table_border`, `get_row_border`, `get_rowgroup_border`, `get_column_border`) — same shape, different source struct. One parameterized getter. | layout_table.cpp:1880–2120 | ~80 | low-med |
| L3 | 3 recursive first-baseline walkers (ledger §4.5, still open): the shared `compute_element_first_baseline` (layout_alignment.cpp:255) is used by grid; flex has its own ~200-LOC family (layout_flex.cpp:3131–3330), table a ~290-LOC third (layout_table.cpp:894–1182). Fold flex + table onto the shared walker, parameterizing the row/cell strut case. | layout_flex.cpp, layout_table.cpp | ~300 | med |
| L4 | Flex justify-content distribution (`align_items_main_axis`, layout_flex.cpp:4123–4294) still partially hand-rolls what `compute_space_distribution` owns; the rest of the alignment layer is already shared. | layout_flex.cpp:4123–4294 | ~60 | med |
| L5 | Parallel flex height walker: `measure_content_height_recursive` (layout_flex_measurement.cpp:402–520) + parts of `measure_flex_child_content` (609–1382) predate the file's later delegation to the intrinsic-sizing API and run their own math (the §4.6 "flex bypasses re-resolver" symptom). Route through the intrinsic height API. | layout_flex_measurement.cpp | ~120–250 | med-high |
| L6 | **intrinsic_sizing.cpp mirrors real layout, twice per engine**: `measure_element_intrinsic_widths` (1970–5027) and `calculate_max_content_height` (5122–6163) each re-implement grid track selection (incl. `repeat()` expansion that also lives in grid_placement.hpp — written 3×), flex line packing (vs `create_flex_lines`/`calculate_flex_basis`), and table column min/max. Extract per-engine "intrinsic contribution" helpers shared by real layout and the intrinsic pass. **Attack incrementally, starting with the self-contained `repeat()` expansion.** | intrinsic_sizing.cpp | ~800–1500 | HIGH |
| L7 | *(restructure, modest net)* `table_auto_layout` (layout_table.cpp:6089, 3,215 LOC, 109 row/col/cell loops) — extract a cell/row/column iterator. | layout_table.cpp | ~150–300 | med |

Test coverage is strong: 494 flex + 344 grid + 703 table layout fixtures, 12,310 reference snapshots.

### Render / paint — est. ~330–450 safe, +250–300 structural

| ID | Finding | Where | Save | Risk |
|---|---|---|---|---|
| P1 | 7 form-control painters open with a byte-identical geometry prologue (scale/x/y/w/h), plus the CSS-override query block ×4 and the DocState/disabled block ×7. One `FormControlBox` struct + `form_control_box()` helper. | render_form.cpp:452–1577 | ~60 | low |
| P2 | Rounded-rect clip-path computed 3–4× (`background_gradient_clip_path`:289, `background_image_clip_path`:1920, `background_image_clip_shapes`:1888, inline in radial:450+). One `background_rounded_clip_path()`. | render_background.cpp | ~35 | low |
| P3 | Gradient/image tile loops are near-clones (`render_linear_gradient_layer`:1837 vs `render_radial_gradient_layer`:1858 + image tiling): same plan→row/col→tile-rect→skip→leaf shape. One `background_for_each_tile(…, callback)`. Preserve radial's deliberate position_rect ignore (:1862). Plus: `svg_color_to_string` (render_svg.cpp:282) ≡ `paint_svg_append_color` (paint_ir.cpp:1205) — share one. | render_background.cpp, render_svg.cpp | ~45 | low-med |
| P4 | DL descriptor table, safe slice (extends ledger §5.1): the ~26-op `DL_` set is hand-enumerated in **11 switch sites / 6 files** — storage serialize/deserialize/free (:114/:221/:506), retained dup/free (:77/:148/:431), bounds (:16), tile (:321/:350), replay (:90). An X-macro `DL_OP_LIST` descriptor {size, serialize, free, bounds} drives storage+retained+bounds first; **defer the replay-core merge** (the risky half). | display_list_storage.cpp, retained_display_list.cpp, display_list_bounds.cpp | ~80–140 | med (storage/retained low) |
| P5 | Paint-op descriptor table incl. PDF (extends ledger §5.3): `render_pdf.cpp` is a **fourth** parallel lowering backend with its own 21-case switches (:119, :1019, :1126), parallel to paint_ir validate (:130), lower-raster (:958), lower-svg (:1504). Extend `PAINT_OP_LIST` into a descriptor table {bounds-fn, per-backend lower fn}; scaffolding and validate/bounds boilerplate collapse, bodies stay. | paint_ir.cpp, render_pdf.cpp | ~120–180 | med-high |
| P6 | *(parity work, defer)* render_svg legacy direct-emission layer: borders still hand-emit SVG polygons (~140 LOC, `svg_emit_border_side`:688 etc.) while backgrounds already route through paint ops; decorated/shadowed text has a dual path (:491–566, ~75 LOC). Requires PaintIR SVG lowering to reach feature parity first — the §5.7 deferred slice. | render_svg.cpp | ~140–215 | med-high |

### Event / editing / state — est. ~275–375

| ID | Finding | Where | Save | Risk |
|---|---|---|---|---|
| E1 | 9 near-identical ~24-line state-transition JSON loggers (bool/int/float/scroll/dropdown-owner/ctx-menu-target/ctx-menu-hover/view-target/doc-bool). Two helpers: scalar-transition + view-ref-transition; scroll stays bespoke. | state_store.cpp:3212–3460 | ~90 | low |
| E2 | 9 `radiant_dispatch_*_event` functions repeat an identical 8–11-line scaffold (dom-resolve, JsCtxScope enter, timer, wrap, dispatch, read-prevented, exit); re-scaffolded again in 2 sim bridges. RAII `JsDispatchScope` + `radiant_dispatch_built_event()`; each function shrinks to its builder line. Watch the void-returning composition/focus variants. | event.cpp:4757–5126 | ~65 | low-med |
| E3 | state_machine `*_transition` family shares the enter/guard/leave/commit/assert epilogue ×6; `hover_transition` (240) and `active_transition` (277) are line-for-line identical except family/event/setter — merge into `single_target_transition()`. Transition ordering is load-bearing; preserve guard early-returns. | state_machine.cpp:57–385 | ~40 | low-med |
| E4 | 3 file-local "editing surface" JSON writers emit the same core object (event.cpp:229, editing_dispatch.cpp:524, state_machine.cpp:1722). One shared `editing_log_write_surface_core_fields()` in event_state_log; callers keep their extra fields. Removes a 3-way format-drift risk. | 3 files | ~18 | low |
| E5 | event_sim reimplements live-pipeline logic (the §5.7 flagged next slice): `find_element_at`:857 vs event.cpp hit-test, radio-group DFS (908–937) ≡ `uncheck_radio_group` (event.cpp:5443), `sim_extract_text`:946 ≡ state_store text extraction. Promote the event.cpp statics to a header and call them. Sim's hit-test is *deliberately* z-unaware — share without changing live behavior. | event_sim.cpp, event.cpp | ~75 | med |
| E6 | Caret/selection snapshot accessors overlap (caret_get_render_snapshot ⊇ visual_snapshot etc.); delegate narrow accessors to the widest one. | state_store.cpp:6871–7010, 7483–7660 | ~35 | low (broad callers) |

### Shell / misc / tooling — est. ~470–630 safe, + large decision items

| ID | Finding | Where | Save | Risk |
|---|---|---|---|---|
| M1 | nth-of-type selector-builder block copy-pasted ×4 (sibling-count loop + snprintf). One `build_element_selector()`. Output is consumed by layout JSON snapshots — must stay byte-identical. | view_pool.cpp:~2000, 2160, 2432, 3120 | ~115 | low-med |
| M2 | JSON field-emit boilerplate across the ~2,200-line view-tree dumper: every field is a 3-line indent/key/comma sequence; `#null` tag-name workaround repeats (~28 lines). `json_field()` + `resolve_tag_name()` helpers. | view_pool.cpp:1246–3459 | ~150–250 | low |
| M3 | 15 identical `clear_X_prop` one-liners + 10 trivial `free_X_prop` wrappers behind the `VIEW_PROP_TEARDOWN[]` table — store a member offset in the table row, keep the 4 non-trivial entries custom. Covered by memtrack/poison. | view_pool.cpp:160–486 | ~100 | med |
| M4 | `load_{text,markdown,wiki,latex,xml}_doc` share a ~30-line parse prologue (url→read→type-str→input_from_source→free) + a repeated katex/math CSS-load tail. Two helpers. Preserve mem_free ownership exactly. | cmd_layout.cpp:3870–4941 | ~115 | med |
| M5 | Trivial deletions: `radiant.cpp` (27-line legacy standalone main, excluded from build, superseded by lambda/main.cpp). | radiant/radiant.cpp | ~27 | very low |
| M6 | *(source-hygiene only — file not compiled)* webdriver_server endpoint boilerplate: ~20 handlers × ~8-line session/element lookup. Only worth doing if the server is kept (see D2). | webdriver/webdriver_server.cpp | ~150 | low |

### D. Decision-needed items (owner call, not scheduled)

| ID | Item | LOC | Question |
|---|---|---|---|
| D1 | `rdt_vector_cg.mm` — complete CoreGraphics vector backend, in `exclude_source_files`, referenced nowhere; but whole-file `__APPLE__`-guarded and edited as recently as Jul 6. | 1,337 | Intentionally-maintained alternate backend, or replaced by ThorVG for good? Delete or document. |
| D2 | `webdriver/webdriver_server.cpp` + `cmd_webdriver.cpp` — both excluded from build; live paths use stubs. | ~1,230 | Pending the lambda/serve HTTP router, or abandoned? |
| D3 | view_pool text dumper (`print_view_block` family, 881–1243) — debug-only (log + NDEBUG-gated side file); the JSON dump is the real test artifact. | ~360 | Is the human-readable dump still used for debugging? |
| D4 | render_svg border/text parity slice (P6) — needs PaintIR SVG lowering feature-parity work first. | ~140–215 | Schedule after P5 proves the descriptor table. |
| D5 | resolve_css_style logging slice beyond the macro (C6 full version). | up to ~900 | Debug-value vs LOC policy call. |

---

## 4. Phased execution plan

Each phase is a self-contained PR-sized change: **small enough to review, gated by the three checks in §2**. Order is strictly ratio-first — pure deletions, then mechanical extractions, then table-driving, then structural work. Later phases benefit from the confidence (and helper patterns) established by earlier ones.

Every phase ends with:

```bash
./utils/verify_loc_reduction.sh --ref <pre-phase-commit> <phase file list>   # must PASS
make build && make test-radiant-baseline                                    # must be 100%
<phase-specific suites>
```

### Phase 0 — Harness + pure deletions (~230 LOC, ~1 day, near-zero risk)
- Commit `utils/verify_loc_reduction.sh`; record `./utils/count_loc.sh` baseline.
- Delete: `radiant.cpp` (M5, 27), `calculate_initial_grid_extent` + call (L1, 40), `#if 0` block resolve_css_style.cpp:6065 (46), 5 no-op CSS cases (C5, 75), plus close ledger §5.5 as won't-fix (doc-only).
- Files: `radiant/radiant.cpp`, `radiant/grid_enhanced_adapter.hpp`, `radiant/resolve_css_style.cpp`.
- Tests: radiant baseline + `make layout suite=baseline` (grid + table fixtures cover L1/C5).

### Phase 1 — CSS mechanical helpers (~510 LOC, low risk)
- C1 `ensure_bound`/`ensure_border`/`ensure_blk` (~250) — mirror resolve_htm_style's accepted pattern.
- C2 4-side border width/style/color resolvers (~235).
- C4 css_animation name→id delegation (~28).
- Files: `resolve_css_style.cpp`, new `resolve_style_helpers.hpp` (or file-top statics), `css_animation.cpp`.
- Tests: radiant baseline + layout baseline (border fixtures). Behavior must be bit-identical; these are pure hoists.

### Phase 2 — CSS box-side unification (~200 LOC, low-med risk)
- C3 margin/padding/inset side + logical fan-out over one `resolve_box_side()`; extract `css_value_axis_type()`.
- ⚠ This one is *not* byte-neutral by construction: it deliberately fixes the TOP-vs-LEFT calc/isnan drift. Run layout baseline before/after and review any diffs as bug-fixes, not regressions. Preserve the hard-wired LTR/horizontal-tb mapping.
- Files: `resolve_css_style.cpp`.
- Tests: radiant baseline + full `make layout suite=baseline`; inspect positioned/margin fixture diffs individually.

### Phase 3 — Dumpers and loaders (~480–600 LOC, low-med risk)
- M2 view_pool JSON field helpers (~150–250), M1 selector builder ×4→1 (~115), M4 cmd_layout `load_*_doc` prologue + CSS-bundle helpers (~115).
- All output-facing: layout JSON snapshots must be **byte-identical** (the snapshot suite is the oracle).
- Files: `view_pool.cpp`, `cmd_layout.cpp`.
- Tests: radiant baseline + layout baseline + markdown/latex/xml layout suites.

### Phase 4 — Event/state/paint scaffolds (~270–330 LOC, low-med risk)
- E1 state-transition loggers 9→2 helpers (~90), E2 dispatch scaffold RAII (~65), E3 SM transition epilogue + hover≡active merge (~40), E4 shared editing-surface writer (~18).
- P1 form-control prologue (~60), P2+P3 background clip/tile + color-string (~80 combined).
- Files: `state_store.cpp`, `event.cpp`, `state_machine.cpp`, `editing_dispatch.cpp`, `event_state_log.*`, `render_form.cpp`, `render_background.cpp`, `render_svg.cpp`, `paint_ir.cpp`.
- Tests: radiant baseline + `make editor-4c-js` + `make editor-4c-view` (golden event/state logs) + form/background/border-radius layout fixtures.

### Phase 5 — Layout targeted unification (~440 LOC, med risk)
- L2 table border getters 5→1 (~80), L3 baseline walkers 3→1 (~300), L4 flex justify onto `compute_space_distribution` (~60).
- Files: `layout_table.cpp`, `layout_flex.cpp`, `layout_alignment.*`, `grid_baseline.hpp`.
- Tests: radiant baseline + full layout baseline (flex 494 / grid 344 / table 703 fixtures); L3 diffs reviewed case-by-case — baseline math is subtle.

### Phase 6 — Descriptor tables (~200–320 LOC, med risk)
- P4 `DL_OP_LIST` descriptor driving storage serialize/deserialize/free + retained dup/free + bounds (**not** the replay-core merge) (~80–140).
- P5 paint-op descriptor table folding validate/bounds scaffolding and bringing `render_pdf.cpp`'s two switches into the table (~120–180).
- Files: `display_list.h`, `display_list_storage.cpp`, `retained_display_list.cpp`, `display_list_bounds.cpp`, `paint_ir.h/cpp`, `render_pdf.cpp`.
- Tests: radiant baseline + retained/tiled parity + SVG-export + PDF fixtures — **all three lowering backends diffed**.

### Phase 7 — Structural (open-ended; start only after 0–6 hold)
Ordered slices, each independently gated:
1. L6a grid `repeat()` expansion: one implementation shared by grid_placement.hpp and both intrinsic_sizing call sites (self-contained, duplicated 3×).
2. E5 event_sim onto shared hit-test/radio/text helpers (~75).
3. L5 flex height walker onto the intrinsic API (~120–250).
4. L6b/c flex-line packing and table column contributions shared between intrinsic pass and real layout (largest remaining mass, ~800–1500 total for L6; HIGH risk — every slice runs the full layout suite).
5. P4b replay-core merge (the deferred §5.1 half), P6 SVG border/text parity — only after descriptor tables are proven.
6. E6 snapshot accessor delegation, L7 table iterator extraction — opportunistic.

### Running totals

| Milestone | Est. net LOC removed (cumulative) |
|---|---|
| Phase 0 | ~230 |
| Phases 0–2 | ~940 |
| Phases 0–4 | ~1,700–1,870 |
| Phases 0–6 | ~2,300–2,600 |
| + Phase 7 completed | ~3,300–4,600 |
| + D1/D2/D3 decided "delete" | up to ~7,500 (≈3.8% of radiant) |

The honest headline: **~2,300–2,600 LOC is high-confidence** (mechanical, golden-test-gated, phases 0–6); the rest depends on structural work (intrinsic sizing above all) and three owner decisions. Equally important, the descriptor tables (Phase 6) change the *slope*: today a new paint op touches 5–6 switches and a new DL op touches 11 — after Phase 6, one table row each.

---

## 5. Rules of engagement

1. **Never move-and-call-it-deleted.** The LOC gate lists new helper files alongside shrunk ones; the total must go down.
2. **Byte-identical first.** Phases 0, 1, 3, 4 must produce identical goldens/snapshots. Phase 2 (drift fix) and Phase 5 (baseline unification) may change outputs — each diff is reviewed as an intentional fix.
3. **One phase, one PR, all three gates.** No phase starts until the previous one is merged and its `count_loc.sh` total recorded.
4. **Per CLAUDE.md:** root-cause comments at any behavior-affecting fix point (the C3 drift fix in particular); `log_debug` not printf; floats in layout code; no `std::` containers in new helpers — use `lib/` types.
5. **When a finding dies on contact** (the code turns out to be load-bearing policy, not duplication), record it in §1.1 above rather than forcing the refactor — the negative list is what keeps future audits cheap.

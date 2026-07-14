# Radiant Coherent Global Headers — Implementation Plan

**Date:** 2026-07-13
**Baseline commit:** `028a75323`
**Parent design:** `vibe/Lambda_Design_Code_Dedup.md` DD4 (coherent module headers; ban per-C-file headers). This doc is the radiant execution plan for DD4.
**Related:** `vibe/radiant/Radiant_Impl_Clean_Up.md` (LOC-reduction phases; coordinate, see §8).

**Goal:** collapse radiant's **140 headers** (105 of them 1:1 per-`.cpp` mirrors) into **5 coherent global headers** plus a short justified-exception list, so that each domain has exactly one place to look for existing structs and functions — the glossary/API-doc/design-space role of DD4.

**Implementation status (2026-07-14): H0-H6 complete; H7 pending.** Every task in the original coherent-header plan is done. Radiant now has 19 headers against the permanent 24-header cap. The five coherent headers own their planned domains; retained grid implementation headers are marked internal; absorbed-header references have been migrated across `doc/dev/radiant/RAD_*.md`; and both `AGENTS.md` and `CLAUDE.md` point contributors at the coherent surfaces. Final H0-H6 verification: `make build` passed, `make test-radiant-baseline` passed all 6,221 required tests, `make lint ARGS='--rule ^no-new-per-file-header$'` passed at 19/24 headers, and `make check-radiant-dup` reported 0 remaining filtered duplicate blocks. The final `count_loc.sh` snapshot records 162 Radiant C/C++ files and 186,975 lines. H7 is a new post-plan phase for the two partially reduced implementation issues; it does not reopen any H0-H6 task.

---

## 1. Target state

**Governing principle (user, 2026-07-13):** `view.hpp` holds what *describes* the view/style tree (data model); `layout.hpp` is the *layout process*; `render.hpp` is the *render process*; `event.hpp` is event/editing/state *handling*. When a header's home is ambiguous, ask "is it tree description or process?" — description → view, process → its process header.

Five global headers (per user decision — style folds into view; edit+state fold into event; shell header named `radiant.hpp`):

| Global header | Domain | Seed today (LOC / fan-in) |
|---|---|---|
| `view.hpp` | describes the view/style tree: DOM/view structs, **+ CSS/style resolution, fonts, transforms, clip shapes, form-control model** (no separate `style.hpp`) | exists — 1,535 / 93 (already the de-facto base header) |
| `layout.hpp` | all layout engines: block/inline/flex/grid/table/multicol/positioned, intrinsic sizing, counters, caching | exists — 848 / 52 |
| `render.hpp` | paint IR, display list, painters, backends (raster/SVG/PDF), vector/video/image players, tiles | exists — 101 / 30 (thin seed) |
| `event.hpp` | events, hit-testing, state machine, **+ editing, ranges, text control, clipboard, + state store/schema/log** (no separate `edit.hpp`/`state.hpp`) | exists — 132 / 8 (thin seed) |
| `radiant.hpp` | app shell: window, surface, UI context, browsing session, script runner, webview, frame clock; also the **single-include external API** for embedders | new |

**Naming decision (`shell.hpp` rejected):** `radiant.hpp` — recommended. It reads as "the engine's own header", matches the top-level `lambda.h` precedent, and naturally doubles as the one header external consumers (`lambda/main.cpp`, module bridges) include: it declares the shell surface and `#include`s the other four. No filename conflict: `radiant/radiant.cpp` (27-line dead main) is deleted by Impl_Clean_Up Phase 0.

End-state header count: **5 global + ~20 justified exceptions ≈ 25** (from 140). The exceptions (§3.3): `webdriver/webdriver.hpp` (own module), 2 platform handles, `rdt_video.h` (platform media ABI; see H1 note), ~9 header-only implementation files, `event_sim.hpp` (test feature, kept separate), and the 6 graph headers (left out of this migration entirely — graph layout is expected to migrate to Lambda script rather than stay in C+, so consolidating its C+ headers now would be wasted work).

What this buys (per DD4): one greppable surface per domain for agents and humans; the header as reviewable API doc; declaration changes become visible single-point edits. Direct LOC saving is secondary but real: ~120 deleted header files × ~8–15 lines of guard/include/banner ceremony, plus include-list dedup in 136 `.cpp` files ≈ **~2,000–2,500 LOC**.

---

## 2. Current inventory (measured at baseline)

139 headers in `radiant/` + 1 in `radiant/webdriver/`. Full size/fan-in census taken 2026-07-13 (reproduce: count `#include "X"` occurrences across `radiant/ lambda/ test/`). Shape of the problem:

- **Fan-in is a power law.** `view.hpp` 93, `layout.hpp` 52, `state_store.hpp` 49, `form_control.hpp` 35, `render.hpp` 30, `display_list.h` 26, `font.h` 26, `layout_box.hpp` 25 … then a long tail of **~95 headers with fan-in ≤ 9**, mostly the private mirrors.
- **34 headers are ≤ 30 LOC** — pure ceremony around 1–5 declarations (`render_text.hpp` is 5 lines for 1 function; `layout_text.hpp` 7; `render_glyph.hpp` 5; `display_list_replay.hpp` 11…).
- **External couplings that must keep working during migration:** `lambda/js` includes 15+ radiant headers (`view.hpp`, `state_store.hpp`, `dom_range.hpp`, `editing_*.hpp`, `text_control.hpp`, `clipboard.hpp`…); `lambda/main.cpp`, `lambda/network`, `lambda/input`, `lambda/format` include a handful; `test/` gtests include ~25 directly.
- **Header-only implementations exist** (code, not declarations): `grid_sizing_algorithm.hpp` (1,477), `grid_enhanced_adapter.hpp` (1,279), `grid_placement.hpp` (616), `grid_track.hpp` (518), `grid_occupancy.hpp` (414), `glyph_sampling.hpp` (259), `render_effect_raster_fallback.hpp` (128), `render_glyph_run_raster_lower.hpp` (80). These must NOT be merged into globals (they'd bloat every TU); they stay as internal impl headers (§3.3).
- **Dead header found during the census:** `grid_baseline.hpp` (262 LOC) has **zero includers** anywhere in `radiant/ lambda/ test/` and no build-config reference. Delete in H0 (after a symbol-level check).
- Style-resolution API today is declared in `layout.hpp` (e.g. `resolve_css_styles` at layout.hpp:607) and `css_temp_decl.hpp` — it moves to `view.hpp` in H4 per the style→view decision.

---

## 3. Design rules

### 3.1 Layering (include direction is one-way, downward)

```
radiant.hpp   (shell; may include everything below)
   ├── event.hpp    (needs view + layout results + state)
   ├── render.hpp   (needs view + layout)
   └── layout.hpp   (needs view)
          └── view.hpp   (base: DOM/view/style/font types; includes only lib/ + system)
```

- A global header may include globals **below** it, never above. Upward references use forward declarations.
- `event.hpp` and `render.hpp` are siblings — neither includes the other (event handlers that need paint types take forward-declared pointers).
- `view.hpp` stays self-contained over `lib/` — it is the ABI floor everything shares.

### 3.2 What goes where

- **Global header:** every struct, enum, and function that more than one `.cpp` (or any external consumer) uses. Organized in clearly-banded sections (`// ===== flex layout =====`) so the header stays greppable — order sections to mirror the old file names for easy history tracing.
- **Stays `static` in the `.cpp`:** genuinely file-local helpers. The migration must NOT promote statics wholesale — only declarations that are already in some header move.
- **Internal impl headers (exceptions):** header-only algorithm code included by 1–3 `.cpp` files. Naming convention: keep the file, add a banner `// internal implementation header — do not include outside radiant/` (rename to `*_impl.hpp` is optional/deferred; renames churn history for little gain).
- **No new per-file headers** in `radiant/` once H1 lands — enforced by a structural lint rule (§7).
- All five globals are `.hpp` (C+ convention). The few `.h` files being absorbed (`font.h`, `paint_ir.h`, `display_list.h`, `animation.h`, …) merge into the `.hpp` targets; any C-ABI declarations keep their `extern "C"` blocks inside the target.

### 3.3 Full disposition map (all 140 headers accounted for)

**→ `view.hpp` (H4)** — absorb 8: `css_animation.h`, `css_temp_decl.hpp`, `symbol_resolver.h`, `font.h`, `font_face.h`, `clip_shape.h`, `transform.hpp`, `form_control.hpp` (form-control *model* describes the view tree; the paint/behavior code stays declared in render/event); plus the style-resolution declarations currently in `layout.hpp`.

**→ `layout.hpp` (H2)** — absorb 28: `layout_abs_children` `layout_alignment` `layout_axis` `layout_box` `layout_cache` `layout_containing_block` `layout_counters` `layout_debug` `layout_flex` `layout_flex_measurement` `layout_flex_multipass` `layout_grid_multipass` `layout_guards` `layout_list` `layout_measure` `layout_mode` `layout_multicol` `layout_pass` `layout_percentages` `layout_positioned` `layout_table` `layout_table_caption` `layout_table_metadata` `layout_text` (all `.hpp`/`.h`), `intrinsic_sizing.hpp`, `available_space.hpp`, `grid.hpp`, `grid_types.hpp`.

**→ `render.hpp` (H1)** — absorb 50: all 33 thin `render_*.hpp` (`backend` `backend_caps` `background` `border` `clip` `columns` `composite` `effects` `export_support` `filter` `form` `geometry` `glyph` `img` `list` `media` `output` `overlay` `paint_block` `paint_boundary` `paint_gateway` `painter` `path` `pdf` `profiler` `raster` `rect` `selection` `state` `svg` `svg_inline` `text` `vector_path` `video`), `paint_ir.h`, `display_list.h`, `display_list_bounds.hpp`, `display_list_storage.hpp`, `display_list_surface_region.hpp`, the 8 tiny `display_list_replay*.hpp`, `retained_display_list.hpp`, `retained_fields.hpp`, `tile_pool.h`, `rdt_vector.hpp`, `gif_player.h`, `lottie_player.h`, `video_frame_wake.h`, `stacking_order.hpp`. `rdt_video.h` declarations are mirrored into `render.hpp`, but the narrow header stays as an exception for platform media backends.

**→ `event.hpp` (H3)** — absorb 21: `event_state_log.hpp`, `handler.hpp`, `editing.hpp`, `editing_controller.hpp`, `editing_dispatch.hpp`, `editing_geometry.hpp`, `editing_host.hpp`, `editing_intent.hpp`, `editing_target_range.hpp`, `dom_range.hpp`, `dom_range_resolver.hpp`, `text_edit.hpp`, `text_control.hpp`, `clipboard.hpp`, `context_menu.hpp`, `scroller.hpp`, `state_store.hpp`, `state_machine.hpp`, `state_schema.hpp`, `source_pos_bridge.hpp`, plus seed `event.hpp`. (`state_store_internal.hpp` stays internal; `event_sim.hpp` stays separate — see exceptions. Note: most of the state_store C+ code is expected to eventually migrate to Lambda script, so the merge treats its declarations as a clearly-banded, removable section rather than interleaving them.)

**→ `radiant.hpp` (H5)** — absorb 5 + new decls: `browsing_session.h`, `script_runner.h`, `webview.h`, `frame_clock.h`, `animation.h`; plus collected declarations for `window.cpp` / `surface.cpp` / `ui_context.cpp` (today scattered or extern'd — this is where they get a proper home).

**Stay (justified exceptions, ~19):**
- `webdriver/webdriver.hpp` — separate module, already coherent.
- Platform: `webview_handle_mac.h`, `webview_handle_linux.h` (included only by their platform files); `rdt_video.h` (platform media ABI). H1 discovered the root cause: macOS AVFoundation/CoreGraphics headers define a global `Rect`, so AVFoundation `.mm` sources cannot include `render.hpp`/`view.hpp`, where Radiant's own global `Rect` is declared. The declarations are still present in `render.hpp` for render consumers, guarded by `RADIANT_RDT_VIDEO_API`.
- Header-only impl: `grid_sizing_algorithm.hpp`, `grid_enhanced_adapter.hpp`, `grid_placement.hpp`, `grid_track.hpp`, `grid_occupancy.hpp`, `glyph_sampling.hpp`, `render_effect_raster_fallback.hpp`, `render_glyph_run_raster_lower.hpp`, `state_store_internal.hpp` — banner-marked internal. (Longer-term these are DD5 candidates to become `.cpp` + methods; out of scope here.)
- `event_sim.hpp` (428 LOC) — a *testing* feature, not runtime; stays separate from `event.hpp`. If it is later kept in release builds (WebDriver support), it can be merged then.
- Graph family — **left out of this migration**: `graph_dagre.hpp`, `graph_edge_utils.hpp`, `graph_layout_types.hpp`, `graph_theme.hpp`, `graph_to_svg.hpp`, `layout_graph.hpp` stay as-is. Graph layout is expected to migrate from C+ to Lambda script; consolidating its headers now would be churn on code with a limited C+ lifespan.

**Delete (dead):** `grid_baseline.hpp` (262 LOC, zero includers) — H0, after grepping its symbol names to confirm no copies live elsewhere.

Tally: 8 + 28 + 50 + 21 + 5 = 112 absorbed/deleted after migration, + 1 dead, + 5 seeds grown in place, + ~20 kept (incl. 6 graph + event_sim + `rdt_video.h`) = 140 accounted.

---

## 4. Migration mechanics (identical for every phase)

Each phase converts one domain in four mechanical steps, each independently green:

**Step A — consolidate.** Move the text of each absorbed header into the target global header (banded section, content unchanged; drop per-file `#pragma once`/guards/banners; keep any `extern "C"`). Order sections roughly by old filename. Resolve duplicate forward-decls/macros as they surface (pre-scan per §7.3).

**Step B — shim.** Each absorbed header's file becomes a 2-line shim:
```cpp
#pragma once
#include "render.hpp"   // consolidated (DD4); shim removed once includers migrate
```
Build + tests must be green here. Shims mean nothing outside the phase breaks — including `lambda/`, `test/`, and any in-flight branch.

**Step C — rewrite includers.** Mechanical, scripted (§7.2): every `#include "old_name.hpp"` in `radiant/ lambda/ test/` → `#include "render.hpp"` (path-adjusted for non-radiant files), then collapse duplicate includes per file. Commit is 100% include-line churn — trivially reviewable.

**Step D — delete shims.** Gate: `grep -rl` proves zero includers per shim; delete the files. Run the full phase gate (below).

**Phase gate (all must pass before the next phase starts):**
```bash
./utils/verify_loc_reduction.sh --ref <pre-phase-commit> <deleted headers…> <target header> <touched .cpp list>
make build && make test-radiant-baseline        # 100%
<domain suite>                                  # per-phase, see §6
time make clean-all && time make build          # compile-time delta recorded in commit msg (§8 risk 1)
make lint                                       # incl. the new structural rule after H1
```
Note the LOC gate lists the *grown* target header alongside the deleted ones — consolidation must still net negative (ceremony + duplicate decls removed).

---

## 5. Phases

Order is deliberately **leaf-first**: render (self-contained consumer, worst shredding — 51 headers) proves the pattern where mistakes ripple least; `view.hpp` (fan-in 93, everything rebuilds when it changes) goes second-to-last, once the process is routine; `radiant.hpp` last because it depends on all four.

### ✅ H0 — Prep (complete)
- Delete `grid_baseline.hpp` (dead).
- Add `utils/rewrite_includes.sh` (§7.2) and the structural lint rule `no-new-per-file-header` in **warn** mode (flips to error after H1).
- Record baselines: header count (140), `time make clean-all && time make build`, `./utils/count_loc.sh`.

### ✅ H1 — `render.hpp` (complete; 51 headers → 1)
- Largest header count, almost fully internal (external exposure only via `render_export_support.hpp` → `lambda/js`, `lambda/main.cpp`; and 6 display-list gtests in `test/`).
- Extra care: `paint_ir.h`/`display_list.h` merge must preserve the `PAINT_OP_LIST`/`DL_` macro machinery verbatim (Impl_Clean_Up P4/P5 build on it).
- Domain suite: radiant baseline + display-list/retained gtests + one SVG-export and PDF fixture run.
- Exit review: compile-time delta, section layout readability, agent-greppability spot-check. **Go/no-go for the rollout pattern.**

### ✅ H2 — `layout.hpp` (complete; 28 headers → 1)
- Keep the 5 grid impl headers internal (banner them in this phase). Graph headers untouched (left out — future Lambda migration).
- Domain suite: radiant baseline + `make layout suite=baseline` (flex 494 / grid 344 / table 703 fixtures).

### ✅ H3 — `event.hpp` (complete; 21 headers → 1)
- The biggest content merge (~4k decl LOC; `state_store.hpp` alone is 1,409). Use strong section bands: `event / editing / ranges / text / state store / state machine / logging`. Keep the state-store band cleanly separable — most of that C+ code is expected to migrate to Lambda script later, and its band should lift out without touching the rest.
- `event_sim.hpp` stays a separate header (test feature); its includes are untouched.
- Heaviest external rewrite: `lambda/js` includes many of these; `test/test_state_store_stubs.cpp` and editor gtests too.
- Domain suite: radiant baseline + `make editor-4c-js` + `make editor-4c-view` + state-store/dom-range/source-pos gtests.
- **Exit deliverable: a structuring rewrite pass over the merged `event.hpp`** (user-requested) — once all pieces are in, reorder/regroup the bands for readability as a separate, declaration-only commit. This is the one phase where "move text verbatim" is followed by a deliberate reorganization.

### ✅ H4 — `view.hpp` absorbs style (complete; 8 headers + declarations from layout.hpp)
- Move `resolve_css_styles`/`resolve_css_property` (+ `css_temp_decl.hpp` machinery) from `layout.hpp` into a `// ===== style resolution =====` band in `view.hpp`; absorb fonts/transform/clip/css_animation/symbol_resolver/form_control (form-control *model* belongs to the view tree per the governing principle).
- Highest rebuild blast radius (fan-in 93) but by now the process is proven; content risk is low (pure moves).
- Domain suite: radiant baseline + layout baseline + css_animation gtest.

### ✅ H5 — `radiant.hpp` (shell) + external-API cleanup (complete)
- Create `radiant.hpp`: shell declarations (absorb the 5 shell headers; write proper decls for window/surface/ui_context) + `#include` of the other four globals.
- Rewrite external consumers (`lambda/main.cpp`, `lambda/module/radiant/*`, `lambda/network`, `lambda/input`, `lambda/format`) to include `radiant/radiant.hpp` (or the specific global where the umbrella is overkill — importer's choice, both are sanctioned).
- Domain suite: full `make test` (baseline + extended) once, since this touches every consumer surface.

### ✅ H6 — Lock-in (complete)
- Flip `no-new-per-file-header` to **error**; add header-count ratchet (≤ 24) to `make lint` (tightens further when the graph family migrates to Lambda script).
- Update `doc/dev/radiant/RAD_*.md` references and `CLAUDE.md`/`AGENTS.md` Key Entry Points table (both files, per convention).
- Retro: compile-time trend, final `count_loc.sh`, and `make check-radiant-dup` re-scan (the filtered duplicate count should tick down from the removed duplicate declarations).

### H7 — Residual implementation dedup [PENDING]

The original header-consolidation work is complete. This new phase records the two implementation-level issues that were reduced by the related LOC cleanup but are not yet fully converged:

1. **Intrinsic sizing vs real layout.** Intrinsic width/height paths now share many declaration probes, classifiers, box-model helpers, and intrinsic grid `repeat()` counting, but they still mirror portions of the real grid, flex, and table algorithms. Pending work is to share narrow per-engine contribution cores, beginning with grid track-list/repeat expansion and proceeding in separately gated flex and table slices.
2. **Paint/display-op maintenance surface.** `PAINT_OP_LIST` and `DISPLAY_OP_LIST` now centralize identity and common flags, and common validation/ownership/preflight/replay shapes consume them. Backend- and payload-specific switches remain, so adding an op still touches multiple sites. Pending work is to move only genuinely common traits into descriptors, add coverage for unhandled ops, and document irreducible dispatch. A generic per-op lowering function table remains rejected unless a concrete implementation strictly reduces aggregate LOC.

The detailed work breakdown and completion gates live in [`Radiant_Impl_Clean_Up.md` Phase 8](Radiant_Impl_Clean_Up.md#phase-8--residual-architectural-convergence-pending). Every H7 slice must strictly reduce aggregate LOC and pass the relevant build, Radiant baseline, layout, raster, SVG, PDF, retained-list, and tiled-replay gates.

The H0-H6 effort estimates were planning inputs and are retained only in history; all six implementation phases are complete. H7 is estimated and scheduled independently, one regression-gated slice at a time.

---

## 6. What this does NOT do (scope fences)

- **No behavior change, no code motion between `.cpp` files, no static promotion.** Only declarations move, only between headers. (Static promotion happens organically later under CLAUDE.md/AGENTS.md rule 13, and via Impl_Clean_Up phases.)
- **No renames of structs/functions** — grep continuity matters more than naming polish during the migration.
- **No conversion of header-only grid/render impl to `.cpp`** — that's DD5 territory, tracked separately.
- **`lambda/` header consolidation** — separate follow-up plan after radiant proves the pattern (DD4 named `lambda/format/` as its pilot).

---

## 7. Tooling

### 7.0 Filtered Lizard duplicate scan
`test/dedup/check_code_dup.py` is the canonical duplicate-code report. It runs Lizard, passes generated-file exclusions directly to Lizard, filters reviewed false-positive blocks through marker-based rules in `test/dedup/exclude.json`, and reports only the remaining blocks. Every exclusion records its affected modules and why sharing the detected shape would be incorrect or less maintainable. See [`test/dedup/Readme.md`](../../test/dedup/Readme.md) for CLI usage and exclusion maintenance.

Use `make check-radiant-dup` for this plan's Radiant measurement. `make check-lambda-dup` scans Lambda, while `make check-code-dup` scans `lib/`, `lambda/`, and `radiant/`; direct script invocation also accepts any combination of those module names. The report is not yet a failing threshold gate, so ratchet decisions compare its filtered `Remaining duplicate blocks` count.

### 7.1 Structural lint rule — `no-new-per-file-header`
In `utils/lint/rules/structural`: flag any *new* `radiant/foo.{h,hpp}` whose basename matches a sibling `radiant/foo.{c,cpp,mm}`, and any `radiant/*.{h,hpp}` not on the allow-list (5 globals + §3.3 exceptions). Warn during H1–H5, error from H6.

### 7.2 `utils/rewrite_includes.sh`
```bash
# usage: rewrite_includes.sh <target-header> <old-header>...
# rewrites #include "<old>" -> #include "<target>" across radiant/ lambda/ test/,
# fixing relative paths for non-radiant files, then dedupes repeated includes per file.
```
Implementation: `grep -rl` per old header over `radiant lambda test` (extensions `.c .cpp .h .hpp .mm`), `sed -i ''` replace (both `"old.hpp"` and `"../radiant/old.hpp"` forms), then an awk pass removing consecutive duplicate include lines. ~30 lines; written in H0, exercised 100+ times, so the per-phase rewrite commits are mechanical and reproducible.

### 7.3 Pre-merge collision scan (per phase, before Step A)
One-liner ritual: concatenate the phase's headers, then check for duplicate `#define` names, duplicate `inline`/`static inline` function names, and duplicate forward declarations with mismatched signatures. Collisions get resolved in Step A (usually: keep one copy; occasionally reveals a genuine drift bug — record those in the phase notes).

### 7.4 Ratchets (permanent, from H6)
- Header count in `radiant/` ≤ 24 (`make lint`).
- The DD1 Lizard dup-ratchet, measured by `make check-radiant-dup`, continues unchanged; this plan should only ever move it down.

---

## 8. Risks & mitigations

1. **Compile time.** Every TU now parses bigger headers (worst case: a leaf `.cpp` that included 2 tiny headers now sees `event.hpp`'s ~4.5k decl lines). Measured at every phase gate (§4). Radiant TUs already average `view.hpp`+`layout.hpp` ≈ 2.4k lines, so the expected delta is a fraction of system-header cost. If clean-build regresses >15%: enable a precompiled header for the radiant project via `build_lambda_config.json` → premake `pchheader` (the five globals are the natural PCH), never by re-splitting.
2. **Include cycles.** The §3.1 layering forbids them structurally; the merge surfaces any latent cycle immediately as a build error in Step A — resolve with forward declarations (the plan's one permitted content edit).
3. **Conflicts with in-flight work** (Impl_Clean_Up phases, feature branches). Shims (Step B) keep old includes working indefinitely, so other branches merge cleanly; only Step C/D commits conflict, and those are include-line-only — rebase is `git checkout --theirs` + re-run `rewrite_includes.sh`. Coordinate: don't run Step C/D of a domain while a large PR touching that domain is open.
4. **Hidden include-order dependencies.** A `.cpp` may compile today only because header A was included before B. Consolidation fixes order permanently (single header), but Step A can break a TU that relied on *not* seeing a declaration (name shadowing). These surface as compile errors — fix in place; they're latent bugs regardless.
5. **`event.hpp` size** (~4k lines). Accepted by design decision (edit+state fold in; state_store merge explicitly approved). Mitigations: strict section bands; `state_store_internal.hpp` and `event_sim.hpp` stay out; the H3 exit includes a user-requested structuring rewrite of the merged header; and the state-store band is kept lift-out-able since most of that code is expected to migrate to Lambda script later.
6. **Test stubs.** `test/*_stubs.cpp` files include fine-grained headers to compile subsets of radiant. Shims keep them green; Step C rewrites them; if a stub deliberately avoided pulling a subsystem in, the consolidated header may force new stub symbols — add stubs, don't re-split headers.

---

## 9. Decision record (all resolved by user, 2026-07-13)

- **RQ1 — shell header name: `radiant.hpp`** (user rejected `shell.hpp`, proposed `radiant.hpp`).
- **RQ2 — graph family: left out of the migration.** No `graph.hpp` consolidation, no fold into `layout.hpp`. Rationale: graph layout may eventually be migrated to Lambda script instead of being coded in C+ — consolidating its C+ headers now would be wasted churn.
- **RQ3 — `event_sim.hpp`: stays separate** from `event.hpp`. It is a testing feature, not a runtime feature. If it is later kept in release builds (for WebDriver support), it can be merged at that point.
- **RQ4 — `form_control.hpp`: → `view.hpp`.** Follows the governing principle: view.hpp holds what describes the view/style tree; layout/render hold their processes; event holds event/editing/state handling.
- **state_store merge: approved** into `event.hpp`, with two riders: (a) the merged `event.hpp` may need a structuring rewrite once all pieces are in, to keep it readable (scheduled as the H3 exit deliverable); (b) most of the state_store C+ code is expected to eventually migrate to Lambda script, so its band is kept cleanly separable.

---

## 10. Out-of-scope legacy dual-path audit backlog (added 2026-07-13; not H-phase tasks)

The H-phases move declarations only (§6 scope fences). The 2026-07-13 audit of the `doc/dev/radiant/RAD_*.md` known-issues sections also found live dual representations/code paths. This table preserves those audit findings for their owning subsystem plans; it is not part of the completed H0-H6 implementation scope and is not included in H7's two-item pending phase.

| # | Dual path | Canonical / legacy | Source | Action |
|---|---|---|---|---|
| DP1 | `DomElement::native_element` — the embedded `Element elmt` is a *copy* of the original parsed Element (dom_element_init), plus a redundant `native_element` pointer into it; two notions of "the element" to keep in sync | embedded element canonical; field carries `TODO(Phase 4): Remove` (dom_element.hpp:297–308) that never landed | RAD_01 §8, RAD_15 §10 | complete the Phase-4 removal: `dom_element_to_element()` accessor, delete the field |
| DP2 | Grid dual representation: legacy `GridTrack` structs ↔ enhanced (Taffy-style) track layer, bridged by a per-pass adapter round-trip (`grid_enhanced_adapter.hpp`), incl. the shrink-guard workaround the round-trip forces. (The dead "pure" `run_track_sizing_algorithm` flagged alongside it in RAD_09 was already removed — see grid_enhanced_adapter.hpp:826.) | enhanced layer is the live algorithm; legacy structs persist as its I/O | RAD_09 §8 | migrate remaining consumers to the enhanced layer; delete the adapter round-trip |
| DP3 | Selection/caret: canonical model + legacy `state_store` projection kept in sync by manual invariants | canonical model | RAD_17 §8, RAD_18 §10, RAD_19 §8 | migrate consumers off the projection, delete it — coordinate with the H3 rider (state-store band is Lambda-script-bound); the most-repeated dual-path issue in the RAD set |
| DP4 | Legacy `render_list_bullet` (formerly in render_list.cpp and called from render_block.cpp) alongside the `::marker` path | `::marker` | RAD_11 §7 | ✅ deleted; list pixels now come only from synthetic `::marker` views |
| DP5 | Legacy contenteditable key-handling path with unfinished stubs, superseded by the transaction path | transaction path | RAD_15 §10 | delete the legacy path |
| DP6 | text_edit dual dispatch surfaces: `te_replace_byte_range` vs `_no_events` (text_edit.hpp:90/96); `te_ime_commit` vs `_prepare`/`_finish` (:198/205/209) | one parameterized entry each | RAD_19 §8 | collapse each pair |
| DP7 | Raster render path only partway migrated onto the shared backend walker — two walkers live | shared backend walker | RAD_13 §10 | finish the migration; delete the raster-only walk |

Sequencing: DP4 is a deletion with tests already green — earliest candidate, and DP-work touches `.cpp` bodies not headers, so it can run parallel to the H-phases. DP1/DP2/DP3 are the real campaigns: DP3 after H3 (its declarations will live in `event.hpp`'s state band), DP2 after H2. DP1 crosses into `lambda/input/css/` — coordinate with the DOM-side owners before scheduling.

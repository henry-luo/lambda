# Radiant Robustness — Unsafe / Non-Robust Code Survey & Structural Fixes

**Date:** 2026-07-13
**Baseline commit:** `028a75323`
**Method:** four parallel code-survey agents — (1) type safety & casts, (2) buffer/string/numeric, (3) recursion depth & termination, (4) error handling & concurrency. Every finding verified against cited source (file:line). Scope excludes memory-allocation issues (covered by `vibe/radiant/Radiant_Design_Memory.md`).
**Related:** `vibe/radiant/Radiant_Design_Memory.md` (R1–R7, S-findings), `vibe/Memory_Safety_Review.md` (clean-abort doctrine), `vibe/radiant/Radiant_Fuzzy_Test.md` (fuzz harness), `radiant/layout_guards.h` (depth/node limits), `doc/dev/C_Plus_Convention.md`.

---

## 1. Executive summary

Radiant's robustness posture is **much stronger than a greenfield audit would expect** — the things a survey usually finds are already done:

- Raw view/DOM downcasts: **0 tree-wide** (a `lam::tagged.hpp` checked-cast family, ~1,270 uses, + 2 lint rules).
- Unsafe libc string calls: `sprintf` 0, `strcpy` 1 (audited), banned by `no-unsafe-libc-str` lint.
- Block-layout recursion: a single shared depth counter (`MAX_LAYOUT_DEPTH`) that all mutual recursion funnels through, transitively bounding every downstream walker.
- Tile-worker cache access: correctly mutex-guarded; font cache provably not worker-reachable.
- Array indexing in grid/table/multicol: consistently clamped; no div-by-zero in the sampled hot paths.

The real risk has therefore **shifted to five subtler structural gaps**, each a *class* of bug rather than a one-off, and each fixable at a choke point rather than by site-by-site patching:

| # | Gap | Class | Severity |
|---|---|---|---|
| G1 | `*_require` casts + invariant `assert`s vanish under `-DNDEBUG` | ~1,050 silent type-confusions + ~7 release null-derefs/infinite-loops | **HIGH** |
| G2 | NaN/Inf → `(int)` cast UB at layout→render boundary; render tier outside the int-cast lint | ~150 unguarded casts; UB on hostile CSS | **HIGH** |
| G3 | Recursion bypasses: intrinsic-measure walk, inline-SVG `<use>` cycles, DOM maintenance walks | stack overflow / infinite recursion on hostile input | **HIGH** |
| G4 | Stale-`View*` handle class: WebDriver staleness guard never called + internal DocState twin (T7) | dangling `View*` via external channel; internal caches guarded only by a manually-invoked prune | **MED** (WebDriver gated: external caller; DocState reachable today) |
| G5 | SVG-DOM picture raced across tiles; signal guard's cross-thread assumption; init/IO return checks | data race + fragile invariants + silent-wrong-output | **MED** |

Everything below is organized so the fixes converge on a **small set of shared mechanisms** (an always-on check macro, a finite-scrub choke point, one recursion-guard RAII, a resolver funnel, an IO-writer RAII) rather than hundreds of edits — matching the C+ direction of the other radiant plans.

---

## 2. Findings by category

### 2.1 Type safety (agent 1)

- **T1 [HIGH] — `*_require` casts are unchecked in release.** `lib/tagged.hpp` `*_require` helpers (`view_require_block` 295, `dom_require_element` 234, `view_require_element` 186, `dom_require<>` 118, `view_require<>` 116, `view_require_text` 51, `dom_require_text` 39 = **~1,039 sites**) use plain `assert()`; release defines `NDEBUG` (build_lambda_config.json:511), so each collapses to a bare `static_cast` with the tag check gone. Reviewers treat these as checked; the guarantee evaporates in production. The null-returning `*_as` family (~234 sites) is safe in all builds.
- **T2 [HIGH] — WebDriver staleness guard never called.** `element_registry_is_stale` (webdriver_session.cpp:94–105) exists but none of the ~10 handlers use it; each does `element_registry_get` → null-check → use, then `view_require_block` on the result (unchecked per T1). Navigation clears the registry (:236/:243), but in-place relayout that recycles view-pool memory leaves live entries pointing at reused storage. Only externally-reachable channel. (Server currently excluded from build — but the code is the shipping WebDriver surface when enabled.)
- **T3 [MED] — `unsafe_*` struct-punning invariant unenforced.** 49 `reinterpret_cast` sites (tagged.hpp:182–239) pun ViewBlock/ViewTable/ViewText on the premise "adds no fields." No `static_assert` ties them to that premise; the day any of those structs gains a field, all 49 silently corrupt.
- **T4 [LOW] — `reinterpret_cast` for plain upcasts** (view_pool.cpp:1331/1394, layout_positioned.cpp:564): should be implicit/`static_cast`; bypasses inheritance checking.
- **T5 [LOW] — `const_cast` on CssValue** (css_temp_decl.hpp:44/93/96, resolve_css_style.cpp:12807): stashes into a mutable temp decl; audit the write path for mutation of shared/cached CssValue.
- **T6 [LOW] — cast-lint file allowlist is hardcoded** (no-radiant-view-cast-*.yml): new files unprotected by default; `reinterpret_cast` not matched.
- **T7 [MED] — DocState transient caches hold raw `View*`/`DomElement*` guarded only by a manually-invoked prune.** (Added 2026-07-13 from the RAD_17 known-issues audit — previously owned by neither this doc nor the memory doc.) The memory doc's V3 verifies the *machinery* (`doc_state_detach_transient_owner`, `clear_dom_view_pool_pointers`, `state_store_prune_after_reflow` state_store.cpp:3144) is correct — but nothing enforces the *call*: the prune has a single call site (event.cpp:4634), and any new/refactored reflow path that skips it leaves stale views to be dereferenced on the next state read. Internal twin of T2 — same stale-handle class, internal channel, and unlike T2 it is reachable today.

### 2.2 Buffer / string / numeric (agent 2)

- **N1 [HIGH] — NaN/Inf → `(int)` cast UB at layout→render.** Render/event tier has 0 `isnan/isfinite` guards but ~150 `(int)` casts of float coordinates (event.cpp 54, render_background.cpp 43, render_raster.cpp 18, render_img.cpp 12, paint_ir.cpp 9, render_text.cpp 8, tile_pool.cpp 6, surface.cpp 3). Example render_raster.cpp:157–160: `(int)x0` where x0 can be NaN (from `0/0` transform, transform.hpp:143 unguarded `denom`) or out-of-range (`calc(1e30px)`). `(int)` of NaN/out-of-range float is UB. Layout clamps only isolated cases (resolve_css_style.cpp:5403 text-indent ±2²⁵); no general scrub.
- **N2 [HIGH] — int-cast lint scoped to only 19 layout files** (run_tidy.sh:134–146); render*.cpp, event.cpp, paint_ir.cpp, surface.cpp, render_raster.cpp, tile_pool.cpp entirely outside it — the enforcement gap that let N1 accumulate.
- **N3 [MED] — integer overflow in pixel-buffer sizing.** surface.cpp:986–988 `pixel_width * pixel_height * 4` in `int`; ~30000² overflows INT_MAX → under-allocation → heap overrun. No max-dimension clamp anywhere. Also surface.cpp:1008, render_img.cpp:116. Mitigated-by-accident today (the untrusted-image path uses non-allocating `image_surface_create_from`; the allocating path gets internal sizes) — but the invariant is undocumented and the `*4` inflation is what happens to save it.
- **N4 [MED] — selector-build truncation silently ignored** (view_pool.cpp four sites ~1989/2157/2439/3124): `snprintf` into `[256]`/`[512]` from untrusted tag+class; truncation return unchecked → wrong WebDriver selector (correctness, not overflow).
- **N5 [LOW] — `strncpy` non-terminated, safe only by adjacent `{}`-init** (render_svg_inline.cpp:2551/2865, `family` from untrusted CSS); a refactor dropping the init reintroduces an over-read.
- **N6 [LOW] — SVG color alpha unclamped** (render_svg_inline.cpp:686–692): `(uint8_t)(a*255)` with NaN/huge `a` from `%f` → cast UB.
- **Verified good:** array indexing clamped (grid_positioning.cpp:484, layout_table.cpp:3048/3061 span validation), multicol column_count forced ≥1 at every division, viewBox sscanf guards divisor>0, UTF-8 caret handling correct (byte offsets, explicit UTF-16↔8 helpers).

### 2.3 Recursion & termination (agent 3)

- **R-block [VERIFIED GOOD]** — `lycon->depth`/`MAX_LAYOUT_DEPTH` is a genuine single choke point; all block/flex/grid/table/multicol/inline mutual recursion funnels through `layout_flow_node`; positioned uses its own guarded entry; MAX_FLEX_DEPTH/MAX_GRID_DEPTH (RAII `GridDepthGuard`)/MAX_IFRAME_DEPTH all present and balanced. **Load-bearing transitive property:** views aren't created past depth 300, so the view tree is capped, which is what bounds every render/paint/event/dump walker even though none has its own guard. (Undocumented — a refactor building views outside `layout_flow_node` silently reopens the whole class.)
- **R1 [HIGH] — intrinsic-measure walk unguarded.** `measure_element_intrinsic_widths` (intrinsic_sizing.cpp:1970) and `calculate_max_content_height` (:5122) recurse over DOM depth with **no depth cap** (only a per-element re-entrancy flag that catches cycles, not linear depth). Invoked at *shallow* `lycon->depth` as measurement subroutines (layout_block.cpp:373/416 shrink-to-fit/auto-height), so the 300 guard never fires. A 10k-deep chain of `float:left`/`inline-block`/table cells → stack overflow in 300-line frames. The sibling `measure_content_height_recursive` *does* cap at 32 (layout_flex_measurement.cpp:396) — these two were just never given it.
- **R2 [HIGH] — inline-SVG `<use>`/`<g>` recursion unguarded + no intra-doc cycle detection.** render_svg_inline.cpp mutual recursion (render_svg_element ↔ group/use_target ↔ children) has no depth counter; the cycle guard `svg_resource_stack_contains` (cap 32) is consulted **only** on the external-file path (:4609), not the intra-doc `#id` branch (:4835). `<use id=a href=#b>`/`<g id=b><use href=#a>` → infinite recursion.
- **R3 [MED] — DOM maintenance walks unguarded:** `clear_cascaded_styles_recursive` (event.cpp:5166, runs *before* layout guard exists) and `view_teardown_visit_node` (view_pool.cpp:566). Small frames, so build-dependent, but genuinely unbounded. The parallel `apply_inline_styles_to_tree` *is* capped (MAX_CSS_TREE_DEPTH=512) — the pattern exists.
- **Termination [VERIFIED GOOD]:** the `while(true)`/`for(;;)` census all terminate (grid fr-sizing freezes ≥1 track/iter over ≤64 tracks; flex multipass bounded by item counts; ancestor loops capped 64/200/256). Text zero-width-glyph loop class not disproven but no advancing-by-zero loop surfaced — flagged for a follow-up read.

### 2.4 Error handling & concurrency (agent 4)

- **E1 [HIGH] — inconsistent assert/abort policy** (overlaps T1). 15 `assert`, 0 `abort` in radiant. Debug-only validation blocks (state_schema/state_machine under `#ifndef NDEBUG`) are fine, but real-invariant guards vanish in release: `assert(cell->td)` then immediate deref (layout_table.cpp:3027→3029) = release null-deref; `assert(text_rect->next != text_rect)` (event.cpp:857) = release infinite loop; negative-dimension asserts (layout_positioned.cpp:993/1126, layout_block.cpp:6289) flow downstream. Inconsistent with the clean-abort doctrine.
- **E2 [MED] — SVG-DOM picture raced across tiles.** `svg_dom_picture_draw` (rdt_vector_tvg.cpp:1905) re-renders a shared `RdtPicture` (shared `pic->pool`, shared element tree) into `render_svg_to_display_list`; if an inline-SVG image spans multiple tiles, N workers call it concurrently on the same `pic`. Reachable in the parallel branch (SVG shapes don't set outer `has_glyphs`). Potential data race pending a read of the inner pool usage.
- **E3 [MED] — signal guard cross-thread assumption.** JS crash/timeout handlers are well-built (async-signal-safe, guarded) but installed process-wide with a single page-thread `sigjmp_buf`; safe only because script exec and tile rendering never overlap temporally — undocumented fragile invariant. Plus recover-and-continue paints on a possibly-corrupt heap after a caught SIGSEGV (documented graceful-degrade-into-undefined).
- **E4 [MED] — `hashmap_set` return ignored everywhere** (cmd_layout.cpp:2130+, layout_counters.cpp:183/350/397): silent-wrong-output class (wrong list numbering, missed dedup), no logging.
- **E5 [LOW] — `pthread_create` unchecked** (tile_pool.cpp:239): failure → garbage thread handle → crash at `pthread_join` teardown. frame_clock.cpp:357 is the correct pattern to copy.
- **E6 [MED] — clipboard paste sanitizer is a minimal stub** (clipboard.cpp:385): strips only `<script>`/`<style>` by substring; `onerror=`/`onclick=`, `<svg><script>`, `javascript:` URLs survive. Gap if pasted HTML can execute handlers.
- **E7 [LOW] — `g_picture_font_ctx` unsynchronized** (rdt_vector_tvg.cpp:84) written once/read by workers, no barrier; `fwrite`/`fclose` returns unchecked in dumps (I3); no `../` path normalization (U3).
- **E8 [MED] — LayoutCache invalidation is coarse and style-keyed only.** (Added 2026-07-13 from the RAD_04 known-issues audit.) Per-element measurement caches (`LayoutCache`, layout_cache.hpp:92) are cleared only when *that element's* styles are re-resolved (layout.cpp:872); nothing ties an entry's validity to descendant mutations, so a DOM/content change that dirties a subtree without re-resolving an ancestor's style can serve that ancestor a stale cached measurement — silent-wrong-layout, the same output class as E4. The invalidation contract ("valid only until any descendant changes") is nowhere stated in layout_cache.hpp.
- **Verified good:** picture caches mutex-correct on every path incl. eviction; PNG/JPEG writers check fopen + setjmp + fclose on all returns; GIF/Lottie buffer aliasing generation-guarded.

### 2.5 Fuzz-coverage gap (agent 4 §6)

The harness (`test/fuzzy`, `test_layout_fuzzy_gtest.cpp`) drives **only** `lambda.exe layout → JSON`. **0% covered:** the render/paint/display-list replay pipeline; the parallel tile path (`render_threads>1` — where E2 lives); image/vector/animation decoders (GIF/Lottie/video/TVG/PNG/JPEG) and the hand-rolled TrueType cmap walker (render_svg_inline.cpp:2697); the script crash-guard recovery path; the clipboard sanitizer. The higher-severity findings here live in exactly the unfuzzed surfaces. Existing deep-nesting seeds only exercise the *guarded* layout paths (so R1/R2 are untested).

---

## 3. Structural fixes

Six shared mechanisms cover almost every finding. Each is a choke point or a uniformly-applied C+ construct — not a site-by-site patch.

### F1 — `RADIANT_CHECK`: an always-on invariant guard (fixes T1, E1; underpins T2)
A macro that in **all** build modes evaluates its condition and, on failure, `log_error`s + controlled-`abort()`s (clean-abort doctrine, `vibe/Memory_Safety_Review.md`). Then:
- In `lib/tagged.hpp`, replace the `assert()` in every `*_require` helper with `RADIANT_CHECK(tag == expected)`. One header edit converts ~1,039 silent release type-confusions into deterministic, diagnosable crashes — zero call-site churn.
- Convert the ~7 real-invariant asserts (layout_table.cpp:3027, event.cpp:857, layout_positioned.cpp:993/1126, layout_block.cpp:6289, resolve_css_style.cpp:3890) to `RADIANT_CHECK`.
- Keep bare `assert` for pure debug diagnostics (the `#ifndef NDEBUG` validation blocks stay).
- Consider a `RADIANT_CHECK_DEGRADE(cond, action)` variant for spots where clean degradation beats abort (e.g. skip a malformed subtree). Decide per site; default to abort for memory-safety invariants, degrade for content invariants.

### F2 — Finite-scrub choke point at layout→render (fixes N1, N6; N2 enforces it)
- Add a single ingestion point — the display-list record path / paint_ir command construction — that runs `isfinite()` on every coordinate/dimension and clamps to a sane range (reuse the ±2²⁵ constant already in resolve_css_style.cpp:5404), with a debug `assert(isfinite(x))` to catch producers. Kills the whole `render_raster.cpp:157` class at one site instead of auditing 150 casts.
- Guard the NaN *sources* too: `transform.hpp:143` inverse `denom==0`, SVG alpha clamp (N6).
- **Enforcement (N2):** extend the `int-cast-type-aware` lint scope (run_tidy.sh:134–146) to render*.cpp, event.cpp, paint_ir.cpp, surface.cpp, render_raster.cpp, tile_pool.cpp. Expect a wave of `INT_CAST_OK` markers — each becomes an audited truncation, and casts downstream of the scrub point are provably finite.

### F3 — One `RecursionGuard` RAII, applied uniformly (fixes R1, R2, R3)
- Introduce `struct RecursionGuard { int& counter; ~RecursionGuard(){ counter--; } }` with a `try_enter(counter, limit)` factory (the shape `GridDepthGuard` at layout_grid_multipass.cpp:162 already proves). Promote all limits into `layout_guards.h` (unify the stray `MAX_MEASURE_DEPTH=32` local).
- Apply to: the two intrinsic-measure functions (R1 — thread a measure-depth counter, cap and bail; highest ROI), inline-SVG render (R2 — add `SvgInlineRenderContext::render_depth` cap 32) **plus** mirror `svg_resource_stack_contains` into the intra-doc `#id` branch for cycle detection, and the two DOM walks (R3).
- Eliminates the decrement-on-every-return-path bug hand-rolled counters invite (layout.cpp already has six `depth--` sites).
- Document the depth-300 view-tree-truncation transitive property in `layout_guards.h` so it can't be silently reopened.

### F4 — Stale-handle funnels: WebDriver + DocState (fixes T2, T7)
- Add `wd_resolve_element(session, id, &out)` that runs `is_stale` → returns `WD_ERROR_STALE_ELEMENT_REFERENCE` before yielding the `View*`; route all ~10 handlers through it. Lint-enforceable: ban direct `element_registry_get` outside the resolver (ast-grep). Also bump the registry version on in-place relayout, not just navigation.
- **Internal twin (T7):** apply the same funnel discipline to DocState's transient view caches — stamp DocState with a relayout generation (bumped in the same place the element-registry version bumps) and have the transient-view accessors `RADIANT_CHECK` (or degrade to a fresh lookup) on generation mismatch, so a reflow path that skips `state_store_prune_after_reflow` fails deterministically instead of dereferencing recycled view-pool memory. Longer-term, key the caches by node `id` like the WebDriver registry (the RAD_17/RAD_23 docs propose the same shape) — that removes the raw pointers entirely; the generation stamp is the cheap interim that makes the missed-prune bug loud.

### F5 — `IoWriter` RAII + status convention for init/IO (fixes E4, E5, I3; hardens init chains)
- Generalize the exemplary `render_output_render_tiled_png` pattern (checked fopen + setjmp + fclose-on-every-return) into an `IoWriter` RAII wrapper (checked open/write/close, error as status, auto-close on scope exit); adopt for state dumps, event logs, font-file reads.
- Make `hashmap_set` failures at least `log_error` (E4 — decide degrade-vs-abort; these are correctness-affecting).
- Check `pthread_create`, track started count, join only started (E5 — copy frame_clock.cpp:357).

### F6 — Documented threading contract + SVG-DOM race fix (fixes E2, E3, E7)
- Write the "worker-visible state" contract: display list + borrowed buffers immutable for the dispatch window; `render_pool_dispatch` stays synchronous (main thread parked). Add a debug assert that no worker pool is active while `js_exec_guarded` is set (E3).
- Fix E2: either serialize `KIND_SVG_DOM` draws with a per-picture mutex, or pre-flatten SVG-DOM pictures to an immutable paint list at record time so workers only replay (no shared-pool re-render). Prefer pre-flatten — it removes the shared mutable state rather than locking it.
- Add a set-once barrier/assert for `g_picture_font_ctx` (E7).

### F7 — Hardening odds-and-ends (T3, T5, T6, N3, N4, N5, E6)
- **T3:** `static_assert(sizeof(ViewBlock)==sizeof(ViewSpan))` etc. beside each `unsafe_*` helper — breaks the build when the punning premise fails.
- **T6:** convert both cast-lint rules from hardcoded file lists to `radiant/**` glob + exclusions, and extend them to match `reinterpret_cast<(View|Dom)…>` (catches T3/T4 outside tagged.hpp).
- **N3:** route surface/image sizing through `lib/checked_math.hpp` (`__builtin_mul_overflow`) + an explicit max-dimension clamp (~16384, browser-like); compute in `size_t`.
- **N4/N5:** adopt the lib `Str`/`StrBuf` tier in the selector builders (drop the `[256]`/`[512]` pair, detect truncation); add an ast-grep rule flagging `strncpy` not followed by explicit NUL-termination.
- **T5:** audit the `CssTempDecl` write path for mutation through the cast-away-const pointer.
- **E6:** replace the substring paste stripper with an allow-list DOM sanitizer (strip event-handler attrs, `javascript:`/`data:` URLs, embedded `<script>`) before pasted HTML reaches script dispatch.
- **E8:** state the LayoutCache invalidation contract in layout_cache.hpp, and clear ancestor caches when a subtree is dirtied (the RAD_04 dirty-propagation proposal). Add a debug/fuzz-mode cross-check: on cache hit, re-measure and `assert` equality with the cached result — turns a silently-served stale measurement into a diagnosable failure.

### F8 — Fuzz corpus + target extensions (fixes §2.5)
- **New corpus seeds** (drive R1/R2/N1/N3): `crash_float_deep_nesting.html` (10k nested `float:left`), `crash_inline_block_deep_nesting.html`, `crash_table_cell_deep_nesting.html`, `crash_svg_use_cycle.html`, `crash_svg_deep_g.html`, CSS `calc(1e30px)`/`calc(0/0)` on transformed elements, images with pathological declared dimensions, `viewBox`/`rgb()` with NaN/Inf, huge class names for selector builders.
- **New fuzz targets** (past layout→JSON): full render-to-raster with `render_threads>1` (tiling + E2), malformed GIF/Lottie/PNG/JPEG/TVG + malformed fonts through the decoders and the cmap walker, inline JS driving the crash-guard recovery path, clipboard paste payloads.
- Demote the `state_schema.cpp` `assert(false)` undeclared-transition asserts to log-only under a fuzz flag so debug fuzzing is viable.

---

## 4. What the survey confirmed is already solid (don't re-audit)

- Raw view/DOM downcasts (0 tree-wide), the `no-radiant-view-cast-*` + `no-unsafe-libc-str` lint rules, `sprintf`=0.
- The block-layout depth-guard cluster and its transitive view-tree-truncation bound.
- Tile-worker picture-cache mutex coverage (verified on every path); font cache provably not worker-reachable; parallel path gated on `!has_glyphs`.
- Array-index clamping in grid/table/multicol; div-by-zero guards on column/track/viewBox divisors.
- UTF-8 caret/selection handling (byte offsets + explicit UTF-16↔8 conversion).
- PNG/JPEG file writers (checked fopen/setjmp/fclose); GIF/Lottie generation-guarded buffer aliasing.
- The tagged-union access discipline (DisplayItem/PaintCmd/RdtEvent all switch-on-tag first).

---

## 5. Priority & sequencing

Ranked by exploitability under untrusted HTML/CSS/image/font input:

1. **F1** (`RADIANT_CHECK`) — one header edit neutralizes the largest silent-failure class (T1) and the release null-derefs (E1); do first, it also backstops everything else.
2. **F2** (finite-scrub + lint scope) — closes the top *numeric* UB class (N1) at one choke point.
3. **F3** (RecursionGuard) — closes the three stack-overflow bypasses (R1/R2/R3), the top *crash-on-hostile-input* class.
4. **F8** (fuzz) — lands alongside F2/F3 so the fixes are regression-tested where coverage is currently 0%.
5. **F4** (stale-handle funnels), **F6** (threading contract + SVG-DOM race) — the WebDriver half of F4 and F6 are gated on external caller / parallel rendering (do before those surfaces ship broadly); F4's DocState generation stamp (T7) is reachable today and can land with F1.
6. **F5, F7** — breadth hardening; fold into the relevant C+ class waves (`Radiant_Impl_Mem_Mgt.md` P4/P5) and the header/lint work.

### Lint-enforceable mechanically (extend `utils/lint`)
`reinterpret_cast<(View|Dom)…>` ban; cast-rule file-list → glob; `element_registry_get` outside resolver; `strncpy`-without-terminator; int-cast scope extension; (soft) `assert(` on a real-invariant pattern once `RADIANT_CHECK` exists. **Not** mechanically enforceable: staleness liveness (F4 forces routing, not liveness), the CssValue const-cast mutation question, the threading contract — these need the F1/F4/F6 constructs + review.

### Coordination
- F1's `RADIANT_CHECK` is a prerequisite for the abort-policy decision in `Radiant_Impl_Mem_Mgt.md` P0.5 (alloc_prop NULL) — build it here, reuse there.
- F3/F5/F6 land as the C+ Shape-B (RAII) and Shape-A constructs defined in `Radiant_Design_Memory.md` §5 — same idioms, so do them in/after the memory plan's P4/P5.
- Fuzz seeds join the harness that the memory plan's `--mem-dump` CI step also feeds.

---

## 6. Can Rust-level safety be asserted on this codebase? (assessment)

Short answer: **no lint can replicate Rust's core guarantee** — the borrow checker is a compile-time *flow* analysis, and C++ tooling checks *state* (patterns, tags, counters), not flow. `vibe/Memory_Safety_Review.md` already fixed this ceiling: discipline-not-proof, clean-abort on violation. But the *outcome* can be approximated by four layers, most of which radiant already has or this doc plans.

### 6.1 Guarantee mapping

| Rust guarantee | Mechanism | Closest radiant equivalent | Gap |
|---|---|---|---|
| No use-after-free / dangling | borrow checker (compile-time) | arenas + pools + generation handles (image `generation`, retained-DL resource validation, gen-handles doctrine) — runtime *detection*, + ASan/fuzzing | detection, not proof |
| No null deref | `Option<T>` + exhaustive match | null-returning `*_as` casts, `RADIANT_CHECK` (F1), clean-abort | opt-in, not type-enforced |
| Tagged-union safety | `enum` + exhaustive `match` | already solid (switch-on-tag verified, §2.4-unions); `-Wswitch`, `PAINT_OP_COUNT` asserts | compiler warns, doesn't force |
| Bounds checking | slices, panics | clamped-index helpers (F7), fuzz + ASan | per-site adoption |
| Data-race freedom | `Send`/`Sync` (compile-time) | architecture: single-threaded page thread, immutable display lists to workers, threading contract (F6), TSan | by design + dynamic, no static proof |
| No integer/cast UB | checked by default | `checked_math.hpp`, finite-scrub choke point (F2), UBSan | opt-in |

### 6.2 The four layers

1. **Pattern lint** — R1–R7 (memory doc) + this doc's F-rules. Cheap, deterministic, catches construct classes; ceiling reached quickly ("state, not flow").
2. **Flow-aware static analysis** — the genuine partial answer:
   - **clang-tidy + Clang Static Analyzer** (`bugprone-*`, `clang-analyzer-core.*`, `cppcoreguidelines-*`): path-sensitive; catches intra-procedural UAF/null-deref/double-free flows. Integration point already exists — `utils/lint/tidy/` + `run_tidy.sh` already run a clang-based checker (int-cast rule), so adding a curated check subset is incremental.
   - **CodeQL**: interprocedural dataflow/taint queries — the only tool that can *assert* a flow property over the whole tree (e.g. "no value from `image_get_dimensions` reaches an allocation without a clamp"; "every `View*` reaching a WebDriver handler passed through `wd_resolve_element`"). Run as a periodic deep-audit tier alongside the L3 LLM sweeps, not per-commit. (CodeQL already noted as prior art in the Unified-AST doc §11.)
   - **Clang `-Wlifetime`** (lifetime profile): the one true borrow-checker approximation for C++; still experimental/noisy — track, don't adopt yet.
3. **Runtime enforcement + fuzzing** — the practical C++ substitute for the borrow checker: ASan/UBSan/TSan under the fuzz harness (F8 extends it to the 0%-covered surfaces), `memtrack_poison_dirs`, debug epochs, `RADIANT_CHECK` turning violations into deterministic aborts. Probabilistic where Rust is total — but on an untrusted-input engine, fuzzer+sanitizers empirically find most of what a borrow checker would reject.
4. **Architecture — the genuinely Rust-equivalent part.** Even Rust cannot express a DOM tree with parent/child/sibling pointers under the borrow checker; Rust codebases handle exactly this class with **arenas + indices + generational handles**, opting out of borrow checking. Radiant already uses that idiom (pools/arenas, generation counters, single-writer threading, immutable data to workers) — what's missing is not the architecture but the *enforcement* that nobody bypasses it, which is precisely what the lint layers (R7 allowlist, cast rules, F1) stand in for.

### 6.3 Practical recommendation (adopt)

- **Add a curated clang-tidy/CSA profile to `make lint`**, ratchet-style like the Lizard gate: `bugprone-use-after-move`, `bugprone-dangling-handle`, `clang-analyzer-core.*`, `cppcoreguidelines-init-variables`, plus checks matching the F-fixes. Start warn, baseline the count, ratchet down.
- **CodeQL as a quarterly deep scan** paired with the L3 LLM sweeps — flow assertions (F4's resolver funnel, F2's clamp-before-alloc) become checkable queries instead of review conventions.
- Do **not** chase `-Wlifetime` or GSL annotations yet; revisit when the lifetime profile stabilizes.

Reference point: this combined stack — pattern lints + CSA + fuzzing farm + generational handles — is operationally the same bar Chrome and Firefox's C++ cores run (cf. Chrome's `raw_ptr`/MiraclePtr = generation-handle mitigation). The ceiling stays discipline-not-proof; the floor rises to "violations are deterministic, diagnosable, and fuzz-discoverable."

---

## 7. Enhancing the fuzz harness to cover the observed issues

### 7.1 What exists today

`test/fuzzy/radiant/test_fuzzy_radiant.sh` orchestrates generative + mutational fuzzing: 46 Python generators (`generators/html_gen.py`) + a mutator (`html_mutator.py`), seeded from `test/layout/data`, results triaged into `results/{crashes,timeouts,slow,badjson}`. It already has flex/grid/table/block/float/text/inline/position/deep-nesting/contradiction generators, a `calc()` value pool with some extremes (`calc(100%/0.001)`, `calc(999999px…)`), a `gen_svg_inline_stress`, and CSS-variable indirection.

**The one structural limit:** every path runs exactly `lambda.exe layout <html> -o out.json` (runner line 157). So the corpus only exercises HTML→CSS→layout→**JSON serialization**. The higher-severity findings in this doc live *downstream of layout* — in render/paint, the parallel tile path, decoders, the recursion bypasses that layout-to-JSON doesn't reach, and the crash-guard recovery path. Those surfaces are **0% covered** regardless of how many HTML files are generated.

### 7.2 Two axes of enhancement

Coverage grows on two independent axes — **new inputs** (generators the existing runner picks up for free) and **new drivers** (running inputs through commands other than `layout→json`). Ordered to match finding severity.

#### Axis A — new generators (cheap; the runner already consumes them)

Each is a `gen_*` function added to `html_gen.py` + a mode string; no runner change. Targets the findings the current generators can't reach:

| New generator | Drives finding | Shape |
|---|---|---|
| `gen_intrinsic_depth_stress` | **R1** (top crash risk) | 5k–20k nested `float:left` / `display:inline-block` / table cells wrapping sized content — forces the *shrink-to-fit / max-content measure* recursion that bypasses `MAX_LAYOUT_DEPTH`. **Distinct from existing `gen_deep_nesting`, which uses plain blocks that don't trigger measurement.** |
| `gen_nan_coordinates` | **N1/N6** | `calc(0/0)`, `calc(1e30px)`, `calc(-1e30px)`, huge/negative `transform: scale/translate/matrix`, `rotate` with degenerate values — on `position:absolute`/`transform`ed elements so NaN/Inf reaches render-tier `(int)` casts. Add the extreme literals to the shared `calc`/length pools so *all* generators sometimes emit them. |
| `gen_huge_dimensions` | **N3** | elements/images/SVG with intrinsic or declared sizes near `INT_MAX` (`width:2147483647px`, 30000×30000 SVG viewBox) to drive the pixel-buffer size multiply. |
| `gen_selector_bomb` | **N4** | tags with multi-KB `class`/`id` attributes (multibyte too) to overflow the 256/512 selector buffers and force silent truncation. |
| `gen_font_family_bomb` | **N5** | multi-KB, multibyte `font-family` strings hitting the `strncpy` paths. |

#### Axis B — SVG/`<use>` recursion (generator + confirms the inline-SVG path runs)

| `gen_svg_use_cycle` | **R2** | `<svg><use id=a href=#b/><g id=b><use href=#a/></g></svg>` and deep-`<g>` (10k) nesting — but see B-caveat: these only bite when inline SVG is *painted/measured*, so pair with a render driver (Axis C) or confirm `layout` walks inline-SVG intrinsic sizing. |

#### Axis C — new drivers (the real coverage expansion)

The generators are wasted on the downstream findings unless the inputs are run through more than `layout→json`. Add runner modes (a `--driver=` flag selecting the command per round):

1. **`--driver=render`** — `lambda.exe render <html> -o out.png` (and `-o out.svg`, `-o out.pdf`). Exercises the entire paint/display-list/raster pipeline where **N1** (NaN→int cast), the decoders, and SVG painting (**R2**) actually execute. *Highest-value single addition* — it turns the existing 46 generators into render fuzzing for free.
2. **`--driver=render --threads=N`** (env `RADIANT_RENDER_THREADS=4` or the equivalent flag) — runs the parallel tile path, the only way to reach the **E2** SVG-DOM cross-tile race. Combine with `gen_svg_inline_stress` sized to span multiple tiles.
3. **Malformed-asset corpus** — a sibling generator dir emitting *broken binary assets* (truncated/bit-flipped GIF, PNG, JPEG, Lottie JSON, TTF/cmap tables) referenced from a host HTML, run under `--driver=render`. Reaches the decoders and the hand-rolled TrueType cmap walker (render_svg_inline.cpp:2697) — currently untouched by HTML-only fuzzing. The mutator (`html_mutator.py`) already knows how to bit-flip; point a variant at asset bytes.
4. **`--driver=script`** — HTML with adversarial inline `<script>` (infinite loops, deep recursion, huge allocations) to drive the SIGSEGV/SIGALRM crash-guard recovery path (**E3/S2**) — verifying it recovers to a sane state rather than crashing/hanging.
5. **Clipboard paste payloads** (**E6**) — a small direct harness (gtest, not the HTML runner) feeding hostile HTML fragments through `clipboard_store_sanitize` and asserting scripts/handlers/`javascript:` URLs are stripped.

### 7.3 Sanitizer integration (multiplies every driver's value)

The current runner catches only hard crashes/timeouts/bad-JSON. Run the fuzz binary built with **ASan + UBSan** (and a periodic **TSan** pass for Axis C.2):

- **UBSan is what makes N1 observable** — `(int)NaN` is silent UB that produces garbage but rarely crashes; UBSan's `float-cast-overflow` check turns it into a reported error. Without UBSan, the NaN findings are nearly unfuzzable.
- **ASan** turns the R1/R2 stack overflows and any decoder heap-overrun (N3) into clean diagnosed reports instead of ambiguous SIGSEGVs.
- **TSan** on the `--threads=N` render path is the only automated way to confirm/refute the E2 race.
- Add a `--sanitizer={asan,ubsan,tsan,none}` runner flag selecting the pre-built instrumented `lambda.exe`; wire the three into the periodic (not per-commit) fuzz job.

### 7.4 One prerequisite

Debug fuzzing currently aborts on the **debug-only `assert(false)` undeclared-transition guards** in `state_schema.cpp` (§2.4/E1) — any adversarial state sequence trips them and masks real findings. Demote those to `log_error`-and-continue under a `RADIANT_FUZZ` compile flag (or land F1's `RADIANT_CHECK` with a fuzz-degrade mode first), so debug+sanitizer fuzzing is viable.

### 7.5 Priority

1. **Axis C.1 (`--driver=render`) + ASan/UBSan (7.3)** — unlocks the entire downstream surface and makes N1 observable; single highest-leverage change.
2. **Axis A generators** — `gen_intrinsic_depth_stress` (R1) and `gen_nan_coordinates` (N1) first; cheap, no runner change, high severity.
3. **7.4 prerequisite** — needed before debug+sanitizer fuzzing is stable.
4. **Axis C.3 malformed assets + C.2 threaded render** — decoder and E2 coverage.
5. **C.4 script recovery, C.5 clipboard, remaining Axis A** — breadth.

These map 1:1 onto F8 in §3 — this section is the build-out detail for that fix. Each new seed that reproduces a finding should also become a **regression fixture** (a deterministic file under `test/layout/data` or a gtest case) so the fix stays fixed, mirroring the existing `view_pool.cpp:2985` "fuzzer-found" guard.

---

## 8. Implementation note

This document is the **design/findings** record. A phased implementation plan (analogous to `Radiant_Impl_Mem_Mgt.md`) should be written next if approved — the natural phases are: RB0 F1 (check macro + convert asserts), RB1 F2 (scrub + lint scope), RB2 F3 (recursion guards + cycle detection), RB3 F8 (fuzz corpus/targets), RB4 F4/F6 (external + concurrency), RB5 F5/F7 (breadth), each gated by `make test-radiant-baseline` + the fuzz run + `make lint`.

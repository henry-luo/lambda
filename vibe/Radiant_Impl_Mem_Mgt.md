# Radiant Memory Management Refactor — Implementation Plan

**Date:** 2026-07-13
**Baseline commit:** `028a75323`
**Parent design:** `vibe/radiant/Radiant_Design_Memory.md` — the policy (R1–R7), the safety findings (S1–S10, V1–V5, font gaps), the lifetime-role matrix (§2), and the C+ class catalog (§5). This doc turns that into execution-grade tasks.
**Related:** `vibe/Memory_Context.md` (factory/registry), `vibe/radiant/Radiant_Imp_Code_Dedup.md` (header consolidation H1–H6 — class declarations land in the 5 global headers), `vibe/radiant/Radiant_Impl_Clean_Up.md` (LOC phases), `doc/dev/C_Plus_Convention.md`.

---

## 0. Ground rules (apply to every task)

1. **Gates per PR:** `make build` · `make test-radiant-baseline` 100% · `make layout suite=baseline` for layout-touching tasks · `make editor-4c-js && make editor-4c-view` for event/state tasks · `./lambda.exe layout <file> --mem-dump` leak report clean · `make lint` green.
2. **Every leak/UAF fix ships with a regression test** in the same PR (see §8 test plan). A fix without a test that fails-before/passes-after is not done.
3. **Root-cause comment at every fix point** (CLAUDE.md/AGENTS.md rule 12) — state the invariant being protected, not what the code does.
4. **Never "fix" V1–V5** (design §4): pool-only destroy of backdrop pools, shared-arena scratches, the detach-transient-owner machinery, the doc/view-tree pool aliasing guard, and the clipboard/log/undo/media patterns are verified correct. P1.6 documents them in-code so they survive future reviews.
5. Bug-fix phases (P0) use the **smallest mechanical change**; structural elegance comes later via the class phases (P4/P5). Do not combine a bug fix and a refactor in one PR.
6. Phases are ordered by dependency, but tasks *within* a phase are independent PRs and can interleave with other work.

---

## P0 — Safety fixes (bugs first; no restructuring)

### P0.1 — S1: session navigation leaks the previous DomDocument  **[HIGH, active leak]**
- **Fix location: the three session functions**, not the callers — `session_navigate` (browsing_session.cpp:152–155), `session_go_back` (:203–206), `session_go_forward`. Each currently does only `radiant_cleanup_network_support(old_doc)`. Add old-document disposal mirroring the non-session path at event.cpp:8225–8227: capture `old_doc`, load the new document, on success `free_document(old_doc)` (it already handles JS-wrapper invalidation, timer abandonment, embed images, state teardown — ui_context.cpp:249).
- **Care:** (a) reload-to-same-URL still produces a *new* DomDocument, so unconditional free of the old pointer is correct — but assert `old_doc != ui_context->document` before freeing; (b) free only on successful load — on failure the old document must stay presented; (c) `window.cpp:369–378` (`show_html_doc`) must not also free — ownership belongs to exactly one place; add a comment there pointing at the session functions.
- **Test:** new event_sim/gtest scenario `session_navigate_no_leak`: navigate A→B→back→forward ×50; assert via memtrack counters (or `mem_context` snapshot count of live document contexts == 1 ± the session cache policy) that documents don't accumulate. This is the fails-before test.

### P0.2 — S2: `layout_cleanup` skipped on early returns  **[HIGH]**
- layout.cpp:3056–3058 and :3062–3064 `return;` after `layout_init` (:3040-ish) without reaching `layout_cleanup` (:3102). Minimal fix: replace the two `return`s with `goto cleanup;` (or hoist the two validity checks *above* `layout_init` — inspect whether they depend on init state; if not, hoisting is the cleaner minimal fix).
- P4.1's `LayoutPassScope` later makes this class of bug impossible; this task just stops the bleeding.
- **Test:** feed a document whose root fails the node_type check; assert no `MEM_KIND_SCRATCH` node remains registered and counter-context allocations are freed (mem-dump snapshot before/after).

### P0.3 — S3: PaintList ownership not enforced by owner  **[HIGH, latent]**
- Move the owned-payload free loop from the PDF-local `pdf_effect_raster_fallback_clear` (render_pdf.cpp:114–169) into `paint_list_clear` and `paint_list_destroy` (paint_ir.cpp:42–55): iterate cmds, free `owns_path`/`owns_stops`/`owns_text` payloads, then reset count / free cmds. Delete the PDF-local helper; its call sites call plain `paint_list_clear`.
- Verify SVG's borrow-only lists are unaffected (`owns_*` never set on that path — render_svg.cpp:149/240/277/1806).
- Also in this PR (same file, S10b): **delete the dead `PaintList.arena` field** (paint_ir.cpp:39, paint_ir.h:423) and its two inconsistent initializers (render_output.cpp:168, render_svg.cpp:1711–1712) — `cmds` is heap-grown; the field only invites a future "it's arena-backed" corruption.
- **Tests:** PDF render fixtures + display-list gtests; add one gtest that builds a PaintList with `owns_*` payloads and asserts memtrack balance after `paint_list_destroy`.

### P0.4 — S4: `radiant_state_reset` latent use-after-reset  **[MED, dead code]**
- **Delete it** (state_store.cpp:2131–2141 + header decl). Zero callers verified; a reset that must coordinate live_ranges/dom_selection/next_range_id/arena is exactly what `DocState::reset()` (P5.3) should own if ever needed — reintroduce it there, correctly, on demand. Deleting is safer than fixing dead code.

### P0.5 — S8: `alloc_prop` NULL-dereference class  **[MED]**
- **Policy decision (recommended): failure is fatal.** `pool_calloc` failing mid-layout means the pool is corrupt or the process is OOM — per the clean-abort doctrine (`vibe/Memory_Safety_Review.md`), continuing risks silent corruption. Change `alloc_prop` (view_pool.cpp:615–623) to `log_error` + abort on NULL instead of returning NULL. One change; the 85+ unchecked callers become correct by contract. Document the contract in the function comment.
- Alternative (rejected): null-check every caller — 85+ edits, high miss risk, and no caller has a sane recovery path anyway.

### P0.6 — S9: FloatBox pool-less fallback has no free path  **[LOW]**
- block_context.cpp:262–264. First determine reachability: grep/trace whether `BlockContext` is ever constructed with `pool == NULL` in the live tree. If unreachable → replace the fallback with an assert (and root-cause comment). If reachable → route the fallback to `lycon->scratch` instead of `mem_calloc`.

### P0.7 — S10c: InputIntent dispose audit  **[LOW]**
- Only the paste path allocates (`editing_intent.cpp:145–151`); event.cpp has ~10 intent build sites and 2 dispose calls. Audit every `insertFromPaste`/paste-adjacent branch; add missing `input_intent_dispose` calls. P4.3 (dtor) later makes the pairing automatic.

---

## P1 — Lint hardening (R3–R7)

All rules follow the existing ast-grep backend conventions (`utils/lint/rules/c-cpp/*.yml` + `manifest.yml` entry + suppress tag). Land each rule **error-clean**: fix the outstanding sites in the same PR.

### P1.1 — R3: `no-raw-create` (factory-only allocator creation)
- Patterns: `pool_create($$$)`, `pool_create_mmap($$$)`, `arena_create($$$)`, `arena_create_default($$$)`, `arena_create_sized($$$)`, `scratch_init($$$)`, `gc_nursery_create($$$)`. Scope `radiant/**`; suppress `RAWCREATE_OK`. Radiant is already at 0 — this pins it. (Extending scope to `lambda/**` is a follow-up once its remaining sites are converted; out of scope here.)

### P1.2 — R4: `no-open-realloc` + fix the 9 stragglers
- Patterns: `mem_realloc($$$)`, `pool_realloc($$$)`; scope `radiant/**`; suppress `REALLOC_OK`.
- Convert in the same PR: layout_flex_measurement.cpp:527, rdt_vector_tvg.cpp:160, font_face.cpp:365, cmd_layout.cpp:1438/1451 → `mem_grow_array`; cmd_layout.cpp:1104/1713/1788/1887 → `pool_grow_array` (12 sites already use lib/mem_grow.hpp — follow those).

### P1.3 — R5: fix `alloca-static-size` false negatives, then remediate the 9 sites
- Add patterns for member/expression sizes: `alloca($X->$F)`, `alloca($X->$F * $$$)`, `alloca($N * sizeof($T))` where `$N` is non-literal. Verify the rule now flags all 9 radiant sites.
- Remediate rather than suppress: gradient stops (render_background.cpp:337/485) and shadow counts (:1240/1415) come from parsed CSS — **attacker-influenced sizes; replace with `scratch_alloc` or a clamped stack buffer + heap fallback**. intrinsic_sizing.cpp:3277/3278/5853 (`num_columns`/`child_count`) and rdt_vector_cg.mm:701/702 — same treatment or an explicit bound check + `ALLOCA_OK` tag with the bound stated.

### P1.4 — R6: destroy-symmetry sweep
- view_pool.cpp:861–862/877–878 and dom_element.cpp:191/195: raw `pool_destroy`/`arena_destroy` on factory-created allocators → `mem_pool_destroy`/`mem_arena_destroy`. Mechanical; behavior identical (hooks already fire), but the convention stops depending on hook installation. Optional rule later; the P5 owner classes make it structural.

### P1.5 — R7 allowlist gate + R2 `OBJ_HEAP_OK`, both **warn mode**
- R7: `pattern: $FN($$$)` + `constraints: {FN: {regex: "(alloc|_free|dup|mmap|memalign)"}}`, allowlist of sanctioned prefixes (design §3 R7) maintained in the rule file. Warn for one milestone; flip to error once the allowlist stabilizes.
- R2 tag rule: typed-struct heap allocation shape `$T* $V = ($T*)mem_calloc($$$)` warns unless the line carries `// OBJ_HEAP_OK: <reason>`. Seed the existing legitimate heap roots (design §1.3: container structs, media players, singletons, FontFaceDescriptor) with tags in the same PR so the rule lands quiet.

### P1.6 — Document V1–V5 in-code
- Add invariant comments at: render_effect_raster_fallback.hpp:120, render_svg.cpp:1811, render_pdf.cpp:2237 (pool-only destroy is intentional — arena lives in the pool; cascade unregisters); retained_display_list.cpp fragment-scratch sharing; ui_context.cpp:276 aliasing guard. Cheap insurance against well-meaning "leak fixes" that would double-free.

---

## P2 — Standardize the stragglers (lifetime homes for orphans)

- **P2.1 flex measurement cache** (layout_flex_measurement.cpp:519–527): file-static `measurement_cache` + manual doubling → owned by `ViewTree` (per-document lifetime; entries reference that document's views), grown via `mem_grow_array`, registered under the document's mem_context. Invalidation on `view_pool_reset_retained` comes free.
- **P2.2 RdtPicture path/data/paint caches** (rdt_vector_tvg.cpp:89–273): hand-rolled mutex-guarded linked lists → `hashmap` (or lib LruCache **with eviction still disabled** — eviction only becomes safe after P6.2's refcount) owned by the vector-engine context, mem_context-registered. Keep the no-evict semantics bit-for-bit in this phase.
- **P2.3 tile memory** (tile_pool.cpp:54/71; render_output.cpp:309/324): per-tile `mem_calloc` loop → one sliced slab allocation per grid; per-frame `jobs` array → `RenderContext.scratch`.
- **P2.4 event_sim parse arena**: per-fixture arena; `SimEvent`s become arena allocations; delete the 81 `mem_free(ev)` early-return frees. Test-harness only — big LOC win, zero runtime risk.
- **P2.5 render_svg_inline** `style_rules[]` grow (:440–447) + `SvgDefTable`/resolver entries → `mem_grow_array` / the existing per-render subscene pool.
- **P2.6 Font gap F-a — register FontContext memory**: add optional factory hooks to `FontContextConfig` (nullable, same pattern as pool/arena release hooks) so radiant's `ui_context.cpp:164` creation registers the font pool + 2 arenas under mem_context; standalone lib tests pass NULL and build as today. Font memory becomes visible to `--mem-dump`.

---

## P3 — Per-pass layout objects onto scratch (makes R2 real)

Highest-care data-motion phase. The flex/grid pass objects move from `mem_calloc`/`mem_free` chains to `lycon->scratch` (mark/restore). Full layout suite (494 flex / 344 grid / 703 table) is the oracle for every task.

- **P3.1 `FlexLengthScratch` / `FlexIterationScratch`** (layout_flex.cpp:3701→4118, 3924→4030): direct conversion to `scratch_calloc`/`scratch_free` — the table/multicol code (layout_table_metadata.cpp:16–46, layout_multicol.cpp:945+) is the established in-tree pattern to copy. Low risk; do first.
- **P3.2 `FlexContainerLayout`** + `flex_items`/`lines` (layout_flex.cpp:167/556/558, destroy :563–571): item counts are known up-front (child count), so allocate exact-size on scratch — no growth-on-scratch needed. Nested containers: the `pa_flex` save/restore (layout_flex_multipass.cpp:496/844) already stacks pointers; scratch's LIFO discipline matches the nesting exactly — assert mark/restore pairing in debug.
- **P3.3 `GridContainerLayout`** + areas/line_names/items/track lists (layout_grid.cpp:31–110, destroy :118–173; grid_utils.cpp:43–94; grow sites :1005–1183): same move, plus the payoff — the five `owns_*` flags (S5) largely dissolve: scratch-owned copies are freed by mark/restore, pool-owned originals (`embed->grid`) are never freed by the pass. Whatever ownership logic remains gets centralized and commented. Growth sites use exact precomputed counts where derivable, else `mem_grow_array` on a scratch-boundary-safe buffer.
- **Exit check:** grep confirms zero `mem_calloc`/`mem_free` in layout_flex*.cpp / layout_grid*.cpp / grid_utils.cpp except tagged `OBJ_HEAP_OK` roots (expected: none).

---

## P4 — C+ wave 1: RAII scope classes (Shape B; behavior-neutral)

Declarations land in `layout.hpp` / `render.hpp` per the header plan; if an H-phase hasn't run yet for that domain, declare in the existing header and let the H-phase move it.

- **P4.1 `LayoutPassScope`** (design §5 sketch): ctor = `layout_init`, dtor = `layout_cleanup`. Replace the init/cleanup call pair in `layout_html_doc` (and any other callers of layout_init — grep first). Retires P0.2's goto. S2-class bugs become unrepresentable.
- **P4.2 `RenderFrameScope`**: ctor/dtor = `render_output_init_context`/`render_output_cleanup_context` (render_output.cpp:155/195); the stack `DisplayList` + `dl_init`/`dl_destroy` pair (:370/460) folds in as a member.
- **P4.3 `InputIntent` dtor** = `input_intent_dispose`; delete the manual dispose calls; S10c pairing becomes automatic.
- **P4.4 Flex/Grid pass scopes**: wrap P3.2/P3.3's mark/restore in `FlexLayoutScope`/`GridLayoutScope` objects (ctor takes `lycon`, marks scratch + saves `pa_flex`/`pa_grid`; dtor restores both). `BlockContextScope` (block_context.cpp:795) is the in-tree precedent — cite it as the house pattern in the class comments.
- **Rule for the wave:** zero logic changes — ctors/dtors call the existing functions verbatim. Diff = call-site mechanics only.

---

## P5 — C+ wave 2: owner classes (Shape A)

Sequenced **with** the header-consolidation phases (each domain's classes land after or inside its H-phase to avoid double churn). Mechanics per class: free functions become methods (bodies unchanged); `extern "C"` shims kept where tests/JIT/external C callers exist; create/destroy pairs become `init()/destroy()`; allocator create+destroy calls move inside, fixing R6 structurally.

| Task | Classes | After | Fixes / notes |
|---|---|---|---|
| P5.1 render | `PaintList`, `DisplayList`, `TileGrid`/`RenderPool`/`WorkerState`, `RetainedDisplayListCache` | H1 | PaintList dtor = P0.3's free loop (now structural); DisplayList re-init/tracking (S10a groundwork); pthread lifecycle encapsulated |
| P5.2 view/DOM | `ViewTree`, `DomDocument` | H4 | R6 symmetry structural; `alloc_prop` becomes `ViewTree::alloc_prop` with the P0.5 contract in one place; teardown walk = `destroy()` |
| P5.3 event/state | `DocState`, `StateStore`, `BrowsingSession`, `ClipboardStore`, `EventStateLog`/`StateDumpLog` (RAII over FILE*), `EditHistory` | H3 | `BrowsingSession::navigate()/go_back()/go_forward()` own old-doc disposal — P0.1's fix moves inside the API so callers *can't* leak; `DocState::reset()` reintroduced correctly if needed (P0.4) |
| P5.4 media/vector | `GifAnimation`, `LottiePlayer`, `RdtPicture` (handle struct, methods only — refcount lands in P6.2) | H1 | dtors centralize the "null surface->pixels + bump generation" invariant (F5/V5) |
| P5.5 counters/context | `CounterContext`, `UiContext` (document ownership as sub-object) | H4 | unify CounterContext's mixed heap/arena backing |

Each P5 task is one PR per class-cluster; gate additionally with `utils/verify_loc_reduction.sh` where net-negative is expected (shim deletion + destroy-chain removal usually is).

---

## P6 — Lifecycle upgrades (behavior changes; each with a dedicated stress test)

- **P6.1 DomRange recycling (S6):** ranges are uniform-size, arena-allocated, refcounted, and never recycled (dom_range.cpp:313–323). Add a free-list inside `DocState`: `DomRange::release()` at refcount 0 unlinks *and* pushes onto `state->range_freelist`; `dom_range_create` pops before `arena_alloc`. (Rejected alternative: `lib/ref_counted_pool.hpp` — a second allocator when the arena+freelist is 20 lines.) **Stress test:** 1M create/release cycles + clone churn; assert `state->arena` size plateaus.
- **P6.2 RdtPicture refcount + cache eviction (S7):** embed `ref_cnt` in RdtPicture (house style — bitfield like Container); `rdt_picture_dup` for SVG_DOM retains instead of shallow-sharing with `owns_pool=false`; `rdt_picture_free` releases; the P2.2 caches gain LRU eviction (safe now — an evicted original stays alive until the last dup releases). **Stress test:** render loop with cache capacity 2 and 10 distinct SVGs; ASan-clean.
- **P6.3 Retained-fragment scratch re-registration (S10a):** on each capture, either re-`dl_init` the fragment list or re-register the scratch node after `dl_clear` (retained_display_list.cpp:295/337/345) so mem-dump accounting stays truthful across frames.
- **P6.4 Font glyph-arena growth (F-b):** instrument a long interactive session (repeated relayout/typing); if `glyph_arena`/`loaded_glyph_cache` growth is confirmed unbounded, add a size-triggered `font_context_reset_glyph_caches` (threshold in FontContextConfig; the generation mechanism already makes mid-session reset safe — consumers revalidate).

---

## 7. Dependency & sequencing summary

```
P0 (bug fixes)          — independent tasks; start immediately; P0.1/P0.2/P0.3 first
P1 (lint)               — after P0.3 (S3 fix precedes rule tuning); P1.2/P1.3 fix sites in-PR
P2 (stragglers)         — after P1 rules exist (they guard the conversions); P2.6 anytime
P3 (pass objects)       — after P1; before P4.4
P4 (RAII scopes)        — P4.1 retires P0.2's goto; P4.4 needs P3
P5 (owner classes)      — per-domain, gated on header phases H1/H3/H4; P5.3 absorbs P0.1's fix
P6 (lifecycle)          — P6.2 needs P2.2; last, each behind a stress test
```

Coordination notes: P0.3 lands **before** `Radiant_Impl_Clean_Up.md` P5 (paint descriptor table) — it simplifies it. P5 here and the Impl_Clean_Up helper extractions (FormControlBox, JsDispatchScope) should use the same Shape A/B idioms — this plan's P4/P5 define them.

---

## 8. Test plan (new tests, all added in the phase that motivates them)

| Test | Phase | Asserts |
|---|---|---|
| `session_navigate_no_leak` (event_sim or gtest) | P0.1 | 50× navigate/back/forward → live document count stable, memtrack balance |
| `layout_early_return_no_leak` | P0.2 | invalid-root layout → no leaked scratch node / counter allocations |
| `paint_list_ownership` gtest | P0.3 | owns_* payloads freed by clear/destroy; memtrack balance |
| alloca remediation fixtures | P1.3 | gradient with 10⁵ stops renders (or cleanly rejects) without stack overflow |
| layout suite full pass | P3.* | byte-identical view trees (494/344/703 fixtures) |
| `range_churn_stress` | P6.1 | 1M range cycles → arena plateau |
| `picture_cache_eviction` | P6.2 | eviction churn ASan/leak-clean |
| long-session font instrumentation | P6.4 | glyph arena bounded after reset threshold |

CI addition (cheap, catches regressions of the whole campaign): a `--mem-dump` step after the layout baseline run asserting the leak report is empty.

---

## 9. Risks

1. **P3 is the risky phase** (data motion in flex/grid hot paths). Mitigations: exact-count allocation (no growth-on-scratch), LIFO assert on mark/restore, full layout suite per task, one engine per PR.
2. **P0.5's abort policy** changes failure behavior from "undefined (NULL deref)" to "defined (clean abort)" — strictly better, but note it in the PR description; batch runs that previously limped past a corrupt pool will now stop.
3. **P6.2 refcount** touches every RdtPicture consumer (display list items, retained fragments, caches); the stress test plus ASan run is mandatory, and eviction stays off until it passes.
4. **Class waves colliding with header phases** — resolved by the sequencing table; when in doubt, headers first, classes second.

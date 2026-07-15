# Radiant Memory Management Refactor — Implementation Plan

**Date:** 2026-07-13
**Baseline commit:** `028a75323`
**Parent design:** `vibe/radiant/Radiant_Design_Memory.md` — the policy (R1–R7), the safety findings (S1–S12, V1–V5, font gaps), the lifetime-role matrix (§2), and the C+ class catalog (§5). This doc turns that into execution-grade tasks.
**Related:** `vibe/Memory_Context.md` (factory/registry), `vibe/radiant/Radiant_Imp_Code_Dedup.md` (header consolidation H1–H6 — class declarations land in the 5 global headers), `vibe/radiant/Radiant_Impl_Clean_Up.md` (LOC phases), `doc/dev/C_Plus_Convention.md`.

---

## Implementation status (verified 2026-07-15)

Legend: ✅ done · ◑ partial · ❌ not done. Verified against the codebase; line numbers in tasks below are the original plan's and are often stale (real locations noted where checked).

**The code work is essentially complete; the §8 test plan is mostly not.** Several P0 fixes shipped directly in their stronger later-phase form (P0.2 → `LayoutPassScope`, P0.7 → `~InputIntent`).

| Phase | Status | One-line |
|---|---|---|
| P0 safety fixes | ✅ 8/8 | all implemented; 2 cosmetic deviations (P0.1 missing `show_html_doc` comment, P0.8 lives in `radiant.hpp`) |
| P1 lint | ◑ 5.8/6 | R3–R6 rules landed error-clean, zero `alloca`/`mem_realloc` in radiant; P1.6 = 4/5 invariant comments |
| P2 stragglers | ◑ 3 done, 3 partial | P2.1/P2.3/P2.4 ✅; P2.2/P2.5/P2.6 ◑ (outcomes met, ownership/mechanism differs) |
| P3 scratch conversion | ◑ flex ✅, grid ◑ | flex zero-heap; grid track lists still heap with `owns_*` intact; exit-check untagged |
| P4 RAII scopes | ✅ 4/4 | all four scope classes exist and are in use |
| P5 owner classes | ◑ 4.8/5 | all done except ClipboardStore (singleton `ClipboardStoreState` + free API, not owner class) |
| P6 lifecycle | ◑ 2 done, 2 partial | P6.1/P6.2 ✅; P6.3 different mechanism; **P6.4 reset exists but only called from a unit test, not a live path** |
| §8 tests | ❌ 1/8 + no CI | only `range_churn_stress` (renamed) exists; CI `--mem-dump` step absent |

**Top remaining work, ranked:** (1) write the §8 regression tests — the P0 fixes have no fails-before/passes-after protection; (2) finish P3.3 (grid track lists onto scratch or tag the heap roots); (3) wire P6.4's size trigger into a live relayout path; (4) smaller: P2.2 cache registration, P2.5 def-table/resolver homes, `ClipboardStore` owner class, 2 missing comments.

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

### P0.1 — S1: session navigation leaks the previous DomDocument  **[HIGH, active leak]**  ✅ DONE
> **Status:** implemented in `BrowsingSession::navigate` (browsing_session.cpp:173-177) + `session_go_history` (:236-240): captures `old_doc`, `assert(old_doc != uicon->document)`, `radiant_cleanup_network_support` + `free_document`. Gap: the requested ownership comment in `show_html_doc` (window.cpp:361-388) was **not** added.
- **Fix location: the three session functions**, not the callers — `session_navigate` (browsing_session.cpp:152–155), `session_go_back` (:203–206), `session_go_forward`. Each currently does only `radiant_cleanup_network_support(old_doc)`. Add old-document disposal mirroring the non-session path at event.cpp:8225–8227: capture `old_doc`, load the new document, on success `free_document(old_doc)` (it already handles JS-wrapper invalidation, timer abandonment, embed images, state teardown — ui_context.cpp:249).
- **Care:** (a) reload-to-same-URL still produces a *new* DomDocument, so unconditional free of the old pointer is correct — but assert `old_doc != ui_context->document` before freeing; (b) free only on successful load — on failure the old document must stay presented; (c) `window.cpp:369–378` (`show_html_doc`) must not also free — ownership belongs to exactly one place; add a comment there pointing at the session functions.
- **Test:** new event_sim/gtest scenario `session_navigate_no_leak`: navigate A→B→back→forward ×50; assert via memtrack counters (or `mem_context` snapshot count of live document contexts == 1 ± the session cache policy) that documents don't accumulate. This is the fails-before test.

### P0.2 — S2: `layout_cleanup` skipped on early returns  **[HIGH]**  ✅ DONE
> **Status:** solved directly in the stronger P4.1 form — `LayoutPassScope` RAII (layout.hpp:2615, layout.cpp:3119-3128); the two validity checks were hoisted *above* init (layout.cpp:3178/3185 with root-cause comments), so no bare `return` sits between init and cleanup.
- layout.cpp:3056–3058 and :3062–3064 `return;` after `layout_init` (:3040-ish) without reaching `layout_cleanup` (:3102). Minimal fix: replace the two `return`s with `goto cleanup;` (or hoist the two validity checks *above* `layout_init` — inspect whether they depend on init state; if not, hoisting is the cleaner minimal fix).
- P4.1's `LayoutPassScope` later makes this class of bug impossible; this task just stops the bleeding.
- **Test:** feed a document whose root fails the node_type check; assert no `MEM_KIND_SCRATCH` node remains registered and counter-context allocations are freed (mem-dump snapshot before/after).

### P0.3 — S3: PaintList ownership not enforced by owner  **[HIGH, latent]**  ✅ DONE
> **Status:** `PaintList::clear()`/`destroy()` free `owns_*` payloads via `paint_cmd_free_owned_payload` (paint_ir.cpp:73-119); `pdf_effect_raster_fallback_clear` deleted; dead `PaintList.arena` field removed (render.hpp:1318-1328, no stray initializers).
- Move the owned-payload free loop from the PDF-local `pdf_effect_raster_fallback_clear` (render_pdf.cpp:114–169) into `paint_list_clear` and `paint_list_destroy` (paint_ir.cpp:42–55): iterate cmds, free `owns_path`/`owns_stops`/`owns_text` payloads, then reset count / free cmds. Delete the PDF-local helper; its call sites call plain `paint_list_clear`.
- Verify SVG's borrow-only lists are unaffected (`owns_*` never set on that path — render_svg.cpp:149/240/277/1806).
- Also in this PR (same file, S10b): **delete the dead `PaintList.arena` field** (paint_ir.cpp:39, paint_ir.h:423) and its two inconsistent initializers (render_output.cpp:168, render_svg.cpp:1711–1712) — `cmds` is heap-grown; the field only invites a future "it's arena-backed" corruption.
- **Tests:** PDF render fixtures + display-list gtests; add one gtest that builds a PaintList with `owns_*` payloads and asserts memtrack balance after `paint_list_destroy`.

### P0.4 — S4: `radiant_state_reset` latent use-after-reset  **[MED, dead code]**  ✅ DONE
> **Status:** deleted — zero matches for `radiant_state_reset` across radiant/ (impl + header decl gone).
- **Delete it** (state_store.cpp:2131–2141 + header decl). Zero callers verified; a reset that must coordinate live_ranges/dom_selection/next_range_id/arena is exactly what `DocState::reset()` (P5.3) should own if ever needed — reintroduce it there, correctly, on demand. Deleting is safer than fixing dead code.

### P0.5 — S8: `alloc_prop` NULL-dereference class  **[MED]**  ✅ DONE
> **Status:** `ViewTree::alloc_prop` (view_pool.cpp:552-563) does `log_error` + `abort()` on NULL with the contract comment (:558). Recommended fatal-failure policy adopted.
- **Policy decision (recommended): failure is fatal.** `pool_calloc` failing mid-layout means the pool is corrupt or the process is OOM — per the clean-abort doctrine (`vibe/Memory_Safety_Review.md`), continuing risks silent corruption. Change `alloc_prop` (view_pool.cpp:615–623) to `log_error` + abort on NULL instead of returning NULL. One change; the 85+ unchecked callers become correct by contract. Document the contract in the function comment.
- Alternative (rejected): null-check every caller — 85+ edits, high miss risk, and no caller has a sane recovery path anyway.

### P0.6 — S9: FloatBox pool-less fallback has no free path  **[LOW]**  ✅ DONE
> **Status:** determined unreachable → `block_context_alloc_float_box` now `assert(ctx && ctx->pool)` then `pool_calloc`; `mem_calloc` fallback removed (block_context.cpp:266-269, root-cause comment :267).
- block_context.cpp:262–264. First determine reachability: grep/trace whether `BlockContext` is ever constructed with `pool == NULL` in the live tree. If unreachable → replace the fallback with an assert (and root-cause comment). If reachable → route the fallback to `lycon->scratch` instead of `mem_calloc`.

### P0.7 — S10c: InputIntent dispose audit  **[LOW]**  ✅ DONE
> **Status:** solved directly in the stronger P4.3 form — `InputIntent::~InputIntent()` calls `input_intent_dispose(this)` (event.hpp:399, editing_intent.cpp:22-26); all manual dispose calls in event.cpp removed. Pairing is automatic.
- Only the paste path allocates (`editing_intent.cpp:145–151`); event.cpp has ~10 intent build sites and 2 dispose calls. Audit every `insertFromPaste`/paste-adjacent branch; add missing `input_intent_dispose` calls. P4.3 (dtor) later makes the pairing automatic.

### P0.8 — S11: `WebViewProp.src`/`srcdoc` not registered as retained fields  **[MED, latent dangling ptr]**  ✅ DONE
> **Status:** implemented via `radiant_retain_webview_src`/`_srcdoc` using `PersistentFieldRef` (radiant.hpp:235-243), called on webview creation (layout_block.cpp:4276-4277). Note: the plan's `webview.h` / `retained_fields.hpp` don't exist — `WebViewProp` lives in `radiant.hpp` and registrations are inline there.
- webview.h:42–43: both are borrowed DOM-attribute pointers ("borrowed from DOM attribute") with no `PersistentFieldRef` registration, so `view_pool_reset_retained` while a webview is live leaves them dangling. Add both fields to `radiant/retained_fields.hpp` following the existing font-family/background-image/surface-path registrations.
- **Test:** gtest/event_sim scenario: create a webview view, force `view_pool_reset_retained`, assert `src`/`srcdoc` still readable (ASan-clean) and equal to the attribute value.

---

## P1 — Lint hardening (R3–R7)

All rules follow the existing ast-grep backend conventions (`utils/lint/rules/c-cpp/*.yml` + `manifest.yml` entry + suppress tag). Land each rule **error-clean**: fix the outstanding sites in the same PR.

### P1.1 — R3: `no-raw-create` (factory-only allocator creation)  ✅ DONE
> **Status:** rule at no-raw-create.yml (all 7 factories, scope radiant/**, suppress RAWCREATE_OK), manifest.yml:44-48 mandatory/error. Radiant clean (0 suppress tags).
- Patterns: `pool_create($$$)`, `pool_create_mmap($$$)`, `arena_create($$$)`, `arena_create_default($$$)`, `arena_create_sized($$$)`, `scratch_init($$$)`, `gc_nursery_create($$$)`. Scope `radiant/**`; suppress `RAWCREATE_OK`. Radiant is already at 0 — this pins it. (Extending scope to `lambda/**` is a follow-up once its remaining sites are converted; out of scope here.)

### P1.2 — R4: `no-open-realloc` + fix the 9 stragglers  ✅ DONE
> **Status:** rule at no-open-realloc.yml (manifest.yml:50-54). Zero `mem_realloc`/`pool_realloc` remain in radiant/; all stragglers converted to `mem_grow_array`/`pool_grow_array` (0 REALLOC_OK tags).
- Patterns: `mem_realloc($$$)`, `pool_realloc($$$)`; scope `radiant/**`; suppress `REALLOC_OK`.
- Convert in the same PR: layout_flex_measurement.cpp:527, rdt_vector_tvg.cpp:160, font_face.cpp:365, cmd_layout.cpp:1438/1451 → `mem_grow_array`; cmd_layout.cpp:1104/1713/1788/1887 → `pool_grow_array` (12 sites already use lib/mem_grow.hpp — follow those).

### P1.3 — R5: fix `alloca-static-size` false negatives, then remediate the 9 sites  ✅ DONE
> **Status:** rule now has member/expression/non-literal-multiplier patterns (alloca-static-size.yml:32-63). All 9 sites eliminated — **zero** `alloca(` calls remain in radiant/ (0 ALLOCA_OK tags); gradient stops use render scratch (render_background.cpp:345-346).
- Add patterns for member/expression sizes: `alloca($X->$F)`, `alloca($X->$F * $$$)`, `alloca($N * sizeof($T))` where `$N` is non-literal. Verify the rule now flags all 9 radiant sites.
- Remediate rather than suppress: gradient stops (render_background.cpp:337/485) and shadow counts (:1240/1415) come from parsed CSS — **attacker-influenced sizes; replace with `scratch_alloc` or a clamped stack buffer + heap fallback**. intrinsic_sizing.cpp:3277/3278/5853 (`num_columns`/`child_count`) and rdt_vector_cg.mm:701/702 — same treatment or an explicit bound check + `ALLOCA_OK` tag with the bound stated.

### P1.4 — R6: destroy-symmetry sweep  ✅ DONE
> **Status:** view_pool.cpp:791-792/813-814 → `mem_arena_destroy`/`mem_pool_destroy` with root-cause comment; dom_element.cpp converted too (now at lambda/input/css/dom_element.cpp:214/219, not radiant/).
- view_pool.cpp:861–862/877–878 and dom_element.cpp:191/195: raw `pool_destroy`/`arena_destroy` on factory-created allocators → `mem_pool_destroy`/`mem_arena_destroy`. Mechanical; behavior identical (hooks already fire), but the convention stops depending on hook installation. Optional rule later; the P5 owner classes make it structural.

### P1.5 — R7 allowlist gate + R2 `OBJ_HEAP_OK`, both **warn mode**  ✅ DONE
> **Status:** R7 allowlist rule (radiant-alloc-allowlist.yml, warn, suppress ALLOC_API_OK, manifest.yml:56-61); R2 obj-heap-ok.yml (warn, manifest.yml:63-68). 15 OBJ_HEAP_OK tags seeded across radiant/ for legitimate roots.
- R7: `pattern: $FN($$$)` + `constraints: {FN: {regex: "(alloc|_free|dup|mmap|memalign)"}}`, allowlist of sanctioned prefixes (design §3 R7) maintained in the rule file. Warn for one milestone; flip to error once the allowlist stabilizes.
- R2 tag rule: typed-struct heap allocation shape `$T* $V = ($T*)mem_calloc($$$)` warns unless the line carries `// OBJ_HEAP_OK: <reason>`. Seed the existing legitimate heap roots (design §1.3: container structs, media players, singletons, FontFaceDescriptor) with tags in the same PR so the rule lands quiet.

### P1.6 — Document V1–V5 in-code  ◑ PARTIAL (4/5)
> **Status:** comments present at render_svg.cpp:1645-1646, render_pdf.cpp:1985-1986, retained_display_list.cpp:189, ui_context.cpp:301-303 (all on-message, "arena-in-pool" invariant). **Gap:** render_effect_raster_fallback.hpp:116-119 has a *related-but-different* comment (dl_destroy-before-pool ordering), not the pool-only-destroy/arena-in-pool invariant.
- Add invariant comments at: render_effect_raster_fallback.hpp:120, render_svg.cpp:1811, render_pdf.cpp:2237 (pool-only destroy is intentional — arena lives in the pool; cascade unregisters); retained_display_list.cpp fragment-scratch sharing; ui_context.cpp:276 aliasing guard. Cheap insurance against well-meaning "leak fixes" that would double-free.

---

## P2 — Standardize the stragglers (lifetime homes for orphans)

- **P2.1 flex measurement cache**  ✅ DONE — now `tree->measurement_cache` owned by `ViewTree`, grown via `mem_grow_array` (layout_flex_measurement.cpp:1237, MEM_CAT_CACHE_LAYOUT), invalidated on reset (view_pool.cpp:784/801). *(orig note:* layout_flex_measurement.cpp:519–527): file-static `measurement_cache` + manual doubling → owned by `ViewTree` (per-document lifetime; entries reference that document's views), grown via `mem_grow_array`, registered under the document's mem_context. Invalidation on `view_pool_reset_retained` comes free.
- **P2.2 RdtPicture path/data/paint caches**  ◑ PARTIAL — converted linked lists → `HashMap` (rdt_vector_tvg.cpp:107-152), no-evict caps preserved; **but still process-wide file-static globals with module `pthread_mutex`, NOT owned by a vector-engine context and NOT mem_context-registered.** *(orig note:* rdt_vector_tvg.cpp:89–273): hand-rolled mutex-guarded linked lists → `hashmap` (or lib LruCache **with eviction still disabled** — eviction only becomes safe after P6.2's refcount) owned by the vector-engine context, mem_context-registered. Keep the no-evict semantics bit-for-bit in this phase.
- **P2.3 tile memory**  ✅ DONE — one sliced `pixel_slab` per grid (tile_pool.cpp:55-71/96); per-frame jobs on `RenderContext.scratch` (render_output.cpp:408/428). *(orig note:* tile_pool.cpp:54/71; render_output.cpp:309/324): per-tile `mem_calloc` loop → one sliced slab allocation per grid; per-frame `jobs` array → `RenderContext.scratch`.
- **P2.4 event_sim parse arena**  ✅ DONE — per-fixture `event_arena` (event_sim.cpp:117-118), SimEvents via `arena_calloc` (:147), the `mem_free(ev)` early-return frees gone (remaining `mem_free` free owned string fields, a separate concern). *(orig:* per-fixture arena; `SimEvent`s become arena allocations; delete the 81 `mem_free(ev)` early-return frees. Test-harness only — big LOC win, zero runtime risk.
- **P2.5 render_svg_inline**  ◑ PARTIAL — `style_rules[]` → `mem_grow_array` (render_svg_inline.cpp:449); **but `SvgDefTable` still a plain `mem_alloc` (:1022) and the image-resolver list still a process-wide static linked list (:46/63)** — not relocated to a per-render pool. *(orig:* `style_rules[]` grow (:440–447) + `SvgDefTable`/resolver entries → `mem_grow_array` / the existing per-render subscene pool.)
- **P2.6 Font gap F-a — register FontContext memory**  ◑ PARTIAL (outcome met, mechanism differs) — the **outcome** is achieved: ui_context.cpp:165-193 creates tracked font pool + 2 arenas (MEM_ROLE_RENDER) and injects them via `FontContextConfig.pool/arena/glyph_arena`, visible to `--mem-dump`; standalone tests pass NULL. **But the planned deliverable — optional factory/registration hook fields on `FontContextConfig` — was not added** (font.h:42-53 has only the nullable data fields). *(orig:* add optional factory hooks to `FontContextConfig` … radiant's `ui_context.cpp:164`.)

---

## P3 — Per-pass layout objects onto scratch (makes R2 real)

Highest-care data-motion phase. The flex/grid pass objects move from `mem_calloc`/`mem_free` chains to `lycon->scratch` (mark/restore). Full layout suite (494 flex / 344 grid / 703 table) is the oracle for every task.

- **P3.1 `FlexLengthScratch` / `FlexIterationScratch`**  ✅ DONE — both on `scratch_calloc`/`scratch_free` (layout_flex.cpp:3694-3695/3800/4112, 3918-3919/4024); no `mem_calloc`/`mem_free` left for them. *(orig:* layout_flex.cpp:3701→4118, 3924→4030): direct conversion to `scratch_calloc`/`scratch_free` — the table/multicol code (layout_table_metadata.cpp:16–46, layout_multicol.cpp:945+) is the established in-tree pattern to copy. Low risk; do first.
- **P3.2 `FlexContainerLayout`** + `flex_items`/`lines`  ✅ DONE — container + items + lines on scratch (layout_flex.cpp:195/591-593, exact-size), teardown = `scratch_restore` (:606-612); wrapped by `FlexLayoutScope` (P4.4). *(orig:* layout_flex.cpp:167/556/558, destroy :563–571): item counts are known up-front (child count), so allocate exact-size on scratch — no growth-on-scratch needed. Nested containers: the `pa_flex` save/restore (layout_flex_multipass.cpp:496/844) already stacks pointers; scratch's LIFO discipline matches the nesting exactly — assert mark/restore pairing in debug.
- **P3.3 `GridContainerLayout`** + areas/line_names/items/track lists  ◑ PARTIAL — container/`grid_areas`/`grid_items`/`line_names` array on scratch (layout_grid.cpp:34/79-80/121/135, teardown `scratch_restore` :201). **But the `owns_*` flags did NOT dissolve — they're still active** (layout_grid.cpp:82-113/173-193; grow :1005-1006), because the inner `GridTrackList`/`GridTrackSize`/area-name allocations remain **heap** (`mem_calloc`/`mem_free` in grid_utils.cpp:41-102/131-155 and layout_grid.cpp:976-1095, the embed-shared CSS-template objects). *(orig:* layout_grid.cpp:31–110, destroy :118–173; grid_utils.cpp:43–94; grow sites :1005–1183.)
   - **Remaining work:** move track lists/track sizes/growth onto scratch (dissolving `owns_*`), or if they're intentionally embed-lifetime, tag the heap sites `OBJ_HEAP_OK` with the ownership rationale so the exit check can pass.
- **Exit check:** ❌ NOT MET — `mem_calloc`/`mem_free` still present and **untagged** in layout_grid.cpp (:197/892/895/976/998/1080-1095) and grid_utils.cpp (:41-155); one in layout_flex_measurement.cpp:1312 (ViewTree cache, not a pass object). layout_flex*.cpp is clean. Grep for `OBJ_HEAP_OK` in these files = 0.

---

## P4 — C+ wave 1: RAII scope classes (Shape B; behavior-neutral)  ✅ DONE (4/4)

Declarations land in `layout.hpp` / `render.hpp` per the header plan; if an H-phase hasn't run yet for that domain, declare in the existing header and let the H-phase move it.

- **P4.1 `LayoutPassScope`**  ✅ — layout.hpp:2615, layout.cpp:3119-3128; used at layout.cpp:3189 in `layout_html_doc`. Retires P0.2's goto.
- **P4.2 `RenderFrameScope`**  ✅ — render.hpp:1612, render_output.cpp:282-296; used at :465/:744.
- **P4.3 `InputIntent` dtor**  ✅ — event.hpp:399, editing_intent.cpp:22-26; manual dispose calls deleted.
- **P4.4 Flex/Grid pass scopes**  ✅ — `FlexLayoutScope` (layout.hpp:1623, impl layout_flex.cpp:614-625, used layout_flex_multipass.cpp:548) + `GridLayoutScope` (layout.hpp:1953, impl layout_grid.cpp:205-216, used layout_grid_multipass.cpp:211); both cite `BlockContextScope` (layout.hpp:2580) as the precedent.
- **Rule for the wave:** zero logic changes — ctors/dtors call the existing functions verbatim. Diff = call-site mechanics only.

---

## P5 — C+ wave 2: owner classes (Shape A)

Sequenced **with** the header-consolidation phases (each domain's classes land after or inside its H-phase to avoid double churn). Mechanics per class: free functions become methods (bodies unchanged); `extern "C"` shims kept where tests/JIT/external C callers exist; create/destroy pairs become `init()/destroy()`; allocator create+destroy calls move inside, fixing R6 structurally.

| Task | Status | Classes | After | Fixes / notes |
|---|---|---|---|---|
| P5.1 render | ✅ | `PaintList`, `DisplayList`, `TileGrid`/`RenderPool`/`WorkerState`, `RetainedDisplayListCache` | H1 | all present w/ init/clear/destroy methods (render.hpp:641/1318/2131/2183/2172; retained_display_list.cpp:28). PaintList dtor = P0.3's free loop |
| P5.2 view/DOM | ✅ | `ViewTree`, `DomDocument` | H4 | `ViewTree` (view.hpp:1648) w/ `alloc_prop`+P0.5 contract; `DomDocument` (dom_element.hpp:97) w/ init/destroy; free `dom_document_destroy` now a thin wrapper |
| P5.3 event/state | ◑ | `DocState`, `StateStore`, `BrowsingSession`, ~~`ClipboardStore`~~, `EventStateLog`/`StateDumpLog`, `EditHistory` | H3 | ✅ all except **ClipboardStore**: exists as file-static singleton `ClipboardStoreState` + free `clipboard_store_*` API (clipboard.cpp:53/244-308), **not a per-owner `ClipboardStore` class**. `BrowsingSession::navigate/go_back/go_forward` own old-doc disposal (radiant.hpp:32-45); free `session_*` shims still delegate |
| P5.4 media/vector | ✅ | `GifAnimation`, `LottiePlayer`, `RdtPicture` (methods only — refcount in P6.2) | H1 | render.hpp:2413/2468, rdt_vector_tvg.cpp:53; `GifAnimation::finish()` centralizes the null-pixels+bump-generation invariant. No `~` dtors (per plan) |
| P5.5 counters/context | ✅ | `CounterContext`, `UiContext` | H4 | layout_counters.cpp:39-140; `UiContext` (view.hpp:2629) w/ init/create_surface/destroy_document/destroy |

> **P5 cross-cutting note:** clusters use the `init()`/`destroy()` method convention, **not** C++ `~` destructors (the only true dtor in this refactor is `~InputIntent` from P4.3). Thin free-function shims (`session_navigate`, `gif_animation_tick`, `dom_document_destroy`, `alloc_prop(lycon,…)`) still exist but delegate to the methods.

Each P5 task is one PR per class-cluster; gate additionally with `utils/verify_loc_reduction.sh` where net-negative is expected (shim deletion + destroy-chain removal usually is).

---

## P6 — Lifecycle upgrades (behavior changes; each with a dedicated stress test)

- **P6.1 DomRange recycling (S6):**  ✅ DONE — `DocState.range_freelist` (event.hpp:2321); `dom_range_create` pops before `arena_alloc` (dom_range.cpp:249-266); `dom_range_release` at ref 0 unlinks + pushes (:284-301). *(The "never recycled" premise is now stale.)*
- **P6.2 RdtPicture refcount + cache eviction (S7):**  ✅ DONE — `atomic_int32 ref_count` on RdtPicture (rdt_vector_tvg.cpp:71); `dup()` retains (:2072-2073), `release()`/`rdt_picture_free` decrements w/ underflow guard, destroys pool at 0 (:2207-2234); LRU eviction `picture_cache_evict_to_capacity_locked` wired into path/data inserts (:361-374/391/423).
- **P6.3 Retained-fragment scratch re-registration (S10a):**  ◑ PARTIAL (different mechanism) — capture calls only `dl_clear` (retained_display_list.cpp:323), **no per-capture re-`dl_init`/re-register**. Instead a single persistent factory-registered cache arena is created once (:280) and survives frame scratch resets by design (documented :189/:215). The accounting goal is met, but not via the literal planned action.
- **P6.4 Font glyph-arena growth (F-b):**  ◑ PARTIAL — reset fn `font_context_reset_glyph_caches` (font_context_glyph_cache.c:3-25), threshold `max_glyph_arena_bytes` default 64 MiB (font.h:50), and size-trigger `font_context_enforce_glyph_arena_limit` (:27-36) **all exist — but the size-trigger is only called from a unit test (test_font_context_gtest.cpp:31), NOT wired into any live relayout/interactive/frame path.** In-app calls are only the unconditional per-document-cleanup reset (cmd_layout.cpp:6809, render_img.cpp:551).
   - **Remaining work:** call `font_context_enforce_glyph_arena_limit` from a live relayout/frame path (after confirming unbounded growth per the plan's instrumentation step).

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

> **Test-plan status: ❌ 1 of 8 written.** This is the campaign's biggest gap — ground rule §0.2 ("a fix without a fails-before/passes-after test is not done") is unmet for every P0 fix. All the code shipped; the regression net did not.

| Test | Phase | Status | Asserts |
|---|---|---|---|
| `session_navigate_no_leak` (event_sim or gtest) | P0.1 | ❌ not written | 50× navigate/back/forward → live document count stable, memtrack balance |
| `layout_early_return_no_leak` | P0.2 | ❌ not written | invalid-root layout → no leaked scratch node / counter allocations |
| `paint_list_ownership` gtest | P0.3 | ❌ not written | owns_* payloads freed by clear/destroy; memtrack balance |
| `webview_retained_fields` | P0.8 | ❌ not written | `src`/`srcdoc` valid across `view_pool_reset_retained` (ASan-clean). *(A `RejectsStaleWebviewSurfaceGeneration` case exists in test_retained_display_list_gtest.cpp:837 but covers a different property.)* |
| alloca remediation fixtures | P1.3 | ❌ not written | gradient with 10⁵ stops renders (or cleanly rejects) without stack overflow. *(Source remediation done; no test fixture.)* |
| layout suite full pass | P3.* | ✅ exists (pre-existing suite) | byte-identical view trees (494/344/703 fixtures) |
| `range_churn_stress` | P6.1 | ✅ written (renamed) | as `RangeChurnPlateausArenaUse` + `RangeReleaseReusesFreelistSlot` (test_dom_range_gtest.cpp:227/209) |
| `picture_cache_eviction` | P6.2 | ❌ not written | eviction churn ASan/leak-clean |
| long-session font instrumentation | P6.4 | ❌ not written | glyph arena bounded after reset threshold |

CI addition (cheap, catches regressions of the whole campaign): a `--mem-dump` step after the layout baseline run asserting the leak report is empty.  **❌ NOT IMPLEMENTED** — no `mem-dump`/`mem_dump` reference in the root Makefile, `.github/workflows/*`, or any `test/*.sh`; the `--mem-dump` flow appears only as a manual gate in §0.1 of this doc.

---

## 9. Risks

1. **P3 is the risky phase** (data motion in flex/grid hot paths). Mitigations: exact-count allocation (no growth-on-scratch), LIFO assert on mark/restore, full layout suite per task, one engine per PR.
2. **P0.5's abort policy** changes failure behavior from "undefined (NULL deref)" to "defined (clean abort)" — strictly better, but note it in the PR description; batch runs that previously limped past a corrupt pool will now stop.
3. **P6.2 refcount** touches every RdtPicture consumer (display list items, retained fragments, caches); the stress test plus ASan run is mandatory, and eviction stays off until it passes.
4. **Class waves colliding with header phases** — resolved by the sequencing table; when in doubt, headers first, classes second.

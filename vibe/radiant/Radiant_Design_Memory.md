# Radiant Memory Management & Object Lifecycle — C+ Class Proposal

**Date:** 2026-07-13
**Baseline commit:** `028a75323`
**Method:** four parallel code-survey agents (view/DOM/layout, render/display-list, event/editing/state, cross-cutting allocator census + lint-gap check). Every claim verified by reading cited source; the census agent ran the `no-raw-alloc` lint rule live.
**Related:** `vibe/Memory_Context.md` (mem_context/mem_factory, Stage 1 complete), `vibe/radiant/Radiant_Mem_Mgt_Fix.md` (2025 batch-mode fixes), `vibe/Memory_Safety_Review.md` (discipline framework), `doc/dev/C_Plus_Convention.md` (class idiom), `vibe/Lambda_Design_Code_Dedup.md` DD5 (C+ classes direction — this doc is its radiant memory-domain execution).

---

## 1. Status quo (measured, mostly good news)

### 1.1 Rule "no raw malloc/calloc/free" — ALREADY DONE and enforced

- `utils/lint/rules/c-cpp/no-raw-alloc.yml` (severity **error**, ast-grep) bans `malloc/calloc/realloc/free/strdup/strndup` across `radiant/**` with `RAWALLOC_OK` suppression tags.
- Live run: **radiant has 0 violations and 0 suppressions.** (14 remaining violations repo-wide are all in `lambda/` — out of scope here.)
- C++ `new`/`delete` is separately banned (`no-new-delete.yml`); radiant has 0 violations and 5 audited placement-new suppressions (TableMetadata, graph list types, counter scope stack).
- **Action here is only hardening**, not migration (see §3 R5 for the one real gap: `alloca`).

### 1.2 Factory discipline — 100% adopted, 0% enforced

Every allocator create-site in radiant already goes through the mem_context factory:

| Create API | Raw calls | Factory calls |
|---|---|---|
| Pool | `pool_create` **0** | `mem_pool_create` **22** |
| Arena | `arena_create` **0** | `mem_arena_create` **13** + `_sized` **2** |
| Scratch | `scratch_init` **0** | `mem_scratch_init` **6** |

But this is convention-only — no lint rule pins it, so it will regress silently (§3 R3).

### 1.3 Tracked-heap usage (the `mem_*` tier)

Census: `mem_alloc` 100 · `mem_calloc` 121 · `mem_realloc` 5 · `mem_free` 553 · `mem_strdup` 155. Hotspots: rdt_vector_tvg (23), cmd_layout (22), surface (14), render_svg_inline (10), grid/flex layout files, media players. Classified against the policy "direct `mem_*` only for stack-oriented allocations":

- **Conforms** (short-lived, function-scoped, paired free): transient index/flag/position buffers, path strings, PNG rows, gradient stop temps, JSON StrBufs.
- **Conforms as heap roots** (long-lived, no natural owning pool): DomDocument/ViewTree container structs, FontFaceDescriptor under UiContext, media players, WebView handles, process singletons (clipboard store, render pool threads). These need a sanctioned category in the policy (§3 R2) — they are object-like but correctly heap-rooted.
- **VIOLATES** — per-layout-pass object graphs hand-managed on the heap with bespoke free chains, where `lycon->scratch` (or the view-tree arena) already exists:
  - `FlexContainerLayout` + items/lines: layout_flex.cpp:167, 556, 558, destroy :563–571
  - `FlexLengthScratch` / `FlexIterationScratch` (literally named scratch): layout_flex.cpp:3701→4118, 3924→4030
  - `GridContainerLayout` + areas/line_names/items + track lists: layout_grid.cpp:31–83, destroy :118–173; grid_utils.cpp:43–94
  - Open-coded array growth via calloc+copy+free: layout_grid.cpp:1005–1183
- **Stragglers bypassing `lib/mem_grow.hpp`** (9 sites; 12 sites already adopted it): layout_flex_measurement.cpp:527, rdt_vector_tvg.cpp:160, font_face.cpp:365, cmd_layout.cpp:1438/1451 (`mem_realloc`) and cmd_layout.cpp:1104/1713/1788/1887 (`pool_realloc`).

### 1.4 The memory-structure zoo (inventory result)

The good news: radiant already uses only the sanctioned lib/ primitives — **Pool, Arena, ScratchArena, tracked heap (mem_*), hashmap, ArrayList, StrBuf, LruCache**. There is no fifth allocator to kill. The deviations are *organizational*, not typological:

| Deviation | Sites | Standard home |
|---|---|---|
| Per-pass heap object graphs (above) | flex/grid | `lycon->scratch` |
| File-static caches outside any lifetime scope | flex measurement cache (layout_flex_measurement.cpp:519), RdtPicture path/data/paint caches (rdt_vector_tvg.cpp:89–273, hand-rolled mutex-guarded linked lists) | owner-scoped LruCache / hashmap-over-pool, registered in mem_context |
| Open-coded realloc grows | 9 sites (§1.3) | `mem_grow_array` / `pool_grow_array` |
| Per-tile heap churn | tile buffers (tile_pool.cpp:54,71), per-frame `jobs` array (render_output.cpp:309) | one sliced allocation / `RenderContext.scratch` |
| 81 hand-written `mem_free(ev)` early-return sites | event_sim.cpp | per-fixture parse arena reset |

---

## 2. The standard lifetime-role matrix

The target model — **four primitives, eight standard roles**. Every allocation in radiant should be attributable to one row; anything that isn't (the §1.4 deviations) gets reparented onto one.

| Lifetime role | Structure | Canonical instance today |
|---|---|---|
| Per-process / worker | Pool (+Arena+Scratch), factory-registered | `tile.worker` (tile_pool.cpp:141–143) |
| Per-document DOM | Pool + Arena | `DomDocument.pool/.arena` (dom_element.cpp:145,153) |
| Per-document view tree | Pool + Arena | `ViewTree.pool/.arena` (view_pool.cpp:835,840) |
| Per-document state | Arenas on document pool | `state.arena/.dirty/.reflow/.store` (state_store.cpp:910–1004) |
| Per-layout-pass | ScratchArena over view-tree arena | `layout.scratch` (layout.cpp:2983) — **must absorb the flex/grid pass objects** |
| Per-render-frame | ScratchArena + per-frame DL/PaintList | `render.scratch` (render_output.cpp:164), `display_list.scratch` |
| Retained cross-frame | Arena inside a cache struct | `retained_dl.arena` (retained_display_list.cpp:215) |
| Short-lived task / export | throwaway Pool+Arena (MEM_ROLE_TEMP) | copy/cut gesture (event.cpp:1466), SVG/PDF backdrops, effect fallback |
| Bounded stack-oriented temp | direct `mem_alloc`/`mem_calloc` + paired `mem_free` | index buffers, path strings |
| Long-lived heap root (no owning pool) | `mem_calloc` + explicit destroy, MEM_CAT-tagged | container structs, media players, singletons |

Cross-subsystem coupling to respect (verified): `view_tree->arena` backs the layout scratch AND render's scratch/paint-list/display-list — so `view_pool_reset_retained`/`view_pool_destroy` invalidates any live display list. The retained cache deliberately deep-clones onto its own arena to escape this; that isolation is a design invariant, not an accident.

---

## 3. Policy rule set (R1–R6)

- **R1 (done, keep):** no raw `malloc/calloc/realloc/free/strdup` — enforced by `no-raw-alloc` (error).
- **R2:** direct `mem_alloc/mem_calloc/mem_free` only for (a) *stack-oriented* allocations — function-scoped, bounded, paired free in the same routine — or (b) *heap roots*: long-lived objects with no natural owning pool, tagged with a MEM_CAT and an explicit destroy path. Object graphs with pass/document lifetime MUST live on the matching pool/arena/scratch from §2. Enforcement: soft lint requiring `// OBJ_HEAP_OK: <reason>` on typed-struct `mem_calloc` (heuristic; ast-grep can match the shape but not prove ownership).
- **R3:** allocator creates in radiant MUST use the factory (`mem_pool_create`/`mem_arena_create`/`mem_scratch_init`). New lint: ban bare `pool_create/arena_create/scratch_init` in `radiant/**`, suppress tag `RAWCREATE_OK`. (Radiant is already at 100% — this pins it.)
- **R4:** growable arrays use `mem_grow_array`/`pool_grow_array` (lib/mem_grow.hpp). New lint: ban `mem_realloc`/`pool_realloc` in `radiant/**`. Catches the 9 stragglers, prevents new ones.
- **R5:** fix the `alloca-static-size` false-negative gap — all **9** radiant `alloca` sites use runtime sizes (`gradient->stop_count` ×2, `shadow_count` ×2, `num_columns`/`child_count` ×3, `count` ×2 in rdt_vector_cg.mm) yet the rule reports zero, because its patterns miss member-access sizes. Add `alloca($X->$F …)` / expression patterns. These are a genuine unbounded-stack-allocation class (a hostile gradient with 10⁶ stops overflows the stack).
- **R6:** destroy symmetry — factory-created allocators are destroyed via `mem_pool_destroy`/`mem_arena_destroy`, not raw `pool_destroy`/`arena_destroy`. Currently ViewTree (view_pool.cpp:861–878) and DomDocument (dom_element.cpp:191–195) create-via-factory but destroy-raw; this *works* (release hooks fire) but silently depends on hook installation. The C+ owner classes (§5) fix this structurally; until then, a mechanical sweep.
- **R7 (allowlist gate — "only these constructs"):** after R1/R3/R4 land, flip from deny-listing to **deny-by-default**: a rule that flags any call whose identifier matches alloc-ish name patterns (`*alloc*`, `*_free`, `*dup`, `mmap`, `sbrk`, `valloc`, `posix_memalign`, `aligned_alloc`) unless it's on the sanctioned allowlist (`mem_*`, `pool_*`, `arena_*`, `scratch_*`, `strbuf_*`, `arraylist_*`, `hashmap_*`, `lru_*`, `mem_grow_array`/`pool_grow_array`, plus third-party prefixes that manage their own memory: `tvg_*`, `FT_*`, `uv_*`…). Feasible in the existing ast-grep backend via `pattern: $FN($$$)` + `constraints: {FN: {regex: ...}}`, or as a `utils/lint/rules/structural` script with an allowlist file. Start in warn mode, flip to error once the allowlist stabilizes. This is what makes "only the sanctioned constructs" *enforced* rather than observed: a new allocation channel (or a vendored lib's allocator leaking into radiant code) fails lint by default instead of by review luck.

**What lint cannot enforce** (for honesty of scope): R2's *placement* judgment — whether a given `mem_calloc` is genuinely stack-oriented vs an object graph that belongs on an arena — is semantic; the `OBJ_HEAP_OK` tag turns it into a reviewable assertion, and the L3 LLM audits + the `--mem-dump` leak report at runtime are the layers that catch structural misuse (hand-rolled caches, wrong-lifetime placement) that name-based linting can't see.

---

## 4. Memory-safety findings (ranked; report per user request)

### Must-fix bugs

| # | Sev | Finding | Evidence |
|---|---|---|---|
| S1 | **HIGH — active leak** | **Session navigation leaks the entire previous DomDocument.** `session_navigate`, `session_go_back`, `session_go_forward` only call `radiant_cleanup_network_support(old_doc)` — never `free_document`; `show_html_doc` overwrites `ui_context.document` without freeing. Proof by asymmetry: the non-session fallback right next to it **does** call `free_document(old_doc)` (event.cpp:8225–8227). Every same-session navigation / back / forward orphans a whole document: pool + 4 state arenas + view tree + all accumulated ranges. | browsing_session.cpp:152–155, 203–206; window.cpp:369–378, 435/445; event.cpp:8221 |
| S2 | **HIGH — leak on edge paths** | **`layout_cleanup` skipped on two early returns** in `layout_html_doc` (invalid node_type; `!root_node`) — leaks the CounterContext heap `scope_stack` ArrayList + per-scope hashmaps, and leaves the scratch mem_node registered. | layout.cpp:3056–3064 vs :3102; layout_counters.cpp:33,52 |
| S3 | **HIGH — latent leak trap** | **PaintList ownership flags not enforced by the owner.** `owns_path/owns_stops/owns_text` exist in the shared struct, but `paint_list_clear` only sets `count=0` and `paint_list_destroy` only frees `cmds`; the free loop lives in a **PDF-local** helper. Any new consumer that sets `owns_*` and calls clear/destroy silently leaks. Fix: move the owned-payload free loop into clear/destroy. | paint_ir.h:100,127,357; paint_ir.cpp:42–55; render_pdf.cpp:114–169 |
| S4 | **MED — latent UAF, dead code** | **`radiant_state_reset` resets the range arena but not `live_ranges`/`dom_selection`** — dangling list head into recycled memory. Zero callers today; exported in the header, corrupts the day it's wired up. Fix or delete. | state_store.cpp:2131–2141 |
| S5 | **MED — fragile invariant** | **Grid `owns_*` double-free surface**: `init_grid_container` memcpy's the pool-owned `embed->grid` then relies on 5 owns_* booleans to decide what `cleanup_grid_container` frees. Currently consistent; one mis-set flag = double-free of pool-owned track lists or leak. (Flex has the same memcpy minus flags.) | layout_grid.cpp:50, 75–110, 124–164; layout_flex.cpp:174 |
| S6 | **MED — unbounded growth** | **DomRange arena grows monotonically for the document's lifetime**: `dom_range_release` at refcount 0 only unlinks — memory is arena-owned, never recycled. Long editing/JS sessions that churn Ranges climb without bound until document teardown. | dom_range.cpp:313–323, 583 |
| S7 | **MED — safe-until** | **RdtPicture SVG_DOM dup shares a pool owned by a process-lifetime cache original** (`owns_pool=false` shallow copy). Safe today only because the picture caches never evict mid-run; adding LRU/eviction without refcounting leaves dups dangling. | rdt_vector_tvg.cpp:1827–1838, 1981–1984, 273 |
| S8 | **MED — NULL-deref class** | **`alloc_prop` can return NULL by design** ("pool may be corrupt", logged) **but callers dereference unconditionally** — e.g. chained `span->bound = alloc_prop(...); span->bound->background = alloc_prop(...)`. | view_pool.cpp:620–623, 642; resolve_css_style.cpp:39–42, 369/419 |
| S9 | LOW | FloatBox `mem_calloc` fallback when `ctx->pool==NULL` has **no free path** (untracked leak on a rare path). | block_context.cpp:262–264 |
| S10 | LOW | Retained-fragment scratch loses its mem_node after first `dl_clear` (never re-`dl_init`'d) → telemetry undercounts; dead `PaintList.arena` field invites a future "it's arena-backed" mistake; InputIntent dispose pairing is convention-only (only paste allocates; 10 build sites, 2 dispose calls). | retained_display_list.cpp:295,337,345; paint_ir.cpp:39/471; editing_intent.cpp:145–151 |
| S11 | **MED — latent dangling ptr** | **`WebViewProp.src`/`srcdoc` are borrowed DOM-attribute pointers with no `PersistentFieldRef` guard** (added 2026-07-13 from the RAD_22 known-issues audit — the surveys missed webview). The retained-fields mechanism (`radiant/retained_fields.hpp`) exists precisely for pool-owned strings that must survive `view_pool_reset_retained` (font family, background image, marker text, surface paths all register); the webview fields — explicitly commented "borrowed from DOM attribute" — never do, so a retained reset while a webview is live leaves both dangling. Fix: register both fields in retained_fields.hpp (M0). | webview.h:42–43; retained_fields.hpp |
| S12 | LOW — ownership ambiguity | **BrowsingSession's network context is borrowed on a comment-only contract** (from the RAD_20 known-issues audit). `thread_pool`/`file_cache` (browsing_session.h:39–40) are caller-owned — "not owned by session — caller manages them" (browsing_session.cpp:96) — with lifetime/re-init split across window.cpp and cmd_layout.cpp. Not a leak today, but S1's fix concentrates *document* ownership in the class while network ownership stays diffuse. Fix: the §5.1 `BrowsingSession` class takes the borrowed pair as explicit `init()` params and states the owns-vs-borrows split in its declaration (M5). | browsing_session.h:39–40; browsing_session.cpp:96 |

### Verified correct — document, do NOT "fix" (each would become a bug if "corrected")

- **V1** Pool-only destroy of backdrop/effect pools (no matching `arena_destroy`) is intentional: the Arena struct + chunks are allocated *from* the pool; the release hook cascades node unregistration (render_effect_raster_fallback.hpp:120, render_svg.cpp:1811, render_pdf.cpp:2237; arena.c:155).
- **V2** Multiple retained fragments sharing one `cache->arena` via independent ScratchArenas is safe (arena free-list coalescing tolerates non-LIFO frees).
- **V3** Dangling `View*` in DocState after relayout is systematically handled (`doc_state_detach_transient_owner` :2684, `clear_dom_view_pool_pointers`, pre-teardown target nulling). Caveat: this verifies the *mechanism*, not its invocation — `state_store_prune_after_reflow` has a single call site (event.cpp:4634) and nothing enforces that new reflow paths call it. Structural enforcement is tracked as **T7/F4** in `vibe/radiant/Radiant_Design_Robustness.md` (assigned there 2026-07-13; previously owned by neither doc).
- **V4** The `doc->pool == view_tree->pool` aliasing guard in `free_document` (ui_context.cpp:276) prevents the obvious double-free.
- **V5** Clipboard deep-copies and clears before write; event/state logs stream to `FILE*` with fixed buffers (no unbounded growth); undo ring is bounded with evicted-slot frees; media buffer aliasing into ImageSurface is generation-guarded (holds as long as replay stays synchronous).

---

## 5. C+ class structure proposal

Per `doc/dev/C_Plus_Convention.md`: struct-based classes, inline methods, no vtables, **no new/delete** (placement-new only at audited boundaries), RAII destructors sanctioned for *stack-allocated* contexts. That gives exactly two class shapes:

**Shape A — owner classes** (pool/heap-resident, explicit lifecycle): `init()` / `destroy()` methods replacing the `x_create`/`x_destroy` free-function pairs. The struct owns its allocators as members; destroy order is encoded once.

**Shape B — RAII scope classes** (stack-resident, per-pass/frame/gesture): constructor acquires (scratch mark, temp pool), destructor releases on every exit path. This shape *structurally eliminates* the S2 class of bug.

The idiom, using the two flagship conversions:

```cpp
// Shape A — view.hpp: ViewTree owns its allocators; destroy symmetry in ONE place
struct ViewTree {
    Pool*  pool;    // views + props
    Arena* arena;   // strings, glyphs, layout scratch backing
    View*  root;
    // ...existing fields...

    bool init(MemContext* doc_ctx);          // mem_pool_create + mem_arena_create (R3/R6)
    void destroy();                           // teardown walk + mem_arena_destroy + mem_pool_destroy
    void reset_retained();                    // clear-pointers walk + recreate allocators
    void* alloc_prop(size_t size);            // pool_calloc + null-policy in one place (S8)
};

// Shape B — layout.hpp: pass-scoped RAII; early returns can no longer leak (S2)
struct LayoutPassScope {
    LayoutContext* lycon;
    LayoutPassScope(LayoutContext* lc, UiContext* uicon, ViewTree* tree) : lycon(lc) {
        layout_init(lc, uicon, tree);         // counter_context + mem_scratch_init
    }
    ~LayoutPassScope() { layout_cleanup(lycon); }  // scratch_release + counter_context_destroy
};
```

### 5.1 Conversion catalog (by subsystem, ranked within each)

**Layout** (fixes S2, S5; retires the §1.3 policy violations):

| Class | Shape | Absorbs | Wins |
|---|---|---|---|
| `LayoutPassScope` / `LayoutContext` | B | layout_init/layout_cleanup | kills S2 structurally |
| `FlexLayout` (pass object) | B, **scratch-backed** | init_flex_container/cleanup + FlexLengthScratch/FlexIterationScratch | removes ~8 mem_calloc/mem_free chains; allocations move to `lycon->scratch` |
| `GridLayout` (pass object) | B, scratch-backed | init_grid_container/cleanup + track-list grows | the 5 `owns_*` flags become dtor logic in one audited place (S5) |
| `CounterContext` | A | counter_context_create/destroy/push/pop | unifies mixed heap/arena backing |
| `BlockContext` | already has `BlockContextScope` RAII | formalize block_context_* as methods | pattern precedent — cite it as the house example |

**View/DOM**:

| Class | Shape | Absorbs | Wins |
|---|---|---|---|
| `ViewTree` | A | view_pool_init/destroy/reset_retained/alloc_prop/free_view + teardown walk | R6 destroy symmetry; S8 null policy centralized |
| `DomDocument` | A | dom_document_create/destroy | pairs with ViewTree; owns pool+arena symmetrically |

**Render/display** (fixes S3; de-risks S7):

| Class | Shape | Absorbs | Wins |
|---|---|---|---|
| `PaintList` | A | paint_list_init/clear/destroy + recorders | **dtor/clear runs the owned-payload free loop → S3 fixed by construction**; delete dead `arena` field |
| `DisplayList` | A | dl_init/dl_clear/dl_destroy/dl_alloc_item + recorders | re-init/tracking (S10) impossible to forget |
| `RenderFrameScope` / `RenderContext` | B | render_output_init_context/cleanup_context | scratch/paint-list/vector become RAII members |
| `TileGrid` + `RenderPool` + `WorkerState` | A | tile_grid_*/render_pool_*/worker_init | encapsulates pthread lifecycle; tile buffers become one sliced allocation |
| `RetainedDisplayListCache` | A | ~15 retained_dl_cache_* fns | fragment re-init policy in one place |
| `RdtPicture` → refcounted handle | A + embedded ref_cnt (house style) | rdt_picture_dup/free + owns_pool bookkeeping | eviction becomes safe → unblocks LRU on the picture caches (S7) |
| `GifAnimation` / `LottiePlayer` | A | create/tick/finish triads | dtor centralizes the "null surface->pixels + bump generation" invariant |

**Event/state** (fixes S1, S4; bounds S6):

| Class | Shape | Absorbs | Wins |
|---|---|---|---|
| `BrowsingSession` | A | session_create/destroy/navigate/go_back/go_forward | **`navigate()` owns old-document disposal → S1 fixed at the API level** (callers can't forget); `init()` takes the borrowed network context (`thread_pool`/`file_cache`) as explicit params, encoding the owns-vs-borrows contract in the declaration (S12) |
| `DocState` | A | radiant_state_create/destroy/reset | `reset()` forced to handle live_ranges/dom_selection → S4 |
| `StateStore` | A | state_store_create/destroy | ordered teardown as dtor |
| `DomRange` | A + ref_cnt (already has one) | dom_range_create/retain/release/clone | `release()` returns node to an arena free-list → bounds S6 |
| `EditHistory` | A | te_history_new/free/push/undo/redo | snapshot lifetime automatic |
| `ClipboardStore` | A (singleton) | entry/item new/free/clone + g_store | one clear()/write() API |
| `EventStateLog` / `StateDumpLog` | A (RAII over FILE*) | *_create/*_close | |
| `InputIntent` | B (stack) | input_intent_dispose → dtor | S10 pairing automatic |
| `EventSimContext` | A + per-fixture parse arena | event_sim_free + 81 mem_free(ev) sites | test-only; big LOC win |

### 5.2 What does NOT convert

- MIR/JIT-callable surfaces stay `extern "C"` free functions (thin wrappers over methods where needed).
- POD data structs (props, DL items, paint ops) stay plain — classes are for *owners with lifecycles*, not for data.
- No forced conversion of stable files; the catalog above is the whole list for this campaign.

---

## 6. Phased plan (incremental; every phase independently green)

Gates for every phase: `make build` + `make test-radiant-baseline` 100% + `make layout suite=baseline` (layout phases) / `make editor-4c-js && make editor-4c-view` (state phases) + **`lambda.exe layout --mem-dump` leak report clean** + `make lint`.

- **M0 — Safety fixes (bugs first, no restructuring).** Fix S1 (free old doc on session navigate/back/forward — mirror event.cpp:8225), S2 (route early returns through cleanup), S3 (move owned-payload free loop into paint_list_clear/destroy), S4 (fix or delete radiant_state_reset), S9 (FloatBox fallback), S11 (register `WebViewProp.src`/`srcdoc` in retained_fields.hpp), and add the S8 null-handling decision (assert-and-abort vs propagate — pick one policy). Each fix gets a root-cause comment per CLAUDE.md rule 12. *S1 needs a batch-navigation test (navigate N times, assert RSS/mem-dump stable).*
- **M1 — Lint hardening (R3/R4/R5 + OBJ_HEAP_OK warn mode).** Four small ast-grep rules + the alloca pattern fix. Radiant is already clean for R3/R4 except the 9 stragglers — fix those in the same PR so the rules land error-clean.
- **M2 — Standardize stragglers.** File-static flex measurement cache + RdtPicture linked-list caches → owner-scoped, mem_context-registered structures; tile buffers → one sliced allocation; per-frame jobs → RenderContext scratch; event_sim parse arena (retires 81 frees).
- **M3 — Per-pass objects onto scratch.** FlexContainerLayout/GridContainerLayout/+Scratch structs move from mem_calloc chains to `lycon->scratch` (mark/restore). This makes R2 true in practice, not just in policy. Highest-care phase: nested flex/grid save/restore (`pa_flex`/`pa_grid`) must keep working; full layout suite is the oracle.
- **M4 — C+ wave 1: RAII scopes (Shape B).** LayoutPassScope, RenderFrameScope, InputIntent dtor, formalize BlockContextScope pattern. Behavior-neutral by construction.
- **M5 — C+ wave 2: owner classes (Shape A).** ViewTree, DomDocument, PaintList, DisplayList, DocState/StateStore, BrowsingSession, ClipboardStore, logs, TilePool family. Mechanical: free functions become methods; `extern "C"` shims kept where external callers exist. Coordinate with the header consolidation plan (`Radiant_Imp_Code_Dedup.md`) — each class lands in its assigned global header (view/layout/render/event).
- **M6 — Lifecycle upgrades.** DomRange arena free-list (S6), RdtPicture refcount + LRU-safe caches (S7), retained-fragment re-init (S10). These change allocation behavior — each gets a dedicated stress test (range churn; picture eviction).

Ordering rationale: bugs before policy, policy before mechanics, mechanics before behavior changes. M4/M5 are also DD5's radiant pilot — the clean-up campaigns in `Radiant_Impl_Clean_Up.md` should land their extracted helpers (FormControlBox, JsDispatchScope) in these class shapes from M4 onward.

---

## 7. Fonts — how font memory is managed today (surveyed 2026-07-13)

The font subsystem lives in `lib/font/` (28 files), not radiant/, and is **already the model citizen for the Shape-A owner class** — in C:

- **One `FontContext` per `UiContext`** (created ui_context.cpp:164, destroyed :316; process/window lifetime, not per-document). It owns: `pool` + `arena` + a dedicated `glyph_arena` (256 KB → 4 MB, font_context.c:125), with `owns_pool`/`owns_arena` flags and a `destroying` flag that skips per-object `pool_free` during bulk `pool_destroy` — the same intentional pattern as V1.
- **Six caches inside it** (font_internal.h:259–292):
  1. `face_cache` (key `"family:weight:slant:size"` → refcounted `FontHandle*`) — **with real LRU eviction at capacity** (font_cache.c:124–128, `lru_tick`);
  2. `file_data_cache` — refcounted raw font-file bytes, mmap'd or `mem_alloc`'d, deduped across handles (the 2025 batch-leak fix, `Radiant_Mem_Mgt_Fix.md`);
  3. `bitmap_cache` and 4. `loaded_glyph_cache` — glyph bitmaps whose data lives in `glyph_arena`, stamped with `glyph_cache_generation`;
  5. `codepoint_fallback_cache`; 6. per-face `advance_cache`/`kern_cache` hashmaps.
- **Invalidation**: `font_context_reset_glyph_caches` (font_context.c:344) clears both glyph hashmaps, `arena_reset`s the glyph arena, and bumps the generation (consumers re-validate against it). `FontHandle` is refcounted; view `FontProp`s release handles in the view-teardown RELEASE_EXTERNAL phase.
- **Radiant side**: `radiant/font_face.cpp` manages `@font-face` descriptors as UiContext heap roots (`mem_calloc`/`mem_strdup` — legitimate R2 category, but carries one of the 9 R4 realloc stragglers at font_face.cpp:365).

**Gaps vs this proposal:**
1. **Not mem_context-registered.** font_context.c uses raw `pool_create`/`arena_create` (:86/:100/:125) — deliberately deferred during the Stage-1 conversion because standalone lib tests compile lib/font without mem_factory. Consequence: all font memory is invisible to `--mem-dump`. Fix options (M2): an optional registration hook (FontContextConfig gains factory callbacks), or radiant registers the context's pool/arenas post-create. The nullable-hook pattern used for pool/arena release hooks is the precedent.
2. **Glyph-cache reset is only wired into batch paths** (cmd_layout.cpp:6835, render_img.cpp:548 — per-document in batch mode). A long-running *interactive* session never resets `glyph_arena`/`loaded_glyph_cache`; `face_cache` evicts handles but glyph bitmaps accumulate until context destroy. Same growth class as S6 — **verify under a long interactive session and, if confirmed, add a size-triggered reset** (the generation mechanism already makes reset safe mid-session).
3. `FontContext` is the natural first `extern "C"`-wrapped Shape-A conversion *outside* radiant when DD5 reaches lib/ — but that's a separate campaign; this doc only claims the two gaps above.

---

## 8. Coordination

- **With header consolidation** (`Radiant_Imp_Code_Dedup.md` H1–H6): class declarations land in the five global headers; do M5 for a subsystem *after* its H-phase to avoid double churn (or fold M5 moves into the H-phase's Step A where timing aligns).
- **With `Radiant_Impl_Clean_Up.md`**: M0's S3 fix touches paint_ir.cpp before Impl_Clean_Up P5 (paint descriptor table) — do S3 first, it simplifies P5.
- **With `vibe/Memory_Context.md` Stage 2** (page-alloc + OOM reclaim): the R2/R3 discipline and owner classes here are prerequisites — a clean ownership graph is what makes cascade reclamation trustworthy.

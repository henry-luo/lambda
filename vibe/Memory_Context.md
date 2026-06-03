# Memory Context — Central Allocator Factory & Introspection

**Scope**: Lambda + Radiant memory management (continuation of [Memory_Pooling.md](./Memory_Pooling.md))
**Status**: Proposal
**Date**: 2026-06-03

> **Staging.** This proposal is delivered in two stages:
> - **Stage 1 (§1–§14)** — the Memory Context as a central allocator **factory** that owns lifecycle and provides introspection (snapshots, leak/ordering enforcement).
> - **Stage 2 (§15)** — a centralized **page/chunk allocation** layer that turns the context into the system's memory-pressure coordinator: allocators that hit OOM ask the context to reclaim memory (drop caches, compact arenas, force GC) and retry, with per-thread back-pressure policy (park / fail / abort).
>
> Stage 2 builds directly on Stage 1: it needs the owned graph, role tags, and per-node size accounting to make cost-based reclamation decisions.

---

## Stage 1 — Implementation Status (2026-06-03)

**Stage 1 is implemented and verified across the engine.** At-a-glance:

| Area | Status | Notes |
|------|--------|-------|
| **Core registry** (`lib/mem_context.{h,c}`) | ✅ | Contexts (root + sub-contexts), single global-mutex thread safety (**TSan-clean**), node registry, `child_count` edges, cascade teardown, **doc URL registry**, flat-POD snapshot + JSON + `MEMCTX-LEAK` report, `mem_unregister_subtree`. Tests: `test_mem_context_gtest` (13). |
| **Factory wrappers** | ✅ | lib-level (`lib/mem_factory.{h,c}`): `mem_pool_create[_mmap]`, `mem_arena_create[_sized]`, `mem_scratch_init`, `mem_nursery_create`. runtime-level (`lambda/mem_factory_rt.{h,cpp}`): `mem_gc_heap_create[_with_pool]`, `mem_name_pool_create`, `mem_shape_pool_create`. Each underlying type has a `mem_node` field + release hook (auto-unregister on destroy; **ref-count-aware** for name/shape; **subtree-unregister** for pool/arena). Tests: `test_mem_factory_gtest` (9). |
| **Call-site migration** | ✅ | **Zero raw `pool_create` in application code** (40 files via factory); all `scratch_init` (6), `gc_nursery_create` (16), and lambda/radiant `arena_create*` (15) routed through the factory. Deferred: lib-level arenas (`font_context.c` glyph arena, `pdf_writer.c`, `memtrack.c`) — would break standalone lib tests that compile them without `mem_factory`. |
| **Per-document attribution** | ✅ | `Input::create` + `dom_document_create` register the doc URL and build a per-document sub-context; the input's arena/name-pool/shape-pool, the DOM pool/arena, the **view-tree pool/arena**, and the **layout scratch** are grouped under it. `--mem-dump` groups memory by source URL (e.g. each parsed `.csv`). |
| **Per-document reclamation** | ✅ where valid | Radiant `DomDocument` is reclaimed per-document via `free_document()` + release hooks (`layout --mem-dump` exits with an empty dump). lambda `input()` data is **GC-coupled** (Items flow into the script graph) → per-document free is unsafe; the shared `global_pool` is correct, and the context provides **attribution** there, not reclamation. |
| **Consumers** | ✅ (partial) | `--mem-dump[=PATH]` CLI flag → JSON snapshot + leak report at exit (in `lambda/main.cpp`); documented in `doc/Lambda_CLI.md` + `--help`. `--mem-top` live TUI: pending. |
| **Verification** | ✅ | Unit: `test_mem_context_gtest` 13/13 + `test_mem_factory_gtest` 9/9 (TSan-clean). Baselines every batch: Lambda **2941/2942** (the 1 fail, `dom_jquery_lib`, is pre-existing/unrelated), Radiant **~5670/5670 exit 0**. Live `--mem-dump` verified on script / convert / layout (no UAF). |

**Optional Stage-1 remainders:** per-call reclamation for short-lived `convert`/`validate`/npm input flows (need a dedicated per-call pool to bypass the shared `global_pool`); the deferred lib-level arenas; a `make check-no-raw-alloc` grep gate; renaming raw primitives to `*_raw`; and the `--mem-top` TUI. **Stage 2** (centralized page-allocation + OOM reclamation, §15) is not started.

> Key engineering decisions along the way: (1) a **nullable release hook** keeps `mempool`/`arena`/etc. decoupled from `mem_context` (lib-only tests link with a NULL hook); (2) a thread-local **teardown guard** prevents the hook re-entering the lock during cascade; (3) **`mem_unregister_subtree`** fixes a dangling-node UAF when a backing pool is bulk-destroyed (caught only by `--mem-dump`, not baselines); (4) per-document **attribution vs reclamation** is split deliberately because input data lifetime is GC-coupled.

---

## Table of Contents

### Stage 1 — Factory & Introspection

1. [Motivation](#1-motivation)
2. [Goals & Non-Goals](#2-goals--non-goals)
3. [Current State Recap](#3-current-state-recap)
4. [Design Overview: The Memory Context as Factory](#4-design-overview-the-memory-context-as-factory)
5. [Core Data Structures](#5-core-data-structures)
6. [The Factory API](#6-the-factory-api)
7. [Ownership, Lifecycle & Cascade Teardown](#7-ownership-lifecycle--cascade-teardown)
8. [Thread Safety](#8-thread-safety)
9. [Snapshots](#9-snapshots)
10. [Snapshot Consumers](#10-snapshot-consumers-json-devtools-top)
11. [Migration Strategy](#11-migration-strategy)
12. [Risks & Open Questions](#12-risks--open-questions)
13. [Best Practices From Other Runtimes](#13-best-practices-from-other-runtimes)
14. [Additional Suggestions](#14-additional-suggestions)

### Stage 2 — Centralized Page Allocation & Memory Pressure

15. [Stage 2: Centralized Page/Chunk Allocation & Memory Pressure Handling](#15-stage-2-centralized-pagechunk-allocation--memory-pressure-handling)
    - [15.1 Motivation & Position in the Stack](#151-motivation--position-in-the-stack)
    - [15.2 The Page/Chunk Choke Point](#152-the-pagechunk-choke-point)
    - [15.3 Reclaimer Registry & Cost Model](#153-reclaimer-registry--cost-model)
    - [15.4 Reclamation Levels (Escalation Ladder)](#154-reclamation-levels-escalation-ladder)
    - [15.5 The OOM Handling Loop](#155-the-oom-handling-loop)
    - [15.6 Thread Disposition: Park / Fail / Abort](#156-thread-disposition-park--fail--abort)
    - [15.7 Thread Safety & Reentrancy Under Pressure](#157-thread-safety--reentrancy-under-pressure)
    - [15.8 Proactive Reclamation (Watermarks)](#158-proactive-reclamation-watermarks)
    - [15.9 Integration Points](#159-integration-points)
    - [15.10 Best Practices From Other Runtimes (OOM-specific)](#1510-best-practices-from-other-runtimes-oom-specific)
    - [15.11 Risks & Open Questions](#1511-risks--open-questions)

---

## 1. Motivation

Today the codebase has **three independent allocator families** — `Pool` (rpmalloc / mmap, `lib/mempool.c`), `Arena` (bump, `lib/arena.c`), `ScratchArena` (LIFO scratch, `lib/scratch_arena.c`) — plus the **GC heap** (`gc_heap` under `Heap`, `lambda/transpiler.hpp`) and the **GC nursery** (`gc_nursery`, `lib/gc/gc_nursery.h`). Each is created *ad hoc* by directly calling `pool_create()` / `arena_create_default()` / etc. at ~40 scattered call sites (see [Memory_Pooling.md §3–§4](./Memory_Pooling.md)). Nothing owns the set; nothing knows:

- How many pools/arenas/heaps are live right now
- How much memory each holds, and what it's for
- Their parent/child (backing) relationships
- Whether any leaked past their owner's lifetime, or were torn down out of order

This makes leak detection, profiling, and lifecycle correctness all manual and error-prone — e.g. the Arena-before-Pool-drain ordering hazard ([Memory_Pooling.md §I2](./Memory_Pooling.md)) is real and currently only documented in prose.

**Proposal**: introduce a `MemContext` that is the **single factory through which every allocator is created and destroyed**. Direct calls to `pool_create`/`arena_create`/`scratch_init`/`heap_init`/`gc_nursery_create` become internal (or are removed); all call sites route through `mem_pool_create(ctx, role, label)`, `mem_arena_create(ctx, backing, role, label)`, etc. Because the context *owns* every allocator, it can:

- guarantee a **single rooted graph** by construction (an allocator cannot exist outside it),
- record kind/role/label/parent at the moment of creation (no separate annotation step),
- enforce **dependency-correct teardown** (children before their backing parent),
- take a cheap **binary snapshot** of the live graph for logging, interactive inspection, or a live `top`-style view.

This is the difference from a pure observer: allocators are not just *registered*, they are **centrally and consistently managed**.

---

## 2. Goals & Non-Goals

### Goals
- **Factory ownership**: every `Pool`, `Arena`, `ScratchArena`, `Heap`, `gc_nursery` is born from and dies through the context. No allocator exists off-graph.
- **Single rooted graph** by construction, with explicit backing (parent) edges.
- **Thread-safe** create/destroy/snapshot (request rule #1).
- **Cascade teardown** in dependency order — the context can destroy a subtree or itself correctly.
- **Cheap snapshot** into a compact binary structure (O(n) in allocator count).
- **Rich metadata** captured at creation: kind, role/label, byte size, alloc count, parent edge.
- **Multiple consumers**: JSON dump, in-memory query, live `top` view.

### Non-Goals
- Not replacing the allocator *implementations*. The factory wraps `lib/mempool.c` / `lib/arena.c` / `lib/scratch_arena.c` — it does not change their bump/rpmalloc internals or the established Pool→Arena→Scratch layering ([Memory_Pooling.md §1](./Memory_Pooling.md)).
- Not intercepting per-allocation calls. `pool_alloc`/`arena_alloc` stay lock-free and unchanged; the factory governs allocator *lifecycle*, not every byte.
- Not a new GC. It manages *allocators*, not individual objects (object-level walking is opt-in and bounded — [§9.4](#94-deep-vs-shallow-snapshots)).

---

## 3. Current State Recap

The allocator zoo and its natural backing edges (the factory makes these edges explicit and mandatory):

```
                 OS / mmap / rpmalloc
                         │
                       Pool ───────────────┐
                      /  │  \               │ (backs)
          (backs)    /   │   \ (backs)      ▼
              Arena ─┤   │   ├─ gc_heap → Heap (EvalContext.heap)
                │    │   │
       ScratchArena  │   └─ NamePool / ShapePool / Input metadata
                │    │
       (LIFO scoped) └─ gc_nursery (numeric temporaries)
```

Fields the factory will populate / take ownership of:
- `EvalContext { Heap* heap; Pool* ast_pool; NamePool* name_pool; gc_nursery_t* nursery; }` — `lambda/lambda-data.hpp:77`
- `Heap { Pool* pool; struct gc_heap* gc; }` — `lambda/transpiler.hpp:10`
- `LayoutContext { Pool* pool; ScratchArena scratch; }` — `radiant/layout.hpp:346,369`
- `RenderContext { ScratchArena scratch; }` — `render.hpp`
- `ViewTree { Arena* arena; Pool* pool; }` — added in [Memory_Pooling.md §8.3 item 5](./Memory_Pooling.md)
- `Input { Pool* pool; Arena* arena; NamePool* name_pool; ShapePool* shape_pool; ArrayList* type_list; void* url; Input* parent; }` — `lambda/lambda-data.hpp:576`; created by `Input::create()` at `lambda/input/input.cpp:894` (arena/name_pool/shape_pool spun up at `:905-907`)
- `InputManager { Pool* global_pool; ArrayList* inputs; }` — `lambda/input/input.cpp:920`; the process-wide owner of all `Input`s
- `DomDocument { Input* input; Pool* pool; Arena* arena; Url* url; NetworkResourceManager* resource_manager; ViewTree* view_tree; }` — `lambda/input/css/dom_element.hpp:53` — the real "document" object tying URL → parsed tree → view tree → network resources
- `NetworkResourceManager { EnhancedFileCache* file_cache; void* resources /* URL→NetworkResource* */; uint64_t document_id; uint64_t navigation_id; }` — `lambda/network/network_resource_manager.h:90`
- `EnhancedFileCache { void* metadata_map /* URL→CacheMetadata* */; CacheMetadata* lru_head/tail; size_t current_size_bytes, max_size_bytes; }` — `lambda/network/enhanced_file_cache.h:44`
- `Script { MIR_context_t jit_context; main_func_t main_func; ArrayList* debug_info; }` — `lambda/ast.hpp:631` — holds the MIR JIT context + generated code for a compiled script
- `FontContext { Pool* pool; Arena* arena; Arena* glyph_arena; struct hashmap* face_cache / bitmap_cache / loaded_glyph_cache / file_data_cache; uint32_t lru_counter; }` — `lib/font/font_internal.h:246`
- `UiContext { struct hashmap* image_cache /* path→ImageEntry{path, ImageSurface*} */; }` — `radiant/view.hpp:1467` (decoded image surfaces); plus the process-wide ThorVG vector/paint caches `g_picture_path_cache` / `g_picture_data_cache` / `g_paint_cache` — `radiant/rdt_vector_tvg.cpp:87-145`

Existing stat hooks the factory reuses for snapshots:
- `pool_get_stats(Pool*, size_t* bytes, size_t* count)` — `lib/mempool.h:113`
- `gc_nursery_total_allocated(gc_nursery_t*)` — `lib/gc/gc_nursery.h:45`
- Arena tracks chunk sizes internally (add an accessor).

The existing low-level creators (`pool_create`, `arena_create_default`, `scratch_init`, …) are **not deleted** — they become the *raw* primitives the factory calls internally. They are removed from the public surface (or renamed `*_raw` and made internal-linkage) so that the only sanctioned path for application code is through the context.

### 3.1 Input, Download & Cache Memory (also centralized)

Because Lambda/Radiant is a document engine, the input-parsing and network subsystems are where most doc-scoped memory actually lives — so they must come under the context too, not just the layout/render/runtime allocators.

**Input parsing.** Each `Input` (`lambda/lambda-data.hpp:576`) owns an `arena`, `name_pool`, and `shape_pool`, and every parser (`lambda/input/input-*.cpp` — json/xml/html/css/markdown/pdf/yaml/latex/toml/csv) allocates its parsed tree from that `Input`'s arena via `MarkBuilder` (`lambda/mark_builder.hpp`). Parsers do **not** create their own pools — good, they already funnel through one allocator per document. The transient `InputContext` (`lambda/input/input-context.hpp:14`) holds an `owned_source_` copy allocated with `mem_alloc(..., MEM_CAT_INPUT_OTHER)` and a `StrBuf` in `Input->pool`.

> **Key finding — there is no per-document teardown today.** `Input::create()` is called with `InputManager::global_pool` (`input.cpp:982`), so **every parsed document's arena/name_pool/shape_pool is backed by one process-wide pool** that is only freed in `~InputManager()` via `pool_destroy(global_pool)` (`input.cpp:947`). Memory for a closed/finished document is *not* reclaimed until process exit. Routing `Input` creation through a **per-document sub-context** ([§7.3](#73-scoped-contexts)) — instead of the shared global pool — is what finally makes per-document reclamation possible. This is a concrete correctness/footprint win, not just observability.

**Network download.** Downloads go through a separate **categorized** allocator: `network_downloader.cpp` accumulates the HTTP body with `mem_realloc(..., MEM_CAT_NETWORK)` (capped at 100 MB), and `NetworkResource` (`network_resource_manager.h:48`) holds `url`/`local_path`/`error_message` as `mem_*`-allocated strings. The `NetworkResourceManager` keys live resources by URL and already carries a `document_id` and `navigation_id`.

**Cache.** `EnhancedFileCache` (`enhanced_file_cache.h:44`) is a URL-keyed, LRU, size-bounded disk-metadata cache: `metadata_map` (URL → `CacheMetadata`), an LRU doubly-linked list, and **`current_size_bytes` / `max_size_bytes` it already tracks** — exactly the accounting a snapshot wants, and exactly the shape of an evictable reclaimer.

**Two integration consequences:**
1. **The existing `MEM_CAT_*` categorized `mem_alloc` is a precursor to this proposal** — it already tags allocations by purpose (`MEM_CAT_NETWORK`, `MEM_CAT_INPUT_OTHER`). The factory's `MemRole` subsumes it: those call sites become context-routed allocations whose node carries the role, and the ad-hoc category enum can be retired or mapped onto `MemRole`.
2. **Doc identity already half-exists.** `DomDocument.url` + `NetworkResourceManager.document_id`/`navigation_id` are the seed of the [§5 doc URL registry](#document-url-registry): wiring them to call `mem_doc_register(ctx, url, parent_doc)` makes the page the parent doc and its downloaded sub-resources (CSS/JS/font/image) children — so cache entries and download buffers attribute to the right page in reports.

### 3.2 JIT Code, Font & Media Caches (also centralized)

Three more memory consumers — none of them ordinary pools/arenas — must be visible to and (where safe) reclaimable by the context. These are tracked with the new `MEM_KIND_JIT` and `MEM_KIND_CACHE` node kinds ([§5](#5-core-data-structures)) so the snapshot accounts for them even though they manage their own backing memory.

**MIR JIT compiled code.** `jit_init()` (`lambda/mir.c:128`) creates a `MIR_context_t` (`MIR_init` + `MIR_gen_init`); generated functions live as **mmap'd executable code pages** owned by MIR. The context, the `main_func` pointer, and `debug_info` (function-address → source map for stack traces) hang off the `Script` struct (`lambda/ast.hpp:631,646-651`); torn down by `jit_cleanup()` (`mir.c:345`, `MIR_gen_finish`/`MIR_finish`) during `runtime_cleanup()` (`runner.cpp:1457`). Today there is **no code-size accounting** — JIT code is invisible to any memory report.
- *Tracking*: register each `Script.jit_context` as a `MEM_KIND_JIT` / `MEM_ROLE_CODE` node, doc-scoped to the script's document where applicable. `debug_info` (an `ArrayList`) is routed through the factory normally.
- *Size*: MIR allocates code via its own allocator. Best path is to give MIR a **custom allocator hook** (if the linked MIR build exposes one) that funnels code-page allocation through `mem_page_alloc` ([§15.2](#152-the-pagechunk-choke-point), `MEM_PAGE_MMAP`) so reserved code bytes are counted exactly; fallback is to estimate from generated-function/module count and record it on the node. ([Open question Q7](#1511-risks--open-questions).)

**Font cache.** `FontContext` (`lib/font/font_internal.h:246`) already has the structure we want: a `glyph_arena` (256 KB→4 MB, `font_context.c:117`), hashmap caches (`face_cache`, `bitmap_cache`, `loaded_glyph_cache`, `file_data_cache`), an `lru_counter`, LRU eviction (`font_cache.c:106` `font_cache_evict_lru`), and a per-document reset (`font_context.c:274` `font_context_reset_document_fonts`, which clears document fonts but preserves system fonts). `FontFileDataEntry` is ref-counted and may be mmap'd or `mem_alloc`'d (`font_internal.h:310`).
- *Tracking*: the `glyph_arena` is a factory-created `MEM_KIND_ARENA` (role `FONT`); the hashmap caches register as `MEM_KIND_CACHE` nodes reporting their byte totals.
- *Doc scope*: **system fonts are process-global (`doc_id = 0`); document fonts are doc-scoped.** This split already exists in `font_context_reset_document_fonts` — the context just gives it a `doc_id`, so a closing document's font glyphs are reclaimed while system fonts persist.

**Image / audio / video cache.** `UiContext.image_cache` (`radiant/view.hpp:1487`) is a path-keyed hashmap of `ImageEntry{path, ImageSurface*}` (`surface.cpp:17`); `ImageSurface` (`view.hpp:357`) holds the decoded RGBA `pixels` plus `source_data`/`source_path` for **lazy decode** (header parsed, pixels left NULL until needed — `surface.cpp:644`). Cleaned at document teardown (`surface.cpp:756` `image_cache_cleanup`). ThorVG adds **process-wide** vector/paint caches with fixed entry caps (`g_picture_*_cache`, `g_paint_cache` ≤256, `g_image_paint_cache` ≤128 — `rdt_vector_tvg.cpp:87-145`, mutex-guarded).
- *Tracking*: register `image_cache` and the ThorVG caches as `MEM_KIND_CACHE` / `MEM_ROLE_MEDIA` nodes. Decoded `pixels` are the heavy item — report `width × height × 4` per live surface.
- *Doc scope*: page images are doc-scoped (keyed by the page that loaded them); the ThorVG global caches are `doc_id = 0`.
- *Audio/video*: no decode cache exists today (Radiant handles images + animated GIF/Lottie only). `MEM_ROLE_MEDIA` and the `MEM_KIND_CACHE` machinery are defined to cover them when added — no schema change needed later.

---

## 4. Design Overview: The Memory Context as Factory

`MemContext` is the owner and birthplace of every allocator. It holds:
- A **mutex** guarding the owned-node list and lifecycle operations.
- An **intrusive doubly-linked list** of `MemNode` records — one per *owned* allocator.
- A **node slab** (internal free-list of fixed-size `MemNode`s) so registry bookkeeping never re-enters a tracked allocator.
- A monotonic **id** counter and **birth/generation** counter.
- A **root** singleton (`mem_context_root()`), plus scoped **sub-contexts** (per worker thread / per document / per request) that link into a parent context and can be torn down as a unit.

Every allocator gains one field: `MemNode* mem_node`. Unlike the observer design, this is **always populated** for any allocator the application uses, because the only way to obtain an allocator is to ask the context for one.

```
MemContext (root)
  ├─ owns Pool#3   role=INPUT   label="input.json.pool"     parent=NULL
  │     └─ owns Arena#9 role=VIEW label="view_tree.arena"   parent=Pool#3
  │            └─ owns Scratch#14 role=LAYOUT "layout.scratch" parent=Arena#9
  ├─ owns Pool#7   role=RUNTIME label="eval.ast_pool"        parent=NULL
  │     └─ owns Heap#8 role=RUNTIME_HEAP label="eval.heap"   parent=Pool#7
  └─ owns Nursery#11 role=RUNTIME_HEAP label="eval.nursery"  parent=NULL
```

### What ownership buys us over registration
1. **No off-graph allocators**: there is no `pool_create()` in application code to forget to register.
2. **Parent edge is automatic**: `mem_arena_create(ctx, backing_pool, …)` reads `backing_pool->mem_node` and wires the edge — call sites cannot get it wrong.
3. **Cascade teardown**: `mem_context_destroy(ctx)` (or destroying a sub-context) frees owned allocators in reverse-dependency order, structurally eliminating the [§I2](./Memory_Pooling.md) ordering hazard.
4. **Consistent policy**: chunk-size defaults, mmap-vs-rpmalloc choice, debug poisoning, and budget caps can be applied uniformly in one place instead of per call site.

---

## 5. Core Data Structures

```c
// lib/mem_context.h

typedef enum MemKind {
    MEM_KIND_POOL = 1,     // lib/mempool.c  (rpmalloc or mmap)
    MEM_KIND_ARENA,        // lib/arena.c
    MEM_KIND_SCRATCH,      // lib/scratch_arena.c
    MEM_KIND_HEAP,         // gc_heap (Lambda runtime objects)
    MEM_KIND_NURSERY,      // gc_nursery (numeric temporaries)
    MEM_KIND_NAMEPOOL,     // interned names
    MEM_KIND_SHAPEPOOL,    // cached shapes
    MEM_KIND_JIT,          // MIR-generated executable code (mmap'd code pages)
    MEM_KIND_CACHE,        // self-managed cache (hashmap + LRU): font / image / vector
} MemKind;

typedef enum MemRole {
    MEM_ROLE_UNKNOWN = 0,
    MEM_ROLE_INPUT,        // input parser (JSON/XML/HTML/…)
    MEM_ROLE_AST,          // code AST (Lambda / Ruby / Python / JS)
    MEM_ROLE_TYPE_SHAPE,   // TypeMap / ShapeEntry / type metadata
    MEM_ROLE_VIEW,         // Radiant view tree
    MEM_ROLE_NODE,         // DOM nodes
    MEM_ROLE_LAYOUT,       // layout scratch
    MEM_ROLE_RENDER,       // render scratch / pixel buffers
    MEM_ROLE_FONT,         // glyphs, font metadata
    MEM_ROLE_CSS,          // CSS engine / stylesheets
    MEM_ROLE_PDF,          // PDF parsing / view
    MEM_ROLE_VALIDATOR,    // schema validation
    MEM_ROLE_RUNTIME_HEAP, // GC heap / nursery
    MEM_ROLE_CODE,         // MIR/JIT compiled code + debug_info
    MEM_ROLE_MEDIA,        // decoded image / audio / video buffers
    MEM_ROLE_TEMP,         // short-lived scratch
} MemRole;

// One ownership record per allocator. Intrusive list node, slab-allocated.
typedef struct MemNode {
    struct MemNode* next;
    struct MemNode* prev;
    void*           allocator;     // the Pool*/Arena*/… this context owns
    struct MemNode* parent;        // backing allocator's node, or NULL
    struct MemContext* owner;      // owning context (for sub-context teardown)
    uint32_t        child_count;   // live children — must be 0 to destroy
    MemKind         kind;
    MemRole         role;
    const char*     label;         // static string
    uint32_t        id;            // stable per-process id
    uint32_t        doc_id;        // owning document (0 = not doc-scoped). See §10.4
    uint32_t        thread_id;     // creating thread
    uint64_t        birth_seq;     // creation order
} MemNode;

typedef struct MemContext {
    void*     mutex;               // opaque (pthread_mutex_t / SRWLOCK)
    MemNode*  head;                // intrusive list of owned nodes
    struct MemContext* parent;     // parent context, or NULL for root
    void*     node_slab;           // internal free-list of MemNode records
    uint32_t  next_id;
    uint32_t  doc_id;              // document this context represents (0 = none);
                                   // owned allocators inherit it. See §10.4
    uint64_t  birth_counter;
    uint32_t  live_count;
    bool      enabled;
} MemContext;

MemContext* mem_context_root(void);
MemContext* mem_context_create(MemContext* parent, MemRole role, const char* label);
void        mem_context_destroy(MemContext* ctx);   // cascade teardown
```

### Snapshot record — compact, fixed-width, copyable

Flat POD records (ids only, no live pointers) so the snapshot can be memcpy'd, dumped, diffed, and shipped without holding the lock or risking use-after-free.

```c
typedef struct MemStatSample {
    uint32_t id;
    uint32_t parent_id;        // 0 = root-level
    uint32_t doc_id;           // owning document (0 = not doc-scoped). See §10.4
    uint16_t kind;             // MemKind
    uint16_t role;             // MemRole
    uint32_t thread_id;
    uint64_t birth_seq;

    uint64_t bytes_reserved;   // capacity held from OS
    uint64_t bytes_in_use;     // live/bump-used bytes (best effort)
    uint64_t alloc_count;      // cumulative allocations (upper bound for rpmalloc)
    uint32_t chunk_count;
    uint32_t flags;            // bit0 mmap, bit1 drained, bit2 leaked, bit3 has-children

    char     label[44];        // copied, truncated — self-contained for offline view
} MemStatSample;               // 104 bytes (doc URL resolved separately via §10.4 registry)

typedef struct MemSnapshot {
    uint64_t       captured_seq;
    uint32_t       count;
    uint32_t       _pad;
    MemStatSample* samples;
    uint64_t       total_reserved;
    uint64_t       total_in_use;
} MemSnapshot;
```

### Document URL registry

Because Lambda/Radiant is a document-processing engine, the overwhelming majority of allocations belong to *some document* — an HTML page and its sub-resources (CSS, JS, images, fonts), a Lambda/Markdown/JSON/PDF input, etc. So allocators carry a compact `doc_id` (4 bytes, not a string) and a separate small registry resolves `doc_id → URL`. Keeping the URL out of every `MemNode`/`MemStatSample` keeps those records fixed-width and cheap; the URL is looked up only when a snapshot is *reported*.

```c
typedef struct MemDocEntry {
    uint32_t    doc_id;        // stable per-process document id (monotonic)
    const char* url;           // owning-copy of the document URL / source path
    uint32_t    parent_doc;    // sub-resource → parent page (0 = top-level doc)
    uint64_t    birth_seq;     // when the doc was registered
    bool        live;          // false once the doc is torn down (kept for diffing)
} MemDocEntry;

// Register a document; returns its id. URL is copied into the registry.
uint32_t mem_doc_register(MemContext* ctx, const char* url, uint32_t parent_doc);
// Resolve an id back to its URL (NULL if unknown). Cheap array/hash lookup.
const char* mem_doc_url(uint32_t doc_id);
// Mark a doc torn down (keeps the entry for post-mortem / diff; id never reused).
void mem_doc_retire(uint32_t doc_id);
```

The registry is owned by the root context (one URL table per process), guarded by the same mutex discipline as the node list ([§8](#8-thread-safety)). `parent_doc` lets sub-resources (an `@import`ed stylesheet, a web-font, an `<img>`) roll up under their owning page in reports.

---

## 6. The Factory API

This is the centerpiece of the revision. Application code never calls `pool_create`/`arena_create`/`scratch_init` directly — it calls the factory, which creates the allocator, records the `MemNode`, and wires the parent edge.

```c
// ---- Pools ----
Pool* mem_pool_create(MemContext* ctx, MemRole role, const char* label);
Pool* mem_pool_create_mmap(MemContext* ctx, MemRole role, const char* label);

// ---- Arenas (parent edge derived from the backing pool's mem_node) ----
Arena* mem_arena_create(MemContext* ctx, Pool* backing,
                        MemRole role, const char* label);
Arena* mem_arena_create_sized(MemContext* ctx, Pool* backing,
                              size_t init_chunk, size_t max_chunk,
                              MemRole role, const char* label);

// ---- Scratch (lives inside an Arena; node parented to that arena) ----
void  mem_scratch_init(MemContext* ctx, ScratchArena* sa, Arena* backing,
                       MemRole role, const char* label);

// ---- GC heap / nursery ----
Heap*         mem_heap_create(MemContext* ctx, Pool* backing,
                              MemRole role, const char* label);
gc_nursery_t* mem_nursery_create(MemContext* ctx, size_t block_size,
                                 MemRole role, const char* label);

// ---- Name / shape pools ----
NamePool*  mem_name_pool_create(MemContext* ctx, Pool* backing, const char* label);
ShapePool* mem_shape_pool_create(MemContext* ctx, Pool* backing, const char* label);

// ---- Destruction (unwires node, validates no live children, frees allocator) ----
void mem_pool_destroy(Pool* pool);          // looks up pool->mem_node
void mem_pool_drain(Pool* pool);            // checks for live children first (§7)
void mem_arena_destroy(Arena* arena);
void mem_scratch_release(ScratchArena* sa);
void mem_heap_destroy(Heap* heap);
void mem_nursery_destroy(gc_nursery_t* nursery);
```

### Internals (illustrative — `mem_pool_create`)

```c
Pool* mem_pool_create(MemContext* ctx, MemRole role, const char* label) {
    Pool* p = pool_create_raw();              // renamed internal primitive
    if (!p) return NULL;
    lock(ctx->mutex);
    p->mem_node = node_slab_alloc(ctx);       // never re-enters a tracked allocator
    *p->mem_node = (MemNode){
        .allocator = p, .parent = NULL, .owner = ctx,
        .kind = MEM_KIND_POOL, .role = role, .label = label,
        .id = ++ctx->next_id, .doc_id = ctx->doc_id,   // inherit doc from context
        .birth_seq = ++ctx->birth_counter,
        .thread_id = current_thread_id(),
    };
    list_push(ctx, p->mem_node);
    ctx->live_count++;
    unlock(ctx->mutex);
    return p;
}

Arena* mem_arena_create(MemContext* ctx, Pool* backing, MemRole role, const char* label) {
    Arena* a = arena_create_default(backing);  // existing primitive
    if (!a) return NULL;
    lock(ctx->mutex);
    a->mem_node = node_slab_alloc(ctx);
    *a->mem_node = (MemNode){ .allocator = a, .parent = backing->mem_node, /*…*/ };
    if (backing->mem_node) backing->mem_node->child_count++;   // track the edge
    list_push(ctx, a->mem_node);
    unlock(ctx->mutex);
    return a;
}
```

### Compile-time kill switch
Under `MEM_CONTEXT_DISABLED` (release-minimal builds), the factory functions become thin inline wrappers that call the raw primitives and skip all bookkeeping, and `mem_node` fields compile out. The call-site signatures stay the same, so the source is identical across builds — only the bookkeeping disappears.

```c
#ifdef MEM_CONTEXT_DISABLED
  #define mem_pool_create(ctx, role, label)        pool_create_raw()
  #define mem_arena_create(ctx, backing, role, lbl) arena_create_default(backing)
  /* … */
#endif
```

---

## 7. Ownership, Lifecycle & Cascade Teardown

This is what the factory model unlocks that registration alone cannot.

### 7.1 The `child_count` invariant
Each node tracks how many live allocators are backed by it. Destroying or draining a node requires `child_count == 0`. This makes the [§I2](./Memory_Pooling.md) hazard a **runtime-enforced invariant**:

```c
void mem_pool_drain(Pool* pool) {
    MemNode* n = pool->mem_node;
    if (n && n->child_count > 0) {
        log_error("MEMCTX: drain of pool#%u '%s' with %u live children — "
                  "destroy children first", n->id, n->label, n->child_count);
        return;   // refuse — would corrupt arenas backed by this pool
    }
    /* … existing drain … */
}
```

### 7.2 Cascade teardown
`mem_context_destroy(ctx)` walks owned nodes in **reverse birth order** (children are always born after parents, so reverse order frees them first) and destroys each via its kind-specific primitive. A sub-context created per document/request can thus free its entire allocator subtree in one call — no manual ordering, no leaks.

```c
void mem_context_destroy(MemContext* ctx) {
    lock(ctx->mutex);
    for (MemNode* n = newest_first(ctx); n; n = n->prev) {
        switch (n->kind) {
            case MEM_KIND_SCRATCH: scratch_release_raw(n->allocator); break;
            case MEM_KIND_ARENA:   arena_destroy_raw(n->allocator);   break;
            case MEM_KIND_HEAP:    heap_destroy_raw(n->allocator);    break;
            case MEM_KIND_POOL:    pool_destroy_raw(n->allocator);    break;
            /* … */
        }
        node_slab_free(ctx, n);
    }
    unlock(ctx->mutex);
    /* detach from parent, free ctx */
}
```

### 7.3 Scoped contexts
- **Root context**: process-lifetime singletons (global font arena, shape pool).
- **Per-document / per-request sub-context**: created on `Input::create` or per layout pass; destroyed when the document is dropped — bulk-frees the whole input/view/layout allocator subtree. This is the natural carrier of `doc_id`: the sub-context calls `mem_doc_register(ctx, url, parent_doc)` once, stores the returned id in `ctx->doc_id`, and every allocator the factory creates under it inherits that id automatically ([§5 registry](#document-url-registry), [§6](#6-the-factory-api)). Sub-resources (CSS/JS/font/image loaded by the page) register with the page's `doc_id` as `parent_doc`, so reports roll them up under the page.
- **Per-worker-thread sub-context**: link/unlink hit the thread-local context lock (uncontended); only root aggregation locks across threads. Mirrors rpmalloc's per-thread heap model and addresses the [§P2](./Memory_Pooling.md) `rpmalloc_thread_finalize()` concern (the context teardown is the natural hook).

---

## 8. Thread Safety

Request rule #1 — first-class treatment.

### 8.1 What is synchronized
- **Create / destroy / drain / reparent** mutate the owned-node list and `child_count` → guarded by the context mutex.
- **Snapshot** reads the list → holds the mutex only during the cheap copy-out.
- **`pool_alloc` / `arena_alloc` / `scratch_alloc` stay lock-free** — the factory governs lifecycle (creation/destruction), which is infrequent, not per-byte allocation.

### 8.2 Lock strategy
- One mutex per `MemContext`. Fast OS primitive (`pthread_mutex_t` / `SRWLOCK`) behind an opaque pointer so only `mem_context.c` includes platform headers.
- Hold time bounded: create/destroy is O(1) list splice + `child_count` bump; snapshot is O(n) memcpy.
- Per-thread sub-contexts ([§7.3](#73-scoped-contexts)) keep the hot create/destroy path uncontended; cross-thread contention only at root-level snapshot aggregation.

### 8.3 Reentrancy discipline
- `MemNode`s come from the context's **internal slab**, never from a tracked allocator — `mem_*_create` cannot recurse into itself.
- The snapshot buffer is allocated from a **dedicated pool private to `mem_context.c`**, also off-graph.
- Stat queries during snapshot (`pool_get_stats`, etc.) read only counters owned by that allocator and never take the registry lock — no lock-ordering inversion.

### 8.4 Snapshot consistency
Two-phase capture: (1) under lock, copy identity + parent-id + role + queried stats into the `MemStatSample` array and stamp `captured_seq`; (2) release lock, then do roll-ups / sorting / JSON / shipping on the detached immutable copy. A node destroyed between phases is harmless — its POD data was already copied.

---

## 9. Snapshots

### 9.1 API

```c
MemSnapshot*     mem_snapshot_capture(MemContext* ctx);  // O(n) under lock, detached copy
void             mem_snapshot_free(MemSnapshot* snap);
MemSnapshotDiff* mem_snapshot_diff(const MemSnapshot* before, const MemSnapshot* after);
```

### 9.2 Cost
Allocator count is small (tens–low hundreds). 96-byte records → a few KB. Capture is sub-millisecond, no per-object traversal in the default shallow mode.

### 9.3 Querying allocator size at capture
| Kind | bytes_reserved | bytes_in_use | source |
|------|----------------|--------------|--------|
| Pool (rpmalloc) | heap span total | n/a (upper bound) | `pool_get_stats` + reserved accessor |
| Pool (mmap) | mmap chunk total | cursor − base | new `pool_get_mmap_stats` |
| Arena | sum of chunk sizes | bump offsets | new `arena_get_stats` |
| ScratchArena | backing-arena delta | head walk (bounded) | new `scratch_get_stats` |
| Heap (gc_heap) | gc arena reserved | live object bytes | gc_heap accessor |
| Nursery | blocks × block_size × elem | used elements | `gc_nursery_total_allocated` |
| JIT (MIR) | mmap'd code pages | code bytes | MIR allocator hook, or estimate (Q7) |
| Cache (font/image/vector) | hashmap + entry bytes | decoded pixels / glyph bitmaps | per-cache size accessor (font `glyph_arena`, image `w×h×4`) |

A few small accessors must be added; none touch hot paths.

### 9.4 Deep vs shallow snapshots
- **Shallow** (default): aggregate counters only. Cheap, always safe.
- **Deep** (opt-in, debug): walk a Heap's object list to bucket bytes by `TypeId` (array/map/element/string/…). Bounded, explicitly requested — never on the live `top` path.

---

## 10. Snapshot Consumers (JSON / DevTools / `top`)

All three consumers operate on the same immutable `MemSnapshot`.

### 10.1 JSON dump → log
Walk `samples[]`, emit a tree (parent_id → children) as JSON to `./log.txt` or `./temp/mem_snapshot_*.json`. Triggered by `--mem-dump`, a signal handler (`SIGUSR2`) for a running process, or programmatically at teardown for leak detection.

```json
{
  "captured_seq": 10472,
  "total_reserved": 41943040,
  "total_in_use": 28311552,
  "nodes": [
    { "id": 3, "kind": "pool", "role": "input", "label": "input.json.pool",
      "bytes_reserved": 1048576, "alloc_count": 5123, "thread": 1,
      "children": [
        { "id": 9, "kind": "arena", "role": "view", "label": "view_tree.arena",
          "bytes_reserved": 524288, "chunk_count": 12 }
      ] }
  ]
}
```

### 10.2 In-memory interactive inspector (browser-devtools style)
Ring buffer of the latest N snapshots + a query surface ("all FONT allocators > 1 MB", "subtree under eval.heap", "diff t0 vs t1 — what grew?"). Flat POD + id joins make queries simple array scans. Surface via the existing WebDriver/inspector plumbing (`radiant/webdriver/`) or a REPL command.

### 10.3 Live `top`-style view
`--mem-top`: capture every ~500 ms, render a sorted table, highlight rows that grew (via `mem_snapshot_diff`):

```
 MEM TOP — 23 allocators — reserved 40.0M / in-use 27.0M        seq 10472
 ID  KIND     ROLE      LABEL                 RESERVED   IN-USE   ALLOCS  THR
  3  pool     input     input.json.pool          1.0M     0.8M     5123    1
  9  arena    view      view_tree.arena        512.0K   480.0K     1840    1
  7  heap     runtime   eval.heap               16.0M    12.0M    93011    1
 12  scratch  render    render.scratch           4.0M     0.0M        0    2
```

### 10.4 Grouping by Document (doc ID / URL)

Since nearly every allocator is doc-scoped, **document is the primary reporting axis** — "which page is eating 200 MB?" is the question that actually gets asked. Every consumer can group `samples[]` by `doc_id` and resolve the URL once via `mem_doc_url()` (the flat-POD snapshot makes this a trivial `doc_id`-keyed fold; sub-resources fold under their `parent_doc`).

**JSON dump — grouped by doc:**

```json
{
  "captured_seq": 10472,
  "total_reserved": 41943040,
  "documents": [
    {
      "doc_id": 1, "url": "https://example.com/report.html",
      "reserved": 33554432, "in_use": 24117248, "allocators": 14,
      "by_role": { "view": 8388608, "node": 4194304, "css": 2097152, "font": 6291456 },
      "sub_resources": [
        { "doc_id": 5, "url": "report.css",         "reserved": 2097152 },
        { "doc_id": 6, "url": "fonts/Inter.woff2",  "reserved": 6291456 }
      ]
    },
    { "doc_id": 0, "url": null, "label": "(process-global)",
      "reserved": 8388608, "in_use": 4194304, "allocators": 6 }
  ]
}
```

**`top` — doc view (`--mem-top --by-doc`):**

```
 MEM TOP (by document) — 3 docs — reserved 40.0M / in-use 27.0M       seq 10472
 DOC  URL                              RESERVED   IN-USE   ALLOCS  ROLES
   1  https://example.com/report.html    32.0M    23.0M       14  view,node,css,font
   2  /home/u/notes.md                    6.0M     3.5M        5  input,view
   0  (process-global)                    2.0M     0.5M        6  font,runtime
```

**Interactive query** ([§10.2](#102-in-memory-interactive-inspector-browser-devtools-style)) gains doc-keyed predicates: "show the subtree for `doc_id=1`", "diff doc 1 between t0 and t1", "which doc grew most since last snapshot", "list docs whose URL matches `*.css`". `doc_id=0` buckets the process-global singletons (font database, shape pool) that belong to no document.

This directly answers the leak question for a document engine: after a page is closed, capture a snapshot and assert its `doc_id` has **zero live allocators** — any survivor is a per-document leak, attributable to an exact URL.

---

## 11. Migration Strategy

Because the factory is **mandatory**, this is a real (but mechanical) migration of all ~40 creation sites, not an optional annotation pass. It is staged so the tree stays green throughout.

**Phase 1 — Core registry (no call-site changes yet)** — ✅ **IMPLEMENTED**
- `lib/mem_context.h` / `lib/mem_context.c`: contexts (root + sub-contexts), global-mutex thread safety, allocator-agnostic node registry (`mem_register`/`mem_unregister`/`mem_node_annotate`/`mem_reparent`), `child_count` edge tracking, cascade teardown (`mem_context_destroy`, children-before-parent), document URL registry (`mem_doc_register`/`mem_doc_url`/`mem_doc_parent`/`mem_doc_retire`), flat POD snapshot (`mem_snapshot_capture`), JSON serializer (`mem_snapshot_to_json`), and a `MEMCTX:`-prefixed log dump (`mem_context_log`).
- Design choice: the core is **allocator-agnostic** — each node carries optional `MemStatFn`/`MemDestroyFn` callbacks, so the core includes no allocator headers and touches **zero existing code**. The Pool/Arena/Heap/etc. factory wrappers (and the `mem_node` field on those structs) layer on in Phase 2.
- Thread safety: a single process-global non-recursive mutex guards all context/node/doc-registry state. Bookkeeping (`MemNode` records, snapshot buffers, doc URLs) uses system `malloc`/`free` directly — never a tracked allocator — so registration can never re-enter itself.
- Tests: `test/test_mem_context_gtest.cpp` — 13 GTest cases (register/unregister counts, parent/child edge, reparent, disabled context, cascade teardown + child-context ordering, doc registry, snapshot counts/totals/parent-linkage/child-context inclusion, JSON fields, and an 8-thread × 2000-iter concurrent register/unregister/snapshot stress test). **All 13 pass, and pass clean under ThreadSanitizer** (no data races).
- Deferred to later phases: the node slab (Phase 1 uses `malloc`/`free` per node — simple and correct; slab is an optimization), `MEM_CONTEXT_DISABLED` passthrough macros, and renaming primitives to `*_raw`.

**Phase 2 — Pool & Arena migration** — ✅ **INFRASTRUCTURE DONE; site conversion in progress**
- `mem_node` field (opaque `void*`) added to `struct Pool` (`lib/mempool.c`) and `struct Arena` (`lib/arena.c`), initialized NULL, with `pool_get/set_mem_node` + `arena_get/set_mem_node` accessors. Additive — untracked allocators just carry NULL.
- Stat accessors: `pool_get_mem_stats(reserved, in_use, alloc_count, is_mmap)` (mmap reserved = sum of chunk sizes; rpmalloc reserved = high-water `alloc_bytes`); arena reuses existing `arena_total_allocated`/`arena_total_used`/`arena_chunk_count`.
- Factory: `lib/mem_factory.{h,c}` — `mem_pool_create`/`mem_pool_create_mmap`/`mem_pool_destroy`, `mem_arena_create`/`mem_arena_create_sized`/`mem_arena_destroy`. Each wraps the raw primitive, registers a node with the right role/label and arena→pool parent edge, and stores the node on the allocator. `mem_*_destroy` is NULL-safe for untracked allocators.
- Tests: `test/test_mem_factory_gtest.cpp` — 6 GTest cases with **real** Pool/Arena (registration, mmap flag, arena→pool parent edge, cascade teardown of real allocators, untracked-pool safety, sized arena). All pass.
- First call site converted: `lambda/lambda-proc.cpp` `pn_output_internal` temp pool (1 create + 7 destroy paths) → `mem_pool_create(NULL, MEM_ROLE_TEMP, "proc.output.format")` / `mem_pool_destroy`. Engine builds clean; lambda baseline unchanged at 2941/2942 (the 1 failure — `dom_jquery_lib`, a jQuery `.css()` output mismatch — is pre-existing and unrelated).
- Remaining: convert the rest of the ~40 sites incrementally (baseline-green per batch); add `make check-no-raw-alloc` grep gate ([§14.8](#14-additional-suggestions)); rename raw primitives to `*_raw` once all sites are migrated.

**Phase 3 — Heap, nursery, scratch, name/shape pools** — 🟡 **STARTED (Phase 3a done)**
- **Pool sweep COMPLETE**: the last 6 raw `pool_create` sites (the `py/js/bash/rb` AST scope pools + `py/js` transpiler `temp_ctx->pool`) are converted — **zero raw `pool_create()` remain in application code** (40 files via the factory).
- **First arenas converted** via `mem_arena_create` (parent edge → backing pool): the primary `Input` arena (`input.cpp:894`, role INPUT — backs every parsed document tree), and the Python/Ruby AST arenas (`py_scope.cpp`/`rb_scope.cpp`, role AST). Verified: Lambda 2941/2942, Radiant 5664/5664 exit 0, `convert --mem-dump` exit 0.
- **New factory wrappers (Phase 3b) — `mem_scratch_init` + `mem_nursery_create` DONE.** Both clean lib-level types now have a `mem_node` field, a release hook (`scratch_set_node_release_hook` / `gc_nursery_set_node_release_hook`, installed by the factory's `ensure_release_hooks`), a `MemStatFn`, and a factory wrapper in `lib/mem_factory.{h,c}`. Scratch nodes (`MEM_KIND_SCRATCH`) parent to their backing arena and report `live_count` (reserved/in-use 0 to avoid double-counting the backing arena); nursery nodes (`MEM_KIND_NURSERY`) report `total_allocated × sizeof(gc_num_value_t)`. Auto-unregister on `scratch_release` / `gc_nursery_destroy`. Added `extern "C"` guards to `lib/gc/gc_nursery.h` (was missing — C++ callers relied on transitive wrapping). Tests: `test_mem_factory_gtest` now 9 cases (incl. scratch register/release, nursery create/destroy, untracked-nursery safety) — all pass. Baselines: Lambda 2941/2942, Radiant 5671/5671 exit 0.
- **Runtime factory wrappers (Phase 3c) — `mem_gc_heap_create` / `mem_name_pool_create` / `mem_shape_pool_create` DONE.** New TU `lambda/mem_factory_rt.{h,cpp}` (compiled into the main build only, keeping the lib-only `mem_factory` test clean). Each underlying type gained a `mem_node` field + a release hook: `gc_heap_set_node_release_hook` (fires in `gc_heap_destroy`), and **ref-count-aware** `name_pool_set_node_release_hook` / `shape_pool_set_node_release_hook` (fire only inside the `ref_count == 0` branch of `name_pool_release`/`shape_pool_release`). Wired `heap_init`/`heap_init_with_pool` → `mem_gc_heap_create*` (role RUNTIME_HEAP, "eval.heap"), and `Input::create`'s `name_pool`/`shape_pool` → the wrappers (roles INPUT / TYPE_SHAPE). Verified live via `--mem-dump`: heap/namepool/shapepool nodes appear correctly and the run exits cleanly.
  - **Critical fix — subtree unregister.** Registering allocators whose *structs live in a backing pool's memory* (input arena/name-pool/shape-pool created from `script.pool` at `runner.cpp:481`) exposed a dangling-node UAF: when `script.pool` is bulk-`pool_destroy`'d, those structs are freed but their nodes survived → snapshot crash in `shape_pool_count`. Fixed by `mem_unregister_subtree()` (lib/mem_context.c): the pool/arena release hooks now unregister the node **and all transitive children** by parent edge, so bulk-destroying a pool removes the arenas/name-pools/shape-pools it backs. Caught by `--mem-dump` (would never show in baselines).
  - ✅ **Call-site sweeps done.** Converted all 6 `scratch_init` sites (→ `mem_scratch_init`, roles LAYOUT/RENDER), all 16 `gc_nursery_create` sites across 6 files (→ `mem_nursery_create`, role RUNTIME_HEAP, per-language labels `eval/batch/py/js/bash/rb.nursery`), and the lambda/radiant `arena_create*` sites (15 files: `document.arena`, `view_tree.arena`, 4 state_store arenas, page-backdrop arenas, tile/event/render/webdriver/retained-display-list arenas → `mem_arena_create`/`mem_arena_create_sized`). Verified: `--mem-dump` on layout + script exit 0 (no UAF; full arena/namepool/shapepool graph shown); Lambda 2941/2942, Radiant 5668/5668 exit 0.
  - **Deferred — lib-level arenas** (`lib/font/font_context.c` glyph arena, `lib/pdf_writer.c`, `lib/memtrack.c`): converting these adds a factory dependency to lib files that standalone lib tests compile without `mem_factory`/`mem_context` → would break those test links. Needs per-test dependency updates or a guard; left raw for now.
  - ✅ **Per-document sub-contexts (attribution) DONE.** `Input::create` (`lambda/input/input.cpp`) now: registers the document URL (`mem_doc_register(abs_url->href->chars, parent_doc)`, with `parent_doc` from the parent input's doc id), creates a per-document `mem_context_create` sub-context tagged with that `doc_id`, and creates the input's `arena`/`name_pool`/`shape_pool` under it (`Input` gained a `void* mem_ctx` field). The backing pool (shared `global_pool`) stays root/doc-0; the input allocators carry the doc id and parent-edge to the pool. Verified live: `--mem-dump` on a 3-file script attributes each input's arena/name-pool/shape-pool to its source URL (`test.csv` doc1, `test_tab.csv` doc2, `test_no_header.csv` doc3), exit 0. Lambda 2941/2942, Radiant 5662/5662.
  - ✅ **True per-document reclamation — achieved where semantically valid (Radiant DomDocument).** Investigation established the boundary:
    - **Radiant `DomDocument`** has a genuine per-document lifecycle with a single teardown point (`free_document()`, `radiant/ui_context.cpp:241`). Its `pool`/`arena`/`view_tree`/`state_store` are **already reclaimed per-document** there, and the release hooks unregister their nodes — verified: `layout --mem-dump` exits with an empty dump (everything freed, no crash). `dom_document_create` now **reuses the input's `mem_ctx` sub-context** for the DOM `pool`/`arena`, so DOM memory is grouped under the same document (URL) as the parsed source and reclaimed together. Cascade teardown (`mem_context_destroy`) is implemented + tested, but `free_document`'s carefully-ordered teardown is kept (the release hooks already make it registry-correct).
    - **lambda `input()` data — per-document reclamation is NOT possible** (confirmed, not a gap to fix): the parsed Items flow into the script's value graph held by the GC heap (`fn_input2` → `input_from_url` → `InputManager` global_pool; returned Items reference input-pool memory kept alive by the script). Freeing per-document would invalidate live references. The shared `global_pool` is the *correct* design here; the Memory Context provides **doc-id attribution** (per-input sub-context) instead of reclamation.
  - **Remaining (optional):** a few short-lived lambda flows (`convert`, `validate`, npm parse) parse→use→discard and currently leak their input into `global_pool` until exit; they *could* use a dedicated per-call pool freed after use (genuine reclamation), but each needs a flow change to bypass the shared `global_pool`. Plus: wire `LayoutContext`/`RenderContext`/`ViewTree` pools to the doc sub-context for fuller grouping; deferred lib-level arenas; `make check-no-raw-alloc` gate. Plus the deferred lib-level arenas and the `make check-no-raw-alloc` grep gate.

**Phase 3b — Input, document & network/cache**
- Give each `Input` a per-document sub-context instead of the shared `InputManager::global_pool` ([§3.1](#31-input-download--cache-memory-also-centralized)) — the single change that enables per-document reclamation.
- Seed the doc URL registry: have `DomDocument`/`NetworkResourceManager` call `mem_doc_register(ctx, url, parent_doc)` (reusing the existing `document_id`/`navigation_id`), with downloaded sub-resources as child docs.
- Migrate `MEM_CAT_NETWORK` / `MEM_CAT_INPUT_OTHER` `mem_alloc` sites to context-routed, role-tagged, doc-scoped allocations; retire or map the `MEM_CAT_*` enum onto `MemRole`.
- Register the `doc.cache` reclaimer on `EnhancedFileCache` (it already has LRU + `current_size_bytes`).

**Phase 3c — JIT code, font & media caches**
- Register `Script.jit_context` as a `MEM_KIND_JIT` node; investigate the MIR allocator hook for exact code-size accounting ([Q7](#1511-risks--open-questions)), else record an estimate.
- Make `FontContext.glyph_arena` factory-created; register the font hashmap caches as `MEM_KIND_CACHE` nodes with `doc_id=0` for system fonts and the page's `doc_id` for document fonts; wire the `font.glyph_cache` reclaimer.
- Register `UiContext.image_cache` and the ThorVG vector/paint caches as `MEM_KIND_CACHE`/`MEM_ROLE_MEDIA` nodes; wire the `image.cache`/`vector.cache` reclaimers.

**Phase 4 — Cascade teardown adoption**
- Replace manual multi-allocator teardown sequences (the fragile [§I2](./Memory_Pooling.md) ordering) with `mem_context_destroy(sub_ctx)`. Remove hand-ordered destroy calls.

**Phase 5 — Consumers** — ✅ **JSON dump + leak report DONE; `--mem-top` TUI pending**
- `mem_context_dump_json_file(ctx, path)` and `mem_context_report_leaks(ctx)` added to `lib/mem_context.{h,c}`. `lambda/main.cpp` parses `--mem-dump[=PATH]` (early, stripped from argv like `--no-log`; default `./temp/mem_snapshot.json`) and, at the process-exit cleanup path (after `runtime_cleanup`, before `mempool_cleanup`), writes the JSON snapshot and logs a `MEMCTX-LEAK:` report of survivors.
- Verified live: `lambda --mem-dump=… <script that parses input>` emits the `input.global_pool` node (correct reserved/in-use/alloc-count) and the leak report flags it as still-live at exit — a true finding, since `InputManager::destroy_global()` is never called and the singleton's pool persists to process exit.
- Pending: `--mem-top` live TUI; optional interactive inspector query surface ([§10.2](#102-in-memory-interactive-inspector-browser-devtools-style)).

Each phase is independently shippable and behavior-neutral; the registry is a no-op under `MEM_CONTEXT_DISABLED`.

> **Allocator-release hook (key safety hardening, Batch 5).** `pool_destroy`/`pool_drain`/`arena_destroy` now call an optional release hook (a nullable function pointer, set by the factory via `pool_set_node_release_hook`/`arena_set_node_release_hook`) that unregisters the allocator's `mem_node`. This means **any** destroy path — factory *or* raw — safely removes the node, eliminating the entire "missed raw destroy on a factory pool → dangling node → UAF on next snapshot" hazard (which baselines wouldn't catch, only `--mem-dump` would). `mempool.c`/`arena.c` stay decoupled from `mem_context` (NULL hook in lib-only unit tests). A thread-local `g_in_teardown` guard makes the hook a no-op during cascade teardown (the cascade already frees nodes), preventing a non-recursive-mutex re-entry **deadlock** that this exposed. Verified live: `--mem-dump` on a script shows the surviving result pool while the destroyed `script.pool` is correctly absent (clean unregister, no crash).
>
> **Migration progress (Pool sites): 34 files converted — Pool sweep essentially complete.** Batch 6 (enabled by the release hook — only create sites need touching) converted the cross-function / struct-field pools: radiant `view_pool.cpp` (`tree->pool` → VIEW), `cmd_layout.cpp` (6 pools → LAYOUT), `window.cpp`/`tile_pool.cpp` (RENDER), `rdt_vector_tvg.cpp`/`rdt_vector_cg.mm` (MEDIA — the cached `pic->pool` raw-destroy now auto-unregisters), `webdriver_server/session.cpp` (TEMP); lambda `input/css/dom_element.cpp` (`document->pool` → NODE), `input/input.cpp` (sys:// pools → INPUT), `module_registry.cpp` (AST), `validator/doc_validator.cpp` (`transpiler->pool` → VALIDATOR). Verified: both baselines green (Lambda 2941/2942, Radiant 5670/5670 exit 0) and `layout --mem-dump` exit 0 with a clean empty dump (all view/layout/render pools created→destroyed→unregistered, no dangling node). **Only 6 raw `pool_create` sites remain — the `py/js/bash/rb` AST scope pools + 2 transpiler `temp_ctx` pools — which back arenas and belong to Phase 3.**
>
> Batch 5 added the coupled script/transpiler group — `lambda/runner.cpp` (`script.pool` at the `Input::create` origin + result pools) and `lambda/transpile-mir.cpp` (script result pools) → `MEM_ROLE_AST`. Their raw `pool_destroy(script->pool)` / `pool_destroy(output_input->pool)` calls in runner.cpp/main.cpp are now safe via the release hook (no need to convert them). Earlier batches (1–4): Batch 4 added `lambda/validator/ast_validate.cpp` (`run_ast_validation` pool, 6 exit-path destroys → `MEM_ROLE_VALIDATOR`) and `lambda/js/js_cssom.cpp` (conditional pool — destroys guarded by `if (free_pool)` so only the factory-created pool is freed → `MEM_ROLE_CSS`). Deferred this batch: `input.cpp` sys:// pools (owned by the returned `Input` on success — cross-function) and `js_mir_entrypoints_require.cpp` (`temp_ctx->pool` struct field freed elsewhere). Batch 3 added the multi-pool functions `lambda/format/format-html.cpp`, `lambda/npm/npm_registry.cpp`, `lambda/npm/npm_lockfile.cpp` (per-variable, every exit path), and `lambda/main.cpp` (`convert` temp pool + 4 REPL `fmt_pool`s). In `main.cpp`, two raw destroys are intentionally left: `pool_destroy(output_input->pool)` and `pool_destroy(script->pool)` reference the *script's own* pool (created in the transpiler/runner path, not yet factory-converted) — their creation sites in `runner.cpp`/`transpile-mir.cpp` must be converted together with these destroys in a later batch.
> - **Lambda:** `lambda-proc.cpp` (`pn_output` temp pool), `input/input.cpp` (`InputManager::global_pool`), `format-yaml/latex/markup/text/json/xml.cpp` (formatter temp pools → `MEM_ROLE_TEMP`), `input/css/dom_node.cpp` (CSS dump → `MEM_ROLE_CSS`), `npm/npm_package_json.cpp` (→ `MEM_ROLE_INPUT`).
> - **Radiant:** `render_svg.cpp`, `render_pdf.cpp` (page-backdrop pools), `render_svg_inline.cpp`, `render_effect_raster_fallback.hpp` (→ `MEM_ROLE_RENDER`), `event.cpp` (temp pool → `MEM_ROLE_TEMP`).
> - **Technique:** for `pool_destroy(VAR)`, use a leading-whitespace-free `old_string` with `replace_all` to catch every indentation in one pass; then a post-edit `grep` sweep confirms no raw create/destroy of the converted variable remains. Convert per-variable in multi-pool functions; never touch externally-owned pool params.
> - **Both baselines green after each batch:** Lambda 2941/2942 (pre-existing `dom_jquery_lib`); Radiant exit 0, 5662–5668/required (`test_wpt_css_syntax_gtest` has 4 pre-existing non-required WPT edge-case fails, unrelated).
> - **Deferred (cross-function pool ownership):** `radiant/rdt_vector_tvg.cpp` — the local `pool` is handed to `Input::create` and cached in `RdtPicture.pool`, freed later via `pool_destroy(pic->pool)` in a different function; converting safely needs that site too. Remaining: multi-pool funcs (format-html, npm_registry/lockfile, runner.cpp, transpile-mir.cpp, main.cpp, cmd_layout.cpp), other radiant sites, and the scope/AST pools (Phase 3).

---

## 12. Risks & Open Questions

| # | Risk | Mitigation |
|---|------|------------|
| R1 | Migration churn — ~40 sites must change creation calls | Mechanical; staged by phase; `MEM_CONTEXT_DISABLED` keeps behavior identical; grep gate prevents regressions. |
| R2 | A `MemContext*` must now be threaded to every creation site | Most sites already have an `EvalContext` / `LayoutContext` / `Input` in scope; attach the sub-context there. Truly context-less sites fall back to `mem_context_root()`. |
| R3 | Lock contention if many short-lived pools are created in a loop | Per-thread / per-document sub-contexts ([§7.3](#73-scoped-contexts)); create/destroy is O(1). Measure against the temp-pool sites. |
| R4 | `mem_node` field bloats every allocator | One pointer (8 B) + slab node; compiled out under `MEM_CONTEXT_DISABLED`. |
| R5 | rpmalloc `alloc_count` is an upper bound (frees not subtracted) | Flagged in snapshot; `bytes_in_use` marked best-effort for rpmalloc pools. |
| R6 | A site destroys a backing pool while children live | `child_count` invariant refuses + `log_error` ([§7.1](#71-the-child_count-invariant)); cascade teardown does it correctly. Turns [§I2](./Memory_Pooling.md) into an enforced rule. |
| Q1 | Root context lazy vs explicit init | Lean lazy (mirrors `ensure_rpmalloc_initialized`), explicit teardown in `main()` exit alongside `mempool_cleanup()`. |
| Q2 | Keep raw primitives publicly callable for tests, or fully seal them? | Keep `*_raw` in a private/test-only header so GTest fixtures can build allocators without a context; application code uses the factory only. |
| Q3 | mmap-backed reuse pool (`script_runner.cpp`) and FreeType realloc pool — still factory-created? | Yes — they are created via `mem_pool_create_mmap` / `mem_pool_create`; the factory just records them. No behavior change. |

---

## 13. Best Practices From Other Runtimes

- **V8 / Chrome — Zones + Heap Snapshots**. V8 funnels compiler allocations through `Zone`s (≈ Arena) and ships flat node/edge snapshots to DevTools. *Borrow*: the flat POD snapshot ([§5](#5-core-data-structures)) + devtools-style inspector ([§10.2](#102-in-memory-interactive-inspector-browser-devtools-style)); named categories (`MemRole`).

- **JVM — Native Memory Tracking (NMT)**. Tags every native allocation with a category and prints reserved-vs-committed per category via `jcmd`. *Borrow*: reserved-vs-in-use distinction; `--mem-dump` ≈ `jcmd VM.native_memory`.

- **Go runtime — `MemStats` + pprof**. Cheap aggregate counters always; detailed sampled profiles on demand. *Borrow*: shallow-by-default / deep-on-demand split ([§9.4](#94-deep-vs-shallow-snapshots)).

- **jemalloc — arenas + `mallctl`**. Namespaces allocations into arenas with a programmatic stats tree, and assigns arenas per thread. *Borrow*: the factory's role-labeled allocators + per-thread sub-contexts ([§7.3](#73-scoped-contexts)) directly mirror this.

- **mimalloc / rpmalloc — per-thread heaps**. We already use rpmalloc first-class heaps. *Borrow*: each first-class heap becomes a labeled factory node; reuse `rpmalloc_thread_statistics` to populate `bytes_reserved` accurately.

- **Apache / Subversion — `apr_pool_t` hierarchy**. APR's canonical model: **pools own child pools**, and destroying a parent cascades to children. This is almost exactly the factory's [cascade teardown](#72-cascade-teardown). *Borrow*: the parent/child pool tree and "destroy the root, everything under it goes" discipline — a proven 20-year-old design.

- **PostgreSQL — `MemoryContext` tree**. PG's `MemoryContext` is a named, hierarchical allocator where contexts are reset/deleted as a unit per query/transaction, with `MemoryContextStats` dumping the tree. This proposal is deliberately close in spirit (hence the name): *borrow* the per-query context reset pattern ([§7.3](#73-scoped-contexts)) and the tree-dump diagnostic.

- **Unreal Engine — LLM scoped tags**. `LLM_SCOPE(ELLMTag::UI)` attributes allocations to subsystems via a scope stack. *Borrow*: a future thread-local "current role" scope so even incidental allocations inherit a role.

- **AddressSanitizer / Valgrind Massif**. Time-series of heap usage with site attribution. *Borrow*: snapshot ring buffer + diff ([§10.2](#102-in-memory-interactive-inspector-browser-devtools-style), [§10.3](#103-live-top-style-view)) = poor-man's Massif at allocator granularity.

- **Linux `top` / `/proc`**. Live sampled sorted resource view. *Borrow*: directly — [§10.3](#103-live-top-style-view).

> The two closest precedents — **APR pools** and **PostgreSQL MemoryContexts** — both validate the central thesis of this revision: a hierarchical, owning, named context tree (not a flat registry) is the proven way to make native memory both *manageable* and *introspectable*.

---

## 14. Additional Suggestions

1. **Leak gate in CI**. After `make test`, capture a teardown snapshot and fail if any node outlives its context (excluding process-lifetime singletons). Catches the [§I2](./Memory_Pooling.md) bug class automatically.

2. **`child_count` ordering assertions** ([§7.1](#71-the-child_count-invariant)). Already core to the factory — turns the documented ordering hazard into an enforced invariant rather than a convention.

3. **High-water marks**. Track peak `bytes_reserved` per node; report at teardown. Cheap, invaluable for tuning arena chunk defaults.

4. **Budget / quota hooks**. Soft caps per role ("font glyph arena < 16 MB"); the factory can warn at creation/snapshot when a role's subtree exceeds budget — a lightweight regression guard.

5. **Allocation-rate tracking for `top`**. Diff `alloc_count` between samples to show allocs/sec per node — surfaces churn (e.g. a per-frame arena that isn't being reset).

6. **`log.conf`-aware dumps**. Snapshot dumps use `log_info`/`log_debug` with a `MEMCTX:` prefix per [CLAUDE.md rule #9](../CLAUDE.md) — greppable, respects existing log config.

7. **Deterministic node ids for golden tests**. Monotonic per-process ids mean a fixed workload yields a reproducible allocator graph — snapshots can be golden-file tested like layout baselines.

8. **`make check-no-raw-alloc`**. A grep gate (sibling to `make check-int-cast`) that fails the build if application code calls `pool_create`/`arena_create`/`scratch_init` directly instead of the factory — enforces the "every site routes through" guarantee mechanically.

9. **Future: object-level type histogram**. The deep-snapshot path ([§9.4](#94-deep-vs-shallow-snapshots)) buckets GC heap bytes by `TypeId` — a heap profiler that understands Lambda's own type system.

---

# Stage 2 — Centralized Page Allocation & Memory Pressure

## 15. Stage 2: Centralized Page/Chunk Allocation & Memory Pressure Handling

Stage 1 makes the context the **owner** of every allocator and gives it a complete, role-annotated view of the live allocator graph. Stage 2 uses that view to make the context the **coordinator of memory pressure**: every coarse-grained backing allocation (a page, an arena chunk, an mmap span, a GC heap block) routes through the context, and when the OS denies one, the context reclaims memory cooperatively and retries instead of letting the allocation simply fail.

### 15.1 Motivation & Position in the Stack

Today an OOM is handled locally and pessimistically. After the [§P1](./Memory_Pooling.md) fix, `mmap_pool_grow` on `MAP_FAILED` just nulls the cursor and the caller returns `NULL` — the failure propagates with no attempt to recover, even though the process may be holding tens of MB of **reclaimable** memory: font glyph caches, document caches, decompression scratch, empty trailing arena chunks, dead GC objects awaiting collection.

Other runtimes never give up on the first failure. The JVM runs a full GC (and clears `SoftReference` caches) before throwing `OutOfMemoryError`; V8 fires a `NearHeapLimitCallback`; iOS/Android push memory-warning callbacks so apps drop caches; the Linux kernel runs `kswapd`/direct reclaim before the OOM killer. Stage 2 brings the same **reclaim-then-retry** discipline to Lambda/Radiant, centralized in the one component that already knows what every allocator is for and how big it is.

**Where it sits** — Stage 2 inserts a thin choke point *below* the allocators and *above* the OS:

```
   Pool / Arena / Heap / Nursery               (Stage 1 — factory-owned)
            │  needs a new page/chunk/block
            ▼
   mem_page_alloc(ctx, size, requester_node)    ← Stage 2 choke point
            │
            ├─ try OS path (mmap / rpmalloc span / malloc)
            │     success → return page
            │     failure → reclamation loop (drop caches → compact → GC) → retry
            │                 still failing → thread disposition (park / fail / abort)
            ▼
        OS / mmap / rpmalloc
```

Per-object `pool_alloc`/`arena_alloc` are unaffected — only the rarer *backing* growth calls touch this path.

### 15.2 The Page/Chunk Choke Point

A single entry point that allocators call to obtain backing memory, replacing their direct `mmap`/`rpmalloc_heap_alloc`/`malloc` chunk calls:

```c
// lib/mem_context.h  (Stage 2)

typedef enum MemPageSource {
    MEM_PAGE_MMAP = 1,     // mmap span (mmap pools, large arena chunks)
    MEM_PAGE_RPMALLOC,     // rpmalloc heap span
    MEM_PAGE_MALLOC,       // system malloc (small metadata)
} MemPageSource;

// Obtain a backing page/chunk. On OS failure, runs reclamation and retries.
// requester is the MemNode of the allocator asking (for cost attribution &
// to avoid asking an allocator to compact itself mid-grow).
void* mem_page_alloc(MemContext* ctx, size_t size,
                     MemPageSource src, MemNode* requester);

void  mem_page_free(MemContext* ctx, void* page, size_t size, MemPageSource src);
```

This is the natural successor to the existing `mmap_pool_grow` ([Memory_Pooling.md §P1](./Memory_Pooling.md)) and the arena chunk-allocation call (`arena_create*` chunks come from `pool_alloc` today). Each gets rewritten to call `mem_page_alloc`. Because Stage 1 already routes creation through the factory, every allocator already carries the `MemNode*` needed for the `requester` argument.

### 15.3 Reclaimer Registry & Cost Model

Subsystems register **reclaimers** with the context — callbacks that can free or compact memory on demand and report how much they recovered.

```c
typedef struct MemReclaimResult {
    uint64_t bytes_freed;     // actually returned to the OS / made allocatable
    bool     made_progress;   // freed anything at all
} MemReclaimResult;

// Attempt to reclaim up to `need` bytes. Must be safe to call under pressure:
// must NOT allocate from the context (use the emergency reserve if it must).
typedef MemReclaimResult (*MemReclaimFn)(void* user, size_t need, MemContext* ctx);

typedef struct MemReclaimer {
    MemReclaimFn fn;
    void*        user;
    const char*  label;        // "font.glyph_cache", "doc.cache", "arena.compact"
    MemRole      role;         // what this reclaimer touches
    uint16_t     cost;         // 0..1000 — effort/latency to run (lower = run first)
    uint8_t      harm;         // 0 = recomputable cache, 1 = compaction (no data loss),
                               // 2 = lossy (drops live-ish state) — last resort
    uint64_t     est_reclaimable; // hint: bytes currently sitting in this reclaimer
} MemReclaimer;

MemReclaimerHandle mem_register_reclaimer(MemContext* ctx, const MemReclaimer* r);
void               mem_unregister_reclaimer(MemReclaimerHandle h);
// reclaimers may update their own est_reclaimable as they fill/drain:
void               mem_reclaimer_update_estimate(MemReclaimerHandle h, uint64_t bytes);
```

**Cost-based ordering.** When pressure hits, the context sorts candidate reclaimers by a score that prefers cheap, high-yield, low-harm reclaimers first:

```
score = harm_weight(harm) * W_HARM
      + cost              * W_COST
      - benefit_rank(est_reclaimable) * W_BENEFIT
```

So the default escalation is: **drop recomputable caches** (harm 0, regenerated on demand) → **compact** (harm 1, no data lost, just slower next access) → **lossy drops / forced GC** (harm 2) → **back-pressure**. The cost model is the same family of reasoning as buffer-pool eviction (clock-sweep) and `SoftReference` clearing — cheapest-to-lose memory goes first.

**Example reclaimers** (mapped to real subsystems from [Memory_Pooling.md](./Memory_Pooling.md)):

| Reclaimer | Role | harm | What it does |
|-----------|------|------|--------------|
| `font.glyph_cache` | FONT | 0 | Drop rasterized glyph bitmaps + LRU-evict faces (`font_cache_evict_lru`) — re-rasterized on demand |
| `font.decompress` | FONT | 0 | Free WOFF1/WOFF2 decompression scratch (`font_decompress.cpp`) |
| `image.cache` | MEDIA | 0 | Drop decoded `ImageSurface.pixels` of non-visible images — re-decodable from `source_data`/`source_path` (`surface.cpp` lazy decode) |
| `vector.cache` | MEDIA | 0 | Evict ThorVG picture/paint caches (`g_picture_*`/`g_paint_cache`) |
| `doc.cache` | INPUT | 0 | Evict `enhanced_file_cache` / network resource cache |
| `scratch.reset` | LAYOUT/RENDER | 1 | `scratch_release` idle scratch arenas not in an active pass |
| `arena.compact` | * | 1 | Run arena free-list coalescing + release empty trailing chunks to the pool/OS |
| `heap.gc` | RUNTIME_HEAP | 1 | Trigger `heap_gc_collect()` — reclaim dead GC objects |
| `jit.debuginfo` | CODE | 1 | Drop `Script.debug_info` stack-trace maps (lossy for diagnostics, recomputable on recompile) |
| `nursery.flush` | RUNTIME_HEAP | 2 | Force-promote/flush nursery blocks |

JIT *code pages* themselves are not reclaimable while the script is live (harm too high — would require recompilation), so they are tracked but excluded from the reclaim set; only the auxiliary `debug_info` is droppable.

**Document liveness as a reclamation signal.** Because every allocator carries a `doc_id` ([§10.4](#104-grouping-by-document-doc-id--url)), the cost model gets a powerful, document-engine-specific input: **target the cheapest documents first.** A page that is closed-but-not-yet-freed, hidden in a background tab, or scrolled far offscreen is a far better eviction target than the foreground document the user is interacting with. The context can mark each `doc_id` with a liveness/visibility hint (`active` / `background` / `retiring`) and have reclaimers prefer non-active docs:

- Drop the glyph/decompress/doc caches of **background** documents before touching the active one.
- Compact or `mem_context_destroy` the sub-context of a **retiring** document immediately — a doc that's closing is pure reclaimable memory, and Stage 1 cascade teardown ([§7.2](#72-cascade-teardown)) frees its whole subtree at once.

This makes "evict the least-valuable document's memory first" the natural default — the same per-target cost reasoning used by browser tab-discarding and OS process-level memory pressure, applied at document granularity.

### 15.4 Reclamation Levels (Escalation Ladder)

Reclamation runs in **levels**, stopping as soon as the retry succeeds. Each level is the set of reclaimers at or below a harm threshold, run cheapest-first:

| Level | Includes | Latency | When |
|-------|----------|---------|------|
| **L0 — Caches** | harm 0 reclaimers (font/doc/decompress caches) | µs–ms | First response to any OOM |
| **L1 — Compact** | + harm 1 (arena compaction, idle scratch release, scratch resets) | ms | L0 insufficient |
| **L2 — GC** | + `heap_gc_collect()`, nursery flush | ms–10s ms | L1 insufficient |
| **L3 — Back-pressure** | no memory left to reclaim | — | All reclaimers exhausted → [§15.6](#156-thread-disposition-park--fail--abort) |

The ladder mirrors Android `onTrimMemory` levels and the JVM's "minor GC → full GC → clear soft refs → OOME" progression: escalate only as far as needed, and only pay the expensive, disruptive steps when the cheap ones don't unblock the allocation.

### 15.5 The OOM Handling Loop

```c
void* mem_page_alloc(MemContext* ctx, size_t size, MemPageSource src, MemNode* requester) {
    void* page = os_page_alloc(src, size);     // mmap / rpmalloc span / malloc
    if (page) return page;

    // OOM. Serialize reclamation so threads don't stampede (§15.7).
    if (!reclaim_begin(ctx)) {
        // another thread is already reclaiming — wait for it, then retry once
        reclaim_wait(ctx);
        page = os_page_alloc(src, size);
        if (page) return page;
        reclaim_begin(ctx);   // our turn now
    }

    for (int level = L0_CACHES; level <= L2_GC; level++) {
        uint64_t freed = run_reclaimers_at_level(ctx, level, size, requester);
        log_info("MEMCTX: OOM for %zu bytes (req '%s'), level %d freed %llu bytes",
                 size, requester ? requester->label : "?", level, freed);
        if (freed == 0) continue;              // this level couldn't help, escalate

        page = os_page_alloc(src, size);
        if (page) { reclaim_end(ctx); reclaim_wake_waiters(ctx); return page; }
    }

    reclaim_end(ctx);
    reclaim_wake_waiters(ctx);

    // L3 — nothing left to reclaim: apply this thread's disposition policy.
    return mem_handle_unrecoverable_oom(ctx, size, requester);  // park / fail / abort (§15.6)
}
```

Key properties:
- **Retry after every productive level**, not just at the end — the cheapest reclaim that unblocks us wins, and we never run GC if dropping a glyph cache sufficed.
- **`requester` is excluded** from being asked to compact itself mid-grow (avoids re-entrancy on the very allocator that's allocating).
- All activity is logged with a `MEMCTX:` prefix per [CLAUDE.md rule #9](../CLAUDE.md), so pressure events are greppable and feed the [§10.3](#103-live-top-style-view) `top` view.

### 15.6 Thread Disposition: Park / Fail / Abort

When reclamation is exhausted (L3), the outcome depends on the **nature of the requesting thread**, declared on its (sub-)context:

```c
typedef enum MemPressurePolicy {
    MEM_OOM_FAIL = 0,   // return NULL; caller raises a Lambda OOM error up the stack
    MEM_OOM_PARK,       // block on a condvar until memory frees up, with timeout
    MEM_OOM_ABORT,      // cancel this thread/task cleanly, release its sub-context
} MemPressurePolicy;

void mem_context_set_oom_policy(MemContext* ctx, MemPressurePolicy policy,
                               uint32_t park_timeout_ms /* PARK only */);
```

| Thread / task nature | Policy | Rationale |
|----------------------|--------|-----------|
| **Interactive main / REPL / CLI** | `FAIL` | Never silently hang or die. Return `ItemError`/raise a Lambda OOM error ([CLAUDE.md error handling](../CLAUDE.md)); the user sees a clean diagnostic and the process stays alive. |
| **Layout / render worker** (best-effort, retryable) | `PARK` then `ABORT` | A frame can wait briefly for memory; if it can't get it within the timeout, abort *this pass* (drop the frame / re-queue) rather than wedge the pipeline. Its sub-context teardown ([§7.2](#72-cascade-teardown)) cleanly frees everything it held — which itself relieves pressure. |
| **Batch / convert / validate job** | `FAIL` | Surface OOM as a job error with the partial result, not a hang. |
| **Detached / cancellable background task** | `ABORT` | Killing it is safe and is itself a reclamation action; cascade teardown reclaims its subtree. |

**Park** uses a context-level condition variable: a parked thread is woken when *any* thread calls `mem_page_free` of significant size or completes a reclamation that freed more than the parked request needs. On timeout, `PARK` falls through to `ABORT` (worker) or `FAIL` (if no safe abort point). **Abort** is cooperative — it sets a cancellation flag the worker checks at safe points (between layout passes / render tiles), then runs `mem_context_destroy` on the task's sub-context. We never `kill()` mid-allocation, which would leak or corrupt; "killed" means *cleanly cancelled at a safe point*.

> **Crucially, Stage 1's cascade teardown is what makes ABORT safe.** Killing a task's sub-context frees its entire allocator subtree in dependency order in one call — so aborting a thread is both correct *and* a large reclamation event that can unblock parked peers.

### 15.7 Thread Safety & Reentrancy Under Pressure

OOM is exactly when concurrency bugs bite, so the rules are strict:

- **Single-reclaimer serialization.** A `reclaim_begin/end` gate (mutex + "in-progress" flag) ensures only one thread runs reclaimers at a time. Other OOM threads **wait** on a condvar, then retry the OS allocation once the reclaiming thread finishes — they may find the freed memory without reclaiming themselves. This prevents a thundering-herd of simultaneous GCs/cache drops (the classic JVM "GC storm" under load).
- **Emergency reserve.** The context holds a small pre-allocated **reserve page** (e.g. 64 KB) carved out at init. The reclamation machinery — sorting candidates, the snapshot it may take to decide *what* to drop, log buffers — allocates from this reserve, never from a tracked allocator. This guarantees reclamation can run even at true zero-memory.
- **Reclaimers must not allocate from the context.** Documented contract on `MemReclaimFn`. A reclaimer that needs scratch uses the reserve or stack. Dropping a cache only frees; compaction only moves within already-owned chunks; GC frees. None should need fresh context memory.
- **No lock inversion.** Reclaimers touch their own subsystem locks (font cache lock, GC lock) but never re-enter the page-alloc path. The page-alloc mutex is always acquired *before* any reclaimer's lock; reclaimers never call `mem_page_alloc`.
- **Park/abort never holds the reclaim gate.** Disposition ([§15.6](#156-thread-disposition-park--fail--abort)) happens after `reclaim_end`, so a parked thread doesn't block other threads' reclamation.

### 15.8 Proactive Reclamation (Watermarks)

Reacting only at hard OOM is jittery. Building on the budget hooks ([§14.4](#14-additional-suggestions)) and high-water marks ([§14.3](#14-additional-suggestions)), the context tracks total `bytes_reserved` across the owned graph against soft/hard watermarks:

- **Soft watermark crossed** → run **L0 (cache drop)** opportunistically on an idle tick / between frames, off the critical path. Like Linux `kswapd` reclaiming in the background before direct reclaim is forced.
- **Hard watermark / OS failure** → the synchronous loop of [§15.5](#155-the-oom-handling-loop).

Watermarks are configurable per context (a render context might cap glyph + pixel memory; a batch job might have a higher ceiling). This converts most pressure into cheap background cache eviction and keeps the expensive synchronous path rare.

### 15.9 Integration Points

Concrete edits, all at *backing-growth* sites (never per-object):

| Site | Today | Stage 2 |
|------|-------|---------|
| `mempool.c` `mmap_pool_grow` | `mmap()`; on `MAP_FAILED` null cursor ([§P1](./Memory_Pooling.md)) | call `mem_page_alloc(ctx, chunk, MEM_PAGE_MMAP, pool->mem_node)` |
| `mempool.c` rpmalloc span growth | `rpmalloc_heap_alloc` | wrap span acquisition through `mem_page_alloc(…, MEM_PAGE_RPMALLOC, …)` |
| `arena.c` chunk allocation | chunk from `pool_alloc` | route chunk request via the pool, which routes pages via `mem_page_alloc` (no arena change needed — it inherits) |
| `gc_heap` block growth | direct pool/OS | `mem_page_alloc(…, requester = heap->mem_node)`; also registers the `heap.gc` reclaimer |
| `gc_nursery_create`/grow | block alloc | `mem_page_alloc`; registers `nursery.flush` reclaimer |
| `font_glyph.c` / `font_decompress.cpp` | own arenas | register `font.glyph_cache` / `font.decompress` reclaimers (L0) |
| `enhanced_file_cache.cpp` | URL-keyed LRU cache w/ `current_size_bytes`/`max_size_bytes` | register `doc.cache` reclaimer (L0) — already has LRU + size accounting; reclaimer just lowers the effective `max_size_bytes` and evicts the tail. Snapshot reads `current_size_bytes` directly |
| `network_downloader.cpp` | `mem_realloc(..., MEM_CAT_NETWORK)` body buffers | route through context (role `INPUT`/`NETWORK`, doc-scoped via the requesting page's `doc_id`); in-flight downloads of background docs become reclaim/abort targets |
| `input.cpp` `Input::create` | shared `global_pool` (no per-doc free, [§3.1](#31-input-download--cache-memory-also-centralized)) | create each `Input` under a per-document sub-context; closing a doc reclaims its arena/name_pool/shape_pool via cascade teardown ([§7.2](#72-cascade-teardown)) |
| `mir.c` `jit_init` | `MIR_context_t`, mmap'd code pages, no accounting | register `Script.jit_context` as `MEM_KIND_JIT` node; optionally hook MIR's code allocator through `mem_page_alloc` for exact code-size accounting ([§3.2](#32-jit-code-font--media-caches-also-centralized), [Q7](#1511-risks--open-questions)) |
| `font_context.c` | `glyph_arena` + LRU hashmap caches + per-doc reset (`font_context_reset_document_fonts`) | `glyph_arena` factory-created (role FONT); register caches as `MEM_KIND_CACHE`; system fonts `doc_id=0`, document fonts doc-scoped → register `font.glyph_cache` reclaimer (L0) |
| `surface.cpp` / `rdt_vector_tvg.cpp` | `UiContext.image_cache` (path→`ImageSurface`), ThorVG global caches | register as `MEM_KIND_CACHE`/`MEM_ROLE_MEDIA`; page images doc-scoped, ThorVG caches `doc_id=0` → register `image.cache`/`vector.cache` reclaimers (L0) |
| `arena.c` | free-list coalescing already exists ([§A4/§8.3.3](./Memory_Pooling.md)) | expose as the `arena.compact` reclaimer (L1) |

Most subsystems only need to **register a reclaimer**; the page-routing is concentrated in `mempool.c` and `gc_heap`, which everything else is already backed by.

### 15.10 Best Practices From Other Runtimes (OOM-specific)

- **JVM — GC-before-OOME + SoftReference**. The JVM never throws `OutOfMemoryError` without first running a full GC and clearing all `SoftReference`s (its "recomputable cache" tier). *Borrow*: the L0-caches-then-L2-GC ladder ([§15.4](#154-reclamation-levels-escalation-ladder)); `harm=0` reclaimers are our SoftReferences.
- **V8 — `NearHeapLimitCallback` / `MemoryPressureListener`**. V8 calls back near the heap limit so the embedder can free externally-held memory before V8 gives up. *Borrow*: the proactive soft-watermark callback ([§15.8](#158-proactive-reclamation-watermarks)).
- **iOS `didReceiveMemoryWarning` / Android `onTrimMemory`**. OS pushes graded pressure signals; apps drop caches per level. *Borrow*: the leveled escalation ladder and per-level harm thresholds.
- **Linux `kswapd` + direct reclaim + OOM killer (`oom_score`)**. Background reclaim first, synchronous direct reclaim under pressure, kill lowest-value task last. *Borrow*: background (watermark) vs synchronous (page-alloc loop) split; the per-context `MemPressurePolicy` + cascade-teardown is our cooperative, safe analogue of `oom_score`-based killing — we cancel the most disposable task at a safe point rather than `SIGKILL`.
- **Database buffer pools (PostgreSQL `shared_buffers`, InnoDB) — clock-sweep eviction**. Evict the cheapest-to-replace page under pressure. *Borrow*: the cost/harm score for reclaimer ordering ([§15.3](#153-reclaimer-registry--cost-model)).
- **Redis `maxmemory` + eviction policies**. A hard ceiling plus a policy (LRU/LFU/TTL) for what to drop, and `OOM command not allowed` errors when nothing is evictable. *Borrow*: configurable per-context watermark ceiling + `FAIL` policy returning a clean OOM error rather than crashing.
- **Go — soft memory limit (`GOMEMLIMIT`)**. A soft cap that makes the GC work harder before the process grows further. *Borrow*: soft watermark → trigger GC reclaimer earlier under a configured ceiling.

### 15.11 Risks & Open Questions

| # | Risk | Mitigation |
|---|------|------------|
| S1 | Reclaimer runs under pressure but itself needs memory | Emergency reserve page ([§15.7](#157-thread-safety--reentrancy-under-pressure)); documented no-alloc contract on `MemReclaimFn`. |
| S2 | Dropping a cache mid-use corrupts a live computation | Reclaimers only drop *recomputable* or *idle* state; `harm` levels gate this. Active scratch (in a live layout/render pass) is never in the idle-reclaim set. |
| S3 | Thundering-herd GCs when many threads OOM together | Single-reclaimer serialization + wait/retry ([§15.7](#157-thread-safety--reentrancy-under-pressure)). |
| S4 | Park deadlock — all threads parked, none can free | Park has a timeout that escalates to ABORT/FAIL; aborting any one task frees its subtree and wakes peers. A global "all-threads-parked" detector forces the lowest-value task to ABORT. |
| S5 | Cost/harm scores mis-tuned → thrash (drop cache, immediately refill, OOM again) | Hysteresis: after a reclaim, require the watermark to recover by a margin before re-arming; track reclaim effectiveness per reclaimer and de-prioritize ones that yield little. |
| S6 | Aborting mid-task leaks or corrupts | Abort is cooperative at safe points only; cleanup is Stage 1 `mem_context_destroy` cascade — never a hard kill. |
| Q4 | Does rpmalloc expose a hook to intercept span allocation, or must we wrap at `pool_alloc` granularity? | Investigate `rpmalloc` span APIs; fallback is to route at the pool-chunk level, which already covers arenas and most growth. |
| Q5 | Should watermarks be global or per-role (e.g. cap font memory independently of view memory)? | Start global per-context; add per-role sub-budgets ([§14.4](#14-additional-suggestions)) if a single role dominates pressure. |
| Q6 | Interaction with rpmalloc's own thread caches holding freed-but-unreturned memory | A reclaimer can call `rpmalloc_thread_finalize`/trim on idle threads to return cached spans to the OS ([§P2](./Memory_Pooling.md)). |
| Q7 | Can the linked MIR build expose a custom allocator / code-size hook for exact JIT accounting? | If yes, route MIR code-page allocation through `mem_page_alloc` for exact bytes; if no, record an estimate from generated-function/module count on the `MEM_KIND_JIT` node. Either way JIT code becomes visible in snapshots ([§3.2](#32-jit-code-font--media-caches-also-centralized)). |

---

*End of proposal.*

# Memory Management Survey Report

**Scope**: Lambda + Radiant codebase  
**Date**: 2026-04-12

---

## Table of Contents

1. [Architecture Overview](#1-architecture-overview)
2. [rpmalloc Usage Review](#2-rpmalloc-usage-review)
3. [Pool Usage Inventory](#3-pool-usage-inventory)
4. [Arena Usage Inventory](#4-arena-usage-inventory)
5. [Pool → Arena Conversion Candidates](#5-pool--arena-conversion-candidates)
6. [Raw malloc/calloc Audit](#6-raw-malloccalloc-audit)
7. [Implementation Bugs & Concerns](#7-implementation-bugs--concerns)
8. [Recommendations](#8-recommendations)
9. [Temp Arena Design Proposal](#9-temp-arena-design-proposal)

---

## 1. Architecture Overview

The codebase uses a **three-tier** memory allocation strategy:

| Tier | Implementation | Purpose | Individual Free? |
|------|---------------|---------|-----------------|
| **Pool** | `lib/mempool.c` wrapping rpmalloc first-class heaps | General-purpose allocator with per-object free capability | Yes (rpmalloc mode) / No (mmap mode) |
| **Arena** | `lib/arena.c` built **on top of** Pool | Bump-allocator for sequential, short-lived allocations | Optional free-list recycling |
| **Raw malloc** | System `malloc/calloc/free` | Pool struct metadata, one-off utilities | Yes |

**Layering**: Arena → Pool → rpmalloc → OS.  
Arena chunks are allocated from Pool. Pool delegates to rpmalloc heaps (or mmap fallback).

### Key Design Principle

- **Pool** is appropriate when objects have **varied lifetimes** and need individual `pool_free()`.
- **Arena** is appropriate when objects are **created together and discarded together** (bulk reset/destroy). Arena's O(1) bump allocation is significantly faster than Pool's rpmalloc per-object tracking.

---

## 2. rpmalloc Usage Review

### 2.1 Configuration

- Compiled with `ENABLE_OVERRIDE=0` — does NOT replace system `malloc`
- Compiled with `RPMALLOC_FIRST_CLASS_HEAPS=1` — enables per-pool heap isolation
- Library: `librpmalloc_no_override.a` (static link)

### 2.2 rpmalloc API Functions Used

| Function | Location | Purpose |
|----------|----------|---------|
| `rpmalloc_initialize(NULL)` | `mempool.c:47` | One-time global init |
| `rpmalloc_thread_initialize()` | `mempool.c:59` | Per-thread TLS setup |
| `rpmalloc_heap_acquire()` | `mempool.c:75` | Per-pool heap creation |
| `rpmalloc_heap_alloc()` | `mempool.c:201` | Allocation |
| `rpmalloc_heap_calloc()` | `mempool.c:230` | Zero-init allocation |
| `rpmalloc_heap_free()` | `mempool.c:247` | Individual deallocation |
| `rpmalloc_heap_realloc()` | `mempool.c:265` | Resize |
| `rpmalloc_heap_free_all()` | `mempool.c:148,162,174` | Bulk free (destroy/drain/reset) |
| `rpmalloc_heap_release()` | `mempool.c:149,163` | Return heap to system |

### 2.3 Assessment: Correct Usage ✅

The rpmalloc integration is **well-designed**:

1. **First-class heaps** provide proper per-pool isolation — allocations from one pool cannot accidentally be freed via another.
2. **Lazy initialization** with mutex guards is correct for multi-threaded startup.
3. **Thread-local** `rpmalloc_thread_initialize()` is called via `ensure_rpmalloc_initialized()` before any pool creation.
4. **Bulk free** via `rpmalloc_heap_free_all()` is the primary cleanup path, which is optimal for rpmalloc.
5. `ENABLE_OVERRIDE=0` correctly preserves system malloc for Pool struct metadata (`malloc(sizeof(Pool))`).

### 2.4 Minor Issues

| Issue | Severity | Details |
|-------|----------|---------|
| `mempool_cleanup()` never called | Low | ✅ **FIXED.** Now called in `main()` exit path after `runtime_cleanup()`, before `log_finish()`. |
| Heap pointer validation is weak | Low | `mempool.c:200` checks `(uintptr_t)pool->heap < 0x10000` — catches NULL-ish pointers but misses other corruption patterns. |
| No `rpmalloc_thread_finalize()` on thread exit | Medium | Worker threads that use pools should call `rpmalloc_thread_finalize()` at exit to release thread-local caches. `mempool_cleanup()` calls it at program exit; per-thread cleanup remains a concern for long-running worker threads. |

---

## 3. Pool Usage Inventory

### 3.1 Complete List of Pool Usage Sites

#### Lambda Core

| File | Pool Source | What is Allocated | Individual `pool_free()`? |
|------|-----------|-------------------|--------------------------|
| `lambda/input/input.cpp` | `input->pool` | TypeMap, ShapeEntry, Map `.data` buffers | **Yes** — `pool_free()` on old `.data` buffer during resize (lines 91, 212, 305, 306, 353, 354) |
| `lambda/lambda-data.cpp` | `input->pool` | Type, Array, Map, Element structs | No |
| `lambda/mark_builder.cpp` | `input->pool` via MarkBuilder | Container construction (delegates to `map_put`) | No (arena for structs, pool for metadata) |
| `lambda/name_pool.cpp` | parent `pool` | NamePool struct, interned strings | No |
| `lambda/shape_pool.cpp` | parent `pool` | ShapePool, CachedShape entries | No |
| `lambda/re2_wrapper.cpp` | `pool` param | TypePattern, String, ShapeEntry | No |
| `lambda/emit_sexpr.cpp:1966` | `pool_create()` | Temp Input for S-expression serialization | No — bulk `pool_destroy()` |
| `lambda/lambda-proc.cpp:395` | `pool_create()` | Temp pool for procedure formatting | No — bulk `pool_destroy()` |
| `lambda/validator/doc_validator.cpp:62` | `pool_create()` | Transpiler, NamePool, validation structs | No |
| `lambda/validator/ast_validate.cpp:213` | `pool_create()` | Temp pool for AST validation | No |
| `lambda/rb/rb_scope.cpp:165` | `pool_create()` | `tp->ast_pool` backing `ast_arena` for Ruby AST nodes | No — ✅ **Migrated to arena** |
| `lambda/rb/build_rb_ast.cpp` | `tp->ast_arena` | RbAstNode allocations | No — ✅ **Migrated to arena** |
| `lambda/py/py_scope.cpp:180` | `pool_create()` | `tp->ast_pool` backing `ast_arena` for Python AST nodes | No — ✅ **Migrated to arena** |
| `lambda/py/build_py_ast.cpp` | `tp->ast_arena` | PyAstNode allocations | No — ✅ **Migrated to arena** |
| `lambda/js/transpile_js_mir.cpp` | `pool_create()` | JS transpiler context, Input | No |

#### Radiant

| File | Pool Source | What is Allocated | Individual `pool_free()`? |
|------|-----------|-------------------|--------------------------|
| `radiant/view_pool.cpp` | `tree->pool` | DomElement, DomText, ViewBlock, ViewText, FontProp, layout properties | **Yes** — 10 `pool_free()` calls for selective view component cleanup (lines 143-173) |
| `radiant/cmd_layout.cpp` | `pool_create()` multiple | ViewTree, ViewBlock, ImageSurface, EmbedProp, CssStylesheet arrays | **Yes** — `pool_realloc()` for stylesheet arrays (lines 851, 980, 1050) |
| `radiant/layout_block.cpp` | `view_tree->pool` | Pseudo-elements (DomElement, DomText), text content | No |
| `radiant/layout_table.cpp` | `view_tree->pool` | Anonymous table cells (DomElement, FontProp) | No |
| `radiant/block_context.cpp:252` | `ctx->pool` | FloatBox | No |
| `radiant/pdf/pdf_to_view.cpp` | `pool_create()` | ViewTree, ViewBlock, ViewText, EmbedProp, DomElement | No — bulk `pool_destroy()` |
| `radiant/pdf/operators.cpp` | `state->pool` | PDFStreamParser, String, PathSegment, PDFSavedState, PDFOperator | No |
| `radiant/pdf/fonts.cpp` | `pool` param | Glyph map arrays, FontProp, PDFFontCache, PDFFontEntry, width arrays | No |
| `radiant/pdf/pages.cpp` | `pool_create()` | Temp pools for page processing, PDFPageInfo | No — bulk `pool_destroy()` |
| `radiant/render_dvi.cpp` | `pool_create()` ×4 | Short-lived rendering pools | No — bulk `pool_destroy()` |
| `radiant/window.cpp` | `pool_create()` | Window rendering setup | No |
| `radiant/event.cpp` | `pool_create()` temp | Temporary event processing pools | No — bulk `pool_destroy()` |
| `radiant/script_runner.cpp:354` | `pool_create_mmap()` | **Mmap-backed** reuse pool for JS runtime | No — mmap bump allocator |

#### CSS System

| File | Pool Source | What is Allocated | Individual `pool_free()`? |
|------|-----------|-------------------|--------------------------|
| `lambda/input/css/css_parser.cpp` | engine pool | CssValue, CssFunction, selectors, names | No |
| `lambda/input/css/css_engine.cpp` | pool param | CssEngine, CssStylesheet | No |
| `lambda/input/css/css_value_parser.cpp` | pool param | CSS values and properties | No |
| `radiant/resolve_css_style.cpp` | pool | CssCustomProp | No |

#### Font System

| File | Pool Source | What is Allocated | Individual `pool_free()`? |
|------|-----------|-------------------|--------------------------|
| `lib/font/font_context.c` | pool param | FontContext, FreeType memory callbacks (`pool_realloc`) | Via FreeType callbacks |

---

## 4. Arena Usage Inventory

### 4.1 Complete List of Arena Usage Sites

#### Lambda Core

| File | Arena Source | What is Allocated | Reset/Destroy |
|------|------------|-------------------|---------------|
| `lambda/lambda-data.cpp` | `input->arena` | Array, Map, Element **container structs** (via `*_arena()` functions) | Destroyed with Input |
| `lambda/mark_builder.cpp` | `builder->arena()` | Container structs during parsing | Destroyed with Input |
| `lambda/input/input-latex-ts.cpp` | arena param | LaTeX output buffers | Destroyed with caller |

#### Radiant

| File | Arena Source | What is Allocated | Reset/Destroy |
|------|------------|-------------------|---------------|
| `radiant/state_store.cpp` | `state->arena` | RadiantState snapshots, DragDropState, caret/selection/focus states | `arena_reset()` per frame |
| `radiant/state_store.cpp` | `dirty_tracker.arena` | DirtyRect linked list | `arena_reset()` per frame |
| `radiant/state_store.cpp` | `reflow_scheduler.arena` | ReflowRequest linked list | `arena_reset()` per frame |
| `radiant/layout.cpp:2156` | `doc->arena` | CounterContext for CSS counters | Destroyed with document |
| `radiant/layout_counters.cpp` | `ctx->arena` | CounterScope, counter arrays, name strings | Destroyed with document |
| `radiant/layout_block.cpp:2596` | `doc->arena` | String struct for text content | Destroyed with document |
| `radiant/layout_list.cpp` | `doc->arena` | Marker text copies | Destroyed with document |
| `radiant/resolve_css_style.cpp:3029` | `doc->arena` | Attribute name copies | Destroyed with document |
| `radiant/event.cpp:3193+` | temp `arena_create_default()` | Short-lived text extraction buffers | `arena_destroy()` immediately |
| `radiant/render_dvi.cpp` | `arena_create_default()` | DVI rendering buffers | `arena_destroy()` at function exit |

#### Font System

| File | Arena Source | What is Allocated | Reset/Destroy |
|------|------------|-------------------|---------------|
| `lib/font/font_context.c:109` | `arena_create_default()` | Font names, paths | Destroyed with FontContext |
| `lib/font/font_context.c:133` | `arena_create(256KB, 4MB)` | Glyph bitmaps (large) | `arena_reset()` per frame |
| `lib/font/font_database.c` | `db->arena` | Font metadata strings (`arena_strdup`) | Destroyed with FontDatabase |
| `lib/font/font_config.c` | arena param | System font paths, names, Unicode ranges | Destroyed with database |
| `lib/font/font_rasterize_ct.c` | glyph_arena | GlyphBitmap, raster buffers | Reset per frame |
| `lib/font/font_glyph.c` | `ctx->glyph_arena` | Glyph bitmap copies, raster buffers | Reset per frame |
| `lib/font/font_decompress.cpp` | arena param | WOFF1/WOFF2 decompression buffers | Caller manages |
| `lib/font/font_config.c:2107` | `arena_create(64KB, 1MB)` | Global font arena | Application lifetime |

#### WebDriver

| File | Arena Source | What is Allocated | Reset/Destroy |
|------|------------|-------------------|---------------|
| `radiant/webdriver/webdriver_server.cpp:825` | `arena_create(64KB, 256KB)` | WebDriverServer struct | Server lifetime |
| `radiant/webdriver/webdriver_session.cpp:137` | `arena_create(64KB, 256KB)` | Session, ElementRegistry, UI context | Session lifetime |
| `radiant/webdriver/webdriver_locator.cpp` | session arena | Text results, locator context | Session lifetime |

#### PDF

| File | Arena Source | What is Allocated | Reset/Destroy |
|------|------------|-------------------|---------------|
| `radiant/pdf/pages.cpp:369` | `arena_create_default()` | Page collection temp data | `arena_destroy()` at function exit |
| `lib/pdf_writer.c:233` | `arena_create_default()` | PDFDocument, page/object metadata | Destroyed with document |

---

## 5. Pool → Arena Conversion Candidates

These sites use Pool with **zero individual `pool_free()` calls** — they only rely on bulk `pool_destroy()`. Switching to Arena would give them faster O(1) bump allocation instead of rpmalloc per-object tracking overhead.

### 5.1 Strong Candidates (Recommended)

| File | Current Pattern | Rationale | Status |
|------|----------------|-----------|--------|
| **`lambda/rb/build_rb_ast.cpp`** + **`rb_scope.cpp`** | `tp->ast_pool = pool_create()` → many `pool_alloc()` → `pool_destroy()` | AST nodes are never individually freed. Pure bump-allocate pattern. | ✅ **Migrated** — `ast_arena` added alongside `ast_pool` |
| **`lambda/py/build_py_ast.cpp`** + **`py_scope.cpp`** | `tp->ast_pool = pool_create()` → many `pool_alloc()` → `pool_destroy()` | Identical pattern to Ruby. | ✅ **Migrated** — same arena pattern |
| **`radiant/pdf/pdf_to_view.cpp`** | `Pool* view_pool = pool_create()` → ~25 `pool_calloc()` calls → `pool_destroy()` | View tree construction never individually frees. | ✅ **Migrated** — All 14 static functions changed from `Pool*` to `Arena*`. 23 `pool_calloc` → `arena_calloc`. 3 external API calls use `arena_pool()` accessor. |
| **`radiant/render_dvi.cpp`** | 4 separate `pool_create()` → allocations → `pool_destroy()` per function | Short-lived rendering pools with no individual frees. | ⏭️ **Already optimal** — uses Arena from Pool internally |
| **`radiant/event.cpp`** (temp pools) | `Pool* tp = pool_create()` → temp processing → `pool_destroy()` | Very short-lived event processing. | ⏭️ **Already optimal** — uses Arena from Pool internally |
| **`lambda/lambda-proc.cpp:395`** | `Pool* temp_pool = pool_create()` → few allocations → `pool_destroy()` | Very short-lived temp pool. | ⏭️ **Blocked** — Pool passed to `format_*()` APIs that require Pool |
| **`lambda/emit_sexpr.cpp:1966`** | `pool_create()` → create Input → `pool_destroy()` | Short-lived serialization context. | ⏭️ **Blocked** — Pool passed to `Input::create()` |
| **`lambda/validator/ast_validate.cpp:213`** | `Pool* pool = pool_create()` → allocations → `pool_destroy()` on many exit paths | Short-lived validation context. | ⏭️ **Skipped** — only 1 pool_calloc, not worth the change |
| **`radiant/pdf/pages.cpp`** (temp pools) | `Pool* temp_pool = pool_create()` → page processing → `pool_destroy()` | Short-lived page collection. | ⏭️ **Deferred** — Low impact, few allocations |

### 5.2 Not Candidates (Must Remain Pool)

| File | Reason |
|------|--------|
| `lambda/input/input.cpp` | `pool_free()` called on old `.data` buffers during map resize. Needs individual free. |
| `radiant/view_pool.cpp` | 10 individual `pool_free()` calls for selective view component cleanup. |
| `radiant/cmd_layout.cpp` | Uses `pool_realloc()` for stylesheet arrays. |
| `lambda/input/css/*` | Long-lived CSS engine structures, pool lifetime matches engine lifetime. Could be arena but no clear win since they persist for entire document. |
| `radiant/script_runner.cpp` | Already uses `pool_create_mmap()` — effectively a bump allocator like arena. |
| `lib/font/font_context.c` | FreeType requires `realloc` callback, needs `pool_realloc()`. |

### 5.3 Borderline Cases

| File | Assessment |
|------|-----------|
| `radiant/pdf/operators.cpp` | No individual frees, but allocations come from an externally-owned pool. Converting would require the caller to pass an arena instead. |
| `radiant/pdf/fonts.cpp` | Same — allocations from external pool. Would require API change. |
| `radiant/layout_block.cpp` / `layout_table.cpp` | Allocate from `view_tree->pool` which **does** use individual frees elsewhere (view_pool.cpp). Cannot convert these independently. |
| `radiant/block_context.cpp` | FloatBox allocated from `ctx->pool` — shared pool, can't convert in isolation. |

---

## 6. Raw malloc/calloc Audit

### 6.1 Properly Freed ✅

| File : Line                                  | What                        | Freed Where                                                                      |
| -------------------------------------------- | --------------------------- | -------------------------------------------------------------------------------- |
| `lambda/validator/suggestions.cpp:43`        | Levenshtein distance matrix | `free()` at line 68-70                                                           |
| `lambda/network/enhanced_file_cache.cpp:59`  | SHA256 hex buffer           | Caller `free()` at line 253                                                      |
| `lambda/network/enhanced_file_cache.cpp:242` | Path buffer                 | `free()` at line 253                                                             |
| `lambda/network/resource_loaders.cpp:38`     | Content buffer              | Caller responsible (API contract)                                                |
| `radiant/render.cpp:174-176`                 | ClipMask + saved buffer     | ~~`free_clip_mask()` at lines 2197, 2283~~ ✅ **Migrated to ScratchArena** (§9.8) |
| `radiant/cmd_layout.cpp:2910`                | MathInfo                    | Loop `free()` at line 2997-2999                                                  |
| `lambda/validator/ast_validate.cpp:348,351`  | URL/format String           | `free()` at lines 364-365                                                        |

### 6.2 Potential Issues ⚠️ — ✅ Fixed

| File                                     | Line                                                 | Issue                                                                                                        | Resolution                                                                                                                                                        |
| ---------------------------------------- | ---------------------------------------------------- | ------------------------------------------------------------------------------------------------------------ | ----------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `lambda/validator/doc_validator.cpp:410` | `malloc(sizeof(String) + len + 1)` for error message | Only when `pool == NULL`. Error merging path at line 501 used `nullptr` pool — copied error messages leaked. | ✅ **FIXED.** Added `Pool* pool` parameter to `merge_validation_results()`. All call sites now pass the context pool. No more `malloc` fallback in the merge path. |

### 6.3 Should Use Pool or Arena Instead

| File | Line | What | Recommendation |
|------|------|------|----------------|
| `radiant/render.cpp:174-176` | ClipMask + saved buffer via raw `malloc` | ✅ **Migrated to ScratchArena** — `save_clip_region()` and `parse_css_clip_shape()` now use `scratch_alloc`/`scratch_calloc` from `rdcon->scratch`. `free_clip_mask()`/`free_clip_shape()` use `scratch_free()`. | ✅ Done (§9.8) |
| `radiant/cmd_layout.cpp:2910` | MathInfo structs in a loop | Short-lived, freed in batch. Arena would be ideal. | Use arena — allocate all MathInfo from temp arena, destroy arena at end. |

---

## 7. Implementation Bugs & Concerns

### 7.1 Pool (mempool.c)

| #      | Severity | Location                             | Issue                                                                                                                                                                                              |
| ------ | -------- | ------------------------------------ | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **P1** | **High** | `mempool.c:100-101` (mmap_pool_grow) | ✅ **FIXED.** mmap failure now nulls `cursor`/`limit`; alloc/calloc check for NULL after `mmap_pool_grow()`.                                                                                        |
| **P2** | Medium   | `mempool.c:281`                      | ✅ **FIXED.** `mempool_cleanup()` is now called in `main()` exit path (after `runtime_cleanup()`, before `log_finish()`). Calls `rpmalloc_thread_finalize()` + `rpmalloc_finalize()` for clean shutdown. |
| **P3** | Medium   | `mempool.c:270-279` (mmap realloc)   | ✅ **FIXED.** mmap bump allocations now embed a 16-byte size header (`MMAP_SIZE_HEADER`). `pool_realloc` reads old size from the header and safely `memcpy`s data to the new allocation. |
| **P4** | Low      | `mempool.c:200`                      | Heap pointer validation `< 0x10000` is platform-specific and incomplete.                                                                                                                           |

### 7.2 Arena (arena.c)

| # | Severity | Location | Issue |
|---|----------|----------|-------|
| **A1** | Medium | `arena.c:arena_free` | ✅ **FIXED.** `arena_free()` now promotes blocks < `ARENA_MIN_FREE_BLOCK_SIZE` to the minimum size, and `arena_alloc_aligned()` enforces a minimum allocation size. All blocks can participate in the free-list. |
| **A2** | Medium | `arena.c:arena_free` | ✅ **FIXED.** `arena_free()` now calls `arena_owns()` to validate pointer ownership before adding to free-list. Foreign pointers are rejected with `log_error()`. |
| **A3** | Low | `arena.c:_arena_alloc_from_freelist` | ✅ **FIXED.** Free-list search now checks both size and alignment before selecting a block, avoiding the remove-then-re-add pattern that lost size information. |
| **A4** | Low | `arena.c:arena_free` | ✅ **FIXED.** Bump-back coalescing: when freed block is at the end of the current chunk, the bump pointer is decremented to reclaim space directly (O(1)). Also fixed `arena_reset()`/`arena_clear()` to clear stale free-list pointers. |

### 7.3 Interaction Concerns

| # | Issue |
|---|-------|
| **I1** | Arena is **not thread-safe** — no synchronization. This is fine because each thread/context owns its own arena. ✅ **Now documented** in `arena.h` header with `@warning ARENA_NOT_THREAD_SAFE`. |
| **I2** | `pool_drain()` invalidates the Pool (`valid = 0`) but Arena chunks allocated from that Pool are now backed by freed memory. Any Arena still referencing this Pool will corrupt memory on next chunk allocation. Ensure Arena is destroyed **before** Pool is drained. |

---

## 8. Recommendations

### 8.1 High Priority — ✅ All Fixed

#### Fix P1: mmap failure causes out-of-bounds write — ✅ DONE

```c
// mempool.c, mmap_pool_grow():
if (mem == MAP_FAILED) {
    log_error("mmap_pool_grow: mmap failed for %zu bytes", chunk_size);
+   pool->cursor = NULL;
+   pool->limit = NULL;
    return;
}
```

And in `pool_alloc()` / `pool_calloc()` mmap path:
```c
if (pool->cursor + size > pool->limit) {
    mmap_pool_grow(pool, size);
+   if (!pool->cursor) return NULL;  // mmap failed
}
```

#### Fix P3: mmap realloc reads past old allocation — ✅ DONE

Originally removed the unsafe `memcpy` as an interim fix. Now fully resolved: mmap bump allocations embed a 16-byte size header (`MMAP_SIZE_HEADER`) before each allocation. `pool_realloc()` reads the old size from the header and safely copies data to the new buffer.

#### Fix A2: arena_free without ownership check — ✅ DONE

`arena_free()` now calls `arena_owns()` before adding to free-list:

```c
void arena_free(Arena* arena, void* ptr) {
    if (!ptr || !arena) return;
+   if (!arena_owns(arena, ptr)) {
+       log_error("arena_free: ptr %p not owned by arena %p", ptr, arena);
+       return;
+   }
    // ... existing free-list logic
}
```

### 8.2 Medium Priority — Partially Done

#### Convert AST pools to arenas (strongest candidates) — ✅ DONE

Ruby and Python AST builders now use `ast_arena` (bump allocator) instead of `ast_pool` directly.

```c
// Before:
tp->ast_pool = pool_create();
// ... pool_alloc(tp->ast_pool, ...) throughout AST building
pool_destroy(tp->ast_pool);

// After:
tp->ast_pool = pool_create();  // keep pool for arena's backing store
tp->ast_arena = arena_create_default(tp->ast_pool);
// ... arena_alloc(tp->ast_arena, ...) throughout AST building
arena_destroy(tp->ast_arena);
pool_destroy(tp->ast_pool);
```

This gives O(1) bump allocation for all AST nodes instead of rpmalloc per-object tracking.

#### Convert short-lived temp pools to arenas — ✅ DONE (3/9 migrated, 6 assessed)

**Migrated to arena:**
- `lambda/rb/build_rb_ast.cpp` + `rb_scope.cpp` — AST arena
- `lambda/py/build_py_ast.cpp` + `py_scope.cpp` — AST arena
- `radiant/pdf/pdf_to_view.cpp` — ViewTree arena (23 pool_calloc → arena_calloc, `arena_pool()` accessor for 3 external API calls)

**Already optimal (no change needed):**
- `radiant/render_dvi.cpp` — already uses Arena from Pool internally
- `radiant/event.cpp` — already uses Arena from Pool internally

**Not converted (assessed):**
- `lambda/lambda-proc.cpp` — Pool passed to `format_*()` APIs
- `lambda/emit_sexpr.cpp` — Pool passed to `Input::create()`
- `lambda/validator/ast_validate.cpp` — only 1 pool_calloc, not worth it
- `radiant/pdf/pages.cpp` — low impact, deferred

#### Call `rpmalloc_thread_finalize()` on worker thread exit

Add thread-exit hooks or `atexit`-style cleanup in thread pool teardown to call `rpmalloc_thread_finalize()`, releasing per-thread caches.

### 8.3 Low Priority / Future Improvements — Items 1-8 ✅ Done

| #   | Suggestion                                     | Rationale                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                             |
| --- | ---------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 1   | **Call `mempool_cleanup()` at program exit**   | ✅ **DONE.** Added `mempool_cleanup()` call in `main.cpp` exit path, after `runtime_cleanup()` and before `log_finish()`. Ensures clean Valgrind/ASan shutdown.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                        |
| 2   | **Add `ARENA_NOT_THREAD_SAFE` documentation**  | ✅ **DONE.** Added `@warning ARENA_NOT_THREAD_SAFE` block to `arena.h` header docstring. Documents single-thread ownership requirement and external synchronization needed for concurrent access.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                      |
| 3   | **Arena free-list coalescing**                 | ✅ **DONE.** Full adjacent-block coalescing implemented in `arena_free()`. On free, `_arena_find_adjacent_block()` scans all bins for blocks physically adjacent to the freed region, removes and merges them iteratively. After coalescing, checks if merged block reaches bump cursor for bump-back reclamation. Helper `_arena_remove_free_block()` handles bin removal.                                                                                                                                                                                                                                                                                                                                                                             |
| 4   | **Pool allocation size tracking in mmap mode** | ✅ **DONE.** 16-byte `MMAP_SIZE_HEADER` embedded before each mmap bump allocation in `pool_alloc()` and `pool_calloc()`. `pool_realloc()` mmap path now reads old size from header and safely copies data via `memcpy`. Fully fixes P3.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                 |
| 5   | **`arena_calloc` for view tree allocations**   | ✅ **DONE.** `ViewTree` now has a dedicated `Arena* arena` field. PDF view tree construction (`pdf_to_view.cpp`) uses `arena_calloc()` for all 23 allocation sites (ViewBlock, ViewText, TextRect, BoundaryProp, etc.). Layout code in `layout_block.cpp`/`layout_table.cpp` can optionally use `view_tree->arena` for permanent allocations in the future.                                                                                                                                                                                                                                                                                                                                                                                            |
| 6   | **Unified temp-arena pattern**                 | ✅ **Superseded by ScratchArena** (§9.8). `ScratchArena` provides a lightweight LIFO scratch allocator backed by Arena, with per-allocation free and mark/restore. Integrated into `LayoutContext` and `RenderContext`.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                |
| 7   | **`doc_validator.cpp:410` potential leak**     | ✅ **FIXED.** `merge_validation_results()` now takes a `Pool*` parameter; all call sites pass the context pool. The `malloc` fallback path in `create_validation_error` is no longer reached during error merging.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                     |
| 8   | **`render.cpp` ClipMask raw malloc**           | ✅ **Migrated to ScratchArena** — `save_clip_region()`, `parse_css_clip_shape()`, `mix_blend_backdrop` now use `scratch_alloc`/`scratch_free` from `rdcon->scratch`.                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                   |
| 9   | **Evaluate removing rpmalloc dependency**      | After converting the 9 strong arena candidates (§5.1), the remaining pool sites that need individual `pool_free()` are few (~3 files: `input.cpp`, `view_pool.cpp`, `cmd_layout.cpp`). These could be served by plain system `malloc` with a simple allocation-tracking list in the Pool struct. This would eliminate the rpmalloc build dependency, simplify the build (no custom static lib), remove TLS lifecycle issues (P2), and improve compatibility with sanitizers (ASan, Valgrind, Instruments). System malloc on macOS (libmalloc magazine zones) and Linux (glibc/jemalloc) is already performant for these patterns. Trade-off: lose `rpmalloc_heap_free_all()` convenience — replaced by iterating a tracking list on `pool_destroy()`. |

---

## Appendix: Pool vs Arena Decision Matrix

Use this when deciding which allocator to use for new code:

| Question | → Pool | → Arena |
|----------|--------|---------|
| Need to free individual objects? | ✅ | ❌ |
| Need `realloc` to grow buffers? | ✅ | ⚠️ (arena_realloc exists but limited) |
| All objects freed together at end? | ⚠️ (works but wasteful) | ✅ |
| High allocation rate, small objects? | ❌ (overhead per alloc) | ✅ (O(1) bump) |
| Objects need to survive parent scope? | ✅ | ❌ |
| Frame-based reset pattern? | ❌ | ✅ (`arena_reset()`) |
| FreeType / external library callbacks? | ✅ (needs malloc-like API) | ❌ |

---

## 9. Temp Arena Design Proposal

### 9.1 Problem Statement

The Radiant layout, render, and event subsystems contain **25+ scoped malloc/free pairs** — temporary buffers allocated at function entry (or mid-function) and freed before return. These use `mem_alloc`/`mem_calloc`/`calloc`/`malloc` paired with `mem_free`/`free`, going through the system allocator for every allocation.

Overhead of system malloc for these:
- Lock contention on the global heap (layout and render are hot paths)
- Per-object bookkeeping (malloc metadata headers)
- Cache pollution from scattered heap addresses
- No bulk-free — each `free()` is a separate syscall-level operation

### 9.2 Concrete Allocation Sites

**High-value targets (large buffers, hot paths):**

| # | File | Function | What | Size | Status |
|---|------|----------|------|------|--------|
| 1 | `layout_table.cpp` | `TableMetadata` ctor/dtor | 12 parallel arrays (grid_occupied, col_widths, row_heights, etc.) | `rows × cols` up to `cols+1` per array | ✅ Migrated |
| 2 | `render_background.cpp` | `box_blur_region()` | Pixel scratch buffer | `rw × rh × 4` bytes | ✅ Migrated |
| 3 | `render.cpp` | `render_block_view()` | `mix_blend_backdrop` pixel buffer | `mbw × mbh × 4` bytes | ✅ Migrated |
| 4 | `render_filter.cpp` | `apply_drop_shadow_filter()` | Shadow pixel buffer | `ew × eh × 4` bytes | ✅ Migrated |
| 5 | `render.cpp` | `save_clip_region()` | ClipMask + saved pixel buffer | `w × h × 4` bytes | ✅ Migrated |
| 6 | `render.cpp` | `parse_css_clip_shape()` | ClipShape + polygon vx/vy arrays | Variable | ✅ Migrated |

**Medium-value targets (smaller buffers):**

| # | File | Function | What | Status |
|---|------|----------|------|--------|
| 7 | `layout_table.cpp` | `perform_table_layout()` | `explicit_col_widths`, `col_x_positions` | ✅ Migrated |
| 8 | `grid_positioning.cpp` | `position_grid_items()` | `row_positions`, `column_positions` | ✅ Migrated |
| 9 | `grid_utils.cpp` | `parse_grid_template_areas()` | 3-level nested `grid_cells` + `unique_names` | ✅ Migrated (mark/restore) |
| 10 | `graph_dagre.cpp` | 3 functions | 5 separate `calloc`/`free` pairs (int/float arrays) | ⏭️ Deferred |
| 11 | `grid_positioning.cpp` | baseline alignment | `row_max_baseline`, `row_max_below` | ✅ Migrated |
| 12 | `intrinsic_sizing.cpp` | grid intrinsic widths | `col_min`, `col_max` | ✅ Migrated |
| 13 | `render_svg.cpp` / `render_pdf.cpp` | text rendering | `text_content` buffer | ⏭️ Deferred |
| 14 | `layout_counters.cpp` | `collect_counter_values_all()` | `temp` int array | ⏭️ Deferred |
| 15 | `resolve_css_style.cpp` | grid-template-areas | `combined` string buffer | ✅ Migrated |

### 9.3 Implemented Design: LIFO Scratch Allocator

A lightweight bump allocator with **per-allocation headers** forming a backward-linked list. Optimized for LIFO free (which is the dominant pattern), with graceful handling of non-LIFO frees.

```c
// ---- Data Structures ----

typedef struct ScratchHeader {
    struct ScratchHeader* prev;  // previous allocation (backward link)
    uint32_t size;               // allocation size (excluding header)
    uint32_t flags;              // bit 0: freed flag
} ScratchHeader;                 // 16 bytes

typedef struct ScratchArena {
    Arena* arena;                // backing arena for chunk allocation
    ScratchHeader* head;         // most recent allocation (top of stack)
} ScratchArena;

// ---- API ----

ScratchArena* scratch_create(Arena* backing_arena);
void*         scratch_alloc(ScratchArena* sa, size_t size);
void          scratch_free(ScratchArena* sa, void* ptr);
void          scratch_reset(ScratchArena* sa);  // free everything
void          scratch_destroy(ScratchArena* sa);
```

**Alloc:**
```
1. Bump-allocate (sizeof(ScratchHeader) + aligned_size) from backing arena
2. Fill header: prev = sa->head, size = user_size, flags = 0
3. sa->head = new_header
4. Return pointer past header (header + 1)
```

**Free (LIFO fast path):**
```
1. Get header from (ptr - sizeof(ScratchHeader))
2. If header == sa->head:          // LIFO case ← the common path
     a. sa->head = header->prev
     b. arena bump-back (arena_free on the header address)
     c. Walk backward: while sa->head && sa->head->flags & FREED:
          // coalesce consecutive freed holes
          arena_free(sa->arena, sa->head)
          sa->head = sa->head->prev
3. Else:                            // non-LIFO (rare)
     a. header->flags |= FREED      // mark as hole, reclaim lazily
```

**Hole coalescing on LIFO free (step 2c) — critical detail:**

When non-LIFO frees create holes in the middle, those holes must be reclaimed together when the tail block is freed:

```
Initial state (4 allocations):
  [A] ← [B] ← [C] ← [D]          sa->head = D
                                    ← = prev pointer

Step 1: scratch_free(B)             non-LIFO → mark as hole
  [A] ← [B̶] ← [C] ← [D]          sa->head = D (unchanged)

Step 2: scratch_free(C)             non-LIFO → mark as hole
  [A] ← [B̶] ← [C̶] ← [D]          sa->head = D (unchanged)

Step 3: scratch_free(D)             LIFO ✓ (D == sa->head)
  3a: sa->head = D->prev = C̶       arena bump-back reclaims D
  3c: C̶ is freed → reclaim         sa->head = C̶->prev = B̶
      B̶ is freed → reclaim         sa->head = B̶->prev = A
      A is NOT freed → stop

Result:
  [A]                               sa->head = A
                                    arena bump pointer rewound past D, C, B
```

All consecutive holes behind the freed tail block are reclaimed in one backward walk. The arena bump pointer rewinds past the entire freed region, making that memory available for future allocations. Without this walk, holes B and C would be permanently leaked until `scratch_reset()`.

### 9.4 Design Assessment

**Strengths:**

| Property | Benefit |
|----------|--------|
| O(1) alloc | Bump allocation, no malloc metadata, no lock contention |
| O(1) LIFO free | Pointer comparison + bump-back — zero overhead |
| Backward coalescing | Non-LIFO frees are reclaimed lazily when the stack unwinds |
| Cache-friendly | Sequential memory layout, allocations adjacent in cache lines |
| Bulk reset | `scratch_reset()` resets arena in O(1) — safety net if caller forgets individual frees |
| No fragmentation (LIFO) | Perfect compaction when frees are strictly LIFO |

**Concerns and mitigations:**

| Concern | Assessment |
|---------|------------|
| **16 bytes overhead per allocation** | For the target sites, allocations are 64 bytes to 4MB pixel buffers. 16 bytes is negligible (<0.1% for pixel buffers, ~25% for a small 64-byte alloc). For very small allocations (<32 bytes), prefer the existing arena without headers. |
| **Non-LIFO free creates holes** | In practice this happens rarely. When it does, the hole is reclaimed on the next LIFO free via backward walk. Worst case: all holes reclaimed on `scratch_reset()`. |
| **Thread safety** | Not needed — each layout/render pass owns its own scratch allocator. Same as existing arena design. |
| **Large pixel buffers** | Pixel buffers (render, blur, shadows) can be 10MB+. These should come from the same backing arena chunk system. Arena already handles large allocations via dedicated chunks. |

### 9.5 Mark/Restore (Implemented as Secondary API)

Mark/restore is implemented alongside per-allocation free:

```c
ScratchMark scratch_mark(ScratchArena* sa);            // save current head position
void        scratch_restore(ScratchArena* sa, ScratchMark mark);  // unwind to saved position
```

This is used for **pure scoped** patterns (allocate N things → free all at scope exit). In practice, `parse_grid_template_areas()` uses mark/restore for its 3-level nested allocation (16×16×32 = 8192+ individual allocs) — bulk restore replaces nested `mem_free` loops.

For sites requiring individual mid-scope free (interleaved alloc/free patterns), the per-allocation LIFO free API is used instead:

- `render_block_view()` — interleaved clip mask / blend backdrop alloc/free
- `render_filter.cpp` — per-iteration shadow buffer alloc/free
- `TableMetadata` — 12 arrays freed in LIFO order in destructor

### 9.6 Rollout Plan — ✅ Complete

**Phase 1: Core implementation** — ✅ DONE
- `lib/scratch_arena.h` / `lib/scratch_arena.c` — full API: `scratch_init`, `scratch_alloc`, `scratch_calloc`, `scratch_free`, `scratch_mark`, `scratch_restore`, `scratch_release`, `scratch_live_count`
- `test/test_scratch_arena_gtest.cpp` — 21 unit tests (LIFO, non-LIFO, large allocs, mixed sizes, backward coalescing, mark/restore). All passing.

**Phase 2: Layout integration** — ✅ DONE
- `layout.hpp` — added `ScratchArena scratch` field to `LayoutContext`
- `layout.cpp` — `layout_init()` calls `scratch_init(&lycon->scratch, doc->view_tree->arena)`, `layout_cleanup()` calls `scratch_release(&lycon->scratch)`
- `layout_table.cpp` — `TableMetadata` constructor takes `ScratchArena*`, 12 `mem_calloc` → `scratch_calloc`, destructor frees in LIFO order. `explicit_col_widths` and `col_x_positions` migrated.
- `grid_positioning.cpp` — `position_grid_items()` takes `ScratchArena* sa`. 4 arrays migrated: `row_positions`, `column_positions`, `row_max_baseline`, `row_max_below`. `align_grid_items()` accesses via `grid_layout->lycon->scratch`.
- `grid_utils.cpp` — `parse_grid_template_areas()` takes `ScratchArena* sa`. Uses `scratch_mark`/`scratch_restore` for 3-level nested allocation (grid_cells + unique_names).
- `intrinsic_sizing.cpp` — `col_min`/`col_max` arrays migrated.
- `resolve_css_style.cpp` — `combined` buffer for grid-template-areas list migrated. Callers of `parse_grid_template_areas` pass `&lycon->scratch`.

**Phase 3: Render integration** — ✅ DONE
- `render.hpp` — added `ScratchArena scratch` field to `RenderContext`
- `render.cpp` — `render_init()` calls `scratch_init(&rdcon->scratch, view_tree->arena)` (after memset), `render_clean_up()` calls `scratch_release(&rdcon->scratch)`. `save_clip_region()`, `parse_css_clip_shape()`, `free_clip_mask()`, `free_clip_shape()` take `ScratchArena*`. `mix_blend_backdrop` migrated.
- `render_background.cpp` — `box_blur_region()` takes `ScratchArena*`, temp pixel buffer migrated. Callers pass `&rdcon->scratch`.
- `render_filter.cpp` — `apply_css_filters()` takes `ScratchArena*`, `shadow_px` buffer migrated. `box_blur_region` calls pass `sa`.

**Phase 4: Deferred (not migrated)**
- `graph_dagre.cpp` — 5 scoped arrays in isolated subsystem with no Arena/LayoutContext access. Plumbing cost outweighs benefit.
- `render_svg.cpp` / `render_pdf.cpp` — `text_content` buffers. `SvgRenderContext`/`PdfRenderContext` have no Arena field. Would require adding Arena to these context structs.
- `layout_counters.cpp` — `temp` int array. Small, low frequency.

### 9.7 Resolved Design Questions

| # | Question | Resolution |
|---|----------|------------|
| 1 | Should `ScratchArena` own its backing `Arena`, or receive one? | **Receive.** `scratch_init(sa, arena)` takes the existing `view_tree->arena`. ScratchArena is a stack-embedded struct, not heap-allocated. |
| 2 | Should pixel-sized buffers (10MB+) go through scratch? | **Yes.** In practice, Arena handles large allocations via dedicated chunks. Pixel buffers (blur, shadow, clip, blend) all go through scratch without issue. No size threshold needed. |
| 3 | Where to store the `ScratchArena`? | **`LayoutContext.scratch`** for layout, **`RenderContext.scratch`** for render. Both are stack-embedded fields initialized in `layout_init()`/`render_init()` and released in `layout_cleanup()`/`render_clean_up()`. |
| 4 | Alignment? | 16-byte alignment confirmed. `ScratchHeader` is exactly 16 bytes (`prev` 8 + `size` 4 + `flags` 4), so payload is naturally 16-byte aligned. |

### 9.8 Integration Summary

**Implementation files:**
- `lib/scratch_arena.h` — API declarations
- `lib/scratch_arena.c` — Implementation (LIFO bump-back, backward hole coalescing, mark/restore)
- `test/test_scratch_arena_gtest.cpp` — 21 unit tests

**Structural plumbing:**
- `LayoutContext.scratch` (layout.hpp) — initialized from `doc->view_tree->arena` in `layout_init()`
- `RenderContext.scratch` (render.hpp) — initialized from `view_tree->arena` in `render_init()`

**Migration scoreboard (§9.2 sites):**

| # | Site | Status |
|---|------|--------|
| 1 | `TableMetadata` 12 arrays | ✅ Migrated |
| 2 | `box_blur_region()` pixel buffer | ✅ Migrated (`ScratchArena*` param) |
| 3 | `mix_blend_backdrop` pixel buffer | ✅ Migrated |
| 4 | `apply_drop_shadow_filter()` shadow buffer | ✅ Migrated (`ScratchArena*` param) |
| 5 | `save_clip_region()` ClipMask + pixels | ✅ Migrated (`ScratchArena*` param) |
| 6 | `parse_css_clip_shape()` ClipShape + polygon | ✅ Migrated (`ScratchArena*` param) |
| 7 | `explicit_col_widths`, `col_x_positions` | ✅ Migrated |
| 8 | `row_positions`, `column_positions` | ✅ Migrated |
| 9 | `parse_grid_template_areas()` grid_cells + unique_names | ✅ Migrated (mark/restore) |
| 10 | `graph_dagre.cpp` 5 scoped arrays | ⏭️ Deferred — isolated subsystem, no Arena access |
| 11 | `row_max_baseline`, `row_max_below` | ✅ Migrated |
| 12 | `col_min`, `col_max` | ✅ Migrated |
| 13 | `render_svg.cpp` / `render_pdf.cpp` text_content | ⏭️ Deferred — context structs lack Arena field |
| 14 | `layout_counters.cpp` temp array | ⏭️ Deferred — low impact |
| 15 | `resolve_css_style.cpp` combined buffer | ✅ Migrated |

**Result: 12/15 sites migrated. 3 deferred (low-value or high plumbing cost).**

**Test verification (zero regressions):**
- Scratch arena unit tests: 21/21 passed
- Arena unit tests: 90/90 passed
- Radiant baseline: 4701/4710 (identical to pre-migration)
- Lambda baseline: 565/566 (identical to pre-migration)

---

*End of survey.*

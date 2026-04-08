# Radiant Layout Engine — Batch Memory Management Fix

Date: April 2025

## Problem

When running `lambda.exe layout` in **batch mode** (processing hundreds/thousands of HTML files sequentially), memory grew without bound. At 1000 files, RSS reached ~970 MB. The primary sources:

1. **Font handle leaks** — face_cache and codepoint_fallback_cache never freed replaced entries; font file data duplicated across handles.
2. **JS GC heap leak** — each `<script>`-bearing HTML file created a ~12 MB GC heap (4 MB data zone + 4 MB tenured zone + 4 MB bump block + metadata) that was never freed. With ~58% of baseline test files containing `<script>` tags, this was the dominant leak.
3. **rpmalloc corruption** — attempting to use `rpmalloc_heap_free_all` to reclaim JS pools caused state corruption and crashes after ~500 pool create/destroy cycles.
4. **Stale JS globals** — static C variables in the JS runtime held `Item` values pointing into destroyed pools, causing use-after-free crashes when pools were freed.
5. **GC root registration** — a `static bool statics_rooted` guard prevented re-registration of GC roots on new heaps after the first heap was destroyed.

## Solution Overview

### 1. Font Leak Fixes (lib/font/)

**Files**: `font_cache.c`, `font_face.c`, `font_fallback.c`, `font_internal.h`, `font_loader.c`, `font_context.c`

- **face_cache**: Free replaced `FT_Face` when cache insert evicts an existing entry.
- **codepoint_fallback_cache**: Free replaced font handles on cache eviction.
- **Font file data dedup**: Share loaded font file data across handles for the same file, avoiding duplicate 1-10 MB buffers per face.

### 2. mmap-Backed Pool Allocator (lib/mempool.c, lib/mempool.h)

Bypasses rpmalloc entirely for JS execution pools. Uses `mmap(MAP_PRIVATE | MAP_ANONYMOUS)` for allocation and `munmap` for deallocation — no fragmentation, no rpmalloc state corruption.

```
Pool struct (mmap mode):
  heap = NULL          ← signals mmap mode (vs rpmalloc mode when non-NULL)
  pool_id              ← unique ID for debugging
  valid                ← POOL_VALID_MARKER when live
  chunks → MmapChunk*  ← linked list of 4 MB mmap'd regions
  cursor               ← bump pointer within current chunk
  limit                ← end of current chunk

MmapChunk:
  next   → MmapChunk*
  base   → uint8_t*    (mmap'd region start)
  size   → size_t      (region size, page-aligned)
```

**Key functions**:
- `pool_create_mmap()` — allocates Pool + initial 4 MB mmap chunk
- `mmap_pool_grow(pool, min_size)` — adds a new chunk when current is exhausted
- `mmap_pool_free_chunks(pool)` — munmaps all chunks
- `pool_destroy(pool)` — frees all chunks + the Pool struct (mmap or rpmalloc mode)
- `pool_drain(pool)` — frees chunks but leaves Pool struct for reuse
- `pool_alloc/pool_calloc` — bump-allocate from cursor; grow if needed (mmap mode) or delegate to rpmalloc (heap mode)

### 3. GC Heap with External Pool (lib/gc/gc_heap.c, lib/gc/gc_heap.h)

`gc_heap_create_with_pool(Pool* pool)` — creates a GC heap reusing an externally-owned pool instead of creating its own. On error paths, sets `gc->pool = NULL` so the caller retains pool ownership.

### 4. Runtime Pool Reuse Plumbing (lambda/transpiler.hpp, lambda/lambda-mem.cpp, lambda/js/transpile_js_mir.cpp)

- `Runtime::reuse_pool` field — when set, the JS transpiler calls `heap_init_with_pool(runtime->reuse_pool)` instead of `heap_init()`.
- `heap_init_with_pool(Pool* pool)` — initializes the Lambda heap using `gc_heap_create_with_pool(pool)`.
- Two call sites in `transpile_js_mir.cpp` check `runtime->reuse_pool` before falling back to standard `heap_init()`.

### 5. Per-File JS State Reset (lambda/js/js_runtime.cpp, lambda/js/js_globals.cpp)

The core fix enabling safe `pool_destroy`. All static globals referencing pool-allocated data are reset between files:

**js_batch_reset()** clears:
- `js_exception_value`, `js_current_this`, `js_new_target`, `js_pending_new_target` — runtime state Items
- `js_array_method_real_this`, `js_input` — execution context
- Module variable table, module registry, module cache
- Global object singletons via: `js_reset_math_object()`, `js_reset_json_object()`, `js_reset_console_object()`, `js_reset_reflect_object()`
- `js_reset_proto_key()` — interned `__proto__` key string
- `js_func_cache_reset()` — function pointer → JsFunction mapping cache
- `js_builtin_cache_reset()` — builtin function cache
- `js_deep_batch_reset()` — generators, promises, async contexts, pending call args, call counter

**js_globals_batch_reset()** clears:
- `js_global_this_obj` — the global `this` object
- `js_ctor_cache_reset()` — constructor function cache (memset + reinit flag)
- `js_process_argv_items` — cached process.argv

**js_dom_batch_reset()** clears:
- DOM-related JS state (pre-existing, called alongside the new resets)

### 6. GC Root Re-Registration Fix (lambda/js/js_runtime.cpp)

**Bug**: `js_runtime_set_input()` had a `static bool statics_rooted` guard that registered GC roots only once (first heap). After the first heap was destroyed and a new one created, the static `Item` variables (`js_current_this`, `js_new_target`, etc.) were not registered as roots on the new heap, leading to premature collection.

**Fix**: Removed the static guard. `heap_register_gc_root` is now called on every `js_runtime_set_input()` invocation, ensuring each new GC heap has the correct roots.

### 7. Script Runner Integration (radiant/script_runner.cpp)

**Per-file lifecycle**:
1. `execute_document_scripts()` creates a fresh mmap pool: `runtime.reuse_pool = pool_create_mmap()`
2. JS transpiler uses the pool via `heap_init_with_pool()`
3. After execution: extract pool from `gc_heap` (set `gc->pool = NULL`), destroy GC heap metadata/nursery/Heap struct, stash pool to `s_js_reuse_pool`
4. `script_runner_cleanup_heap()` — called during per-file cleanup — calls `pool_destroy(s_js_reuse_pool)`

### 8. Cleanup Ordering (radiant/cmd_layout.cpp)

Critical: JS globals must be reset **before** the pool is destroyed.

```
js_batch_reset();                  // clear all JS static Items
js_dom_batch_reset();              // clear DOM JS state
js_globals_batch_reset();          // clear constructor cache, global this
script_runner_cleanup_heap();      // pool_destroy — safe now, no dangling refs
font_context_reset_document_fonts();  // reset per-document font state
image_cache_cleanup();             // release decoded image cache
```

## Results

| Metric | Before fix | After fix (Phase 1) | After fix (Phase 2) |
|--------|-----------|-----------|-----------|
| RSS at 1000 files | ~970 MB | ~612 MB | — |
| main_arena at 100 files | — | ~99 MB | **444 KB** |
| main_arena at 2500 files | — | ~370 MB | **444 KB** (stable) |
| RSS at 100 files | — | — | ~806 MB |
| Non-font RSS growth | — | ~661 KB/file (peak) | ~40 KB/file (steady-state, measured with current RSS) |
| Max files before crash | ~850 (rpmalloc) | 2500+ | 2500+ (pre-existing stack overflow on specific files) |
| Layout correctness | — | 50/50 verified | All unit tests pass |
| Build errors | 0 | 0 | 0 |

## Files Modified

| File | Changes |
|------|---------|
| `lib/mempool.c` | MmapChunk struct, mmap mode in Pool, `pool_create_mmap()`, `mmap_pool_grow()`, `mmap_pool_free_chunks()`, `pool_drain()`, dual-mode alloc/calloc/free/realloc |
| `lib/mempool.h` | `pool_create_mmap()`, `pool_drain()` declarations |
| `lib/gc/gc_heap.c` | `gc_heap_create_with_pool(Pool*)` |
| `lib/gc/gc_heap.h` | `gc_heap_create_with_pool` declaration |
| `lambda/transpiler.hpp` | `heap_init_with_pool()` declaration, `Runtime::reuse_pool` field |
| `lambda/lambda-mem.cpp` | `heap_init_with_pool(Pool*)` implementation |
| `lambda/js/transpile_js_mir.cpp` | Two `heap_init` call sites check `runtime->reuse_pool` |
| `lambda/js/js_runtime.cpp` | Comprehensive `js_batch_reset()`, 7 new reset functions, `js_deep_batch_reset()`, GC root re-registration fix |
| `lambda/js/js_globals.cpp` | `js_ctor_cache_reset()`, `js_process_argv_items` reset in `js_globals_batch_reset()` |
| `radiant/script_runner.cpp` | mmap pool lifecycle (create → extract → stash → destroy) |
| `radiant/cmd_layout.cpp` | Cleanup ordering, MEMDIAG diagnostics, crash signal handler |
| `lib/font/font_cache.c` | Free replaced face on cache eviction |
| `lib/font/font_face.c` | Font handle leak fix |
| `lib/font/font_fallback.c` | Codepoint fallback cache eviction fix |
| `lib/font/font_loader.c` | Font file data deduplication |
| `lib/font/font_context.c` | Document font reset support, file_data ref-count decrement, handle leak fix in reset_document_fonts |
| `lib/font/font_internal.h` | Cache stats struct for diagnostics, ref_count in FontFileDataEntry, file_data_path in FontHandle |

## Phase 2: Font File Data Malloc Migration (April 2025)

### Problem

Font file data (TTF/OTF raw bytes, decompressed WOFF/WOFF2 data) was allocated via the arena bump allocator. When faces were evicted from the LRU face_cache (max 64 entries), their font file data could NOT be individually freed — it stayed in the arena forever. This caused main_arena to grow to ~370 MB at 2500 files, with massive jumps when CJK fonts were loaded (+66MB, +203MB).

### Solution

Changed font file data from arena allocation to `malloc` with reference counting:

1. **FontFileDataEntry** gained `int ref_count` — tracks how many FontHandles reference the cached data
2. **FontHandle** gained `char* file_data_path` — links handle back to its file_data_cache entry
3. **file_data_cache** gets a `file_data_free` callback for cleanup on hashmap_free/clear
4. **font_decompress_woff1/woff2** — when `arena=NULL`, use `calloc`/`malloc` instead of arena (backward compatible: tests still pass arena)
5. **font_load_face_internal** — TTF and WOFF paths both use cached data directly (no copy), set `file_data_path` on handles
6. **font_load_memory_internal** — uses `malloc` for data copy (for data URIs and direct memory loads)
7. **font_handle_release** — decrements file_data ref_count; when reaching 0, removes entry from cache and frees data
8. **font_context_reset_document_fonts** — fixed pre-existing bug: now releases handles when deleting from face_cache (hashmap_delete does NOT call free callback)

### Files Modified (Phase 2)

| File | Changes |
|------|---------|
| `lib/font/font_internal.h` | `ref_count` in FontFileDataEntry, `file_data_path` in FontHandle |
| `lib/font/font.h` | `main_arena_bytes`, `glyph_arena_bytes`, `loaded_glyph_count` in FontCacheStats |
| `lib/font/font_loader.c` | malloc-based file data, `file_data_free` callback, `mem_strdup` for paths, direct FT_New_Memory_Face for WOFF cache hits |
| `lib/font/font_decompress.cpp` | `arena=NULL` → malloc mode for woff1/woff2 decompression |
| `lib/font/font_context.c` | ref_count logic in `font_handle_release`, handle release in `reset_document_fonts`, split arena stats |
| `radiant/cmd_layout.cpp` | Current-RSS via `mach_task_basic_info`, split arena MEMDIAG |

## Remaining Issues

### 1. ~~Font Arena Growth~~ — FIXED (Phase 2)

Font arena (main_arena) reduced from ~370 MB to **444 KB** (stable) via malloc+refcount migration. See Phase 2 above.

### 2. Non-Font RSS Growth (~40 KB/file steady-state)

Previously estimated at ~661 KB/file using peak RSS (`ru_maxrss`). With current-RSS measurement (`mach_task_basic_info`), steady-state growth is only ~40 KB/file after the initial font loading ramp. The ~760 MB non-font RSS is dominated by FreeType internal state in the rpmalloc font pool (64 cached faces × CJK font state). This is stable and not a leak.

**Potential approaches** (if further reduction needed):
- Lower `max_cached_faces` to reduce FreeType pool retention
- Periodically recreate the FreeType library + rpmalloc pool to reclaim pages

### 3. Pre-existing Stack Overflow in Batch Mode

Some HTML files trigger stack overflow (`___chkstk_darwin`, `EXC_BAD_ACCESS code=2`) during JS compilation when processed in batch mode. This is a pre-existing issue (reproduces without font changes) caused by deep recursion in the JS transpiler or layout code. Affects files with complex forms/inputs.

### 4. Diagnostic Code Still Present

`cmd_layout.cpp` contains:
- `[MEMDIAG]` fprintf with `#include <sys/resource.h>` — RSS/font_arena tracking per file
- `crash_signal_handler()` with `backtrace()` — crash diagnostics

These are useful for monitoring but should be removed or guarded before release.

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

| Metric | Before fix | After fix |
|--------|-----------|-----------|
| RSS at 1000 files | ~970 MB | ~612 MB |
| RSS reduction | — | **37%** |
| Max files before crash | ~850 (rpmalloc corruption) | 2519+ (no crash) |
| Layout correctness | — | 50/50 files verified |
| Build errors | 0 | 0 |

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
| `lib/font/font_context.c` | Document font reset support |
| `lib/font/font_internal.h` | Cache stats struct for diagnostics |

## Remaining Issues

### 1. Font Arena Growth (~361 MB at 2500 files)

The font arena (bump allocator, no individual free) grows from ~99 MB at 1000 files to ~361 MB at 2500 files. Large jumps occur when new font faces are loaded (e.g., CJK fonts adding ~200 MB). The arena stores glyph outlines, metrics, and rasterized data.

**Potential approaches**:
- Periodically compact or reset the font arena between batches of files
- Implement per-face arena segments that can be freed when a face is evicted
- Cap the arena size and evict least-recently-used glyph data

### 2. Non-Font RSS Growth (~661 KB/file)

At 2500 files, non-font RSS is ~1653 MB (~661 KB/file). Possible causes:
- rpmalloc fragmentation from InputManager pool cycling (create/destroy per file)
- General malloc fragmentation from per-file allocations
- Accumulated metadata not covered by existing cleanup

### 3. Diagnostic Code Still Present

`cmd_layout.cpp` contains:
- `[MEMDIAG]` fprintf with `#include <sys/resource.h>` — RSS/font_arena tracking per file
- `crash_signal_handler()` with `backtrace()` — crash diagnostics

These are useful for monitoring but should be removed or guarded before release.

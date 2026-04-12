# Proposal: Complete Memory Tracking for Lambda/Radiant

**Goal**: Every byte allocated during Lambda execution is accounted for — either tracked by memtrack, managed by Pool/Arena, or owned by a GC heap. At shutdown, all tracked allocations reach zero. No raw `malloc`/`calloc`/`free` calls remain in `lambda/` or `radiant/`.

---

## Table of Contents

1. [Design Principles](#1-design-principles)
2. [Current State](#2-current-state)
3. [Target State](#3-target-state)
4. [stdlib.h Elimination](#4-stdlibh-elimination)
5. [Migration Inventory](#5-migration-inventory)
6. [New Categories](#6-new-categories)
7. [STATS Mode Fix: Size Tracking on Free](#7-stats-mode-fix-size-tracking-on-free)
8. [Pool/Arena Integration](#8-poolarena-integration)
9. [Shutdown Verification](#9-shutdown-verification)
10. [Migration Plan](#10-migration-plan)
11. [Validation & CI](#11-validation--ci)

---

## 1. Design Principles

1. **All memory falls into one of three buckets:**
   - **Tracked** — `mem_alloc`/`mem_calloc`/`mem_free` via memtrack. Every alloc has a matching free.
   - **Pool/Arena** — Managed by `pool_alloc`/`arena_alloc`/`scratch_alloc`. Freed in bulk by `pool_destroy`/`arena_reset`/`scratch_restore`.
   - **GC Heap** — Script engine heap (`heap_alloc`). Collected by garbage collector.

2. **No raw `malloc`/`calloc`/`realloc`/`free`/`strdup` in `lambda/` or `radiant/`.**
   Every call becomes `mem_alloc`/`mem_calloc`/`mem_realloc`/`mem_free`/`mem_strdup` with an appropriate `MemCategory`.

3. **`<stdlib.h>` is not directly included in `lambda/` or `radiant/` source files.**
   A new header `lib/mem.h` provides the tracked allocation API plus re-exports the non-allocation stdlib functions (`strtol`, `atoi`, `abs`, `getenv`, `qsort`, `exit`, etc.).

4. **At program exit, tracked allocation count reaches zero.**
   `memtrack_shutdown()` reports any leaks. In CI, non-zero leak count is a test failure.

5. **`lib/` foundation types are exempt.**
   Core allocator infrastructure (`mempool.c`, `arena.c`, `memtrack.c`, `hashmap.c`, `arraylist.c`, `strbuf.c`, `str.c`, `gc/*.c`) may use raw `malloc` since they _are_ the allocation layer. These files are the only ones that include `<stdlib.h>` directly.

---

## 2. Current State

### 2.1 memtrack Adoption

| Area | Files using memtrack | Raw malloc files | Status |
|------|---------------------|-----------------|--------|
| Radiant layout/render | ~20 files | ~15 files | Partially migrated |
| Lambda core | 3 files | ~96 files | Barely started |
| lib/ (excl. foundations) | 5 files | ~15 files | Partially migrated |

### 2.2 Raw malloc/calloc/free Inventory

| Directory | Raw alloc calls | Files affected |
|-----------|----------------|----------------|
| `lambda/` | ~478 | 96 files |
| `radiant/` | ~49 | 15 files |
| `lib/` (non-foundation) | ~80 | ~15 files |
| **Total to migrate** | **~607** | **~126 files** |

### 2.3 Largest Offenders

| Count | File | Pattern |
|-------|------|---------|
| 19 | `lambda/input/pdf_decompress.cpp` | Decompression buffers |
| 18 | `lambda/runner.cpp` | Import graph, paths, Script structs |
| 18 | `lambda/js/transpile_js_mir.cpp` | Transpiler structs, import graph |
| 17 | `lambda/input/markup/inline/inline_format_specific.cpp` | Temp string buffers |
| 14 | `lambda/py/py_builtins.cpp` | String operation buffers |
| 14 | `lambda/js/js_globals.cpp` | String operation buffers |
| 13 | `lambda/bash/bash_runtime.cpp` | Command/env buffers |
| 12 | `lambda/transpile-mir.cpp` | MIR transpiler allocations |
| 12 | `lambda/lambda-error.cpp` | Error message formatting |
| 10 | `radiant/layout_graph.cpp` | Graph layout structs |

### 2.4 memtrack STATS Mode Blind Spot

In `STATS` mode, `mem_free(ptr)` only decrements `current_count` but **cannot decrement `current_bytes`** because the allocation size is not stored (no per-allocation hashmap in STATS mode). This means `current_bytes` only grows, making the stat useless for monitoring. This must be fixed.

---

## 3. Target State

```
┌─────────────────────────────────────────────────────────────┐
│              lambda/ and radiant/ source files               │
│          #include "mem.h"  (NEVER #include <stdlib.h>)       │
├──────────┬──────────────┬──────────────┬────────────────────┤
│ mem_alloc│  pool_alloc  │ arena_alloc  │    heap_alloc      │
│ mem_free │  pool_free   │ arena_free   │    (GC managed)    │
├──────────┴──────────────┴──────────────┴────────────────────┤
│                    lib/memtrack.c                            │
│              (only file using raw malloc)                    │
├─────────────────────────────────────────────────────────────┤
│           lib/ foundations: mempool.c, arena.c,              │
│           hashmap.c, arraylist.c, strbuf.c, str.c,          │
│           gc/*.c  (use raw malloc — they ARE the layer)      │
├─────────────────────────────────────────────────────────────┤
│                  System malloc / mmap                        │
└─────────────────────────────────────────────────────────────┘
```

At shutdown:
```
memtrack: shutdown — 0 live allocations, 0 bytes outstanding
memtrack: peak usage: 42.3 MB across 156,892 allocations
memtrack: category breakdown:
  MEM_CAT_PARSER:  0 B (peak 8.2 MB, 45231 allocs, 45231 frees)
  MEM_CAT_LAYOUT:  0 B (peak 12.1 MB, 89012 allocs, 89012 frees)
  ...
```

---

## 4. stdlib.h Elimination

### 4.1 The Problem

77 files in `lambda/` and `radiant/` include `<stdlib.h>`. Most use it for both allocation functions AND non-allocation functions (`strtol`, `atoi`, `getenv`, `qsort`, `abs`, `exit`, etc.). We cannot simply remove the include.

### 4.2 Solution: `lib/mem.h` Unified Header

Create `lib/mem.h` — the single header that `lambda/` and `radiant/` files include for both tracked allocation and stdlib utilities:

```c
#ifndef MEM_H
#define MEM_H

// Tracked allocation API
#include "memtrack.h"

// Re-export non-allocation stdlib functions that lambda/radiant code needs.
// This avoids direct <stdlib.h> includes while keeping strtol/atoi/etc. available.
#include <stdlib.h>

// Poison raw allocation functions in lambda/radiant code.
// Any direct call to malloc/calloc/realloc/free will produce a compile error.
// This does NOT affect lib/ foundation files (they don't include mem.h).
#if defined(MEMTRACK_POISON_RAW_ALLOC)
// GCC/Clang: use #pragma GCC poison
#pragma GCC poison malloc calloc realloc free strdup strndup
#endif

#endif // MEM_H
```

### 4.3 Compile-Time Enforcement

- `lambda/` and `radiant/` files include `lib/mem.h` (which includes `<stdlib.h>` internally).
- Build defines `-DMEMTRACK_POISON_RAW_ALLOC` for `lambda/` and `radiant/` targets.
- `#pragma GCC poison` makes any raw `malloc`/`free` call a **compile error**.
- `lib/` foundation files include `<stdlib.h>` directly and do NOT define `MEMTRACK_POISON_RAW_ALLOC`.

### 4.4 Migration Decision Tree

For each `malloc`/`calloc`/`realloc` call, do **not** blindly convert to `mem_alloc`. First evaluate the allocation's lifetime and usage pattern:

```
Is this allocation short-lived and scoped to a single function/pass?
  ├─ YES → Does a ScratchArena already exist in this scope?
  │         ├─ YES → Use scratch_alloc(). No free needed.
  │         └─ NO  → Is the call site in a hot loop or parser pass?
  │                   ├─ YES → Introduce a ScratchArena. Use scratch_alloc().
  │                   └─ NO  → Use mem_alloc() with appropriate category.
  └─ NO  → Is this allocation tied to a well-defined owner with bulk cleanup?
            ├─ YES → Use pool_alloc() or arena_alloc() from the owner's pool/arena.
            └─ NO  → Use mem_alloc() with appropriate category. Ensure matching mem_free().
```

**Prefer Pool/Arena/Scratch over mem_alloc when possible.** Bulk-freed allocations are cheaper (no per-object free overhead) and cannot leak individually. Only fall back to `mem_alloc`/`mem_free` for allocations that genuinely need independent lifetime management.

Examples of good Pool/Arena candidates (convert to pool/arena, NOT mem_alloc):
- Parser temporary buffers that live for one parse pass → `scratch_alloc`
- Layout structs allocated per-layout-pass and freed at end → `arena_alloc`
- AST nodes tied to a Script's lifetime → `pool_alloc` from Script's pool
- Decompression buffers in `pdf_decompress.cpp` → `scratch_alloc`

Examples that must be mem_alloc (independent lifetime):
- Module registry entries that persist across script runs
- Error message strings returned to callers
- Import graph nodes freed individually during resolution
- Runtime singletons (allocated once, freed at shutdown)

### 4.5 Migration Per File

For each file in `lambda/` and `radiant/`:

1. Replace `#include <stdlib.h>` with `#include "mem.h"` (if not already included via another header).
2. **For each `malloc`/`calloc` call, apply the decision tree (§4.4):**
   - If short-lived or bulk-freed → convert to `scratch_alloc`/`arena_alloc`/`pool_alloc` and remove corresponding `free`.
   - Otherwise → convert to `mem_alloc(size, MEM_CAT_XXX)` / `mem_calloc(n, size, MEM_CAT_XXX)`.
3. Replace remaining `realloc(ptr, size)` → `mem_realloc(ptr, size, MEM_CAT_XXX)`.
4. Replace remaining `free(ptr)` → `mem_free(ptr)` (only for mem_alloc'd pointers; pool/arena/scratch pointers have no individual free).
5. Replace all `strdup(s)` → `mem_strdup(s, MEM_CAT_XXX)` (or `scratch_alloc` + `memcpy` if short-lived).
6. Build with `-DMEMTRACK_POISON_RAW_ALLOC` — any missed call is a compile error.

---

## 5. Migration Inventory

### 5.1 Phase 1 — Radiant (49 calls, 15 files)

Radiant already has heavy memtrack adoption. Remaining raw calls are in:

| Priority | File | Calls | Category | Notes |
|----------|------|-------|----------|-------|
| P1 | `radiant/layout_graph.cpp` | 10 | `MEM_CAT_LAYOUT` | Graph layout structs |
| P1 | `radiant/rdt_vector_tvg.cpp` | 9 | `MEM_CAT_RENDER` | ThorVG wrapper structs |
| P1 | `radiant/graph_dagre.cpp` | 8 | `MEM_CAT_LAYOUT` | Dagre algorithm arrays |
| P2 | `radiant/graph_theme.cpp` | 5 | `MEM_CAT_STYLE` | DiagramTheme struct |
| P2 | `radiant/graph_edge_utils.cpp` | 4 | `MEM_CAT_LAYOUT` | Edge routing points |
| P2 | `radiant/webdriver/webdriver_actions.cpp` | 3 | `MEM_CAT_TEMP` | Base64 buffers |
| P3 | `radiant/render_svg_inline.cpp` | 2 | `MEM_CAT_RENDER` | Font path, text |
| P3 | `radiant/graph_to_svg.cpp` | 2 | `MEM_CAT_FORMAT` | SVG generator options |
| P3 | `radiant/cmd_layout.cpp` | 2 | `MEM_CAT_LAYOUT` | MathInfo, Runtime |
| P3 | `radiant/font_face.cpp` | 2 | `MEM_CAT_FONT` | Stylesheet path |
| P3 | `radiant/layout_table.cpp` | 1 | `MEM_CAT_LAYOUT` | Grid occupancy bitmap |
| P3 | `radiant/surface.cpp` | 1 | `MEM_CAT_RENDER` | File path from URL |

### 5.2 Phase 2 — Lambda Core (150 calls, ~30 files)

Core runtime, AST, eval, memory, data:

| Priority | File | Calls | Category | Notes |
|----------|------|-------|----------|-------|
| P1 | `lambda/runner.cpp` | 18 | `MEM_CAT_EVAL` | Import graph, Script structs, paths |
| P1 | `lambda/transpile-mir.cpp` | 12 | `MEM_CAT_AST` | MIR transpiler allocations |
| P1 | `lambda/lambda-error.cpp` | 12 | `MEM_CAT_EVAL` | Error message formatting |
| P1 | `lambda/build_ast.cpp` | 5 | `MEM_CAT_AST` | Number/hex literal strings |
| P2 | `lambda/main.cpp` | 6 | `MEM_CAT_EVAL` | Runtime, paths |
| P2 | `lambda/lambda-vector.cpp` | 6 | `MEM_CAT_CONTAINER` | Vector item buffers |
| P2 | `lambda/module_registry.cpp` | 6 | `MEM_CAT_EVAL` | Module paths/entries |
| P2 | `lambda/pack.cpp` | 6 | `MEM_CAT_TEMP` | Binary pack/unpack |
| P2 | `lambda/mark_editor.cpp` | 4 | `MEM_CAT_CONTAINER` | Array/element realloc |
| P2 | `lambda/vmap.cpp` | 4 | `MEM_CAT_CONTAINER` | HashMap, value boxes |
| P2 | `lambda/validator/doc_validator.cpp` | 4 | `MEM_CAT_PARSER` | ValidationResult/Error |
| P2 | `lambda/transpile.cpp` | 4 | `MEM_CAT_AST` | C transpiler |
| P3 | `lambda/lambda-eval.cpp` | 3 | `MEM_CAT_EVAL` | Eval-time allocs |
| P3 | `lambda/lambda-mem.cpp` | 2 | `MEM_CAT_STRING` | Memory helpers |
| P3 | `lambda/lambda-eval-num.cpp` | 2 | `MEM_CAT_EVAL` | Numeric buffers |
| P3 | `lambda/mir.c` | 4 | `MEM_CAT_AST` | FuncDebugInfo list |
| P3 | `lambda/lambda-stack.cpp` | 1 | `MEM_CAT_EVAL` | sigaltstack memory |
| P3 | Others (13 files) | ~13 | Various | 1-2 calls each |

### 5.3 Phase 3 — Input Parsers (120 calls, ~30 files)

| Priority | Subsystem | Files | Calls | Category |
|----------|-----------|-------|-------|----------|
| P1 | Markup parser | 15 files in `input/markup/` | ~55 | `MEM_CAT_INPUT_MARKUP` |
| P1 | PDF decompress | `input/pdf_decompress.cpp` | 19 | `MEM_CAT_INPUT_PDF` |
| P2 | MDX parser | `input/input-mdx.cpp` | 8 | `MEM_CAT_INPUT_MDX` |
| P2 | HTTP input | `input/input_http.cpp` | 7 | `MEM_CAT_INPUT_OTHER` |
| P2 | CSS subsystem | 3 files in `input/css/` | ~6 | `MEM_CAT_INPUT_CSS` |
| P3 | Others | 7 files | ~12 | Various input cats |

### 5.4 Phase 4 — Script Engines (200 calls, ~35 files)

| Priority | Engine | Files | Calls | Category |
|----------|--------|-------|-------|----------|
| P1 | JavaScript | `js/*.cpp` (12 files) | ~75 | `MEM_CAT_JS_RUNTIME` (new) |
| P1 | Python | `py/*.cpp` (6 files) | ~35 | `MEM_CAT_PY_RUNTIME` (new) |
| P2 | Bash | `bash/*.cpp` (5 files) | ~35 | `MEM_CAT_BASH_RUNTIME` (new) |
| P2 | Ruby | `rb/*.cpp` (4 files) | ~15 | `MEM_CAT_RB_RUNTIME` (new) |
| P3 | TypeScript | `ts/*.cpp` (1 file) | ~1 | `MEM_CAT_TEMP` |

### 5.5 Phase 5 — Network/Serve (45 calls, ~12 files)

| Priority | Subsystem | Files | Calls | Category |
|----------|-----------|-------|-------|----------|
| P1 | Network core | 4 files | ~28 | `MEM_CAT_NETWORK` (new) |
| P2 | Serve/HTTP | 7 files in `serve/` | ~25 | `MEM_CAT_SERVE` (new) |

### 5.6 Phase 6 — lib/ Non-Foundation (80 calls, ~15 files)

| Priority | File | Calls | Category | Notes |
|----------|------|-------|----------|-------|
| P1 | `lib/shell.c` | 21 | `MEM_CAT_TEMP` | Command building, pipe/redirect |
| P1 | `lib/file.c` | 18 | `MEM_CAT_TEMP` | File I/O buffers, path construction |
| P1 | `lib/url_parser.c` | 17 | `MEM_CAT_TEMP` | URL parsing/manipulation |
| P1 | `lib/file_utils.c` | 15 | `MEM_CAT_TEMP` | Path resolution, temp dirs |
| P2 | `lib/url.c` | 13 | `MEM_CAT_TEMP` | URL/String structs |
| P2 | `lib/cmdedit.c` | 13 | `MEM_CAT_TEMP` | Command-line editing |
| P2 | `lib/image.c` | 8 | `MEM_CAT_IMAGE` | Decode buffers |
| P3 | `lib/num_stack.c` | 5 | `MEM_CAT_EVAL` | Numeric stack |
| P3 | `lib/log.c` | 5 | exempt | Log system (foundation-adjacent) |
| P3 | `lib/font/*.c` | 10 | `MEM_CAT_FONT` | Font data loading |
| P3 | `lib/pdf_writer.c` | 2 | `MEM_CAT_FORMAT` | PDF output buffers |
| P3 | `lib/base64.c` | 2 | `MEM_CAT_TEMP` | Encode/decode buffers |

### 5.7 Exempt — lib/ Foundation (never migrate)

These files ARE the allocation infrastructure. They use raw malloc by design:

| File | Reason |
|------|--------|
| `lib/memtrack.c` | The tracker itself |
| `lib/mempool.c` | Pool allocator (wraps rpmalloc) |
| `lib/arena.c` | Arena allocator (wraps Pool) |
| `lib/scratch_arena.c` | Scratch allocator (wraps Arena) |
| `lib/hashmap.c` | Used by memtrack internally |
| `lib/arraylist.c` | Core container type |
| `lib/str.c` | Core string type |
| `lib/strbuf.c` | Core string builder |
| `lib/gc/*.c` | Garbage collector (manages its own heap) |
| `lib/priority_queue.c` | Core data structure |

---

## 6. New Categories

Add to `MemCategory` enum in `memtrack.h`:

```c
    // Script engine runtimes
    MEM_CAT_JS_RUNTIME,     // JavaScript runtime (transpiler, builtins, state)
    MEM_CAT_PY_RUNTIME,     // Python runtime
    MEM_CAT_RB_RUNTIME,     // Ruby runtime
    MEM_CAT_BASH_RUNTIME,   // Bash runtime

    // Network/serve
    MEM_CAT_NETWORK,        // Network downloads, cache, HTTP
    MEM_CAT_SERVE,          // HTTP server, ASGI, REST

    // System
    MEM_CAT_SYSTEM,         // Main runtime, module registry, sysinfo
```

These 7 new categories cover the ~280 calls in script engines and network code. Total categories: ~44.

---

## 7. STATS Mode Fix: Size Tracking on Free

### 7.1 Problem

In `STATS` mode, `mem_free(ptr)` decrements `current_count` but cannot decrement `current_bytes` because no per-allocation size is stored. The `current_bytes` counter only increases, making it useless.

### 7.2 Solution: Inline Size Header

Embed the allocation size (and category) in a small header before the user pointer, without requiring a hashmap:

```c
// 16-byte header (keeps 16-byte alignment of user data)
typedef struct MemAllocHeader {
    uint32_t size;          // allocation size (up to 4 GB)
    uint16_t category;      // MemCategory
    uint16_t magic;         // 0xBEEF — validates this is a tracked allocation
} MemAllocHeader;
_Static_assert(sizeof(MemAllocHeader) == 8, "header must be 8 bytes");

// In STATS mode:
void* mem_alloc(size_t size, MemCategory cat) {
    void* raw = malloc(sizeof(MemAllocHeader) + size);
    MemAllocHeader* hdr = (MemAllocHeader*)raw;
    hdr->size = (uint32_t)size;
    hdr->category = (uint16_t)cat;
    hdr->magic = 0xBEEF;
    // update stats ...
    return (void*)(hdr + 1);  // return pointer past header
}

void mem_free(void* ptr) {
    MemAllocHeader* hdr = ((MemAllocHeader*)ptr) - 1;
    if (hdr->magic != 0xBEEF) { log_error(...); return; }
    // stats.current_bytes -= hdr->size;  ← NOW POSSIBLE
    // stats.categories[hdr->category].current_bytes -= hdr->size;
    free(hdr);
}
```

**Cost**: 8 bytes per allocation (not 16 — the header is 8 bytes, and user data follows directly; if 16-byte alignment is needed, pad to 16). In DEBUG mode the existing 32-byte guard scheme already stores size, so the header can be skipped.

### 7.3 Alignment Consideration

For 16-byte aligned user pointers (SIMD), the header must be padded to 16 bytes:

```c
typedef struct MemAllocHeader {
    uint32_t size;
    uint16_t category;
    uint16_t magic;
    uint8_t  _pad[8];      // pad to 16 bytes for alignment
} MemAllocHeader;
```

Or use the 8-byte header and adjust: allocate `8 + size`, return `raw + 8`. Since `malloc` returns 16-byte aligned, `raw + 8` is 8-byte aligned. If 16-byte alignment is required for specific allocations, use `mem_alloc_aligned()` separately.

For most allocations (structs, strings, buffers), 8-byte alignment is sufficient. Reserve 16-byte alignment for SIMD (`__m128`) use cases only.

---

## 8. Pool/Arena Integration

### 8.1 Current State

`memtrack_pool_create`, `memtrack_arena_create`, etc. are **stub functions** that just delegate to `pool_create`/`arena_create` without tracking.

### 8.2 Tracking Strategy

Pool and Arena allocations don't need per-object tracking (that's Pool/Arena's job). What we track:

| What | How | Category |
|------|-----|----------|
| Pool creation/destruction | `memtrack_pool_create`/`destroy` — increment/decrement a pool counter | `MEM_CAT_POOL` (new) |
| Arena creation/destruction | `memtrack_arena_create`/`destroy` — same | `MEM_CAT_ARENA` (new) |
| Total pool bytes | On `pool_destroy`, add pool's total allocated bytes to memtrack stats | — |
| Total arena bytes | On `arena_destroy`, add arena's total allocated bytes | — |

This is **lightweight** — only lifecycle events are tracked, not individual pool/arena allocations. At shutdown, all pools/arenas should be destroyed, so their counters should reach zero.

### 8.3 Implementation

```c
Pool* memtrack_pool_create(void) {
    Pool* pool = pool_create();
    if (pool) {
        atomic_fetch_add(&g_memtrack.pool_count, 1);
    }
    return pool;
}

void memtrack_pool_destroy(Pool* pool) {
    if (pool) {
        atomic_fetch_sub(&g_memtrack.pool_count, 1);
    }
    pool_destroy(pool);
}
// (Similarly for arena)
```

At shutdown, `pool_count` and `arena_count` must be zero.

---

## 9. Shutdown Verification

### 9.1 `memtrack_shutdown()` Enhanced

```c
void memtrack_shutdown(void) {
    MemtrackStats stats;
    memtrack_get_stats(&stats);

    if (stats.current_count > 0) {
        log_error("memtrack: LEAK — %zu allocations (%zu bytes) still live at shutdown",
                  stats.current_count, stats.current_bytes);

        if (g_memtrack.mode == MEMTRACK_MODE_DEBUG) {
            // In debug mode, list all leaked allocations with file:line
            memtrack_log_allocations();
        } else {
            // In stats mode, show per-category breakdown
            for (int i = 0; i < MEM_CAT_COUNT; i++) {
                if (stats.categories[i].current_count > 0) {
                    log_error("  %s: %zu allocs, %zu bytes",
                              memtrack_category_names[i],
                              stats.categories[i].current_count,
                              stats.categories[i].current_bytes);
                }
            }
        }
    } else {
        log_info("memtrack: clean shutdown — 0 live allocations");
    }

    if (g_memtrack.pool_count > 0) {
        log_error("memtrack: %d pool(s) not destroyed at shutdown", g_memtrack.pool_count);
    }
    if (g_memtrack.arena_count > 0) {
        log_error("memtrack: %d arena(s) not destroyed at shutdown", g_memtrack.arena_count);
    }

    // ... existing cleanup ...
}
```

### 9.2 Return Code

`memtrack_shutdown()` returns `size_t` — the number of leaked allocations. In CI, this is checked:

```c
// In main.cpp
size_t leaks = memtrack_shutdown();
if (leaks > 0) {
    return EXIT_FAILURE;  // fail the process
}
```

---

## 10. Migration Plan

### Phase 0: Infrastructure (1 session)

| # | Task | Description |
|---|------|-------------|
| 0.1 | Create `lib/mem.h` | Unified header: includes `memtrack.h` + `<stdlib.h>`, defines `MEMTRACK_POISON_RAW_ALLOC` pragma poison |
| 0.2 | Fix STATS mode `mem_free` | Add inline size header (§7) so `current_bytes` decrements correctly |
| 0.3 | Add new categories | 7 new categories (§6) in `memtrack.h` |
| 0.4 | Implement pool/arena tracking stubs | Lifecycle tracking (§8) |
| 0.5 | Add `memtrack_shutdown` return value | Return leak count for CI |

### Phase 1: Radiant Cleanup (1 session)

Migrate the remaining ~49 raw calls in 15 radiant files. This is the smallest batch and validates the workflow.

- Replace `#include <stdlib.h>` → `#include "mem.h"` in all 15 files
- Convert all `malloc`→`mem_alloc`, `free`→`mem_free`, etc.
- Build with `-DMEMTRACK_POISON_RAW_ALLOC` for `radiant/` target
- Run `make test-radiant-baseline` — zero regressions
- Run with `MEMTRACK_MODE=DEBUG` — verify zero leaks in layout tests

### Phase 2: Lambda Core (1-2 sessions)

Migrate ~150 calls in ~30 core files (`runner.cpp`, `transpile*.cpp`, `build_ast.cpp`, `lambda-eval.cpp`, `lambda-error.cpp`, `lambda-vector.cpp`, `vmap.cpp`, etc.).

- These are the most impactful — core runtime + AST + eval
- Run `make test-lambda-baseline` after each batch
- Run with `MEMTRACK_MODE=DEBUG` — verify zero leaks

### Phase 3: Input Parsers (1-2 sessions)

Migrate ~120 calls in ~30 files. Mostly mechanical — markup parsers are highly repetitive (temp string buffers).

- Markup parser files (15 files, ~55 calls) — all use `MEM_CAT_INPUT_MARKUP`
- PDF decompress (19 calls) — `MEM_CAT_INPUT_PDF`
- Other parsers (15 files, ~46 calls)

### Phase 4: Script Engines (2-3 sessions)

Migrate ~200 calls in ~35 files. These are the most numerous but follow a consistent pattern per engine:

- JavaScript: 12 files, ~75 calls → `MEM_CAT_JS_RUNTIME`
- Python: 6 files, ~35 calls → `MEM_CAT_PY_RUNTIME`
- Bash: 5 files, ~35 calls → `MEM_CAT_BASH_RUNTIME`
- Ruby: 4 files, ~15 calls → `MEM_CAT_RB_RUNTIME`

### Phase 5: Network/Serve + lib/ (1-2 sessions)

- Network: 4 files, ~28 calls → `MEM_CAT_NETWORK`
- Serve: 7 files, ~25 calls → `MEM_CAT_SERVE`
- lib/ non-foundation: ~15 files, ~80 calls → various categories

### Phase 6: Poison Enforcement (1 session)

- Enable `-DMEMTRACK_POISON_RAW_ALLOC` globally for `lambda/` and `radiant/` builds
- Fix any remaining compile errors
- Verify all `<stdlib.h>` includes are removed from `lambda/` and `radiant/`
- Document exempt files (lib/ foundations)

---

## 11. Validation & CI

### 11.1 Test Mode

Run all tests with `MEMTRACK_MODE=DEBUG`:

```bash
MEMTRACK_MODE=DEBUG make test
```

This enables:
- Full allocation hashmap (every alloc is recorded)
- Guard byte verification on every free
- Leak report on shutdown

### 11.2 CI Gate

Add a CI step that runs baseline tests with memtrack debug and checks for zero leaks:

```bash
# In CI pipeline
MEMTRACK_MODE=DEBUG make test-lambda-baseline
MEMTRACK_MODE=DEBUG make test-radiant-baseline
# Both must exit 0 (zero leaks)
```

### 11.3 Compile-Time Check

Build with poison enabled to catch any new raw malloc calls:

```bash
make build EXTRA_CFLAGS="-DMEMTRACK_POISON_RAW_ALLOC"
# Must compile cleanly — any raw malloc is a compile error
```

### 11.4 Runtime Summary

At the end of every test run, log the memtrack summary:

```
memtrack: clean shutdown — 0 live allocations
memtrack: session stats:
  total allocs: 892,341
  total frees:  892,341
  peak usage:   67.2 MB (at allocation #445,123)
  categories:
    MEM_CAT_LAYOUT:    peak 18.3 MB, 234,567 allocs
    MEM_CAT_RENDER:    peak 12.1 MB, 89,012 allocs
    MEM_CAT_INPUT_CSS: peak 4.2 MB, 45,231 allocs
    MEM_CAT_JS_RUNTIME: peak 8.7 MB, 123,456 allocs
    ...
```

---

## Summary

| Metric | Current | Target |
|--------|---------|--------|
| Raw malloc/free calls in lambda/radiant | ~607 | 0 |
| Files with `#include <stdlib.h>` in lambda/radiant | 77 | 0 |
| memtrack adoption (lambda/) | 3 files | all files |
| memtrack adoption (radiant/) | 20 files | all files |
| Leak count at shutdown | unknown | 0 (verified) |
| STATS mode byte tracking | broken (monotonic) | correct (bidirectional) |
| CI leak detection | none | every test run |

**Total estimated effort**: 8-12 sessions across 6 phases.

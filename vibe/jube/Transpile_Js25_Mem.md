# Transpile_Js25: test262 Memory Profiling

## Instrumentation

Added per-test RSS tracking to the `js-test-batch` mode in `main.cpp`:
- `get_rss_bytes()` using `mach_task_info` (macOS) / `/proc/self/statm` (Linux)
- RSS sampled before and after each test execution
- Extended `BATCH_END` protocol: `BATCH_END <status> <elapsed_us> <rss_before> <rss_after>`
- Gtest runner parses and writes `temp/_t262_memory_o0.tsv` with per-test memory data
- Summary printed after Phase 2 with top-20 leakers, peak RSS, and growth stats

## Results (M-series Mac, 12 workers, -O0, 27,089 tests)

### Overall

| Metric | Value |
|--------|-------|
| Tests with memory data | 27,089 / 27,089 |
| Peak RSS (normal batch) | **893 MB** |
| Peak RSS (crash batch) | **5,851 MB** |
| Min RSS (batch startup) | 69 MB |
| Avg RSS delta per test | **+1 MB** |
| Tests > 1 MB growth | 3,762 (14%) |
| Tests > 10 MB growth | 1,063 (4%) |
| Tests > 100 MB growth | 18 |

### Per-Test Leak: ~1 MB/test Average

With `hot_reload` mode (default), the persistent heap grows steadily. `js_batch_reset()` clears JS variable/exception state but does not reclaim GC heap allocations. Only `heap_destroy()` + `heap_init()` (triggered by crash/timeout recovery) actually frees memory.

### Memory Growth Accelerates with Heap Size

| Batch RSS bracket | # Tests | Avg delta/test |
|-------------------|---------|----------------|
| 0–100 MB | 11,634 | +1.0 MB |
| 100–200 MB | 13,933 | +0.9 MB |
| 200–300 MB | 522 | +2.7 MB |
| 300–400 MB | 57 | +6.3 MB |
| 400–500 MB | 70 | +9.5 MB |
| 500–600 MB | 16 | +14.1 MB |
| 600–800 MB | 75 | +20.5 MB |
| 800–900 MB | 64 | +0.8 MB |

As the heap grows past ~200 MB, per-test allocation cost increases sharply — likely due to GC struggling with a large live-object set. Most batch workers stay in the 0–200 MB range (50 tests/batch), but a few slow batches accumulate into the 200–800 MB zone.

### Exit Code Distribution

| Exit Code | Count | Meaning |
|-----------|-------|---------|
| 0 (pass) | 10,280 | Normal pass |
| 1 (fail) | 16,038 | Test assertion failed |
| 137 (SIGKILL) | 770 | Killed by OS (OOM or timeout) |
| 139 (SIGSEGV) | 1 | Segfault |

### Crash Batch (770 tests at 5.85 GB)

All 770 crash tests (exit 137) share identical `rss_before=4,342,753 KB` / `rss_after=5,991,680 KB`. This is a single batch worker that spiraled after crash recovery — the heap was never effectively reclaimed, and subsequent tests ran in an increasingly bloated process.

### Top Memory Growth Tests

**Passing tests (exit 0):**

| Test | Delta |
|------|-------|
| `RegExp/character_class_escape/non_whitespace` | **+751 MB** |
| `Function/length/S15.3.5.1_A3_T1` | +110 MB |
| `Function/length/S15.3.5.1_A2_T1` | +108 MB |
| `Function/length/S15.3.5.1_A4_T1` | +108 MB |
| `expressions/array/spread/err_sngl_err_itr_value` | +65 MB |

**Failing tests (exit 1):**

| Test | Delta |
|------|-------|
| `decodeURI/S15.1.3.1_A2.5_T1` | +293 MB |
| `decodeURIComponent/S15.1.3.2_A2.5_T1` | +288 MB |
| `encodeURI/S15.1.3.3_A2.3_T1` | +140 MB |
| `decodeURIComponent/S15.1.3.2_A1.10_T1` | +137 MB |
| `for-of/body/dstr/assign_error` | +124 MB |

### RSS Decrease (Only 20 Tests)

Only 20 tests showed RSS decrease — all minor (max −26 MB from `Object.isFrozen`). This confirms `js_batch_reset()` does not trigger GC collection or heap shrinking. The only effective memory reclaim is the full `heap_destroy()` + `heap_init()` path after crash/timeout.

## Memory Lifecycle in Batch Mode

```
batch start:
    heap_init()              → 69 MB RSS (GC heap + pools)
    preamble_compile()       → ~85 MB RSS

per test (hot_reload=true):
    transpile + execute      → RSS grows by ~1 MB (avg)
    js_batch_reset_to()      → clears JS vars/exceptions (NO heap reclaim)
    GC may run               → marks/sweeps but pool memory stays committed

after crash/timeout:
    heap_destroy()           → RSS drops back to ~69 MB
    heap_init()              → fresh heap
    preamble recompile       → ~85 MB

batch end:
    heap_destroy()           → process exits
```

## Conclusions

1. **~1 MB/test steady leak** is the dominant pattern. Over 50 tests/batch this means each batch worker reaches ~120 MB. Acceptable.
2. **Outlier tests** (RegExp non-whitespace, URI encode/decode, Function.length) allocate 100–750 MB each. These should be batched carefully or given memory limits.
3. **Crash recovery is the only memory reclaim** — `js_batch_reset()` needs a GC collect call or pool reset to prevent heap bloat.
4. **The 5.85 GB crash batch** is a single worker that never recovered. The 770 tests in it are all crash-signal tests (quarantined crashers).
5. **No memory stress for normal batches** — peak 893 MB across 12 workers is fine for 16 GB+ machines.

---

## Reference: Radiant Batch Memory Fix

See [`vibe/radiant/Radiant_Mem_Mgt_Fix.md`](radiant/Radiant_Mem_Mgt_Fix.md) — the Radiant layout engine had the same problem when processing hundreds of HTML/JS files sequentially (RSS reached ~970 MB at 1000 files). The fix established a pattern that applies directly to test262:

### Radiant Per-File Lifecycle (working solution)

```
per file:
    pool = pool_create_mmap()               // fresh mmap pool
    runtime.reuse_pool = pool
    transpile_js_to_mir(&runtime, ...)       // heap_init_with_pool(pool)
    // ... execute ...

    // extract pool from gc_heap, destroy gc metadata
    pool = runtime.heap->gc->pool
    runtime.heap->gc->pool = NULL
    gc_heap_destroy(runtime.heap->gc)
    free(runtime.heap)
    gc_nursery_destroy(runtime.nursery)

    // reset JS state BEFORE freeing pool (ordering critical!)
    js_batch_reset()
    js_dom_batch_reset()
    js_globals_batch_reset()                // ← not called in test262!

    // now safe to free pool (no dangling refs)
    pool_destroy(pool)                      // munmap all chunks → RSS drops
```

Key lessons from Radiant:
1. **`js_globals_batch_reset()`** clears `js_global_this_obj`, constructor cache, `process.argv` — test262 batch never calls this.
2. **`pool_create_mmap()`** bypasses rpmalloc entirely — clean `munmap` on destroy, no fragmentation.
3. **Reset ordering**: all JS globals cleared before pool destroy to avoid use-after-free.
4. **GC root re-registration**: `static bool statics_rooted` guard must be removed so new heaps register roots.

---

## Proposed Fix: Per-Test Heap Cycling in test262 Batch Mode

### Diagnosis: Why Memory Leaks

In `hot_reload=true` mode (default for test262 batch), the GC heap persists across all ~50 tests in a batch. Between tests, only `js_batch_reset_to()` runs (with preamble) or `js_batch_reset()` (without). These reset JS variable state but:

1. **No GC collect** is triggered between tests — dead objects from the previous test survive until next allocation-triggered GC.
2. **No pool reclaim** — the rpmalloc pool grows monotonically. Even after GC sweeps, freed memory stays committed in rpmalloc's span cache.
3. **`js_globals_batch_reset()` not called** — `js_global_this_obj`, constructor cache, `process.argv` accumulate stale heap pointers.
4. **`js_batch_reset_to()`** (preamble path) is even more limited — doesn't reset module registry, module cache, console/reflect caches.

### Proposed Approach: Periodic Heap Reset

Full per-test heap destroy+recreate is too expensive (must recompile preamble, ~2ms). Instead:

#### Option A: GC Collect + Globals Reset Between Tests (Low Risk)

Add to the normal-test cleanup path in `main.cpp` (after `js_batch_reset_to()`):

```cpp
// After each test (hot_reload path, no crash):
if (has_preamble) {
    js_batch_reset_to(preamble_var_checkpoint);
} else {
    js_batch_reset();
}
js_globals_batch_reset();     // NEW: clear global this, ctor cache, process.argv
heap_gc_collect();            // NEW: force GC — sweep dead objects from previous test
```

**Expected impact**: Reduces live-object set between tests. GC can reclaim dead objects before the next test allocates more. Won't reduce RSS (pool memory stays committed) but will slow the growth rate by reducing GC pressure from a large live set.

**Risk**: Low. `js_globals_batch_reset()` is already used in Radiant. `heap_gc_collect()` is safe to call between tests.

#### Option B: Periodic Heap Destroy + Recreate (Medium Risk)

Every N tests (e.g., N=10), do a full heap cycle:

```cpp
// Every 10 tests within a batch:
if (test_count_in_batch % 10 == 0) {
    js_batch_reset();
    js_globals_batch_reset();
    heap_destroy();
    gc_nursery_destroy(batch_context.nursery);

    // Recreate heap
    batch_context.nursery = gc_nursery_create(0);
    heap_init();
    batch_context.pool = batch_context.heap->pool;
    batch_context.name_pool = name_pool_create(batch_context.pool, nullptr);
    batch_context.type_list = arraylist_new(64);

    // Recompile preamble into new heap
    if (saved_harness_src) {
        preamble_state_destroy(&preamble);
        memset(&preamble, 0, sizeof(preamble));
        Item pres = transpile_js_to_mir_preamble(&runtime, saved_harness_src, "<harness>", &preamble);
        has_preamble = (pres.item != ITEM_ERROR);
        if (has_preamble) preamble_var_checkpoint = preamble.module_var_count;
    }
}
```

**Expected impact**: Caps per-worker RSS growth. Every 10 tests, memory drops back to ~85 MB. Peak RSS per worker stays under ~100 MB instead of growing to 893 MB.

**Risk**: Medium. Preamble recompile adds ~2ms × (50/10) = ~10ms per batch. With 540 batches / 12 workers, that's ~0.5s total overhead (~1% of 48s). Preamble MIR context lifecycle must be handled carefully.

#### Option C: mmap Pool per Test (Radiant Pattern, Higher Effort)

Use `pool_create_mmap()` per test, extract and destroy pool after each test:

```cpp
// Before test:
runtime.reuse_pool = pool_create_mmap();

// After test:
js_batch_reset_to(preamble_var_checkpoint);
js_globals_batch_reset();
// extract pool, destroy gc metadata, pool_destroy() → munmap
```

**Expected impact**: Near-zero RSS growth per test. Each test's allocations are completely freed via `munmap`.

**Risk**: Higher. Requires decoupling the persistent preamble heap from per-test heap. Preamble objects live in the batch heap, but test objects would live in a per-test mmap pool — two separate allocation domains. The GC would need to handle cross-pool references or the preamble must be allocated separately.

### Recommended Order

1. **Option A first** (GC collect + globals reset) — minimal code change, low risk, quantify improvement.
2. **Option B if needed** (periodic heap reset every N tests) — caps growth, small perf overhead.
3. **Option C only if** profiling shows rpmalloc fragmentation is the dominant leak source.

---

## Benchmark Results: Option A vs Option B

Both options implemented and benchmarked (M-series Mac, 12 workers, -O0, 27,089 tests, 3 runs each).

### Option A: `js_globals_batch_reset()` + `heap_gc_collect()` After Each Test

Added after every `js_batch_reset_to()` / `js_batch_reset()` in the normal cleanup path.

| Run | Phase 2 | Peak RSS | Avg delta/test | Regressions | batch-lost |
|-----|---------|----------|----------------|-------------|------------|
| 1 | 49.7s | 5842.8 MB | +47,415 KB | — | 0 |
| 2 | 52.7s | 5811.1 MB | +45,739 KB | — | 0 |
| 3 | 52.0s | — | — | 227 | 0 |
| **Avg** | **~51.5s** | **~5840 MB** | **~47,400 KB** | — | — |

### Option B: Full Heap Destroy + Recreate Every 10 Tests

Every 10 tests: `js_batch_reset()` → `js_globals_batch_reset()` → `heap_destroy()` → `gc_nursery_destroy()` → `heap_init()` → preamble recompile. Between resets: `js_globals_batch_reset()` only.

| Run | Phase 2 | Peak RSS | Avg delta/test | Regressions | batch-lost |
|-----|---------|----------|----------------|-------------|------------|
| 1 | 92.2s | 5851.1 MB | +48,097 KB | — | 2,968 |
| 2 | 63.2s | 5885.9 MB | +50,160 KB | — | 0 |
| 3 | 63.0s | 5810.6 MB | +45,749 KB | 184 | 0 |
| **Avg** | **~63s** | **~5850 MB** | **~48,000 KB** | — | — |

Option B Min RSS observed: **86.4 MB** (confirms heap reset does reclaim memory).

### Comparison vs Baseline

| Metric | Baseline | Option A | Option B (N=10) |
|--------|----------|----------|------------------|
| Phase 2 time | ~48s | ~51.5s (+7%) | ~63s (+31%) |
| Peak RSS | ~5850 MB | ~5840 MB | ~5850 MB |
| Avg delta/test | ~48,000 KB | ~47,400 KB | ~48,000 KB |
| Regressions | — | 227 | 184 |
| batch-lost | 0 | 0 | 2,968 (run 1) |

### Conclusions

1. **Neither option reduces peak RSS** — the dominant memory growth is not in the GC heap. Likely sources: MIR compiler contexts, name pool allocations, rpmalloc internal fragmentation.
2. **Option A adds ~7% runtime overhead** from `heap_gc_collect()` on every test, with no memory benefit.
3. **Option B adds ~31% overhead** from preamble recompilation every 10 tests, plus instability (2,968 batch-lost on first run). Min RSS of 86 MB proves heap reset works, but RSS climbs right back.
4. **Both options reverted to baseline.** The leak is outside GC-managed memory — future investigation should focus on MIR context lifecycle and rpmalloc pool fragmentation.

---

## Fix: Enhanced `js_batch_reset_to()` Cleanup

### Problem

`js_batch_reset_to()` (used by preamble mode in test262 batch) was missing 7 cleanup calls that `js_batch_reset()` performs. These left stale state between tests:

| Cleanup call | What it clears | Memory impact |
|---|---|---|
| `module_registry_cleanup()` | `strdup` + `calloc` per registered module | Real malloc leak per import-using test |
| `js_module_cache_reset()` | Module specifier counter | Prevents stale module lookups |
| `js_reset_json_object()` | Cached JSON global | Null one pointer |
| `js_reset_console_object()` | Cached console global | Null one pointer |
| `js_reset_reflect_object()` | Cached Reflect global | Null one pointer |
| `js_reset_proto_key()` | Interned `__proto__` key | Null one pointer |
| `js_func_cache_reset()` | func_ptr → JsFunction cache | Zero one counter |
| `js_deep_batch_reset()` | Generators, promises, async contexts | Zero static arrays |

### Change

Added all missing cleanup calls to `js_batch_reset_to()` in `lambda/js/js_runtime.cpp`. Now `js_batch_reset_to()` matches `js_batch_reset()` except for the module var checkpoint logic (which is intentional — preserves preamble harness vars).

### Results (3 runs, same config as above)

| Metric | Baseline | Enhanced cleanup |
|--------|----------|------------------|
| Phase 2 time | 45.4s | ~45.6s (avg of 46.6, 44.2, 46.1) |
| Peak RSS | 5812 MB | ~5846 MB |
| Passed | 13278 | **13331 (+53)** |
| Regressions | 211 | **184 (-27)** |
| batch-lost | 0 | 0 |

**Conclusion**: Zero performance overhead. Improved test isolation gives +53 passing tests and -27 regressions. The stale generators/promises/module state from previous tests was causing spurious failures.

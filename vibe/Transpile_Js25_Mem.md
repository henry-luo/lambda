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

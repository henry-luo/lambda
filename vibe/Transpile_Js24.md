# Transpile_Js24: test262 Phase 2 Runtime Reduction

## Current State

Phase 2 stable at **~74s** for 27,089 tests (8 workers, batch=50, slowest-first dispatch, timeout quarantine). All Js23 optimizations complete and closed.

### What Was Tried and Didn't Help (Tier 1 from Js23 Analysis)

| Strategy | Result | Why it failed |
|----------|--------|---------------|
| Persistent worker processes | No improvement | posix_spawn overhead (~1.5s total) is negligible vs 74s. Harness compile per batch is fast. Load balancing already good with slowest-first dispatch. |
| MIR binary cache | No improvement | `MIR_read()` + `MIR_link()` + `MIR_gen()` still required per test — link+gen dominates, not parse+transpile. Cache I/O overhead offsets transpile savings. |
| Thread-based workers | No improvement | Pipe I/O is not the bottleneck. Process-level isolation is needed for crash safety (SIGSEGV kills thread = kills process). |

**Conclusion**: The ~74s floor is dominated by **MIR link + native code generation** across 27K one-shot scripts, not by runner infrastructure overhead.

---

## Stage 1: Increase Worker Count

**Current**: `T262_MAX_PARALLEL_BATCHES = 8` in `test/test_js_test262_gtest.cpp`.

On machines with >8 cores (e.g., M2 Pro 12-core, M3 Max 16-core), 4-8 cores sit idle. Even on 8-core machines, worker utilization is <100% due to variable test cost and I/O gaps between batch dispatches.

### Implementation

Changed `T262_MAX_PARALLEL_BATCHES` from 8 to 12.

### Results (M-series Mac, April 2026)

| Workers | Run 1 | Run 2 | Phase 2 avg | vs 8 workers |
|---------|-------|-------|-------------|--------------|
| 8 | 73.9s | 74.0s | **~74s** | baseline |
| 12 | 48.7s | 48.0s | **~48s** | **−35%** |
| 16 | 49.2s | 48.0s | **~48s** | −35% (no further gain) |

**Decision**: 12 workers. Going from 12→16 yields zero improvement — the machine saturates at 12 (memory bandwidth / thermal limits). 16 workers would only waste ~800MB additional memory for no throughput gain.

### Status: ✅ Done

`T262_MAX_PARALLEL_BATCHES = 12` committed. Phase 2 reduced from ~74s to ~48s.

---

## Stage 2: MIR Context Reuse Within a Worker

**Current**: Each test gets a full `MIR_init()` → `MIR_gen_init()` → transpile → link → gen → execute → `MIR_gen_finish()` → `MIR_finish()` cycle (~1.3ms overhead per test).

### Implementation (Attempted — 4 iterations)

**v1 — Graveyard pattern**: Create `reuse_ctx` at batch startup, `graveyard_ctx` for old modules. After each test, move modules via `MIR_change_module_ctx()` to graveyard.
- **Result**: 25,535 batch-lost (massive crash)
- **Root cause**: `MIR_change_module_ctx()` calls fatal error if `item->addr != NULL` (i.e., after `MIR_link`). Loaded/linked modules cannot be moved between contexts. (`mac-deps/mir/mir.c` line 2851)

**v2 — No graveyard, reverse find_func**: Let modules accumulate in reuse ctx. Changed `find_func()` in `mir.c` to search from DLIST_TAIL (newest module first) so the latest `js_main` is always found.
- **Result**: 61.6s, batch-lost: 0, but 459 regressions
- **Root cause**: After MIR error recovery via `longjmp`, the reuse ctx is left in a corrupted state. Subsequent tests compiled into the corrupted ctx produce wrong results.

**v3 — Dirty flag + ctx recreation**: Added `reuse_ctx_dirty` flag set on any `longjmp` (crash/timeout/MIR error). After each test, if dirty, call `jit_cleanup()` on corrupted ctx and create a fresh one.
- **Result**: 47.8s, 1,062 batch-lost, 233 regressions
- **Root cause**: `jit_cleanup()` crashes on corrupted ctx (MIR_gen internal state left inconsistent by `longjmp`)

**v4 — Leak corrupted ctx**: Instead of calling `jit_cleanup()` on corrupted ctx, simply leak it and create a new one.
- **Result**: 59.0s, batch-lost: 0, 229 regressions — **23% SLOWER** than baseline (~48s)

### Key Technical Findings

- **`MIR_change_module_ctx()` FAILS on linked modules**: Once `MIR_link()` runs, `item->addr` is set. Any attempt to move the module to another context triggers `MIR_ctx_change_error` → fatal exit. The graveyard pattern is impossible.
- **`MIR_link()` is incremental**: Only processes `modules_to_link` VARR (populated by `MIR_load_module`), not all modules in the context. Module accumulation doesn't slow down linking.
- **`find_func()` searches linearly**: Scans all modules' items. With modules accumulating, this grows linearly. Reversing to TAIL-first search mitigates stale matches but not the scan cost.
- **`MIR_finish()` is the ONLY way to reclaim internal state**: It calls `string_finish()`, `simplify_finish()`, `code_finish()`, `remove_all_modules()`, and destroys all hash tables/VARRs (`mac-deps/mir/mir.c` lines 888–930). Without calling it, string tables, code pages, item hash tables, and simplify state grow without bound.
- **MIR context is not designed for long-lived reuse**: The ~1ms savings from skipping `jit_init()`/`MIR_finish()` is overwhelmed by the growing overhead of accumulating internal state (strings, code pages, hash tables).

### Results (M-series Mac, 12 workers)

| Version | Phase 2 | batch-lost | Regressions | Notes |
|---------|---------|------------|-------------|-------|
| Baseline (no reuse) | 48–50s | 0 | ~200 | Normal per-test init/finish |
| v1 (graveyard) | — | 25,535 | — | `MIR_change_module_ctx` fatal on linked modules |
| v2 (accumulate + reverse find) | 61.6s | 0 | 459 | Corrupted ctx after longjmp |
| v3 (dirty flag + cleanup) | 47.8s | 1,062 | 233 | `jit_cleanup` crashes on corrupted ctx |
| v4 (leak corrupted) | 59.0s | 0 | 229 | 23% slower — unbounded state growth |

### Conclusion

**❌ Abandoned.** MIR's internal data structures (string tables, code pages, hash tables, simplify state) grow without bound when `MIR_finish()` is not called between tests. There is no API to selectively clean up a module's resources or reset internal state. The ~1ms/test savings from skipping context init/teardown is completely erased by the overhead of growing internal state. Code reverted.

### Status: ❌ Infeasible (MIR context not designed for long-lived reuse)

---

## Stage 3: Preamble Binary Cache

**Current**: Each batch process transpiles the harness (sta.js + assert.js, ~12KB) from source on startup. With ~440 batches (12 workers), this is 440 redundant compilations.

### Implementation (Attempted)

Implemented full preamble binary cache:
1. On first batch: transpile harness → `MIR_write()` to `temp/_t262_preamble.cache` (20KB)
2. Subsequent batches: `MIR_read()` + `MIR_load_module()` + `MIR_link()` instead of full transpile
3. Cache format: magic header + FNV-1a hash of harness source + module_var_count + entries array + MIR binary
4. Atomic rename for race-safe writes from parallel workers
5. `setjmp`/`longjmp` MIR error recovery around cache load (MIR errors default to `exit(1)`)

### Key Technical Findings

- **MIR_read only caches IR, not native code**: `MIR_link()` + `MIR_gen()` (the expensive -O3 JIT compilation) still runs for every cache load. The cache only skips source → MIR IR transpilation.
- **MIR error handler must be installed before cache load**: `batch_mir_error_handler` was only set during per-test execution. Any MIR error during `MIR_read`/`MIR_load_module`/`MIR_link` called the default handler → `exit(1)` → killed entire batch process. Required a dedicated `pcache_mir_error_handler` with `setjmp`/`longjmp` recovery.
- **`MIR_load_module()` IS required after `MIR_read()`**: Contrary to initial assumption, modules from `MIR_read` need explicit loading before `MIR_link`.

### Results (M-series Mac, 12 workers)

| Config | Phase 2 | batch-lost | Notes |
|--------|---------|------------|-------|
| No cache (baseline) | 46–49s | 0 | Normal operation |
| Cold cache (first run, saves) | 49.5s | 0 | +0.5s overhead from writing cache |
| Warm cache (all loads) | 54.3s | 0 | **+5s SLOWER** — MIR binary parse + re-JIT > source transpile |

### Conclusion

**❌ Abandoned.** The MIR binary format preserves IR, not generated native code, so `MIR_link()`+`MIR_gen()` at -O3 still runs on every load — the most expensive step. File I/O + binary parsing adds more overhead than it saves from skipping the cheap source→IR transpilation of a small harness. Code reverted.

### Status: ❌ Infeasible (MIR binary cache does not cache native code)

---

## Stage 4: Profile Per-Test Breakdown

Before implementing further optimizations, instrument the per-test pipeline to identify where time is actually spent.

### Implementation

Add timing points in `js-test-batch` main loop (in `lambda/main.cpp`):

```
For each test:
    t0 = now()
    parse()              → t1 (parse time)
    build_ast()          → t2 (AST time)
    early_errors()       → t3
    jit_init()           → t4 (MIR init time)
    transpile_mir()      → t5 (transpile time)
    MIR_link()           → t6 (link time)
    find_func + execute  → t7 (execution time)
    jit_cleanup()        → t8 (cleanup time)
```

Output via BATCH_END protocol extension:
```
\x01BATCH_END <exit_code> <total_us> parse=<us> ast=<us> transpile=<us> link=<us> exec=<us> cleanup=<us>\n
```

### Analysis

Aggregate the timing data across all 27K tests to determine:
- **Distribution**: What fraction of time is parse vs transpile vs link vs gen vs exec?
- **Long tail**: Which tests have disproportionate transpile or link time?
- **Correlation**: Does MIR item count correlate with link time?

This data determines which subsequent optimizations have ROI.

### Risk

None. Read-only instrumentation.

---

## Stage 5: Adaptive Micro-Batches

**Current**: Fixed 50-test batches. Even with slowest-first dispatch, the last few batches define the tail latency.

### Implementation

Replace fixed batch size with adaptive dispatch:
- Start with batch=50 for bulk throughput
- When <200 tests remain, switch to batch=10
- When <50 tests remain, switch to batch=1

This ensures no straggler batch has more than a few seconds of work.

### Expected Impact

Depends on current tail latency. If the slowest remaining batch takes 9.3s (current estimate), micro-batching the tail could recover 3-5s.

### Risk

Low. More batches = more posix_spawn calls, but only for the tail (30-40 extra spawns, <0.5s).

---

## Stage 6: Skip Known-Stable Tests (Dev Iteration Mode)

For development iteration (not CI), skip tests that have passed consistently across the last N runs.

### Implementation

Maintain `temp/_t262_stable.tsv` tracking per-test pass streak. A test is "stable" if it passed in the last 5 consecutive runs.

New flag: `--fast` or `--changed-only`:
- Only run tests that failed in the last run, or are in recently-changed test262 categories
- Run stable tests with probability 10% (random sampling for regression detection)

### Expected Impact

If 80% of tests are stable, Phase 2 drops to ~15s for the remaining 20% + sampling overhead.

**Not suitable for CI** — must run full suite for release validation.

### Risk

Low. Purely additive (new CLI flag), doesn't affect default behavior.

---

## Stage 7: Parallel Parse + Transpile Pipeline

**Current**: Each test is processed serially within a worker: parse → AST → transpile → link → exec → cleanup.

### Implementation

Within each worker process, use 2 threads:
- **Thread A (prep)**: Parse + AST + transpile the *next* test while Thread B executes the *current* test
- **Thread B (exec)**: MIR link + execute

This overlaps CPU-bound transpilation with CPU-bound execution, potentially improving single-worker throughput by up to 2×.

### Feasibility Concerns

- Tree-sitter parser is not thread-safe (uses static state) — would need one parser per thread
- MIR context is thread-local — transpile and link/exec must use separate contexts, then transfer module via `MIR_change_module_ctx`
- Preamble state sharing between threads adds complexity

### Expected Impact

Theoretical: up to 2× per-worker throughput → Phase 2 halved.
Practical: likely 20-30% improvement due to synchronization overhead and uneven parse/exec time ratios.

### Risk

High. Complex thread coordination, parser thread-safety concerns, potential for subtle race conditions.

---

## Stage 8: Multi-Thread Instead of Multi-Process

**Idea**: Replace the current multi-process worker model (`fork`/`popen` per batch) with a multi-threaded model. Threads could share a single compiled preamble (harness) in memory, eliminating redundant preamble compilation across workers.

### Why It Won't Work

The runtime has **42+ global/static mutable variables** that are not thread-safe:

| Category | Examples | Impact |
|----------|----------|--------|
| **JS execution state** | `js_current_this`, `js_new_target`, `js_pending_call_args`, `js_module_vars[4096]` | Threads corrupt each other's function calls and variable state |
| **Exception handling** | `js_exception_pending`, `js_exception_msg_buf[1024]` | One thread's exception overwrites another's |
| **Signal-based recovery** | `batch_timeout_jmp`, `mir_error_jmp`, `batch_crash_jmp` (static `jmp_buf`) | Threads jump to wrong recovery points; `SIGALRM`/`SIGSEGV` handlers are per-process |
| **MIR compiler state** | `g_batch_mir_error_handler`, `g_jm_preamble_mode`, `g_jm_preamble_in/out` | Concurrent compilations corrupt shared state |
| **Caches & registries** | `js_func_cache_keys[512]`, `js_builtin_cache[]`, `ascii_char_table[128]` | Unsynchronized concurrent access causes data races |

Even with a shared preamble, the compiled native functions call back into global JS runtime state (`js_module_vars`, `js_current_this`), so threads would corrupt each other during execution.

**Estimated refactoring effort**: 3–5 days to move all JS runtime state into per-thread `EvalContext`, replace signal-based recovery with thread-local mechanisms, and add synchronization to shared caches. High risk of subtle concurrency bugs.

### Status: ⏭️ Skipped (architecture incompatible, effort/risk too high)

---

## Implementation Priority

| Stage | Description | Impact | Risk | Effort | Priority |
|-------|-------------|--------|------|--------|----------|
| **4** | Profile per-test breakdown | Enables all others | None | Low | **Do first** |
| **1** | Increase worker count | 8-46% (core-dependent) | Low | Trivial | **Quick win** |
| **5** | Adaptive micro-batches | 3-5s | Low | Low | Quick win |
| **2** | MIR context reuse | ~5% | Medium | Medium | After profiling |
| **6** | Skip stable tests (dev mode) | ~80% for dev iteration | Low | Medium | Quality-of-life |
| **3** | Preamble binary cache | <1% | Low | Low | Skip unless persistent workers added |
| **7** | Parallel parse pipeline | 20-30% | High | High | Only if profiling shows parse is significant |
| **8** | Multi-thread workers | Shared preamble | High | High | Skipped — 42+ globals not thread-safe |

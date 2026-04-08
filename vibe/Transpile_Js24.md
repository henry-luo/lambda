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

**Current**: Each test gets a full `MIR_init()` → `MIR_gen_init()` → transpile → link → gen → execute → `MIR_gen_finish()` → `MIR_finish()` cycle.

MIR has no `MIR_remove_module()` API, but `MIR_change_module_ctx(old_ctx, module, graveyard_ctx)` can transfer completed modules to a throwaway context, keeping the active context lean.

### Implementation

```
Per worker process (once):
    active_ctx = MIR_init()
    MIR_gen_init(active_ctx)
    graveyard_ctx = MIR_init()     // sink for dead modules

Per test:
    MIR_new_module(active_ctx, "js_script")
    transpile → MIR_finish_module → MIR_load_module
    MIR_link(active_ctx, ...)       // links only active modules
    find_func → execute
    MIR_change_module_ctx(active_ctx, test_module, graveyard_ctx)  // move to graveyard

Periodically (every N tests):
    MIR_finish(graveyard_ctx)       // bulk free dead modules
    graveyard_ctx = MIR_init()      // fresh graveyard
```

### Expected Savings Per Test

| Operation | Current (per test) | With reuse |
|-----------|-------------------|------------|
| `MIR_init()` | ~0.3ms | 0 (amortized) |
| `MIR_gen_init()` | ~0.5ms | 0 (amortized) |
| `MIR_gen_finish()` | ~0.2ms | 0 (amortized) |
| `MIR_finish()` | ~0.3ms | 0 (periodic bulk) |
| **Total saved** | **~1.3ms/test** | |

Over 27K tests: ~35s of core-time saved → with 8 workers: **~4s wall-time** (~5% improvement).

### Risk

Medium. Must verify:
- `MIR_link()` doesn't re-process graveyard modules (it shouldn't — they've been moved out)
- Generator state remains valid across module boundaries
- Import resolution still works after module transfer
- Memory doesn't leak (graveyard periodic flush)

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

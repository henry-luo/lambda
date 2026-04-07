# JavaScript Transpiler v22: Performance Bottleneck Analysis and Optimization Proposal

## 1. Executive Summary

Per-test timing instrumentation was added to the `js-test-batch` BATCH_END protocol (microsecond resolution via `gettimeofday()`). A full timing sweep of 52,534 test262 tests (8 parallel workers, 278s wall time) reveals that **650 tests (1.2%) consume 37.9% of total execution time**, and the median test takes only 7.9ms. The biggest performance lever is **reducing per-test compilation overhead** — the MIR JIT at optimization level 2 dominates cost for 99% of tests that are individually simple.

### Timing Profile (52,415 non-timeout tests, 854s total)

| Tier | Tests | Time | % |
|------|-------|------|---|
| >5s | 15 | 125.1s | 14.6% |
| 1–5s | 23 | 48.3s | 5.7% |
| 500ms–1s | 32 | 21.0s | 2.5% |
| 100–500ms | 580 | 129.6s | 15.2% |
| 50–100ms | 534 | 36.9s | 4.3% |
| 10–50ms | 16,856 | 280.0s | 32.8% |
| <10ms | 34,375 | 213.2s | 25.0% |

| Statistic | Value |
|-----------|-------|
| Mean | 16.3ms |
| Median | 7.9ms |
| p90 | 19.1ms |
| p95 | 27.3ms |
| p99 | 137.0ms |

### Current Baseline (v22 — post Stage 2 fix)

| Metric | Value |
|--------|-------|
| Phase 2 (clean batch) | 91.7s |
| Passing tests | 11,554 (42.8%) |
| Quarantined crashers | 201 |
| Improvements from v21 baseline | +196 |

Note: The v21 baseline (12,296 passing) was from a snapshot before several commits were merged
(two-module MIR split, numeric types support). The 938 tests that "regressed" against the v21
baseline are from committed code changes, not from the Stage 2 sparse array fix. The Stage 2 fix
contributed ~196 improvements (30 crasher recoveries + 166 other). The v22 baseline is the first
accurate baseline for the current codebase at HEAD.

## 2. Top 20 Slowest Tests Analysis

### Raw Data

| Rank | Time | Status | Test |
|------|------|--------|------|
| 1 | 9809ms | CRASH | `built-ins/Array/prototype/lastIndexOf/15.4.4.15-5-16.js` |
| 2 | 9603ms | CRASH | `built-ins/Object/defineProperty/15.2.3.6-4-184.js` |
| 3 | 9512ms | CRASH | `built-ins/Object/defineProperty/15.2.3.6-4-183.js` |
| 4 | 9388ms | CRASH | `built-ins/Array/prototype/slice/length-exceeding-integer-limit-proxied-array.js` |
| 5 | 9100ms | CRASH | `built-ins/Object/defineProperty/15.2.3.6-4-185.js` |
| 6 | 8596ms | CRASH | `built-ins/Array/prototype/indexOf/15.4.4.14-5-16.js` |
| 7 | 8552ms | FAIL | `built-ins/Map/valid-keys.js` |
| 8 | 8453ms | CRASH | `built-ins/Array/S15.4_A1.1_T10.js` |
| 9 | 8294ms | CRASH | `built-ins/Array/prototype/indexOf/15.4.4.14-5-12.js` |
| 10 | 8272ms | CRASH | `built-ins/Array/prototype/lastIndexOf/15.4.4.15-5-12.js` |
| 11 | 8114ms | CRASH | `built-ins/Array/property-cast-nan-infinity.js` |
| 12 | 7921ms | CRASH | `staging/sm/Array/length-truncate-with-indexed.js` |
| 13 | 7426ms | PASS | `built-ins/RegExp/character-class-escape-non-whitespace.js` |
| 14 | 7055ms | FAIL | `staging/sm/String/string-code-point-upper-lower-mapping.js` |
| 15 | 5003ms | FAIL | `language/literals/regexp/S7.8.5_A1.1_T2.js` |
| 16 | 4606ms | FAIL | `intl402/DateTimeFormat/prototype/formatToParts/compare-to-temporal.js` |
| 17 | 4548ms | FAIL | `language/literals/regexp/S7.8.5_A2.1_T2.js` |
| 18 | 3757ms | FAIL | `built-ins/encodeURIComponent/S15.1.3.4_A2.3_T1.js` |
| 19 | 3583ms | FAIL | `built-ins/encodeURI/S15.1.3.3_A2.3_T1.js` |
| 20 | 3541ms | FAIL | `annexB/built-ins/RegExp/RegExp-leading-escape-BMP.js` |

### Pattern Categories

**Category A — Huge Sparse Array Crashes (12 tests, 8–10s each)**

Tests assign to indices like `arr[Math.pow(2,32)-2]` or `arr[987654321]`. Runtime `js_array_set_int()` fills gaps linearly:
```cpp
// js_runtime.cpp line ~2800
while (arr->length < (int)index) {
    array_push(arr, undef);   // O(index) operations!
}
```
For `arr[987654321]`, this pushes ~987M undefined items (7.4GB memory). For `arr[4294967294]`, `(int)` cast overflows to `-2` — the loop skips but the array is corrupted. Both crash with SIGSEGV (exit=139).

**No sparse array representation exists.** Arrays are always dense.

**Category B — eval() / Dynamic Regex in Loops (4 tests, 3–5s each)**

Tests like `S7.8.5_A1.1_T2.js` iterate 65,535 times calling `eval("/" + xx + "/")`. `eval()` is not implemented — falls through to `js_call_function("eval")` which errors on each iteration. Even so, the error + exception object creation + loop overhead takes 5s for 65k iterations.

The `RegExp-leading-escape-BMP.js` and `RegExp-trailing-escape-BMP.js` tests similarly create 65k regex patterns in a loop via `eval()`.

**Category C — Unicode Full-Range Iteration (4 tests, 3–7.5s each)**

- `character-class-escape-non-whitespace.js` (7.4s, PASS): 65k iterations of `String.fromCharCode(j) + str.replace(/\S+/g, ...)`
- `encodeURIComponent/S15.1.3.4_A2.3_T1.js` (3.7s, FAIL): Triple-nested loop over UTF-8 byte ranges (~49k iterations), each calling `encodeURIComponent`
- `decodeURI/S15.1.3.1_A2.4_T1.js` (1.4s, FAIL): Similar triple-nested loop for 3-byte UTF-8 sequences

These are legitimately expensive tests — 50k–65k runtime function calls in a tight loop.

**Category D — Huge-Length Boundary Tests (5 tests, 1–2s each)**

Tests use `{length: Infinity}` or `{length: 2**53-1}` with `Array.prototype.indexOf.call(...)`. Runtime methods iterate from 0 to `Number.MAX_SAFE_INTEGER`, hitting the 10s timeout or running for seconds.

## 3. Optimization Proposals

### Stage 1: Adaptive MIR Optimization Level — ✅ DONE (use -O0)

**Problem:** Every test uses `jit_init(2)` — MIR optimization level 2 includes SSA, GVN/CCP, copy propagation, DSE, DCE, LICM, pressure relief, and coalescing. For test262 tests (mostly 10–30 lines), the optimization overhead exceeds execution benefit.

**MIR Optimization Levels (from `mir-gen.c`):**

| Level | Pipeline |
|-------|----------|
| 0 | Fast gen: register allocator + machine code generator only |
| 1 | + combiner (code selection) |
| 2 | + SSA, GVN/CCP, copy propagation, DSE, DCE, LICM, pressure relief, coalescing (**old default**) |
| ≥3 | Same as level 2 (no additional passes gated by `>=3`) |

**A/B Profiling Results (26,729 valid non-timeout/non-crash tests, three-way comparison):**

| Level | Sum of per-test µs | vs -O2 | Phase 2 wall clock | Total wall clock (all phases) |
|-------|-------------------|--------|-------------------|-------------------------------|
| **-O0** | **341.6s** | **+8.7% faster** | **70.5–71.7s** | **~95s** |
| -O2 | 374.0s | baseline | 72.4s | ~97s |
| -O3 | 385.9s | 3.2% slower | 76.3s | ~78s* |

\* O3 Phase 2b retry was anomalously fast (0.3s vs ~23s) due to fewer recoveries; typical total ~100s.

Note: "Sum of per-test µs" is the sum of all individual `elapsed_us` values reported by `BATCH_END` — useful for comparing optimization levels apples-to-apples but always larger than wall clock because it includes per-test overhead that overlaps with batch IPC. Phase 2 wall clock is the actual elapsed time for the main batch execution loop.

**Per-test winner count:**

| Level | Fastest count | % |
|-------|--------------|---|
| O0 | 10,864 | 40.6% |
| O2 | 7,891 | 29.5% |
| O3 | 7,974 | 29.8% |

**Ideal adaptive (oracle, pick best per test):**

| Strategy | Total | vs O2 |
|----------|-------|-------|
| Best of O0,O2 | 288.5s | +22.9% |
| Best of O0,O3 | 289.3s | +22.6% |
| Best of O0,O2,O3 | 269.6s | +27.9% |

**Source size analysis — O0 wins across nearly all file sizes:**

| Size bucket | Count | Avg O0 | Avg O2 | Avg O3 | O0/O2 | O0 wins% |
|-------------|-------|--------|--------|--------|-------|----------|
| <200B | 33 | 5.9ms | 5.4ms | 4.8ms | 1.09 | 39% |
| 200–500B | 3,877 | 8.5ms | 8.9ms | 9.2ms | 0.96 | 53% |
| 500B–1KB | 10,494 | 10.9ms | 12.1ms | 12.6ms | 0.90 | 55% |
| 1–2KB | 7,231 | 15.9ms | 17.4ms | 17.9ms | 0.92 | 55% |
| 2–5KB | 4,921 | 14.2ms | 15.6ms | 15.8ms | 0.91 | 57% |
| 5–10KB | 103 | 13.6ms | 15.3ms | 19.0ms | 0.89 | 48% |
| 10–50KB | 29 | 73.7ms | 114.7ms | 107.6ms | 0.64 | 86% |
| >50KB | 40 | 141.7ms | 127.3ms | 127.6ms | 1.11 | 35% |

**O0/O3 per-test ratio percentiles (O0/O3 < 1.0 means O0 is faster):**

| Percentile | O0/O3 ratio |
|------------|-------------|
| P5 | 0.400 |
| P25 | 0.663 |
| P50 (median) | 0.934 |
| P75 | 1.323 |
| P95 | 2.377 |

**Key findings:**

1. **-O0 wins overall by 8.7%** — MIR optimization cost exceeds runtime benefit for short-lived test262 scripts (median 7.9ms execution time)
2. **No useful adaptive threshold** — O0 dominates across all file sizes except tiny (<200B, only 33 tests) and huge (>50KB, only 40 tests). Source-size-based adaptive strategies never beat pure O0.
3. **Oracle adaptive could save 22.9%** but no practical predictor achieves it — the O0-vs-O2 crossover depends on loop iteration counts at runtime, not on static source properties.
4. **-O3 is strictly worse than -O2** — Level 3 adds no optimization passes beyond level 2 in MIR, but measurement noise plus marginally different code generation makes it 3.2% slower.

**Implementation:**

- Added global `g_js_mir_optimize_level` in `transpile_js_mir.cpp` (default: 2 for general use)
- Replaced all 5 hardcoded `jit_init(2)` calls with `jit_init(g_js_mir_optimize_level)`
- Added `--opt-level=N` CLI flag to `js-test-batch` in `main.cpp`
- Preamble (`transpile_js_to_mir_preamble`) always uses -O3 via save/restore pattern — harness code benefits from optimization since it's compiled once and reused across all tests
- GTest (`test_js_test262_gtest.cpp`) defaults to `--opt-level=0` and passes it through `posix_spawn` argv to `lambda.exe`
- Timing data written to `temp/_t262_timing_oN.tsv` (suffix varies by opt level)

### Stage 2: Sparse Array Support (High Impact, Medium Risk)

**Problem:** 12 tests crash because `js_array_set_int()` fills index gaps with a linear loop.

**Proposal:** Add a sparse threshold — when an index exceeds current length by more than a configurable gap (e.g., 10,000), store the element in a backing hashmap instead of gap-filling:

```cpp
extern "C" Item js_array_set_int(Item array, int64_t index, Item value) {
    Array* arr = array.array;
    if (index >= 0 && index < arr->length) {
        arr->items[index] = value;
    } else if (index >= 0) {
        int64_t gap = index - arr->length;
        if (gap > SPARSE_THRESHOLD) {  // e.g., 10000
            // Store in sparse backing map: arr->sparse_map[index] = value
            js_array_sparse_set(arr, index, value);
        } else {
            // Existing behavior: fill gaps
            Item undef = make_js_undefined();
            while (arr->length < (int)index) array_push(arr, undef);
            if ((int)index == arr->length) array_push(arr, value);
            else arr->items[index] = value;
        }
    }
    return value;
}
```

Requires changes to:
- `Array` struct in `lambda-data.hpp`: add `HashMap* sparse_entries` field
- `js_array_get_int()`: check sparse map for indices ≥ arr->length
- `js_array_length()`: track virtual length separately from dense length
- `Array.prototype.indexOf/lastIndexOf/includes`: skip empty gaps in sparse region

**Estimated impact:** Fixes 12 crashers → moves from quarantine. Reduces 12 × ~9s = 108s of wasted time.

### Stage 3: Batch MIR Context Reuse (Medium Impact, Medium Risk)

**Problem:** Each test creates a **new MIR context** via `jit_init()` and destroys it after execution. `MIR_init()` allocates memory pools, `MIR_gen_init()` sets up the code generator.

**Proposal:** Reuse the MIR context across tests in the same batch, only creating new modules:

```cpp
// Before batch loop:
MIR_context_t batch_ctx = jit_init(0);

// Per test:
MIR_module_t mod = MIR_new_module(batch_ctx, test_name);
// ... transpile and compile ...
// After execution, unload the module:
MIR_unload_module(batch_ctx, mod);
```

This avoids `MIR_init()/MIR_finish()` per test (~0.5ms each) and import resolver hashmap rebuild.

**Risk:** MIR internal state leakage between tests. Needs validation that `MIR_unload_module` properly cleans up.

**Estimated savings:** ~1ms per test × 27,000 = **27s**.

### Stage 4: Real Preamble Caching (Medium Impact, High Effort)

**Problem:** The current "preamble" only caches module constant name→index mappings. The harness code (`sta.js` + `assert.js`, ~4KB) is re-parsed and re-transpiled every test.

**Proposal:** Cache the compiled MIR module for the harness. Each test links against the cached preamble module instead of re-compiling it:

```
Harness compilation (once per batch):
  parse(sta.js + assert.js) → AST → transpile → MIR module → JIT compile → cache

Per test:
  parse(test.js) → AST → transpile → MIR module → link to cached preamble → JIT compile → execute
```

This requires MIR's multi-module linking to work across compilation sessions. The preamble module would export harness functions (assert.sameValue, assert.throws, etc.) and the test module would import them.

**Estimated savings:** ~2–4ms per test × 27,000 = **54–108s**.

### Stage 5: Guard on Huge Length Iteration (Low Effort, Low Impact)

**Problem:** `Array.prototype.indexOf/lastIndexOf.call({length: Infinity}, ...)` iterates up to `Number.MAX_SAFE_INTEGER` (9 × 10^15), causing timeout.

**Proposal:** In array methods that iterate by index, add a check: if the object is not a real Array (no `arr->items` backing store), clamp maximum iteration to a reasonable limit (e.g., 1M iterations). This matches how real engines handle array-like objects with absurd lengths.

```cpp
// In js_array_indexOf_call, js_array_lastIndexOf_call, etc.:
int64_t max_iter = is_real_array(obj) ? length : MIN(length, 1000000LL);
```

**Estimated impact:** Prevents timeout in ~7 boundary tests.

### Stage 6: RegExp Compilation Cache (Low Impact, Low Effort)

**Problem:** Each `new RegExp(pattern, flags)` creates a fresh RE2 compilation. Tests that create thousands of regex patterns in loops are slow.

**Proposal:** Cache compiled RE2 objects by `(pattern, flags)` key in a thread-local hashmap:

```cpp
// In js_regexp_create or equivalent:
HashMap* regex_cache; // key = hash(pattern + flags), value = RE2*
RE2* cached = hashmap_get(regex_cache, pattern_hash);
if (cached) return cached;
// ... compile and cache ...
```

**Estimated impact:** Saves ~1–3s for the 4 RegExp-heavy tests. Minimal overall impact on batch time.

## 4. Prioritization and Expected Impact

| Stage | Effort | Risk | Savings (Phase 2) | Status |
|-------|--------|------|-------------------|--------|
| 1. Adaptive MIR opt level | Low | Low | ~32s (O2→O0: 374→342s sum) | ✅ DONE — default -O0 for test262 batch |
| 2. Sparse array guard | Medium | Medium | ~(30 crasher recoveries + 166 passes) | ✅ DONE — SPARSE_GAP_MAX + int64_t fix |
| 3. Batch MIR context reuse | Medium | High | ~16s | BLOCKED — MIR lacks module unload API |
| 4. Real preamble caching | High | Medium | ~216s | ✅ ALREADY DONE (v21: harness pre-compilation) |
| 5. Huge-length guard | Low | Low | ~(timeout fixes) | ✅ ALREADY DONE (js_array_like_to_array cap) |
| 6. RegExp cache | Low | Low | ~1–2s | SKIPPED — target tests already <40ms |

### Stage Status Details

**Stage 2** — Implemented in `js_runtime.cpp`:
- Added `#define SPARSE_GAP_MAX 1000000` guard to `js_array_set_int`, `js_array_set`, `js_property_set` (arr.length setter)
- Fixed `(int)index` truncation → `int64_t` in all three functions
- All 11 sparse array crasher tests now pass (exit=0, no crash)
- `Object.defineProperty` path covered via `js_property_set` call chain
- Final results: 196 improvements (30 crasher recoveries, 166 other), 201 crashers remaining (down from 231)

**Stage 3** — BLOCKED: MIR has no `MIR_unload_module` API. `find_func` searches all modules linearly (wrong result with accumulated modules). `MIR_link` behavior with accumulated modules is undocumented. Measured jit_init+MIR_finish cost: ~0.6ms/test (16s for 27K tests). Risk outweighs savings without MIR API support.

**Stage 4** — Already implemented in v21 via `transpile_js_to_mir_preamble()`. Measured: 2.73ms/test WITH preamble vs. 10.76ms WITHOUT (~8ms savings × 27K = 216s). Harness compiled once, function objects persist across tests via hot-reload heap.

**Stage 5** — Already handled by `js_array_like_to_array()` safety cap (`len > 100000 → len = 100000`). All 17 huge-length boundary tests pass. No additional changes needed.

**Stage 6** — Target tests (RegExp-leading-escape-BMP.js, character-class-escape-non-whitespace.js) measured at <40ms each. The 7-9s times from initial analysis were due to concurrent batch load. Not worth implementing.

## 5. Measurement Infrastructure

### Added in This Analysis

1. **BATCH_END timing protocol**: `\x01BATCH_END <exit_code> <elapsed_us>\n` — microsecond resolution per test
2. **Timing TSV writer**: `temp/_t262_timing.tsv` (52,534 entries) — generated by GTest after Phase 2
3. **Parallel timing collector**: `temp/collect_timing.py` — standalone Python script, 8 workers, 278s for full sweep

### Timing data file format
```
test_name\texit_code\telapsed_us
built-ins_Array_isArray_15.4.3.2-1-1_js\t0\t3422
```

Exit codes: 0=pass, 1=fail, 124=timeout, 139=crash (SIGSEGV)

## 6. Post-v22 Progress: Runtime Fixes and Feature Gate Openings

### Current Baseline (post-v22 sessions)

| Metric                               | Value                                         |
| ------------------------------------ | --------------------------------------------- |
| Total test262 files                  | 53,399                                        |
| Tests batched (non-skipped)          | 38,649                                        |
| Tests skipped (unsupported features) | 10,172                                        |
| **Passing tests**                    | **28,477 (73.7% of batched, 53.3% of total)** |
| Quarantined crashers                 | 185 (136 crash-exit + 48 missing)             |
| Phase 2 wall clock (8 workers, -O0)  | 83.1s                                         |

### Progression from v22 baseline (11,554) → current (28,477)

| Run | Change | Passing | Delta | Regressions |
|-----|--------|---------|-------|-------------|
| v22 baseline | — | 11,554 | — | — |
| +Stage 2 sparse fix | Runtime | 11,750 | +196 | 0 |
| +v21→v22 rebase | Committed code | 12,713 | +963 | 0 |
| +9 runtime fixes | Runtime | 27,015 | +14,302 | 0 |
| +Symbol.iterator gate | Feature gate | 27,778 | +763 | 0 |
| +Symbol gate | Feature gate | 28,310 | +532 | 0 |
| +Symbol.toPrimitive, Symbol.hasInstance | Feature gate | 28,477 | +167 | 0 |

**Total improvement: +16,923 tests (146% increase), 0 regressions across all runs.**

### Runtime Fixes Applied (v22 sessions)

1. **Sequence expression / pattern / labeled statement function collection** — `jm_collect_functions` in `transpile_js_mir.cpp` missed SEQUENCE_EXPRESSION, LABELED_STATEMENT, ARRAY_PATTERN, OBJECT_PATTERN node types, causing undeclared function errors.

2. **typeof for built-in global functions** — `jm_transpile_unary` typeof now performs compile-time detection for 14 built-in function names (parseInt, parseFloat, eval, isNaN, isFinite, decodeURI, etc.), returning `"function"` directly.

3. **hasOwnProperty for accessor properties** — `js_has_own_property` in `js_globals.cpp` now checks for `__get_<key>` and `__set_<key>` accessor markers in addition to own data properties.

4. **js_get_length_item + Array.prototype.length** — New `js_get_length_item()` returns raw Item for `.length` access (avoids boxing/unboxing). MAP fallback changed from `make_js_undefined()` to `js_property_get(object, key)` for full property access chain. Added `Array.prototype.length = 0` in prototype initialization.

5. **`.length` type inference fix** — `jm_get_effective_type` in `transpile_js_mir.cpp` hardcoded `.length` as `LMD_TYPE_INT`. For unknown MAP objects, the native int fast path treated raw Item bits as an integer (producing garbage like 12893290736). Fixed to return `LMD_TYPE_ANY` for unknown object types.

6. **ToPrimitive in js_add** — `js_add` now performs valueOf→toString chain for MAP operands and converts FUNC to string, matching ES spec ToPrimitive abstract operation.

7. **js_to_string MAP case** — Recursive conversion for non-string `toString()` results.

8. **ToPrimitive in js_equal** — Full valueOf→toString ToPrimitive chain in abstract equality.

9. **ToPrimitive in js_less_than** — Same ToPrimitive fix for comparison operators (`<`, `>`, `<=`, `>=`).

### Feature Gates Opened

| Feature removed from UNSUPPORTED_FEATURES | Tests unlocked | Tests passing |
|-------------------------------------------|---------------|---------------|
| `Symbol.iterator` | ~1,830 | +763 |
| `Symbol` | ~1,205 | +532 |
| `Symbol.toPrimitive` | ~98 | +167 (combined) |
| `Symbol.hasInstance` | ~69 | (combined above) |

### Known Limitations Discovered

- **Symbol.toStringTag**: `Object.prototype.toString` does not check `Symbol.toStringTag` — gate remains closed (131 tests)
- **Named regex groups**: RE2 does not support `(?<name>...)` syntax — gate remains closed (100 tests)
- **JSON.parse/stringify as values**: Only work at call sites, not as property references or first-class function values
- **Symbol.asyncIterator**: Protocol doesn't work for `for-await-of` on custom objects — gate remains closed (538 tests)

## 7. Remaining Unsupported Features (Skipped Tests)

Total skipped: **10,172 tests** across features in UNSUPPORTED_FEATURES set + module/async flags.

### By Feature (sorted by total blocked tests)

"Sole" = tests where this is the **only** blocking feature (i.e., enabling it would directly unlock those tests).

| Feature | Total | Sole | Category |
|---------|------:|-----:|----------|
| `Temporal` | 6,662 | 6,534 | Stage 3 API — very large surface area |
| `async-iteration` | 4,968 | 4,340 | Async generators + Symbol.asyncIterator protocol |
| `dynamic-import` | 946 | 593 | `import()` expressions |
| `Reflect.construct` | 696 | 445 | Basic Reflect API |
| `regexp-unicode-property-escapes` | 681 | 585 | `\p{...}` in regex (RE2 limitation) |
| `iterator-helpers` | 567 | 558 | Iterator.prototype methods (Stage 3) |
| `Symbol.asyncIterator` | 538 | 1 | Mostly overlaps with async-iteration |
| `explicit-resource-management` | 477 | 443 | `using` / `Symbol.dispose` (Stage 3) |
| `Proxy` | 468 | 331 | Proxy handler traps |
| `Reflect` | 468 | 230 | Overlaps with Reflect.construct etc. |
| `resizable-arraybuffer` | 463 | 384 | ArrayBuffer.prototype.resize |
| `SharedArrayBuffer` | 463 | 144 | Shared memory (concurrency) |
| `Atomics` | 376 | 112 | Atomic operations (concurrency) |
| `Symbol.species` | 276 | 205 | @@species protocol for subclassing |
| `top-level-await` | 271 | 252 | Module-level await |
| `regexp-modifiers` | 230 | 230 | `(?ims:...)` inline flags |
| `source-phase-imports` | 228 | 12 | `import source` syntax |
| `cross-realm` | 201 | 57 | Cross-realm object identity |
| `regexp-v-flag` | 187 | 85 | Unicode sets (`/v` flag) |
| `change-array-by-copy` | 132 | 121 | `toSorted`, `toReversed`, `toSpliced`, `with` |
| `Symbol.toStringTag` | 131 | 81 | `Object.prototype.toString` tag |
| `Atomics.waitAsync` | 101 | 0 | All overlap with Atomics |
| `regexp-named-groups` | 100 | 82 | `(?<name>...)` (RE2 limitation) |
| `import-attributes` | 100 | 23 | `import ... with { type: "json" }` |
| `Symbol.replace` | 98 | 81 | @@replace protocol |
| `Array.fromAsync` | 95 | 94 | Async array creation |
| `Symbol.match` | 88 | 77 | @@match protocol |
| `ShadowRealm` | 64 | 57 | Isolated evaluation contexts |
| `Symbol.matchAll` | 63 | 49 | @@matchAll protocol |
| `Symbol.split` | 58 | 32 | @@split protocol |
| `Float16Array` | 49 | 44 | Float16 typed array |
| `FinalizationRegistry` | 49 | 35 | GC callback hooks |
| `Reflect.set` | 46 | 18 | Reflect.set() |
| `Symbol.unscopables` | 44 | 36 | `with` statement filtering |
| `IsHTMLDDA` | 42 | 31 | `document.all` special behavior |
| `Symbol.search` | 37 | 32 | @@search protocol |
| `WeakRef` | 37 | 23 | Weak references |
| `tail-call-optimization` | 35 | 34 | Proper tail calls |
| `AggregateError` | 31 | 28 | Error subclass for Promise.any |
| `regexp-match-indices` | 31 | 19 | `d` flag / `.indices` property |
| `symbols-as-weakmap-keys` | 29 | 25 | Symbol keys in WeakMap/WeakSet |
| `hashbang` | 29 | 29 | `#!` line support |
| `decorators` | 27 | 27 | Class decorators (Stage 3) |
| `caller` | 23 | 23 | `Function.prototype.caller` |
| `Reflect.setPrototypeOf` | 23 | 4 | Reflect.setPrototypeOf() |
| `import.meta` | 23 | 20 | Module metadata |
| `json-parse-with-source` | 22 | 20 | JSON.parse source text access |
| `regexp-lookbehind` | 19 | 17 | `(?<=...)` / `(?<!...)` (RE2 limitation) |
| `regexp-duplicate-named-groups` | 19 | 16 | Same group name in alternatives |
| `arbitrary-module-namespace-names` | 16 | 15 | Non-identifier module export names |
| `json-modules` | 13 | 0 | JSON import modules |
| `Uint8Array` | 11 | 7 | Uint8Array base64/hex methods |
| `Atomics.pause` | 6 | 5 | Atomics.pause() |
| `well-formed-json-stringify` | 1 | 1 | Lone surrogate escaping |

### Highest-Impact Opportunities (by sole-blocked tests)

| Priority | Feature | Sole tests | Effort | Notes |
|----------|---------|-----------|--------|-------|
| 1 | `Temporal` | 6,534 | Very High | Entire Date/Time API — hundreds of methods |
| 2 | `async-iteration` | 4,340 | Medium | Symbol.asyncIterator protocol on custom objects |
| 3 | `regexp-unicode-property-escapes` | 585 | Hard | RE2 doesn't support `\p{}`; would need ICU or PCRE2 |
| 4 | `iterator-helpers` | 558 | Medium | Iterator.prototype.{map,filter,take,drop,...} |
| 5 | `dynamic-import` | 593 | Medium | `import()` expression support |
| 6 | `Reflect.construct` | 445 | Medium | `new.target` + proxy-like construction |
| 7 | `explicit-resource-management` | 443 | Medium | `using` declaration + Symbol.dispose |
| 8 | `resizable-arraybuffer` | 384 | Medium | ArrayBuffer.prototype.resize/transfer |
| 9 | `Proxy` | 331 | High | Full Proxy handler trap implementation |
| 10 | `regexp-modifiers` | 230 | Medium | Inline regex flag modifiers |
| 11 | `top-level-await` | 252 | Low | Module-level await |
| 12 | `Symbol.species` | 205 | Medium | @@species for Array/Promise/RegExp subclassing |
| 13 | `change-array-by-copy` | 121 | Low | 4 new Array.prototype methods |
| 14 | `Array.fromAsync` | 94 | Low | Single async method |
| 15 | `regexp-named-groups` | 82 | Hard | RE2 limitation — needs regex engine change |

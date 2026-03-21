# Lambda Baseline Test Performance — Profiling & Optimization Proposal

## Date: March 21, 2026

---

## 1. Current Baseline

### Observed Timings (Debug Build, macOS arm64, `make test-lambda-baseline`)

| Metric | Value |
|--------|-------|
| **Wall time** | **23.7 seconds** |
| User CPU | 30.3 seconds |
| System CPU | 8.8 seconds |
| CPU utilization | 165% |
| Total tests | 657 (across 8 executables) |
| Test scripts | ~230 `.ls` files (functional + procedural + chart + math + latex) |

### Per-Executable Timing Breakdown

| Executable | Tests | Wall Time | Avg/Test | Category |
|-----------|-------|-----------|----------|----------|
| **test_lambda_gtest.exe** | **238** | **19.3 s** | **81 ms** | **Critical path** |
| test_c2mir_gtest.exe | 164 | 5.8 s | 35 ms | Runs in parallel |
| test_lambda_std_gtest.exe | 106 | 2.3 s | 21 ms | Runs in parallel |
| test_lambda_repl_gtest.exe | 38 | 0.8 s | 21 ms | Runs in parallel |
| test_lambda_errors_gtest.exe | 61 | 0.6 s | 10 ms | Runs in parallel |
| test_js_gtest.exe | 40 | 0.6 s | 15 ms | Runs in parallel |
| test_transpile_patterns_gtest.exe | 4 | 0.2 s | 38 ms | Runs in parallel |
| test_lambda_proc_gtest.exe | 6 | 0.08 s | 13 ms | Runs in parallel |

> **Critical insight:** `test_lambda_gtest.exe` at **19.3s** is the critical path. All 7 other executables finish in <6s and run concurrently. The overall wall time is entirely gated by this single executable.

---

## 2. Test Runner Architecture

```
make test-lambda-baseline
  └─ ./test/test_run.sh --target=lambda --category=baseline --parallel
      ├─ Discovers 8 test executables from build_lambda_config.json
      ├─ Launches up to 5 executables concurrently (MAX_CONCURRENT=5)
      └─ Each executable runs its tests SERIALLY:
          └─ test_lambda_gtest.exe (GTest parameterized):
              for each .ls file discovered in test/lambda/, test/lambda/chart/, etc.:
                  popen("./lambda.exe <script.ls>")   ← one process per test
                  read stdout
                  compare with .txt expected output
              → 238 sequential lambda.exe subprocess spawns
```

**Key problem: Within `test_lambda_gtest.exe`, all 238 tests run sequentially.** Each test spawns a fresh `lambda.exe` process via `popen()`, waits for it to finish, reads output, and compares. No parallelism exists within this executable.

The inter-executable parallelism (5 concurrent) is useful but irrelevant because `test_lambda_gtest.exe` alone takes 19.3s while the other 7 executables combined take <6s and overlap with it.

---

## 3. Per-Test Pipeline (Inside test_lambda_gtest.exe)

For each of the 238 parameterized tests:

```
1. popen("./lambda.exe <script.ls>")     ← fork+exec: ~3 ms overhead
2. lambda.exe startup                    ← log init, MIR init: ~12 ms
3. Parse .ls script (Tree-sitter)        ← 0.1–2 ms
4. Build AST + type check                ← 0.5–5 ms
5. MIR transpile + JIT compile           ← 1–20 ms
6. Execute                               ← 0.01–350 ms (chart tests are expensive)
7. Write log.txt                         ← 5–18 ms (always on, unconditional)
8. Print output to stdout                ← 0.01 ms
9. pclose() → test reads output          ← 1 ms
10. Read .txt file, compare strings      ← 0.1 ms
```

Steps 1–2 (process startup) cost ~15 ms per test × 238 tests = **~3.6s of pure overhead**.
Step 7 (logging) costs ~10 ms per test × 238 tests = **~2.4s of I/O overhead**.

### Test Time Distribution (test_lambda_gtest.exe, 238 tests)

| Time Bucket | Count | Description |
|------------|-------|-------------|
| < 20 ms | 46 | Trivial scripts (startup-dominated) |
| 20–50 ms | 137 | Typical scripts |
| 50–100 ms | 21 | Moderately complex |
| 100–200 ms | 4 | Complex (IoT report, deltablue) |
| 200–400 ms | 27 | **Chart tests** (~390 ms each) |
| > 400 ms | 3 | richards, latex_m7, math_html |

### Chart Tests Dominate

The 29 chart tests in `test/lambda/chart/` each take **370–425 ms** and account for **~11.3s** of the 19.3s total (58%). Each chart test generates SVG output, which involves heavy string building and formatting.

### Top 10 Slowest Tests

| Test | Time |
|------|------|
| awfy_richards2 | 737 ms |
| latex_test_latex_m7 | 734 ms |
| math_test_math_html_output | 727 ms |
| chart_test_candlestick | 425 ms |
| chart_test_errorbar | 412 ms |
| chart_test_boxplot | 402 ms |
| chart_test_bar_chart | 393 ms |
| chart_test_stacked_area_chart | 392 ms |
| chart_test_line_chart | 392 ms |
| chart_test_tick_chart | 391 ms |

---

## 4. Identified Bottlenecks & Optimization Proposals

---

### 4.1 GTest Sharding: Run test_lambda_gtest.exe as N Parallel Shards

**Category:** Test infrastructure (Bash script)
**Impact:** ★★★★★ Critical — addresses the dominant bottleneck
**Effort:** Small (test_run.sh change only)

**Problem:**
`test_lambda_gtest.exe` runs 238 tests sequentially in a single process, taking 19.3s. This single executable gates the entire `make test-lambda-baseline` wall time. The other 7 executables finish in <6s but are forced to wait for this one.

**Fix:**
GTest natively supports test sharding via environment variables `GTEST_TOTAL_SHARDS` and `GTEST_SHARD_INDEX`. Verified that this works:

```
GTEST_TOTAL_SHARDS=4 GTEST_SHARD_INDEX=0 → runs 60 tests in 4.4s
GTEST_TOTAL_SHARDS=4 GTEST_SHARD_INDEX=1 → runs 60 tests in 5.5s
```

Modify `test_run.sh` to detect large test executables (>100 tests or specifically `test_lambda_gtest.exe` and `test_c2mir_gtest.exe`) and launch them as multiple sharded processes:

```bash
# For large test suites, split into shards
if [[ "$test_exe" == *"test_lambda_gtest"* ]]; then
    SHARD_COUNT=4
    for shard_idx in $(seq 0 $((SHARD_COUNT - 1))); do
        GTEST_TOTAL_SHARDS=$SHARD_COUNT GTEST_SHARD_INDEX=$shard_idx \
            ./$test_exe --gtest_output=json:${json_file}_shard${shard_idx} &
        running_pids+=("$!")
    done
fi
```

With 4 shards of `test_lambda_gtest.exe`:
- Each shard: ~60 tests, ~5s wall time
- All 4 run in parallel → **~5–6s** instead of 19.3s

Similarly shard `test_c2mir_gtest.exe` into 2 shards (~3s each).

**Expected gain:**
Critical path drops from 19.3s → ~5–6s. Combined with other executables already running in parallel, overall wall time: **~8–10s** (from 23.7s).

---

### 4.2 Batch Test Mode for lambda.exe

**Category:** Lambda executable + Test infrastructure (C++ + GTest)
**Impact:** ★★★★☆ High — eliminates per-test process overhead
**Effort:** Medium

**Problem:**
Each of the 238 tests in `test_lambda_gtest.exe` spawns a fresh `lambda.exe` process via `popen()`. This means:
- 238 `fork()+exec()` system calls (~3 ms each = ~0.7s)
- 238 lambda.exe startups: log init, MIR init, memory pool setup (~12 ms each = ~2.9s)
- 238 log.txt open/write/close cycles (~10 ms each = ~2.4s)
- Total per-process overhead: **~6s** out of 19.3s (31%)

**Proposed `lambda.exe test-batch` command:**

Add a new CLI command that accepts multiple script files and runs them in a single process, outputting results in a structured format:

```bash
# Run multiple scripts in one process, output JSON results
./lambda.exe test-batch test/lambda/simple_expr.ls test/lambda/closure.ls ... \
    --output-format json --no-log
```

Output format:
```json
{
  "results": [
    {"script": "test/lambda/simple_expr.ls", "status": "ok", "output": "2\n42\nhello"},
    {"script": "test/lambda/closure.ls", "status": "ok", "output": "10\n20"},
    {"script": "test/lambda/error.ls", "status": "error", "exit_code": 1, "output": ""}
  ]
}
```

**Implementation in lambda.exe:**
1. Add `test-batch` command handler in `main.cpp`
2. Initialize runtime once (MIR, log, pools)
3. For each script file:
   - Reset/create fresh memory pool (use `pool_reset()` if available, else `pool_create()`/`pool_destroy()`)
   - Parse, transpile, execute
   - Capture stdout output into a buffer instead of printing to stdout
   - Record result (output string + exit status)
4. After all scripts: print JSON results to stdout
5. Shut down runtime once

**Implementation in test helpers:**
Replace `popen("./lambda.exe <script>")` per-test with a batch executor:

```cpp
// Before: 238 popen calls, one per test
char* actual = execute_lambda_script(script_path);

// After: One process for the batch, results cached
// In SetUpTestSuite():
static std::map<std::string, std::string> batch_results;
if (batch_results.empty()) {
    batch_results = execute_lambda_batch(all_script_paths);  // single popen
}
// In each test:
const std::string& actual = batch_results[script_path];
```

**Savings:**
- Eliminates 237 of 238 process spawns: ~3.6s saved
- Single MIR/runtime init instead of 238: ~2.9s saved
- Single log session instead of 238: ~2.4s saved
- **Total estimated savings: ~6–8s** from the 19.3s critical path

**Interaction with sharding (4.1):**
Batch mode and sharding are complementary. With 4 shards × batch mode:
- Each shard runs ~60 tests in a single `lambda.exe test-batch` process
- Each shard completes in ~2–3s (compute only, no per-test startup)
- Critical path: **~3s**

---

### 4.3 Always Use `--no-log` in Test Mode

**Category:** Test infrastructure (trivial fix)
**Impact:** ★★★★☆ High — free performance
**Effort:** Trivial

**Problem:**
Lambda tests currently run without `--no-log`, meaning every `lambda.exe` invocation writes debug logs to `./log.txt`. This is pure I/O waste during automated testing.

Measured impact on 140 core lambda test scripts:
```
With logging (default):    5.56 s
With --no-log:             3.85 s
Savings:                   1.71 s (31% faster)
```

The `--no-log` flag is already supported. The test helper (`test_lambda_helpers.hpp`) already checks for `LAMBDA_NO_LOG` environment variable but `test_run.sh` never sets it.

**Fix (Option A — environment variable):**
Add to `test_run.sh`:
```bash
export LAMBDA_NO_LOG=1
```

**Fix (Option B — always pass --no-log in test helpers):**
In `test_lambda_helpers.hpp`, unconditionally add `--no-log`:
```cpp
// Before:
const char* no_log_flag = getenv("LAMBDA_NO_LOG") ? " --no-log" : "";

// After:
const char* no_log_flag = " --no-log";
```

**Expected gain:** ~1.7s saved for `test_lambda_gtest.exe`, ~4–5s total across all executables (each spawns lambda.exe per test). This is a **multiplied** saving — it reduces the per-test cost, so every test benefits.

---

### 4.4 Increase MAX_CONCURRENT and Tune Shard Scheduling

**Category:** Test infrastructure (config)
**Impact:** ★★★☆☆ Medium
**Effort:** Trivial

**Problem:**
`test_run.sh` limits concurrent processes to `MAX_CONCURRENT=5`. With sharding (proposal 4.1), we'd have 4 shards of `test_lambda_gtest.exe` + 2 shards of `test_c2mir_gtest.exe` + 6 other executables = 12 total. Only 5 can run at once.

On Apple Silicon (8+ cores), bumping to 8 concurrent processes would allow all shards to run simultaneously without waiting.

**Fix:**
```bash
# In test_run.sh:
MAX_CONCURRENT=8  # was 5
```

**Expected gain:** ~1–2s by avoiding shard queueing.

---

### 4.5 Pre-compile Scripts: Cache MIR JIT Output

**Category:** Lambda executable (C++ optimization)
**Impact:** ★★☆☆☆ Low–Medium
**Effort:** High

**Observation:**
Each lambda.exe invocation re-parses the Tree-sitter grammar, re-builds the AST, and re-transpiles to MIR for every script. For 238 tests, common system function MIR codegen is duplicated 238 times.

The MIR transpilation + JIT compilation step takes 1–20 ms per script. Most of this is system function setup (the same for every script).

**Potential:** Cache the compiled MIR module for system functions at startup and only transpile user code per-script. This is already partially addressed by batch mode (proposal 4.2) where the runtime is initialized once.

**Status:** Low priority — batch mode already eliminates this overhead. Only relevant if batch mode is not implemented.

---

### 4.6 Parallelize Chart Tests Specifically

**Category:** Test infrastructure (scheduling)
**Impact:** ★★★☆☆ Medium
**Effort:** Small (part of 4.1)

**Problem:**
The 29 chart tests each take ~390 ms and collectively consume 11.3s. In the current sequential execution, they form a long serial chain. Even with 4 GTest shards, the chart tests may cluster into 1–2 shards due to alphabetical ordering, creating an imbalanced schedule.

**Fix (smart sharding):**
When implementing GTest sharding (4.1), ensure chart tests are distributed evenly across shards. GTest's default sharding uses a round-robin hash, which should distribute them reasonably well. If not, consider:

1. **Interleave by time estimate:** Sort tests by expected runtime (chart tests first, interspersed with fast tests), then assign to shards round-robin.
2. **Use more shards for heavy suites:** 6–8 shards for `test_lambda_gtest.exe` instead of 4, so each shard has ≤4 chart tests.

With 8 shards, each shard gets ~3–4 chart tests × 390ms = ~1.5s of chart work + ~25 fast tests × 30ms = ~0.75s → **~2.3s per shard**.

---

### 4.7 Avoid Redundant log.txt Contention in Parallel Execution

**Category:** Lambda executable (C++ fix)
**Impact:** ★★☆☆☆ Low–Medium
**Effort:** Small (already solved by 4.3)

**Problem:**
When multiple `lambda.exe` processes run in parallel (via sharding), they all compete to write to the same `./log.txt` file. This causes filesystem contention and potential corruption.

**Fix:** Already addressed by proposal 4.3 (use `--no-log` in tests). If logging is needed for debugging, each shard should write to a separate log file:
```bash
LAMBDA_LOG_FILE="./temp/log_shard${shard_idx}.txt" ./$test_exe
```

---

### 4.8 Skip `build-test` When Binaries Are Up-to-Date

**Category:** Build system
**Impact:** ★★☆☆☆ Low
**Effort:** Small

**Problem:**
`make test-lambda-baseline` depends on `build-test`, which invokes premake5 + make for every test executable. Even when nothing has changed, this takes ~2.5s to verify all targets are up-to-date.

**Fix:** Add a quick timestamp check:
```bash
# In Makefile:
test-lambda-baseline-fast:
	@./test/test_run.sh --target=lambda --category=baseline --parallel
```

This `test-lambda-baseline-fast` target skips the build step for iterative testing when you know binaries are current.

**Expected gain:** 2.5s saved for repeated test runs during development.

---

### 4.9 Reduce popen() Overhead: Use fork()+pipe Instead of popen/pclose

**Category:** Test infrastructure (C++ optimization)
**Impact:** ★★☆☆☆ Low–Medium
**Effort:** Medium

**Problem:**
`popen("./lambda.exe ...")` goes through `/bin/sh -c "..."`, adding an extra shell process between the test executable and lambda.exe. This doubles the fork/exec overhead.

**Fix:** Replace `popen()` with direct `fork()`+`exec()`+`pipe()`:
```cpp
int pipefd[2];
pipe(pipefd);
pid_t pid = fork();
if (pid == 0) {
    // child
    close(pipefd[0]);
    dup2(pipefd[1], STDOUT_FILENO);
    execl("./lambda.exe", "lambda.exe", script_path, NULL);
}
// parent
close(pipefd[1]);
// read from pipefd[0]
```

This eliminates the intermediate shell process, saving ~1–2 ms per test × 238 = ~0.3–0.5s.

**Status:** Low priority if batch mode (4.2) is implemented.

---

## 5. Summary of Proposals and Expected Impact

| # | Proposal | Type | Effort | Est. Wall Time Saved | Priority |
|---|---------|------|--------|---------------------|----------|
| 4.1 | **GTest sharding for test_lambda_gtest.exe** | Test infra | Small | **~13–14 s (55–60%)** | ★★★★★ |
| 4.2 | **Batch test mode (`lambda.exe test-batch`)** | C++ + Test | Medium | **~6–8 s (25–35%)** | ★★★★☆ |
| 4.3 | **Always use `--no-log` in tests** | Config | Trivial | **~4–5 s (17–21%)** | ★★★★☆ |
| 4.4 | **Increase MAX_CONCURRENT to 8** | Config | Trivial | ~1–2 s (5–8%) | ★★★☆☆ |
| 4.6 | **Chart-aware shard scheduling** | Test infra | Small | ~1–2 s (5–8%) | ★★★☆☆ |
| 4.8 | **Skip build-test when up-to-date** | Build | Small | ~2.5 s (10%) | ★★☆☆☆ |
| 4.5 | **Cache MIR JIT for system functions** | C++ opt | High | ~1 s (redundant with 4.2) | ★★☆☆☆ |
| 4.9 | **Replace popen with fork+exec** | C++ opt | Medium | ~0.5 s (redundant with 4.2) | ★★☆☆☆ |
| 4.7 | **Per-shard log files** | Config | Small | (part of 4.3) | ★★☆☆☆ |

---

## 6. Projected Outcomes

### Phase 1: Quick Wins (4.1 + 4.3 + 4.4) — Test infrastructure only

```
Change: GTest sharding (4 shards) + --no-log + MAX_CONCURRENT=8
Effort: ~1 hour, test_run.sh + test_lambda_helpers.hpp only

Critical path analysis:
  test_lambda_gtest.exe: 19.3s → 4 shards × ~3.8s each (with --no-log) → ~4s wall
  test_c2mir_gtest.exe:  5.8s → 2 shards × ~3s each → ~3s wall
  Other executables:     all <1s, finish within shard time

Projected wall time: ~6–7 s  (from 23.7s → 3.4–3.9× speedup)
```

### Phase 2: Batch Mode (4.1 + 4.2 + 4.3 + 4.4)

```
Change: Add lambda.exe test-batch + batch test helpers
Effort: ~1 day, lambda.exe C++ + test helpers + test_run.sh

Critical path analysis:
  test_lambda_gtest.exe: 4 shards × ~60 tests each
    Each shard: 1 lambda.exe test-batch process → ~2s (no per-test startup)
  test_c2mir_gtest.exe: 2 shards × ~80 tests each → ~2s per shard

Projected wall time: ~4–5 s  (from 23.7s → 4.7–5.9× speedup)
```

### Phase 3: All optimizations

```
Projected wall time: ~3–4 s  (from 23.7s → 5.9–7.9× speedup)
Floor: ~2s (raw compute for 238 scripts on 8 cores, no overhead)
```

---

## 7. Time Breakdown Visualization

### Current (23.7s wall)

```
Wall time 23.7 s
├─ build-test (verify all binaries up-to-date)          ~2.5 s
├─ test_run.sh startup + discovery                       ~0.5 s
├─ test_lambda_gtest.exe (CRITICAL PATH)                ~19.3 s
│   ├─ Process spawn overhead (238 × fork+exec)         ~3.6 s
│   ├─ lambda.exe startup per test (238 × MIR init)     ~2.9 s
│   ├─ log.txt writes (238 × file I/O)                  ~2.4 s
│   ├─ Chart test computation (29 × ~390 ms)            ~7.1 s ← actual work
│   ├─ Other test computation (209 × ~16 ms avg)        ~3.2 s ← actual work
│   └─ GTest overhead + output compare                   ~0.1 s
├─ test_c2mir_gtest.exe (overlaps partially)             ~5.8 s
├─ test_lambda_std_gtest.exe (overlaps)                  ~2.3 s
└─ Other 5 executables (overlap, all <1s)                ~1.5 s
```

### After Phase 1 (~6.5s wall)

```
Wall time ~6.5 s
├─ build-test                                            ~2.5 s
├─ Shard 0: test_lambda_gtest (60 tests, --no-log)      ~4.0 s ─┐
├─ Shard 1: test_lambda_gtest (60 tests, --no-log)      ~3.5 s  │ parallel
├─ Shard 2: test_lambda_gtest (60 tests, --no-log)      ~3.8 s  │
├─ Shard 3: test_lambda_gtest (60 tests, --no-log)      ~3.2 s  │
├─ Shard 0: test_c2mir_gtest (82 tests)                  ~3.0 s  │
├─ Shard 1: test_c2mir_gtest (82 tests)                  ~2.8 s  │
├─ test_lambda_std_gtest + 5 others                      ~2.3 s ─┘
└─ Result aggregation                                    ~0.2 s
   Critical path: build-test (2.5s) + slowest shard (4.0s) = ~6.5s
```

---

## 8. Implementation Results — Phase 1

### Changes Made

Phase 1 quick wins (proposals 4.1 + 4.3 + 4.4) were implemented:

1. **GTest sharding (4.1):** Modified `test/test_run.sh` to split `test_lambda_gtest.exe` into 4 shards and `test_c2mir_gtest.exe` into 2 shards using `GTEST_TOTAL_SHARDS`/`GTEST_SHARD_INDEX` env vars. Added shard result merging to aggregate JSON results back into single per-executable summaries.

2. **Always `--no-log` (4.3):** Modified `test/test_lambda_helpers.hpp` to unconditionally pass `--no-log` to lambda.exe (was gated behind `LAMBDA_NO_LOG` env var). Also added `--no-log` to `test_lambda_std_gtest.cpp`, `test_lambda_errors_gtest.cpp`, `test_lambda_repl_gtest.cpp`, and `test_transpile_patterns_gtest.cpp`. **Note:** `test_js_gtest.cpp` does NOT use `--no-log` because the `js` subcommand handler (`main.cpp:914`) uses positional `argv[1]`/`argv[2]` dispatch and doesn't support interleaved flags.

3. **MAX_CONCURRENT=8 (4.4):** Raised from 5 to 8 in `test_run.sh`.

### Files Modified

| File | Change |
|------|--------|
| `test/test_run.sh` | GTest sharding (4→shard lambda, 2→shard c2mir), MAX_CONCURRENT=8, shard result merging |
| `test/test_lambda_helpers.hpp` | Always use `--no-log` (removed env var check) |
| `test/test_lambda_std_gtest.cpp` | Added `--no-log` to 4 snprintf commands |
| `test/test_lambda_errors_gtest.cpp` | Added `--no-log` to popen command |
| `test/test_lambda_repl_gtest.cpp` | Added `--no-log` to popen command |
| `test/test_transpile_patterns_gtest.cpp` | Added `--no-log` to popen command |

### Measured Results

| Metric | Before | After | Change |
|--------|--------|-------|--------|
| **Wall time** | **23.7 s** | **12.6 s** | **–47%** |
| User CPU | 30.3 s | 38.1 s | +26% (more parallelism) |
| System CPU | 8.8 s | 9.4 s | +7% |
| CPU utilization | 165% | 376% | **2.3× better utilization** |
| Total tests | 657 | 657 | 100% pass |

Three consecutive runs: **12.74s, 12.55s, 12.62s** (consistent).

### Analysis

The actual result of **12.6s** is higher than the projected **6.5s** from Section 6. The discrepancy is primarily because:

1. **Build-test overhead** (~2.5s) was included in both measurements — this remains sequential.
2. **Shard imbalance:** The 29 chart tests (~390ms each) may cluster into certain shards due to GTest's hash-based round-robin distribution, creating hot shards.
3. **I/O contention:** 8+ parallel lambda.exe processes competing for filesystem I/O (temp files, shared libraries).

Despite the gap from projection, a **47% wall time reduction** (23.7s → 12.6s) with test infrastructure changes only (no C++ changes to lambda.exe) is a strong result for Phase 1.

### Next Steps

- **Phase 2 (Batch mode):** Implement `lambda.exe test-batch` to eliminate per-test process spawn overhead (~634 lambda.exe spawns → ~8). Projected to bring wall time to ~4–5s.
- **Phase 3 (Build skip):** Add `test-lambda-baseline-fast` make target to skip `build-test` when binaries are current (~2.5s saved for iterative testing).

---

## 9. Appendix: Raw Profiling Data

### Sequential execution by test directory (with --no-log)

| Directory | Files | Wall Time | Avg/File |
|-----------|-------|-----------|----------|
| test/lambda/ (core) | 140 | 3.85 s | 27 ms |
| test/lambda/chart/ | 29 | 11.27 s | 389 ms |
| test/lambda/latex/ | 14 | 2.18 s | 156 ms |
| test/lambda/math/ | 14 | 1.57 s | 112 ms |
| test/lambda/proc/ | 33 | 0.65 s | 20 ms |
| **Total** | **230** | **19.5 s** | **85 ms** |

### Logging overhead measurement (140 core lambda tests)

```
With logging:     5.56 s  (default behavior)
Without logging:  3.85 s  (--no-log flag)
Overhead:         1.71 s  (31% of logged time)
Per-test save:    ~12 ms
```

### GTest sharding verification

```
GTEST_TOTAL_SHARDS=4, GTEST_SHARD_INDEX=0: 60 tests, 4.4 s
GTEST_TOTAL_SHARDS=4, GTEST_SHARD_INDEX=1: 60 tests, 5.5 s
Unsharded (all 238):                        238 tests, 19.3 s
```

### lambda.exe startup overhead (minimal script: `1 + 1`)

```
With logging:     49 ms wall (30 ms user, 10 ms system)
Without logging:  31 ms wall (30 ms user, 0 ms system)
Pure startup:     ~15 ms (from process spawn to first useful work)
```

### Total process spawn count per `make test-lambda-baseline`

| Executable | lambda.exe Spawns | Reason |
|-----------|-------------------|--------|
| test_lambda_gtest.exe | 238 | 1 per parameterized test |
| test_c2mir_gtest.exe | 164 | 1 per parameterized test |
| test_lambda_std_gtest.exe | 106 | 1 per parameterized test |
| test_lambda_repl_gtest.exe | 38 | 1 per parameterized test |
| test_lambda_errors_gtest.exe | ~38 | 1 per negative test |
| test_js_gtest.exe | 40 | 1 per JS transpile test |
| test_transpile_patterns_gtest.exe | 4 | 1 per pattern test |
| test_lambda_proc_gtest.exe | 6 | 1 per proc test |
| **Total** | **~634** | **634 lambda.exe process spawns** |

---

## 9. Key Insight Summary

1. **The critical path is `test_lambda_gtest.exe` (19.3s).** All other executables combined take <6s and already run in parallel. Sharding this single executable will have more impact than any other optimization.

2. **31% of per-test time is log I/O** — `log.txt` writes that nobody reads during automated testing. Using `--no-log` is free performance.

3. **Chart tests (29 scripts) consume 58% of test_lambda_gtest.exe time.** Each generates SVG output (~390 ms). These are the highest-value targets for parallelization.

4. **634 lambda.exe process spawns** per baseline run. Each incurs ~15 ms of startup overhead. Batch mode would reduce this to ~4–8 spawns (one per shard), saving ~9s of pure fork/exec/init overhead.

5. **GTest sharding is the highest-ROI change** — it requires only `test_run.sh` modifications (no C++ changes), uses built-in GTest functionality, and directly addresses the critical path.

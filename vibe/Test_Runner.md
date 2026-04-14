# Test Runner Redesign

## Problems with the Old Script (`test/test_run.sh`)

The original test runner was a 1300-line Bash script that accumulated several reliability issues:

### 1. Hardcoded Timeout — False Kills

The script used a fixed `TIMEOUT_DURATION="180s"` for most tests and `600s` for a handful of special cases (`test_c2mir_gtest`, `test_lambda_gtest`, `test_py_gtest`, `test_js_test262_gtest`).

This caused false timeouts in practice:

- **First run after compilation**: Cold JIT caches and code loading make the first execution significantly slower than subsequent runs. A timeout tuned for warm runs kills valid first runs.
- **Slow machines**: A 4-core cloud VM takes 2–3x longer than a MacBook Pro. The same 180s timeout that works on a fast laptop triggers false failures on CI.
- **Test suite growth**: As new test cases are added, the total runtime of a test executable increases. Timeouts that were generous yesterday become tight tomorrow.

### 2. Hardcoded Parallelism

`MAX_CONCURRENT=12` was baked into the script. On an 8-core machine this over-subscribes the CPU and slows everything down. On a 32-core machine it wastes capacity. On a 4-core CI runner it causes severe contention.

### 3. Bash Complexity

At 1300 lines, the script was doing things Bash is bad at:

- **JSON parsing** via `jq` — fragile pipelines, hard to debug, `jq` not available on all systems.
- **Parallel process management** — PID arrays, `kill -0` polling with `sleep 0.1`, manual array cleanup loops.
- **Multiple JSON formats** — GTest, Criterion, Catch2, Lambda runner — each with different `jq` queries.
- **Cross-platform support** — 50+ lines of Windows/MSYS2 path conversion.
- **Result aggregation** — string-based counters, manually constructed JSON via `echo`.

---

## New Script (`test/test_run.js`)

The replacement is a ~400-line Node.js script. Node.js was chosen because:

- Already in the project (`package.json` exists) — no new dependency.
- Native JSON parsing eliminates the `jq` dependency.
- `child_process.spawn` with streams gives event-driven process management.
- `os.cpus()` provides CPU count for auto-scaling.
- Cross-platform path handling via `path.join()`.

### CLI — Drop-in Replacement

Same flags as the old script:

```bash
node test/test_run.js --target=library --parallel
node test/test_run.js --exclude-target=jube --category=baseline
node test/test_run.js --raw --sequential
```

All `make test*` targets updated in the Makefile to call `node test/test_run.js`.

### Auto-Scaled Parallelism

```
MAX_CONCURRENT = os.cpus().length - 1   (minimum 1)
```

No configuration needed. A 4-core CI VM runs 3 concurrent tests; a 16-core workstation runs 15.

---

## Why Progress-Based Idle Timeout

### The Problem with Per-Test Timeout Config

A per-test fixed timeout requires answering: "how long should `test_lambda_gtest` take?" This question has no stable answer:

1. **Machine-dependent**: The same test takes 30s on a fast Mac and 120s on a slow cloud VM. You'd need per-machine multipliers, which must be calibrated.
2. **Growth-dependent**: As tests are added to `test_lambda_gtest`, its runtime grows. A timeout set today underestimates tomorrow's run.
3. **Run-order-dependent**: On the first run after compilation, JIT warmup and disk caching add overhead. The same binary runs faster the second time.
4. **Maintenance burden**: Every new test executable or test suite change requires reviewing and potentially updating timeout values. This is easy to forget and causes intermittent CI failures.

A calibration-probe approach (run a fast test first, derive a speed factor, scale timeouts) solves the machine-dependency problem but not the growth problem, and adds complexity.

### How Idle Timeout Works

Instead of asking "how long should this test take?", we ask: **"is this test still making progress?"**

```
spawn(testExe) → pipe stdout + stderr
  → on each line of output, reset the idle timer
  → if idle timer exceeds threshold → kill the process
```

GTest prints structured progress output for every test case:

```
[ RUN      ] StringTests.Concat
[       OK ] StringTests.Concat (2 ms)
[ RUN      ] StringTests.Split
[       OK ] StringTests.Split (1 ms)
```

A healthy test produces output every few seconds. A stuck test — deadlock, infinite loop, waiting on a resource that will never arrive — produces no output at all.

The idle timeout catches exactly the failure mode we care about (stuck process) with zero false positives on slow-but-progressing tests.

### Idle Timeout Scaling

The only machine-dependent parameter is the idle threshold itself, and this needs only coarse scaling:

| CPU Cores | Idle Timeout |
|-----------|-------------|
| 8+        | 120s        |
| 4–7       | 180s        |
| 1–3       | 240s        |

The rationale: on a loaded slow machine, the gap between consecutive test case outputs is wider because the OS scheduler gives less CPU time. But even on a 2-core machine, a 4-minute gap between two `[  OK  ]` lines means the test is stuck.

Manual override via environment variable:

```bash
LAMBDA_TEST_IDLE_TIMEOUT=300 node test/test_run.js --parallel
```

### Properties

| Property | Per-test timeout | Idle timeout |
|----------|-----------------|--------------|
| **False timeout on slow machine** | Common | Cannot happen (test still outputs) |
| **False timeout on first run** | Common | Cannot happen (JIT warmup still outputs) |
| **Adapts to test suite growth** | No — must update config | Yes — more tests = more output |
| **Detects stuck test** | Eventually (after fixed duration) | Quickly (after idle period) |
| **Configuration needed** | Per-test timeout values | None |
| **Maintenance burden** | Must review on every change | Zero |

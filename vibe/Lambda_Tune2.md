# Lambda Tune 2: MIR Benchmark Regression

**Date**: June 27, 2026
**Repo baseline inspected**: `bb40c4c81` plus the local `lambda/lambda-mem.cpp` tuning patch

This note records the diagnosis and first fix for the Lambda/MIR benchmark regression seen between `test/benchmark/Overall_Result4.md` and `test/benchmark/Overall_Result9.md`.

## 1. Regression Summary

Result4 is the last full pre-Result7 report with Lambda/MIR tables and the same Node.js reference version as Result9 (`v22.13.0`). Result5 has some MIR timings, but it was a rejected direct-string-pointer experiment and is not a clean baseline.

Overall MIR/Node.js geometric mean:

| Report | Raw | Dedup |
|---|---:|---:|
| Result4 | 1.21x | 1.05x |
| Result9 | 4.62x | 4.31x |
| Change | 3.82x worse | 4.11x worse |

Suite-level MIR/Node.js regression:

| Suite | Result4 | Result9 | Change |
|---|---:|---:|---:|
| R7RS | 0.44x | 2.03x | 4.6x worse |
| AWFY | 0.62x | 3.60x | 5.8x worse |
| BENG | 0.94x | 1.57x | 1.7x worse |
| KOSTYA | 2.09x | 9.86x | 4.7x worse |
| LARCENY | 1.47x | 6.19x | 4.2x worse |
| JetStream | 7.28x | 21.2x | 2.9x worse |

The shape was not uniform. Recursive and function-call-heavy benchmarks regressed much more than tight loops. In the focused R7RS slice, `fib`, `fibfp`, `tak`, `cpstak`, and `ack` were much slower, while `sum` and `sumfp` stayed flat.

## 2. Workload Check

The focused R7RS benchmark files did not change across the relevant period:

- `test/benchmark/r7rs/fib2.ls`
- `test/benchmark/r7rs/tak2.ls`
- `test/benchmark/r7rs/ack2.ls`
- `test/benchmark/r7rs/sum2.ls`

That made this a runtime/compiler regression, not a benchmark workload change, at least for the representative R7RS slice.

## 3. Git History Clues

### Main Suspect: `c07158052`

Commit:

```text
c07158052c821ff0b2b8208e5f9e9889de2f4a86 pdf fix
AuthorDate: Tue May 5 09:07:14 2026 +0800
```

Touched files:

| File | Change |
|---|---:|
| `lambda/lambda-mem.cpp` | 141 lines changed |
| `lambda/sys_func_registry.c` | 5 lines changed |
| `lambda/transpile-mir.cpp` | 352 lines changed |

This commit introduced JIT GC root frames for Lambda/MIR:

- `emit_jit_root_frame_enter()`
- `emit_jit_root_frame_exit()`
- `heap_jit_gc_root_frame_enter()`
- `heap_jit_gc_root_frame_set()`
- `heap_jit_gc_root_frame_get()`
- `heap_jit_gc_root_frame_exit()`

Result4-era source did not have these helpers. Current MIR emitted `heap_jit_gc_root_frame_enter()` at function entry and `heap_jit_gc_root_frame_exit()` at function exit. The runtime implementation allocated and freed a `JitGcRootFrame` on every call, even when the function never stored a GC root slot.

That matches the observed benchmark shape:

- recursive/function-call-heavy code got much slower;
- scalar loop benchmarks stayed near the same speed;
- the regression appeared after Result4-era source.

### Ruled Out: Direct-Call Guard Change

Another plausible history clue was a later direct-call guard change related to `is_fn_variable`. A test patch relaxed that guard for declared function entries, rebuilt release, and reran the focused benchmark slice.

Result: no meaningful improvement. The direct-call guard was not the major cause of this regression.

### Blocked Comparator: C2MIR

C2MIR could not be used as a clean discriminator in the current benchmark runner:

```text
./lambda.exe run --c2mir test/benchmark/r7rs/fib2.ls
Error: Unknown run option '--c2mir'
```

So the diagnosis used current MIR behavior, git-history evidence, and targeted runtime experiments.

## 4. Root Cause

The hot path was per-call JIT GC root-frame management in Lambda/MIR.

Before the fix, every MIR function call did this:

1. enter function;
2. call `heap_jit_gc_root_frame_enter()`;
3. allocate a `JitGcRootFrame` with `mem_calloc`;
4. execute function body;
5. call `heap_jit_gc_root_frame_exit()`;
6. unregister/free any blocks;
7. free the frame.

For scalar recursive functions such as `fib(n: int)` and `ack(m: int, n: int)`, most calls do not need a root frame at all. They use native integer registers and do not store GC-rooted locals. The old implementation still paid heap allocation/free overhead on every call.

An unsafe measurement-only probe that skipped user-function root frames confirmed the direction: scalar recursive benchmarks got much faster. That probe was not kept because it would be unsafe for functions that allocate or hold GC-managed values.

## 5. Fix

The landed fix is runtime-side and preserves the MIR contract.

File:

- `lambda/lambda-mem.cpp`

Main changes:

1. `heap_jit_gc_root_frame_enter()` now increments a thread-local depth counter instead of immediately allocating a frame.
2. A `JitGcRootFrame` is allocated lazily only when MIR calls `heap_jit_gc_root_frame_set()`.
3. Each frame stores the depth at which it was created, so nested scalar calls do not accidentally read or pop a caller's frame.
4. Frame headers are reused through a bounded thread-local cache:

```c
#define JIT_GC_ROOT_FRAME_CACHE_MAX 256
```

5. `heap_destroy()` clears active and cached JIT root frames so tracked memory is not retained across runtime teardown.

This keeps root safety for functions that actually create root slots, while making scalar-only function calls pay only a cheap depth increment/decrement.

## 6. Before/After Focused Benchmark

Command:

```bash
python3 test/benchmark/run_benchmarks.py -e mir,nodejs -s r7rs -b fib,tak,sum,ack -n 3 --no-save
```

Release build, median of 3 runs:

| Benchmark | Before fix | After fix | Change |
|---|---:|---:|---:|
| `r7rs/fib` | ~52.2 ms | 29.4 ms | 1.78x faster |
| `r7rs/fibfp` | ~56.0 ms | 30.7 ms | 1.82x faster |
| `r7rs/tak` | ~3.30 ms | 1.74 ms | 1.90x faster |
| `r7rs/cpstak` | ~6.53 ms | 3.56 ms | 1.83x faster |
| `r7rs/ack` | ~161 ms | 96.4 ms | 1.67x faster |
| `r7rs/sum` | ~0.289 ms | 0.280 ms | flat |
| `r7rs/sumfp` | ~0.069 ms | 0.069 ms | flat |

The improvement is strongest where function-call volume dominates. Loop-only scalar benchmarks stay flat, which confirms that this fix targets the regression shape instead of shifting unrelated costs.

## 7. LambdaJS Impact

This change is not expected to affect LambdaJS performance directly.

The JIT GC root-frame helpers are emitted by `lambda/transpile-mir.cpp`. The LambdaJS MIR sources under `lambda/js/` do not reference:

- `heap_jit_gc_root_frame_*`
- `emit_jit_root_frame_*`
- `create_gc_root_slot`

LambdaJS shares `lambda-mem.cpp`, but if no JIT root frames are active, the new lazy/cache path is inert except during cleanup.

## 8. Verification

Completed checks:

| Check | Result |
|---|---|
| Release lambda build | passed |
| Focused R7RS benchmark slice | passed |
| `make test-lambda-baseline` | 3123/3123 passed |
| `git diff --check` | passed |

The first sandboxed `make test-lambda-baseline` attempt was blocked while writing Git submodule metadata under `.git/modules/test/yaml/config`. Rerunning with escalation allowed the submodule setup and the full baseline gate passed.

## 9. Remaining Work

This fix recovers a significant fraction of the Result4-to-Result9 regression but does not fully restore Result4 performance.

Recommended next steps:

1. Run a full Result10 benchmark after this patch to quantify suite-wide recovery.
2. Profile `ack` and other remaining recursive outliers to see whether remaining cost is root-block creation, call ABI boxing, or general MIR call overhead.
3. Audit whether MIR can statically omit root-frame enter/exit for proven scalar-only functions. The runtime lazy-frame fix is safer and landed first; a compile-time elision pass may recover more but needs a precise proof to avoid GC unsafety.
4. Revisit C2MIR runner support if it is still useful as a historical comparator.

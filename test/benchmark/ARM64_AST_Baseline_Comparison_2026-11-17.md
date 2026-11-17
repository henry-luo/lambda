# ARM64 AST baseline experiment — 2026-11-17

**Archived 2026-11-18:** `ast-native` is removed from all current builds under
D8.3.1v3. These are historical measurements; reproduction requires the pinned
experiment executable. Source and fixtures are retained in `ast_native/archive/`.


The prototype emits executable ARM64 instructions directly from admitted analyzed AST bodies. It creates no MIR module. It is an explicit `ast-native` mode; ordinary AUTO, strict T0 and MIR execution remain available (D8.3.1v2–D8.3.4). This is a limited scalar-body experiment, not a general-purpose replacement for MIR L0.

## Outcome

On these four rows, total reported compilation is about 3× faster and compiler-boundary RSS is about 14–15% lower than MIR L0. Self-reported execution is about 82–92× slower, making end-to-end execution about 7–9× slower. The current prototype is therefore not a useful MIR L0 replacement for these compute-heavy workloads.

The emitter itself takes 73–78 microseconds per script in the separate metric samples. The main execution costs retained by design are boxed runtime operations, rooted intermediates, and generic calls through T0 activation/contract handling on every recursive invocation. Identifying their individual contributions requires further profiling; these measurements do not isolate them.

## Scope and method

Unchanged registered `r7rs/fib` and `r7rs/fibfp` workloads, typed and untyped. Every row compiled its recursive kernel: 1 admitted definition, 2 rejected definitions, 635,621 native body invocations and 2 interpreted wrapper body invocations. Procedures, error-channel functions, loops, local declarations/stores, captures, tail-call transformations, collections and effects are outside the initial native admission. Other functions stay in T0, with MIR promotion disabled for this mode.

Both modes use the same pinned release executable: `temp/arm64_baseline/lambda-release`, SHA-256 `35ae34ee65e0061c827dc8e9bdaade1a113d9686b1131e4a69938449107abefd`. Source commit `ae76dbf6de6917dd2a7b397bd6c211f8d118959b` plus the local implementation and earlier Input/DOM build repairs. Host: Apple M3, 24 GiB RAM, macOS 26.5.2. Three measured alternating pairs plus one warmup per row; every pair has identical non-timing stdout. Cache disabled; large-source MIR interpreter fallback disabled; MIR optimization level explicitly 0. Raw samples and source hashes are in [the result artifact](ast_native/arm64_scalar_results_2026-11-17.json).

The comparison is between full execution configurations: MIR compiles four generated functions, while this prototype emits one function and interprets its wrappers. Compilation and code-size differences include that coverage difference; they are not equal-coverage backend efficiency measurements. Native dispatch still uses T0 parameter/return admission and stack guards. All GC-visible native intermediates live in a `RootFrame`; no conservative stack scanning is used (D6.3.3).

## Timing

Self and wall times below are medians of three runs, in milliseconds. Compilation is `COMPILER_TIMING.build_transpile_us` from one separate instrumented run per mode, converted to milliseconds. It includes parsing, AST construction, validation and planning/emission. Wall time includes process startup and teardown; self time uses the unchanged script timer.

| Workload | Variant | L0 compile | AST compile | L0 self | AST self | L0 wall | AST wall |
|---|---|---:|---:|---:|---:|---:|---:|
| fib | untyped | 2.079 | 0.622 | 0.815 | 74.005 | 9.375 | 81.312 |
| fib | typed | 2.074 | 0.621 | 0.810 | 74.424 | 8.818 | 81.322 |
| fibfp | untyped | 2.116 | 0.632 | 0.800 | 65.826 | 10.066 | 73.416 |
| fibfp | typed | 1.911 | 0.638 | 0.801 | 66.413 | 9.478 | 73.806 |

## Memory and generated code

Single separate samples, process RSS in MiB. Compile RSS is the lifetime high-water mark sampled at the compiler boundary, not compiler-only allocations. Process peak RSS includes compilation, execution and teardown; execution-only peak was not isolated. Code bytes exclude runtime helpers and interpreted wrappers. Each prototype image maps one 16 KiB executable page, even though only 876 bytes contain generated code.

| Workload | Variant | L0 compile RSS | AST compile RSS | L0 process RSS | AST process RSS | L0 code bytes (4 functions) | AST code bytes (1 function) |
|---|---|---:|---:|---:|---:|---:|---:|
| fib | untyped | 15.44 | 13.14 | 16.86 | 15.17 | 4640 | 876 |
| fib | typed | 15.39 | 13.20 | 16.81 | 15.23 | 4640 | 876 |
| fibfp | untyped | 15.56 | 13.16 | 16.94 | 15.17 | 4128 | 876 |
| fibfp | typed | 15.41 | 13.20 | 16.86 | 15.22 | 3808 | 876 |

## Validation and reproduction

The focused scalar fixture matches its golden in T0, MIR and native modes. Native mode also passes forced-GC/freed-memory-poison execution. A separate fixture verifies that procedures and error-channel functions are excluded, and that unsupported loops, self-tail calls and fallible results retain their T0 behavior. The focused gate asserts native admission and actual native execution, rather than accepting silent interpretation. Release build and host export checks passed. The Lambda baseline passed 7,600/7,601 tests; `proc_sync_rtc` failed because the optional WebRTC dependency is unavailable, confirmed by a direct rerun. Test262 passed 40,261/40,261 with no regressions on the final release. The Lambda baseline run preceded the final native-only admission/setup-error refinements; the focused native gate and performance measurements passed on the final release.

```sh
python3 test/benchmark/test_ast_native.py --exe temp/arm64_baseline/lambda-release
python3 test/benchmark/run_ast_native_benchmarks.py --exe temp/arm64_baseline/lambda-release
LAMBDA_EXEC_BACKEND=ast-native LAMBDA_DISABLE_MIR_CACHE=1 temp/arm64_baseline/lambda-release run test/benchmark/r7rs/fib.ls
```

The executable images contain process-local AST and helper addresses and are freed with the Script. They are not persistent images for reuse across processes. MIR L2 remains available; persistent MIR/native image work is outside this experiment.

A next iteration should measure and reduce the generic call/activation cost, then add guarded direct scalar instructions and broader admission. Performance must be remeasured after those changes; fast emission alone does not make the current execution configuration faster.

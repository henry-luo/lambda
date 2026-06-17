# Lambda Benchmark Results: LambdaJS Engine - Round 7 Fresh Run

**Date:** 2026-06-16  
**Platform:** Darwin arm64, Apple Silicon macOS  
**Lambda version:** release build (`make release`, `./lambda.exe` 15 MB)  
**Node.js:** v24.7.0  
**Methodology:** 3 runs per benchmark, median of self-reported execution time (`__TIMING__`, excludes process startup)  
**Runner:** `python3 test/benchmark/run_benchmarks.py -e lambdajs,nodejs -n 3`  
**Results source:** `test/benchmark/benchmark_results_v3.json`  
**Suites measured:** R7RS, AWFY, BENG, KOSTYA, LARCENY, JetStream  

This is a fresh LambdaJS-vs-Node run. It should replace the carried-forward LambdaJS numbers in older reports when discussing the current tree.

---

## Summary

LambdaJS produced usable timings for **57 / 62** registered JS benchmarks. Five LambdaJS cases failed to produce a timing within the runner's 120s-per-run timeout / success criteria:

- `awfy/havlak`
- `awfy/cd`
- `jetstream/navier_stokes`
- `jetstream/hashmap`
- `jetstream/raytrace3d`

Across the **57 timed** benchmarks, LambdaJS is **18.43x slower than Node.js** by geometric mean. LambdaJS is faster than Node on two BENG cases:

- `beng/fannkuch`: **0.63x** LambdaJS / Node.js
- `beng/pidigits`: **0.11x** LambdaJS / Node.js

| Suite | Total | LambdaJS timed | Missing LambdaJS | Geo mean LambdaJS / Node.js |
|---|---:|---:|---:|---:|
| R7RS | 10 | 10 | 0 | 6.94x |
| AWFY | 14 | 12 | 2 | 52.13x |
| BENG | 10 | 10 | 0 | 6.62x |
| KOSTYA | 7 | 7 | 0 | 19.66x |
| LARCENY | 12 | 12 | 0 | 9.72x |
| JetStream | 9 | 6 | 3 | 215.75x |
| **Overall timed** | **62** | **57** | **5** | **18.43x** |

Lower is better. Values below 1.0x mean LambdaJS is faster than Node.js.

---

## Full Results

### R7RS

| Benchmark | LambdaJS (ms) | Node.js (ms) | Ratio |
|---|---:|---:|---:|
| fib | 15.3 | 1.72 | 8.91x |
| fibfp | 19.7 | 1.30 | 15.14x |
| tak | 1.20 | 0.301 | 3.98x |
| cpstak | 2.39 | 0.429 | 5.56x |
| sum | 20.9 | 0.808 | 25.88x |
| sumfp | 1.35 | 0.805 | 1.68x |
| nqueens | 3.05 | 1.48 | 2.06x |
| fft | 5.96 | 1.02 | 5.81x |
| mbrot | 25.2 | 0.804 | 31.32x |
| ack | 78.1 | 14.7 | 5.31x |

**Geo mean:** 6.94x.

### AWFY

| Benchmark | LambdaJS (ms) | Node.js (ms) | Ratio |
|---|---:|---:|---:|
| sieve | 0.474 | 0.370 | 1.28x |
| permute | 27.8 | 0.298 | 93.25x |
| queens | 18.2 | 0.443 | 41.14x |
| towers | 56.4 | 0.390 | 144.66x |
| bounce | 7.65 | 0.365 | 20.94x |
| list | 9.10 | 0.214 | 42.55x |
| storage | 28.9 | 0.369 | 78.16x |
| mandelbrot | 1136.8 | 21.2 | 53.67x |
| nbody | 526 | 6.36 | 82.82x |
| richards | 4172 | 39.0 | 107.09x |
| json | 70.7 | 1.52 | 46.38x |
| deltablue | 2872 | 7.82 | 367.35x |
| havlak | --- | 90.7 | --- |
| cd | --- | 30.7 | --- |

**Geo mean over timed cases:** 52.13x.

### BENG

| Benchmark | LambdaJS (ms) | Node.js (ms) | Ratio |
|---|---:|---:|---:|
| binarytrees | 45.2 | 4.43 | 10.21x |
| fannkuch | 1.81 | 2.89 | **0.63x** |
| fasta | 7.76 | 4.52 | 1.72x |
| knucleotide | 105 | 4.90 | 21.37x |
| mandelbrot | 134 | 10.9 | 12.25x |
| nbody | 996 | 6.82 | 146.11x |
| pidigits | 0.246 | 2.27 | **0.11x** |
| regexredux | 15.3 | 2.37 | 6.42x |
| revcomp | 61.0 | 3.60 | 16.95x |
| spectralnorm | 67.6 | 2.08 | 32.44x |

**Geo mean:** 6.62x.

### KOSTYA

| Benchmark | LambdaJS (ms) | Node.js (ms) | Ratio |
|---|---:|---:|---:|
| brainfuck | 719 | 32.5 | 22.11x |
| matmul | 2361 | 14.1 | 167.36x |
| primes | 14.5 | 4.60 | 3.15x |
| base64 | 1596 | 14.3 | 111.56x |
| levenshtein | 54.7 | 2.51 | 21.80x |
| json_gen | 40.8 | 4.89 | 8.34x |
| collatz | 5886 | 1223 | 4.81x |

**Geo mean:** 19.66x.

### LARCENY

| Benchmark | LambdaJS (ms) | Node.js (ms) | Ratio |
|---|---:|---:|---:|
| triangl | 1680 | 65.1 | 25.77x |
| array1 | 3.06 | 1.68 | 1.82x |
| deriv | 92.9 | 2.39 | 38.82x |
| diviter | 2527 | 386 | 6.55x |
| divrec | 26.3 | 7.81 | 3.36x |
| gcbench | 1039 | 20.8 | 49.89x |
| paraffins | 1.56 | 0.613 | 2.54x |
| pnpoly | 127 | 4.00 | 31.78x |
| primes | 13.5 | 4.37 | 3.09x |
| puzzle | 20.7 | 2.14 | 9.68x |
| quicksort | 25.6 | 1.63 | 15.71x |
| ray | 14.0 | 1.48 | 9.48x |

**Geo mean:** 9.72x.

### JetStream

| Benchmark | LambdaJS (ms) | Node.js (ms) | Ratio |
|---|---:|---:|---:|
| nbody | 1013 | 4.16 | 243.57x |
| cube3d | 25870 | 13.0 | 1992.58x |
| navier_stokes | --- | 7.03 | --- |
| richards | 654 | 4.88 | 134.13x |
| splay | 1502 | 9.06 | 165.78x |
| deltablue | 1767 | 5.89 | 300.09x |
| hashmap | --- | 11.4 | --- |
| crypto_sha1 | 241 | 7.73 | 31.16x |
| raytrace3d | --- | 12.2 | --- |

**Geo mean over timed cases:** 215.75x.

---

## Worst Current Ratios

| Benchmark | LambdaJS (ms) | Node.js (ms) | Ratio | Likely dominant cost |
|---|---:|---:|---:|---|
| jetstream/cube3d | 25870 | 13.0 | 1992.58x | 3D numeric arrays, object/field traffic |
| awfy/deltablue | 2872 | 7.82 | 367.35x | polymorphic method dispatch, object slots |
| jetstream/deltablue | 1767 | 5.89 | 300.09x | polymorphic method dispatch |
| jetstream/nbody | 1013 | 4.16 | 243.57x | float-heavy loops |
| kostya/matmul | 2361 | 14.1 | 167.36x | dense numeric array loops |
| jetstream/splay | 1502 | 9.06 | 165.78x | allocation and tree object traffic |
| beng/nbody | 996 | 6.82 | 146.11x | float-heavy loops |
| awfy/towers | 56.4 | 0.390 | 144.66x | call/recursion overhead and object traffic |
| jetstream/richards | 654 | 4.88 | 134.13x | object dispatch and scheduler objects |
| kostya/base64 | 1596 | 14.3 | 111.56x | string/array push and repeated builtin dispatch |

---

## Current Code Analysis

The current performance profile matches several visible implementation bottlenecks in the LambdaJS engine.

### 1. Generic property access is still expensive

`js_property_get()` in `lambda/js/js_runtime.cpp` is necessarily correctness-heavy. Even after the MapKind fast path, a miss or dynamic property access can flow through:

- key coercion and name allocation for non-string keys
- `js_ordinary_get_own()`
- string wrapper virtual properties
- `js_prototype_lookup_ex()`
- built-in method fallback tables
- class/collection special cases

This is visible around `js_property_get()` and the fallback/builtin resolution paths in `lambda/js/js_runtime.cpp`. The OOP-heavy outliers (`deltablue`, `richards`, `splay`, `deriv`, `gcbench`) are exactly the workloads that repeatedly hit object fields, methods, or prototype-sensitive access.

### 2. Shaped slots exist, but the fastest path is not truly inline yet

The engine already creates shaped constructor objects via `js_new_object_with_shape()` and records `slot_entries` for O(1) access. It also has native slot helpers:

- `js_get_slot_f()`
- `js_get_slot_i()`
- `js_set_slot_f()`
- `js_set_slot_i()`

However, the current native field-load lowering uses type-guarded helper calls even on a shape-cache hit, because a compile-time FLOAT slot can hold a runtime INT and raw loading previously corrupted values. That is correct, but it means the "shape guard" currently saves hash/prototype lookup while still paying a C helper call and ShapeEntry type check per field read.

This matters for `nbody`, `deltablue`, `richards`, `cd`, `cube3d`, and `deriv`: their hot loops are many small field reads/writes.

### 3. Regular JS array reads are not using the inline array-get helper

`jm_transpile_array_get_inline()` is defined in `lambda/js/js_mir_expression_lowering.cpp`, but the native-expression path in `lambda/js/js_mir_calls_boxing_types.cpp` still calls `js_array_get_int()` and then unboxes when reading a regular JS array element in numeric code.

That is an immediate mismatch with the benchmark profile:

- `kostya/matmul`: 167x slower
- `jetstream/cube3d`: 1993x slower
- `r7rs/mbrot`: 31x slower
- `beng/spectralnorm`: 32x slower
- `beng/nbody`: 146x slower

Typed-array paths are more advanced; regular dense arrays are still paying runtime call overhead in native numeric expressions.

### 4. Typed arrays have raw paths, but inference coverage is narrow

Typed-array lowering can return native values directly through `jm_transpile_typed_array_get_native()` and can hoist typed-array data/length before loops. That is the right direction.

The weak point is coverage: the optimization depends on recognizing the receiver variable as a typed array (`typed_array_type >= 0`). If a typed array is stored in an object field, passed through a helper, returned from a factory, captured, aliased, or loaded from a property, the engine often falls back to generic property/builtin paths.

This limits improvement on JetStream-style numeric programs, where arrays often travel through object graphs instead of remaining as simple locals.

### 5. Method devirtualization is monomorphic and conservative

`jm_resolve_native_call()` resolves direct native calls for:

- identifier calls to collected native-versioned functions
- `obj.method()` where `obj` is a typed class instance with a known class entry

This helps simple class workloads, but deeply polymorphic code falls back to dynamic dispatch. `deltablue` and `richards` are the clearest examples: many methods are called through object graphs and inheritance patterns that the current static resolver cannot prove monomorphic.

### 6. Allocation and GC pressure remain visible

The allocation-heavy cases are still large gaps or missing:

- `larceny/gcbench`: 49.9x slower
- `beng/binarytrees`: 10.2x slower
- `jetstream/splay`: 165.8x slower
- `awfy/havlak`: no usable LambdaJS timing

This points at object allocation throughput, shape allocation, temporary boxing, and GC marking cost. The previous bump-pointer object allocation work helps, but benchmark objects still carry boxed fields, companion maps, shape entries, and temporary Items through the runtime.

---

## Proposed Performance Work

These are ordered by expected benchmark impact and implementation risk.

### P1. Wire inline dense-array element reads into native numeric expressions

**Target files:** `lambda/js/js_mir_calls_boxing_types.cpp`, `lambda/js/js_mir_expression_lowering.cpp`  
**Current issue:** regular array numeric reads call `js_array_get_int()` and unbox, even though `jm_transpile_array_get_inline()` exists.  
**Plan:** when `jm_get_js_array_var()` succeeds and index type is INT, emit `jm_transpile_array_get_inline()` instead of a runtime call, then unbox or consume the loaded Item. For loops, extend the existing P4h hoist to regular arrays by hoisting `items`, `length`, and `capacity` when the array is not reassigned and no sparse/prototype-mutating operation occurs in the loop.

**Expected wins:** `matmul`, `cube3d`, `spectralnorm`, `mbrot`, dense-array portions of `nbody`, `quicksort`, and `pnpoly`.

**Correctness gates:** holes must fall back to `js_array_get_int()`; arrays with `extra != 0`, arguments-exotic arrays, sparse length expansion, prototype numeric accessors, and in-loop array mutation must stay on the slow path.

### P2. Add a stable-field-type fast path for shaped slots

**Target files:** `lambda/js/js_mir_calls_boxing_types.cpp`, `lambda/js/js_mir_expression_lowering.cpp`, `lambda/js/js_runtime.cpp`  
**Current issue:** shape-cache hits still call `js_get_slot_i/f()` because runtime slot type can differ from compile-time inference.  
**Plan:** track per-constructor slot type stability. If all observed writes to a slot are numeric-stable (INT-only or FLOAT-only, or safely INT-to-FLOAT widened), emit a true inline load/store after a shape pointer guard. If a runtime write changes the slot family, invalidate the class slot-stability flag and fall back to helpers.

**Expected wins:** OOP/numeric workloads: `awfy/deltablue`, `awfy/richards`, `jetstream/deltablue`, `jetstream/richards`, `beng/nbody`, `jetstream/nbody`, `larceny/deriv`, `awfy/cd`.

**Correctness gates:** preserve mixed INT/FLOAT coercion, accessor/proxy/prototype behavior, `Object.defineProperty`, `Object.seal/freeze`, subclass fields, and post-constructor shape changes.

### P3. Broaden typed-array type propagation through fields and returns

**Target files:** `lambda/js/js_mir_statement_lowering.cpp`, `lambda/js/js_mir_calls_boxing_types.cpp`, `lambda/js/js_mir_function_collection_class_inference.cpp`  
**Current issue:** typed-array raw paths fire primarily when the receiver is a recognized local variable.  
**Plan:** propagate typed-array metadata through:

- `this.arr = new Float64Array(n)` constructor fields
- `let a = obj.arr` when `obj` class and field are known
- function returns from typed-array factory functions
- module constants and class fields that hold typed arrays

Then allow `obj.arr[i]` and local aliases of typed-array fields to lower to raw loads/stores.

**Expected wins:** `nbody`, `cube3d`, `navier_stokes`, `matmul`, `spectralnorm`, and any benchmark with arrays nested inside simulation objects.

**Correctness gates:** property replacement, typed-array detachment, resizable ArrayBuffer, subclassed typed arrays, and aliasing through user-visible property writes.

### P4. Add polymorphic inline caches for method calls

**Target files:** `lambda/js/js_mir_function_collection_class_inference.cpp`, `lambda/js/js_mir_expression_lowering.cpp`, `lambda/js/js_runtime.cpp`  
**Current issue:** native method calls require a statically known receiver class. Polymorphic OOP code falls back to property lookup plus `js_call_function()`.  
**Plan:** emit a small shape/class dispatch chain for hot method call sites:

1. load receiver shape or class id
2. compare against 2-4 known shapes/classes
3. call the corresponding native method directly
4. fall back to generic dispatch on miss

Populate the cache either statically from class hierarchy when safe, or dynamically from the first few runtime receivers.

**Expected wins:** `deltablue`, `richards`, `havlak`, `splay`, and object-heavy JetStream/AWFY cases.

**Correctness gates:** method overrides, monkey-patching prototypes, `super`, accessors, proxies, bound functions, and cross-realm function identity.

### P5. Specialize `Array.prototype.push` and common string/array builtins under pristine-prototype guards

**Target files:** `lambda/js/js_runtime.cpp`, `lambda/js/js_globals.cpp`, `lambda/js/js_mir_expression_lowering.cpp`  
**Current issue:** builtin method calls like `arr.push(x)` and string/array operations still pay property lookup and override checks unless specifically lowered.  
**Plan:** maintain realm-scoped pristine flags for `Array.prototype`, `String.prototype`, and selected constructors. When pristine, lower:

- `arr.push(x)` to direct dense append
- `arr[i] = x` append/grow patterns to direct capacity growth
- `str.charCodeAt(i)` / `str[i]` to direct string helpers

Fall back immediately when a prototype or relevant constructor is mutated.

**Expected wins:** `kostya/base64`, `beng/revcomp`, `beng/knucleotide`, `awfy/storage`, `awfy/list`.

**Correctness gates:** realm isolation, prototype mutation, subclass arrays, sparse arrays, non-writable length, accessors, proxies, and Symbol species.

### P6. Recover safe numeric ADD inference

**Target files:** `lambda/js/js_mir_calls_boxing_types.cpp`, `lambda/js/js_mir_function_collection_class_inference.cpp`  
**Current issue:** `+` is conservative because it can mean string concatenation. That pushes some numeric recursion and loops into boxed runtime addition.  
**Plan:** infer numeric ADD only when both operands are proven non-string and non-object at the call site. Add fixed-point return inference for self-recursive numeric functions so `ack`/`fib`-style functions keep native types across recursion.

**Expected wins:** R7RS recursion and integer loops, parts of `collatz`, `diviter`, and `sum`.

**Correctness gates:** string concatenation, valueOf/toString coercion, BigInt, Symbol TypeError, and object operands.

### P7. Reduce allocation pressure from numeric boxing and temporary objects

**Target files:** `lambda/js/js_mir_calls_boxing_types.cpp`, `lambda/js/js_runtime.cpp`, `lambda/lambda-data.cpp`, `lib/gc/gc_heap.c`  
**Current issue:** float boxing and temporary object creation still put pressure on the GC and allocator.  
**Plan:** after P1-P3 increase native value coverage, audit remaining `jm_box_float()` and temporary object hot sites using focused counters. For hot numeric loops, delay boxing until observable sinks. For allocation-heavy object constructors, pool/cache shape metadata and avoid per-instance metadata writes when the shape is already stable.

**Expected wins:** `gcbench`, `binarytrees`, `splay`, `havlak`, `cd`, and float-heavy simulations.

**Correctness gates:** GC rooting, observable object identity, numeric object wrappers, `Object.defineProperty`, and exception paths.

---

## Suggested Next Measurement Loop

1. Add a focused benchmark subset file for the current worst cases:
   - `awfy/deltablue`
   - `awfy/richards`
   - `kostya/matmul`
   - `kostya/base64`
   - `beng/nbody`
   - `beng/spectralnorm`
   - `larceny/gcbench`
   - `jetstream/cube3d`
2. Before each optimization, run that subset with `-e lambdajs,nodejs -n 3`.
3. After each optimization, run:
   - focused benchmark subset
   - `make test262-baseline`
   - `make test-lambda-baseline`
4. Only then rerun the full 62 benchmark sweep.

This keeps the feedback loop short while still protecting the JS conformance surface.

---

## Latest Rerun After P1 Dense-Array Fast Path

**Date:** 2026-06-16  
**Build:** release build from `make release` (`./lambda.exe` 15 MB)  
**Runner:** `python3 test/benchmark/run_benchmarks.py -e lambdajs -n 3`  
**Corrected follow-up:** `python3 test/benchmark/run_benchmarks.py -s jetstream -b cube3d -e lambdajs -n 3`  
**Results source:** `test/benchmark/benchmark_results_v3.json`  
**Comparison baseline:** `temp/benchmark_results_v3_before_inline_array.json`

This section records the fresh LambdaJS benchmark run after wiring guarded inline dense-array element reads into native numeric expressions. The earlier Round 7 report above is intentionally preserved; this is an incremental update on top of it.

The full rerun produced one noisy `jetstream/cube3d` median (`35.30s`). A targeted rerun immediately afterward produced `21.62s` with `--no-save`, then `23.46s` with saving enabled. The saved result therefore uses the corrected `23.46s` median.

### Performance Delta vs Previous Round 7 JSON

| Metric | Result |
|---|---:|
| Comparable LambdaJS benchmarks | 57 |
| Geometric mean speedup | **1.035x** |
| Geometric mean improvement | **+3.49%** |
| Summed runtime before | 57,822 ms |
| Summed runtime after | 54,181 ms |
| Summed runtime improvement | **+6.30%** |
| Benchmarks faster by >2% | 32 |
| Benchmarks slower by >2% | 2 |
| Benchmarks within +/-2% | 23 |

### Suite-Level Delta

| Suite | Comparable | Geo mean improvement | Summed runtime improvement |
|---|---:|---:|---:|
| R7RS | 10 | +2.05% | +1.34% |
| AWFY | 12 | +6.36% | +4.18% |
| BENG | 10 | +2.95% | -3.23% |
| KOSTYA | 7 | +6.22% | +6.33% |
| LARCENY | 12 | +0.64% | +2.06% |
| JetStream | 6 | +3.80% | +8.12% |

### Largest Wins

| Benchmark | Before (ms) | After (ms) | Speedup | Improvement |
|---|---:|---:|---:|---:|
| kostya/matmul | 2360.82 | 1867.47 | 1.264x | +20.90% |
| awfy/mandelbrot | 1136.84 | 920.86 | 1.235x | +19.00% |
| awfy/sieve | 0.474 | 0.417 | 1.138x | +12.12% |
| awfy/towers | 56.44 | 50.75 | 1.112x | +10.07% |
| jetstream/nbody | 1013.48 | 914.50 | 1.108x | +9.77% |
| beng/pidigits | 0.246 | 0.222 | 1.108x | +9.75% |
| jetstream/cube3d | 25869.7 | 23460.4 | 1.103x | +9.31% |
| awfy/bounce | 7.65 | 7.06 | 1.084x | +7.73% |
| beng/revcomp | 60.95 | 57.23 | 1.065x | +6.11% |
| beng/spectralnorm | 67.57 | 63.86 | 1.058x | +5.50% |

### Regressions to Watch

| Benchmark | Before (ms) | After (ms) | Speedup | Change |
|---|---:|---:|---:|---:|
| beng/nbody | 996.45 | 1057.59 | 0.942x | -6.14% |
| larceny/primes | 13.50 | 14.08 | 0.959x | -4.32% |

`beng/nbody` is the only substantial slowdown in a benchmark directly related to the dense numeric-array work. It should be rechecked with a focused 5-run or 10-run pass before treating it as a real regression, because the same run showed clear gains in `jetstream/nbody`, `kostya/matmul`, `beng/spectralnorm`, and `awfy/mandelbrot`.

### Current LambdaJS vs Node.js Snapshot

Node.js was not rerun in this latest pass; the ratios below use the existing Node.js medians already stored in `benchmark_results_v3.json`.

| Suite | Total | LambdaJS timed | Missing LambdaJS | Geo mean LambdaJS / Node.js |
|---|---:|---:|---:|---:|
| R7RS | 10 | 10 | 0 | 6.80x |
| AWFY | 14 | 12 | 2 | 49.01x |
| BENG | 10 | 10 | 0 | 6.43x |
| KOSTYA | 7 | 7 | 0 | 18.51x |
| LARCENY | 12 | 12 | 0 | 9.66x |
| JetStream | 9 | 6 | 3 | 207.86x |
| **Overall timed** | **62** | **57** | **5** | **17.81x** |

Missing LambdaJS timings remain unchanged:

- `awfy/havlak`
- `awfy/cd`
- `jetstream/navier_stokes`
- `jetstream/hashmap`
- `jetstream/raytrace3d`

### Comparison to Past Results

The latest current-tree number is the dense-array rerun above. It is a small improvement over the first June 16 Result7 snapshot, but it is materially worse than the older recorded LambdaJS benchmark runs.

Older `Overall_Result3.md` and `Overall_Result4.md` publish MIR Direct overall summaries, so the LambdaJS/Node values below were computed from their raw `LambdaJS` and `Node.js` columns. Missing `---` rows were excluded. `Overall_Result6.md` did not rerun JetStream, so its March R7 aggregate is a five-suite LambdaJS result only.

| Report | Date | Scope | LambdaJS / Node.js geo mean | Coverage | Read |
|---|---:|---|---:|---:|---|
| `Overall_Result7.md` latest rerun | 2026-06-16 | 6 suites | **17.81x slower** | 57 / 62 timed | Current tree; use this for current discussions |
| `Overall_Result7.md` initial fresh run | 2026-06-16 | 6 suites | 18.43x slower | 57 / 62 timed | Latest rerun improved this by 1.035x geo mean |
| `Overall_Result4.md` raw LambdaJS columns | 2026-03-19 | 6 suites | ~7.93x slower | 61 / 62 timed | Older tree had better coverage and aggregate ratio |
| `Overall_Result3.md` raw LambdaJS columns | 2026-03-09 | 6 suites | ~8.78x slower | 60 / 62 timed | Similar older six-suite baseline |
| `Overall_Result6.md` March R7 | 2026-03-24 | 5 suites, no JetStream | ~2.14x slower | 53 / 53 timed | Strong older LambdaJS-only optimization snapshot, not six-suite comparable |

On the shared five-suite family from `Overall_Result6.md` (R7RS, AWFY, BENG, KOSTYA, LARCENY), the latest Result7 snapshot is about **13.34x** slower than Node over 51 timed cases, versus about **2.15x** in the March R7 table over 53 timed cases. That is roughly **6.2x worse** on the comparable suite family, before adding the current JetStream gap of **207.86x** over its six timed cases.

`Overall_Result.md` and `Overall_Result5.md` are not used as LambdaJS-vs-Node comparison baselines here because they are MIR Direct/C2MIR and direct-string-pointer experiment reports. `Overall_Result2.md` is also left out of the aggregate table because LambdaJS coverage was still partial and included one non-positive timing, making its geometric mean unsuitable as a clean benchmark baseline.

### Interpretation

The inline dense-array fast path is a net positive. The strongest gains landed exactly where expected: dense numeric loops and array-heavy workloads (`matmul`, `mandelbrot`, `cube3d`, `spectralnorm`, `sieve`). The optimization also improved the current LambdaJS/Node geometric ratio from the earlier Round 7 snapshot's **18.43x** to **17.81x** over the same 57 timed benchmarks.

The remaining large gaps are still dominated by object dispatch, allocation pressure, boxed float traffic, and long-running JetStream cases that do not complete under the current harness timeout. The next performance pass should keep P2/P3 from the proposal above as the main targets: shaped-slot field fast paths and broader typed-array/array propagation through object fields.

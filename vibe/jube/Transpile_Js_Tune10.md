# Transpile JS Tune10 Proposal: Execution-Profile Guided Benchmark Tuning

Date: 2026-06-16
Status: proposal

Primary sources:

- Latest benchmark record: `test/benchmark/Overall_Result7.md`
- New execution profiler: `lambda/js/js_exec_profile.h`, `lambda/js/js_exec_profile.cpp`
- Runtime helper instrumentation: `lambda/js/js_runtime.cpp`
- MIR runtime-call-site instrumentation: `lambda/js/js_mir_calls_boxing_types.cpp`
- Sample profile reports:
  - `temp/js_exec_profile_smoke.tsv`
  - `temp/js_exec_profile_awfy_richards.tsv`
  - `temp/js_exec_profile_awfy_deltablue.tsv`

Validation baseline after adding the profiler:

| Gate | Result |
| --- | --- |
| `./test/test_js_gtest.exe` | 196 / 196 passed |
| `make test262-baseline` | 40253 / 40253 fully passing, 0 regressions |

The Tune10 goal is to stop tuning from aggregate benchmark wall time alone and
move to a per-mechanism loop: profile a benchmark, choose the hottest runtime or
MIR helper family, implement one targeted optimization, then reprofile the same
benchmark before running the broad gates.

---

## 1. Profiling Tooling Available Now

The current opt-in profiler is controlled by `JS_EXEC_PROFILE`:

```bash
JS_EXEC_PROFILE=1 ./lambda.exe js test/benchmark/awfy/sieve2_bundle.js --no-log
JS_EXEC_PROFILE=time JS_EXEC_PROFILE_OUT=temp/js_exec_profile_sieve.tsv ./lambda.exe js test/benchmark/awfy/sieve2_bundle.js --no-log
```

Modes:

| Mode | Meaning |
| --- | --- |
| unset / `0` | profiler disabled |
| `JS_EXEC_PROFILE=1` | count runtime helper calls and generated MIR runtime-call sites |
| `JS_EXEC_PROFILE=time` or `2` | also collect inclusive and self wall time per runtime helper family |

Report columns:

| Column | Meaning |
| --- | --- |
| `calls` | runtime executions of an instrumented helper family |
| `inclusive_ms` | time inside that helper including nested instrumented helpers |
| `self_ms` | time in that helper excluding nested instrumented helpers |
| `avg_self_ns` | average self time per call |
| `mir_sites` | number of generated MIR call sites mapped to that helper family |

The report also includes a second section:

```text
# MIR runtime call sites by helper
runtime_call    mir_sites
```

That second section is important because aggregate `other_runtime_call` is too
coarse. It identifies which native helpers the generated MIR still depends on,
even when the helper is not directly timed yet.

---

## 2. What The First Profiles Say

### 2.1 AWFY sieve

`sieve` is a small dense-array and loop benchmark. It is not the worst LambdaJS
case, but it is useful as a sanity check because the expected hot paths are
simple.

Representative profile:

| Event | Calls | Self ms | Notes |
| --- | ---: | ---: | --- |
| `property_set` | 12,102 | 2.197 | dense array writes still route through property-set paths in some forms |
| `property_get` | 4,265 | 0.286 | global/property reads |
| `call_function` | 15 | 0.435 | function dispatch self cost is visible but not dominant |
| `array_get_int` | 4,999 | 0.079 | direct array-get helper is cheap but still a C helper |

MIR-site shape in the same run:

| Runtime helper | MIR sites |
| --- | ---: |
| `js_check_exception` | 60 |
| `push_d` | 16 |
| `js_set_module_var` | 16 |
| `js_args_save` / `js_args_restore` | 14 / 14 |
| `js_property_set` + `js_property_set_v` | 21 |
| `js_property_get` | 2 |

Takeaway: small benchmarks still emit many generic runtime calls. Some are
control-plane calls (`js_check_exception`, args save/restore), while others are
real data-path calls (`js_property_set`, `push_d`, `array_get_int`).

### 2.2 AWFY richards

`richards` is the clearest object/property benchmark in the current profile.

Representative profile:

| Event | Calls | Inclusive ms | Self ms | Avg self ns |
| --- | ---: | ---: | ---: | ---: |
| `property_get` | 14,168,072 | 3,522.891 | 3,522.518 | 248 |
| `property_access` | 7,258,901 | 834.044 | 279.785 | 38 |
| `property_set` | 1,739,497 | 281.396 | 277.796 | 159 |
| `call_function` | 2,252,012 | 21,806.567 | 708.178 | 314 |
| `get_slot_i` | 164,922 | 2.922 | 2.922 | 17 |
| `set_slot_i` | 163,250 | 2.917 | 2.917 | 17 |

Takeaway: `call_function` has huge inclusive time because it wraps execution of
the JS callee body, but its self time is much smaller than `property_get`.
The true runtime-helper bottleneck is object property lookup, not the call
frame itself.

### 2.3 AWFY deltablue

`deltablue` shows a similar shape, but with heavier writes.

Representative profile:

| Event | Calls | Inclusive ms | Self ms | Avg self ns |
| --- | ---: | ---: | ---: | ---: |
| `property_get` | 7,807,115 | 1,242.860 | 1,242.296 | 159 |
| `property_set` | 798,667 | 707.932 | 622.415 | 779 |
| `property_access` | 4,182,924 | 377.505 | 161.554 | 38 |
| `call_function` | 1,182,583 | 20,093.132 | 1,145.168 | 968 |
| `new_object` | 54,250 | 1.229 | 1.229 | 22 |
| `new_object_shape` | 8,116 | 1.796 | 1.796 | 221 |

Takeaway: property writes are much more expensive here than in `richards`, so
Tune10 should treat `property_set` as a first-class target rather than only
optimizing reads.

---

## 3. Tune10 Priorities

Tune10 rolls in every performance proposal from `Overall_Result7.md`, but
reorders them using the execution-profile data. The old proposal was based on
benchmark ratios; the new order is based on measured helper self time and MIR
helper-site counts.

The priority order:

| Tune10 phase | Main target | Overall_Result7 source | Why this order |
| --- | --- | --- | --- |
| P0 | Finish dense-array-read work and profiling coverage | OR7 P1 | Core dense-array read path is already partially implemented and measured; close the loop-hoist/profiling gaps before deeper numeric work. |
| P1 | Shape-guarded property reads | OR7 P2, broadened | `richards` spends about 3.5 s self time in `property_get`; this is the strongest profile signal. |
| P2 | Shape-guarded property writes | OR7 P2, broadened | `deltablue` spends about 622 ms self time in `property_set`; writes are the second concrete OOP hotspot. |
| P3 | Method call devirtualization | OR7 P4 | `call_function` self time is visible, but much of its inclusive time is callee body work; do after field lookup improves. |
| P4 | Typed-array propagation through fields/returns | OR7 P3 | Important for numeric object graphs, but not proven by the first OOP profiles. |
| P5 | Numeric boxing, dense-array boundary cleanup, numeric ADD inference | OR7 P1, P6, numeric slice of P7 | Dense numeric wins are proven; boxing/ADD need more direct instrumentation before broad rewrites. |
| P6 | Pristine builtin fast paths | OR7 P5 | Likely useful for base64/string/array-push workloads, but needs targeted profiles. |
| P7 | Remaining object allocation and GC pressure | remaining OR7 P7 | Real benchmark gap, but too broad until P1-P5 reduce avoidable temporary traffic. |
| P8 | MIR control-plane helper cleanup | supporting work | Broad small win; keep after the data-path bottlenecks unless a profile shows otherwise. |

This deliberately does not start with compile-time tuning. Tune8 and Tune9
already worked heavily on test262 compile/runtime clusters. The benchmark gap
now shows a runtime shape: OOP and numeric-loop benchmarks spend real time in
property helpers and boxed/native boundary helpers.

### 3.1 Overall_Result7 Roll-In Map

| Overall_Result7 proposal | Tune10 handling |
| --- | --- |
| P1 inline dense-array element reads | P0 closes the already-landed core path and audits remaining `js_array_get_int()` uses; P5 handles numeric-loop boxing at dense-array boundaries. |
| P2 stable-field-type shaped slots | P1/P2 broaden this from "make slot helpers faster" to "avoid generic `property_get`/`property_set` entirely under shape guards." |
| P3 typed-array propagation through fields and returns | P4 keeps this as a distinct phase, after object field fast paths establish the metadata needed for `obj.arr[i]`. |
| P4 polymorphic inline caches for method calls | P3 implements it after property lookup costs are reduced enough for call dispatch to become the next clear hotspot. |
| P5 pristine `Array.prototype.push` and builtin fast paths | P6 keeps it, but requires fresh `base64` / `revcomp` / `knucleotide` profiles before implementation. |
| P6 safe numeric ADD inference | P5 includes it with numeric boxing cleanup, because both need the same native-type proof machinery and correctness gates. |
| P7 allocation/boxing pressure | P5 covers numeric boxing first; P7 covers remaining object allocation, shape metadata, and GC pressure after avoidable helper traffic is reduced. |

---

## 4. P0: Finish Dense-Array Read Work And Expand Profiling

### Current issue

The latest `Overall_Result7.md` rerun shows the core guarded inline dense-array
read fast path is already a net win:

| Metric | Result |
| --- | ---: |
| Geometric mean speedup over previous Round 7 JSON | 1.035x |
| Summed runtime improvement | 6.30% |
| Largest directly related win | `kostya/matmul` +20.90% |

However, the original OR7 P1 plan had two parts:

1. use inline dense-array element reads in native numeric expressions;
2. hoist regular-array `items`, `length`, and `capacity` for safe loops.

The first part landed. The loop-hoist part is not yet clearly complete, and
there are still `js_array_get_int()` sites that may be legitimate fallbacks or
missed optimization opportunities.

### Proposed work

P0-A: Audit remaining `js_array_get_int()` MIR sites.

- Use `JS_EXEC_PROFILE=1` to collect helper-name MIR-site counts on
  `sieve`, `matmul`, `cube3d`, `spectralnorm`, and `nbody`.
- Classify every remaining `js_array_get_int()` site as required fallback,
  cold path, or missed inline opportunity.

P0-B: Complete safe regular-array loop hoisting.

- Hoist dense array `items`, `length`, and relevant capacity/shape guards when
  the array is a known plain dense array.
- Keep the slow path for holes, sparse arrays, companion props, `extra != 0`,
  prototype numeric accessors, arguments objects, and in-loop mutation.

P0-C: Extend profiler coverage before P5.

- Add direct timing counters for `push_d`, `it2d`, and `it2i` if their linkage
  and call sites make that safe.
- Add optional counters for object allocation and shaped-object creation sites
  before the allocation phase.

### Measurement

Acceptance:

- Any remaining dense-array optimization reduces `js_array_get_int` calls or
  MIR sites on numeric benchmarks without increasing fallback failures.
- `kostya/matmul`, `beng/spectralnorm`, and `awfy/sieve` do not regress versus
  the latest Round 7 P1 rerun.
- `./test/test_js_gtest.exe` and `make test262-baseline` stay green.

Expected benchmark wins:

- `kostya/matmul`
- `jetstream/cube3d`
- `beng/spectralnorm`
- `r7rs/mbrot`
- dense-array portions of `nbody`

---

## 5. P1: Shape-Guarded Inline Property Reads

### Current issue

`richards` spends about 3.5 s self time in `property_get` in one profiled run.
That is too large to fix with small cleanups inside `js_property_get()`.
The hot path needs to bypass generic property lookup when the receiver shape is
known and stable.

Existing shaped-slot helpers (`js_get_slot_i`, `js_get_slot_f`) are fast when
they fire, but profile data shows they are a tiny fraction of richards runtime:
about 165K calls versus 14.2M `property_get` calls.

### Proposed work

Add a guarded inline property-read path in MIR lowering:

1. Identify monomorphic field reads from constructor-shaped objects.
2. Emit a shape pointer or class id guard.
3. Emit direct field load for stable INT/FLOAT/Item slots.
4. Fall back to `js_property_get()` on guard miss or unstable slot type.

Start with reads where the property key is a compile-time string and the
receiver variable has a known constructor/class entry.

### Files to inspect first

- `lambda/js/js_mir_expression_lowering.cpp`
- `lambda/js/js_mir_calls_boxing_types.cpp`
- `lambda/js/js_mir_function_collection_class_inference.cpp`
- `lambda/js/js_runtime.cpp`

### Correctness gates

Inline reads must not fire when any of these are possible:

- accessor property on the receiver or prototype chain;
- proxy receiver;
- deleted property with prototype fallback;
- `Object.defineProperty` or descriptor mutation after construction;
- class field type instability not covered by the load kind;
- private fields, symbols, or computed dynamic keys;
- cross-realm prototype or intrinsic identity assumptions.

### Measurement

Run before and after:

```bash
JS_EXEC_PROFILE=time JS_EXEC_PROFILE_OUT=temp/js_exec_profile_richards_p1.tsv ./lambda.exe js test/benchmark/awfy/richards2_bundle.js --no-log
JS_EXEC_PROFILE=time JS_EXEC_PROFILE_OUT=temp/js_exec_profile_deltablue_p1.tsv ./lambda.exe js test/benchmark/awfy/deltablue2_bundle.js --no-log
```

Acceptance:

- `property_get` calls or self time drop materially on `richards`.
- No increase in `property_set` self time large enough to erase the gain.
- `./test/test_js_gtest.exe` passes.
- `make test262-baseline` reports 0 regressions.

Expected benchmark wins:

- `awfy/richards`
- `jetstream/richards`
- `awfy/deltablue`
- `jetstream/deltablue`
- `jetstream/splay`
- `larceny/deriv`

---

## 6. P2: Shape-Guarded Inline Property Writes

### Current issue

`deltablue` spends about 622 ms self time in `property_set`, with an average
self cost around 779 ns per call. This is much more expensive than the typed
slot setter helpers, which profile at around 17 ns in `richards`.

The write path must preserve JavaScript's `OrdinarySet` behavior, so a blanket
fast path is unsafe. But a shaped own-data-property write with no accessors,
no proxy, and a stable receiver shape can be much cheaper.

### Proposed work

Add a guarded write path:

1. Prove the write targets an existing own data slot on a known shape.
2. Emit shape guard.
3. Emit direct slot write with stable slot type handling.
4. Preserve write barriers or GC-visible slot updates if the stored value is an
   object/string/container Item.
5. Fall back to `js_property_set()` on guard miss or descriptor instability.

Start with constructor-initialized fields and simple `this.x = value` or
`obj.x = value` stores in collected native functions.

### Correctness gates

Inline writes must fall back for:

- setter on receiver or prototype;
- non-writable data property;
- sealed/frozen/non-extensible object;
- proxy receiver;
- numeric index write to arrays or typed arrays;
- private fields;
- prototype mutation after compile;
- strict-mode assignment errors.

### Measurement

Acceptance:

- `property_set` self time drops on `deltablue`.
- `property_get` does not regress materially.
- Object descriptor tests in `test_js_gtest.exe` remain green.
- `make test262-baseline` reports 0 regressions.

Expected benchmark wins:

- `awfy/deltablue`
- `jetstream/deltablue`
- `awfy/storage`
- `awfy/list`
- `jetstream/splay`

---

## 7. P3: Method Call Devirtualization After Property Fast Paths

### Current issue

`call_function` self time is not the biggest issue in `richards`, but it is
still visible:

| Benchmark | Calls | Self ms |
| --- | ---: | ---: |
| `richards` | 2,252,012 | 708.178 |
| `deltablue` | 1,182,583 | 1,145.168 |

Do not start here: call inclusive time is dominated by callee body execution.
But after property read/write costs fall, dispatch overhead will become more
visible.

### Proposed work

Emit a small shape/class dispatch chain for hot `obj.method()` sites:

1. Guard receiver shape/class.
2. Direct-call the matching native method body.
3. Support 2 to 4 receiver classes per call site.
4. Fall back to current property lookup plus `js_call_function()` on miss.

This can be static first:

- build candidates from class hierarchy and collected method entries;
- only lower when prototype mutation is not observed;
- only for string-literal method names.

Dynamic inline caches can be a later round if static coverage is too narrow.

### Correctness gates

- method override after object creation;
- prototype monkey-patching;
- `super` and home object behavior;
- bound functions;
- accessors returning callables;
- proxies;
- cross-realm function identity.

### Measurement

Acceptance:

- `call_function` self time drops on `richards` or `deltablue`.
- `property_get` call count also drops for method lookup sites.
- No failures in class/prototype/super test262 clusters.

Expected benchmark wins:

- `richards`
- `deltablue`
- `havlak`
- `cd`
- `splay`

---

## 8. P4: Typed-Array Propagation Through Fields And Returns

### Current issue

Typed-array lowering can already return native values when the receiver is a
recognized typed-array local. The limitation is metadata reach. Numeric
benchmarks often store arrays inside objects or return them from helpers:

- `this.pos = new Float64Array(n)`
- `let a = obj.pos`
- factory functions returning typed arrays
- module constants holding typed arrays

When the receiver is not recognized as typed-array metadata, lowering falls
back to generic property and builtin paths. This is exactly the OR7 P3 concern.

### Proposed work

Propagate typed-array type metadata through:

1. constructor assignments such as `this.arr = new Float64Array(n)`;
2. class field reads such as `let a = obj.arr` when the object shape is known;
3. factory function return inference;
4. module constants and immutable bindings;
5. local aliases of typed-array fields.

Then allow `obj.arr[i]` and aliases to lower to raw typed-array loads/stores
using the existing typed-array native helpers.

### Correctness gates

- property replacement after construction;
- typed-array detachment;
- resizable ArrayBuffer length changes;
- subclassed typed arrays;
- aliasing through user-visible property writes;
- prototype or accessor replacement for the field name.

### Measurement

Profile and benchmark:

```bash
JS_EXEC_PROFILE=time JS_EXEC_PROFILE_OUT=temp/js_exec_profile_nbody_p4.tsv ./lambda.exe js test/benchmark/jetstream/n-body.js --no-log
JS_EXEC_PROFILE=time JS_EXEC_PROFILE_OUT=temp/js_exec_profile_cube3d_p4.tsv ./lambda.exe js test/benchmark/jetstream/3d-cube.js --no-log
```

Acceptance:

- More typed-array field or alias accesses reach raw/native paths.
- Generic `property_get` / `property_access` time drops in typed-array-heavy
  numeric benchmarks.
- `test262` typed-array and resizable-buffer clusters remain green.

Expected benchmark wins:

- `jetstream/nbody`
- `jetstream/cube3d`
- `jetstream/navier_stokes`
- `beng/spectralnorm`
- typed-array portions of `matmul`

---

## 9. P5: Numeric Boxing, Dense-Array Boundary Cleanup, And ADD Inference

### Current issue

The profiler does not yet time `push_d` execution directly, but MIR-site counts
show it even in `sieve`:

| Helper | Sites |
| --- | ---: |
| `push_d` | 16 |
| `it2d` | 2 |

The historical performance notes already identify float boxing as a remaining
gap for `nbody`, `matmul`, `mandelbrot`, and `spectralnorm`. OR7 also called
out safe numeric ADD inference as a separate proposal; it belongs here because
it shares native-type proof and boxed-fallback machinery with boxing cleanup.

### Proposed work

P5-A: Time boxing helpers directly.

- Extend `JS_EXEC_PROFILE` to time `push_d`, `it2d`, and `it2i` if safe.
- Use the timing to distinguish "many generated sites" from "actually hot at
  runtime."

P5-B: Delay boxing to observable sinks.

Specific lowering targets:

- regular dense array numeric reads that still call `js_array_get_int()`;
- numeric loop temporaries boxed only to be immediately unboxed;
- float fields loaded through shaped slots and consumed as native doubles;
- return-value paths where the caller accepts native numeric values.

P5-C: Recover safe numeric ADD inference.

- Infer numeric `+` only when both operands are proven non-string and
  non-object at the call site.
- Add fixed-point return inference for self-recursive numeric functions so
  `ack` / `fib` style functions can keep native types across recursion.
- Keep boxed `js_add` fallback for mixed, object, string, BigInt, and Symbol
  cases.

### Correctness gates

- `NaN`, `-0`, `Infinity`, and int/float conversion semantics;
- BigInt and Symbol TypeError cases;
- object `valueOf` / `toString` coercion;
- string concatenation;
- array holes and prototype numeric getters;
- typed-array detachment and resizable buffer checks.

### Measurement

Profile and benchmark:

```bash
JS_EXEC_PROFILE=time JS_EXEC_PROFILE_OUT=temp/js_exec_profile_matmul_p5.tsv ./lambda.exe js test/benchmark/kostya/matmul.js --no-log
JS_EXEC_PROFILE=time JS_EXEC_PROFILE_OUT=temp/js_exec_profile_nbody_p5.tsv ./lambda.exe js test/benchmark/jetstream/n-body.js --no-log
```

Expected benchmark wins:

- `kostya/matmul`
- `beng/spectralnorm`
- `beng/nbody`
- `jetstream/nbody`
- `awfy/mandelbrot`
- R7RS recursion/integer-loop benchmarks touched by numeric `+`

---

## 10. P6: Pristine Builtin Fast Paths After Property Work

### Current issue

Historical profiling showed `arr.push(x)` pays property lookup and override
checks. The new profiler gives a path to quantify that in benchmark runs by
watching:

- `property_get` calls;
- `property_access` calls;
- `array_push` calls;
- MIR helper sites around `js_property_get`, `js_call_function`, and builtin
  dispatch.

### Proposed work

Add realm-scoped pristine flags for selected prototypes:

- `Array.prototype`
- `String.prototype`
- selected typed-array prototypes

When pristine, lower common calls directly:

- `arr.push(x)` to dense append;
- `arr[i] = x` append/grow patterns to direct capacity growth where OR7 P5
  requested it and length semantics are provably safe;
- `str.charCodeAt(i)` / `str[i]` to direct string helpers;
- simple `Array` iteration helpers where callback semantics are not involved.

This must be realm-scoped. A process-global intrinsic cache is unsafe because
test262 multi-realm cases can observe the wrong prototype identity.

### Correctness gates

- prototype mutation flips the pristine flag;
- realm isolation;
- subclass arrays;
- non-writable length;
- sparse arrays;
- accessors and proxies;
- Symbol species where relevant.

Expected benchmark wins:

- `kostya/base64`
- `beng/revcomp`
- `beng/knucleotide`
- `awfy/storage`
- `awfy/list`

---

## 11. P7: Allocation And GC Pressure After Avoidable Traffic Is Reduced

### Current issue

OR7's allocation/GC proposal remains meaningful, but it was too broad to do
first. The allocation-heavy gaps are still major:

- `larceny/gcbench`
- `beng/binarytrees`
- `jetstream/splay`
- `awfy/havlak`
- `awfy/cd`

Before changing allocator or GC behavior, Tune10 should first remove avoidable
generic property, call, array, typed-array, and numeric boxing traffic. Then the
remaining allocation profile will be easier to read.

### Proposed work

P7-A: Add allocation counters.

- Count shaped-object creation, plain object creation, boxed float creation,
  temporary arrays, and function/closure allocations.
- Keep output in the `JS_EXEC_PROFILE` report or a sibling TSV under `temp/`.

P7-B: Pool/cache stable shape metadata.

- Avoid per-instance metadata writes when constructor shape metadata is already
  stable and realm-local.
- Do not share mutable shape entries across objects without clone-on-write
  safety.

P7-C: Reduce temporary object churn.

- After P1-P6, use the remaining profile to identify hot object creation sites
  that can be avoided or delayed without changing object identity.

### Correctness gates

- GC rooting and write barriers;
- observable object identity;
- descriptor mutation;
- `Object.defineProperty`, `seal`, `freeze`;
- realm isolation;
- exception paths that allocate errors.

Expected benchmark wins:

- `gcbench`
- `binarytrees`
- `splay`
- `havlak`
- `cd`

---

## 12. P8: Reduce Hot MIR Control-Plane Runtime Calls

### Current issue

The MIR helper-site section from `sieve` shows a lot of generated calls that
are not data-path operations:

| Helper | Sites |
| --- | ---: |
| `js_check_exception` | 60 |
| `js_args_save` / `js_args_restore` | 14 / 14 |
| `js_eval_local_pop_frame` | 17 |
| `js_check_tdz` | 4 |

Some of these are required, but they should be conditional on the surrounding
code shape. For benchmark hot loops with no throwing helper calls, repeated
exception checks and frame save/restore can be pure overhead.

### Proposed work

Add per-function and per-basic-block effect summaries:

1. During lowering, mark whether an emitted operation can throw.
2. Emit `js_check_exception` only after calls that can actually set the pending
   exception state.
3. Avoid args save/restore for statically direct calls that do not use the
   transient args buffer.
4. Avoid local-frame pop helpers where scope analysis proves no runtime locals
   frame was pushed.

This phase is codegen hygiene. It should not change JS semantics.

### Correctness gates

- exceptions from builtins and property access still propagate;
- `try` / `catch` / `finally` behavior unchanged;
- async/generator yield state unchanged;
- direct native calls with hidden throw paths still checked;
- TDZ checks preserved for uninitialized lexical reads.

### Measurement

Use `JS_EXEC_PROFILE=1` first. The expected signal is a lower `mir_sites` count
for `js_check_exception`, `js_args_save`, `js_args_restore`, and
`js_eval_local_pop_frame`.

Then run `JS_EXEC_PROFILE=time` on short benchmarks to ensure the reduction is
visible in wall time.

Expected benchmark wins:

- broad small wins across all benchmarks;
- larger wins in call-heavy microbenchmarks and tight loops.

---

## 13. Measurement Loop For Every Phase

Each phase should follow the same loop:

1. Capture a focused profile before the change.
2. Implement one optimization only.
3. Build release.
4. Capture the same focused profile after the change.
5. Run the focused benchmark with at least 5 repetitions if the change looks
   promising.
6. Run correctness gates.
7. Keep, revise, or revert.

Recommended focused benchmark set:

| Benchmark | Why |
| --- | --- |
| `awfy/richards2_bundle.js` | `property_get` and method dispatch |
| `awfy/deltablue2_bundle.js` | `property_set`, `property_get`, method dispatch |
| `test/benchmark/awfy/sieve2_bundle.js` | small control benchmark with dense array writes |
| `test/benchmark/jetstream/n-body.js` | numeric fields and float boxing |
| `test/benchmark/kostya/matmul.js` | dense numeric arrays |
| `test/benchmark/jetstream/base64.js` | strings, array push, builtin fast paths |
| `test/benchmark/beng/spectralnorm.js` | dense numeric and boxed numeric traffic |
| `test/benchmark/larceny/gcbench.js` | allocation and GC pressure |

Profile commands:

```bash
JS_EXEC_PROFILE=time JS_EXEC_PROFILE_OUT=temp/js_exec_profile_richards.tsv ./lambda.exe js test/benchmark/awfy/richards2_bundle.js --no-log
JS_EXEC_PROFILE=time JS_EXEC_PROFILE_OUT=temp/js_exec_profile_deltablue.tsv ./lambda.exe js test/benchmark/awfy/deltablue2_bundle.js --no-log
JS_EXEC_PROFILE=time JS_EXEC_PROFILE_OUT=temp/js_exec_profile_sieve.tsv ./lambda.exe js test/benchmark/awfy/sieve2_bundle.js --no-log
```

Correctness gates:

```bash
./test/test_js_gtest.exe
make test262-baseline
```

Performance gates:

```bash
python3 test/benchmark/run_benchmarks.py -e lambdajs -n 5
```

For final reporting, compare against the current `Overall_Result7.md` LambdaJS
snapshot and record the new result in a new section without overwriting prior
sections.

---

## 14. Expected Outcome

Tune10 should make the next round less guessy:

- If P1/P2 succeed, OOP benchmarks should show a visible drop in
  `property_get` and `property_set` self time before any full benchmark run.
- If P3 succeeds, `call_function` self time and method-lookup property gets
  should drop.
- If P4 succeeds, typed-array-heavy object graphs should show fewer generic
  property/builtin helper calls.
- If P5 succeeds, numeric benchmarks should show fewer boxing helper sites and
  less runtime helper time.
- If P6 succeeds, string/array builtin benchmarks should show lower
  property-lookup and builtin-dispatch cost.
- If P7 succeeds, allocation-heavy benchmarks should show lower allocation
  counters and lower GC/memory pressure.
- If P8 succeeds, small benchmark generated helper-site counts should fall.

The highest-confidence first target is P1: `richards` has a measured
`property_get` bottleneck of about 3.5 s self time in a single profile, while
the existing shaped-slot helpers are already cheap when reached. That makes
broader shape-guarded property-read lowering the best first Tune10 experiment.

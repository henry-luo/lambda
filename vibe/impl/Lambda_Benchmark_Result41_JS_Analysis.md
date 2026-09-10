# Result41: LambdaJS performance and shared implementation opportunities

Date: 2026-09-10. Analysis of the checked-in Result38–41 JSON measurements;
source inspection at `57addc5cf`. Informative analysis and proposed work, not a
new semantic or design ruling. This historical analysis did not rerun
benchmarks or change runtime code. The subsequent requested implementation and
fresh release A/B measurements are recorded separately in
[Lambda_Impl_JS2_Performance.md](Lambda_Impl_JS2_Performance.md). Bottleneck
attribution below distinguishes observed paths from causes that still need
profiling.

Authority: **D1.3** (one runtime, profile-owned semantics), **D1.5** (precise GC),
**D2.4.1–D2.4.3** (explicit representation), **D3.3.2v2** (inference does not
create source contracts), **D8.2.4–D8.2.6** (indexed analysis and shared lowering),
**D8.3.1–D8.3.4** (function planning), **D8.4.1v2** (compile-predicted guards,
no inline caches), and **D8.4.3v2** (explicit completions).

## Measurement interpretation

**Result41 is a substantial LambdaJS recovery relative to Result38, with serious
allocation regressions. Results39 and 40 contain no LambdaJS measurements.**
Their Test262 gates are correctness measurements, not JS benchmark results.

| Record | Lambda revision | Samples | Engines timed | Node / QuickJS metadata |
|---|---|---:|---|---|
| 39 | `440c6485d1` | 15 | Lambda untyped/typed, C2MIR, Node | 24.7.0 / 2026-06-04 |
| 40 | `440c6485d1` | 15 | Lambda untyped/typed, C2MIR, Node | 24.7.0 / 2026-06-04 |
| 41 | `cbd220d923` | 3 | Above plus LambdaJS and QuickJS | 22.13.0 / 2025-09-13 |

39 and 40 record the same commit and executable size, but different archived
SHA-256 hashes; do not call them byte-identical. Result41 has no suite cooldown,
versus 10 seconds in 39/40. Its eight Lambda timing cells for `awfy/havlak` and
`jetstream/navier_stokes` were replaced from a later `086c8685a6` run. Its JS
cells were not replaced. Only Darwin arm64 is recorded as machine identity.

Calculated from positive paired timing cells, using
`exp(mean(log(new_ms / old_ms)))`; lower means less elapsed time:

| Engine | R40/R39 | R41/R40 | R41/R39 |
|---|---:|---:|---:|
| Untyped Lambda | 0.989 | 1.352 | 1.337 |
| Typed Lambda | 1.029 | 1.441 | 1.483 |
| C2MIR reference | 1.006 | 1.033 | 1.039 |
| Node, 62 matched rows | 0.991 | 1.346 | 1.335 |

The other columns have 63 pairs. 39/40 are close in aggregate. Comparing 41
with 40, untyped Lambda and Node both take about 35% longer geometrically;
therefore a roughly unchanged Lambda/Node ratio hides substantial absolute
movement. Node changed versions, and several typed Lambda sources changed too,
so these are observations, not isolated runtime regression measurements.

Some untyped Lambda changes are far larger than plausible ordinary timing
noise: `havlak` 55.943 → 2,492.88 ms (44.6×), `navier_stokes` 154.741 →
1,760.02 ms (11.4×), and `quicksort` 0.992 → 11.249 ms (11.3×). The first two
are the later refreshed cells. Investigate these separately before making the
Result41 untyped column a tuning target. JS beating Lambda on `navier_stokes`
is partly a comparison against this degraded Lambda result.

Sources: [R39 JSON](../../test/benchmark/benchmark_results_v39.json),
[R40 JSON](../../test/benchmark/benchmark_results_v40.json),
[R41 JSON](../../test/benchmark/benchmark_results_v41.json),
[R41 report](../../test/benchmark/Overall_Result41.md).

## The actual JS trend

Result38 is the preceding checked-in report with JS measurements. It records
the same Node and QuickJS versions as 41, three samples, and 63 JS rows. Git
shows no changes to those benchmark JS source files between its `b23793e832`
revision and Result41; the added JS microbenchmarks are outside these rows.
This is a much better JS comparison than 39/40, although not an interleaved
same-machine A/B experiment.

| Measure, workload timing | Result38 | Result41 |
|---|---:|---:|
| LambdaJS / Node geometric mean | 31.50× | 20.45× |
| LambdaJS / QuickJS geometric mean | 4.35× | 2.83× |
| Result41 / Result38 JS time | — | 0.652× |

That is **34.8% less JS time geometrically, or a 1.53× speedup**; 46 of 63 rows
improve. Result41 has successful timings for all 63 rows and records a
40,261/40,261 Test262 baseline gate. JS beats QuickJS on nine rows and Node on
one (`pidigits`, a short workload).

| JS workload | R38 ms | R41 ms | Change |
|---|---:|---:|---|
| kostya/primes | 3,724.659 | 135.455 | 27.5× faster |
| beng/spectralnorm | 314.531 | 38.742 | 8.12× faster |
| awfy/sieve | 10.108 | 1.256 | 8.05× faster |
| awfy/cd | 25,932.505 | 4,103.861 | 6.32× faster |
| text/prettier_ast | 27,865.565 | 5,749.786 | 4.85× faster |
| awfy/havlak | 82,502.984 | 18,392.906 | 4.49× faster |
| beng/binarytrees | 164.103 | 779.828 | 4.75× slower |
| larceny/gcbench | 4,063.758 | 18,719.297 | 4.61× slower |
| jetstream/splay | 1,036.018 | 1,782.084 | 1.72× slower |
| text/text_search | 95,942.930 | 114,130.766 | 1.19× slower |

Result41 end-to-end JS/Node is 4.92×, versus 20.45× for workload time. These
answer different questions; fixed launch costs compress the end-to-end ratios
for short programs. Parser/cache work affects startup and compilation, whereas
the larger execution gaps require changes in generated operations and runtime
paths.

Source: [R38 JSON](../../test/benchmark/benchmark_results_v38.json).

## What untyped Lambda demonstrates—and what the suite does not

The raw 63-row JS/untyped geometric ratio is 13.90×, but it is not an honest
same-workload aggregate:

* [microdiff.ls](../../test/benchmark/text/microdiff.ls) sums precomputed change
  type/path lengths; [microdiff.js](../../test/benchmark/text/microdiff.js)
  traverses and diffs nested snapshots. The resulting 6,718× ratio is invalid
  as an engine comparison, even though the checksums agree.
* [hyphen.ls](../../test/benchmark/text/hyphen.ls) applies vowel/consonant rules;
  [hyphen.js](../../test/benchmark/text/hyphen.js) runs Liang-pattern hyphenation.
  Its 100× ratio also compares different algorithms.

Removing these two known mismatches leaves 12.16× across 61 rows. This is a
screened descriptive number, not certification that every remaining port is
equivalent. JS/Node and JS/QuickJS comparisons still use the same JS programs
on these two rows and remain useful.

| Result41 workload | Untyped Lambda ms | LambdaJS ms | JS/Lambda |
|---|---:|---:|---:|
| r7rs/fib | 1.526 | 47.204 | 30.9× |
| r7rs/sum | 1.200 | 26.038 | 21.7× |
| r7rs/fft | 0.285 | 22.722 | 79.7× |
| beng/binarytrees | 8.909 | 779.828 | 87.5× |
| larceny/gcbench | 204.644 | 18,719.297 | 91.5× |

The R7RS suite ratio is 26.39×. The unannotated Fibonacci and summation sources
make the opportunity particularly clear: user-written type annotations are
not a prerequisite for much faster code. Lambda already recovers useful
representation and call facts that JS lowering often loses or cannot exploit.
The common MIR backend does not establish a precise speed ceiling—semantics
and ports differ—but the gap is ample reason to inspect frontend lowering.

## Tuning priorities

### 1. Investigate the tree allocation regression first

There is a concrete source-level explanation to test, beyond a generic claim
that “GC is slow.” Both tree JS programs use `{left: null, right: null}` leaves.
The predicted literal shape preinstalls those entries, but
[`js_predicted_slot_initialize`](../../lambda/js/js_runtime.cpp) explicitly
returns false for a null value. In
[`js_create_data_property`](../../lambda/js/js_globals.cpp), `key_exists` then
also prevents the ordinary new-key fast path, and the operation proceeds to
allocate/populate a descriptor object and call `js_object_define_property`.

Thus a very common leaf initialization demonstrably takes descriptor machinery
that numeric literal microbenchmarks avoid. This is a strong candidate for the
matching 4.6–4.8× tree regressions, **not a measured attribution of their full
cost**. Profile allocation counts, descriptor creation, shape detachment and
GC time on the actual release revision, then test the generic null-slot
initialization contract. Do not special-case a benchmark or blindly treat a
typed shared slot as null: sibling instances and GC tracing must remain valid.

### 2. Complete shared shape propagation and guarded field operations

Several Tune10 changes are already present: numeric-key helper emission,
ordinary property-add shortcuts, shared shape machinery, literal pre-shaping,
the arguments fix, and guarded literal reads. The Tune10 introductory status
and Formal Design appendix status are older than the implementation; its §12
and the source describe the literal support that actually landed.

The remaining gap is concrete:

* JS's `jm_emit_predicted_shape_get` only follows an identifier to its own
  object-literal declarator. It emits a **C helper call** to
  `js_shaped_slot_get`; parameters and function returns get no such candidate.
* Lambda's `mir_expr_candidate_shape` follows bindings, parameters and callee
  return shapes. `mir_module_unique_shape_for_field` supplies a guarded
  candidate when one module shape owns a field name, including cases reached
  through untyped containers. Lambda also emits guarded direct reads/writes.
* JS still needs constructor-prefix handling, the guarded-store counterpart,
  and inline guard/load/store emission. Constructor preallocation requires
  correct unwritten-property visibility when `this` escapes or calls user code.

Extract the existing candidate propagation and physical slot operations for
both clients. JS supplies its descriptor/prototype/exotic admission checks and
its fallback; Lambda supplies its own mutation/COW policy. Preserve runtime
shape checks: a candidate is not a proof. **D8.4.1v2**, **D8.2.4–D8.2.6**,
**D3.3.2v2** govern this work. Target `havlak`, `cd`, `nbody`, `splay`, and tree
traversal. Do not reintroduce mutable per-site caches.

Sources: [JS lowering](../../lambda/js/js_mir_expression_lowering.cpp),
[Lambda lowering](../../lambda/runtime/transpile-mir.cpp),
[Tune10 §12](../jube/JS_Tune10_Fast_Paths.md#12-round-5--t10-2-items-1-and-2-for-object-literals).

### 3. Move dense numeric indexing into generated code

The numeric-key recovery is real, but the current path still converts an
eligible native key to F64, calls `js_get_number_reference`, checks finite /
nonnegative / integral / index range, and calls the element kernel returning an
Item. It avoids numeric-to-string conversion; it does not yet produce a native
array load. Stores similarly use `js_set_number_assignment`.

Share index representation/range facts, element-lane loads and stores, and
loop-invariant proof handling with Lambda. Keep a proven integer index integral
instead of converting it to a double and rediscovering integrality. Preserve
JS holes, inherited indices, accessors, strict-store failures, typed-array
detachment/resizing and coercion behavior on misses. Hoist a proof only across
operations that cannot invalidate it. A JS array's inferred numeric contents
are not a Lambda declared-array certificate (**D3.3.3v3**, **D2.4.3**).

This targets `fft`, `fannkuch`, `array1`, `matmul`, `quicksort`, and
`text_search`. The last consumes 114.13 seconds, **47.0% of the sum of JS row
medians**; its timed search loops index precomputed code arrays, so replacing
string conversion alone would miss the central workload. The whole text suite
accounts for 66.9% of that sum. These shares describe this suite's weighting,
not an application workload distribution.

Sources: [numeric key runtime](../../lambda/js/js_props.cpp),
[text search workload](../../test/benchmark/text/text_search.js).

### 4. Use one function plan and completion path for direct recursion

JS explicitly disables direct non-tail self calls so the runtime call-depth
RangeError remains catchable. That means Fibonacci repeatedly crosses the
dynamic call boundary despite a statically known callee. This is an observed
lowering choice, not a hypothesis that the function simply lacks inference.

Move the required depth/recovery check into a shared direct-call/entry protocol
with JS's error policy, then use proven native or direct boxed entries for
eligible recursive calls. Retain binding stability, argument evaluation order,
guard failure, precise roots and ordinary error propagation. Arbitrary methods
still require observable Get-before-Call; an inferred receiver class cannot
select a method by spelling (**D6.2.2v2**, **D8.3.2–D8.3.4**).

A related simplification is unusually concrete: JS native paths still emit
`js_native_throw_publish` / `js_native_throw_take`, which write and clear
`async_await.native_throw_lane`. This is an extra pending-error protocol beside
the shared completion machinery. Migrate it to the existing planned
normal/error companion result, including the boxed wrapper. That aligns with
**D1.4v3**, **D5.2.1v3**, and **D8.4.3v2** while removing adapters and polls.
Do not merely delete them; they currently preserve thrown errors.

Sources: [direct-call selection](../../lambda/js/js_mir_function_collection_class_inference.cpp),
[call lowering](../../lambda/js/js_mir_expression_lowering.cpp),
[native function lowering](../../lambda/js/js_mir_function_class_lowering.cpp),
[throw transport](../../lambda/js/js_runtime.cpp).

## How to share more implementation

The repository already shares `Item`, heap/GC/side stacks, the indexed AST
foundation, `FnAnalysis`, `MirValue`, `MirEmitter`, and significant call,
completion, destination and module lowering. RootVector and context-capsule
consolidation are also present. The next step should extract missing shared
operations from the two working clients, as **D8.2.3** requires, rather than add
another intermediate representation or restart AST unification.

| Common implementation to extend | Profile-owned decisions |
|---|---|
| ID-keyed representation, shape and effect facts | JS coercion, escape/rebinding semantics; Lambda contracts |
| Slot layout, scalar storage, shape guards, allocation initialization | JS property descriptors/prototypes; Lambda COW and admission |
| Dense element address/load/store and proof invalidation | JS holes, exotics, detached buffers; Lambda bounds/errors |
| Function planning, direct-call ABI, roots and companion completion | JS `this`, arguments, call depth, Get-before-Call, throws |
| String buffer/search and other compatible leaf algorithms | UTF-16/code-point indexing and language-visible conversions |

Share below semantic admission, as **D1.3** requires. For example, Lambda's
zero is truthy (**S3.1**), JS's numeric zero is falsy; Lambda captures are
immutable snapshots (**S9.1.4**, **D6.2.3v2**), JS captures lexical bindings by
reference. These cannot be unified by routing JS through Lambda's operators or
closure semantics. Representation conversion is not coercion (**D2.4.3**).

Rooting improvements should use the existing helper effect/ownership catalog
and emitter liveness, rather than a blanket removal of root stores or scalar
adoption. JS helpers can return module-owned scalar homes; the formal
**D5.2.3** implementation note records a failed global removal. Prove individual
ownership contracts, then let both frontends benefit from the same optimization.

## Suggested acceptance sequence

1. Repair the two incomparable ports or remove them from cross-language
   aggregates. Record source and binary hashes, machine identity, iterations,
   checksums, tier and dependency versions. Keep mixed-revision refreshes visible.
2. Profile and A/B the tree null-initialization path on a release build; require
   gains on both tree rows plus correctness for heterogeneous/null shared slots.
3. Extract shared shape facts and field lowering, with a JS fallback; measure
   ordinary object workloads and guard-hit coverage.
4. Extract dense indexing and loop proofs; measure numeric kernels and the
   long-running text-search workload.
5. Consolidate native completion and guarded recursive entry; measure R7RS.

Use interleaved release A/B runs with sufficient samples, and keep cold/compiler
time separate from workload time. For each code change, require Lambda baseline,
the recorded Test262 baseline, focused semantic cases, and precise-GC stress
where ownership changes. MIR emission checks should verify the intended
operation disappears from the hot loop, while timing verifies the payoff.
Track guard hits, helper calls, boxes, descriptor/shape allocations and root
traffic as well as elapsed time. No speedup target here is claimed as measured
until that experiment is performed.

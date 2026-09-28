# JS MVP release-profile comparison and further LambdaJS tuning

**Date:** 2026-09-28. **Status:** build integration and MVP comparison complete;
the first two numeric-path items now have a guarded production implementation
and ordinary-release A/B evidence. Array consumers, property/call cost, and
string/RegExp materialization remain investigation targets.

Authority: [Formal Semantics](../../doc/Lambda_Formal_Semantics.md) **S1.11**
(hosted JS follows ECMAScript); [Formal Design](../../doc/Lambda_Formal_Design.md)
**D2.4.3**, **D3.3.2v2**, **D5.3.1–D5.3.5**, **D8.4.1v2–D8.4.3v2**.
No normative ruling changes are proposed.

## 1. Result and scope

`make build-release-profile` now links MVP into `lambda-profile.exe` under
`NDEBUG`, O3 and LTO. The CLI accepts `js --runtime=mvp` in that configuration;
ordinary release still excludes MVP. The JSON build configuration controls
profile-specific definitions and source exclusions; generated Lua is not edited.

The [complete report](../../test/benchmark/js_mvp/profile_20260928/README.md)
attempts all 63 canonical workloads. **62 validate against Node and have five
alternating full/MVP process pairs**: 620 accepted measured samples, plus
preflight/reference runs. Across those 62 rows:

| Measure | Full LambdaJS / MVP |
|---|---:|
| Execution-time geometric mean | **1.692×** |
| Ratio of summed execution medians | **1.580×** |
| Process-wall-time geometric mean | **1.921×** |
| Rows won | Full JS **14**, MVP **48** |

These are **62-row comparable-population results**, not a passing 63-row
acceptance score. Both backends use one archived executable. Full JS explicitly
selects `JS_EXECUTION_BACKEND=mir`, not the shipped AUTO default. Timers exclude
startup/JIT; wall times include them. Counter collection was disabled during
timing, but dormant full-JS profiling hooks remain compiled in. An ordinary
release must be used for accepting a future production optimization.

Node supplies output oracles, not a new Node performance column. Source/input
hashes, exact executable hash, all samples, and alternating order are in the
[raw artifact](../../test/benchmark/js_mvp/profile_20260928/comparison.json).
All three engines receive identical JetStream wrappers preserving source strict
mode. The current Tune14 manifest verifies all 63 source/input contracts.
Five pairs provide diagnostic evidence; small sub-millisecond differences should
not be treated as robust optimization wins.

## 2. The MVP correctness boundary matters

Navier–Stokes's post-timing frame-15 check fails in MVP with
`MVP member write result on undefined is unsupported`. Full JS and Node both
produce the expected density digest `-257786486`.

The source's `checkResult` is a non-strict plain function which writes
`this.result`. MVP's direct-call lowering passes `undefined` as the receiver
for non-method calls in
[`mvp_generic_emit_call`](../../lambda/js/mvp/mvp_generic_mir.cpp).
The independent minimal probe `function f(){this.result=1;return this.result;}`
followed by `console.log(f())` succeeds in full JS/Node and fails in MVP.
Thus the stronger oracle exposes a receiver-semantics limitation before it can
establish numerical parity. The failed row has no accepted timing ratio; its
source or oracle was not weakened.

Two further [fresh probes](../../test/benchmark/js_mvp/profile_20260928/semantic_probes.json)
show why the MVP implementation cannot simply replace the full engine:

| Probe | Full JS / Node | MVP |
|---|---|---|
| `function add1(x){return x+1;} add1("2")` | `21` | `3` |
| `new Uint8Array(1)`, then store `257` and read | `1` | `257` |

In particular, MVP's numeric entry uses `mvp_to_number` on arguments. Transfer
its compact physical operations only behind genuine guards, preserving original
values for the semantic fallback (**S1.11**, **D2.4.3**, **D3.3.2v2**).

## 3. Priorities from current evidence

The largest absolute gaps are more useful for throughput planning than ratios
alone. They are opportunities to investigate, not promises that full semantics
can attain MVP's time.

| Workload | Full JS ms | MVP ms | Full / MVP | Gap ms |
|---|---:|---:|---:|---:|
| text/text_search | 43,950.905 | 19,328.751 | 2.274× | 24,622.154 |
| awfy/havlak | 13,650.036 | 6,534.590 | 2.089× | 7,115.446 |
| text/three_way_merge | 9,905.909 | 5,688.243 | 1.741× | 4,217.666 |
| larceny/triangl | 6,223.108 | 2,940.461 | 2.116× | 3,282.647 |
| awfy/cd | 4,130.178 | 1,782.946 | 2.316× | 2,347.232 |
| text/log_pipeline | 14,091.013 | 11,968.733 | 1.177× | 2,122.280 |

### First: finish numeric paths through loop state and array consumers

This is the strongest current target. A separate 12-workload counter/MIR survey
exited zero throughout; its [evidence](../../test/benchmark/js_mvp/profile_20260928/diagnostics.json)
is outside the timing population. Dynamic counts cover the whole invocation,
including setup; compile-time admission counts are not dynamic executions.

`text_search` records **690,478,674** Number arithmetic-head hits and
**1,076,027,644** Number comparison-head hits. It also records **195,211,930**
existing-own-array-element hits. Array specialization is already working in
many places; boxed arithmetic and comparisons remain around it. Finalized MIR
for `naiveSearch_n` and `kmpSearch_n` still contains `js_cmp_raw`, equality,
subtraction and array helper calls. `boyerMooreSearch` has only a boxed body.

The numeric microbenchmarks isolate another part of the same problem:

- R7RS `mbrot`: **13.751×**. `count_n` exists, but its `r` and `i` parameters
  remain Item operands while `step`, `x`, `y` are doubles. The supposedly native
  body still has five `js_add`, four `js_multiply`, one `js_subtract` and one
  `js_cmp_raw` static call site. The run executes 1,355,130 Number arithmetic
  helper hits.
- AWFY `mandelbrot`: **17.635×**. Its native entry already takes a double
  parameter, yet its loop retains boxed operations: **65,842,377** arithmetic
  head hits and **8,195,047** Number comparison hits. Creating a native entry
  alone is not the missing optimization.
- `sum` is only **1.036×** and records one arithmetic head hit outside its
  native loop. It is a useful positive control for the intended generated form.

Extend the existing function-owned inference and `MirNumericOpPlan` routes in
[`js_mir_function_collection_class_inference.cpp`](../../lambda/js/js_mir_function_collection_class_inference.cpp),
[`js_mir_module_batch_lowering.cpp`](../../lambda/js/js_mir_module_batch_lowering.cpp)
and [`js_mir_expression_lowering.cpp`](../../lambda/js/js_mir_expression_lowering.cpp):

1. Separate guarded Number-entry candidates from source contracts. The current
   operand-local evidence misses `r + x * step`; guard the additional candidate
   inputs rather than coercing them or assuming `+` is numeric.
2. Solve loop-carried local facts across dependent bindings and writes. The
   current recursive binding proof seeds an active binding as invalid until its
   initializer resolves; inspect the mutual `zr`/`zi` dependencies and use a
   bounded joint proof for initialized Number state. Preserve widening on any
   non-Number write and definite-initialization checks.
3. Carry guarded array values and lengths through comparisons, index updates,
   arithmetic and stores without intermediate boxing. Reuse current packed,
   ArrayNum and typed-array guards. A hole, descriptor, proxy, detached buffer,
   changed kind or coercing operand must enter the existing semantic path.

**Exit evidence:** hot-loop MIR loses the identified arithmetic/comparison
helpers and unnecessary scalar materialization; dynamic helper counts fall;
Number/non-Number guard probes and output oracles pass. Test `fft`, `base64`,
`triangl` and `three_way_merge` as coverage candidates, not assumed wins.

### Second: reduce the cost of successful property accesses and calls

Havlak has **52,295,421** named probes, **50,315,057** hits, and **24,159,104**
reduced `this` activations. `log_pipeline` has **42,092,166** named probes with
only four misses. Increasing hit rate alone cannot address these workloads.
`microdiff` is **5.455×** slower than MVP and has 28,672 non-receiver-kind
fallbacks, which merits a separate classification before changing admission.

Audit the ordinary hit from `js_get_name_id` through `js_named_fast_lookup`,
field reading/writing and completion handling in
[`js_runtime.cpp`](../../lambda/js/js_runtime.cpp). Pass already-proven key,
receiver and layout facts through the shared kernel instead of reclassifying
them. Use existing compile-predicted shapes for direct scalar/tagged slots.
Do not call a field read `NO_GC` when it can materialize a scalar.

For `js_call_mir_this_direct`, distinguish callee effects that require current
`this`/home installation, argument ownership and root setup from cases where
those operations are provably redundant. Reuse function capability metadata
and caller-rooted arguments. Keep realm changes, re-entry, accessors, indirect
callee changes and stack/error behavior correct. No mutable call/shape caches
are permitted (**D8.4.1v2**); preserve precise roots and returned completions
(**D5.3**, **D8.4.3v2**).

**Exit evidence:** measure hit-path cost as well as hit rate across Havlak,
Bounce, CD, Richards, hashmap and log_pipeline. Counter frequency is not a
CPU-time attribution, so require an isolated release A/B before claiming gain.

### Third: reduce string and RegExp materialization inside existing paths

`log_pipeline` executes **2,160,000** ASCII splits, **28,800,000** ASCII slices,
**21,600,000** numeric-index helper calls and **6,480,000** ToNumeric calls.
Its substring reuse already has 24,663,929 hits; adding another cache would
duplicate an existing mechanism. Inspect split-result element access, copying,
numeric parsing and dispatch together before selecting a small shared change.

`regexredux` is **14.002×**, but its absolute gap is only **7.931 ms**. It already
uses the bulk path for all nine matches and 13 replacements. The bulk loop
still invokes `js_regexp_update_last_match`, which materializes match state,
and the general replacement-template helper on each match. Investigate lazy
materialization or a proven literal-template route while preserving observable
legacy RegExp state, exceptions and custom `exec`/symbol hooks. Do not repeat
the historical proposal merely to add bulk RegExp execution.

This is a secondary throughput priority, with useful cross-workload coverage in
`base64`, `hyphen`, `knucleotide`, `fast_diff` and `log_pipeline` to be measured.

## 4. Preserve demonstrated strengths and accept changes with evidence

Full JS is already faster on `collatz` (0.173× MVP), `ray` (0.358×), `array1`
(0.402×), `gcbench` (0.405×), `matmul` (0.621×), `pidigits` (0.648×), `quicksort`
(0.723×), `primes` (0.783×), and `prettier_ast` (0.792×). Retain these as
regression controls. The results do not support replacing `Item`, the heap or
the array representation wholesale.

The old quadratic Map order-list update has already been replaced by hash
entries identifying stable ordered nodes. Number heads already precede generic
root setup. The next work should extend or simplify those implementations,
not propose them as absent features. This follows the remaining coverage and
operation-boundary goals in [Tune16](../jube/JS_Tune16.md).

For each proposed change, freeze an ordinary-release control and candidate;
preserve source/input/wrapper and binary hashes; run interleaved equal-output
pairs on the affected rows and the controls above. Keep compile-time admission
counts separate from dynamic operations and wall time separate from execution.
Use finalized MIR to establish the mechanism and longer paired runs to accept
small gains. Finish with the full canonical matrix, explicit failed-row
accounting, forced-GC checks for changed ownership/effects, and
`make test-lambda-baseline` plus `make test262-baseline`. A `NO_GC` leaf requires
transitive effect verification (**D5.3.2**), not just benchmark survival.

Build and validation outcomes for this integration are retained in
[validation.json](../../test/benchmark/js_mvp/profile_20260928/validation.json)
and the report: Lambda baseline **5,996/5,996**, MVP unit tests **51/51**, and
Test262 **40,261/40,261** with zero regressions and no retries. The release-profile
build, isolation/manifest audits and CLI/harness checks pass. The Premake
self-test stops at an existing Linux multiarch probe on macOS, reproduced with
the unmodified generator; that does not prevent the actual profile build.
The numeric-path implementation and its validation are recorded in the
[follow-up report](../../test/benchmark/js_mvp/tune_20260928/README.md).

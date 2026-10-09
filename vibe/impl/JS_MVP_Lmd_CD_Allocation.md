# JS MVP CD allocation and shape stability

Date: 2026-10-09. Follow-up to [guarded numeric regions](JS_MVP_Lmd_Numeric_Regions.md).

## Diagnosis and changes

A fresh native sample attributes 47.4% of inclusive CD CPU time to tree insertion
and rebalancing, versus 6.9% to GC. These percentages overlap with callees and
must not be added. Dynamic counters record 202,396 property-helper fallbacks for
`treeInsert`'s unchanged `key` field. Independent nullable links multiply receiver
shapes, and one class-wide allocation template oscillates between different
construction sites' field types.

- A proven receiver-unobserving constructor records its literal-null fields.
  When one becomes a Map link, select a widened allocation template through
  Lambda's existing `type_tree_retype_field`. Later instances initialize that
  field to zero in Lambda's existing nullable Map pointer lane. Parameter-derived
  nulls keep their original behavior; extra instance fields never enter the
  allocation template. Existing objects and shared shapes remain unchanged
  (**D3.4.3v5**, **D3.4.5**).
- Inlined construction sites retain their completed shared shapes. Learning a
  new nullable link changes a class epoch and invalidates older site templates.
  Constructor identity guards, ordinary retyping and the generic construction
  path remain authoritative. The existing precise root machinery covers all
  allocations (**D5.2–D5.3**, **D6.2.1**).
- Preserve the RHS nullability already known by the compiler, including boxed
  Number/string addition results. Emit the nullable-store exception only when
  null remains possible, on the existing type-mismatch edge.
- Extend the existing guarded numeric inliner to bounded, allocation-free direct
  function bodies with multiple field reads. Restrict expansion to calls inside
  functions. Snapshot arguments before guards and reuse the original values on
  fallback; retain the region's existing purity and layout proof.

The one new internal helper, disclosed before implementation, is
`mvp_lmd_class_widen_allocation`. It reuses Lambda's shape transition API; no new
JIT import or object representation is introduced. Site caches use 16 bytes per
inlined construction site; nullable-field metadata belongs only to class plans.

An earlier experiment also expanded allocation-containing numeric methods.
Its small workload gain increased CD cold compilation time substantially, so
that experiment was removed. Artifacts preserve it as `inline-screen`.

## Evidence

Artifacts: `temp/mvp_cd_structure_20261009/`. The control is the incoming release
at `024f2a12c`, SHA-256
`f85933d635f3cdcb60f69b2fc9717dd8195bd50133fd944b3e6ba406ed03c96f`.
Diagnostic instrumented binaries use **D5.4.4** counters; their timings are not
performance evidence. Native sampling repeats the unchanged CD kernel 80 times
(344,400 collisions) and captures an eight-second interval. Performance runs
use unchanged inputs and checked output, release
binaries, pinned native MIR, fresh processes, balanced control/candidate/peer
order, and self-reported workload times. Initial compilation is excluded from
workload time and recorded separately as process wall time. Node tiering within
the workload remains included.

The final release candidate is SHA-256
`2756f62f430544be114769d8ec133af30d58befc5818e3d7279079a8dafe8f40`.
`accepted/manifest.json` freezes the binary and sources; `accepted-profile/`
contains the diagnostic compiler and counters. Earlier complete benchmark runs
are retained under `pre-mismatch/` and `pre-kind-guard/`; their relocation notes
explain the original paths in their records.

### Structural evidence

Single unchanged CD kernel, 4,305 collisions, no counter overflow:

| Counter | Before | After |
|---|---:|---:|
| `treeInsert` `key` property-helper fallbacks | 202,396 | 1,013 |
| `isInVoxel` function entries | 435,072 | 2 |
| Total function/frame entries | 2,609,469 | 2,174,399 |
| Helper calls | 6,497,321 | 4,823,132 |
| Root reloads | 14,885,948 | 9,850,703 |

The late native sample, before the final store-path cleanups, attributes 40.4%
of inclusive time to `RedBlackTree.put` and 9.8% to GC. Tree traversal,
polymorphic field/method guards and boxed call/return boundaries remain larger
targets than GC alone. `Node`'s parameter-derived `value` still changes between
Boolean and Map layouts; the counter records 82,438 slow initialization stores.
Effectful `treeInsert` result objects and recursive vector arguments still
materialize. Further work should specialize those mutable traversal loops and
call/return boundaries while preserving fallback progress and object identity;
the present change does not claim to eliminate those costs.

### Final workload timings

Self-reported milliseconds, medians of 15 runs per lane, Node v22.13.0:

| Workload | Before | After | Change | Node |
|---|---:|---:|---:|---:|
| CD | 243.194 | 195.086 | 19.8% faster | 35.677 |
| Richards | 80.628 | 75.242 | 6.7% faster | 8.239 |
| NBody | 44.957 | 45.017 | within noise | 5.343 |
| Bounce | 0.817 | 0.826 | confirmation below | 0.726 |
| Storage | 0.514 | 0.503 | interval reaches parity | 0.685 |
| SHA1 | 13.741 | 13.732 | within noise | 8.732 |

CD is **5.47× Node**, **1.084× typed Lambda** (180.049 ms), and **3.11× faster
than the canonical Lambda reference** (606.374 ms). The typed port uses its
native tables rather than the JS red-black-tree representation, so this does
not isolate the effect of typing. Richards is 75.242 ms versus typed Lambda's
78.260 ms. MVP/Node include class setup and verification; Lambda clocks the
benchmark kernel. The frozen sources and commands are recorded in
`accepted/new6/comparison.json`.

CD's candidate/control 95% interval is 0.794–0.810. The identical-control peer
is 0.982–1.000, showing a roughly 1% measurement floor; the improvement is much
larger. Cold process medians are **3,182.5 → 3,143.7 ms** for CD and
**644.7 → 641.0 ms** for Richards. NBody remains **388.2 → 388.2 ms**.
All 60 workloads pass their unchanged oracles in 15-run comparisons. No other
workload has a candidate/control interval wholly above parity. Shared Lambda
`fib`, `gcbench` and `json_gen` pass 30 paired runs each; their intervals include
parity. This does not establish absence of smaller regressions.

### Regression follow-up

An initial Bounce screen showed about a 1% slowdown. Nullable stores now branch
only on a type mismatch, preserve the compiler's RHS nullability facts, and
skip allocation-policy work for classes without nullable initializers. Its
constructor, random-number method, hot loop and benchmark method match the
control MIR after label/address normalization. A 30-run final confirmation still
measures **0.8170 → 0.8245 ms (+0.92%)**, with a candidate/control 95% interval
of **1.0061–1.0147**; the identical-control peer interval includes parity.
This remaining 7.5 μs cost is unresolved. Unchanged MIR does not establish its
cause, and regression-free performance acceptance remains open.

### Correctness gates

The 65 focused release tests pass both normally and with forced collection and
freed-memory poisoning. New coverage includes allocation-template widening,
site invalidation, extra instance fields, deletion, later type changes,
parameter-derived nulls, and guarded numeric fallback with early returns.
The Lambda/input baseline passes **6,485/6,485** tests. Test262 passes
**40,261/40,261**, with two workers, zero retries, and all 169 batches completing
successfully. The exception-helper catalog check also passes. The workspace
release binary matches the measured SHA-256 after the gates, and all frozen
source hashes remain unchanged (`accepted/closeout-hashes.json`).

Commands and logs are retained in `accepted/validation.json`:

```sh
make build-release-compile
make -C build/premake config=release_native test_js_mvp_lmd_gtest -j8
JS_EXEC_BACKEND=mir JS_MIR_INTERP=0 ./test/test_js_mvp_lmd_gtest.exe
LAMBDA_GC_FORCE_EVERY=1 LAMBDA_GC_POISON_FREED=1 \
  JS_EXEC_BACKEND=mir JS_MIR_INTERP=0 ./test/test_js_mvp_lmd_gtest.exe
make test-lambda-baseline
make test-js-exception-catalog ensure-test262-gtest
make build-release-compile
./test/test_js_test262_gtest.exe --baseline-only --batch-only --run-async \
  --async-list=test/js262/test262_baseline.txt --jobs=2
```

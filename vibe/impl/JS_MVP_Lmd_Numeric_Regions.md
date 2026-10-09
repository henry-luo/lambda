# JS MVP guarded numeric regions

Date: 2026-10-09. Implements the bounded specialization work in
[JS_MVP_Lmd §25](../jube/JS_MVP_Lmd.md#25-guarded-numeric-regions).

## Scope

- Guard complete read-only field paths using the existing four-shape property
  caches. Ordinary execution warms the caches. Numeric and Boolean storage,
  intermediate Map pointers and captured method identity are checked before
  the specialized body executes. Misses retain ordinary semantics.
- Keep guarded field values and scoped numeric locals in native MIR registers.
  Inline scalar comparators beyond loop sites and propagate their facts through
  guarded caller bodies. Simple passthrough getters retain their packed reads.
- Inline bounded, receiver-unobserving constructors after ordinary construction
  establishes a reusable layout. Reuse `mvp_lmd_object_new`, Lambda shapes and
  the existing inline frame; retain precise safepoints and real object identity.
  Constructor bodies have a smaller inlining budget to account for their extra
  allocation guard and fallback entries.
- Extend existing scalar-object analysis across direct, capture-free factories
  that only return a simple constructor result with forwarded/literal arguments.
  Only field-only consumers qualify. Escape, identity, reflection, unknown fields,
  mutation of the constructor binding and more complicated factory bodies retain
  allocation. This does not eliminate CD's effectful `treeInsert` result object.
- Lower proven plain-object stores/deletes directly to existing Lambda
  `map_shape_set` / `map_shape_delete`, retaining their GC effects, canonical
  keys and generic fallback for other receiver families.
- Correct cached and shaped Function field reads to preserve Lambda's direct
  pointer representation, including strict identity.

The design reuses **D3.4.3v5** shapes and **D5.2–D5.3** ownership/root analysis.
No helper is declared `NO_GC` merely because its fast path is numeric: emitted
operations retain their effects, and allocations remain safepoints
(**D5.3.1–D5.3.2**). Function representation and public entries remain aligned
with **D6.2.1**, **D6.2.3v2–D6.2.4**. No runtime helper, new object layout,
descriptor, proxy or prototype-mutation feature is introduced.

The six new compiler helpers were disclosed before implementation:
`numeric_region_path`, `numeric_region_read`, `plan_numeric_region`,
`guard_property_cache`, `guard_numeric_region`, and `scalar_factory_plan`.
Constructor and factory handling otherwise extend existing helpers. Read lookup
reuses bounded function plans; class lookup reuses the existing binding and
class AST. Program, binding, function and ordinary object-plan records retain
their original sizes (3,896, 256, 1,016 and 80 bytes on this arm64 build).
Factory-only metadata uses an optional extension.

## Evidence and acceptance

Artifacts are under `temp/mvp_numeric_regions_20261009/`. Intermediate screens
are retained separately; `accepted/` holds the final source and binary.
`matched-control-stripped.exe` is compiled from the pre-change sources with the
same release flags and dependency objects, then stripped with the same `strip -x`
step as the production candidate. Earlier screens used the unstripped
`matched-control.exe`; those comparisons remain recorded separately.
`control.exe` preserves the incoming release. Commands and source/binary hashes are recorded alongside the runs.

Performance comparisons use unchanged benchmark inputs and output oracles,
pinned native MIR, fresh processes, self-reported workload timing, balanced
three-lane permutations and an identical-control peer. Initial compilation is
excluded from workload timing; process wall time is recorded separately.
Node tiering during the workload remains included. Lambda and Node timing
boundaries follow the frozen benchmark manifests; SHA1's Lambda port is the
canonical port, not labeled untyped.

### Final performance

Release candidate SHA-256:
`f85933d635f3cdcb60f69b2fc9717dd8195bd50133fd944b3e6ba406ed03c96f`.
Matched control:
`45fb573d373ddd83aed92f7dea416bbed3b5999040fa38b48c8760e5a4e652a1`.
Source hashes, commands and environment are in `accepted/manifest.json` and the
individual comparison records.

Self-reported milliseconds, medians of 15 measured runs per lane:

| Workload | Before | After | Change | Lambda reference | Node |
|---|---:|---:|---:|---:|---:|
| CD | 369.288 | 244.041 | 33.9% faster | 606.756 | 35.669 |
| Richards | 86.475 | 80.520 | 6.9% faster | 302.559 | 8.201 |
| NBody | 45.178 | 45.256 | within noise | 7.412 | 5.338 |
| Bounce | 0.818 | 0.816 | within noise | 0.070 | 0.722 |
| Storage | 0.514 | 0.528 | confirmation below | 0.607 | 0.687 |
| SHA1 | 13.852 | 13.793 | within noise | 50.513 | 8.722 |

CD remains **6.84× Node**. Its paired ratio 95% bootstrap interval is
0.654–0.675; Richards is 0.923–0.937. All 60 unchanged benchmark workloads pass
their oracles (15 pairs per workload). Three shared Lambda clients—`fib`,
`gcbench`, and `json_gen`—also pass 30 paired comparisons, with confidence
intervals including parity. Longer confirmation of noisy MVP rows is recorded
below; this is a measurement result, not a guarantee for all programs or
differences below the noise floor.

Cold process medians also improve for CD, 3,731 → 3,146 ms, and Richards,
687 → 644 ms. An intermediate build regressed NBody cold time by 37 ms while
duplicating constructor paths at startup-only factory sites. Reserving part of
the inlining budget for allocation/fallback entries removes 9,092 generated MIR
instructions from NBody without changing CD or Richards' instruction counts.
The final NBody cold comparison is **387.5 → 388.4 ms**. These wall times include
parsing, compilation and process overhead and are separate from the workload
table.

### Mechanism check

Separate diagnostic binaries enable the existing execution-profile hooks on the
same CD workload and verify its 4,305-collision result. Their timings are not
performance evidence. The diagnostic build from the final frozen sources
produces these counts:

| Dynamic event | Before | After | Reduction |
|---|---:|---:|---:|
| Function-frame entries | 5,937,729 | 2,609,469 | 56.1% |
| Root reloads | 22,966,462 | 14,885,948 | 35.2% |
| Runtime helper calls | 8,973,554 | 6,497,321 | 27.6% |

The counts support the call/ownership-boundary diagnosis. Significant dynamic
traffic remains; effectful `treeInsert` returns and escaping objects still
allocate. General escape analysis across effectful returns and wider caller/callee
specialization remain future work.

The final Lambda baseline passes **6,484/6,484** (2,112 input and 4,372 runtime
tests). An initial run timed out on the LaTeX phase-3 corpus at 60 seconds;
isolated release replays matched the exact fixture on both control (55.2 s) and
candidate (54.6 s), and the complete final gate passes it at 56 seconds. Logs
and initial/final result files are retained separately.

The initial Test262 run had zero semantic regressions but one Unicode identifier
case passed only on retry. Its final complete rerun uses two workers with
unchanged tests and timeouts: **40,261/40,261 fully passing, zero retries, zero
batch failures**. The main `lambda.exe` hash matches the measured release
candidate after all gates.

### Regression investigation

Intermediate metadata expansion and code placement coincided with small
ordinary-object regressions, despite identical emitted MIR for those workloads.
Restoring compact compiler records removed unnecessary allocations; it did not
fully remove the measured deletion regression. Native inspection showed the
same map mutation instructions and changed wrapper addresses, without proving
a particular cache or branch-placement cause.

The wrapper inspection also exposed repeated receiver-family checks on proven
plain objects. Direct reuse of Lambda's existing mutation functions removes
that work without padding, benchmark-specific logic or a new runtime helper.
The final 90-run comparison uses the stripped control consistently:

| Workload | Before (ms) | After (ms) | Paired ratio 95% interval |
|---|---:|---:|---:|
| Object growth | 2.913 | 2.9315 | 0.999–1.013 |
| Escaped retyping | 5.683 | 5.253 | 0.918–0.932 |
| Deletion | 1.147 | 1.125 | 0.973–0.986 |
| Map lookup | 7.7185 | 7.7585 | 0.996–1.016 |
| JSON generation | 5.779 | 5.7425 | 0.990–0.999 |

Retyping and deletion improve by 7.6% and 1.9%; growth and lookup intervals
include parity. Timing and binary-layout effects below those intervals remain
unresolved. These comparisons establish the combined candidate's behavior,
not the isolated contribution of each change.

Three smaller regressions remain confirmed on this final candidate:

| Workload | Runs/lane | Before (ms) | After (ms) | Slower | Ratio 95% interval |
|---|---:|---:|---:|---:|---:|
| Map iteration | 90 | 1.2925 | 1.303 | 0.81% | 1.007–1.009 |
| Nqueens | 90 | 0.926 | 0.934 | 0.86% | 1.002–1.018 |
| Storage | 60 | 0.513 | 0.523 | 1.95% | 1.016–1.023 |

The identical-control peer intervals include parity in all three confirmations.
Map iteration and nqueens emit identical MIR after embedded addresses are
normalized. Storage's recursive tree builder and random-number method also
retain their instructions after label/address normalization; its benchmark
setup gains constructor expansion, while the full bundle loses 12,417 MIR
instructions. Storage process wall time improves from 726.1 to 639.6 ms, but
that does not cancel its workload regression. Code/data placement is a possible
explanation, not an established cause. No padding or workload-specific bypass
was added. **Correctness gates pass; regression-free performance acceptance
remains open for these three rows.**

### Validation commands

```sh
make build-release-compile
make -C build/premake config=release_native test_js_mvp_lmd_gtest -j8
JS_EXEC_BACKEND=mir JS_MIR_INTERP=0 ./test/test_js_mvp_lmd_gtest.exe
LAMBDA_GC_FORCE_EVERY=1 LAMBDA_GC_POISON_FREED=1 \
  JS_EXEC_BACKEND=mir JS_MIR_INTERP=0 ./test/test_js_mvp_lmd_gtest.exe
make test-lambda-baseline
make test262-baseline
./test/test_js_test262_gtest.exe --baseline-only --batch-only --run-async \
  --async-list=test/js262/test262_baseline.txt --jobs=2
```

Focused release tests: **64/64**, both normally and with forced collection and
freed-memory poisoning. Fixtures exercise lane/shape changes, deletion, method
replacement, argument effects, signed zero, NaN, subnormals, constructor identity,
factory escape and TDZ. MIR assertions verify allocation removal for the
eligible scalar factory consumer and direct reuse of Lambda shape mutations.

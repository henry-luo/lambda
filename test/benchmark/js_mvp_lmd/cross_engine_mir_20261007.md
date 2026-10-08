# MVP-Lmd cross-engine release comparison — 2026-10-07

Current MVP compared with full LambdaJS (LJS), unannotated Lambda ports, and
Node **v22.13.0**. On the 13 standard workloads with all four lanes, the
geometric mean of paired median speedups is **2.18× versus LJS**,
**0.79× versus untyped Lambda**, and **1.57×
versus Node**. A value above one means MVP is faster. This describes this
admitted subset; it is not an overall JavaScript engine ranking.

## Measurement

- Release SHA-256: `9af6d491cfe0306286f1572dca16559925dcbdffe2ce4e29eeba21df8660ff64`. All Lambda lanes use that same
  executable; Node path, executable hash, and source manifests are in the JSON.
- Native MIR pinned for both JS lanes and untyped Lambda. Each Lambda lane
  produced fresh finalized MIR during a discarded process. AST/AUTO and MIR
  interpretation are disabled. Selector authority: **D8.1.1v17**,
  **D8.1.3v24**; optimization policy: **D8.4.1v2**.
- Fifteen fresh measured processes per engine/workload, after one discarded
  process per lane; rotating lane order. A duplicate MVP lane measures
  identical-binary noise. **2,025 measured
  result oracles passed**, with no failed or timed-out lane.
- Times are each engine's self-reported workload execution. Startup, initial
  parsing/JIT compilation, and teardown are excluded. Node tiering during its
  workload is included; no persistent-VM warmup is performed. Short rows can
  therefore favor MVP strongly and do not predict warmed V8 throughput.
- MVP times its generated program entry; LJS/Node bracket the same adapted
  workload with `performance.now()`. MVP also includes top-level declaration
  setup. Timing/output harness operations are replaced; algorithm, inputs,
  repetition counts, and result computations are retained.
- Untyped Lambda runs the canonical unannotated `.ls` port with its timer.
  Its numeric/container semantics remain Lambda's (**S1.11**, **D2.2.5**).
  Collatz uses `shr` for even halving while JS uses `/ 2`; this is a port
  comparison. `cpstak` in both languages is the repository's double direct
  Takeuchi adaptation, not a closure-passing workload.
- `gcbench` builds the same report string, including its final long-lived
  traversal, in every lane and prints after timing. Its Lambda adapter is
  archived. `pnpoly` checks exact `[total,count]` in JS and exact report text
  in Lambda; Lambda retains one report print inside its timer.
- One-minute host load: **4.0 → 13.1**
  on 8 logical CPUs. Duplicate-MVP paired medians span
  **0.887–1.125**.
  Small margins are unresolved; per-row ranges and bootstrap intervals are
  retained. Time columns are independent medians; aggregate ratios use paired
  medians. This is a cross-engine comparison, not an isolated tuning A/B.

## Workloads with all four ports

| Workload | MVP ms | LJS ms | Untyped Lambda ms | Node ms |
|---|---:|---:|---:|---:|
| r7rs/fib | 1.642 | 2.216 | 1.753 | 2.251 |
| r7rs/fibfp | 1.678 | 2.237 | 2.627 | 2.263 |
| r7rs/tak | 0.141 | 0.398 | 0.150 | 0.792 |
| r7rs/cpstak | 0.267 | 0.804 | 0.303 | 1.320 |
| r7rs/sum | 0.305 | 0.768 | 0.306 | 1.701 |
| r7rs/sumfp | 0.031 | 0.078 | 0.079 | 1.303 |
| r7rs/ack | 15.005 | 22.229 | 19.552 | 19.840 |
| larceny/diviter | 2811.657 | 650.786 | 292.828 | 512.315 |
| larceny/divrec | 0.642 | 16.502 | 1.254 | 8.111 |
| kostya/collatz | 543.389 | 1687.582 | 309.008 | 1460.121 |
| js_mvp_lmd/integer_dense | 5.577 | 32.947 | 23.876 | 7.035 |
| js_mvp_lmd/integer_indirect | 2.158 | 9.562 | 6.987 | 3.099 |
| larceny/pnpoly | 378.735 | 126.726 | 17.010 | 10.667 |
| larceny/deriv | 30.343 | 158.890 | 34.169 | 13.235 |
| larceny/gcbench | 197.043 | 899.147 | 224.100 | 60.143 |

Including the two integer microbenchmarks, the 15-row geometric mean MVP
speedups are 2.42× versus LJS,
0.96× versus Lambda,
and 1.53× versus Node.
The headline aggregate excludes those two microbenchmarks.

## Additional supported JS microbenchmarks

No equivalent untyped Lambda port is present for these rows.

| Workload | MVP ms | LJS ms | Node ms |
|---|---:|---:|---:|
| js_micro/lit | 0.169 | 156.040 | 4.781 |
| js_micro/named | 4.734 | 181.395 | 7.622 |
| js_micro/args_ctl | 5.653 | 84.521 | 2.767 |
| js_micro/args_fp | 5.732 | 88.399 | 2.707 |
| js_mvp_lmd/object_fields | 0.473 | 12.480 | 2.048 |
| js_mvp_lmd/object_growth | 4.318 | 53.069 | 4.795 |
| js_mvp_lmd/object_retype | 0.502 | 75.232 | 1.725 |
| js_mvp_lmd/object_retype_escaped | 8.161 | 73.364 | 1.679 |
| js_mvp_lmd/object_delete | 2.771 | 112.932 | 1.365 |
| js_mvp_lmd/map_lookup | 15.499 | 126.549 | 4.874 |
| js_mvp_lmd/map_iteration | 2.239 | 817.365 | 4.540 |
| js_mvp_lmd/numeric | 80.353 | 155.182 | 80.715 |
| js_mvp_lmd/dense_array | 81.038 | 343.270 | 12.630 |
| js_mvp_lmd/calls | 5.543 | 50.559 | 4.951 |
| js_mvp_lmd/strings | 2.740 | 0.290 | 0.304 |

`object_retype_escaped` is the existing aliased diagnostic. The extra local
alias retains allocation in MVP's conservative analysis; the object does not
escape its function. The unaliased row benefits from scalar replacement.
`js_micro/named` combines the original two four-million-iteration loops.

## Findings and remaining limits

MVP is competitive with untyped Lambda in simple numeric/recursive regions,
and its direct field and Map loops avoid much of full LJS's generic execution
cost. Node still has substantial advantages on recursive allocation, Map
lookup, mixed dense-array traffic, and the string microbenchmark. The tables
retain every measured row, including MVP's losses.

`diviter` exposes a current numerical inlining weakness. In finalized MVP MIR,
the inlined quotient loop's `q++` contains `i2d`, `dadd`, then `d2i` on every
iteration; the standalone function uses integer `add`. The conversion chain
is a plausible explanation for the gap and the next bounded tuning candidate
under **D2.2.5** and **D8.4.1v2**. This run did not isolate that change causally
or modify the compiler.

`pnpoly` is also much slower than both reference engines and its untyped
Lambda port. Its MIR retains generic Item array parameters, numeric-key to
string/property-key conversion, and generic element-kind/number handling.
Propagating array/index/element facts through closed calls is another bounded
tuning candidate; the presence of fallback calls in MIR alone does not prove
that all of them run on the measured path.

The measured standard subset comprises seven R7RS, five Larceny, and one
Kostya workload. Broader suite coverage is limited by excluded capabilities
such as typed arrays, array constructors/methods, Math/string libraries,
classes, captures, or prototypes. This phase continues to exclude
`__proto__`, descriptors/accessors, and proxies, per the MVP document §10.7.

Evidence: [full JSON](cross_engine_mir_20261007.json). Exact release binary,
adapted sources, finalized MIR, raw stdout/stderr, and runner snapshot are
under `temp/mvp_lmd_fourway_20261007/measured/`. Runtime and compiler sources
were unchanged for this comparison.

Reproduction from repository root:

```sh
python3 temp/mvp_lmd_fourway_20261007/run.py --runs 15 \
  --output temp/mvp_lmd_fourway_20261007/replay
```

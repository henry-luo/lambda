# JS MVP method and field specialization

2026-10-09. Implements the bounded first step of [MVP §24](../jube/JS_MVP_Lmd.md#24-method-specialization-and-stable-class-fields).
Authority: **S1.11**, **D1.3v3**, **D3.4.3v5–D3.4.6**, **D5.2–D5.3**,
**D6.2.3v2–D6.2.4**. No semantic ruling changes.

## Implementation and limits

- Parameterized methods, setters and small statement bodies share the existing
  inline frame and scalar ownership rules. The guard compares the captured
  callee entry after argument evaluation. Missing/extra arguments, overrides,
  borrowed receivers, strict mode and early returns retain their semantics.
  Expansion has a node budget, a depth limit and a recursion check. Statement
  bodies require a loop call site, reusing the direct-call profitability rule. `super`
  retains the ordinary call path.
- Known callback targets use that guard and read the actual function's shared
  closure environment. Mutable captures remain cells; lexical `this` and
  `new.target` use the existing receiver cell, explicitly rooted across calls.
  Functions that create captured locals are excluded. Dynamically stored
  callbacks without a known target, including Richards' task functions, retain
  the ordinary ABI.
- Class links reuse Lambda's existing nullable Map pointer carrier. A first
  null-to-Map write takes the ordinary immutable transition. Later null writes
  store a zero pointer with the shared `map_field_store`, preserving that Map
  shape, just as Lambda's `fn_map_set` does. Other value kinds keep ordinary
  retyping. Constructor writes retain exact initial types, and factories can
  finish initialization after `new` without widening to `TypedItem`.
  GC, reflection and the existing pointer cache all use the shared layout.
  No common runtime or cache-layout change is required.
- Simple unobserved class locals reuse the existing scalar-object escape proof,
  field slots and constructor proof. Arguments execute once, in order, and the
  constructor binding still receives its TDZ check. Only fixed receiver-free
  assignments and stable constructor bindings qualify. Aliases, identity,
  methods, reflection, escape, derived constructors and receiver observation
  retain allocation. This does **not** yet eliminate CD's constructor-returned
  vectors passed through method chains.

New compiler helpers, disclosed before coding: `inline_call_body` and
`scalar_constructor_plan`. No new runtime helper or import. The attempted
promotion of `container_retype_field` was removed after finding that Lambda's
existing nullable pointer carrier already supplies the required representation.
Shared Function, closure, GC and packed-field layouts are unchanged.

## Evidence

Artifacts: `temp/mvp_specialize_20261008/`. The final control is built from the
clean starting checkout `1875c7d307bd2f66e747443d14a360d06fe7dec7`:
`matched-control.exe`, SHA-256
`39674b6328c233582b0aa26cb2694085409b2b79ff64c62cc141d0de3d2ce2fa`.
`matched-control.json` records the source hashes. Only the task's owned source
edits were temporarily removed for that build, then restored and checked.
Early diagnostic runs used the previous accepted release
(`c0953ebc4326e6656e6d6e8b24c36c58941fb4cd57ce1b50bb69ff7ff8a2e5c9`),
which predates unrelated DOM/font/loader merges; those runs do not supply the
final comparison. Timings retain exact workload hashes and an identical-control
lane.

Final candidate: `packed.exe`, SHA-256
`10839e04476ab5c92113aa812139aa57a45488a26b0d3367a72a07f9bf6aa0b7`.
`packed.json` and `packed-source/` retain the exact source; `packed-gates.json`
records the sequential build, focused tests and measurements. The working
sources and `lambda.exe` match that manifest after the final baseline run.

### Workload time

Release, native MIR (`JS_EXEC_BACKEND=mir`, `JS_MIR_INTERP=0`,
`LAMBDA_JS_LARGE_INTERP=0`), with backend dumps and output checks. Each lane uses
fresh processes, one discarded warmup and balanced six-permutation ordering;
the peer runs the identical control binary. Self-reported workload time excludes
initial compilation/startup. No benchmark overlaps a build or test gate.

| Workload | Control ms | Candidate ms | Time reduction |
|---|---:|---:|---:|
| Richards | 102.3335 | 86.8635 | 15.1% |
| CD | 378.7225 | 368.6520 | 2.7% |
| Permute | 0.7975 | 0.6090 | 23.6% |
| Queens | 0.4600 | 0.3785 | 17.7% |
| Towers | 1.7815 | 1.3070 | 26.6% |

All **60 workloads** pass output checks: `packed-48` uses 30 balanced pairs,
`packed-classes` uses 60, and `packed-six` uses 18. Their aggregate geometric
mean candidate/control time ratio is **0.9852**; this is a cohort summary, not
evidence that every workload improves. Richards' paired 95% ratio interval is
**0.8439–0.8541**; CD's is **0.9626–0.9814**. Bounce, Storage and NBody do not
show a material improvement in this phase.

Fresh references in `packed-six/comparison.json` use Node **v22.13.0** and
Lambda with JIT pinned. Richards is **303.661 ms** in untyped Lambda and
**8.2171 ms** in Node; CD is **610.3335 ms** and **35.8045 ms**, respectively.
MVP remains **10.57×** and **10.30×** slower than Node. MVP timing includes
class creation/setup; the Lambda port clocks `benchmark()`. SHA1's reference
is the canonical Lambda port and is not labeled untyped.

### Regressions and limits

The full no-regression acceptance target remains **unmet**. A separate
120-pair confirmation (`packed-confirm`) retains two slowdowns:

| Workload | Control ms | Candidate ms | Change | Paired 95% ratio interval |
|---|---:|---:|---:|---:|
| Map lookup | 7.6865 | 7.8510 | 2.14% slower | 1.0117–1.0312 |
| GCbench | 87.7665 | 87.9125 | 0.17% slower | 1.0002–1.0031 |

Identical-control peers are within noise for both. Base64's initial slowdown
does not persist in confirmation (ratio interval **0.9993–1.0065**). The
120-pair object growth/deletion/retyping/string checks (`packed-micro`) and
60-pair shared Lambda fib/GCbench/JSON generation checks
(`packed-shared-lambda`) show no confirmed slowdown.

Map lookup and GCbench emit the same MIR operations, differing in addresses.
The native Map-call helper contains the same **693 instructions** after
relocation normalization (`compare_native.py`, `*-map-native.txt`). This
narrows the investigation but does not establish the cause; native layout,
addresses and allocation-state effects remain hypotheses. No alignment or
padding workaround is introduced.

Inlining also increases cold process time. Median process wall times, including
startup/compilation and execution, are:

| Workload | Control ms | Candidate ms |
|---|---:|---:|
| Bounce | 818.88 | 952.83 |
| Storage | 703.61 | 808.96 |
| Richards | 614.85 | 689.52 |
| CD | 2567.04 | 3748.34 |

These are not pure compiler timings. The added cost is material for one-shot
scripts, especially CD. Further work needs a tighter aggregate expansion budget
and hot-call selection, plus a root-cause diagnosis of the two workload
regressions. CD's returned vector allocations and Richards' dynamically stored
callback calls also remain targets; the bounded optimizations above do not
remove them.

Earlier TypedItem widening, expanded field-cache MIR and shared retype-helper
promotion experiments were removed after exposing regressions. The final
nullable Map representation avoids those changes entirely.

### Correctness gates

- Release MVP tests: **58/58**, both normally and with forced GC/poisoning
  (`packed-unit.log`, `packed-unit-gc.log`). New coverage includes guarded
  argument/receiver/capture semantics, early returns, scalar-local escape/TDZ,
  and physical nullable Map shape/reflection behavior.
- Combined Lambda/input baseline: **6,468/6,468**
  (`review-lambda-baseline.log`). Despite the log prefix, this run used the
  final source recorded in `packed-baseline-source.json`.
- Final unchanged `make test262-baseline`: **40,261/40,261**, zero retries,
  failures or unstable cases (`packed-test262-baseline.log`). The preceding
  run had three Unicode identifier tests exceed the timing threshold and pass
  only on isolated retry (`review-test262-baseline.log`). Preserve that
  instability record; no harness, threshold or baseline was changed.

Published `MVP_Result3`–`MVP_Result7` artifacts are unchanged. Implementation
and correctness validation are complete; performance acceptance remains open
for the regressions and cold compilation cost recorded above.

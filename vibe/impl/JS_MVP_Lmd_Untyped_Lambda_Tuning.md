# JS MVP tuning against untyped Lambda

**Date:** 2026-10-19

**Status:** initial tuning milestone implemented (P0, P1, P2a/P2b and a measured
P4 analysis fix). Functional gates pass; the Havlak execution target is partly
met. Performance tradeoffs, the two-worker qualification and remaining work are
recorded in §9.

**Scope:** the current MVP MIR compiler and its shared Lambda runtime paths.
Improve unchanged JavaScript workloads using evidence from untyped Lambda;
retain the configured Test262 suite's zero-failure, zero-timeout, zero-retry gate.

**Authority:** **S1.11**, **S9.1.3**, **D8.1.3v26**, **D8.4.1v2–D8.4.3v2**,
**D3.4.3v5–D3.4.5**, **D5.2–D5.3**, especially **D5.3.4**. This plan changes no
language or architecture ruling. The formal specs govern the optimizations;
historical tuning documents are implementation evidence only.

## 1. Conclusions and priority

The earlier **36.62× Havlak execution gap is invalid**: the JS benchmark runs
50 extra full-graph recognitions, while the Lambda port runs one. With those
counts matched, this investigation measures **1.39–1.50×** MVP/Lambda execution
time. The ports still use different representations and some different
algorithmic steps. Neither number is a pure comparison of compiler overhead.

There are nevertheless concrete opportunities:

1. **Fix numeric-local dominance inference first.** A conservative binding
   proof boxes an accumulator on every iteration of the π-sum loop. A source-only
   diagnostic removes that cost using machinery already present in MVP. The
   same pattern occurs in the matrix benchmark.
2. **Reduce Havlak's closure and dispatch costs.** Runtime sampling identifies
   closure creation, GC, and TypeMap lookup. Extend bounded static call/receiver
   proofs and reuse Lambda's representation and rooting primitives.
3. **Track compilation as a separate performance target.** Matched short Havlak
   takes about 6.5 seconds end to end in MVP versus 0.29 seconds in Lambda on this
   host. Execution-only tables hide this substantial remaining gap.
4. **Profile string and RegExp rows after the above.** Their large port ratios
   mix engine costs with different algorithms, APIs, and timing boundaries.

Do not restore property/callee caches, rewrite canonical benchmarks to obtain
the gain, or replace JS semantics with Lambda semantics. **D8.4.1v2** requires
compile-predicted guards and shared-kernel fallback; **S1.11** keeps the languages'
semantic responsibilities separate.

## 2. Correcting the benchmark evidence

### 2.1 Havlak repeat mismatch

The relevant entrypoints are:

| Port | Source and symbol | Arguments affecting repeated work |
|---|---|---|
| Untyped Lambda | `test/benchmark/awfy/havlak.ls:851`, `main` | `lta_main(1, 1, 10, 10, 5)` |
| JavaScript | `test/benchmark/awfy/havlak2_bundle.js:1201`, `Havlak.innerBenchmarkLoop` | `LoopTesterApp.main(innerIterations, 50, 10, 10, 5)`; harness supplies `innerIterations = 1` |

Both perform one initial full-graph recognition, then the specified extra
recognitions. Thus the canonical Lambda run performs **2**, versus **51** in
JS; both also perform one small dummy recognition. Both verify **1,605 loops
and 5,213 nodes**, because the extra recognitions use disposable loop graphs.
The output oracle cannot detect the repeat mismatch. Dividing the old ratio by
50 or 25.5 would also be wrong: setup and the timed work are not uniform.

This investigation froze diagnostic copies and changed only the repeat argument.
Canonical files and goldens remain unchanged. Each row below has one warmup per
lane, a Node JS oracle warmup, and seven alternating fresh-process pairs.

| Extra recognitions, both ports | Full-graph recognitions | Lambda execution ms | MVP execution ms | MVP / Lambda | Lambda process wall ms | MVP process wall ms |
|---:|---:|---:|---:|---:|---:|---:|
| 1 | 2 | 125.800 | 188.682 | 1.500× | 289.035 | 6,499.908 |
| 50 | 51 | 3,161.630 | 4,401.250 | 1.392× | 3,422.697 | 9,680.808 |

All **28 measured outputs and 6 warmup outputs** match their existing oracles.
These are **screening measurements**, not quiet-host acceptance: Spotlight was
active, timing varied materially, and this capture has no identical-control
lane. The two-sided paired-bootstrap ratio intervals are approximately
**1.390–1.897** and **1.256–1.556**. Those intervals do not remove systematic
host or port differences. Lambda verifies after its timer; JS verifies inside
its timer. Process wall time additionally includes startup, parsing, compilation,
initialization, output, and teardown; it is not a compiler-only timer.

The old 36.62× row and the **2.27× eleven-row geometric mean containing it must
not be used as tuning targets**. Preserve historical artifacts and annotate
their comparability limitation; produce a new versioned comparison after the
contract audit. Do not silently overwrite published timing columns.

### 2.2 Remaining port differences

Even at matched counts, Havlak differs substantially:

| Operation | Untyped Lambda port | JavaScript port | Consequence for diagnosis |
|---|---|---|---|
| DFS-number lookup | Integer block IDs and nested indexed arrays | Block identities in a source-defined `IdentityDictionary` | JS builtin `Map` tuning does not directly fix this dictionary |
| Union/find | Numeric `parentDfn`; `uf_find_set` follows IDs without path compression | Object parent links; `findSet` allocates a `Vector` and performs path compression through a callback | Different work and allocation patterns |
| Traversal | Procedural loops and direct helper calls | `Vector.forEach`, `hasSome`, closures and methods | Static callback specialization is a relevant compiler target |
| Node state | Records and numeric state codes | Objects initialized by `initNode`; string state labels | Shape and scalar proofs differ |
| Mutation | Explicit `var`/inout updates | Ordinary JS shared-reference mutation | Reuse physical mechanisms, not Lambda aliasing assumptions (**S9.1.3**) |

Other rows from the preceding seven-pair screening capture remain useful for
triage, not as claims of identical engine work:

| Workload | Lambda ms | MVP ms | MVP / Lambda | Interpretation |
|---|---:|---:|---:|---|
| `julia/matrix_statistics` | 16.540 | 96.985 | 5.86× | Matching loop dimensions; nested accumulators and numeric-array paths deserve investigation |
| `julia/iteration_pi_sum` | 6.969 | 23.615 | 3.39× | Matching 5,124,750 summation iterations; concrete boxing issue below |
| `beng/revcomp` | 0.573 | 4.181 | 7.30× | Lambda uses bulk substitutions and line reversal; JS uses character lookup/concatenation and split/reverse/join |
| `beng/regexredux` | 0.733 | 2.948 | 4.02× | Lambda patterns/native replacements versus JS RegExp; file-input timing also differs |
| `beng/knucleotide` | 2.062 | 3.678 | 1.78× | Audit substring, key and counting representations before choosing a kernel |
| `awfy/json` | 6.015 | 9.678 | 1.61× | Parser-port object/call workload; do not assume this measures builtin `JSON.parse` |

MVP was already faster on the measured `parse_integers`, `formatted_output`,
`fasta`, and `pidigits` ports. Keep these in the regression population.
The old entry-file annotation scan also misclassified `text/hyphen`: its shared
implementation imports annotated helpers. “Untyped Lambda” needs a transitive
import audit, not a filename or entry-file regex.

## 3. A confirmed numeric inference opportunity

`test/benchmark/julia/micro_common.js:91`, `iterationPiSum`, initializes
`var total = 0.0` inside an outer loop and updates it inside an inner loop.
`gram` and `square` have the same accumulator shape.

In `lambda/js/mvp-lmd/mvp_lmd_mir.cpp:1227`, declaration collection marks an
initializer as dominating only for selected syntactic positions: top level,
function-body declarations, lexical block declarations, or loop initializers.
A function-scoped `var` initialized inside an ordinary nested block does not
qualify. At `:11334`, native binding admission requires `b->dominated`.
The restriction is conservative for general JS hoisting and control flow, but
misses the safe use pattern in these benchmarks.

The captured π-sum MIR (`mvp_lmd_f10`, matching source declaration order and
its 500/10,000 constants) contains number-home adoption, tag decoding, boxing,
and copying back into the accumulator on every inner iteration. Integer `k`
already has a native lane. This is not simply a missing arithmetic instruction.

A diagnostic changed **only the accumulator declaration from `var` to `let`**
in temporary copies: `iterationPiSum` for π-sum; `gram` and `square` for matrix.
Neither kernel's order, counts, output, nor engine changed. Five rounds included
two identical canonical controls and the diagnostic source:

| Source-only probe | Canonical control ms | Identical control ms | Lexical accumulator ms |
|---|---:|---:|---:|
| π-sum | 35.982 | 37.741 | 5.497 |
| Matrix statistics | 151.894 | 199.172 | 103.384 |

All **30 measured and 6 warmup outputs** pass. π-sum's diagnostic inner loop
holds `total` in a double register with `ddiv`/`dadd`, and boxes only at a later
boundary. Its function shrinks from **313 to 249 MIR locals**. Matrix's controls
differ by 31%, so its precise percentage gain is not qualified. These probes
demonstrate an inference opportunity, **not implemented engine speedups**.

The production fix must infer the safe numeric lifetime in the **unchanged
`var` source**. Rewriting benchmarks to `let`, globally treating nested `var`
initializers as dominating, or borrowing Lambda's block-binding semantics
would not fix the compiler's missing proof.

## 4. Havlak runtime and compilation evidence

A two-second native sample of the pinned release started after an execution
marker immediately before the existing Havlak timer. The marker was added only
to a diagnostic copy. The run passed; compilation precedes the marker. The
matching release link map resolves native frames, while generated MIR code
addresses remain unnamed.

There are **1,642 sampled stacks**. Each category below counts a stack once if
the named path occurs anywhere in it; **categories overlap and must not be
added together**:

| Path present in stack | Samples | Share | Implication |
|---|---:|---:|---|
| Closure creation: `mvp_lmd_function_new` or `heap_calloc_closure_env` | 323 | 19.7% | Investigate short-lived callback objects/environments and creation metadata |
| `mvp_lmd_class_invoke` | 323 | 19.7% | Includes work below invocation, including allocation/GC; not 19.7% pure dispatch overhead |
| GC collection | 298 | 18.1% | Reduce allocations and retention before changing collector policy |
| `typemap_hash_lookup*` | 279 | 17.0% | Extend source shape proofs and safe reuse of invocation-local lookup results |
| `map_shape_field_to_item` | 79 | 4.8% | Retain native lanes where the storage descriptor proves them |
| `MvpLmdRealmScope` | 38 | 2.3% | Small measured share; a blanket call-ABI rewrite is not the first fix |

About 25% of exclusive samples are unresolved generated-code frames. A single
sample does not identify individual JS method hotness or allocate all time to
the categories above. Map code IDs back to source before expanding a particular
method; static MIR call counts are not runtime frequencies.

One concrete creation cost is visible in source and sample:
`mvp_lmd_function_new` (`mvp_lmd_mir.cpp:10019`) recomputes
`js_ast_collect_parameter_facts`, parent classification, and constructibility
from the same AST on each creation. Parameter-fact collection contributes
17 exclusive samples, approximately 1%; precomputing it is a small bounded
improvement, not a substitute for reducing allocation.

The emitted Havlak MIR also warrants a compilation budget:

| Matched 50-repeat port | MIR functions, including wrappers | Instructions before backend optimization | Text dump bytes |
|---|---:|---:|---:|
| MVP | 131 | 257,593 | 7,935,812 |
| Lambda | 115 | 19,905 | 706,791 |

These counts exclude declarations, labels and comments. They measure emitted
MIR, not final machine code or executed instructions. The different ports and
fallback requirements explain some expansion; the **12.9× instruction ratio**
justifies inspecting per-function duplication and compilation phases. It does
not prove all extra code can be removed.

## 5. Reuse the current implementation

| Existing mechanism | Relevant implementation | Extension boundary |
|---|---|---|
| Native binding and numeric regions | MVP `walk` declaration handling, `representation_pass`, `plan_numeric_region`, `guard_numeric_region` | Add explicit lifetime/effect evidence; do not loosen all bindings or all public-realm calls |
| Dense array loops | MVP `dense_array_loop_node`, `dense_array_loop`, `sequence_method_call`; Lambda `mir_dense_loop_scan`, `mir_emit_dense_loop_guard` | Share physical guards/emission where valid; retain JS holes, prototypes, coercion, mutation and fallback |
| Packed field access | MVP `object_shapes`, `own_data_field`, `field_read`, `property_access`; Lambda inferred member lowering near `transpile-mir.cpp:27430` | Predicted shape guard, shared `typemap_hash_lookup_key`, `shape_entry_storage`, `map_shape_field_to_item` |
| Direct and guarded calls | MVP `direct_target`, `call`, `inline_body`, `inline_call_body`, `method_candidate_name` | Existing individual-operand ABI and bounded candidate guards; dynamic boundary remains available |
| Closure representation | MVP `new_function`, `mvp_lmd_function_new`; shared `Function`, closure environment and owned-slot stores | Precompute immutable definition facts; eliminate allocation only after escape/identity proof |
| Small-object scalar replacement | MVP `plan_scalar_objects`, `scalar_constructor_plan`, `scalar_factory_plan` | Currently bounded and excludes identity, aliases and method receivers; widen only for measured safe cases |
| Allocation/layout | MVP `constructor_layout_reusable`, `mvp_lmd_class_invoke`; Lambda shared shapes and `map_alloc_for_type` | Constructor reuse already exists; preserve absent properties until initialization is observably complete |
| Ownership | `lambda/runtime/mir_emitter_shared.hpp`, `RootFrame`/`Rooted`, representation-aware MIR events | **D5.3.4**: rooting policy stays in `MirEmitter`; no second JS rooting subsystem |

The current shared-dispatch work is documented in
[MVP §36.10](../jube/JS_MVP_Lmd.md#3610-remove-cache-machinery-and-reuse-lambda-field-specialization).
It already reduced Havlak from about 3,419 to 2,654 ms in its own paired screen.
Do not claim those changes again. The older
[class tuning record](JS_MVP_Lmd_Class_Tuning.md) describes retired per-site
caches; those sections are superseded by **D8.4.1v2**, not a template for this work.
The existing [library tuning record](JS_MVP_Lmd_Library_Generators.md#6-library-workload-tuning)
already covers prepared RegExp subjects/scratch and ASCII offset shortcuts.

Reuse AST-independent representation and emission helpers directly. When a
needed helper is file-local, promote a suitable shared interface instead of
copying it. Lambda and JS AST analyses can remain separate when their binding,
aliasing or completion semantics differ. **D8.4.2v2** governs direct operands;
**D8.4.3v2** requires explicit completion propagation on fallible edges.

## 6. Implementation sequence

### P0 — Establish comparable contracts and diagnostic attribution

**Files:** `test/benchmark/js_mvp_lmd/expansion.py`, benchmark registry/manifest
and paired runner; compiler diagnostics only where existing facilities lack
source attribution.

- Record input dimensions, repeat counts, setup/timer boundaries and checked
  work separately from final output. Give the two matched Havlak contracts
  distinct IDs. Keep the original JS 50-repeat contract for engine A/B tests.
- Freeze the full source/import closure and audit numeric annotations through
  that closure. Replace the Julia-only helper-copy special case with generic
  dependency handling. Preserve relative imports and hash every resolved input.
- Add diagnostic phase/call counts for graph construction and recognitions;
  prove the repeat mismatch is rejected even though the final result is equal.
  Counters and profiling must be disabled in acceptance timings.
- Emit a bounded diagnostic map from function ID to source name/span, emitted
  instructions, roots/homes, and specialization rejection reason. Use it to
  choose hot calls, not source spelling or benchmark-specific thresholds.
  Reuse `jit_log_code_addresses` for generated addresses and extend its source
  attribution rather than creating a separate address registry.

**Exit:** two explicitly matched Havlak contracts pass; an intentionally
mismatched repeat contract fails comparison admission; imported annotations and
missing dependencies cannot silently enter the “untyped” population. Preserve
historical results with a correction pointer.

### P1 — Infer nested numeric accumulator lifetimes

**Files:** `mvp_lmd_mir.cpp` declaration collection, binding facts,
`representation_pass`, loop analysis; existing optimizer/MIR fixtures.

1. Add a bounded structured definite-assignment witness for nonescaping locals
   initialized inside a loop body. Start with the π-sum/Gram/square pattern:
   every read and update is dominated by that iteration's initializer, all
   writes remain numeric, and no reference observes the function-hoisted value
   before initialization or outside the admitted region.
2. Feed the witness into existing native-binding admission. Keep the accumulator
   in a native double register, use the current range proof for integer indices,
   and materialize an owned Item only at array storage, return, or other required
   representation boundaries. Do not change arithmetic order or enable fast-math.
3. Initially decline captured/eval-visible locals, conditional initializers,
   switch entry, unproven `continue`/exception paths, and outside-region reads.
   A later region-only specialization may explicitly merge initialized and
   undefined states; it is not required for this milestone.
4. Use Lambda's accumulator/flow planning as a model
   (`mir_loop_accumulator_plan`, `mir_flow_note`, `mir_flow_stable_source`), while
   keeping JS `var` hoisting, closure cells, coercions, NaN, infinities and `-0`.

**Exit:** unchanged π-sum and matrix sources pass; MIR proves no repeated
accumulator boxing/adoption in the admitted inner loop. Strengthen existing
`NumericBindingProofSeparatesInitializersFromLoopWrites` and
`NumericLocalFactsSurviveBoxedAndVoidReturns` coverage with nested `var`,
zero-iteration/outside read, skipped initialization, capture, and widening cases.
The lexical-source probe is only diagnostic and does not enter published engine
results. Provisional screening goal: at least 2× faster canonical π-sum; final
acceptance still requires paired engine evidence below.

### P2 — Reduce Havlak callback creation and invocation work

**Files:** MVP `LmdFunction`, definition collection, `new_function`,
`mvp_lmd_function_new`, `call`, `inline_body`, capture analysis; shared Function
and environment helpers only if a measured gap requires an interface extension.

**P2a: immutable creation metadata.** Compute arity/formal length,
constructibility, home classification, and capture extent once per definition.
Reuse facts already collected by the frontend. Runtime creation copies these
facts and installs the live program/realm/closure identity. Keep a fresh callable
where JS can observe identity; no singleton substitution or mutable site cache.
Verify default/rest/destructured parameters, arrows, methods, generators, async
functions and observable `length`/`name`/constructibility.

**P2b: one bounded source-callback path.** Start with a profiled
`Vector.forEach`/`hasSome` call and its literal callback. These are source methods,
not intrinsic `Array.prototype.forEach`. Prove the consumer invokes its callback
synchronously and neither stores it nor observes its identity/properties. Extend
the existing guarded method/inline frame to carry callback code and captured
values through individual operands where practical (**D8.4.2v2**).

Capture the live method before evaluating arguments. Guard that captured
function's identity and the required realm/receiver facts; do not reread a
possibly replaced method after argument effects. Preserve lexical `this`, mutable
capture cells, early return, throws and finally. On a miss, use ordinary callable
creation/invocation with already evaluated arguments; if correct fallback would
replay effects or expose identity, retain the materialized closure instead.

Only after both call proofs succeed may `new_function`/environment allocation be
elided for that invocation. Preserve real stack/recursion guards. Do not change
the dynamic `Item* + argc` protocol globally, reuse one closure across iterations,
or turn `Vector` into a builtin by name.

**Exit:** measured closure/environment allocations fall at the selected unchanged
Havlak site; replacement, callback escape/identity, recursive callbacks, capture
mutation, extra-argument effects and GC stress all retain behavior. One bounded
consumer is sufficient for the milestone; unproven consumers keep fallback.
Evaluate P2a and P2b separately so small metadata savings are not attributed to
allocation elimination.

### P3 — Carry shape and array facts through the hot graph

**Files:** MVP `shape_walk`, `object_shapes`, `own_data_field`,
`read_reference`, `property_access`, `numeric_region_path`, dense-array analysis;
shared shape/representation emitter helpers.

- Follow constructor, field, return and indexed-element facts to receivers such
  as `UnionFindNode`, `Vector`, and dictionary entries. Determine which current
  proof is missing using the source/code map, then extend that proof rather
  than adding a special case for these class names.
- Use immutable source-predicted shapes and guard chains. Within a region proven
  free of relevant mutation/reentrancy, reuse the checked shape/data/field
  result in invocation-local registers. Extend the existing region machinery;
  do not introduce mutable lookup state (**D8.4.1v2**).
- Retain JS ownership, descriptor, own-shadowing and prototype checks. Recheck
  writable storage after RHS effects. A pointer to movable data must not survive
  a safepoint; root its owner and reload afterward. Lookup equality alone does
  not prove a previously loaded property value is unchanged.
- For matrix kernels, first apply P1, then inspect remaining array guards and
  loads. Extend `dense_array_loop` with the smallest missing local/effect proof.
  Reuse Lambda's Array/ArrayNum physical access; do not change ordinary JS arrays
  into typed arrays or skip holes, inherited indexed getters, resizing or
  out-of-bounds semantics. Bailout must resume without replaying completed writes.
- Revisit allocation only if the new profile still justifies it. Constructor
  shape reuse and small-object scalar replacement already exist. Havlak stores
  nodes before calling `initNode`; preinstalling its properties can expose fields
  too early. Graph nodes have observable identity and are not blanket candidates
  for scalar replacement. Temporary vectors passed through methods need the P2
  escape proof before any allocation removal.

**Exit:** a profiled family has fewer live lookup/decoder calls or allocations,
unchanged results, and bounded generated code. Validate field deletion/retyping,
accessors, inherited method replacement, proxies, foreign Lambda values,
array holes/growth, alias writes, and precise roots. Keep the shared kernel as
the semantic miss route; no new JS object representation is needed.

### P4 — Reduce compilation cost without sacrificing execution

**Files:** MVP call expansion, `compact_property_dispatch`, function planning and
MIR diagnostics; existing MIR/code-size budgets.

- Record parse/analysis, MIR emission, optimization and code-generation times
  separately. Rank Havlak functions by emitted instructions and compile cost.
  The whole-process gap alone does not identify which phase is responsible.
- Prefer a guarded direct entry when inlining would replicate a large fallback
  graph. Factor equivalent emission through existing helpers, retain the compact
  semantic dispatch arm, and budget actual emitted cost across nested expansion.
- The current four-method candidate bound, 768-node method expansion budget and
  numeric-region limits already exist. Diagnose where source-node budgets miss
  emitted growth; do not raise or remove them indiscriminately. Avoid an eager
  general lazy-JIT redesign as part of this milestone.
- Measure both short matched Havlak and canonical 50-repeat execution. A smaller
  module that makes the long run slower is a tradeoff to report, not an automatic
  win. Measure module MIR, optimized/native bytes, peak memory and roots/homes.

**Exit:** a source-attributed reduction in compilation work and total latency,
with targeted execution gains retained and unchanged size budgets passing.
Set a numeric compile-time target only after the phase profile; the current
12.9× MIR ratio is not a promise of 12.9× removable work.

### P5 — Conditional library follow-up

After P1–P4, profile `revcomp`, `regexredux`, `knucleotide`, and `json` on unchanged
JS source. Pick one dominant shared operation per milestone.

For strings, reuse shared builders and character/ASCII proofs only when JS
UTF-16 indexing and coercion permit them. A concatenation-loop optimization must
prove the intermediate string is unobserved; it cannot change coercion order.
For RegExp, inspect existing subject preparation, scratch, compiled matching and
result materialization before adding anything. Preserve `lastIndex`, zero-width
progress, captures, replacement callbacks, overridden hooks and Unicode modes.
Lambda's pattern semantics do not license substituting a different matcher for
arbitrary ECMAScript expressions. Vendor engines remain untouched.

**Exit:** one diagnosed unchanged-source workload improves against the same
engine control, with semantic fallback and focused conformance coverage.
Defer this phase if profiles show the ratio is mainly a port/algorithm difference.

## 7. Validation, regression policy and completion

Do not rebuild the removed broad MVP script duplicates. Allocate tests by role:

| Evidence | Home / gate | What it establishes |
|---|---|---|
| Runtime behavior | Existing `test_js_gtest`, focused `test_js_opt_gtest`, retained ownership/entry tests in `test_js_mvp_lmd_gtest` | Newly admitted behavior and adversarial fallback edges |
| Emission contract | `test_js_mir_emission_gtest`, `test/mir/js`, `test/mir/js_opt` | Native loop/call/field path exists; no per-site cache state; code stays bounded |
| Lifetime | Existing forced-GC/poison profiles on affected closure/field/array cases | Precise roots, movable data reload, scalar-home ownership |
| Shared runtime | `make test-lambda-baseline` | Required aggregate Lambda/input regression gate |
| JS baseline | `make test262-baseline` | Required saved Test262 baseline |
| Complete configured JS suite | Pinned MVP runner and immutable corpus policy from the stability audit | Every selected test fully passes without retry, timeout, crash, partial or missing result |
| Performance | Frozen release controls/candidate, unchanged contracts and paired oracles | Runtime gain, compilation cost and regression evidence |

Extend existing focused cases where possible. Add a separate regression only
for a distinct missing invariant, not another copy of general ECMAScript
coverage. Any new golden-driven Lambda fixture needs its `.txt`. No test should
require an optimizer to bypass a required guard merely to meet a timing goal.

For each implementation phase, run focused checks and a short release screen.
At the integration milestone run both required baselines and the **entire
configured** Test262 suite. The last qualification was **40,782/40,782**, with
**2,131 existing exclusions** and zero retries/timeouts/failures; see
[the stability audit](JS_MVP_Test262_Stability.md). Rediscover and reconcile the
inventory if the pinned corpus/policy legitimately changes. Do not freeze a
stale expected count, shrink selection, extend timeouts, or modify the harness
to conceal a runtime failure. A zero process exit alone is insufficient.

Performance acceptance should use:

1. Private release binaries with source, binary, input and runner hashes; the
   same flags and inputs on both engine lanes. No debug timing or concurrent
   build. Keep instrumentation out of measured execution.
2. Five-to-seven pairs for triage, then at least 21 alternating pairs for each
   claimed gain, with identical-control captures. If controls disagree materially,
   treat the run as inconclusive and repeat under stable conditions.
3. A candidate/control interval below 1 against both controls for a claimed
   improvement, plus a predeclared practical threshold. Use a provisional 10%
   execution reduction for a targeted Havlak phase; do not promise cumulative
   phase gains or exact parity with a different-language port.
4. The full current JS benchmark inventory and exact output checks. The latest
   shared-dispatch screen covers 84 fresh non-jq contracts; the four jq rows in
   Result10 are historical. Rerun them to claim a fresh 88-workload qualification,
   or explicitly report 84 fresh plus four unqualified historical rows.
5. Per-row execution, total wall time, compile phases, code size, memory and
   allocation/GC counters captured separately. Investigate a slowdown confirmed
   against both controls; a favorable geometric mean cannot hide one.

**Completion:** P0 contracts are trustworthy; P1 canonical numeric loops use the
proved native lane; Havlak has at least one independently measured P2/P3 gain;
compilation costs are reported; all semantic and aggregate gates pass. P5 is a
separate conditional milestone. An inconclusive phase is recorded as such and
is not retained merely because its hypothesis sounded plausible.

## 8. Evidence and reproduction

Analysis tree: `b6f0005ec91a09db329383646405518dc7ae7a7a`. Existing dirty edits
to `test/benchmark/julia/micro_common.ls` and `micro_common2.ls` predate this
analysis and were retained. No runtime or canonical benchmark changes were made
for this plan.

All new execution used the already qualified private release:

```text
temp/js_mvp_test262_stability/final_release.exe
SHA-256: 6af99034e5a0196a0610d2151d7e4f5b420918602e624d4c39200656a46352c5
JS_EXEC_BACKEND=mir
JS_EXECUTION_BACKEND=mir
JS_MIR_INTERP=0
LAMBDA_JS_LARGE_INTERP=0
LAMBDA_EXEC_BACKEND=jit
LAMBDA_TIER=jit
```

Artifacts under
[`temp/js_mvp_untyped_tuning_plan_20261019/`](../../temp/js_mvp_untyped_tuning_plan_20261019/):

- `matched_havlak.py`, `matched_havlak.json`, `repeat_1/`, `repeat_50/`:
  asserted source substitutions, frozen hashes, every output, MIR and timing.
- `numeric_probe.py`, `numeric_probe/results.json`: source-only accumulator
  probes, identical controls, outputs and before/after MIR.
- `profile_havlak.py`, `havlak_sample_symbolicated.txt`,
  `havlak_profile_summary.json`, `summarize_sample.py`: execution-scoped native
  sample, matching-link-map symbolication and nonduplicated category counting.
  The initial sandboxed attach failed; the later authorized sample succeeded.
- `analysis_provenance.json`: source/link-map hashes and emitted MIR metrics.

Reproduce the measurements sequentially from the repository root:

```sh
python3 temp/js_mvp_untyped_tuning_plan_20261019/matched_havlak.py
python3 temp/js_mvp_untyped_tuning_plan_20261019/numeric_probe.py
python3 temp/js_mvp_untyped_tuning_plan_20261019/profile_havlak.py
python3 temp/js_mvp_untyped_tuning_plan_20261019/summarize_sample.py
```

The sampler needs permission to attach to its own launched process. Temporary
artifacts are diagnostic and may not survive checkout cleanup; the tables and
root-cause findings above are the durable record. Earlier port timings are in
`temp/lambda_vs_js_mvp_20261019/final/comparison.json`; their Havlak comparability
claim is corrected here. This original analysis predates the implementation in
§9; its source-only probes are not engine performance results.

## 9. Implementation, 2026-10-19

### 9.1 Changes

- **P0:** `test/benchmark/js_mvp_lmd/contracts.py` freezes relative Lambda imports
  transitively under the benchmark root (**S16.9.8**), rejects missing/unsupported
  imports, and audits numeric annotations in every imported source. Havlak work
  contracts record dummy/full recognition counts independently of its output.
  The expansion runner labels canonical Havlak as unmatched; its new
  `--havlak-matched-repeats=1|50 --lambda-reference` option versions frozen copies
  with matching counts. Four contract tests cover mismatch rejection, imports,
  annotations, cycles and missing inputs. Canonical benchmarks remain unchanged.
  An explicit control manifest also adds both engine-control lanes to fresh
  workloads; Node remains their independent output oracle.
- **P1:** `block_initializer_dominates` reuses the shared AST binding-use index
  (**D8.2.4**) to admit numeric `var` locals whose every observation follows the
  complete initializer statement within the same block activation. Captured,
  eval-visible, generator/async and implicit-write bindings remain excluded.
  Existing representation/range inference and `MirEmitter` ownership carry the
  resulting native lanes (**D5.3.4**).
- **P2a:** formal length, duplicate-parameter and callable/home classification
  are definition metadata. Creation still allocates fresh Functions and installs
  the live program, realm, receiver and capture cells.
- **P2b:** a fixed source class method consuming a single literal arrow callback
  can inline one synchronous, nonescaping consumer. All uses of its callback
  parameter must be direct calls in that consumer. Existing bounded inline-body
  admission applies to both bodies. The guard tests the captured method's ABI,
  entry, context, program and object receiver; fallback materializes the fresh
  callback and invokes the already captured callee. Extra arguments, spread,
  captured parameters, escaping/inspected callbacks, dynamic classes, eval,
  complex parameters and unproved bodies retain ordinary calls. Mutable captures
  and lexical `this` retain the shared precise cells; no per-site cache was added
  (**D8.4.1v2**, **D5.3.4**).
- **P4:** a native compilation sample found repeated recursive traversal in
  `primitive_property_key`, rather than binding lookup, dominating the planning
  phase. A query-local node visitation table reuses conjunctive obligations and
  the existing induction across cycles. It is discarded after each root query,
  so provisional cycle assumptions never escape into a different proof or a
  changed representation pass. Havlak's emitted MIR before/after this change is
  identical after pointer normalization. Optional `JS_TRANSPILE_TIMING=1`
  diagnostics now map source spans to shared instruction/root/home telemetry,
  emission time and eager-link code-generation time. Generated addresses continue
  to use `jit_log_code_addresses`; no new address registry exists.

Two MIR fixtures exercise the newly admitted numeric lifetime and callback path,
with behavior checks for hoisted undefined, skipped initialization, captures,
numeric widening, eval, exceptions, NaN/negative zero, replacement, getters,
callback identity/escape, recursion and argument effects. Existing GTests and
Test262 retain general language coverage; the retired 817-case duplication is
not restored.

### 9.2 Qualification record

Artifacts: `temp/js_mvp_untyped_tuning_impl/`. Original performance release:
`final.exe`, SHA-256
`94d3a838043597fa3eacfac08f456cb1289d6a097078c0816488d1e0884ac873`.
The original qualified release in §8 is the engine control. `final_provenance.json`
and `runtime.patch` pin the source delta and executable/link-map identities.
`control_source_check.json` verifies all recorded JS runtime sources against the
pre-tuning HEAD; its `main.cpp` differs only by the subsequently added sentence-game
demo catalog entry. The Test262 fixes were already present in the control binary.

**Release provenance correction:** a later timing-enabled Json probe crashed in
the original archive's `timed_codegen_interface` on MIR's null finish callback.
Disassembly confirms that archive lacks the null guard already present in the
recorded source. The freshly rebuilt release contains the guard and passes the
same probe. It is preserved as `qualified.exe`, SHA-256
`63fe391be4de54417ba6bda1eb162fe0fd22e3283370c1a869035ca6d17ad518`;
the source hash remains unchanged. The full configured Test262 gate was repeated
against this exact executable; the strict result and its slow-case diagnosis are
recorded below. Earlier normal-mode performance captures
remain attributed to `final.exe`; they must not be relabeled with the new hash.
See `qualified_provenance.json`, `json_phase.stderr`, and
`json_phase_current.stderr`. The final Lambda baseline used a rebuilt debug host
that already contains the guard.

The first 21-pair capture (`paired21/comparison.json`, derived wall intervals in
`paired21_summary.json`) passed every output and retained two identical control
lanes. Times below are medians, in milliseconds:

| Workload | Control execution | Candidate execution | Candidate/control | Control wall | Candidate wall |
|---|---:|---:|---:|---:|---:|
| π-sum | 23.112 | 3.238 | 0.140 | 122.324 | 77.244 |
| matrix statistics | 95.188 | 60.270 | 0.633 | 266.030 | 190.400 |
| DeltaBlue | 105.914 | 99.955 | 0.944 | 1449.516 | 1565.727 |
| Havlak | 2836.139 | 2667.166 | 0.940 | 6616.327 | 5025.515 |
| Json | 16.806 | 18.336 | 1.091 | 3849.150 | 4095.748 |

π-sum and matrix have one-sided 95% ratio upper bounds below 0.141 and 0.636
against both controls. DeltaBlue's execution improvement is small; its **8% wall
regression is confirmed against both controls**, attributable to callback MIR
expansion (roughly 10% more MIR). That tradeoff is not hidden by an
aggregate mean. Json and Havlak controls drifted materially under concurrent host
load; their execution comparisons are inconclusive. Havlak's ratio upper bound
exceeds 1 against both controls, so its 6% median difference does not qualify the
provisional 10% target. The subsequent repeat below resolves this uncertainty.

The complete repeat (`paired21_repeat/comparison.json`, `summary.json`) retains
all 21 rounds and both controls. No build or test ran concurrently with this
capture, although system indexing remained active:

| Workload | Control execution | Candidate execution | Execution ratio | Control wall | Candidate wall | Wall ratio |
|---|---:|---:|---:|---:|---:|---:|
| DeltaBlue | 105.520 | 99.338 | 0.941 | 1442.602 | 1573.659 | 1.091 |
| Havlak | 2706.103 | 2469.199 | 0.912 | 6295.905 | 4733.768 | 0.752 |
| Json | 9.755 | 9.809 | 1.006 | 1676.982 | 1732.741 | 1.033 |

Havlak execution and wall ratios are below 1 against both controls at 95%
confidence; the execution gain is **8.8%**, below the provisional 10% target.
DeltaBlue's **9.1%** wall regression and Json's **3.3%** wall regression are
confirmed against both controls. Json has no material execution change. Static
MIR attribution (`paired21_repeat/mir_growth.json`) shows DeltaBlue growing from
235,554 to 259,117 instructions and Json from 236,092 to 242,762. Json's entire
6,670-instruction increase is in bundled `Vector.remove`, `Set.contains`,
`Set.collect`, and `IdentitySet.contains`; its parser functions are unchanged.
Eager generation compiles those helper bodies even for this parser workload.
A follow-up should budget emitted expansion or avoid specializing demonstrably
uncalled bodies; no benchmark-name restriction or lazy-JIT redesign was added.

The rebuilt release's final 21-pair target capture
(`qualified_targets21/comparison.json`) passes all 198 measured/preflight
outputs. It records Havlak execution at **2,536.620 ms** versus
**2,756.764/2,789.054 ms** for the two controls: an **8.0%** reduction against the
first control. Process wall time is **4,858.281 ms** versus
**6,473.961/6,419.834 ms** (25.0% lower against the first control), with wall
ratio upper bounds **0.7634/0.7681**. Execution ratio upper bounds are
**0.9314/0.9227**, while the
control/control interval includes 1. The provisional 10% execution target remains
unmet. Unchanged π-sum runs at **5.982 ms** versus **30.408/32.402 ms** (**5.08×**
against the first control); matrix statistics at **69.783 ms** versus
**109.763/109.785 ms** (**1.57×**). Both improvements qualify against both controls.
The final capture includes external host load and retains every sample; it is
not described as a quiet-host measurement. The broader five-pair screen is
recorded separately.

The rebuilt release's broader five-pair screen passes **81/81** contracts.
Together with the target capture this is **84/84** distinct non-jq contracts and
**1,662** successful captures. All 88 distinct source/record hashes and 44
source, runner and policy hashes were rechecked (`qualified_benchmark84.json`).
The full published inventory is 71 standard workloads plus 17 microbenchmarks;
the standard-workload manifest alone does not contain the micros.

`qualified_screen81/summary.json` flags startup increases for Bounce (16.5%),
DeltaBlue (9.4%) and Storage (9.0%). These are five-pair screening results under
host load. Bounce/Storage each emit 6,237 additional instructions in the same four
bundled helper methods expanded in Json; no other function metrics change.
The screen also flags small execution differences for Towers (1.5%) and
log-pipeline (2.7%). Their function/local/opcode/call counts and emitted instruction
sequences match after address normalization (`screen_mir_comparison.json`);
that does not by itself rule out a runtime/layout effect. A separate 21-pair
repeat of those two rows ran after conformance testing against the same
candidate; its results follow. The known DeltaBlue/Json compilation tradeoff remains explicit.

The 21-pair Towers follow-up confirms a small execution regression:
**2.234 ms** versus **2.180/2.190 ms** (2.5%/2.0% slower, respectively).
Both two-sided 95% ratio intervals are above 1 (approximately
1.007–1.045 and 1.007–1.046); the control/control interval includes 1.
The absolute difference against the first control is 0.054 ms. Unchanged
address-normalized MIR does not explain or eliminate this result; its runtime
or code-layout cause remains unassigned. No benchmark-specific workaround was
added. Evidence: `qualified_followup21/comparison.json` and
`screen_mir_comparison.json`.

The log-pipeline repeat passes all outputs at **5,275.707 ms** versus
**5,180.314/5,132.482 ms**. Its execution ratio intervals are **0.994–1.038**
and **1.015–1.040**. The difference is therefore **not confirmed against both
controls**; the process-wall comparison has the same mixed outcome. It remains
inconclusive, rather than being labeled a gain or a resolved regression. The
follow-up preserves all 132 additional successful captures in
`qualified_followup21/`; no sample was discarded.

The refreshed five-pair matched-port screen passes all outputs. For 2 full
recognitions MVP/Lambda execution is 121.474/76.118 ms (1.60×); for 51 it is
2530.412/1917.840 ms (1.32×). Respective process wall medians are 2496.016/180.169
and 4759.493/2023.118 ms. These are different-language, different-representation
ports, without identical Lambda-control lanes; they are context, not the causal
engine A/B qualification. Evidence: `matched1_perf/`, `matched50_perf/`.

The P4 diagnostic reduced Havlak's lower-pass planning from 1,681,832 to 31,673 µs.
MIR emission remained about 190–198 ms; eager MIR linking/generation about
2.06 seconds. These instrumented phase captures explain the mechanism; they are
not paired latency acceptance. The earlier diagnostic's `link_us` includes eager
code generation, and its later `codegen_us` was address retrieval/logging; the
final diagnostic now labels these accurately.

Callback expansion increases canonical Havlak's emitted MIR from 257,593 to
295,602 instructions (14.8%). This is a real compilation/code-size tradeoff,
bounded by the existing inline admission and caller budget. Do not describe it
as a code-size reduction. The query-local analysis improvement leaves that MIR
unchanged.

Separate execution-window samples show closure-creation paths in 323/1,642
control stacks (19.7%) and 54/1,690 candidate stacks (3.2%). A GC diagnostic run
records 31 versus 25 collections, and five versus four full data compactions.
These observations support the callback-allocation mechanism; sample shares
overlap and are not allocation counts or paired speedup estimates. Peak resident
set is approximately 832 versus 826 MiB, while macOS's separate peak memory
footprint counter is 538 versus 554 MiB: there is no general peak-memory reduction
claim. TypeMap lookup is now the largest named sampled category (23.3% inclusive),
supporting P3 as follow-up. Evidence: `havlak_gc_comparison.json`,
`havlak_profile_summary.json`, `profile_metadata.json`.

Completed focused checks:

- Four workload-contract tests; matched Havlak versions with 2 and 51 full
  recognitions pass in MVP, untyped Lambda and Node.
- Both new MIR fixtures pass their emission checks and behavioral oracles.
  All 36 MIR-emission cases also pass with compile timing enabled against the
  rebuilt release (`timing_emission.log`).
  Forced-GC/freed-memory-poisoning runs pass, including callback exception
  propagation through a call and the caller's `finally`.
- Nine existing native closure, ABI, computed-key and recursive-inlining cases
  pass with forced GC and freed-memory poisoning (`focused_native_gc.log`).
- Three existing numeric-binding/local-fact and callable-metadata trace contracts
  pass against the rebuilt private debug host (`focused_opt_debug.log`). The
  initial release-host attempt lacked the debug trace hooks; its child scripts
  completed successfully, and the correctly configured trace rerun passes.
- **84/84 non-jq benchmark contracts**, 251 successful captures: 83 workloads
  against two identical engine-control lanes, and hashmap against Node. Input
  hashes were rechecked in `correctness84.json`. This is a correctness capture
  under load, not a latency qualification.

The interrupted `full88_correctness` capture is preserved. The four jq contracts
remain **unqualified**, as allowed by §7: archived runs already exceed the
180-second screening budget, and this milestone does not claim a fresh 88-row
result. No Test262 exclusion or timeout was changed.

The original archive's full configured canonical Test262/MVP run passed
**40,782/40,782**, with
**zero failures, timeouts, crashes, partial passes and retries**. All 413 batches
returned all expected results; the failure manifest has no data rows. The
existing 2,131 exclusions and pinned corpus remain unchanged. Evidence:
`test262_full.log`, `test262_full_summary.json`, `test262_full_artifacts/`.

The first full configured run against the rebuilt `qualified.exe` is **not a
strict pass**: 40,780/40,782 fully passed, zero semantic failures, two slow partials
and two Phase 4 retries. All 413 batches completed with all results; no timeout,
crash, missing result or batch kill occurred. The two RegExp match-indices tests
exceeded the unchanged three-second CPU gate (3.388/3.423 seconds). Evidence:
`test262_qualified_full.log`, `test262_qualified_full_summary.json`, and
`test262_qualified_full_artifacts/`. A zero process exit status does not satisfy
the strict gate.

Both exact inputs pass serially at approximately 1.25 seconds CPU in the
untuned control, original archive and rebuilt release. Four concurrent workers
also pass: maximum CPU time is 1.771 seconds for the control and 1.753 for the
candidate. Replaying the surrounding 100 canonical tests passes 100/100; the two
cases take 1.230/1.223 seconds. A diagnostic attributes most of their latency to
eager code generation for `propertyHelper.js`/`deepEqual.js`, not the RegExp
assertions. This does not establish the cause of the full-run slowdown; no
runtime defect has reproduced and no timing gate, exclusion or harness was
changed. Preserve `indices_repro/` and record the final qualification separately.

The final qualification uses a fixed **two-worker** budget on this host's four
performance cores (plus four efficiency cores). Live inspection also found an
unrelated demo and Spotlight consuming CPU; neither was interrupted. This is a
resource setting, not a change to test selection or timing limits. It does not
prove the cause of the previous four-worker slowdown or qualify four-worker
timing stability. Host evidence is in `qualification_host_context.json`.

The final saved Test262 baseline against `qualified.exe` passes
**40,261/40,261**, with zero failures, partial results and retries; all 169 batches
return their expected results. This uses the standard `make test262-baseline`
target and its default seven workers. Evidence: `test262_qualified_baseline.log`,
`test262_qualified_baseline_summary.json`, and
`test262_qualified_baseline_artifacts/`.

The final **full configured MVP/MIR** run against that same executable passes
**40,782/40,782**, with **zero failures, timeouts, crashes, partial results,
missing results and retries**. All 413 batches return their expected results;
the failure manifest has no data rows. It uses the fixed two-worker budget and
takes 2,016.4 seconds. The original 2,131 exclusions, canonical corpus and timing
limits are unchanged. The two previously slow match-indices cases now take
1.317/1.543 seconds wall time and pass their original CPU gates. Evidence:
`test262_qualified_two_workers.log`,
`test262_qualified_two_workers_summary.json`, and
`test262_qualified_two_workers_artifacts/`. This qualifies the recorded resource
setting; it does not erase or resolve the earlier four-worker timing limitation.

The final Lambda aggregate baseline passes **7,888/7,888**: 5,776 runtime tests
and 2,112 input cases, with zero failures, partial passes or skips. The aggregate
includes **20/20** MIR size-ratchet checks; `test/mir/mir_budgets.json` is unchanged.
Evidence:
`lambda_baseline_final.log`, `lambda_baseline_final_summary.json`, and the
`final_*_results.json` snapshots. An earlier saved Test262 baseline against the original archive also passed
40,261/40,261 (`test262_baseline.log`, `test262_baseline_summary.json`); the exact
final-binary result above supersedes it for release qualification.

The first Lambda aggregate attempt passed 7,886/7,888. It had a forced-GC child termination:
`js_dynamic_adapter_span`, exit 137 with no output. Its immediate isolated
reproduction and three additional repetitions pass both GC schedules. The
concurrent OS log reports provenance-sandbox launch failures in another checkout
at the same time, but does not establish the exact cause of this child's kill.
No runtime or harness workaround was added. Preserve `gc_stress_initial_failure.json`,
`gc_span_repeat.log`, `gc_span_three.log`, and `gc_span_system.log`; the final
complete baseline repeated the existing two-worker setting, without changing
selection or timeout budgets, and passed all 317 GC stress cases.

The other failure was an existing duplicated PDF oracle:
`BinarySchemaTest.LambdaPdfOffsetsAndImageStreamsAreValidInBothTiers` expected
eight booleans in C++, although the committed script and its canonical `.txt`
already contain eleven checks. Both execution tiers and the PDF checker succeeded.
The native test now reads the three existing canonical goldens instead of keeping
independent C++ copies. No script, expected result or PDF validation was weakened;
the focused two-tier PDF case passes. This test-only repair does not alter the
qualified runtime executable.

### 9.3 Remaining scope

The core milestone has a verified P1 gain, a measured P2 gain, reduced Havlak
planning cost, and passing aggregate/conformance gates. The provisional 10%
Havlak execution target is not met (final gain: 8.0%). Towers' 2.5% execution
regression remains unattributed; callback-driven startup/code-size growth and the
four-worker Test262 timing limitation remain explicit acceptance limits.

P3's additional receiver/array fact propagation and P5's string/RegExp follow-up
are not implemented. P4's repeated-analysis fix is implemented; further emitted
code budgeting and separate generated-native-byte accounting remain open. The
DeltaBlue/Json compilation regressions belong to that follow-up. The initial
bounded milestone targets the demonstrated
numeric lifetime, callback and compilation costs; a new profile must justify any
further shape/array expansion. Existing shared shape access, dense-array guards,
constructor reuse, call ABI, completion propagation and rooting policy remain
the mechanisms to extend. No formal ruling changed.

# Typed Lambda: implementation proposal for reducing common overheads

Status: implementation in progress; acceptance criteria in §3 remain open.

Date: 2026-09-29. Inspected revision: `7d45dda98ecdfd3f390c15d5f00604001809d073`.

Location: `./impl`, as requested. This is an informative implementation plan.

The objective is a substantial improvement across unrelated typed workloads by
removing repeated numeric, access, call, and ownership protocol. The central
change is to preserve and consume proofs across expressions, loops, and calls,
instead of adding another collection of benchmark-specific fast paths.

The [formal semantics](../doc/Lambda_Formal_Semantics.md) and
[formal design](../doc/Lambda_Formal_Design.md) remain authoritative. In
particular, **S1.6**, **D2.4.1–D2.4.3**, **D5.3.1–D5.3.4**, and **D8.2.5v3**
govern representation transparency, proof ownership, precise rooting, and the
single compiler pipeline. This proposal changes no ruling or public ABI.

## 1. Why the previous round did not produce broad improvement

The final Result49 comparison used the original archived release and the tuned
release on identical final Lambda sources, with five alternating pairs for each
of 63 typed rows. Output matched in every row. Geometric-mean execution time
was **0.9586×** the original: about **4.14% less time**. The sum of row medians
fell from **7,492.306 to 6,070.803 ms**, or **18.97%**. Large improvements in
several expensive rows account for the much stronger summed-time result.

Holding the old C2MIR medians fixed gives approximately **3.03×** typed/C2MIR,
down from **3.1615×**. This is an inference across runs, not a fresh simultaneous
C2MIR measurement. The earlier **2.5×** planning target was not reached. See the
[Result49 analysis and implementation history](../vibe/impl/Lambda_Benchmark_Result49_Typed_Analysis.md)
and the [final paired samples](../temp/result49_typed_impl/paired_result49_final_with_call_vs_original.json).

Private-array growth, ASCII text operations, bounded loop proofs, typed path
reuse, and small aggregate elimination helped where their admission predicates
matched. They did not solve the common loss of facts at boundaries. Once a
range, nullness, storage, or effect fact disappears, the next operation often
needs a check, a conversion, a helper call, root publication, and a storage
reload. These costs reinforce one another.

There is no established universal removable 3× tax. Several original Result49
scalar workloads, including `sum`, `sumfp`, `ack`, and `matmul`, were close to or
faster than their C ports. They are useful controls for avoiding regressions.

## 2. Evidence and its limits

### 2.1 Current release inspection

The follow-up inspection used a fresh release at the revision above, archived
as [lambda_current_release](../temp/result49_common_overhead/lambda_current_release),
SHA-256 `cf062095b7e08b33912c29bec11356ac2dcf44306fa0a42cdd86c3182b2d66e0`.
Lambda ran with forced JIT and MIR caching disabled. The C reference used the
standalone C2MIR driver, built from pinned MIR revision
`99c65079038f3ba9242ef646f308c266cfd7a8e5`. This driver is a benchmark tool;
the retired Lambda C-text back end remains retired.

The following are **static finalized MIR instruction counts before backend
optimization**, including cold and potentially dead paths. They are not dynamic
instruction counts, CPU percentages, or predicted speedups.

| Routine | Typed Lambda | C2MIR C port | What merits investigation |
|---|---:|---:|---|
| Fib `fib` | 51 | 9 | Native stack probe, finite-int checks, cold arithmetic helpers |
| Towers `push_disk` | 199 | 10 | Array witnesses, checked mutation, var homes, conversions |
| Towers `pop_disk_from` / C `pop_disk` | 203 | 9 | Repeated indexed-read and mutation protocol |
| Towers `move_disks` | 257 | 21 | Repeated small-call protocol and integer checks |
| NBody `advance` | 3,135 | 102 | Guarded loop plus generic sibling; boxing and store paths |
| SHA1 `rol` | 125 | 5 | `int`/`u32` transitions, shift validation, result publication |

Source and MIR hashes and per-function call targets are in the
[static census](../temp/result49_common_overhead/current_static_census.json).
Fib already uses native scalar arguments/results and has no GC root frame;
"everything is boxed" is not an accurate diagnosis. NBody's static size does
not mean its hot path executes thirty times as many instructions.

The C programs implement their own representations and contracts. For example,
SHA1's C rotate stays in `uint32_t`, whereas the Lambda helper accepts and
returns `int` around a `u32` operation. Towers' C port directly mutates fixed
integer arrays. Lambda must preserve total `int` arithmetic (**S4.1.1–S4.1.2**),
nullable reads (**S7.1.1v3**), checked writes (**S7.1.3v2**), and snapshots
(**S9.1.2–S9.1.3**). Optimizations must prove these obligations irrelevant at a
particular operation; deleting them to match C is not valid.

### 2.2 Executed ownership counters

| Current diagnostic run | Recorded work | Consequence |
|---|---|---|
| Towers | 8,191 unique ArrayNum mutations; zero recorded shared copies | Copy elimination cannot explain its remaining gap by itself |
| NBody | No recorded COW table activity | Inspect arithmetic/access protocol and native code before blaming COW |
| Richards | 710,700 unique map mutations; 1,000 map copies / 64,900 bytes; 100 ArrayNum copies / 10,400 bytes | Repeated access/mutation protocol deserves priority over bulk copying |
| Splay | 951,168 map copies / 61,825,920 bytes | Ownership and lifetime analysis is a separate high-value track |

These are existing runtime counters from the `*_current_cow.tsv` files under
[the diagnostic artifact directory](../temp/result49_common_overhead/).
`array_checked_store_calls == 0` does not establish that no bounds checks or
mutation helpers executed: `array_num_set_cow_idx` is outside that counter.
Instrument each actual path before drawing a broader conclusion.

These runs were diagnostics, not a new timing comparison. The current
[manifest](../temp/result49_common_overhead/manifest.json) hashes entry scripts
but does not cover every transitive import or C build input. The next baseline
must include those inputs, notably `test/benchmark/richards2_core.ls`.
Temporary evidence may be cleaned locally; the final implementation report
must retain a durable compact manifest, raw measurements, and result tables.

## 3. Work order and success criteria

| Priority | Work | Expected breadth | Required evidence |
|---|---|---|---|
| P0 | Complete executed-work census and reproducible baseline | All benchmark families | Comparable runs and identified hot protocol |
| P1 | Order-independent, per-entry transitive effects | Direct calls, recursion, imported helpers | Fewer executed safepoint stores/reloads at proven edges |
| P2 | Shared range, nullness, and relational access proofs | Integer, numeric-array, indexing, bitwise workloads | Fewer executed checks/conversions in hot regions |
| P3 | Consume those proofs through bounded inlining and region lowering | Small helpers and nested loops | Lower call/frame overhead without excessive code growth |
| P4 | Parameter/path effects and precise storage invalidation | Record/array mutation and graph algorithms | Fewer traversals and redundant checks/reloads |
| P5 | Aggregate lifetime and ownership improvements | Temporary numeric aggregates and copy-heavy graphs | Fewer materializations/copies with unchanged snapshots |

P1–P4 are the main attempt at broad improvement. P5 addresses additional
materialization and genuine copying; Splay's benefit must not substitute for
success on unrelated families. Expected breadth is a hypothesis, not a speedup
forecast. P0 may change the order if the executed evidence contradicts it.

Proposed acceptance targets, fixed before the final campaign:

- Final/current release geometric-mean time **≤ 0.90× across all 63 typed rows**.
- At least **three independent mechanism families** each show **≥ 5% lower
  geometric-mean time**, with a paired confidence interval excluding parity.
  Families and membership are frozen in P0; benchmark-suite names alone do
  not define independent mechanisms.
- Every row remains in the report, including unchanged and slower rows.
  Confirmed material regressions require a fix or an explicit documented
  tradeoff; noisy sub-millisecond rows require a longer matched harness.
- Full correctness gates pass. No performance gain compensates for a root,
  snapshot, null, error, or numeric-semantic violation.
- Compilation time, peak compilation memory, and generated code size are
  reported. A >10% aggregate compile-time or code-size increase triggers
  review; a >25% per-workload increase requires a specific justification.

These are engineering acceptance targets, not claims that the proposed work
will reach them. The old 2.5× C2MIR ratio remains a stretch comparison and must
be measured anew. If only a few rows improve, report bounded gains and leave
the broad-performance objective unmet.

## 4. P0 — measure the overhead that actually executes

Extend existing compiler timing, MIR diagnostics, and `COW_EXEC_PROFILE`
facilities before inventing another profiling subsystem. Collect per-function
and per-source-operation counts where practical, with an aggregate fallback.

| Counter group | Required distinction |
|---|---|
| Calls | Raw, boxed boundary, indirect, runtime helper; actual entry variant |
| Guards | Numeric exceptional case, null, bounds, type/shape, ownership; executed and failed |
| Representation | Native-to-Item, Item-to-native, number-home reservation, actual scalar allocation |
| Roots | Executed dirty-live stores, root reloads, frame entry, maximum live slots |
| Storage | Data-pointer reload, length reload, path traversal, admission check |
| Mutation | Unique fast path, detach, copied bytes, root replacement, share mark |
| Code | Hot-region MIR size, total MIR size, machine-code size and spills where observable |

Do not classify all boxing as heap allocation, all helper calls as safepoints,
or all safepoint costs as GC time. Count collections/allocated bytes separately.
Use native sampling or generated-code inspection to distinguish helper work,
branches, spills, and cache pressure; counters alone do not attribute CPU time.

Instrumentation is enabled at compilation/run setup. Emit no counter updates
or per-operation flag tests when disabled; avoid locks/atomics in normal hot
paths (**D5.4.4**). Timing runs use uninstrumented release binaries. Diagnostic
runs use the same source and workload but are reported separately.

P0 deliverables: complete source/build manifest, A/A noise check, source-relative
Lambda/C comparison notes, counter coverage map, and a ranked hot-operation
table. Select pilots from call-heavy scalars, numeric arrays, bitwise kernels,
record graphs, and text/collection workloads, plus already-fast controls.

## 5. P1 — complete the call-effect analysis before lowering

### 5.1 Root cause in the current implementation

In [transpile-mir.cpp](../lambda/runtime/transpile-mir.cpp),
`mir_narrow_body_gc_effect()` scans a body after
`finalize_gc_root_publication()` and `finalize_side_root_frame()`.
`mir_call_target_is_gc_free()` recognizes self recursion, audited runtime
imports, and previously recorded GC-free bodies. The recording array has 64
entries. Callers emitted earlier are not revisited; mutually recursive bodies
cannot generally establish the same proof. Binder raw variants are excluded
from this narrowing path.

Changing declaration order can therefore change available effect precision.
Merely making the array larger or sorting functions does not solve recursive
components, variant boundaries, or facts needed before frame finalization.

### 5.2 Proposed analysis

1. Build the direct call graph over **planned entries/variants**, using the
   existing function index and `FnAnalysis`/`FnVariantAnalysis`. Keep boxed
   admission, native bodies, inferred fallbacks, and imported entries distinct.
2. Derive local effects conservatively from operations and their possible
   lowerings, including slow/error paths, boxing, COW, implicit cleanup,
   prologues, and return publication. Reuse import metadata and shared lowering
   decisions; do not duplicate a name-based allowlist in the transpiler.
3. Solve transitive effects over strongly connected components. For a closed
   component, start from all local effects and union reachable effects to a
   fixed point. A recursive cycle with no collecting operation and no unknown
   outgoing edge can prove `NO_GC`; recursion alone does not imply allocation.
4. Unknown/indirect/unaudited targets remain conservative. Treat unresolved
   cross-module and hosted-language calls the same way (**D1.9**).
5. Publish stable per-entry summaries before callers are lowered. Root planning
   consumes them through the existing emitter call-effect interface.
6. Before linking or publishing code, verify the complete emitted call graph
   against the claimed summaries, including compiler-generated helpers.
   A contradiction fails compilation safely; it never publishes under-rooted
   code. A future conservative re-lowering fallback may be added if needed.

Use the existing fields independently: `may_gc`, `may_reenter`,
`may_set_exception`, `may_return_error`, `may_suspend`, and `has_unknown_call`.
Also preserve the existing number-stack/watermark effect. `NO_GC` does not mean
no mutation, no error, no scalar-home allocation, or no reentry. Purity alone
does not establish any of those claims (**D6.1.3**, **D5.2.3**).

Reuse the function collection/canonicalization and fixed-point experience of
`mir_solve_may_defect()`; factor common graph machinery instead of copying its
walk for every effect. Preserve its separate defect meaning (**D6.1.3**).
The existing solver deliberately owns its answers per compilation because
satellite compiles share ASTs. New solved entry effects and path summaries must
likewise be compilation-owned and immutable while consumed. Reuse the
`FnVariantAnalysis` schema/accessors without publishing mutable solver state
into a shared AST memo that another compile can overwrite.

### 5.3 Scheduling and implementation boundary

Place graph collection and solving inside the existing typed pass manager
(**D8.2.5v3**). The initial effect pass can produce conservative results; after
entry/representation planning, refine effects for the exact planned entries
before MIR lowering. Declare dependencies and record both costs in the same
compiler timing stream. No alternate pipeline or general second IR is needed.

The pre-lowering planner must overapproximate every helper that lowering may
emit. Start with operations whose lowering is shared and auditable. An
unsupported operation remains conservative until its lowering/effect decision
can be represented once and reused. Do not claim precise summaries from an
AST purity walk while ignoring an allocating generated fallback.

This avoids relying on a post-finalization graph pass to remove already
inserted root protocol. Retain the emitted-graph scan as a verifier, then retire
the 64-body list as an effect authority. Do not mutate finalized MIR or rerun
frame finalization piecemeal. **D5.3.2** requires transitive verification, and
**D5.3.4** keeps root/final-store insertion exclusively in `MirEmitter`.

Acceptance: forward and reverse declaration orders produce equivalent effect
summaries and hot-path root protocol; a closed recursive component is precise;
one allocating or unknown edge makes its callers conservative; graphs larger
than 64 bodies have no arbitrary precision cliff. At least two call-heavy
pilots must show a measured protocol reduction before claiming performance value.

## 6. P2 — propagate range, nullness, and access facts

### 6.1 One source of facts, with explicit lifetimes

Respect the four authorities in **D2.4.1–D2.4.3**: AST semantic contract,
analysis representation, emitted `MirValue`, and MIR physical registers.
Do not infer semantic signedness, nullability, or safety from `MIR_T_I64`.

Reuse `FnVariantAnalysis`, existing binding analysis, `MirValue`, and
`MirFlowScope`. Keep program-point ranges and path relations in transient
lowering/control-flow state tied to actual bindings and their invalidations.
Type-invariant optimization facts may use the existing lazy `Type` side record;
node-specific facts must not be placed on shared type singletons. Do not add
an ID-keyed per-node fact database (**D8.2.5v3**).

The first shared fact set should answer:

| Fact | Initial source | Invalidated or weakened by |
|---|---|---|
| Finite integer interval, separately excluding poison/null | Literal, dominating test, bounded induction | Assignment, merge, unknown value/call result |
| Exact scalar representation and non-nullness | Contract plus admission/proof | Widened union, nullable operation, incompatible merge |
| `0 <= index < length(array)` and bounded affine offsets | Loop condition and safe induction proof | Index write, root/length change, unknown aliasing call |
| Array contract/layout | Existing typed-array witness | Root replacement or incompatible store/admission |
| Place identity and uniqueness | Existing place/spine proof | Alias publication, prefix writes, replacement, relevant call |

At joins retain only facts valid on every predecessor. Use conservative
widening and a bounded iteration budget for loops; decline optimization when
the proof budget is exceeded. A budget limit must affect speed only.

### 6.2 Numeric lowering

For each arithmetic operation, prove operands exclude special values and the
result remains in the required domain before removing poison/range branches
(**S4.1.1–S4.1.2**). A non-null `int` annotation alone is insufficient. Prove
intermediate and induction-update bounds, not just final output bounds; the
analysis arithmetic must itself avoid host overflow.

Propagate representation demand through locals, branches, comparisons, and
native call/results using `em_require_rep()` and **D8.2.6**. Preserve
`FnReturnAnalysis` as the sole return-shape/companion authority
(**D5.2.1v3**). Eliminate round trips only when the source semantic value remains
identical. Keep `u32` arithmetic in its admitted lane where this is proven;
do not silently reinterpret Lambda `int` arithmetic as C wrapping arithmetic.

Preserve NaN, signed zero, exceptional arithmetic, and error behavior. Do not
enable reassociation or fast-math as a substitute for proof. Keep the existing
generic operation when precision is insufficient.

### 6.3 Indexed reads and writes

Extend the existing typed-array contract and dense-loop planners with the
shared relations. For a loop over `i`, establish the required extent once,
including affine accesses such as `base + stride * i + offset` only when all
intermediate arithmetic is proven safe. Prove each participating array's
extent independently; equal loop counts do not imply equal lengths.

When the proof holds, use the raw lane directly and avoid repeated bounds/null
tests. Otherwise preserve **S7.1.1v3** and **D2.5.3**: an invalid read produces
`null`, not a fabricated native value. Writes retain hard-error behavior under
**S7.1.3v2**, and ArrayNum storage remains non-null (**D2.6.2**).

Hoisted guards must be side-effect free and must not eagerly raise an error
on an empty or untaken loop. A failed proof guard selects the ordinary path,
which raises only at the original operation. Mutation/alias invalidation is
part of the proof, not an afterthought.

Acceptance: several unrelated numeric/indexing fixtures lose redundant hot
checks while their dynamic/nullable variants retain them; counters confirm
reductions in at least two benchmark families. MIR size alone is insufficient.

## 7. P3 — reduce calls, frames, and repeated guarded regions

Extend the existing `mir_inline_callee_ok()` and body-budget machinery using
P1/P2 facts. Prefer small scalar/access helpers whose inlining exposes range,
bounds, and lifetime facts. Use deterministic code-growth budgets and measured
call frequency; exclude recursive expansion and unsupported control flow.
Do not introduce runtime-feedback specialization or mutable generated code
(**D8.3.1v2–D8.3.4v3**).

Inlining must evaluate arguments once in source order and preserve admission,
error propagation, plain-parameter snapshots, and `var` write-back. The initial
extension stays within the existing eligible synchronous shapes. Closures,
variadics, task-dependent procedures, and arbitrary consuming calls are not
new native-entry candidates under this proposal.

For loops, reuse existing guarded/generic copies where profitable. A successful
guard provides facts for a whole region. If a later operation invalidates a
fact, transfer at a defined continuation with the exact current state. Never
restart completed mutations or replay argument effects. Prefer proof before
the first side effect to complicated mid-region transfers.

A cold allocating fallback still makes the containing callable entry
`MAY_GC`. Only calls eliminated by proof/inlining, or a region with no remaining
safepoint, avoid the corresponding publication. Do not label a whole function
`NO_GC` because its common path allocates nothing. Do not add an unchecked
third entry, arbitrary range-keyed variants, or a new return ABI; existing
raw entries retain the two proof-producing paths of **D8.3.3**.

Let existing emitter liveness and frame finalization exploit fewer safepoints.
Zero-root-frame elision, canonical dirty-live stores, scratch-slot coloring,
and fixed argument-root suffixes already exist (**D5.3.1**, **D5.3.5**). The
work is to improve the facts they receive, not replace them with per-call
root pushes. Number homes and watermarks remain separately owned
(**D5.1.1v2–D5.1.4**, **D5.2.2v3–D5.2.3**).

If counters show a wide mutable local pushing a new scalar home on each loop
iteration, implement its declaration-owned reusable home under **D5.2.2v3**.
Only reclaim an iteration's number-stack extent when **D5.2.3** proves that no
live Item points into it. This is a distinct, measured slice; it must not
reintroduce the retired caller-home function-return ABI or treat number slots
as GC roots.

The tiny `lambda_stack_is_exhausted` call is a separate measured candidate in
call-heavy scalar code. First measure its executed share. If material,
evaluate a Lambda-side equivalent probe using supported MIR/platform
facilities, with cross-platform overflow/recovery tests. Preserve entry limit
checking under **D5.1.2**; no vendor patch, assumed frame-size budget, or removal
of recursion protection is part of P3.

Acceptance: lower executed calls/root publication/frame traffic in selected
helpers, bounded code growth, and correct allocating fallbacks under forced
GC. Cold-path outlining alone does not satisfy the phase.

## 8. P4 — retain storage and ownership facts across safe calls

GC effects and argument mutation effects answer different questions. Add a
bounded per-parameter/path summary alongside existing function analysis for
reads, writes, retention/sharing, root replacement, length/storage changes, and
unknown effects. Derive it from the existing binding/use index, compose it over
direct calls, and collapse uncertain/deep paths to a conservative parent path.
Map effects through actual aliases at the call site; two parameters may name
the same root. A proven read-only call is not necessarily non-retaining.

Use the same summary for typed access, place-handle reuse, and inline admission.
Do not create independent scans that disagree about a call's writes. Existing
pure-call/read-place and repeated-path optimizations are starting points.
The summary describes a proof; it does not change the source calling convention.

| Event | Facts that may survive | Required invalidation/action |
|---|---|---|
| `NO_GC`, non-reentrant, non-retaining read-only call | Root, layout, length, uniqueness, data pointer | None for unaffected roots |
| Collecting but otherwise read-only call | Stable header identity, proven logical shape/length | Precisely root live owners; invalidate/reload live data-zone pointers |
| Non-collecting write to a disjoint field | Unaffected paths | Invalidate written-path value facts and affected aliases |
| Prefix/root replacement, resize, reorder | Facts proven independent of that prefix | Kill dependent path/extent/storage facts and reacquire at next use |
| Retention or new observable snapshot | Identity/layout as independently proven | Drop uniqueness proof; preserve required share marking |
| Unknown/reentrant call | Only facts independently stable under arbitrary permitted effects | Conservatively invalidate affected mutable state and honor safepoints |

**D4.4.4v4** requires data-zone pointers to be reloaded after allocation points;
stable object headers do not make array buffers or packed-field pointers
stable. `NO_GC` alone also does not protect a buffer from resize/replacement.

`lambda_after_may_gc_call()` already reloads cached typed-array items/length,
and records loads for liveness pruning. Extend that mechanism so GC relocation
and logical mutation have distinct invalidation reasons. Keep surviving
logical length facts only with proof; reload live data pointers when required.
Use existing emitter ownership and record reloads through its established path.

Expand synthesized place handles only within **D4.4.4v4**: runtime spine test,
tier-shared static decision, and required store-back or share mark on every
writing path. Preserve partial writes on error and `var` home publication.
Snapshot and move-out behavior remains **S9.1.2–S9.1.3**, **D4.4.5–D4.4.6**.

Acceptance: nested-loop and reordered-call fixtures reuse unaffected facts;
prefix mutation, aliasing, reentry, and allocation fixtures invalidate exactly
the required facts. Richards/Havlak-style workloads must show fewer executed
path operations, not merely a smaller helper definition.

## 9. P5 — remove unnecessary materialization and actual copies

Generalize the existing small-aggregate work through escape/use analysis and
destination demand (**D8.2.6**). Start with fixed-size numeric arrays and
records whose fields are consumed locally or written into a known destination.
Carry fields in native lanes until an observable aggregate is required.

Preserve field evaluation order, contract failures, alias snapshots, and
cleanup. At an escape, materialize once into correctly owned storage; if that
allocation can collect, scalar-held references remain precise roots. An
unknown consumer, capture, dynamic indexing, or unsupported merge declines
scalar replacement. Destination-owned scalar storage and companion returns
continue to follow **D5.2**, rather than inventing another ABI.

For Splay, first attribute copies to exact share-mark and mutation sites.
Distinguish required copies from marks caused by conservative lifetime
analysis. Extend existing permitted borrow/store-back and move-out shapes
only where **D4.4.4v4–D4.4.6** prove snapshots unobservable. Count eliminated
copies and bytes, then measure execution time and GC separately.

A general consuming-call convention or changed snapshot contract is outside
this implementation plan. If measured residual copies require such a change,
prepare a separate design decision updating both formal and working documents
before implementation. Do not promise elimination of all 951,168 Splay copies.

Acceptance: a reusable escape/destination rule succeeds on multiple source
shapes, preserves forced-GC behavior, and reduces executed materializations or
copies. A benchmark name or literal workload dimension must never select it.

## 10. Implementation map and boundaries

| Existing owner | Proposed changes / reuse |
|---|---|
| [ast-core.hpp](../lambda/runtime/ast-core.hpp) | Reuse per-entry analysis schemas and return authority; compilation-owned effect/path solutions avoid races on shared ASTs |
| [compiler_pass.cpp](../lambda/runtime/compiler_pass.cpp), [compiler_timing.hpp](../lambda/runtime/compiler_timing.hpp) | Declare effect/proof dependencies and record costs in the existing schedule |
| [build_ast.cpp](../lambda/runtime/build_ast.cpp) | Reuse binding/use and ownership analyses; tier-shared decisions where required |
| [transpile-mir.cpp](../lambda/runtime/transpile-mir.cpp) | Shared fact consumers, loop/inline planning, precise invalidation; retire emission-order effect authority |
| [mir_emitter_shared.hpp](../lambda/runtime/mir_emitter_shared.hpp) | Call-effect consumption, emitted-graph verification integration, canonical rooting/reload/finalization ownership |
| [lambda-stack.cpp](../lambda/runtime/lambda-stack.cpp) | Only a measured stack-probe change if justified; retain recovery contract |
| Existing COW/runtime helpers | Complete diagnostic coverage; helper changes only for a demonstrated residual hot cost |
| [run_paired_benchmarks.py](../test/benchmark/run_paired_benchmarks.py) | Extend reproducibility/family reporting by reusing the current paired harness |

Extract shared operation/effect helpers before adding a third similar lowering
case. Promote an existing file-local helper when reuse requires it. Use C++17
and `lib` containers/allocators, keep scratch outputs under `./temp`, and route
diagnostics through the logging API. Shared emitter changes also require
relevant LambdaJS coverage, since root and representation behavior is shared.

Do not edit MIR/vendor sources, resurrect C-text transpilation, introduce
conservative native-stack scanning, or add dynamic inline caches (**D8.4.1v2**).
No source rewrite is included in engine-performance figures. A separate
all-`u32` SHA1 experiment may quantify a source-representation ceiling, but it
must retain its own label and samples.

## 11. Regression tests that pin the mechanisms

Use existing [MIR emission](../test/test_mir_emission_gtest.cpp),
[MIR ratchet](../test/test_mir_ratchet_gtest.cpp),
[optimization](../test/test_lambda_opt_gtest.cpp), and
[forced-GC](../test/test_mir_gc_stress_gtest.cpp) GTests. Each new `.ls` fixture
has a matching `.txt`; structural fixtures also have `.mir-check` assertions.

| Area | Positive case | Required counterexample / stress case |
|---|---|---|
| Effects | Forward/backward calls, self/mutual recursion, >64 bodies | One allocating edge, indirect/import unknown, raw entry versus allocating boxed wrapper |
| Analysis ownership | Concurrent satellite compiles agree with eager compilation | Independent entry plans must not overwrite another compile's effects or return-lane decisions |
| Effect verification | Planned/emitted summaries agree | Inject a contradictory claim at the test seam; compilation must reject it before publication |
| Integer range | Bounded recurrence and safe affine index | Domain limits, poison, null, intermediate overflow, division/shift exceptional inputs |
| Bounds | Dominating length proof, nested induction | Empty/short/different-length arrays, negative index, alias resize, root replacement |
| Representations | Native values through branch/call/result | Nullable joins, Item escape, companion/error return, NaN/signed zero |
| Inlining | Small helper with once-only argument evaluation | Observable arguments, `var` alias/write-back, partial-write error exit |
| Storage | Read-only call preserves logical facts | Forced compaction invalidates data pointers; `NO_GC` mutator resizes storage |
| Ownership | Dead snapshot or admitted handle/store-back | Live old snapshot, sibling/prefix alias, retaining call, nested handle, branch exit |
| Aggregate | Local fixed aggregate and destination consumption | Escape/capture/unknown consumer, mixed scalar/reference fields, GC during materialization |
| Frames | Proven scalar/GC-free helper drops unused protocol | Allocation only on cold branch, recursion overflow, watermark cleanup ordering |

Prefer assertions about hot-region operations and safepoints over exact
register numbers or whole-function instruction counts. A valid checked
fallback may contain the very helper that the hot region must avoid. Pair each
positive structural assertion with a negative test proving the guard/fallback
still exists when the proof is unavailable. Use counter-based GTests for
executed work where static MIR cannot distinguish the paths; never use noisy
wall-clock thresholds as unit-test assertions.

Run the existing GC effect and native-root audits:
[check_gc_effects.py](../utils/check_gc_effects.py) and
[check_gc_root_hazards.py](../utils/check_gc_root_hazards.py).
Extend coverage without weakening their conservative defaults. Run forced-GC
and tier parity for affected mechanisms, then `make test-lambda-baseline` for
engine changes. Shared-emitter changes also run the affected JS tests and
required JS baseline; failures must be fixed rather than filtered away.

## 12. Measurement and release gates

1. **Freeze inputs.** Archive baseline/candidate release binaries and hashes;
   record commit, compiler/build flags, MIR revision, CPU/OS, environment,
   benchmark parameters, all Lambda transitive imports, all C sources/includes,
   and the C timer/driver. Record dirty patches if present.
2. **Match timed regions.** Separate parse/build/JIT/link/startup from execution.
   Check that C and Lambda perform equal algorithmic work and validation.
   Force JIT for the typed steady-state comparison; measure default tiering and
   end-to-end latency separately. Keep MIR cache policy identical.
3. **Establish noise.** Run A/A before A/B. For very short rows, add a matched
   repeated-work harness long enough to measure reliably; validate state reset
   and output, and retain the original row results separately.
4. **Screen each phase.** Use alternating, order-balanced paired release runs,
   identical warmup, and output validation. Nine pairs is an initial screen,
   not proof for a noisy row. Diagnostics/counters are disabled for timing.
5. **Confirm independently.** Predeclare a larger paired campaign after the
   screen, retain all samples, and use confidence intervals on paired time
   ratios. Do not stop when significance first appears or replace losing
   screen samples with favorable reruns. Record reruns separately.
6. **Report breadth.** Publish all 63 rows, family geometric means, overall
   geometric mean, sum of medians, regression controls, compilation metrics,
   code size, and uncertainty. For ratios `r_i = candidate_i / baseline_i`,
   the overall time ratio is `exp(mean(log(r_i)))`; every row has equal weight.
7. **Refresh C2MIR.** Run the pinned standalone C driver in the same campaign.
   Report both the engine A/B and Lambda/C ratios, with source-semantic
   differences. A historical C median is not a contemporaneous control.

For each retained change, record the chain: **proof gained → executed protocol
removed → measured workload effect**. If static code shrinks but counters or
timing do not improve, investigate cold-code removal, backend elimination,
spills, or a different dominant cost before expanding the optimization.

## 13. Delivery sequence and completion checklist

Deliver reviewable slices in dependency order:

1. P0 census/manifest changes and the frozen baseline/families.
2. P1 conservative graph infrastructure, order/SCC tests, pre-lowering summaries,
   and emitted verification; then precise operation coverage justified by pilots.
3. P2 shared scalar facts and representation consumers, followed by relational
   array proofs. Each consumer lands with positive and fallback GTests.
4. P3 bounded helper/region extensions using those facts, with code-size budgets.
5. P4 path effects and invalidation, first read-only calls, then disjoint writes,
   then additional admitted ownership shapes.
6. P5 aggregate/ownership work selected by the remaining executed census.
7. Fresh full-corpus release confirmation and a durable implementation report.

Each slice updates this plan with actual scope, checks, evidence, and remaining
limits. Establish provisional diagnostic toggles only where useful for A/B;
avoid accumulating permanent alternative lowering modes. A feature is not
complete merely because its admission path exists or its MIR test passes.

- [ ] Baseline and C reference are reproducible from complete manifests.
- [ ] Effect precision is independent of declaration order and the old 64-body cap.
- [ ] Numeric/access proofs have explicit lifetimes and conservative fallbacks.
- [ ] Rooting and return ABI remain owned by their existing authorities.
- [ ] Storage invalidation distinguishes GC relocation from logical mutation.
- [ ] Snapshot/ownership and allocating cold paths pass stress tests.
- [ ] Important changes are pinned by MIR/opt GTests, with negative cases.
- [ ] Release measurements demonstrate the removed executed work.
- [ ] Breadth, regression, compilation, and code-size gates are reported honestly.
- [ ] Remaining gaps are explained by evidence, with no unmeasured speedup claim.

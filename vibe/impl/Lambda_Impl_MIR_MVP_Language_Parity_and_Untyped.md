# Lambda MIR MVP: language parity and untyped execution

**Date:** 2026-11-05

**Status:** implementation plan; implementation has not started.

**Starting tree:** `71296a16a9622f764229720ed2c321330efcd278`, branch `master`.

Extend the existing compiler in `lambda/runtime/mvp/` to execute the full
Lambda language surface, pass the Lambda GTests, and beat Full MIR on every
registered untyped benchmark. Preserve the existing typed benchmark performance.
Features that Full MIR already routes to the AST interpreter may use that same
route in MVP. This does not require a second implementation of those features.

This continues the [MVP design](../Lambda_Design_MIR_MVP.md) and
[typed implementation record](Lambda_Impl_MIR_MVP.md). The runtime value model,
object layouts, type system, scheduler and precise GC remain shared.

## Current checkpoint

- **Updated:** 2026-11-05. Planning only; no engine changes or new test results.
- **Goal:** complete language coverage and zero failing Lambda GTests with the
  applicable JIT invocations using MVP; faster execution than Full MIR on all
  canonical untyped benchmark entries; no typed performance regression.
- **Repository:** main checkout, branch `master`, starting commit above. The
  preceding tuning work was committed as `bdded83e2` and merged into this tree.
- **Settled:** preserve Full MIR's existing interpreter delegation, shared
  semantic helpers and runtime ABIs. Keep MVP's own planning and MIR lowering.
  An MVP rejection must not silently select the production compiler.
- **Settled:** untyped inference chooses representations and guarded variants;
  it does not introduce annotations or stricter runtime contracts
  (**D4.3.1–D4.3.3, D7.3.1–D7.3.4**). No feedback caches (**D7.4.1**).
- **Inventory inspected:** MVP admission/lowering, runner selection, dynamic
  dispatch, interpreter interfaces, GTest discovery, benchmark registration and
  the phase3 reports. All 77 registered entries have a canonical untyped `.ls`.
- **Historical evidence:** typed phase3 has 77 faster medians, 76 clear wins
  after completed confirmations, CD inconclusive, two starting-MVP regression
  flags and four unfinished confirmations. Its 8,770 baseline checks do not
  establish MVP language parity. No current untyped performance claim exists.
- **First action:** P0: freeze the actual test/benchmark populations, classify
  every missing feature and existing failure, audit Full's delegation routes,
  and add a compiler-aware GTest lane plus untyped benchmark selection.
- **Open measurements:** current forced-MVP GTest failures, the number of
  failures in each semantic category, and untyped coverage/timings. Obtain them
  before assigning tuning priorities; do not infer them from typed results.
- **Validation scope:** Lambda gates only. This documentation change needs
  document checks, not a build, benchmark campaign or Radiant baseline.

## 1. Acceptance contract

The expansion has three independent completion gates. A performance aggregate
or an interpreter-only test run cannot substitute for any gate.

| Gate | Required result |
|---|---|
| Language and GTests | Every currently implemented public Lambda feature has a recorded execution route and semantic coverage. The complete applicable Lambda GTest population passes with MVP selected for JIT execution. Positive, negative and error-payload cases all retain their existing oracles. |
| Untyped benchmarks | Every registered canonical untyped entry runs correctly in MVP. For every valid Full comparison, the one-sided 95% paired-bootstrap upper bound of MVP/Full execution time is strictly below `1.0`. Slower, tied, invalid and inconclusive rows remain open. |
| Existing typed benchmarks | All typed outputs and ownership checks pass. No new statistically supported regression against a freshly captured starting MVP; investigate slower rows and retain unresolved results. Close the existing CD/Base64/DeltaBlue qualification debt before claiming complete performance closure. |

P0 freezes the populations, including discovered tests and excluded slow
benchmarks. The current benchmark registry contains 77 entries across eight
suites. If the registry changes, append the new entries explicitly and record
the population change. Do not quietly remove difficult rows.

"All language features" includes modules, packages, objects, document values,
views, resources and concurrency, as well as scalar expressions. Full MIR
parity is the minimum coverage reference, not the semantic oracle. A feature
that both engines implement incorrectly remains a defect under the formal
specification. Features explicitly marked unimplemented in the specifications
must remain visible in the inventory; do not label them supported merely
because Full MIR also lacks them. This plan does not invent syntax or settle
open semantic rulings.

The default AUTO policy and persistent T0 REPL remain as specified by
**D8.3.1–D8.3.6**. Switching the default compiler, replacing Full MIR, and adding
MVP background promotion are separate rollout work, not prerequisites for an
explicit MVP JIT lane.

## 2. Authority and implementation constraints

| Concern | Governing rulings and consequence |
|---|---|
| Representation | **D2.3.1–D2.3.5**: separate semantic type, value representation, ABI and register class; preserve `Item` and the canonical conversion router. |
| Untyped inference | **D4.3.1–D4.3.4**: boxed execution is the semantic reference; body inference does not create a source contract; inferred array narrowing belongs to its binding. |
| Calls | **D7.2.1–D7.2.4, D7.3.1–D7.3.5, D7.4.1–D7.4.3**: definition-site identity, boxed closures, complete guarded variant keys, explicit context/operands and completion transport. |
| Analysis | **D8.2.4–D8.2.6**: use the shared AST index and pass manager; semantic facts stay on AST analysis; demand lowering must preserve behavior. |
| Ownership | **D5.4.1–D5.4.6, D6.2, D6.3**: preserve COW snapshots, caller-owned `var` homes, scalar homes, precise root slots and recovery boundaries. |
| Collections and control | **S2.5, S2.6, S12.1.1v3–S12.1.4v4, D3.3.1**: retain functional collection, procedural last-result behavior, effect color and destination-specific append. |
| Errors and contracts | **S7.4–S7.11, S11.4.1v5, S12.3**: retain Error identity, evaluation/short-circuit order, rich admission failures and call validation. |
| Objects and queries | **S11.2–S11.3, S14.1–S14.3**: matching, structural versus nominal membership, joins, ordering and windows retain their declared semantics. |
| Tasks and resources | **S12.4, S13, D7.5, D6.3.6**: reuse the scheduler, activation and resource ownership rules across native/interpreted calls and suspension. |
| Verification | **D8.6.1–D8.6.3**: retain emission ratchets, release MIR dumps and forced-GC/poison oracles. |

No formal ruling change is proposed. If implementation exposes an unresolved
semantic question, record the exact ruling gap and ask before dependent work.

Use existing runtime kernels and shared physical emitter primitives. Before
adding a helper, find its current owner. Promote or parameterize a shared
implementation rather than copy production AST lowering or build a parallel
semantic library. Any helper change must identify its GC, reentry, argument
ownership, result-home and completion contracts.

Retain the removed C-text backend's removal; C2MIR is not an acceptance target
for this expansion. Do not edit vendor code, restore native-stack GC scanning,
change canonical benchmark workloads, or add annotations to make an untyped
benchmark easier to compile.

## 3. Starting gaps found in the source

These observations identify work; they are not a completed coverage census.

| Current boundary | Evidence and required expansion |
|---|---|
| Compiler selection | `runner.cpp::load_script` requires explicit JIT for MVP and rejects MVP with AUTO/interp. The GTests deliberately invoke all three tiers. A global environment override is therefore insufficient. |
| Function planning | `mvp_plan.cpp::lambda_mvp_plan` rejects captured, variadic, dependent and suspending definitions, and most nested named definitions. Rejection diagnostics are not always precise descriptions of the predicates. |
| Calls | `admitted_call` rejects named/spread/pipe arguments and indirect calls. Existing support includes direct fixed-arity calls, some defaults, public imports and noncapturing function values; extend it rather than replace it. |
| Content and control | Planning rejects multi-value functional content and non-while loop forms. Query support already includes grouping/ordering, but multiple sources, optional/keyed forms, `let`, limits and offsets remain restricted. |
| Patterns and recovery | Match admission is limited to base-type cases; filtered handlers and further contract forms remain outside the admitted subset. |
| Remaining syntax | The admission switch rejects node families outside its list. Inventory elements/lists, decomposition, objects, navigation, document operations, views and tasks by actual AST kinds and tests. |
| Builtins | Many fixed pure registry calls work; variadic, mutating and suspending cases need their real ABI/place handling. Simply deleting the builtin allowlist is unsafe. |
| Untyped measurement | `lambda_mir_mvp.py` selects `<name>2.ls` in its execution loop. All 77 `<name>.ls` files exist, but coverage, output validity and performance have not been qualified for MVP. |

The previous broader MVP gate reported 62 failure headers, including emission
assertions, semantic/GC failures and four Full-only negative-probe failures;
two historical reference failures concern C benchmark controls. These are not
62 independent MVP language features. P0 must reproduce and classify the
failures before choosing fixes. No known semantic failure is grandfathered into
the language-parity exit gate.

## 4. Execution model and delegation

### 4.1 Three visible routes

Record one route for each definition/feature using the shared planning facts:

1. **MVP native MIR:** specialized scalar/container/call lowering with proofs.
2. **MVP boxed MIR:** complete generic lowering through shared semantic kernels.
   This is the correctness fallback when specialization cannot prove its shape.
3. **Existing interpreter delegation:** only where the audited Full execution
   path already delegates that feature. Preserve its boundary and runtime ABI.

Routes 1 and 2 belong to MVP. Route 3 counts as supported language behavior,
but must be reported as delegated. A runtime builtin call is not automatically
interpreter delegation. MIR's bytecode interpreter, used by code-generation
policy/diagnostics, is also distinct from the Lambda AST interpreter.

P0 must build the actual delegation allowlist from current source and execution
traces. Do not assume closures, async procedures, views or object methods all
delegate: several have real generated lowering in Full MIR. The shared dynamic
dispatcher already supports interpreter-owned `Function` values through
`lambda_dynamic_invoke_by_count`, `interp_call` and `interp_call_borrowed`.
Reuse those routes where applicable, including the promotion policy already
owned by the runtime. Do not invent a parallel dispatcher.

Trace subsequent promotion as well as the initial call. A delegated function
must not quietly promote through Full MIR under an MVP label. A published
image must honor the selected compiler; a body MVP cannot promote retains its
permitted T0 route under **D8.3.3**. Any remaining production-compiled execution
is reported explicitly and cannot qualify an MVP benchmark win.

For a feature Full MIR compiles, a temporary missing MVP lowering remains an
open item. An unrestricted catch-all interpreter or production-compiler
fallback would conceal the gap and does not satisfy this plan.

### 4.2 Boundary invariants

- Evaluate operands/arguments once, in source order. A guard miss consumes the
  already evaluated values; it must not repeat side effects or initializers.
- Keep module identity, imported bindings, constant pools and retained AST
  lifetimes valid through callbacks and any delegated execution.
- Use caller-owned homes for `var` transport and explicit scalar-return homes;
  never let a borrowed wide scalar outlive its owner.
- Treat unaudited helpers and interpreted/dynamic calls as collecting and
  reentering. Reload movable data buffers and invalidate affected shape/place
  facts after such boundaries.
- Preserve closure snapshot/live-borrow permissions from the shared analysis.
  A closure stays boxed; an escaping environment is traced precisely.
- Use the existing activation/scheduler for suspension. Do not reconstruct a
  suspended body by replay, create a second coroutine engine, or jump between
  native stacks to transport an ordinary Error.
- Preserve pure/procedural checks, resource cleanup and error payloads across
  native-to-interpreted and interpreted-to-native calls.

## 5. Implementation phases

Dependencies: **P0 → P1 → P2 → P3 → P6** for language closure, and
**P0 → P1 → P2 → P4 → P5 → P6** for untyped speed. Start small untyped
measurements after P1/P2; finish remaining language families without waiting for
every benchmark to be tuned. Each phase has a finite exit list.

### P0 — Reproducible coverage and measurement

**Changes**

1. Freeze the real GTest discovery list, negative-test rules, platform goldens,
   slow-benchmark exclusions and per-suite execution modes. List registered
   Lambda semantic tests separately from Full-specific emission tests and
   independent C reference checks.
2. Add compiler-aware execution to the shared test harness. Ordinary semantic
   scripts get a forced-JIT MVP lane. Tests that explicitly request `interp`
   or `auto` retain those modes without the incompatible MVP override; their
   JIT counterpart uses MVP. Preserve existing tier-parity assertions and
   specialized environment settings. In-process compiler tests must also
   exercise the selected backend, not only CLI subprocesses.
3. Register an aggregate MVP semantic gate, proposed as
   `make test-lambda-mvp`, through `build_lambda_config.json` and the existing
   runner. Do not maintain a second hand-picked list of passing scripts.
   Include MVP semantic fixtures in real test discovery where currently only
   the focused Python suite reaches them.
4. Extend `lambda_mir_mvp.py` with a proposed
   `--variant typed|untyped|both` selector. Key cases by suite/name/variant and
   use the chosen source's own `.formatted.txt`/`.txt` oracle. Preserve the
   current typed default and source-manifest verification.
5. Freeze all 77 untyped entries and their transitive imports/data. Record
   whether an untyped entry imports annotated helpers; canonical mixed entries
   remain in the population, but are not advertised as fully annotation-free.
6. Capture current Full, current MVP and starting-MVP controls with matching
   source/build identity. Measure validity first, then timings for supported
   rows. An MVP rejection is an uncovered row, not a zero-duration result.
7. Produce a feature matrix: AST kind/contract, Full route and symbol, MVP
   route or rejection, positive/negative/GC fixture and implementation phase.
   Produce a failure census with reproducible commands and root-cause groups.

**Exit:** every discovered test and registered benchmark is accounted for;
compiler identity proves the intended lane ran; Full delegation is an explicit
source-backed list; untyped failures and timings are measured rather than
inferred. Validate runner/oracle selection with both a scalar entry and an
import-heavy text entry before the broad sweep.

### P1 — Complete boxed values, expressions and control

**Changes**

- Give every current scalar domain a correct boxed lowering: sized integers,
  nonfinite numbers, decimal, datetime, symbols, binary, strings, type/pattern
  values, null and Error. Reuse numeric and scalar-home primitives.
- Complete unary/binary operations, comparisons, membership, coercions,
  vectorized operations and truth handling through the shared kernels.
- Implement functional multi-value collection and procedural last-result
  behavior, lists versus arrays, elements/attributes and spread, ranges,
  decomposition, loop forms and callable-local control transfers.
- Complete recovery/filter handling and contract outcomes without losing Error
  identity. Audit declared binding/assignment behavior against current
  **S11.4.1v5**, rather than retaining older block-skipping assumptions.
- Make generic Item lowering the complete path for unknown types. A failed
  optimization proof selects it, rather than rejecting a valid program.

**Exit:** each family has positive, negative, nullable/error and mixed-value
cases in the real harness. Forced and seeded poisoned-GC runs match ordinary
execution. No semantic failure in these families remains waived as legacy.

### P2 — Calls, closures, objects and modules

**Changes**

- Complete static named/default/rest/spread/pipe argument normalization and
  evaluation order using the shared call analysis. Preserve specified errors
  for dynamic named calls and dynamic `var` signatures; accepting invalid
  syntax is not feature parity (**D7.2.2**).
- Add general function-value calls through the canonical dispatcher, retaining
  arity, effect color, return companion, resource and native callback rules.
- Implement nested definitions, captured closures and proven confined nested
  procedures from the shared capture plan. Cover recursion and mutual
  recursion with precise environment/scalar ownership.
- Complete nominal object construction, fields, inheritance, bound methods,
  receiver admission and method calls through existing object mechanisms.
- Complete public/private imports, imported values/types/functions, module
  initialization and side effects, package entry calls, module state and
  callback lifetime. Initialization executes once under both compilers.
- Keep public/dynamic boundaries boxed as required; preserve direct native
  edges where proofs exist. Do not route an imported hot helper through Full
  MIR merely because MVP is missing its body lowering.

**Exit:** nested/imported callbacks, default side effects, late callbacks after
GC, closure snapshots, legal live borrows and invalid borrow/effect cases pass.
An import-heavy untyped benchmark executes with audited MVP/delegated routes
through its entire import closure.

### P3 — Remaining language surface and GTest closure

**Changes**

- Complete multi-source/optional/keyed queries, joins, `let`, where/group/order,
  limits/offsets, windows and sequence construction. Evaluate effect/error
  producers even when the result is discarded; use the correct append mode.
- Complete structural/nominal match and type values, recursive/constrained/
  dependent/binary contracts, pattern operations and runtime type construction
  through the existing type/validator machinery.
- Complete path/parent/root/occurrence navigation, document construction and
  edits, views and handlers, resources and the remaining system-function ABIs.
- Integrate task creation, waits, messaging, cancellation, suspension and faults
  through the existing runtime. Retain audited Full interpreter delegation
  where it exists. A feature Full compiles needs MVP lowering or a shared
  semantic producer; its missing implementation is not an allowed delegation.
- Close the remaining P0 matrix rows and MVP semantic failures. Test negative
  inputs for proper diagnostics instead of disabling checks to obtain parity.

**Exit:** the entire applicable `test_lambda_gtest.exe` run passes in its MVP
compiler lane, as do procedural/runtime/concurrency semantic companions and
all MVP behavior/GC fixtures. The normal Lambda baseline passes separately.
Record expected negative tests as passes only through the actual harness's
oracle. No added skip, parked failing fixture or broad interpreter fallback
can satisfy the exit.

### P4 — Untyped scalar and call specialization

**Changes**

- Reuse shared AST inference, use/def indexes, call graph and function analysis.
  Converge recursive components deterministically. Distinguish declared
  contracts, effective body facts, entry-shape predictions and emitted carriers.
- Plan bounded raw variants using complete exact semantic-shape keys
  (**D7.3.1**). A private closed function with one proven shape can use its raw
  entry directly; open callers enter `_b`, whose guard selects a variant or the
  complete boxed body. Union/`any` canonical operands remain Items; do not turn
  them into source-level admission tests. Closures/variadics stay boxed where
  required by **D7.3.4**.
- Carry proven numeric/bool/string result domains through locals, branches,
  loop joins, direct calls and registry result relations. Handle mixed numeric,
  nullable, Error, nonfinite and overflow cases on their real semantic paths.
- Expand range proofs, native arithmetic/branches, bounded inlining and safe
  self-tail handling using the existing analyses. Failed proofs remain boxed.
- Add untyped adversarial tests: a second caller with another type, numeric
  widening after an assignment, recursive widening, a late Error and an open
  imported call. Exact-guard misses must preserve boxed behavior and effects.

**First benchmark batch:** `r7rs/{fib,sum,sumfp,tak,ack,fibfp,cpstak}`,
`kostya/collatz`, `julia/iteration_pi_sum` and scalar-heavy Mandelbrot cases.

**Exit:** every scalar batch entry runs unchanged, produces its own golden and
beats Full in a release screen. MIR/profile evidence explains remaining boxes
and call boundaries. The complete semantic gate remains green.

### P5 — Untyped collections, shapes, strings and query kernels

**Changes**

- Retain binding-local inferred array element/rank facts and exact physical
  guards. Reuse typed access primitives without writing inferred contracts or
  certificates onto containers. Shape changes, view creation, unknown writes,
  reentry and COW replacement invalidate the affected witnesses.
- Optimize untyped array access/update/iteration, native numeric buffers,
  construction, indexing and length. Hoist only guards whose dataflow lifetime
  is proven; reload movable buffers after safepoints.
- Use static literal/receiver shape facts and guarded field access for untyped
  maps/objects. Open shape misses use shared property kernels. Do not add
  property/callee caches or runtime type feedback.
- Extend ownership-based snapshot elision, RMW handles, borrowed parameters
  and call inlining to untyped values under the same shared COW analysis.
- Reduce string construction, indexing, split/search/replace, query output,
  sorting/grouping and imported-helper overhead with general source-shape or
  demand proofs. Preserve Unicode, option, Error and sequence behavior.
- Profile all unresolved benchmark families together and batch fixes by root
  cause. Audited runtime helper changes are allowed; benchmark-specific
  recognition or replacement of a benchmark algorithm by C is not.

**Benchmark batches**

| Batch | Initial cases | Main question |
|---|---|---|
| Numeric arrays | array1, sieve, spectralnorm, all nbody ports, matmul, FFT, quicksort | Where do inferred lanes get erased or repeatedly guarded? |
| Recursive/allocation | nqueens, queens, permute, paraffins, binarytrees, gcbench, triangl | Which copies, result containers, roots and calls are required? |
| Open records/calls | Richards/DeltaBlue ports, Havlak, CD, JSON, Splay, HashMap, raytrace3d | Which shapes/callees are statically knowable, and where are safe boxed misses needed? |
| Text/IO/query | Base64, regexredux, revcomp, knucleotide, Julia formatting/parsing and all text entries | Are time and allocation spent in kernels, construction, imports, or generic dispatch? |

These are tuning batches, not a reduced final benchmark population. Every
registered entry, including cases not named in this table, remains mandatory.

**Exit:** all 77 untyped entries are correct and measured; every remaining Full
gap has a profile/MIR explanation and a bounded fix or remains explicitly open.
All 77 typed entries remain in regression checks after shared hot-path changes.

### P6 — Final qualification and handoff

1. Freeze the candidate sources, transitive test/benchmark inputs and matching
   release/debug manifests. Complete semantic/GC gates before quiet timing.
2. Run the whole compiler-aware Lambda GTest gate and normal Lambda baseline.
   Native helper/GC changes require their affected runtime unit gates as well.
   Reused unchanged test results must have explicit source/binary provenance;
   they cannot stand in for the required MVP GTest run.
3. Run the complete typed and untyped paired release campaigns described below.
   Complete prespecified confirmations; retain primary and repeat results.
4. Verify every benchmark used the requested compiler throughout relevant
   modules and hot bodies. Interpreter delegation must match the audited
   feature routes; generated/generic/kernel/delegated counts stay visible.
5. Publish coverage, failure census, test commands/counts, raw timing samples,
   per-row confidence bounds, unresolved rows, build/input hashes and the final
   regression analysis. Update this checkpoint and the parent design record.

**Exit:** all three §1 gates pass. If any required test, timing or confirmation
is unfinished, or a Full control is invalid, retain partial status and identify
the exact remaining work. Do not replace per-row qualification with a favorable
geometric mean.

## 6. Files and ownership

| Area | Existing owner / planned changes |
|---|---|
| Admission and function plans | `lambda/runtime/mvp/mvp_plan.cpp`, `mvp_internal.hpp`: replace subset exclusions with complete semantic plans; consume shared inference/effect/capture facts. |
| MIR lowering | `lambda/runtime/mvp/mvp_mir.cpp`: boxed completeness, expanded syntax and guarded untyped/native consumers. Split coherent implementation units if needed; do not clone switches/helpers. |
| Shared analysis | `lambda/runtime/ast-core.*`, `build_ast.cpp`, existing type/call/COW analyses: add reusable facts only at their proper shared owner. |
| Physical emission | `mir_emitter_shared.hpp`, `mir_container_access.hpp`, `mir_path.hpp`, `mir_function_value.hpp`: shared transport, guards, roots, places and construction. |
| Runtime boundaries | `lambda-eval.cpp`, `sys_func_registry.*`, `interp.hpp`/`interp.cpp`, `activation.*`, existing document/type helpers: reuse or narrowly extend canonical behavior with audited effects. |
| Module/compiler integration | `runner.cpp`, existing MIR pass callbacks and module layout finalization: retain selector identity, ownership and import initialization. |
| Semantic harness | `test/test_lambda_helpers.hpp`, `test/test_lambda_tier_helpers.hpp`, `test/test_lambda_gtest.cpp`, related procedural/runtime/concurrency tests and `build_lambda_config.json`. |
| Benchmarks | `test/benchmark/lambda_mir_mvp.py`, `test_lambda_mir_mvp.py`, provenance/paired-bootstrap helpers: variants, transitive manifests and truthful coverage/performance status. |
| New fixtures/results | `test/lambda/mvp/` with paired `.ls`/`.txt`; versioned reports in `test/benchmark/lambda_mvp/`; temporary work only in `temp/lambda_mir_mvp/language_untyped/`. |

New C/C++ code follows the C+ convention and the `lib` collection types. No
generated build-file or parser edits are needed; registration changes go through
their source configuration.

## 7. Correctness gates and finite verification budget

### 7.1 Required test lanes

| Lane | Purpose |
|---|---|
| MVP semantic GTests | Entire discovered `test_lambda_gtest.exe` population with ordinary scripts forced to MVP JIT, plus MVP on each explicit JIT counterpart. Preserve actual T0/AUTO behavior in the other tier arms. |
| Companion Lambda semantics | Procedural, runtime errors, optimizer behavior, concurrency, std/extended suites and affected package/native callback fixtures. Record which invoke generated code and which validate shared runtime behavior. |
| Delegation/cross-boundary tests | MVP → interpreted callee → MVP callback; imported state once; wide scalar returns; `var` write-back; capture snapshots; cancellation/cleanup and GC during callbacks. |
| Ownership stress | Expanded semantic fixtures in normal, force-every poisoned and seeded poisoned modes; suspension-specific parked roots and result-home ownership where relevant. |
| Normal baseline | `make test-lambda-baseline`, preserving ordinary engine behavior and all registered aggregate gates. No Radiant baseline is requested for this Lambda task. |
| Structural checks | Existing Full emission tests retain their own expectations; MVP gets meaningful structural assertions for native hot paths, boxed fallback and rooting. Do not force both emitters to generate identical MIR. |

P0 proposes a central `make test-lambda-mvp` command; that target does not exist
yet. Until it is implemented, a focused existing command is:

```bash
LAMBDA_EXEC_BACKEND=jit LAMBDA_MIR_COMPILER=mvp \
  ./test/test_lambda_gtest.exe --gtest_filter='AutoDiscovered/*'
```

This focused command is not the full acceptance gate: explicit tier tests,
in-process compilation, companion binaries and current discovery exclusions
still need the compiler-aware integration above.

Use focused semantic/GC checks while implementing a family, one family sweep at
its boundary, and an aggregate gate at milestone boundaries. Repeat a broad run
only after relevant changes, failures or unresolved concerns. Keep structural
expectation changes separate from semantic fixes and justify each by emitted
behavior. Do not weaken goldens, skip failures or silently enlarge emission
budgets (**D8.6.1**).

### 7.2 Failure accounting

Record compile rejection, wrong output, lost Error payload, crash, timeout,
resource failure, GC mismatch, stale structural assertion and invalid control
separately. A baseline pass in AUTO is not evidence that MVP compiled a script.
A shared-runtime defect may require a shared fix, but Full's existing behavior
cannot override the formal oracle. Preserve all initial failures and reruns.

Host/environment failures need an identified cause and a separate rerun record;
they are not removed from the report. Existing failures remain visible until
fixed or explicitly outside a gate's documented population.

## 8. Performance protocol

### 8.1 Workloads and controls

- Compare MVP and Full on the **same canonical untyped source**, imports, data,
  output oracle and workload. Typed-versus-untyped ports are separate rows,
  not substitutes for a compiler comparison.
- Use a matching release build, preferably both compiler selectors in the same
  private executable. Preserve a fresh starting MVP for regression comparison.
  The historical phase3 binary/report is context, not current-tree evidence.
  Record MIR optimization/generation policy and verify native generation in
  both timed lanes; any policy-selected MIR bytecode interpretation is a
  separate execution mode, not AST delegation or a native JIT comparison.
- Freeze transitive imports and retain existing annotations found in canonical
  untyped modules. Report mixed modules. Do not erase contracts, rewrite a
  port or inject annotations to manufacture the desired result.
- Validate full stdout/results, exit status and required negative outcomes
  before accepting a timing. A wrong Full result invalidates the control;
  correct it and freeze new comparable sources before counting a win.

### 8.2 Timing and acceptance

The requested speed target is **self-reported execution time**, excluding
startup and JIT compilation, consistent with the previous comparison. Also
record process wall time and available compile/pass times as separate metrics
so an execution win does not conceal excessive compilation. Report each
benchmark's setup/output boundary; do not change it between compilers.

1. During implementation, use fixed short screens on the affected batch;
   profiling and MIR inspection are diagnostic, never performance evidence.
2. Before final sampling, prescribe 30 measured paired rounds and two warmups
   for all 77 untyped entries and the 77 typed regression entries. Randomize
   engine order deterministically and retain raw observations/orders.
3. Preselect 50-pair confirmations for close/noisy rows from independent screens
   and historical risks, including CD. Publish that selection before final
   primary samples. Retain primary and confirmation results, including an
   inconclusive repeat after a passing primary.
4. A clear win requires the one-sided 95% paired-bootstrap MVP/Full upper bound
   below `1.0` for each required row. Aim for a practical margin in tuning;
   statistical uncertainty is not a license to classify parity as a win.
5. For starting-MVP regressions, retain the two-sided 95% ratio interval and
   flag a lower bound above `1.0`. Investigate slower medians, preserve earlier
   flags and define any independent follow-up before it samples. No repeated
   sampling until a flag disappears. Unresolved regression evidence stays open.
6. A zero/timer-limited observation requires a matched, consumed-result batch
   supplement for both compilers, with identical kernel calls and a recorded
   source transform. Retain the original observations and report supplements
   separately; never treat zero as proof of zero execution cost.
7. Run timing without concurrent builds/baselines/profilers. Pause for competing
   jobs and discard the entire disturbed round. Use private binaries and source
   snapshots; never replace the shared executable during another task's tests.

Register case-specific timeouts from P0's legitimate workload measurements.
Do not reinterpret a timeout as a fast result or remove the row. If a final
candidate changes after qualification, rerun the affected gates and mark old
artifacts historical; a broad generic lowering change requires the full
performance population again.

The final report includes all rows, per-suite distributions, median/geometric
mean speedups, confidence bounds, compiler/delegation routes, invalid controls,
typed regressions and incomplete runs. Aggregate gains cannot offset one
benchmark that is slower than Full MIR.

## 9. Delivery sequence and first implementation slice

| Delivery | Required reviewable result |
|---|---|
| P0 | Population/delegation manifests, classified failure report, verified MVP GTest lane and typed/untyped runner with correct oracles. |
| P1 | Complete boxed expression/content/control paths and passing family fixtures. |
| P2 | General calls, nested/captured functions, object/module boundaries and import-heavy execution. |
| P3 | Full language matrix closure and passing MVP Lambda semantic gates. |
| P4 | Source-backed scalar/call specialization, guard-miss tests and measured untyped scalar gains. |
| P5 | Collection/shape/string/query gains across every remaining benchmark family, with typed regression evidence. |
| P6 | Frozen, complete correctness and performance qualification; published raw reports and updated checkpoint. |

Start with **P0 plus the first P1 vertical slice**: run the real forced-MVP
corpus, classify the largest rejection families, add correct boxed fallback for
those families, and demonstrate unchanged untyped scalar/import-heavy entries.
Do not begin another long full benchmark campaign before this establishes that
the harness is measuring the intended compiler and workloads.

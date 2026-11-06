# Lambda MIR MVP: language parity and untyped execution

**Date:** 2026-11-05

**Status:** goal paused at the user's wrap-up request; language parity and performance gates remain incomplete.

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

- **Updated:** 2026-11-06. Goal paused at the user's wrap-up request. Full
  implemented language/GTest parity, faster untyped benchmarks and typed
  performance preservation remain open.
- **Scope:** only Full's existing interpreter delegations (D8.3.1–D8.3.3).
  Shared value layouts, scheduler and precise GC. Lambda baseline only; no
  Radiant baseline/C2MIR execution. No commit requested; root exes untouched.
- **Tree:** `master`, HEAD `b8e29b077` plus recorded changes. Evidence under
  `temp/lambda_mir_mvp/language_untyped/`.
- **Last qualified release:** `p15_ndim_owners-release.exe`, SHA256
  `b81fef5ba18aa42050cadaa25a6c556b46d60555adab86cf3f73da0933fc80f0`.
  Frozen graph 769 release units / 3,543 source files; source drift zero.
  Current native VFS snapshots verified. All p14/p15 jobs are terminal.
  `p15-ndim-owners-validation-summary.json` records provenance.
- **Wrap-up audit:** release binary, 3,543 source hashes and 1,262 input
  hashes still match P15. All tracked build/test session handles are closed.
  The subsequent N-D representation investigation was read-only; no further
  compiler change was started. Resume from the next action below.
- **Inputs:** 1,262 recorded files and corpus v2's 483 frozen files, drift zero.
  Authorized v2 benchmark repairs remain distinct from original 72/77 (§10.8).
  Variadic tail-oracle repairs retain `[1, 1]` (S16.9.7, §10.55). No existing
  p13–p15 oracle changed; p14's newly authored declarations-only helper golden
  was corrected to empty and both compiler policies rerun (§10.58).
- **Implemented:** boxed values/collections, calls/defaults, captures, queries,
  paths, var transport, procedures/tasks/scopes, CRUD/open/fault frames,
  nominal types/methods, scalar joins, inferred fields, decomposition and
  precise confined pn homes (§10.43–§10.54). Variadics (§10.55), dependent
  definitions/binder values (§10.56), call forms/error-only domains (§10.57).
- **New p14:** imported type-value effects; shared JS bridge (arity 0–8),
  boxed adapters and canonical hosted import identity (S5.5.1v2, D7.2.1,
  D7.4.1–D7.4.3; §10.58). No additional AST fallback.
- **New p15:** all coordinates of N-D reads, fixed native coordinate storage,
  receiver roots and tensor requirements through aliases/direct calls
  (S8.2.4v3, D6.3.1, D6.3.4; §10.59). Frozen p14 Full controls reproduce the
  collecting-key buffer defect fixed in both compilers. All seven image
  fixtures now pass MVP under normal and both forced-GC policies.
- **Focused checks:** **177/177 MVP** across 59 fixtures / three GC policies;
  Full **165/177**, T0 **156/177**. AUTO/production control **75/75**. Twenty-five
  Python methods pass **970 script checks**, including typed 77/77 under each
  compiler. Untyped **77/77 each**. Native cache/index/import **11 + 11 GC**,
  admission/diagnostics **4 + 4 GC**, security **98/98 explicitly MVP**.
- **Real harness:** **137/152 MVP, 119/152 Full**. All ten added MVP rows pass;
  no prior passing row regresses in the common 142. The same fifteen MVP
  failures also fail Full. Full/T0 binder, var/rest and JS-value controls and
  T0 index exclusions remain visible. Aggregate Lambda baseline unqualified.
- **Census:** old frozen p12 supervisor completed **86/86** compiler chunks.
  Population 2,123; paired reports **1,873**, passing **1,334 MVP / 1,637 Full**.
  Input drift zero. Chunks 14, 16, 22, 23, 24 have six missing/timed-out reports;
  250 cases remain outside the paired count. Reported failures: 320 MVP-only,
  219 shared, 17 Full-only. This does not qualify p15. Both census sessions
  (49890, resumed 6287) are terminal. Disk-interrupted attempt preserved;
  atomic state writes and superseded-archive cleanup are recorded separately.
- **Known gaps:** N-D literal/dynamic-row normalization (three retained screen
  failures), views/edit/event lowering, named-function declaration snapshots,
  captured dependent annotations/default ordering, method lexical captures and
  receiver places, dynamic/projected var transport and native result joins.
  S9.1.4v2 governs legal capture writes; Full's legacy pn-parent write-back
  grants no new permission. Preserve shared frontend/package failures.
- **Performance:** no current timing qualification. Old p2 untyped screen:
  26/77 lower medians, 24 screening upper bounds below Full, GM 1.1913.
  Preserve all 49 v6 confirmations. Old typed p6/p5: ten slower/twelve faster
  flags, GM 0.99763. Goldens do not prove performance preservation.
- **Next:** replace unproved nested-row storage with a canonicalization proof
  for literal/opaque N-D producers; implement native views and investigate the
  census's MVP-only cases and unresolved chunks. Finish aggregate Lambda gates
  and 30-pair primary / 50-pair confirmation timing, including batched Sumfp,
  without competing jobs.

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
add annotations to make an untyped benchmark easier to compile. Canonical
workloads stay fixed within a measurement corpus. The user's later approval
allows repairing stale ports/oracles, with the original corpus retained and a
new source freeze before any comparison (§10.7).

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
3. Register an aggregate MVP semantic gate as `make test-lambda-mvp`, reusing
   the population in `build_lambda_config.json` and the existing runner. The
   baseline entry is implemented without duplicate runner-only aliases; extend
   it to the audited companion/in-process population before P0 closes. Do not
   maintain a second hand-picked list of passing scripts.
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

P0 added the central `make test-lambda-mvp` command. Its in-process and extended
coverage audit is still open. A focused CLI discovery command is:

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

## 10. Implementation evidence

### 10.1 P0 population and controls

The initial discovery census executed 2,006 scripts with explicit MVP/JIT on
an observed pinned executable (SHA256
`b0f1c750fd02890d114a2266a7cc063384e89fee854bc95fb8ab53c997e22c96`):
289 passed and 1,717 failed. `starting-mvp-discovered.json` and the losslessly
compressed raw log retain every result; `starting-census-summary.json` records
repeated rejection mentions, not independent feature counts. This host was
not a fresh source-matched build, so its results establish an investigation
starting point only.

The subsequent fresh release control rebuilt all 766 translation units.
`starting-build-manifest.json`, `starting-build-sources.json` and
`starting-runner-manifest.json` identify the executable, frozen sources, compiler
commands and flags. The initial untyped screen used this same executable for
MVP and Full, with one warmup and three paired samples per case, canonical
unchanged workloads, own goldens and Lambda-only execution timers.
`p0-untyped-screen.json` retains all 77 rows and failures;
`p0-untyped-screen-summary.json` distinguishes validity and median screening.

Full's five invalid controls are `beng/nbody`, `jetstream/nbody`,
`jetstream/richards`, `jetstream/deltablue` and `text/prettier_ast`. Their outputs,
assertion failures or runtime errors remain recorded. No failed engine supplies
a timing ratio or a successful performance gate. The first interrupted screen
and its missing-oracle diagnosis remain in `p0-untyped-missing-oracles.*`.

Nine untyped JetStream entries lacked goldens. New expectations came from the
unchanged scripts' success assertions; this did not change existing scripts,
workloads or oracles. `benchmark-source-manifest.json` freezes all relevant
benchmark files. `untyped-dependency-inventory.json` records each entry's local
import cone and annotated callable headers; the inventory does not claim to
count every local annotation. `benchmark-runtime-inputs.json` freezes five
actual file inputs. Those input hashes were captured after the first screen,
so that screen does not establish unchanged input bytes throughout its run.

The harness policy uses `LAMBDA_TEST_MIR_COMPILER`: ordinary script cases get
JIT, explicit JIT gets the selected compiler, and explicit T0/AUTO arms retain
their modes with the production selector (**D8.3.1**). Shared structured CLI
execution preserves specialized environment settings and argv boundaries.
Unsupported MVP admission cannot satisfy an expected language error. This is
covered by the routing test and actual positive/negative cross-tier probes in
`p0-harness-complete-routing.json`. Native helper-only tests are not evidence
that MVP lowered a language feature. In-process compiler tests and additional
companion loaders still need route-by-route auditing.

### 10.2 P1 sequence, element and lexical-scope slices

MVP now uses explicit item-position demand for lists, spread and query results.
Only direct producers receive void in item position (**S2.5.5v2**); a conditional
still evaluates its selected arm in value position. Functional blocks collect
all value contributions, while procedural blocks preserve the last value
through trailing statements (**S2.5.3v3**). Script and element content use
normalizing append; lists use sequence append (**D3.3.1**). Shared AST helpers
replace the identical Full/T0 classifiers instead of adding a third variant.
The frontend's existing classifier callers now use that same shared helper.

Ordinary elements share keyed-field staging with map literals. Fixed attributes
use descriptor fill; computed attributes and spreads use the existing literal
helpers. Content uses the canonical content append and finish functions.
Pointer conversion reuses the runtime's pointer-lane/container classification,
covering element/range and binary/datetime carriers without changing layouts
(**D2.3.4–D2.3.5**). Datetime literals publish through the existing owned scalar
ABI. File-output elements still reject explicitly until their local constructor
error region is implemented.

A list-local declaration at script scope no longer enters the module slab.
Otherwise two independent `let dt` list scopes generated the same `_gvar_dt`
symbol. Only bindings in the script's global scope get import references;
lexical locals stay in the entry frame. Imported/public bindings retain their
existing reference scheme. No name-specific exception was added.

The sequence candidate's build/source manifests identify every tested byte.
`p1-family-comparison.json` compares the same 61 cases on the fresh starting MVP
and candidate: 33 → 42 passes, nine gains, no new failures. Existing
`array_spread_element_type`, `binary_decode`, `computed_keys`,
`map_spread_override`, `spread` and `spread_star` now pass, alongside the three
new fixtures. Nineteen failures stay visible, including older fixtures that
violate the current frontend's error-bearing return contracts.

`p1-typed-goldens.log` passes all 77 typed entries under both compilers and the
three new fixtures under ordinary/forced-every/seeded poisoned GC. This is a
correctness check, not a timing result. `p1-runner-checks.log` verifies both
canonical source/oracle variants and untyped supplemental harness construction.
A transient procedural-discard mistake exposed by the family check was fixed:
a terminal return owns the result, so preceding calls keep discard demand.
The superseded failing candidate and its logs remain available.

### 10.3 Current feature and delegation inventory

The following is a source-backed work inventory, not a claim of complete
support. Rejections remain failures; they do not select Full lowering or a
whole-script AST fallback (**D8.3.1–D8.3.3**).

| Family | Full route | MVP state / next phase | Semantic evidence or remaining probe |
|---|---|---|---|
| Scalar literals and numeric/string arithmetic | MIR instructions and registered scalar helpers | Native paths retained; binary/datetime, boxed power, vector comparisons and reference/subtype relations added; remaining surface open | Typed corpus; `mir_mvp_elements`, `binary_decode`; remaining scalar GTests |
| Lists, functional blocks, spread and root content | Local MIR collection and shared append/finalize helpers | P1 sequence slice implemented | `mir_mvp_content`, `mir_mvp_root_content`, spread family |
| Arrays and shaped maps | Local MIR plus shared allocation/admission/COW | Existing typed and boxed paths; broader domain tests still fail | Typed corpus and all registered MVP array/record fixtures |
| Ordinary elements and attributes | `emit_element_storage`, literal/fill/content runtime helpers | P1 construction added; qualified attributes and nominal contracts need complete family checks | `mir_mvp_elements`, `computed_keys`; element corpus |
| File/PDF output elements | Constructor-local MIR error region plus output runtime | Explicit rejection pending P1/P3 region support | Existing PDF/output GTests; no waiver |
| Type values, `is`, constrained tests and match | MIR type expressions, runtime membership, local predicate lowering | Generic/structural membership and value/range/union match added; exact carrier guards kept separate from numeric subtyping; predicates remain P1/P3 | `array_ndim_join_spread`, `datetime`, type/match corpus |
| Conditions and all loop/query forms | Local MIR control, collection/group/order/join helpers | While, single-source group/order/windows and dependent products; grouped products, optional sources, joins and remaining loops stay P1 | Complete loop/query GTests, failure and GC cases |
| Declared binding/error outcomes | Shared admission with local outcomes | Current deferred boxed-outcome path retained; all producer/native boundaries need audit under S11.4.1v5 | Negative runtime cases and MVP outcome fixtures |
| Named/default/rest/spread/pipe calls | Shared call analysis plus MIR entries/adapters | Fixed direct/default subset and ordinary boxed indirect calls; normalization remains P2; dynamic named/var calls stay rejected per S12.3.2 | Existing call, order, var and negative-signature tests |
| Nested closures and function values | MIR environments/boxed dispatcher | Anonymous and named zero-capture function/procedure values and indirect calls added; capture environments remain P2 | Closure/capture corpus and forced GC |
| Nominal objects/methods/modules | Local MIR and shared module/object runtime | Existing public imports; remaining object/function/module forms P2/P3 | Package corpus and in-process loaders |
| Views, edits, events, references, CRUD and tasks | MIR lowering plus shared subsystem C ABIs | Complete route audit and P3 implementation pending | Existing document/concurrency/runtime GTests |
| Calling an already interpreted Function | `lambda_dynamic_invoke_by_count` in `lambda-eval.cpp`, then `interp_call` / `interp_call_borrowed` if it does not promote | Allowed same shared dispatcher at P2; do not create a new per-AST fallback | Ordinary dispatcher exercised; interpreted Function/promotion probe remains open; dynamic var rejection retained |
| MIR bytecode interpretation | Full can select MIR's interpreter explicitly or for large O0 input | Distinct from the AST interpreter and from unsupported-feature delegation | Record the executed mode; no native-speed claim for this route |

The audited existing interpreted-Function dispatch is conditional on the actual
`Function::entry_abi`; it upgrades a hot function before selecting the native
prototype. This does not authorize routing arbitrary rejected MVP nodes to T0.
There is no new delegation in this implementation slice. The remaining matrix
must enumerate current AST kinds/contracts and their fixtures before P0 closes.


### 10.4 Callable values, operators and ordered patterns

`p1_pattern_membership` includes the following source-backed additions:

- Zero-capture named definitions can be passed, returned, stored and called.
  A single shared-index scan finds actual value references by definition
  identity. Lambda need not populate `AstFuncNode::entry`, so relying on that
  field alone missed escapes. Escaping definitions use checked Item parameters
  and the C-reachable companion transport; direct-only definitions retain
  their native entries (**D7.2.2, D7.4.2**). Callable/type definitions are not
  local storage and cannot enter borrow/snapshot-elision reads.
- The shared dynamic dispatcher retains argument slots with verbatim array
  append, callee-Error ordering, colour checks and a caller-owned scalar home
  (**D7.2.2, D6.2.3, S12.1.4v4**). No new AST fallback is introduced. Named
  and `var` dynamic-call restrictions remain as ruled by **S12.3.2**.
- Bare builtin references use the same publication helper as Full, preserving
  identity, names and colours. The existing runtime publishes these as foreign
  identity values, so dynamic Core invocation rejects; this implementation
  does not claim builtin callback adapters.
- Power, keyword vector comparisons, subtype relations and reference equality
  use shared semantic kernels. The two MIR emitters share the binary helper
  mapping and the value-pattern physical test. Symbolic scalar comparisons
  keep their existing behavior (**S10.2.2v2, S11.1.4**).
- Literal, range, union, structural and first-class patterns use membership or
  equality according to the pattern value, accepting only `BOOL_TRUE`.
  Match evaluates the scrutinee once and scopes nested `~`/`~key` correctly.
  Ordinary nonconstrained handler filters use that same test and preserve an
  unmatched input (**S11.2.1, S10.1.3, S7.6.9**). Constrained predicates remain
  rejected pending their existing callable-environment lowering.

A family mismatch exposed an older MVP semantic error: exact native carrier
checks had been substituted for membership. **S4.4.1** includes `int` in
`float`; **S11.2.1** selects the first matching arm. Numeric and range-to-array
membership now retain the shared kernel, while exact native dispatch guards
remain unchanged. `match_expr` and three existing handler cases newly pass.

The old `mir_mvp_scalar_outcomes` golden expected `scalar_ordered(3)` to skip
its earlier `case float` and select `case int`. That expectation contradicted
both rulings. Current MVP, current Full and fresh starting Full all produce
`false`. The golden now records `false`, and the source comments its reason;
the case/order/workload were not changed. The original golden, hashes and all
three controls are retained in `scalar-outcomes-oracle-correction.json` and
its referenced artifacts. The initial 75-case run remains 50 passes/25
failures under its original oracle; a separate 2/2 rerun verifies the corrected
scalar case and `match_expr`. Do not rewrite the original census as passing.

All seven P1 fixtures and three P2 fixtures pass normal, forced-every/poison
and seeded/poison modes on both compilers (`p1-pattern-membership-gc.log`).
Open-var remains MVP-only in that focused Python check because Full still
mutates a simultaneous plain snapshot. All 77 canonical typed entries pass
both compilers (`p1-pattern-membership-typed-goldens.log`). The latest completed
untyped rerun, on the preceding `p1_patterns`, remains 64/77 with the same 13
failures and no new failures among the original 59 correct cases. None is
performance qualification.

The compiler-route audit also found that extended discovery already uses the
shared CLI helper. DOM-node native data tests do not compile Lambda scripts.
Security runtime and concurrency tests call `run_script_mir` in-process with
explicit tier overrides; they still need scoped compiler selection and a
source-matched native test build. Their native helper passes cannot be counted
as MVP lowering evidence. The baseline make target is implemented, but its
complete applicable population is not yet qualified.


### 10.5 Membership proofs, query windows and performance debt

The first subtype-correct candidate used generic membership even when a
producer proved its successful domain. The typed SHA-1 screen exposed its
cost. MVP now uses the shared value-set relation
`lambda_type_contract_is_subtype` to discharge that test, with the Error arm
kept separate. This relation does not rely on boundary conversion. Unknown
numeric/range domains still use membership; native entry guards keep their
exact-carrier meaning (**S4.4.1, S11.1.4, D4.3.1**). The ordered-float-arm
oracle remains correct. The SHA-1 body has the starting instruction count
again; its retained MIR diff changes embedded host addresses, not helper-call
counts. Fib's inspected body is identical. Towers' body grows and changes
inline callees; its screen flag remains an investigation, not attributed to
noise (`proof-emission-comparison.json`).

Single-source query `let` clauses now run in row scope before `where`, and
`limit`, `offset` and `limit last` trim the ordered stream before its one list
finish (**S2.5.2v2, S14.1.1v2**). Both MIR emitters use the same physical
finalizer and the existing `for_window` helper. Offset then limit are evaluated
through each emitter's value hook; sorted output stays precisely rooted across
those evaluations. No new collection/window semantic implementation was copied.
Multiple sources, joins, optional sources and decomposition remain open.

`p1-query-windows-std.json` passes seven real standard-library cases: row lets,
for clauses, grouping/windows, exponent, vector arithmetic and match. All 77
typed entries pass both compilers on that exact release
(`p1-query-windows-typed-goldens.log`). The new fixture tests empty/singleton
collapse, invalid dynamic counts, ordering and retained out-of-band floats.
MVP passes all three GC modes. Full fails the seeded poison mode: its retained
first float becomes zero. Fresh starting Full and the preceding Full candidate
reproduce the same defect (`query-windows-full-seeded-controls.json` and
`query-window-values-probe.json`). The valid oracle and failed Full subtest
remain in place; the root cause is still open.

`p1-typed-regression-screen-summary.json` records all 77 valid typed rows,
18 flagged three-pair intervals, Sumfp's subresolution observation and four
whole disturbed rounds discarded by the runner. No Radiant/C2MIR timing was
run. `typed-confirmation-selection.json` preselects 22 typed follow-up rows
from those independent flags and the existing CD/Base64/DeltaBlue/timer debt;
it does not contain final samples. After the actual membership proof change,
the separate SHA-1 diagnostic measures 23.987 ms versus 23.973 ms, ratio
1.0006; its two-sided interval is approximately [0.964, 1.080]. Preserve both
screens and every earlier flag. No repeated-sampling pass or no-regression
claim is made.


### 10.6 Multiple-source queries and references

The current tree factors single-source iteration into one recursive source
walker. It evaluates a dependent inner collection once per enclosing row,
retains each source snapshot, and applies row lets/filtering at the complete
row before the existing ordering/window finish (**S9.2.3, S2.5.2v2**).
Straight procedural products are included. Grouped products, joins, optional
sources and explicit early-control transfers still reject with specific
reasons until their row/cleanup lowering is implemented; the broad language
gate remains open. `mir_mvp_query_sources` has its own golden and covers
dependent sources, map keys, Unicode text, empty sources, ordered windows and
procedural accumulation. Both compilers pass its three GC modes. Five selected
standard-library query cases pass; all 77 typed entries pass both compilers
on the subsequent `p1_query_products_top` candidate.

Reference forcing and address lookup now call the shared `fn_force` and
`fn_address_of` helpers, with their GC/reentry effects included in planning
(**S2.4.3v3, S2.4.4**). The shared unary helper mapping serves both emitters.
`temp` installs context-owned documents and does not borrow a caller variable;
its ordinary one/two-argument forms are admitted. The new reference fixture
passes both compilers in all three GC modes. All 77 typed entries pass both
compilers on `p1_references`; the untyped correctness sweep is running.

The subsequent verified source slices add type-query expressions, inline
string/symbol pattern values and computed path keys (§10.7). Pattern-bearing
contracts remain separate pending their physical representation audit.

### 10.7 Type queries, navigation and open writes

`p1_query_pattern_paths` adds type queries through `fn_query`, compiled inline
string/symbol pattern values, and computed paths. Both MIR emitters now use
the same computed-path physical helper (**S8.2.4v3, S11.1.2v4, S2.4.2v5**).
Its new fixture passes both compilers in all three GC modes. The broader
58-case real harness family has 25 passes and 33 failures; eight timed-out
cases remain an investigation. No timeout was excluded from that result.

Parent/root navigation shares one MIR helper and the structural lineage
classifier used by T0. Nested match contexts save/restore item, key, parent
and root separately (**S10.4.3v2, S10.5.3v2**). Ordinary stored maps have no
hidden lineage. The new fixture uses structural equality for ordinary maps;
identity equality is not a pointer comparison for those values. A relative
path's selected root retains its root step (`\\./`). The first fixture draft
used the wrong identity/path expectations; its failed probe remains in
`p1-navigation-gc.json`, followed by the corrected fixture checks. No existing
oracle was weakened.

Inferred binding observations no longer become write contracts. Unannotated
bindings have open admission, retain only proven private lanes, and widen to
Item before an open `var` borrow can replace them. Place invariance runs after
the inference/effect fixed point. Erased computed/member writes use
`index_assign_cow`, preserving actual array/map/element key dispatch and the
replacement owner (**D4.3.1, S12.2.1v3, S8.2.1v4, S9.1.3**). The first
open-contract candidate exposed an array-only setter regression in jq Mix;
the shared setter fixes it. Keep both reports. The resulting candidate passes
all 77 typed goldens under both compilers and the original corpus's same
72/77 untyped rows as Full. All new focused fixtures pass the three GC modes;
Full's previously recorded seeded window failure remains the sole failure
in that Python method.

The real harness's relative-path return case exposed missing collection
effects for path literal allocation. `p1_path_effects` plans that effect;
the five-case path/navigation/open-write slice now passes. Native security
and concurrency fixtures also use scoped compiler selection, preserving
explicit T0/AUTO arms and restoring selectors after fatal assertions. Their
two translation units pass release syntax checks; native execution is not
yet qualified.

The user approved benchmark repairs after the paired original corpus reached
the same five invalid controls in both compilers. `benchmark_repairs/before.json`
retains original source hashes and copies. The repairs in progress are:

| Entry | Root cause and repair |
|---|---|
| Beng/JetStream N-body | Mutators accepted plain snapshot arrays; add the missing caller-owned `var` modes, already present in the typed ports (**S9.1.2, S9.1.3**). Keep the simulation sizes, steps and energy oracle. |
| JetStream Richards | Pointer aliases drift under value semantics. Extract the existing AWFY untyped handle-store algorithm once for both untyped wrappers; retain 50 iterations and counters 2322/928 (**S9.1.2, S9.1.3, S9.1.5v2**). Typed kernels remain unchanged. |
| JetStream DeltaBlue | Local record/array snapshots swallowed writes. Borrow the planner and publish through its owner; keep `stay` Boolean. Consume the chain/projection result in the existing 20 rounds and check checksum 5980, as the typed port does (**S9.1.2, S12.2.1v3**). |
| Text Prettier | The untyped entry prints the formatted document, but only its checksum oracle existed. Add the complete stdout oracle already established for the same AST/printer in the typed entry. |

All six affected wrappers pass both compilers in
`benchmark-repairs-validation.json`. Corpus v2 freezes 483 files: five changed
sources and two additions. Its paired sweeps on `p1_path_effects` pass all 77
untyped entries in both compilers, with runtime input hashes checked before
and after. The original 481-file corpus and all earlier results remain intact.
Performance acceptance, including every earlier typed flag, remains open.

### 10.8 Last-index scope and native-test qualification

`last` now reads the evaluated subscript receiver once, restores the enclosing
receiver after nested keys, and works in reads, writes and projected `var`
places (**S7.2.2–S7.2.3**). The two MIR emitters share the existing structural
test that excludes a nested key's own `last`. Ordinary subscripts without
`last` add no receiver boxing. The real `phase2_last_index` case and the new
focused fixture pass; both compilers pass its three GC modes. All 77 typed
goldens pass both selectors on that exact release and corpus v2.

Private security/concurrency runners now link the source-matched release
objects and the real host broker. Their first archive link selected
`mempool.c`'s standalone weak VM stubs, so both smoke commands failed before
the first script with GC slab allocation errors. Those runs do not qualify
MVP. The corrected private link retains all CLI-image objects so the strong
VM provider is selected. Original logs/manifests
remain under `p1_last_index-native-tests/`; corrected artifacts use the
`-providers` directory. No shared executable or vendor file was replaced.

`p1-last-index-v2-typed-screen.*` completes all 77 rows and discards five
disturbed whole rounds. Fourteen rows have a two-sided interval above parity;
these are diagnostic flags from three pairs, not primary qualification.
Towers is still about 20% slower. The earlier 18 flags remain retained;
`typed-confirmation-selection-v2.json` appends the new ones to the prior
selection, producing 31 preselected follow-up rows. Sumfp is timer-limited.

### 10.9 Immutable MVP image reuse

The source-matched native smoke passes three of four security arms. The
remaining JIT arm rejects `AST_NODE_START`, exposing the still-open task
lowering; it is not waived. The original cache smoke passes its explicit T0
arm but fails the JIT image-reuse counters: `runner.cpp` deliberately disabled
artifact reuse for MVP. Full passes the three selected cache controls.

MVP now publishes through the same immutable-image and dependency-cone
protocol. Its backend key is `mir-direct-mvp`, separate from Full's
`mir-direct`, so identical source cannot select another compiler's image
(**D8.5.1**). No runtime value/layout or cache ownership protocol changed.
`p2_image_cache` passes eight source-matched native cases: eager image reuse,
MVP/Full isolation and repeat reuse, logical/dense module mapping, explicit T0
template/import reuse, MIR import-cone reuse, changed-dependency invalidation
and concurrent single-flight compilation. New/old compiler selectors are
restored by the shared native fixture guard. A new test's const-qualification
compile error was corrected; its failed source/log remain retained.

The all-77 untyped v2 `--lambda-only` screen on this release is running.
Completed rows expose large scalar/loop gaps, for example diviter around
15 times Full and mbrot around 10 times Full. Do not confuse 77/77 correctness
with performance completion. Investigate shared parameter/loop/boxing costs
across the whole unresolved population; keep declared `any` admission open
when adding private observations (**D4.3.1–D4.3.4**). All performance gates,
the broad language matrix, task/closure/pipe lowering and aggregate GTests
remain open. No final performance qualification or new fallback is claimed.

### 10.10 Untyped performance screen and private integer inputs

The three-pair `p2-image-cache-v2-untyped-screen.json` includes all 77
repaired-corpus rows with valid outputs. MVP has the lower median on 19;
17 have a screening one-sided interval below parity. The geometric mean
MVP/Full ratio is 2.226. These small samples do not qualify performance.
`p2-image-cache-v2-untyped-screen-summary.json` retains all 60 open rows
and the worst ratios. HashMap (487 times Full) needs an ownership-path
investigation; several scalar kernels retain unnecessary boxed parameters.

The next candidate adds private integer parameter observations to direct-only,
non-escaping definitions. Public/boxed, defaulted, borrowed and reassigned
parameters retain their existing carriers. All direct incoming edges are
validated, including recursive edges; a non-integer, nullable or Error
observation widens back to Item in the same fixed point as local writes.
The source admission contract remains unchanged, including implicit
`any ! error` admission (**D4.3.1, D4.3.2, S11.4.3**). The first candidate
checked only explicit `any`, so its Diviter diagnostic made no gain. That
result and source are retained; the corrected candidate includes implicit
parameter admission without altering it.

`p4_closed_inputs` also extends the existing exclusive projected borrow to
open parameter contracts. A borrow needs ownership rather than a packed
layout proof. The previous snapshot capture made each HashMap store clone
its entire array. The shared `cow_path_borrow_keys` path preserves aliases,
rooted homes, replacement publication and error writeback (**S9.1.2,
S9.1.3v3, D5.4.6**). Multiple/retaining arguments keep the established
snapshot path. The new fixture covers an outer snapshot, nested places,
scalar leaves, replacement, simultaneous plain arguments and partial error
publication.

All 77 v2 untyped goldens pass MVP; all 77 typed goldens pass both compilers.
The two new fixtures pass all 12 compiler/GC combinations. Four focused
real-harness cases pass. The content/GC matrix passes 95/96, retaining only
the pre-existing Full seeded-window defect. Source-matched seven-pair
diagnostics report Fib 2.265 ms vs Full 1.078 ms, Diviter 224.469 ms vs
264.408 ms, and HashMap 118.317 ms vs 72.525 ms. Only Diviter is below
parity in this slice. These are workload timers and diagnostics, not
end-to-end measurements or final acceptance. The earlier HashMap 34-second
result remains in its original report. Typed performance preservation,
all remaining untyped rows and broad language qualification remain open.

A subsequent candidate seeds private integer results only for closed,
unannotated definitions whose normal exits are explicit returns. Every
return edge must retain an integer success domain at the widening fixed
point; null, collected content or unknown returns retain their old ABI.
This targets the remaining boxed recursive results. `p4_integer_results`
passes all 77 untyped goldens and the semantic matrix except the known Full
seeded-window defect.

`p4_integer_effects` defers open-receiver effects until these lanes are
known. Its sweep exposed a native-tail-call bug: source admission boxed an
untyped operand and the backedge moved that Item into the native integer
register. The recursive fixture and Divrec hung; this task's affected
sweeps were stopped and retained as failed/incomplete, with exact metadata
in `p4-integer-effects-interruption.json`. The fix converts each admitted
backedge operand to its planned binding carrier, as the initial direct
entry does. No benchmark workload or timeout was changed.

`p4_private_calls` additionally preserves a proven integer carrier across
an open parameter boundary, avoiding a redundant box/unbox while keeping
ordinary Error admission for other inputs (**D4.3.2, S11.4.3**). This release
passes all 77 untyped MVP goldens and all 77 typed goldens under both
compilers. The 96-case content/GC matrix retains only the known Full seeded
window failure; all 18 callable/GC combinations and three open-var checks
pass. The expanded integer fixture covers recursive returns, nullable
returns, collected fallthrough and Error outcomes. Four real-harness
input/borrow/path cases pass.

The new all-77 untyped and all-77 typed release screens run serially, pausing
around other checkout builds. Early diagnostics show Fib 0.569 ms versus
Full 1.100 ms and Sum 0.102 ms versus 0.267 ms. These do not qualify the
remaining rows. Preserve the earlier typed 31-row confirmation selection;
Towers and other regression flags remain open until measured on this source.
No full language/GTest or performance gate has passed.

### 10.11 Exact float domains and final place admission (in progress)

The closed scalar proof now shares one incoming/return-edge walker for integer
and float candidates. Float selection requires exact carrier agreement across
all calls; integer membership in `float` does not authorize a hidden inferred
conversion (**D4.3.1, D4.3.2, S4.4.1**). Defaulted, borrowed, reassigned and
escaping parameters retain the previous entry protocol.

This review exposed an existing constant-propagation defect: a typed float
function with one literal direct caller and an escaped value use could replace
the callback's later argument with the direct caller's constant. The new
named-value regression reproduces old MVP output `1.25, 1.25` against Full's
`1.25, 2.5`; `p4-float-constant-escaping-control.json` retains that probe.
`plan_float_parameters` now excludes boxed/escaping entries. The original
pre-extension fixture is retained in `pre-float-named-fixture/`.

`p4_scalar_domains` (SHA256
`31d2dd7ca06784e0e4fe76e1845b80d3253eb8d16c58226f9522a7defb422cff`)
passes the extended callable/GC checks and all 77 typed goldens under both
compilers; the content matrix still has only the known Full seeded-window
failure. Its full untyped sweep is **74/77**, with Havlak, Splay and jq BF
rejected by early write-place carrier admission. Retain that failed census.
Provisional scalar function results had seeded locals before the producer
proof widened; write-place admission ran before the same fixed point.

The next candidate, `p4_scalar_places`, validates place structure/ownership
early and validates its physical carrier only after inference converges,
using one parameterized admission helper. The expanded fixture includes
record/array results with a scalar alternative followed by projected writes.
Its SHA256 is
`257fe88f708e92ff5504db00be46d7909dface39ab86f2f73c5c257fc4edfafe`.
All 77 untyped MVP goldens and all 77 typed goldens under both compilers pass.
The content/GC matrix still has exactly the known Full seeded-window failure;
all 18 callable/GC and three open-var checks pass. Five real-harness
input/borrow/named-value/path cases pass. This is the latest validated engine
release; no performance campaign on this float candidate has completed.
The earlier `p4_private_calls` all-77 untyped screen and then typed
regression screen are still running, pausing around a separate checkout
baseline. The binaries and source manifests are frozen, so these
results must not be attributed to the newer float candidate.

The earlier `p2_image_cache` source-matched cache runner now also passes all
eight selected cases under forced-every-allocation GC with poison enabled
(`p2-image-cache-native-forced-gc.json`). These certify that cache slice;
they are not native qualification of the newer scalar candidate.

**Next recovery steps:**

1. The `58427` and `77455` all-77 campaigns completed; their summaries are
   retained. `screen_frozen_candidate.py p4_no_value_inline` now runs the
   current complete untyped and typed screens serially. Do not start competing
   benchmark runners. Each report manifest owns its frozen release even if
   the checkout changes.
2. Summarize every untyped open row and every typed regression flag; append
   new flags to the preselected 37-row confirmation population. Sumfp's
   resolution-limited typed case still needs its matched batch.
3. Complete the new `p4_no_value_inline` screens, including all float
   kernels and typed regressions. The focused Towers flag remains open. Preserve all earlier failed
   or slower evidence. No latest-source performance win is yet claimed.
4. Resume the P1 language slices (pipes/provisos and remaining calls/control),
   P2 captures/normalization and P3 tasks. Full's pipe walk currently uses
   child-only element traversal despite **S10.1.2v4/S10.1.6** requiring the
   unified attribute/child walk; reuse the canonical iteration helpers rather
   than copying that discrepancy. A proviso also needs ordinary condition
   Error transport and the correct subject/key/parent/root bindings.
5. Before admitting `AST_NODE_START`, treat its inner argument-array syntax
   as task materialization, mark the callable boxed and exclude the inner
   syntax from ordinary incoming-argument inference, matching Full's
   prepass. Preserve real task-scope join/unwind ownership; a runtime helper
   call alone does not close this feature. Native task/security and the full
   language matrix remain open.

### 10.12 C Bool outcomes and completed integer screens

The generic builtin lowerer treated raw `BOOL_ERROR` as a successful Boolean.
The new fixture reproduces six wrong observations on `p4_scalar_places`: bad
`contains`/`starts_with`/`ends_with` input, `not`, conditional selection and
postfix propagation. Full produces the correct Error outcomes; the exact
probe remains in `p1-builtin-bool-before.json`. MVP now consumes the registry's
instantiated result relation, matching Full's existing policy: only a plain
error-free Boolean result can retain the raw lane, while an error-capable
result or `^` uses the existing `bool_call_outcome` helper (**S7.8.1, S7.9.3**).
No runtime helper, type model, fallback or oracle changed.

`p1_builtin_bool`, SHA256
`e8089c1c2f47fad05c2c129c48411ed2cbab67bd0dbbeff6da3beebb733f8695`,
passes all 77 untyped MVP goldens and all 77 typed goldens under both
compilers. All 18 callable/GC and three open-var combinations pass; the
102-case content/GC matrix retains only the pre-existing Full seeded-window
failure. Six focused cases pass in the real GTest harness. The broader
language matrix and native task/security gates remain open.

The older integer-only `p4_private_calls` all-77 screens completed. Untyped
outputs are valid on all rows: 22 lower medians, 21 screening intervals below
parity, geometric mean MVP/Full 1.648 and 18 disturbed whole rounds discarded.
All 56 open rows remain in `p4-private-calls-v2-untyped-screen-summary.json`.
This improves the previous screen's 19/17 and 2.226, but the target is open.
Its typed screen flags eight possible regressions: Fib, Diviter, Permute,
Storage, Towers, Mbrot, Microdiff and jq Records. Towers is 23.9% slower than
starting MVP. Five disturbed rounds were discarded; Sumfp remains
resolution-limited. Previous flags are retained. The append-only
`typed-confirmation-selection-v3.json` now has 33 cases. These three-pair
results do not qualify performance or apply to the newer float/Bool release.

The latest `p1_builtin_bool` all-77 untyped and all-77 typed screens are
running serially (session `77455`), with a source-frozen release and corpus
v2. No C2MIR or Radiant campaign ran.

### 10.13 No-result procedure inlining and preserved control failures

A small procedure with only declarations/local writes has no normal scalar
result. The shared `ast_proc_block_last_value` classifies this; any explicit
return owned by the same function clears the fact. Nested definitions use
their own owner. The inliner may now consume such a procedure's Item result
without treating it as an unknown wide scalar. It still retains the null
result, ordinary Error outcomes, borrow publication and scope exits
(**S2.5.3v3, S12.1.2v3, D6.2.3**). No return ABI or user contract changed.
This addresses the push/pop inlining topology behind Towers without naming
that benchmark in compiler code.

`p4_no_value_inline`, SHA256
`00969c5b1e2aaef66a6e579e054e8fe10dd875926c380bad0345f1c36181b44a`,
passes all 77 untyped MVP goldens and all 77 typed goldens under both
compilers. Seven focused real-harness cases pass. All 54 MVP content/GC
combinations, all 18 callable/GC combinations and three open-var modes pass.
The four-method command records four failed Full subtests: the known
seeded-window defect and all three modes of the new scalar-var snapshot
loop. Its checks preserve null replacement, partial-store errors, early
returns and a wide value saved across repeated calls at one inline site.

`p4-no-value-scalar-store-controls.json` reproduces the new Full mismatch on
`p1_builtin_bool`, this candidate and the freshly rebuilt starting release;
MVP passes each control. Removing the preceding deliberate OOB error does
not fix Full; explicit T0 passes (`p4-no-value-no-prior-error-controls.json`).
The normal-sized float probe also fails Full's saved-value check
(`p4-no-value-normal-var-controls.json`). The exact Full root cause remains
unresolved. Do not weaken the golden or claim this overall command passed.

The nine-pair Towers diagnostic is 0.156 ms versus Full 1.237 ms and starting
MVP 0.131 ms. Its MVP/starting ratio is 1.191, two-sided interval
[1.055, 1.366]; the typed regression remains open. The one-sided MVP/Full
upper bound is 0.139; this focused diagnostic is not complete performance
qualification. No disturbed round was discarded.

The completed `p1_builtin_bool` all-77 screen has all outputs valid, 21
lower medians, 21 screening intervals below Full, geometric mean 1.355 and
three disturbed rounds discarded. Its typed screen flags nine rows:
Divrec, GCbench, Binarytrees, Permute, Towers, Regexredux, Formatted Output,
Microdiff and jq Tree; Sumfp remains resolution-limited. The append-only
confirmation population is now 37. These results belong to that preceding
release, not this inlining candidate. Primary and confirmation campaigns,
the complete language matrix and aggregate MVP GTests remain open.

### 10.14 Provisos, variadic print and native scalar borrows

MVP now lowers the single-value proviso (**S10.1.5v3**) with Full's shared
physical subject/result helpers. The subject is evaluated once; `~key` is
null; the enclosing parent/root survive; a rejected predicate gives null.
The nullable result retains its emitted carrier, so consuming it in numeric
comparisons does not invent an unknown Item-to-Bool conversion. The new
`mir_mvp_provisos` fixture includes implicit fields, nested matches, ordinary
predicate Error, short-circuiting and a cold float held across allocation.
Sequence and aggregate pipes still require their own lowering.

`mir_mvp_scalar_var_lanes` exposed two distinct native borrow defects: the
precise caller cell needs a tagged Item, and an initializer's singleton range
cannot survive a var call. Native int/Bool parameters are now admitted;
shared publication converts local lanes to Items, call setup boxes the cells,
and reload converts to the caller's carrier. Range proofs exclude borrowed
bindings and escaping entries; float constant proofs also exclude borrows.
Dense array entries retain their existing no-replacement policy and decline
scalar var parameters. The fixture exercises inline/native calls, forwarding,
projected field COW, partial-error publication and an escaping integer reader
(**S9.1.3, D4.3.1–D4.3.3, D6.1.1, D7.4.2**). Native float and other scalar
var contracts still need their ownership/transport work; this is not complete
var-parameter coverage.

Variadic and zero-argument `print` use Full's extracted physical lowerer.
Separators precede evaluation of each following argument, preserving observable
source order; positional values and wide scalars keep their ownership. The
existing single-argument MVP provider outcome is preserved; Full's existing
variadic completion remains null. No provider-failure behavior was newly
qualified. The new fixture checks nested printing, zero arguments and cold
scalar arguments (**S7.9.3, D6.2.3**).

`p1_scalar_var_proofs`, SHA256
`41f21a88ecf5c1a07144e9f0b99a04ce3b135a4370b0cfca8240394c6220ef16`,
passes all 77 untyped MVP and 77 typed goldens under both compilers. All 18
new-fixture compiler/GC combinations pass, as do the existing callable/open-var
checks. Three newly discovered cases pass the real core harness
(`p1-scalar-var-proofs-core-slice.json`). Two earlier selection attempts are
retained: one had an incorrect relative executable path, the other used the
older runner without MVP-directory discovery and selected zero tests. Neither
is test evidence. The successful runner is `p2_discovery_gtest.exe` in the
private source-matched CLI workspace.

The broader five-method Python command is **failed**: four known Full control
subtests and 42 failures across 14 older MVP ownership fixtures. The latter
include stale acknowledgment/declaration expectations and Error comparisons;
their exact classification is not yet complete. Every one also fails both
compilers in the fresh starting release in normal mode
(`p1-scalar-var-legacy-fixture-controls.json`). No test was removed, weakened
or declared passed. The new scalar-var golden and the existing benchmark
oracles remain unchanged by this investigation.

The completed `p4_no_value_inline` untyped screen has 77 valid outputs, 23
lower medians and 20 screening intervals below Full. Geometric mean MVP/Full
is 1.341; seven disturbed whole rounds were discarded. All 57 open rows remain
in `p4-no-value-inline-v2-untyped-screen-summary.json`. This three-pair screen
is not final qualification. Its typed campaign is still running as PID 90771's
serial campaign; recover process metadata before launching any competing
measurements.

**Recovery:** `p4_explicit_top_returns` is built (SHA256
`88186008b8c52d7d4a1e04362eaf7a23c2cf271a1f9852632bd639769b9c34ea`).
It extends the existing closed-result proof to explicit top contracts while
retaining public admission and the Error companion (**D4.3.1, D7.4.2**).
Mixed/nullable/unknown returns widen back to Item. Its extended fixture,
complete typed/GC command (session `7622`) and all-77 untyped correctness
sweep (`60812`) are running. Do not claim this candidate validated yet.
Next finish these checks and the preceding typed timing screen; retain all
old flags in the append-only confirmation selection. Then continue the open
language slices and performance rows. Full language, native and aggregate
MVP gates remain open; no commit or completion is claimed.

### 10.15 Closed top results and readonly primitive-array inputs

The existing closed-result fixed point now also accepts explicit `any` top
return contracts. Every normal return must retain the exact inferred int or
float carrier; mixed, nullable and unknown returns restore the Item ABI.
Public return admission and ordinary Error transport remain unchanged
(**D4.3.1–D4.3.3, D7.4.2**). The extended closed-parameter fixture covers
recursive int/float results, explicit top contracts, nullable/mixed outcomes
and returned Error. `p4_explicit_top_returns` passes all 77 untyped MVP and
77 typed goldens under both compilers; its four-method command retains the
four Full control failures known at that point.

Readonly closed parameters can also retain a primitive-array element domain.
The observation is unbounded and does not install a source contract or an
array certificate. Every incoming edge must prove the same primitive element
kind; int-to-float subtyping cannot merge element carriers. The existing
`mvp_parameter_borrowable` indexed-use audit rejects mutation/whole-owner
escape while allowing scalar child reads. An erased child-retention flag
alone was too conservative: the first `p4_closed_array_inputs` candidate
passed correctness but left the example reader on generic `fn_add` and
`fn_len_checked`. Reusing the ownership audit produces native arithmetic
and header length reads while retaining checked index misses. Both MIR dumps
are preserved; no benchmark name or workload constant appears in this proof
(**D4.3.1–D4.3.3, D5.4.4, S9.1.2–S9.1.3**).

`p4_closed_array_borrows`, SHA256
`d8dcb7c3994ee79fa1f3d3bf412577a5a60f8b3087bee5a8fa6cd1c0056e9575`,
passes all 77 untyped MVP and 77 typed goldens under both compilers. All 66
MVP content/GC, 18 callable/GC and three open-var combinations pass. The
closed-array fixture also checks different extents, mixed int/float caller
arrays, open element replacement, escaped calls and Error argument transport.
Its extended five-row oracle passes MVP in all three GC modes. Full gets two
older observations wrong (open replacement and the escaped call), including
on the fresh starting release; exact controls remain in
`p4-closed-array-full-controls.json`. The latest four-method command therefore
has seven Full failed subtests, not an overall pass. The 14 older ownership
fixtures from §10.14 remain existing aggregate-gate debt; none was removed.

The preceding `p4_no_value_inline` typed screen is now complete: 77 valid
outputs, 16 possible-regression flags, four disturbed whole rounds discarded
and Sumfp timer-limited. Array1, Sieve, Spectralnorm, AWFY Nbody, Fibfp, Ray,
Cpstak, Binarytrees, Pidigits, Towers, Navier–Stokes, Iteration Pi Sum,
Formatted Output, Splay, Hyphen and jq Mix are flagged. These three-pair
intervals do not confirm regressions. The append-only
`typed-confirmation-selection-v5.json` has 44 rows, retaining every earlier
flag; primary/confirmation campaigns remain open.

**Recovery:** the current engine source matches `p4_closed_array_borrows`.
A fresh real-harness discovered-script census is running in private workspace
`workspace-census-closed-array-borrows` (session `37441`). Its Lambda fixture
tree is copied/frozen; the remaining linked test roots are hashed in the
7,525-file `p4-closed-array-borrows-census-inputs.json`. Use the completed
report to rank language failures; do not qualify in-process/native GTests
from this CLI-only census. Its runner is `p2_discovery_gtest.exe`, ordinary
scripts select actual MVP/JIT, and the requested worker limit is one.

The previous performance campaign completed. A new all-77 untyped then typed
screen on this exact frozen release is queued/running as session `42810`;
`--pause-for-jobs` prevents measurements during this census or the separate
checkout baseline. Do not launch another benchmark runner. Summarize its
reports when complete and append any typed flags without dropping old rows.
The language, native and aggregate gates remain open. Next continue pipes,
captured callable environments/normalization and the remaining control/task
routes. No interpreter fallback, C2MIR/Radiant campaign, commit or completed
goal is claimed.

### 10.16 Canonical sequence pipes and lexical activation contexts

MVP mapping/filter pipes now share `em_pipe_walk` with Full. It uses the
canonical `item_keys`/`iter_len`/`iter_key_at`/`iter_val_at` helpers, retains
precise source/member/key/output roots, lifts scalars and finishes through
`pipe_end`. Full's duplicated type-specific walker is removed. T0 uses the
same canonical iteration functions. This corrects the old child-only element
walk under **S10.1.2v4/S10.1.6**: attributes precede children. Map keys, text
kinds, empty/scalar/list results, nested parent/root/key bindings and ordinary
predicate Error are covered by the new fixture. C Bool truth classification
is explicitly zero-extended before exact `BOOL_TRUE` selection.

MVP still rejects aggregate pipes pending argument injection. Sequence bodies
with nonlocal exits, potentially failing local boundaries or var-place calls
also stay rejected until resource-unwind lowering owns their key lists.
Unsupported syntax remains an error, with no interpreter/Full fallback.

`p1_sequence_pipe_roots`, SHA256
`224a953758c772df7e26618d5c1f92c7590eb58a117fce39924eaff51b6d481c`,
passes all 77 untyped MVP and 77 typed goldens under both compilers. The
initial valid pipe fixture passes all nine MVP/Full/T0 × GC combinations.
Earlier probes used invalid element/statement-block syntax and are retained
as failed construction attempts, never test evidence. The final fixture uses
ordinary element content and a value-producing callback.

A separate probe then found lexical `~key` leaking into an ordinary called
function: MVP's inlined body and T0 answered `[false, false]` for a function
defined outside the pipe; Full answered `[true, true]`. Exact controls remain
in `p1-pipe-inline-context-before.json`. Per **S10.1.3**, inlining now saves,
clears and restores implicit context, while T0's function entry installs a
scoped context barrier before defaults/admission/body evaluation. Method and
constraint candidates still bind their own subjects. This changes no MIR
instruction in ordinary context-free inlining.

The current `p1_pipe_activation_context`, SHA256
`fb14dc147e7e6b4ae5f37dc4a3ee20cb60c012474c102da17b8677ca9773a56c`,
passes all 77 untyped MVP and 77 typed goldens under both compilers. All 69
MVP content/GC combinations pass; the corresponding four-method command
retains the same seven Full failed subtests. The extended pipe fixture,
including the activation-boundary counterexample, passes all nine three-tier
GC combinations (`p1-pipe-activation-context-three-tier-probe.json`). The
focused real pipe family is MVP 5/8 versus Full 8/8. Aggregate injection blocks
`pipe_spread` and `pipe_that_kind`; `typed_array_pipe` additionally exposes
numeric comprehension finalization (`shape`/`reshape`). Do not alter those
oracles or call the family complete.

### 10.17 Timing monitor correction and active census

The array-release campaign began measuring while its private census was still
running. The monitor only classified generic `.exe` programs above a 5% CPU
threshold; a GTest supervisor between worker bursts could therefore be missed.
The census and campaign have distinct process groups, so group exclusion was
not the cause. Private `*_gtest.exe` supervisors, `test-batch` children and
Python unittest/pytest suites now block timing regardless of transient CPU.
`test_idle_test_supervisors_block_benchmarking` passes with zero-CPU private
supervisors, an excluded own process group and an idle desktop process.

Only this task's timing process group 8378 was stopped. Its 11-row partial
report remains unchanged, with a hash and explicit invalidation in
`p4-closed-array-borrows-screen-invalidation.json`; session `42810` ended 143.
No foreign baseline or census was stopped. Do not resume/overwrite that
campaign or claim its rows as performance evidence. Historical diagnostic
screens also predate the monitor fix and may have missed private test bursts;
retain their flags as investigative leads, never final confidence evidence.
Clean primary/confirmation runs remain required.

**Recovery checkpoint:** current engine source matches
`p1_pipe_activation_context`; its release build, source/runner manifests,
three-tier probe, typed/GC log and all-77 untyped report are complete. No
benchmark runner is active. The older source-frozen
`p4_closed_array_borrows` discovered-script census still runs (session `37441`,
GTest PID 7022); the copied Lambda fixture tree cannot be changed by new root
fixtures. It had processed about 990 scripts at the last log check and was
working through the linked demo roots in batch 24. A busy child alone does
not establish a stuck script: the batch has 50 entries and a 60-second
per-script alarm. Preserve partial output and establish a specific failing
script/root cause before interrupting the run or changing timeout behavior.

Next finish/classify that census, implement aggregate call normalization and
resource exits, then captured callable environments and the remaining
control/task/type routes. The aggregate pipe AST is already a call with a
virtual first receiver (`pipe_inject`/`pipe_receiver`); shared
`ast_resolve_call_arguments` owns that source shape. Every incoming-domain,
range and ownership proof must consume it correctly before general user-call
injection is admitted. Reuse existing builtin and dynamic-call emitters rather
than cloning their C ABI/error/ownership protocols. Keep the 44-row typed
confirmation population and Sumfp batch requirement. Full language/native
and performance goals remain open; no commit or completion is claimed.


### 10.18 Aggregate pipes, scalar array finalization and completed census

The frozen `p4_closed_array_borrows` discovery census is complete: **713/2,077
pass; 1,364 fail**, including seven missing batch results. Exact JSON/log and
`p4-closed-array-borrows-discovered-census-summary.json` retain the population.
This is an older CLI-only candidate, not current language qualification or an
in-process/native gate. The seven missing rows need batch-root investigation;
no timeout, oracle or harness failure was suppressed. No census or benchmark
runner is active.

Per **S10.1.2v4**, aggregate pipes now lower through MVP's existing direct,
builtin and dynamic call protocols. A shared positional accessor prepends the
non-owning `pipe_receiver` without rewriting the AST. Incoming-domain widening,
integer/float constants, private arrays, borrow aliases, extents, effects,
string recipes, native numeric helpers and closed-call witnesses all consume
that position. The emitter evaluates it once; dynamic calls keep the receiver
rooted across callable evaluation. Named and spread call arguments remain
open; no new fallback was added.

`p1_aggregate_pipes`, SHA256
`75607d058124d40e93a97aa5c8a6d3445b17482d02d2ea3933c07a26f8ab22ce`,
passes the new aggregate fixture under MVP and T0 in normal/every/seeded GC.
The first new probe also pinned receiver/callee order, exposing a difference
from Full. **S7.7.1v3 explicitly leaves strict left-to-right evaluation order
non-normative**; that observation is not a proved Full defect. The new fixture
now counts producer evaluations without depending on their order. Preserve
the first source-hashed diagnostic in `p1-aggregate-pipes-three-tier-probe.json`
separately; rerun the corrected fixture before claiming its nine-way gate.
The focused real pipe family passes **8/9**, retaining the existing
`typed_array_pipe` oracle. An inadvertently broad Python discovery command was
stopped before its legacy reference checks; its partial log is unqualified.
Use explicit Lambda-only methods for this goal.

The comprehension failure comes from skipping finalization of dynamically
produced scalar elements in generic array builders. `array_end_flat` now
reuses the shared exact scalar-lane promoter while keeping nested row owners;
`array_end` retains its existing tensor promotion then calls that same flat
finalizer. MVP's generic array literals always finalize scalar storage, using
tensor folding only when the destination requires it. This keeps the physical
choice separate from language contracts (**D2.3.1–D2.3.5, D3.2.1–D3.2.2**).
The release `p1_array_scalar_finish` is building; correctness and performance
checks for that source remain open.


### 10.19 Scalar finalization validation and active ownership investigation

`p1_array_scalar_finish`, SHA256
`fecc66b6a5f65d3f72359af76b2b4710b32667a7f2e7580397681532e55def45`,
passes the real pipe family **9/9**, all **77 untyped** goldens under both
compilers and all **77 typed** goldens under both. The corrected aggregate
fixture passes all nine tier/GC combinations on its preceding release;
`p1-aggregate-pipes-evaluation-count-probe.json` records source/oracle hashes.

The four-method run has **eight failed subtests**, not a clean pass: the
seven known Full controls and a new MVP seeded `mir_mvp_query_windows`
failure. Before scalar finalization this MVP case passed. Its first saved
cold float becomes zero. Keep this regression open and keep its oracle.

The census also identified abstract numeric contracts being rejected as type
objects. Admission now uses `type_is_global_meta_type`, retaining `number`
and `integer` in the shared Item/owned-result protocol, as the canonical
router already requires (**D2.3.1–D2.3.5, S11.4.1v5**).
`p1_abstract_numbers`, SHA256
`0b8c7adcc890b4464739e13ea4d00470a6a4f31be75c37ac35c47fea6cf48fe4`,
passes its new argument/result/deferred/callable/cold-scalar fixture in all
nine tier/GC combinations. It inherits the still-open window regression.
The fixture's initial return annotations omitted possible Error; construction
was corrected to `number^`/`integer^` before the passing probe.

**Active debug source:** temporary, environment-guarded logging in
`lambda-data-runtime.cpp` and `collection_runtime.cpp`. Remove all of it
before qualification. `p1_scalar_finish_trace` used stripped release debug
macros and produced no diagnostic; `p1_scalar_finish_trace2` uses scoped
error logging instead. Its log proves the first saved value is already zero
*before* scalar promotion; the array's data pointer then moves during
allocation and its second owned scalar is rebased correctly. Therefore a
missing source-array root inside the scalar promoter is not an established
root cause. The next private release, `p1_scalar_append_trace`, instruments
the incoming scalar across append-buffer growth. `Rooted<Item>` retains a
tagged word but does not own a borrowed scalar payload; inspect this interval
before changing the precise ownership protocol. No conservative scanning,
test skip or timing claim was added.


### 10.20 Append ownership fix and stable pre-capture gate

The trace establishes the cold-float root cause. In
`p1-scalar-append-trace-mvp.log`, an incoming tagged scalar retains the same
pointer across `expand_list`, but its value changes from approximately
`3e-310` to zero. That pointer borrows the source array's movable data buffer;
`Rooted<Item>` preserves a tagged word, not its scalar payload. The shared
append growth path now calls the existing `lambda_item_adopt_scalar_home`
before allocation and retains that home through the final store. Native and
boxed growth use one helper; capacity-available appends keep their path.
All temporary trace code has been removed (**D5.3.1, D6.2.3, D6.3.1**).

`p1_append_scalar_homes`, SHA256
`b5322ed5251fea5535c006db551f5bf033c9fbc8c0065e01bd046c9f7d97b32c`,
passes **77/77 untyped and 77/77 typed in each compiler**, **75/75 MVP
content/GC combinations**, all **16 window compiler/seed controls**, and the
expanded real pipe family **12/12**. The five-method Python command retains
**six Full failures**: the three procedure-result modes and three closed-array
modes. The previously known Full seeded-window failure is fixed. Eight
preselected window seeds are now retained in a dedicated regression method;
the preceding control sweep also found older MVP failing seed 101, before
scalar finalization, so this was an exposed shared lifetime defect.

Source-matched private native security/concurrency executables were rebuilt
with strong runtime providers. The eight selected image/cache checks pass in
both normal and every-allocation GC (**8/8 each**). The full native suites and
aggregate baseline remain open. Root executables were not replaced.

### 10.21 Immutable captures and static named calls

The shared function-value helper now owns plain/closure object construction,
field counts, names, type metadata and the boxed context/companion ABI.
Full's frontend keeps its capture permission/read logic while both compilers
share the physical environment loop and owned-slot reader. Names pass the
interned `char*` expected by `to_closure_named`; the former Full branch passed
a `String*` there. MVP supplies readonly lexical snapshots, receives the
hidden environment before user operands, and gives loaded cold scalars their
own activation homes (**S9.1.4v2, D7.2.1–D7.2.3, D6.2.3**). Per-activation
bindings retain no parent MIR registers, borrowed homes or writable witnesses.
Mutable/live, imported/unindexed and recursive function captures remain
explicitly unadmitted. No fallback was added.

`p2_immutable_captures`, SHA256
`792b9e128c91e758504ff834bc90d7f45e35333a3662995af7a0066f4fd98d7f`,
passes **27/27** tier/GC combinations across the new fixture, existing
`fn_local_snapshots` and `transpile_typed_closure`. Its real closure slice is
**5/7**. The new fixture's initial reserved identifier and implicit Error
return annotations were corrected before this passing probe.

Static named calls now use the shared `ast_resolve_call_arguments` mapping in
all incoming proofs and emission, including omitted defaults, var places and
pipe receivers. The map constructor's registry names now identify its actual
fixed ABI providers (`vmap_new`/`vmap_from_array`), so MVP uses its ordinary
builtin lowering. `p2_named_capture_calls`, SHA256
`3b5485f2f85c25b02d00d3827ce5ee648a3590a00cd6d8f31575ca2e7f21b11e`,
passes the expanded real closure/named slice **10/10**. Its broader four-fixture
matrix is **28/36**, not complete: Full misorders named pipe operands (3),
T0 excludes that combination (3), and MVP loses a let-list string capture
under every/seeded GC (2). Keep each failure and oracle.

The capture GC gap is the environment's construction interval: `heap_calloc`
with tag zero does not trace initialized capture slots before a Function owns
it. Both frontends now reuse the existing `heap_calloc_closure_env` and its
`GC_TYPE_ENVIRONMENT`/paired-slot layout. The latter already traces initialized
Items independently of Function publication. `p2_traced_capture_env`, SHA256
`fe052180e40842d8c1cae1c9a7e108a2c61a0c7be672cf525b46c2e3735b9f26`,
passes the affected comprehension and capture fixture in all **18/18** tier/GC
combinations. No GC scanning model or value layout was added.

Full's dispatcher route and T0 now normalize static named aggregate arguments
against their formals while retaining the pre-evaluated receiver. Dynamic
named calls remain rejected (**S10.1.2v4, S12.3.2**). Optional holes retain the
existing missing-argument marker; variadic source counts remain visible in
Full. `p2_named_pipe_defaults` is building; its expanded default/var-pipe fixture
and complete output/GC gates are pending.

The benchmark fingerprint now includes `collection_runtime.cpp` (48 files).
New runner manifests resolve those registered paths against each frozen source
snapshot. Historical 47-file manifests remain unchanged; do not overwrite
older provenance. No current performance campaign is running, and no commit or
completion is claimed.

### 10.22 Named transport milestone and procedure admission

`p2_named_borrow_transport`, SHA256
`494c794d68d7a7ef658d88fafc78afde8227c3a9a8af2e2d6fdc6ed0f6252bee`,
closes the focused named/default/var-pipe and capture GC matrix: **36/36**
tier/GC combinations and **22/22** real pipe/closure/named fixtures. Both
compilers retain **77/77 untyped and typed** benchmark correctness. The
five-method command still has six Full control failures; it is not a clean
aggregate pass. Private native image/cache checks pass **8/8** in each GC
mode. Native security tests pass **85/96**, including all 48 interpreter
controls; 11 JIT/MVP rows fail with task-creation or builtin admission
rejections. Their oracles and compiler-selection assertions are unchanged.

A source-frozen screen for this engine is queued behind other tasks' tests;
`p2-named-borrow-transport-screen-supervisor.log` and its serial typed/untyped
reports retain the evidence. The monitor pauses for builds and test
supervisors. No performance target is closed by these correctness results.

The next slice admits registered fixed synchronous native procedures through
MVP's existing ABI/root/result emitter. Effect colour alone is not an ABI or
an ownership proof (**D7.1.2–D7.1.3**). Async procedures retain their current
admission boundary. `set` requires explicit place lowering: root owners use
the shared COW setter; projected owners detach and publish their spine before
mutating the installed private leaf (**S9.1.3v2, S9.2.2**). It invalidates any
inferred element-domain preservation proof. This source is not yet validated.

### 10.23 Fixed native procedures and VMap place ownership

`p2_native_procedures`, SHA256
`44139ba58b26cd40ad8b495dba9c9f2463df24a447f42c7a50ebe2f813439d3e`,
admits fixed synchronous native procedures and place-aware `set`. Its four
fixture matrix passes all MVP rows but exposes missing insertion capture in
Full and a stale T0 projected-set admission guard. The native security suite
improves to **86/96**; ten JIT failures remain and no oracle is weakened.

The common VMap hashmap insertion now share-marks retained child values.
Full stages the setter operands once and uses its existing insertion-capture
helper to update both runtime sharing and compiler ownership facts. T0's
scan now accepts the projected local-root shape already handled by its
place-borrow evaluator. `p2_vmap_capture`, SHA256
`a1d5664c65b30a391b05f989d84ff9e17c80c7d1aca7664da45f5088e0854794`,
passes **36/36** tier/GC combinations over the new `mir_mvp_vmap_places`,
existing VMap, projected mutator and clock-precision fixtures. The oracle
retains nested/root snapshots, cold float payloads, rejected keys and list
insertion captures (**S9.1.3v2, S9.2.2, S9.3.1**).

Audit confirms parking needs no MIR state-machine transform: a runtime
activation retains its native stack and precise roots. Fixed native
procedures therefore use the same registry-driven emitter whether or not
they may park (**S13.1.2v2, D7.5.3**). `p2_fixed_native_abi`, SHA256
`c0c899d82ca38620d3e6c9435e4cf48de596a6c36fcbda349f26615bb4d31c6a`,
passes all MVP and T0 rows in the next four-fixture matrix, including direct
and indirect parking calls; the matrix is **33/36**, because Full crashes
in all three modes of the new parking fixture. The frozen starting Full
also exits 139 on its unchanged source/oracle. The retained MIR dump shows
an escaping untyped parameter inferred as ArrayNum from a visible direct
edge; its boxed entry strips an integer tag and asserts the array witness.
Array-only inference now requires non-escaping, non-boxed callers; without
an array-specific guard/slow body, an open caller supplies no representation
proof (**D4.3.1–D4.3.3, D7.2.2**). `p2_closed_array_entry` is building to
validate that fix. Task creation/structured scopes remain unadmitted.

The native fixed-ABI security suite and full typed/untyped correctness checks
are in progress. New benchmark fingerprints include `vmap.cpp` (49 files),
resolved against each candidate's frozen 3,116-file source inventory. Existing
48-file timing provenance remains unchanged. No performance or aggregate
Lambda GTest gate is complete.

### 10.24 Native admission audit and wildcard member steps

`p2_closed_array_entry`, SHA256
`5f5e12bd1047489f422d74a7c3a1b13c21119d8fbf7a2017896f4781bbc1cb6f`,
passes **36/36** parking/map tier/GC checks and **7/7** real-harness fixtures.
Both compilers pass every typed and untyped benchmark output. The focused
five-method command passes all 84 MVP content/GC rows and retains six older
Full failures, exactly the procedure-result/closed-array controls. Native
cache checks pass **8/8 normal and forced GC**. No timing is claimed from
these correctness commands.

The fixed-ABI security run initially reports **89/96**, but the scoped
SQLite test accepted an `AST_NODE_OPEN_STAM` compiler rejection as a runtime
error. Its oracle only asserted Error, allowing a stale error payload to
hide the missing runtime execution. Inline security tests now retain the
loaded Script and require its MVP main entry, with no interpreter routing.
The source-matched rerun is **88/96**; the additional failure is truthful
coverage debt. This check changes no production permission policy and skips
neither calls nor oracles. File-load refusals retain their existing checks.

Two other failures are real wildcard-step lowering omissions. An isolated
four-entry directory yields `0/0` in MVP and `4/4` in Full for `.**#` and
iteration over `.**`. The MIR dump shows MVP calling `fn_member_by_id` for
`**`, treating the step as a named field. Both frontends now use one shared
`em_wildcard_step` for the existing `fn_step_items`/`fn_step_closure` kernels
(**S8.2.5, S2.4.6–S2.4.7**); MVP plans their collecting/reentry effects.
`p2_wildcard_steps` is building. Validate the existing wildcard/run/path
fixtures and native enumeration/budget tests before claiming this route.
Structured task scopes, resources/open and all aggregate gates remain open.

### 10.25 Wildcard and persistent-slot validation

`p2_wildcard_steps`, SHA256
`6b750641501d515f5f60e3b69011a4073a88261da5f9d54de67cfb9982a8c45b`,
passes both native enumeration/budget cases under interpreter/JIT in normal
and every-allocation GC (**4/4 each**). The ordinary wildcard family is
**3/5**. The five-fixture tier/GC matrix is **28/45**, retaining every
failure. Quoted member names crash MVP because it casts the literal as an
identifier. A list-valued map field also keeps its spread bit and is spliced
by a following member step. Both are implementation defects, not oracle
changes (**S8.2.5–S8.2.6, S2.5.6**).

Member lowering now handles quoted keys as values through `fn_member`.
Full's existing list guard/slot-image emitter is promoted to the common MIR
header; both frontends use it for persistent field/attribute slots. Its
original fast guard still calls `slot_image` only on an actual list.
`p2_member_slot_images` failed to build because its promoted guard referred
to Full's private offset constant; v2 uses `offsetof(Container, flags)` but
revealed the now-unused old constant. v3 removes that constant; failed
source snapshots and logs remain separate.

`p2_member_slot_images_v3`, SHA256
`dfc773e97b6219aef4d70a89147937abcb8e153b500efb269a12fe6cbbb52004`,
passes **9/9** real wildcard/list/navigation fixtures and **36/36** tier/GC
combinations over wildcard steps, run stepping, list landing and MVP element
construction. All **77 untyped** outputs pass in both compilers. The native
security suite, including explicit inline-script admission, passes **90/96**:
only five task-creation cases and scoped SQLite/open remain. It is not an
aggregate Lambda baseline pass.

The broader five-method check has **eight failures**: the six older Full
controls plus new MVP typed List/Havlak compile rejections. Slot imaging had
widened even non-list map domains to `any`; emission added an admission/error
edge absent from the plan. The revised proof preserves non-list domains and
plain array contracts through the imaging operation, while list-specific
predicates still require admission (**D4.3.1–D4.3.3, S2.5.6**).
`p2_slot_image_domains` is building. Do not claim typed preservation until its
complete output and ownership checks pass.

The path wildcard/query fixtures' forced-GC corruption is shared pre-existing
debt: starting Full, `p2_closed_array_entry` and `p2_wildcard_steps` all fail
both unchanged oracles in every-allocation GC. Source/oracle/binary hashes
and outputs are retained in `p2-wildcard-path-full-gc-controls.json`. These
controls establish prior failures, not a root-cause fix. Normal path fixtures
pass the real harness. No oracle, GC knob or timeout was weakened.

### 10.26 Slot-image domain proof and joined roots

`p2_slot_image_domains`, SHA256
`f98f4229c62e7b62099377a5f9fec77f8b4dd22a9764896e73698fac39cafec5`,
restores all **77 typed** outputs in each compiler and all **84 MVP
content/GC combinations**. Its four-fixture wildcard/list/element matrix is
**36/36**. The five-method command retains exactly the six older Full
controls; the typed List/Havlak admission regression is fixed. All **77 untyped** outputs also pass under each compiler; both sweeps are complete.

The new shared image helper returns a joined Item register. Code review
found that preserving an input's domain also retained its metadata without
registering this new physical owner as a root candidate; the allocating
arm's register alone does not cover later field evaluation or map allocation.
The helper now registers its joined result through the existing frontend
root hook (**D6.3.1–D6.3.3**). `p2_slot_image_roots` is building to validate
this ownership fix. Its gates remain open; no failed oracle is removed.

Task-scope implementation audit for the next slice: promote Full's indexed
launch classifier rather than copying it; task-target references must escape
and the syntactic argument array is not a direct-call edge. Reuse the task
argument normalization/launch ABI through a shared MIR helper. Follow the
shared `owns_task_scopes` fact: enter block scopes, preserve the entry's task
base, join normal block/loop exits and cancel error exits, including deferred
block failures. The common MVP epilogue is before precise root/number pops;
its stack-overflow cold exit returns directly without executing that epilogue.
Keep scoped definitions out of inlining/tail-call elision until their cleanup
edges are implemented. Preserve `start`'s capture restrictions, escape and
document ownership (**S13.1.1v3–S13.1.4, S13.3.1v2, D7.5.1–D7.5.3**).

The first joined-root snapshot, `p2_slot_image_roots` (SHA256
`6f29b3f9a2e69f0949ea6a43b62fd57d6a341e610addaabff8e8704b8eb47e91`),
built successfully. Review moved the root hook after the initial value move:
Full's eager root oracle must never read an uninitialized joined register.
`p2_slot_image_roots_v2` is building with that instruction order; qualify v2
rather than the preceding snapshot. Source-frozen timing remains at 74/77
untyped rows and is paused for ongoing builds/tests, with typed still queued.

### 10.27 Validated rooted slot-image milestone

`p2_slot_image_roots_v2`, SHA256
`cb578043050a4e1db94f5f1f818876b5771342c4fbae2808cf1e5229c5676d72`,
passes **77/77 typed and untyped** outputs in each compiler, **36/36** focused
wildcard/list/element tier/GC combinations and **11/11** real-harness
fixtures. The five-method command passes every MVP row (84 content/GC,
12 callable/GC, 16 window compiler/seed controls and 3 open-var modes); it
retains the six older Full procedure-result/closed-array failures. The two
typed admission regressions from v3 are fixed, and no golden changed.

Fresh source-matched native executables pass the eight selected cache cases
in normal and every-allocation GC. The complete security suite is **90/96**:
all 48 interpreter controls and 42/48 JIT/MVP controls pass. Five task-launch
cases and scoped SQLite/open remain explicitly failed. Forced-GC recursive
walk and entry-budget controls pass **4/4**. Root executables remain untouched.
The native build manifest retains the test/header snapshots and object owners.

The monitored untyped screen on the older frozen
`p2_named_borrow_transport` is complete: **77 valid rows, 26 lower medians,
24 one-sided screening bounds below Full**, geometric mean MVP/Full
**1.19133695598**, and **13 discarded disturbed whole rounds**. Each case has
three retained pairs and one warmup; this does not qualify performance.
`p2-named-borrow-transport-untyped-screen-summary.json` retains all 53 rows
that do not clear that interval gate. The serial typed screen is still
running; retain its flags before any new timing campaign. The current rooted
slot-image engine is not covered by those timings.

Release/source manifests, corpus v2 hashes and raw logs remain under the
artifact root. The current runner fingerprint has 49 files; historical
48-file timing provenance is unchanged. `git diff --check` and fixture/golden
pairing checks pass. No commit or completion is claimed; the goal is active.

### 10.28 Task/resource cleanup implementation refinement

The next slice should use a lexical cleanup stack in MVP rather than add
independent copies of task/open unwinding at every exit. Each record keeps
its parent, kind and task scope register; an open record closes through the
existing `fn_open_end` ABI. `MvpControl` and `MvpErrorTarget` retain their
cleanup destination. Return/error, loop transfer and deferred-block failure
emit only the records crossed by that edge, leaving compiler stack state
unchanged for other generated branches. The normal statement path closes
its own record before the existing failure join; the error path has already
unwound it and must not close it twice. An inlined activation starts a fresh
compiler cleanup chain and restores its caller's chain, so a callee error
cannot unwind the caller. Keep resource/scoped entries out of inlining until
that isolation is tested. This refines §10.26's epilogue sketch; the cold
stack-overflow return still bypasses unentered-body cleanup.

Use Full's `analysis->owns_task_scopes` syntactic fact to avoid adding runtime
scope calls to ordinary benchmark functions. Launch targets need boxed
entries, and the indexed launch classifier must be shared with Full: its
syntactic argument array is not a normal direct call. Preserve packed-array
argument normalization, escaped/document-owned tasks and frontend capture
checks. `lambda_task_scope_current` can lazily create the root task through
`lambda_current_task`; retain conservative call effects rather than assuming
it is allocation-free. Open aliases need a planned local register despite
having no ordinary declarator. Audit handler/fault cleanup before admitting
resource bodies (**S13.1.1v3–S13.1.4, S13.3.1v2, S12.4.4v4, D7.5.1–D7.5.3**).
No task/resource lowering code has been added in this milestone.

### 10.29 Completed monitored typed screen

The serial screen on frozen `p2_named_borrow_transport` finishes with all
**77 typed outputs valid**, **15 possible-regression flags**, one
resolution-limited case (Sumfp), and two discarded disturbed whole rounds.
`p2-named-borrow-transport-v2-typed-screen-summary.json` retains every flag
and its interval. The append-only confirmation selection v6 contains **49
rows**, retaining all 44 prior rows and adding Ack, Matmul, Brainfuck,
JetStream Richards and Prettier. Primary 30-pair/two-warmup and preselected
50-pair confirmation campaigns remain required; use matched batched Sumfp.
Do not treat this three-pair result as typed preservation, or apply it to
newer source. The screen supervisor exited successfully; no campaign is
running. No C2MIR or Radiant baseline was run by this goal.

### 10.30 Task launch and lexical task cleanup in progress

MVP now lowers same-context `start` through a shared MIR task-value helper
used by Full. It retains packed-argument normalization, boxed function
entries, escape/document ownership and frontend capture checks. Full's
indexed launch classifier is promoted rather than copied. A task target is
an escaping value; its syntactic vector is not a direct parameter edge
(**S13.1.1v3–S13.1.4, D7.2.2**).

MVP uses a lexical cleanup chain. Statement scopes, loop destinations and
block-failure joins record their base; success joins children, error exits
cancel then join, and loop transfers close only crossed scopes. Inlined
activations isolate the compiler chain; task-owning entries are currently
excluded from expansion and tail-call elision. Runtime scope calls are added
only for the shared `owns_task_scopes` fact, so ordinary benchmark functions
retain their path (**S13.3.1v2, D7.5.1–D7.5.3**).

`p2_task_scopes`, SHA256
`ef34e0bf48b0d94d9344381cf0cd6721768b26731b729c116522c3a20c672853`,
passes **69/72** focused tier/GC combinations: the new task-value fixture
(packed vectors, cold floats, named/dynamic targets, readonly captures and
argument snapshots), scope join/return/loop/escape, no replay and cancellation.
Only `start_wait` crashes in all three MVP modes. Its zero-argument worker
has an explicit argument array/options; the remaining ordinary-call effects
loop dereferences nonexistent formals. Launch nodes already carry their own
conservative effects, so the inner syntax is now excluded from that loop.
No timeout or oracle is changed.

The next snapshot also prepares public imported function references through
the same existing planner factory for direct calls, callable values and task
targets. Scope-enter/current result descriptors now identify their native
scope records as non-GC pointers while retaining conservative effects;
`lambda_current_task` may create a root task. New paired
`conc/start_imported` tests the direct and stored imported target forms.
`p2_task_call_edges` is building; its full gates are pending. The first
source-matched native task security suite is running on `p2_task_scopes`.
Resource/open cleanup, handler/fault exits and broader concurrency remain
open. Root binaries remain untouched and no new benchmark campaign runs.

### 10.31 Task call-edge milestone and broad native controls

`p2_task_call_edges`, SHA256
`01c9ec854024dd0762ef8efe7bccc50c1a0364d411f24c95720b745441da588d`,
passes **108/108** tier/GC combinations over twelve task/cancellation/scope
fixtures and **13/13** real-harness fixtures. Explicit empty vectors/options
and public imported targets now pass, including stored imported callable
values. The durable Python `test_scoped_tasks` retains the twelve fixtures;
its GC/compiler runner is shared with the existing content/callable methods,
with their original populations and oracles preserved.

Both compilers retain **77/77 typed and 77/77 untyped** corpus-v2 outputs.
The six-method Python command passes all MVP subtests, including the 72
compiler/GC scoped-task rows, and retains only the six older Full controls.
Source-matched native security is **95/96**: all interpreter controls and
47/48 JIT/MVP controls pass. Scoped SQLite/open is the sole remaining native
security failure. No performance measurement on this newer source is claimed.

The complete private native concurrency executable is also run under both
compiler policies. Both runs have exactly the same **45 failed tests** plus
one failed suite setup (the JSON contains 46 failure records). No MVP-only or
Full-only failure occurs. The first failure is DOM member ordinal parity;
subsequent logs show a current evaluation context left bound and refusing
fresh runtime owners. This is unresolved broad-gate evidence, not a clean
concurrency pass. Raw outputs, JSONs and paired failure sets remain in
`p2-task-call-edges-native-concurrency-control-summary.json`. Investigate the
ordinal/provider and context ownership causes before aggregate qualification;
do not weaken those assertions or count this as MVP-specific language debt.

Resource/open work can reuse the lexical cleanup chain. The open alias is
already an indexed ordinary declarator (`alias_decl`), with no source
initializer; bind the successful `fn_open_begin` result instead of evaluating
that empty initializer. Protect its produced domain from initializer-based
narrowing. Share Full's CRUD emission with frontend key/error callbacks.
Normal exits commit, error exits roll back, and a failing commit must abort
remaining outer records without closing the completed record twice. Preserve
Full's explicit Error-return rollback inside an open block, task joins before
commit and handler/fault ownership (**S12.4.4v4, S13.3.1v2, D6.3.3**).
No resource/open lowering is included yet.

Fresh-process selected cache checks on `p2_task_call_edges` also pass **8/8
normal and 8/8 forced GC**. This confirms the focused cache behavior but does
not clear the broad concurrency suite's contamination/ordinal failures.

### 10.32 Transaction scopes and shared CRUD emission

MVP now supports `open` and CRUD through the runtime's existing data model.
Full and MVP share CRUD emission with frontend key/result callbacks; no edit
ABI or interpreter fallback is added. An open alias uses its indexed ordinary
declarator but binds the successful runtime producer with an open Item domain.
The lexical cleanup chain commits clean exits, rolls back error exits and
removes a failed commit before unwinding ancestors. Return admission precedes
commit; an explicit Error return rolls back even when admitted by the return
contract (**S12.4.4v4, D6.3.3**).

The initial `p3_open_cleanup` snapshot fails compilation because a scripted
refactor expected two boxed setter guards but found three. The corrected
`p3_open_cleanup_v2`, SHA256
`7f66937790d6e04628d1cf2a3cbffd144f1cfdd5e812aa126da01afb19e8e88c`,
passes native security **96/96**, including actual MVP admission for scoped
SQLite, but only **24/27** focused tier/GC rows. The unchanged inner-block
store-failure fixture exposes procedure-wide rollback: the shared guard must
use the local block failure join, while explicit returns keep their callable
exit (**S7.7.6v2**). No oracle is changed.

`p3_open_containment`, SHA256
`b871ced3a8b6c71188a542f42a7c27a9aaa7f9bb529aa5044ed7bc4ef7dd8e98`,
fixes that boundary and passes **27/27** tier/GC rows, **3/3** real-harness
fixtures and source-matched native security **96/96**. Its durable Python
open/task/typed-benchmark command passes all three methods; MVP and Full each
retain **77/77 untyped** and **77/77 typed** outputs. Evidence is under
`p3-open-containment-*`. Runner fingerprints now include the write-set header
and implementation; prior manifests remain unchanged.

### 10.33 Procedural local-fault handlers and fresh census

On the open-containment engine, the existing local-system-fault and
fault-after-wait fixtures fail MVP in all three GC modes; Full and T0 pass.
The equality-depth fixture already passes. MVP ordinary recovery had no native
recovery frame. Promote Full's physical frame/checkpoint/landing emission to
`em_local_fault_expression`, used by both compilers. The JIT activation calls
the platform checkpoint directly; the landing restores precise watermarks,
publishes the durable fault and cancels abandoned child scopes before recovery.
MVP lexical cleanup also retires a protected frame on explicit control exits.
The recovery-frame native pointer has an explicit non-GC result descriptor;
conservative call effects remain (**S7.6.1v9, S7.6.7v5, S7.11.1v2, S7.11.4,
D7.5.3**).

`p3_local_fault_handlers`, SHA256
`838ef37f401ebc553aa2b48076da82c7201d9f5dd778a0c3ed44eaa2f901cb30`,
passes **36/36** tier/GC rows over system/equality faults, fault after parking
and handler results, plus **4/4** real-harness fixtures. The durable Python
fault/open/task/typed-benchmark command passes all four methods; both compilers
retain **77/77 typed** and **77/77 untyped** outputs. No performance result is
inferred from these correctness runs. Runner fingerprints now include the
recovery-frame sources, with historical identities unchanged.

The fresh MVP discovered-script census runs against this release and a private
fixture snapshot. `p3-local-fault-handlers-census-inputs.json` hashes **8,043**
inputs; the private Lambda tree is frozen and benchmark inputs use corpus v2.
Its log/JSON use the `p3-local-fault-handlers-discovered-census` prefix. Results
and Full controls are pending; this is not aggregate GTest qualification.

### 10.34 Private module storage and numeric literal expansion

The fresh census identifies private composite globals such as record tables
and nested arrays that have no storage descriptor. A module with public
functions can have no top-level call or `pub let`; the old `context_globals`
test therefore misses those lexical reads. Plan global storage before call
and effect analysis. Cross-function globals that cannot use the existing
immutable native-scalar or primitive literal-table paths use the same
per-evaluator slab as exported values. The literal-array proof is shared
between planning and emission rather than copied; preserve declared admission
proofs for native scalar slots (**D6.3.3, S9.1.2**).

The paired `_mir_mvp_module_values` / `mir_mvp_module_values` fixtures exercise
private record tables, nested numeric arrays and computed text. The first
probe is invalid because its helper name `number` is reserved; rename that
new helper to `nested_value`, retaining the earlier report. The corrected
fixture passes MVP in all three GC modes. Its unchanged oracle exposes a
Full container-store refusal and T0 mutation of the provider's aliased table;
these controls remain failures, not an accepted semantics difference.

`p4_module_wide_literals` also admits i64/u64 contracts through boxed runtime
arithmetic and lowers their literals through the existing number-home ABI.
Complex literals use `complex_new`. Nullable exact-width values retain Item
transport; no data model or numeric semantics changes (**D2.3.4–D2.3.5,
D6.2.3**). The initial eight-fixture matrix is **60/72**: all Full/T0 numeric
controls pass, while MVP wide arithmetic and nested linear-science operations
fail, and the module mutation controls fail as above. No failing oracle is
changed.

### 10.35 Scalar-home result liveness and handler result domains

Wide arithmetic exposes a common emitter lifetime defect. The frozen
`p4_module_wide_literals` minimal trace prints `[3, 3, 6, 0, 9]` for two
independent i64 bindings and their operations. Every manually owned scalar
home is colored at byte offset zero. Home addresses have fixups, but the
returned tagged Items do not identify their payload home to the liveness
pass. Bind materialized local-home addresses and propagate that binding through
shared float boxing and Item adoption. The payload remains live through its
last observer; no global disabling of coloring or conservative scanning is
added (**D6.2.3, D5.4.4**).

`p4_owned_home_liveness`, SHA256
`f1d4276f919d1ee3022cc9b4313a36d34efc632c32eb2d1d112507d5b3ce15dc`,
produces the correct independent values, with distinct live home offsets.
Its partial matrix has **39/45** completed rows before the driver encounters
an existing MIR-structural fixture without a text golden. The six failures
are MVP's three nested-linear-science rows and the older three Full procedure
controls. This partial driver exit is retained, not reported as a passed gate.
The existing wide-arithmetic fixture now passes all tiers/GC modes.

Plan conservative effects for procedural native fault-frame setup.
`admission_block_outcomes` additionally exposes an unrelated unnecessary
admission helper after `or^`: a consumed null/error cannot survive its
pass-through arm. Join that remaining domain with the recovery body's actual
contract, keeping fresh recovery errors and filtered misses. This removes the
unplanned collecting validation call without weakening its destination
(**S7.6.1v9, S7.6.8, S11.4.1v5**).

The current `p4_handler_domains`, SHA256
`74e409e21a13351336869c90f0f9929578af2c9c2464ccae4c3794d67ff4472d`,
passes the durable five-method numeric/fault/open/task/typed-benchmark command
(**304 script checks**) and retains **77/77 untyped** under each compiler.
Both compilers also retain **77/77 typed** outputs. The selected real fixture
family is **12/14**; only nested linear-science and constrained-handler support
remain. The handler tier/GC matrix is **42/45**, with three MVP constrained-type
refusals. The current module matrix retains **3/3 MVP passes** and **six
Full/T0 failures** described above. Logs/JSONs use `p4-handler-domains-*`.
No timing comparison or typed performance preservation is claimed.

### 10.36 Completed MVP discovered-script census; Full control pending

The frozen `p3_local_fault_handlers` census completes **950/2,086** discovered
scripts, with **1,136 failures** and zero drift across its 8,043 hashed inputs.
Its summary retains every failed test and all admission reasons. Frequent
shared causes include unsupported native-module builtin dispatch, private
module composite storage, native Boolean result joins, object/view routes,
parameter contracts and constrained types. Repeated reason counts include
transitive import diagnostics and must not be called unique-test counts.
P4 changes do not retroactively qualify this older snapshot.

The Full JIT control uses the same executable and private corpus. It is still
running in exec session **59932**, with the
`p3-local-fault-handlers-full-discovered-control` log/JSON prefix. Pair failure
sets only after it finishes, and retain timeouts/missing batch results. The
older broad native ordinal/context failures remain separate. No aggregate
Lambda baseline or performance gate is declared green.

### 10.37 Retained call-result ownership (2026-11-06)

The module-value oracle exposes a shared ownership proof defect: every call
result was classified as a fresh owner, including a returned global or
captured value. Full consequently selected a raw child-map setter for an
Input-owned static map; T0 changed the provider's retained map. This is an
implementation error under **S9.1.2**, not an oracle or language change.

Reuse the existing returned-parameter alias analysis with a retained-scope
query. A function-owned `FnAnalysis` fact propagates through later-defined and
recursive callees; globals and outer captures retain an observer. Opaque
callables have no freshness proof. Shared producer classification, Full's
store planning and T0's call/binding capture now read that fact. Argument-root
marks still follow their separate parameter-alias facts. Successful postfix
propagation preserves the source owner (**S7.6.3v3, S9.1.2, D8.2.4**).

`p4_retained_call_results`, SHA256
`d8002c59221506fd1ea79faa5f7ff9c380828e324e65a2f33d71f7003303f39d`,
passes the initial direct/forwarded/stored-callable module oracle in all nine
tier/GC combinations, plus both compilers' **77/77 typed and untyped** outputs
and the then-current six-method Python command. The broader matrix is
**42/45**, retaining only the older three Full `mir_mvp_closed_arrays` failures.

`p4_retained_propagation`, SHA256
`32bd5468826584c0dab4235b5f6749a800bc69f261f4ce3935429bfaa671e1d2`,
also covers propagation and retained closure storage. The expanded oracle has
twelve true results and passes **9/9** tier/GC combinations. The first expanded
probe/combined Python command is invalid due to a missing semicolon before a
leading lambda in the new helper; preserve those red logs. After correcting
that test syntax, the module method passes its six compiler/GC rows. The five
other methods passed in the combined run, including every typed benchmark;
both separate untyped sweeps retain **77/77**. Four selected real fixtures
pass. Source-matched native security is **96/96**, and the eight selected
cache tests pass under both normal and forced GC. Broad native concurrency
is not qualified by these focused results.

`p4-retained-propagation-module-controls-v3-dependencies.json` records the
main source/golden plus the helper source/golden and verifies every dependency
hash after the run. The temporary matrix runner now supports explicit shared
input dependencies. No old report is rewritten to claim the expanded fixture.

### 10.38 Shared range-member admission regression and new census

The completed `p3_local_fault_handlers` paired census has **950/2,086** MVP
passes and **1,693/2,086** Full passes: **757 MVP-only**, **14 Full-only** and
**379 shared** failures. Test input hashes are stable, but those manifests
cover `test/`, not all linked package sources. Repeated import diagnostics are
not unique-test counts. Many Full errors cascade from timezone package
admission, so investigate the shared compiler before assigning them to
pre-existing debt.

A controlled AST build on identical current package scripts accepts
`chart/timezone.ls` with the fresh starting release and rejects it with the
then-current release. Binary bisect accepts `p1_query_datetime` and rejects
`p1_range_frontend`. Package script hashes remain stable. The earlier
character-range fix changed the unknown member type from an unsafe `int`
guess to `any`, accidentally admitting Error into the iterator domain.
Calls consuming those members then require error handling despite the range
never producing Error members. This is a shared frontend regression introduced
by this goal, not a stale package port.

Keep construction failure separate from member admission. A successful range
contains `int | string`; use that clean union for unresolved bounds. This
preserves codepoint strings and clean call admission, while source construction
errors still propagate (**S11.1.3, S7.7.3, S7.9.3**). No package source, oracle
or diagnostic is weakened. The paired `mir_mvp_range_domains` fixture covers
numeric/character dynamic bounds consumed by another clean function and
explicit propagation of a failed range construction.

`p4_range_member_domains`, SHA256
`0f8f7ecc08e26ae67778cc9ca76c73fd5452b7b5935b0c6b33983e8f9cc1104c`,
restores timezone-package AST admission. Its range/query/module matrix passes
**27/27**, and all seven durable focused Python methods pass **322 script
checks**. MVP and Full retain **77/77 typed** and **77/77 untyped** corpus-v2
outputs. Existing constrained-handler, matrix and other language gaps remain.
No current performance qualification is inferred.

A new paired census runs this release with separate compiler workspaces and
two batch workers per policy. Its private snapshot copies the current Lambda
fixtures and `lmd`, retains corpus-v2 benchmarks, and hashes other linked test
inputs. `p4-range-member-domains-census-inputs.json` contains **8,989** files,
including **940 lmd files**. Both policies discover **2,089** scripts. Exec
sessions are **2946** (MVP) and **96780** (Full). Results are pending; do not
apply corrected-source results to the old census or declare baseline success.

### 10.39 Constrained-type implementation audit; code pending

The next shared feature slice is constrained-type membership/match/handler
patterns. **S11.4.6v4** explicitly enforces the base only at ordinary annotated
boundaries; first-class generic `fn_is` also tests the base only. Named/inline
`is` and match patterns execute every predicate layer, innermost first
(**S11.1.3, S11.2.1, S11.4.11**). Preserve this distinction.

Full's `emit_constrained_type_test` provides the shared base/predicate/truth
shape to promote with frontend predicate/truth callbacks. MVP needs its
synthetic `that` functions to use canonical boxed exports; their name means
`mvp_function_expression` currently excludes them. Bind their candidate
parameter as current item/root at entry, with null index/parent. Reuse the
existing imported-function planner factory with an explicit declaring owner
for imported predicate exports, then use the existing hidden-context/env ABI.
Do not copy Full's physical emission or introduce an interpreter fallback.
Predicate errors/falsy results fail membership; recovery filters and chained
layers use the same test. Plan their GC/reentry effects. Allow base-only
annotated constrained contracts through the existing validator.

Local constrained types may capture type bindings, which the current readonly
capture admission rejects. Audit those compile-time values in the shared
capture reader and binding setup before admitting recursive/outer-local cases.
Retain explicit refusal until their ownership/ABI is implemented; generic
runtime membership cannot substitute for executing named predicates. No
constrained-type lowering is added in this checkpoint.


### 10.40 Native constrained predicates and current release graph (2026-11-06)

**S11.2.1/S11.4.11** require named/inline constrained membership and match
patterns to admit the base, then execute each predicate layer innermost first
in its declaring scope. A falsy or Error answer fails the test.
**S11.4.6v4** keeps ordinary annotations and first-class generic membership
base-only. No semantic ruling changes here.

Promoted Full's physical base/predicate/truth loop and imported boxed-entry
trampoline into `mir_function_value.hpp`; both MIR implementations call these
helpers. MVP plans synthetic `that` functions as canonical boxed exports,
uses the existing imported-function factory with an explicit declaring owner,
and records implicit predicate GC/reentry edges. At entry, the candidate is
current item/root and parent/key are null. Immutable captured type bindings
remain type values and occupy their original environment slots without being
mistaken for mutable lexical homes. Local calls discharge the result companion
before truth classification. There is no new interpreter fallback.

HEAD advanced to `b8e29b077` while the goal was inactive. The old private
release graph omitted three newly merged units (`io_rdb.cpp`,
`io_rdb_value.cpp`, `rdb_spool.c`), and its source inventory omitted macro
files such as `dom_api.def`. The first private compile therefore linked with
undefined RDB providers. It is retained as **compilation-only**, never as a
successful tested binary. Extended the private graph to match the current
`lambda-rt`/`lambda-lib` source patterns and froze omitted tracked source,
header and macro inputs. The qualified graph has 769 units and 3,543 input
files; the source inventory check reports zero drift. Build root binaries and
vendor sources remain untouched.

Evidence uses `p5-constrained-current-*` for the initial predicate-only release
and `p5-constrained-objects-*` for the final slice. The initial MVP matrix is
15/21: five predicate fixtures pass all three GC modes; two are refused at
nominal declarations. The final release fixes those declarations/literals:
all seven constrained fixtures pass all three tiers and GC modes (**63/63**).
The seven fixtures cover inline/named/aliased tests, recursion to depth 2,000,
outer locals and nested closures, imported private helpers/module bindings,
Error answers, annotation bases, and filtered functional/procedural handlers.
`test_constrained_type_predicates` keeps these cases in the durable Python gate.

### 10.41 Checked nominal literal values (2026-11-06)

MVP now materializes immutable nominal type descriptors and constructs nominal
maps/elements through the existing `object_from_fields` runtime boundary.
That boundary resolves optional presence and admits all fields before exposing
the packed layout (**S16.9.5/S11.4.10**). Declared defaults are evaluated only
for absent keys. Written and spread fields use the existing keyed builder and
persistent slot images; content uses `object_content_fill`, preserving element
normalization (**S2.1.3/D3.3.1**). A shared literal-staging helper replaces the
repeated map/element staging setup before adding the object case.

`mir_mvp_nominal_values.ls/.txt` pins spread independence, default values,
optional absence, nominal/structural identity and normalized content. Together
with `object_default`, `object_constraint` and `object_field_admission`, all
three tiers/GC modes pass **36/36**. Object-level constraint behavior remains
Full's existing base-only behavior; method-bearing types are explicitly
refused until their registration/receiver ABI is implemented. The unchanged
`object_boxed_field_layout` fixture exposes this remaining refusal in three
MVP rows; its six Full/T0 controls pass. Do not report that 81-row diagnostic
matrix as all green.

Final release SHA256:
`032973621496469fb4343e7e78d82a9abd6777a8a822d9260e6930ab2a8c7555`.
Nine focused Python methods pass **388 script invocations**, including 77/77
typed goldens under each MIR policy. Separate frozen-v2 untyped runs retain
**77/77** under each policy. The real private discovered-script harness passes
**11/11** selected tests with `LAMBDA_EXEC_BACKEND=jit` and
`LAMBDA_TEST_MIR_COMPILER=mvp`. A negative-test filter matches zero discoveries;
that command is retained as zero executed tests, not negative-test coverage.
No benchmark timing or aggregate Lambda baseline result is claimed.

Source-matched private native executables also pass security **98/98** and
the selected cache lane **8/8 normal + 8/8 forced GC**. The two view cache
cases remain outside this focused lane, as before; the full native concurrency
failure set remains open. Native logs, JSON and executable/object hashes are
recorded under `p5-constrained-objects-native-*` and
`p5_constrained_objects-native-tests-admission/manifest.json`.

### 10.42 Range census completion and merge-input limitations (2026-11-06)

The older range-repaired Full census completes **1,731/2,089**, with 358
failures. Its MVP pair records 1,048 subprocess completions and then stalls;
a process check finds the harness alone, with no child processes. Terminated
only that task's stalled harness (PID 23336), retaining its partial log and
exit 143. No complete MVP count or paired failure partition exists for it.

The 8,989-input drift check detects **ten** changed linked demo/UI inputs
following the merge; the copied Lambda/lmd fixtures remain frozen. Those
linked-input changes and the older executable mean this census does not
qualify the current source tree. Do not attribute all shared failures to
pre-existing debt or combine it with a later result as one experiment.
The previous shared range-member regression is repaired as recorded in §10.38;
this new census evidence does not reverse that focused source-matched control.


### 10.43 Native nominal method entries and receiver snapshots (2026-11-06)

**S12.3.3v2/S12.3.4v2** define bound fn methods and dynamic invocation;
**S7.4.6v3/S12.1.2v3** govern early completion of either callable colour.
MVP now identifies a method's declaring nominal type from the immutable index,
exports its canonical boxed entry, and receives the receiver through the same
hidden argument as Full. Receiver fields become activation-local bindings;
current item/root are self, while parent/key are null. Procedure completion
publishes only potentially written fields at the common return point.
Optional presence survives untaken writes and explicit null assignments
(**S16.9.5**). Declaring scopes, field contracts and nominal identity remain
shared; no runtime data layout changes or new interpreter fallback are added.

Promoted Full's physical field load/presence test, field publication and method
registration into `mir_function_value.hpp`. The two frontends retain their
binding/representation choices but share these physical operations. MVP
prepares a procedure receiver before creating its mutating bound closure and
publishes the replacement to the caller. The initial method screen passes
15/33; remaining typed string-field loads were described as unrestricted
`any`, so the converter could not select a pointer lane. Restoring the
receiver's admitted field contract fixes that decoding root cause.

The new `mir_mvp_bound_methods.ls/.txt` pins a saved fn receiver before a later
pn mutation. The valid fixture initially returns `[false, false, true]` on all
three tiers. A separate control with the preceding `p5_constrained_objects`
release reproduces **0/6** under Full/T0, proving the shared failure predates
this method implementation. The first authoring attempt omitted mandatory
acknowledgement and was rejected E228; that invalid attempt is retained
separately, not counted as runtime evidence.

Root cause: the shared method binder retained self without recording a value
capture. It now roots self precisely and captures fn receivers by value;
procedure receivers preserve the caller's detached writable owner
(**S12.3.3v2/S9.3.1**). This fixes MVP/T0, but Full still skipped preparation
when its static `cow_marked` fact was absent. Runtime method binding can share
a receiver without changing that compiler fact. Full now consults the runtime
COW preparation path for every procedure-method receiver. All nine snapshot
rows subsequently pass. No conservative stack scanning is introduced.

### 10.44 Method defaults, raw spreads and nominal element writes

The 27-row expanded MVP screen passes 24/27; only named-method optional holes
remain. The frontend had already resolved/reordered these calls. Their omitted
argument nodes were lowered as explicit null, suppressing defaults or failing
a typed parameter. Both direct and dynamic MVP serialization now preserve
`ITEM_MISSING_ARGUMENT`; explicit null remains a supplied value
(**S12.3.1/S12.3.4v2**). The five-fixture tier/GC control then passes **45/45**.

A broader 22-fixture matrix reaches **192/198**. `object_update` crashes in MVP
while compiling a method's `<Vec *:~, ...>` body. LLDB and the AST dump show
that nominal updates store their spread as a raw expression, including
`AST_NODE_CURRENT_ITEM`. Shared literal staging incorrectly casts every item
to `AstNamedNode` before reading its key/flags. It now checks for
`AST_NODE_KEY_EXPR` before accessing that payload; raw expressions use the
existing spread lane. This also removes an accidental dependence on the
payload bytes of identifier spread nodes.

`proc/object_open_instance` exposes the other refusal: MVP admitted array/map
member-write carriers but omitted the element structural kind. Element-shaped
nominal instances use the same attribute-write boundary, so admission now
includes that actual carrier (**S2.1.4**). The existing builder preserves the
nominal record and content across shape growth. Both repairs retain the
original fixtures and goldens; the complete current matrix is **198/198**.

### 10.45 Current method milestone evidence and remaining work

Current release `p5_method_update_places-release.exe`, SHA256
`2fafec587161419615061f48e33da47f6e7bee7906e87e6166bc5e02cbc87fec`, is a private
O3 ThinLTO native build on HEAD `b8e29b077` plus the recorded uncommitted method,
constraint and nominal work. Artifacts use `p5-method-update-places-*`.
The 769-unit graph/3,543-file inventory from §10.40 remains; the native cache
fixture is independently frozen by its native-build overlay and manifest.

- Current tier/GC method matrix **198/198**, with the imported helper hashed.
- Ten durable Python methods **514 script invocations**, including 77/77 typed
  goldens under each compiler; `test_nominal_methods` adds 21 method fixtures.
- Separate corpus-v2 untyped checks **77/77** under each compiler.
- Real discovered-script harness **22/22** with MVP/JIT selected.
- Current private native security **98/98**; selected cache **8/8 normal plus
  8/8 forced GC**. The compiler-isolation test now checks both a plain callable
  and a nominal receiver snapshot across MVP/Full/MVP fresh runtimes, retaining
  the original cache-owner assertions (**D8.5.1**). Its local runtime owner
  guarantees teardown even on a fatal assertion. The final formatted fixture
  snapshot also passes **8/8 normal + 8/8 forced GC**; both manifest identities
  are retained, with `p5_method_update_places-native-tests-methods-final/manifest.json`
  matching the current fixture source. The CLI source inventory differs only
  at this independently rebuilt native fixture; runtime/compiler source drift
  is zero. Combined evidence is indexed by
  `p5-method-update-places-validation-summary.json`.

This does not close all method combinations, the complete language/GTest gate
or performance acceptance. Lexical/live captures, explicit receiver places and
var-argument dispatch still need their remaining acceptance cases. Views,
foreign/native builtins, dependent/variadic definitions, result-domain joins,
linear-science row carriers and the recorded shared ownership/concurrency
faults remain open. No Radiant/C2MIR campaign or current timing qualification
is claimed.


### 10.46 Descriptor-backed native import metadata (2026-11-06)

The older census records 1,098 repeated builtin-admission messages. A current
DOM failure identifies the root: `import_resolver` resolves descriptor-backed
system functions through `find_dynamic_sys_func_import`, while
`jit_import_get_metadata` only consults the static import table. MVP admission
requires that metadata and therefore refuses valid fixed-arity native module
calls such as `dom.next_sibling`.

The shared metadata lookup now recognizes that same descriptor-backed symbol
set. It declares the actual public Item result (**D7.6.1**) and a dynamic scalar
result class, while retaining conservative GC, reentry and exception effects
(**D7.1.3**). Unknown symbols still fail lookup. MVP's existing registry-based
fixed-arity lowering consumes this shared repair; no backend allowlist or
interpreter fallback is introduced. Runtime type/data layouts are unchanged.

`p6_native_module_metadata` builds only `mir.c` relative to the preceding engine.
Its first 11-fixture DOM matrix is **96/99**; only `dom_edit_plan` remains,
exposing an independent scalar-join lowering failure. The dedicated new
`mir_mvp_module_builtins` fixture verifies direct published native signatures
with one through five arguments, including document selection boundaries and
mutation. The initial authoring attempt used first-class builtin references;
all tiers reject those at the existing E229 Core dynamic-ABI boundary. That
unsupported reference route is unchanged; the valid fixture exercises the
published direct-call ABI. Keep that initial diagnostic separate from the
later direct-call result.

### 10.47 Scalar conditional outcomes and wider package controls

MVP already widened pointer `if` joins when an emitted branch had a boxed
outcome, but retained inferred native scalar joins. `dom_insert_html` exposes
the Boolean case: the source infers bool, while the actual C builtin producer
publishes an Item. `_is_clip_def` in the PDF package has the same problem.

Generalized that existing emitted-carrier join decision to all scalar lanes
(**D2.3.4/D2.3.5**). A boxed outcome remains Item until its authorized enclosing
boundary; a join originally allocated as floating-point receives a separate
I64 result register. This preserves Error alternatives instead of decoding
them as native values (**S7.9.3**). Neither source contracts nor builtin return
contracts are strengthened by this change.

`mir_mvp_scalar_joins` pins Boolean, integer, float and Error-valued selection.
After correcting a missing statement separator in the new fixture, the old
metadata-only engine refuses it **0/3**, reproducing the Boolean conversion
failure. The repaired engine and Full/T0 pass both new fixtures **18/18**.
The DOM command registry then passes all three tiers and GC modes. A broader
four-fixture package diagnostic is **33/36**: PDF image/interpreter, citeproc
data and MathML pass, while `test_latex_citeproc_errors` reaches a new MVP
runtime failure, `expected array, got array`, in all three GC modes. Full/T0
retain its unchanged golden. This is open correctness work, not a passing
package aggregate.

Current private release `p6_native_calls_joins-release.exe`, SHA256
`253938ce884899e7b0af13bbbb5607f4d38787508c2ce50d1dd666ee818d4826`, uses the
existing 769-unit release graph and 3,543-file source inventory on HEAD
`b8e29b077` plus recorded uncommitted changes. Runtime/compiler drift is zero;
the latest native test fixture has its own snapshot below. Package inputs
(943 files) have zero observed drift.

- New native-call/conditional family: **117/117** tier/GC checks.
- Wider package diagnostic: **33/36**; combined diagnostic **150/153**.
- Eleven Python methods: **592 script invocations**, including 77/77 typed
  goldens under each MIR compiler. Separate untyped v2 runs: **77/77** each.
- Real harness: 16 selected scripts pass; the separately selected citeproc
  error fixture fails. Retain both JSON files as **16/17**, not an all-green gate.

No aggregate Lambda baseline or current performance qualification is claimed.
A short monitored typed comparison with `p5_method_update_places` completes
with all 77 outputs retained, ten slower flags and twelve faster flags. Of
76 usable ratios, 39 medians are higher and 37 lower; their geometric mean is
0.99763. Sumfp's near-zero samples cannot produce a meaningful ratio. The
`p6-native-calls-joins-typed-regression-screen*.json` artifacts retain the raw
samples and flags. These self-reported workload times use three measured
pairs and one warmup, without Full measurements; they cannot replace the
primary/confirmation campaigns or clear the existing v6 confirmation set.

### 10.48 Effective native VFS audit and corrected cache evidence

The first native metadata unit reports the old `EXPECT_EQ(reentry, YES)` even
though the current source checks that reentry is not `NO`. The private native
builder appended a fixture mapping after the CLI source overlay's mapping of
the same path. Clang selected the first mapping, so copying/hashing the new
fixture did not prove the compiler read it. This also invalidates §10.45's
claim that the extended nominal-method cache assertions had executed; the
base eight tests passed, but the added case was shadowed. Preserve the earlier
reports and their limitation instead of applying later results retroactively.

The private builder now removes the old mapping before adding the native
snapshot, asserts unique virtual paths and retains virtual source names so
relative includes resolve through the intended overlay. The final audit finds
exactly one mapping to the current native snapshot. Both native executables
are rebuilt under `p6_native_calls_joins-native-tests-native-effective/`.
Current effective builds pass **98/98 security**, and **9/9 selected cache and
metadata tests normally plus 9/9 under forced GC**. The metadata unit verifies
boxed results, dynamic scalar classification and conservative effects for
three descriptor entries, plus rejection of an unknown symbol. The expanded
MVP/Full/MVP cache test now genuinely exercises nominal method snapshots across
fresh runtimes. Two view cache cases remain outside this selected lane.

Authoritative native reports use `p6-native-calls-joins-native-effective-*`;
the corresponding `manifest.json` and unique effective `overlay.json` identify
the compiled test sources and runtime objects. Earlier `native-imports`
reports are superseded for newer fixture assertions.

### 10.49 Inferred record fields are not constructor contracts (2026-11-06)

The citeproc failure from §10.47 reproduces during `style` import alone.
Its `ENUMS`/`ATTRIBUTES` initializers fail; standalone arrays pass. The minimal
control `{names: ["a", "b"]}` fails only MVP, while a computed-key map and
Full/T0 pass. The homogeneous inferred array retains its first literal's
element type. `binding_value` supplies the inferred map shape as a destination,
and `map_literal` mistakenly admits every field against that observation.

Only trusted declared destinations now select constructor admission. Inferred
descriptors still select storage; they add no runtime obligation
(**D4.3.1–D4.3.3**). The new `mir_mvp_inferred_record_fields` fixture includes
string, int, bool, float, mixed and nested arrays, inferred function results,
and an explicit array contract that must still reject invalid input. Old p6
fails the fixture in all three GC modes; Full/T0 retain its golden.

Private p7 release SHA256
`5bfa508c0656caa6143bb33e2a63f7263857edcda686c25d9f1b6e1167132ba2`
passes **36/36** focused tier/GC checks, **616** Python script checks and all
77 typed/untyped outputs under each MIR compiler. Citeproc's unchanged error
oracle now passes. Evidence: `p7-inferred-record-fields-*`.

### 10.50 Named and positional decomposition

MVP now emits comma decomposition (**S16.8.7**), using the canonical target
entries from the shared AST. The RHS runs once; each target captures its
projected child and owns scalar bytes across later projections (**S9.1.2,
D6.2.3**). Extracted the projection call into `em_decompose_item`, used by
both MIR emitters. Targets retain Item transport without an inferred source
contract. Globals use the existing module storage path; local targets receive
their declaring activation (**D8.2.4**). Synthetic targets cannot supply an
initializer-based contextual constructor proof.

Initial lowering passes **27/27** tier/GC checks. Expanded coverage adds an
unread target, exposing a shared-index omission: synthetic targets acquired
binding IDs only through identifier uses or assignments. The index now
publishes all decomposition entries at the declaration. Binding publication
can grow storage independently of the structural node count. A native unit
publishes 320 targets across two nodes and checks identity reuse. Its effective
native build passes with the current test snapshot.

Current `p8_decomposition_indexed` release SHA256
`2c3cb863e51397e414361c7a468a1fe51605c6800f4c46594f1c2ad1da61011a`
passes **54/54** focused tier/GC checks and **21/21 real GTests under each MIR
compiler**. Cases cover lexical and global bindings, missing members,
shadowing, COW projections, rebindings, wide scalars and a procedural RHS
evaluated once. The pre-index expanded candidate retains its **51/54** failure
report; it does not qualify the repaired release. Mutable/live capture
decomposition still depends on the unimplemented shared-home path.

Final focused validation retains **77/77 typed and 77/77 untyped** v2 outputs
under each MIR compiler. Thirteen Python methods pass **640 script checks**.
Current effective native index/cache/metadata tests pass **10/10 normally and
10/10 with forced GC/poison**, and the security suite passes **98/98 with MVP
explicitly selected**. A first security run omitted the selector and used
Full; it also finishes **98/98** before a proposed stop, so no process is
terminated. Keep its report as a Full control, not MVP evidence.

The validation summary records binary/source/runner hashes, package inputs,
fixture hashes and unique effective native VFS mappings. Runtime/compiler
drift is zero; the native index unit is newer than the CLI inventory and is
verified through its separate snapshot. No current performance acceptance
or aggregate Lambda baseline is claimed.

### 10.51 Broader MVP fixture census and retained failures

Rebuilt the real discovery harness from p7's frozen current source, using the
unique effective VFS mappings from §10.48. A selection covering all 87 then
present MVP fixtures plus five matching controls runs **92** scripts. MVP
passes **71**, Full **63**; **16** failures are shared, five are MVP-only and
thirteen Full-only. These are diagnostic results, not baseline qualification.
The shared set includes source-contract failures and existing output
differences; no scripts, goldens or failure accounting are changed.

The MVP-only set contains `copy_loops`, `iterations`, `kernel_loops`,
`inferred_record_fields` and `scalar_var_lanes`. Three time out. The other
two finish their script but are charged abnormal process exits by the
harness's existing teardown check. The new record fixture passes a separate
one-case real harness run (**1/1**); this does not clear its batch failure.
Keep the root cause open. The subsequent p8 21-case positive selection passes
both policies, but does not replace this broader failing selection.

Reports: `p7-inferred-record-fields-real-{mvp,full}-gtest.json`, their summary,
and `p7-inferred-record-fields-isolated-gtest.json`. Existing MVP fixture and
recorded package input hashes remain unchanged during the run; the census is
still narrower than the aggregate Lambda gate.

### 10.52 Store containment follows the receiver binding (2026-11-06)

All three MVP-only loop timeouts reproduce with p8 in short, isolated runs.
PTY capture shows the last completed operation: the next call attempts a
rejected array store. MVP uses the current statement scope, abandoning a
loop body's tail and its counter update, then repeats the same iteration.
Full selects the receiver binding's declaring region (**S7.7.6v2**).

Array, member and projected-place stores now select that binding's scope;
parameter/module roots retain callable containment. Inlined calls retain
their own completion boundary. The same error branch emits the existing
retained/partial-state diagnostic with the receiver name and skipped tail.
This changes neither whole-binding continuation (**S7.7.4v2/S7.7.5v3**) nor
the destination's store admission contract.

`mir_mvp_store_containment` covers parameter arrays, outer and inner block
locals, fields, nested paths, partial writes and COW snapshots. Full/T0 pass
its initial control **6/6**. Private p9 SHA256
`961fd0356f6db04b343710da13dd1c2a2c0261bd86fdcf4e057ed54b0a1ace72`
passes the new fixture, all three previously timed-out fixtures and
`ledger_containment`: **45/45** tier/GC checks. Fourteen Python methods pass
**670** script checks; all 77 typed/untyped v2 outputs remain valid for both
MIR compilers. A first private build catches an omitted source argument at
one projected-var publication call; the corrected p9 build is authoritative.

The broader real harness is **80/95 MVP versus 66/95 Full**. All fifteen
remaining MVP failures also fail Full; no MVP-only failure remains in this
selection. The earlier two abnormal batch exits no longer reproduce here;
do not infer a separate lifecycle root cause from that observation. Existing
fixture/package hashes remain unchanged. Reports use `p9-store-boundaries-*`.

### 10.53 Confined nested-procedure capture homes

MVP now consumes the shared confinement facts for parent-fn locals
(**S9.1.4v2, D7.2.3**). Each executing declaration, including decomposition,
creates the runtime's existing owned-slot home. Confined pn environments
capture that home; pure functions and escaping closures copy the current
value. Snapshot capture rules and the pn-from-fn prohibition remain enforced
by the shared builder. Legacy pn-parent mutable snapshot write-back stays
open and is still explicitly refused.

Shared bindings use Item transport while their original annotation continues
to own admission. Shared-home reads/allocations enter the GC effect plan.
Publication updates the home; post-call reloads use raw loads without
overwriting a pending scalar companion. Cached layout/ownership witnesses
are invalidated on reentry. Active homes follow the lexical scope stack,
so callbacks after a branch cannot read an unexecuted or exited local.
Home-owning functions retain an activation boundary for precise ownership;
the existing native-call route handles them. Shared home construction and
noncalling reload emission are extracted for both MIR frontends.

Six existing fixtures fail the old p9 MVP **0/18**. Current p10 passes those
plus `mir_mvp_confined_capture_homes`: **63/63** across three tiers and three
GC modes. This includes scalar tails, COW children, decomposed targets, fresh
loop homes, creation-time snapshots, typed numeric admission and branch exit.
The first version of the new fixture omitted an explicit `any` return on a
snapshot of a deferred float outcome; all tiers correctly reject it with
E208. The source now admits that outcome explicitly; its golden is unchanged.

Private p10 SHA256
`bb80290be0e68a7ed7c1567f30396be1361221c8b33a93e667e2f21be02e8b3b`
passes **712 script checks** in fifteen Python methods. The expanded real
harness is **87/102 MVP versus 73/102 Full**, retaining the same fifteen
shared failures. The native compiler-isolation test adds a shared-home and
pure-snapshot image across MVP/Full/MVP runtimes; final effective native
results are recorded below. No aggregate Lambda or current performance
acceptance is claimed.

### 10.54 Nullable capture outcomes and final checkpoint

Extended the new fixture with retained validation errors and nullable int,
float and bool snapshots. After replacing two reserved type words used as
local names, MVP and T0 agree; Full captures nullable bool null as integer
`2`. The old p9 Full reproduces the same value in all GC modes, proving it
precedes the shared-emission refactor.

Full had already decoded the Boolean initializer into the 0/1/2 lane but
rebuilt its producer descriptor from the original AST expression. Boxing
that value into a shared home loses the nullable bool contract. The producer
now retains the admitted physical lane and its nullable contract
(**D2.3.4–D2.3.5**), preserving null through the existing boxing operation.
No runtime value layout or source contract changes.

Final private release `p10_capture_outcomes`, SHA256
`b6bdd2d85bc018b0bf4e2da2a355e7c8b42e27088f5eec5dc6cbc5a9b7573051`,
passes **108/108** focused tier/GC checks and **712** script checks across
fifteen Python methods. Both MIR policies retain **77/77 typed and 77/77
untyped** benchmark outputs. Effective native index/cache/metadata tests
pass **10/10 normally + 10/10 forced GC/poison**, including the new
MVP/Full/MVP shared-home image case; security passes **98/98 with MVP
explicitly selected** and its interpreter controls retained.

The broader real GTest selection remains **87/102 MVP versus 73/102 Full**.
All fifteen MVP failures also fail Full. Retain that failing selection;
these results do not qualify the aggregate Lambda gate. Current source,
fixture and package drift is zero. The final validation summary records
source/runner hashes and verifies the native overlay's unique effective
test mappings. Current performance qualification and the full goal stay open.

### 10.55 Variadic definitions, positional slots and call completion

MVP now admits variadic functions and procedures through the shared boxed
entry protocol (**D7.2.2, D7.4.1**). Fixed parameters retain their signature
positions and `Function.arity`; a trailing Item carries the rest array.
Direct calls package extras after fixed-argument admission. Dynamic calls
use the existing signature adapter, and imported definitions and bound
methods retain their declaring context. Rest definitions remain outside
MVP's current inline and dense-call expansions, whose state does not model
the receiving rest activation.

`em_argument_array` owns physical argument construction for Full rest calls,
MVP direct rest calls and MVP dynamic calls. The verbatim append preserves
nulls, adjacent text and each list's array image (**D3.3.1, S2.5.6**). Named
resolution now counts supplied extras as well as fixed slots, while formal
proofs and var-place checks stop at the fixed parameter list.

Both MIR policies keep previous/current rest owners in fixed precise frame
slots; ambient `EvalContext.current_vargs` is not a GC root (**D6.1.1,
D6.3.1**). Their common completion restores the previous context, including
parameter rejection and explicit return. Full's wrapper installs the
receiving context before defaults; its existing default-activation scan now
recognizes `varg()`/`varg(n)`. Noncollecting bodies still reserve and initialize
their owner slots. An intermediate register-root implementation failed
`func_param` because the root finalizer removed its root frame; that candidate
is superseded by fixed ownership, rather than an artificial liveness read.

The existing Full/T0 self-tail transform rebound only fixed parameters and
discarded rest operand evaluation. It now builds and publishes the recursive
call's new rest array before rebinding fixed slots (**S16.9.7**). The two
tracked `interp_variadic*.txt` tail results change from `[2, 7]` to `[1, 1]`.
`p11-variadic-oracle-repairs.json` retains old/new text and hashes. These are
core oracle corrections; the frozen v2 benchmark corpus does not change.
The new admission case also exposed Full's unchecked declared tail operand
and T0's premature rest evaluation. Full checks an unproved non-error
declared operand; T0 evaluates extras after fixed admission (**S7.7.3**).

Five new executable fixtures and one companion module cover positional
null/text/list values, wide scalar ownership, defaults, nested restores,
closure values, imports, bound methods, self-recursive effects, rejection
order and var rebinds. Every new script has its paired golden. The native
cache-isolation case adds a variadic/default/tail image across fresh
MVP/Full/MVP runtimes. The new Python methods retain the Full var-rebind
failure as a recorded control: MVP passes `mir_mvp_variadic_exits`, Full
returns the old caller value, and forced T0 excludes that var/rest call.
The oracle and failing controls remain visible.

Private release `p11_variadic_admission` (O3 ThinLTO, no DEBUG), SHA256
`8b0e4ae983c7ee759b8c2a46d9f7d90e205081cb687eaefda0c5baeaf1a828b9`,
passes **78/78 MVP** tier/GC checks across 26 fixtures. Full and T0 each pass
75/78; the six controls above are the only failures in the 234-row matrix.
AUTO handoff passes **9/9**. Seventeen Python methods pass **793** script
checks, including **77/77 typed** goldens under each compiler; canonical
untyped outputs also pass **77/77 each**. Native cache/index/metadata passes
**10/10 normal + 10/10 forced GC/poison**; security passes **98/98** with MVP
selected. The real harness expands from 102 to 117 cases: **102/117 MVP,
87/117 Full**, with the same fifteen shared MVP failures and no newly failing
row on either compiler's original population. Source, recorded-input and
benchmark-corpus drift is zero; native effective overlays are verified.

Evidence: `p11-variadic-final-validation-summary.json`, its tier/GC, AUTO,
Python, native and real-harness reports, and the frozen release manifests.
These are focused correctness results. Language/GTest parity, dependent
definitions and current performance gates remain open.


### 10.56 Boxed dependent definitions and first-class binder values (2026-11-06)

MVP now emits dependent definitions through a boxed entry rather than rejecting
all signatures containing binders. The activation allocates the selection
slots in the existing fixed precise-root suffix. Every site admits its value
before publishing a selection, and dependent reference parameters are checked
after the sites have joined. Defaults, body boundaries and declared dependent
returns use that activation's environment (**S11.4.8v2, D4.1.4**). Dependent
annotations remain receiving contracts; value observers retain an open Item
representation until an independent proof permits narrowing (**D2.3.1,
D4.3.3**). No additional interpreter delegation was introduced.

The written dependent return contract must survive success-domain planning.
Removing its error alternative had also replaced a `TypeBoundRef` with its
copied `type` bound, causing a returned number to be checked as a type value.
MVP now checks the written contract in the callee and publishes an ordinary
boxed result to the caller. Binder names in closure environments retain the
selected first-class type value, rather than reading an unrelated child's
slot. Empty nested sites read their declared bound, as required by
**S11.4.8v2**; Full and T0 share that fallback.

Type-expression classification is shared with the AST builder. A type-valued
expression such as `int` cannot take its represented type's integer carrier:
that had incorrectly packed `[int, int, 40]` as a numeric array. Forwarding a
runtime type value now retains deferred admission instead of requiring literal
syntax. Static binder collection uses a conservative approximation for a
runtime type value or nested site and refuses to use that approximation as an
exact specialization key. Nested array arguments retain their whole written
container contract; the site's scalar bound is not the argument's contract
(**S11.4.8v2, D2.3.1, D4.1.4**). A negative fixture still rejects an integer
passed to `T: type` with E207.

An admission result needs more information than the Item's error tag.
`any` and `error` may admit Error as data (**S11.4.3**), while an incompatible
repeated binder selection must still fail. The shared admission kernel now
optionally reports a separate Boolean status through
`lambda_type_check_env_status`, without repeating the check or changing Item
layout. An unchanged Error fast return reports whether it was admitted; a
binder site still performs the walk that publishes its selection. MVP reuses
one native status cell per activation, allocated at entry so loop checks do
not grow the native stack. Existing boundary APIs retain their signatures
and rich failure diagnostics. The native regression uses the canonical
`alloc_type_kind` creation path, which publishes the process's binder-presence
flag before a binder-bearing contract can be observed.

New paired fixtures cover dependent defaults and imported/dynamic calls,
forwarded type values, wrong dependent returns, deferred local reassignment,
empty nested sites, repeated-site joins, captured type names, escaping
first-class type snapshots, wide scalar lifetime and Error-as-data admission.
The helper module and E207 negative fixture also have paired goldens.
`NegativeScriptTest.TypeBinderDiagnosticsRemainSpecific` includes the new
rejection on interp/JIT/AUTO; a native admission test distinguishes accepted
Error data from failure and checks that a rejected join preserves its prior
selection. The cache isolation test alternates MVP/Full/MVP across fresh
runtimes and verifies independent invocation selections. Its possible error
outcomes are explicitly propagated under **S7.5–S7.6**.

Qualification uses `p12_binders_qualified-release.exe` (release O3 ThinLTO,
769 units), SHA256
`47e6865187f0d4f43674ec5a4d7b26dd3da0761881cf39084e1cafede21deade`:

- Tier/GC matrix: **111/111 MVP**, **102/111 Full**, **102/111 T0** across
  37 fixtures. AUTO checks pass **12/12**. A later captured metadata import
  is separately rechecked **9/9** with source/golden dependency hashes.
- Twenty Python methods pass **853 script invocations**, including all
  77 typed goldens under both compilers. All 77 untyped v2 goldens also pass
  each compiler. These are output checks, not speed measurements.
- Native cache/index/import metadata pass **10/10 normal and 10/10 forced
  GC**. Admission/diagnostic checks pass **4/4 normal and 4/4 forced GC**.
  Security passes **98/98 with MVP selected** and interpreter controls retained.
- The real harness passes **114/129 MVP and 97/129 Full**. All twelve new
  rows pass MVP; no prior passing row regresses on the common 117-case
  population under either policy. The same fifteen MVP failures also fail
  Full; none is dropped or relabeled as supported.

The non-MVP matrix failures remain visible: `mir_mvp_binder_values` exposes
Full's dependent numeric conversion/type captures and T0's captured binder
environment, and `mir_mvp_binder_outcomes` exposes admitted Error completion.
The values fixture has matching failures under the earlier p11 Full/T0
controls; the outcomes fixture has recorded before/after controls. Full/T0
also retain the variadic var/rest failures from §10.55. No shared failure is
used as a reason to weaken a golden.

`p12-binder-qualified-validation-summary.json` records binary, runner, native
object/overlay and result hashes. The 3,543-file CLI source inventory has no
engine drift. Two native test units were corrected after that inventory and
are qualified through separate current VFS snapshots and the same frozen
engine objects. Their old failed runs remain as diagnostics; only the corrected
native runs qualify those fixtures. All 1,208 recorded inputs and 483 frozen
corpus files are unchanged at final audit. No existing oracle or benchmark
source changed in p12. Full language closure, aggregate Lambda GTests and
current typed/untyped performance targets remain open.


A follow-up diagnostic retains an additional boundary-ordering gap:
`observed(T: type, value: T, witness = type(value))` returns an integer type
witness for `observed(float, 1)` under all three tiers, while admission of
`value` converts it to float. The temporary expected-value probe records
**0/9** tier/GC matches in `p13-binder-default-observer-before.json`; it is not
part of the passing p12 population. The MVP parameter pass defers dependent
reference admission until after later defaults have evaluated. Resolve the
interaction between repeated-site joins, default dependencies and converted
parameter reads before claiming full dependent-call coverage
(**S11.4.8v2, S11.4.6v4**). No shared failure or oracle was changed.


### 10.57 Named system operands, numeric type calls and empty normal domains

**2026-11-06.** A fresh paired census identified three separate call-boundary
families. MVP rejected named system operands, treated explicit sized/u64
conversion types as Function values, and crashed during closed-domain planning
when a call argument contained only Error. These are implemented without new
interpreter delegation.

Named system operands now use the existing shared interpreter planner's
system-call admission predicate. Their AST wrappers forward the child value
and descriptor; system operands keep their source order and registry ABI.
This covers the existing `skip_null` aggregate forms without fabricating a
second option resolver. Dynamic Lambda named calls retain the declaration
requirement (**S12.3.2**).

One shared AST classifier identifies the numeric Type producers that Full
already intercepts for explicit conversion. MVP, Full and T0 use that
classifier; both MIR compilers use the same physical conversion emitter.
The returned Item retains conversion errors before any caller propagation,
without conflating explicit conversion with declared-contract admission
(**D4.2.1, D2.3.1**). The new `mir_mvp_numeric_type_calls` fixture checks
signed/unsigned wrapping, f32, a wide u64 result, Error transport and retained
array values under GC.

The admission crash was localized with small source reductions and an O3
release image with local symbols retained. Its backtrace ends in
`update_closed_values`; this was a compiler-planning crash, before a MIR dump
could be produced. `lambda_type_remove_error` returns NULL for an error-only
contract. The planner dereferenced that result while choosing a private
parameter lane, even though the implicit parameter rejects that argument
before the function can be entered (**S7.7.3**). The same root cause was
reproduced in an Error-bound bitwise argument, a successful-return domain scan
and an inferred mutable Error binding.

The shared private planner helper now normalizes that empty success domain to
`none`. Error-only call edges and exits provide no successful carrier evidence;
other observers can safely inspect the bottom type. The receiving contracts,
Error values, call effects and failure transport stay intact (**D4.3.1,
S11.1.7**). `mir_mvp_empty_normal_domains` combines all these cases with normal
calls, so the regression covers both the rejected edge and the surviving
successful lane. The original `admission_call_outcomes` now matches its existing
golden across all three tiers and GC modes; no oracle was changed.

Qualification uses `p13_empty_normal_domain-release.exe` (O3 ThinLTO, 769-unit
release graph), SHA256
`367b7a65f45f9cdaf6780c1ae506f89ef2d390be3ea40825c6bda43f3d47c3cd`:

- **129/129 MVP** tier/GC checks across 43 fixtures; Full and T0 each pass
  **120/129**, retaining the nine controls from §10.55–§10.56. AUTO passes
  **30/30**. The original fixtures remain in the population.
- Twenty-one Python methods pass **877 script invocations**. All 77 typed
  goldens and all 77 untyped v2 goldens pass under each compiler.
- Native cache/index/import metadata pass **10/10 normal + 10/10 forced GC**;
  admission/diagnostic checks pass **4/4 + 4/4**; security passes **98/98**
  with MVP selected and explicit interpreter controls retained.
- The real harness passes **120/135 MVP and 103/135 Full**. Six new rows
  include the two new fixtures plus `agg_null_absence`,
  `admission_call_outcomes`, `error_conversion_outcomes` and
  `fallible_scalar_member_call`. All six pass both policies. No prior passing
  row regresses in the common 129-case population; the same fifteen MVP
  failures also fail Full.

`p13-call-forms-validation-summary.json` records the executable, runner,
fixture, native object/overlay and report identities. The 3,543 source files,
1,220 recorded inputs and 483 frozen corpus files have zero drift at final
audit. Separate conversion-family screens retain the still-failing image and
imported-type cases; they are not silently included in passing counts. This
qualifies focused correctness, not the aggregate Lambda baseline or current
speed targets.

The frozen p12 discovery census is independent of this p13 qualification.
`run_discovery_census.py` records 2,123 discovered tests, copies the Lambda/lmd
inputs, uses corpus v2 benchmark sources and hashes 9,044 inputs in total.
It compares 50-case selections under both compiler policies through the real
harness, with separate reports and an explicit supervisor deadline. A stalled
or unreported compiler chunk remains unresolved; its cases are not counted as
passing. The first audit has 700 paired reported cases (**515 MVP, 603 Full**),
no input drift, and one unresolved MVP chunk. The live runner continues beyond
that audit. `p12-current-paired-census-summary.json` records the exact compared
population and partial status; do not combine it with p13 as one experiment.
Shared chart-package failures include frontend E312 on a range-index/len
comparison before either compiler runs. They remain visible shared failures,
not evidence that an MVP lowering route has been exercised.

An authority audit also distinguishes the old capture-write compatibility
code from valid new coverage: **S9.1.4v2** permits writes only in the confined
parent-fn case and otherwise retains immutable snapshots. Full's legacy
pn-parent write-back code grants no additional language permission. Keep the
capture-admission audit visible; do not remove MVP's rejection merely to
reproduce a nonconforming write-back path.


### 10.58 Imported type values and JavaScript export adapters

**D6.3.2** keeps the emitted-helper effect check authoritative. Imported type
values exposed an incorrect default effect for `base_type`: this helper reads
only the execution context's static literal-type table and does not allocate
or reenter. The registry now records that bounded read as noncollecting. MVP
can compile `import_type_values.ls` without weakening the ratchet.

**D7.4.1–D7.4.3, D6.2.3** require the foreign callable and its result owner to
cross their actual ABI. A JS import symbol denotes a hosted function object,
not a Core code address. The synthetic declaration publishes an arity but has
no Core AST parameter chain. MVP now reads its declared arity, imports the
hosted object under its actual symbol, and emits a checked Core boxed adapter
for direct calls and function values. It uses Full's existing bridge range
(0–8 arguments); larger foreign signatures remain rejected. Both MIR compilers
share `em_js_export_call`, which precisely roots the hosted callable and owns
the result's scalar home. This adds no Lambda interpreter delegation.

**S5.5.1v2, D7.2.1** govern adapter identity. A cross-import probe showed that
each importer previously created a new synthetic AST for the same guest
namespace. Per-importer native adapters also have different code addresses.
MVP stamps the existing `Function::def` / `def_module` fields with the export
site; equality uses the site when both operands publish one and otherwise
retains the existing native-pointer path. The canonical `ModuleDescriptor`
retains its synthetic import Script, distinct from the guest's parsed Script
at the same file path. The runtime owns that AST and clears the descriptor's
borrowed pointer before retiring it. It is never a process-wide AST cache or
a new GC root. JS-tainted import cones already exclude sealed MIR images.

The new paired fixtures `mir_mvp_js_exports` and `mir_mvp_js_export_values`
exercise direct bridge arities 0–8, cold floating results retained across
allocation, text, guest module state, errors, saved/array-held functions,
higher-order calls, closure captures and cross-import function identity.
`_mir_mvp_js_export_module.ls` has its own empty golden because it has no
entry expression. Its initial newly authored null oracle was corrected after
both compilers emitted no output; no existing oracle changed. The initial
and repeated real-harness reports remain separate. The native fresh-runtime
case covers state isolation and cross-import equality in three executions.
Full/T0 still reject ordinary calls through imported JS function values with
E229; those controls keep their semantic oracle and remain visible failures.

Qualification artifacts use prefix `p14-foreign-descriptor` under the task's
temporary directory. The current release is frozen before broad validation;
initial identity probes pass the three GC policies. The final 49-fixture matrix passes **147/147 MVP**, with **135/147**
under each control. AUTO/production passes **45/45**. Twenty-four Python
methods pass **910 script invocations** (including 77 typed goldens per
compiler), and untyped goldens pass **77/77 each**. Native checks pass
**11 + 11 forced GC** cache/index/import cases, **4 + 4** admission cases,
and **98** security cases with MVP selected. The real harness passes
**127/142 MVP, 109/142 Full**, with no prior passing row regressing. The
same fifteen shared failures remain. All 1,239 recorded inputs and 483
frozen corpus files are unchanged after the final checks; engine/native
source provenance is verified. Final AUTO controls use production: the
explicit MVP policy continues to require JIT, and the deliberately rejected
AUTO/MVP attempt remains a separate diagnostic artifact. Aggregate Lambda baseline and current performance remain
unqualified. This milestone does not establish all-language parity.


### 10.59 Complete coordinate reads and temporary storage ownership

**S8.2.4v3, S7.1.1v3, D2.3.4** require an N-D read to consume its entire key
list and keep an invalid receiver, wrong rank or invalid coordinate as null.
MVP previously emitted its single-index reader for a multi-key AST node,
returning a leading-axis row and skipping later key effects. The shared
`em_index_nd` stages every native coordinate and calls the existing runtime
reader. Full preserves its existing per-coordinate native conversions; MVP
uses the existing checked `fn_int64_index`. Both evaluate the receiver before
the keys and preserve its `last` binding. Multi-key reads no longer acquire
a first-row value/range proof or an ASCII-character specialization.

**D6.3.1, D6.3.4** distinguish the rooted receiver from raw temporary coordinates. A
collecting-key probe failed under both frozen p14 Full and the initial p15
candidate: `heap_data_calloc` produces data-zone bytes without an independent
object owner, so rooting their raw address cannot keep them alive. Coordinates
now occupy fixed native-frame storage allocated once per lexical read, outside
precise GC roots. The receiver remains precisely rooted across every key.
`em_fixed_stack_storage` also replaces the existing fixed bitcast fallback and
MVP admission-status allocation shape. This introduces no stack scanning.

Nested row storage is an MVP representation optimization. Its existing tensor
requirement now includes multi-key observations, immutable aliases and direct
call arguments, rather than coordinate writes alone. This makes the existing
`ndim_any_receiver` case work while retaining the prior row representation for
other consumers. It is not complete N-D construction support: the separate
`p15-ndim-literal-screen.json` retains three MVP failures (`typed_array_ndim_index`,
`typed_array_ndim_literal`, `typed_array_ndim_dynamic`). The first still exposes
unnormalized inner rows of 3-D literals; shape observers and opaque row producers
need a broader canonicalization proof. `typed_array_ndim_iter` passes that screen.

New paired fixtures `mir_mvp_ndim_reads` and `mir_mvp_ndim_effects` cover scalar
and cold-float results, all coordinates, misses, `last`, allocating keys and
observable receiver/key evaluation order. Together with the existing dynamic
receiver and seven image fixtures, **30/30 MVP and 30/30 Full** checks pass over
three GC policies. T0 retains its explicit index-expression exclusions in the
control report. `p15-ndim-owners-family.json` records that focused result.
The frozen candidate's final 59-fixture matrix passes **177/177 MVP**;
Full passes **165/177**, T0 **156/177**, retaining their existing controls and
index exclusions. AUTO/production passes **75/75**. Twenty-five Python methods
pass **970 script invocations**, including all 77 typed goldens under each
compiler; all 77 canonical untyped goldens pass under each compiler too.
Native cache/index/import checks pass **11 + 11 forced GC**, admission checks
**4 + 4**, and security **98/98 with MVP selected**. The real harness passes
**137/152 MVP, 119/152 Full**, adding ten passing MVP rows with no prior passing
row regressing; the same fifteen shared failures remain. All 1,262 recorded
inputs and 483 corpus files have zero drift. Engine/native effective-source
provenance is verified in `p15-ndim-owners-validation-summary.json`. Existing
semantic oracles remain unchanged. Aggregate Lambda baseline and performance
qualification remain open.

The independent frozen p12 census also finished its 86 compiler chunks. Five
chunks retain six missing/timed-out reports (250 unpaired cases); they do not
become passes. Its 1,873 paired reports have 1,334 MVP and 1,637 Full passes,
with zero input drift. The disk-interrupted unsaved attempt was preserved and
repeated after reclaiming superseded task link archives; source objects,
executables and evidence remain. No census process is still running. This
older diagnostic is not combined with p15 as a current whole-corpus gate.

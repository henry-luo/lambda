# Lambda implementation proposal — Interpreter and MIR runtime unification

## Second-pass runtime retirement

The second pass audited legacy Lambda execution paths on `master` at
`5d543154c05d14ea10c890811ecf6342126cb20a` with the first uncommitted
implementation below. Evidence is in `temp/interp-mir-retirement/`.

- The source inventory covers 1,434 definitions in the walker, planner,
  loader, module state, evaluator, memory runtime and MIR driver. It checks
  direct references, function-address uses and emitter strings across the
  Lambda sources, linked owners, modules, tools and tests. The 696 low-reference
  rows are a review queue. A registry row alone provides no call-site evidence.
- The whole-module AUTO experiment is removed under D8.3.1v3 and D8.3.4.
  The loader still builds eager MIR on an actual AUTO admission failure.
  LambdaJS can still request native exports over an initialized Lambda slab
  (D9.3.1–D9.3.2). Old source-size, instruction-count and document-pressure
  selectors were removed under D8.3.1v3 and AI19. Eager and satellite compilation
  each carry one explicit diagnostic choice through context creation, linking
  and teardown. The standalone Radiant host retains the old document policy
  variable at its link boundary.
- The old MIR Direct selector and the closed JS MIR module lease list had no
  callers that selected an alternate branch or acquired a lease. Retired
  raw bitwise helpers had only declarations and import rows. The live carriers
  are MIR instructions for proven native integers and checked boxed helpers for
  the generic operation (D7.6.1, S4.4.4). Both execution paths use the same JS
  export bridge for wide-integer conversion and result homes (D6.2, S4.9.1).
- The single-operand scratch object now owns a slot or faults through the
  recovery boundary (D6.3.3, S7.11.1v2). The old overflow arm could give allocating
  helpers an unrooted local. The paired-span allocator remains available for
  real dynamic extents. `effect_bounds` exposed a live undercount when the
  return of an indirect call needed admission while its arguments still had
  their roots. The call now reuses its callee slot for return admission and
  projected-`var` write-back. All six ordinary and forced-GC tier runs match
  the golden. The debugger trace from the failing candidate is in
  `scratch-backtrace.txt`.
- The second pass removes another 608 physical production lines under
  `lambda/`, including 263 from the interpreter files. Over both passes the
  reductions are 845 and 583 respectively. See `implementation-loc.tsv`.
- The debug probes verify generated native code past the previous O0
  threshold, exact output from the explicit MIR diagnostic, and an O0 Radiant
  document with native imports. The three GC recipe rows labeled JIT or MIR
  interpreter now explicitly select `--tier=jit`. Their forced-GC output
  matches the goldens under D8.6.3.

### Second-pass qualification

- `make test-lambda-interp` passed 1,612/1,612. `make test-lambda-baseline`
  with loopback networking passed 6,113/6,114 on the aggregate run, including
  all 595 MIR emission and 368 MIR forced-GC checks. The Procart drawing
  batch missed a strict interpreter result under concurrent load. Its isolated
  rerun passed. See `final-interp-baseline.log`, `final-lambda-baseline.log`
  and `procart-isolated.log`.
- The desktop no-MIR build had no MIR linker/generator symbols and passed all
  52 Lambda and LambdaJS golden runs, including forced/poisoned GC. The
  no-JS stub compilation and the 284-symbol host export check passed.
  Both LambdaJS resident-export checks and the Radiant O0 package import
  check passed. Binary identities are in `release-sha256.txt`.
- The pinned release passed all 40,261 Test262 baseline cases with zero
  non-fully-passing cases and zero regressions. The committed T0 gate
  supplies the second-pass corpus result. The full paired census below
  records the first-pass artifacts. See `test262-baseline.log`.
- The release comparison uses the first-pass candidate as control. All 90
  strict T0 pairs across ten R7RS rows matched observable stdout and reported
  zero fallback and exclusion. The geometric mean execution-time ratio
  was 0.9944. Four individual rows improved by about 0.5–1.9% with their
  paired 95% intervals below parity. See `paired-t0.json`.
- On 1,000, 5,000 and 20,000 generated statements at O0, the same output
  was produced in both releases across three measured pairs each. The
  current release generated native code on every sample. Median elapsed
  times were 78 to 120 ms, 1.47 to 2.06 s, and 28.14 to 36.94 s respectively.
  Eager native startup costs more on these workloads after automatic MIR
  interpretation was retired. See `scale-probe.json`.

### Remaining candidates from the second pass

| Surface | Evidence and next reduction |
| --- | --- |
| Satellite compiler and publication | `mvp_compile_function_image` uses one definition in the worker snapshot. All eight compile events in `worker-cluster.log` recorded `members=1`. The synchronous collector's sole caller outside the driver is the resident-export GTest. D8.3.3 calls for clusters and D8.3.4 requires a frozen input to the worker. Compute cluster membership on the evaluator, pass the frozen definitions to the existing queue and migrate the GTest to that path. Publication must retain an image before exposing any of its entries. Check a candidate receiving a second promotion while a cluster job is queued. [Worker and link](../../lambda/runtime/mvp/mvp_driver.cpp) · [Publication](../../lambda/runtime/interp.cpp) |
| REPL-only unindexed admission | File execution scans the published Lambda AST when available. A REPL entry still creates a temporary `Script` and walks the fragment before admission. A range-scoped scan over the retained index could remove the dual walk once rollback tests prove that rejected fragments restore the old AST range and import cone (D8.2.4, D8.3.6). [Scan](../../lambda/runtime/interp_plan.cpp) · [Transaction](../../lambda/runtime/runner.cpp) |
| Stack selection and bulk T0 setup | `script_run.cpp` walks the AST to match call names against bounded definition and call arrays. The match includes ordinary calls and can miss indirect recursion. Its handler gate changes stack selection deliberately. Replace it with a prepared call-graph fact only after testing imported and indirect recursion, handler fault recovery and the deep `ack2` case (D6.3.3, S7.4.3, S7.11.4). [Stack selection](../../lambda/runtime/script_run.cpp) |
| Borrow transport | The entry-backed write-back fallback in `interp_call_internal` is still used by view state and object fields. D5.4.6 and S9.2.3 require publication to the caller's actual place. Give those cases an explicit rooted place/home with the same detach and effect order before removing the legacy entry fields. [Borrow transport](../../lambda/runtime/interp.cpp) |
| Ambient MIR property reads | `lambda_active_module_name_id` and `lambda_active_module_name_item` remain registered and in the effect allowlist. The current emitter loads a checked module state explicitly and uses `lambda_module_name_id_at`. A retirement audit can verify every profile's generated references and remove the ambient pair along with its allowlist rows (D5.6, D6.4.3). [Registry](../../lambda/runtime/sys_func_registry.c) · [Emitter](../../lambda/runtime/mir_emitter_shared.hpp) |
| Bounded integer loops and diagnostic forms | The typed `sum` ablation below gives a concrete reason to keep the guarded loop. The explicit MIR interpreter still runs generated frame and GC tests under D8.6.3. Only its automatic selection paths were retired. [Loop](../../lambda/runtime/interp.cpp) · [GC gate](../../Makefile) |

## Implementation and qualification

Implemented on `master` at `5d543154c05d14ea10c890811ecf6342126cb20a` on 2026-11-19.
The implementation covers Lambda T0, eager MIR Direct and AUTO satellites under D8.3.1v3–D8.3.6.
The pinned release control and candidate are in `temp/interp-mir-unification/`, at SHA-256
`1dcd7f4a3e4a6d901a9657d3b17c2bec6d54664dc762f1a2ac3f8088b82818e9` and
`3b9260ddd28410ca1736a4983ba90a640060193b9374bece3348aa73dcf057f9`.

- A single table selects the checked helper for 23 generic binary operators. The builtin registry
  supplies the physical return kind to both tiers and selects boxed count and bitwise helpers.
  The new `checked_runtime_operators` golden shows that T0 keeps the original error in `in`,
  `at` and `of` (S7.9.3). Guarded unboxed MIR operations retain their native lowering.
- Eligible fixed-arity lexical calls reuse the caller's rooted argument window and the existing
  dispatcher. Each avoids a second root-span reservation and `argc + 1` duplicate root writes.
  Wide, adapted and hosted calls use the owning adapter. The initial generic shortcut slowed
  Fibonacci by 5–7% and was replaced by the lexical guard. Argument admission uses one pass
  for ordinary signatures and binds type variables before admission when required (S4.2.2).
- Checked nested stores reuse the planned root window and share RHS capture with flat stores
  (S9.1.2, D5.4.6). Lambda semantic child edges now belong to the shared AST profile (D8.2.4).
  REPL and module entry share slab preparation and activation. Whole-script and satellite
  publication share boxed entry lookup and update the state cell under D8.3.4.
- The three interpreter files lost 320 physical lines and 238 nonblank, noncomment lines.
  Changed runtime files lost 237 physical and 148 nonblank, noncomment lines, including the new
  operator table. The build generator and configuration changes bring the production patch to
  229 fewer physical lines overall. See `implementation-loc.tsv`.

### Release performance

- Strict T0 matched 252 exact-output pairs across 12 R7RS rows. Geometric means were 0.962 of
  control execution time, 0.984 of wall time and 1.001 of peak RSS. A repeat on the ten manifest
  R7RS rows matched all 110 pairs at 0.942 of control execution time. The paired runner checks
  runtime execution, fallback and exclusion counts for each timed T0 sample. These runs shared
  the host with test work. The 10% cohort target was not reached. See `final-interp-r7rs.json`
  and `final-interp-repeat.json`.
- All 20 manifest rows matched exact output across the three tiers. Ten broader rows matched
  all 90 pairs per tier, with execution-time geometric means of 1.046 for eager JIT and 0.983
  for AUTO. Native MIR entry was confirmed on both artifacts for those eager-JIT rows. A
  cache-disabled cold comparison matched 189 pairs across three fixtures. Each broader and
  cold per-row 95% interval included parity. See `final-eager-jit.json`, `final-auto.json`,
  `eager-jit-native-proof.tsv` and `cold-entry-summary.tsv`.
- In a release ablation over 66 exact-output T0 pairs, generic loop dispatch was 17.4 times
  slower on typed R7RS `sum` (95% interval 16.6–19.7). The other five rows included parity.
  We kept the guarded integer loop. Evaluating static preplanning separately requires a
  workload that isolates loop entry cost. See `loop-experiment/matched-loop-ablation.json`.

### Correctness

- The local Lambda baseline runner passed 6,114/6,114 and `make test-lambda-interp` passed
  1,611/1,611 with local networking enabled. This includes asynchronous promotion diagnostics,
  native MIR emission and forced GC stress. The release candidate passed 138 focused three-tier
  checks with ordinary and forced GC, plus whole-script AUTO and REPL rollback probes.
  Test262 had zero regressions across 40,261 baseline cases, with four passing on isolated retries.
- Both pinned censuses covered 2,004 scripts. The control had 1,818 matches, 185 LaTeX timeouts
  and the expected error-regression mismatch. The candidate had 1,887 matches, 117 LaTeX
  timeouts, zero mismatches and zero exclusions. All 11 candidate-only timeouts matched their
  goldens in both tiers on longer retries, with zero T0 fallback. Two additional timeout samples
  matched on both releases. The 117 timeouts are inconclusive under the standard budget.
  See `census-verdict-differences.tsv`, `census-timeout-retries.tsv` and `latex-long-retry.tsv`.
- The no-MIR desktop and safety worker builds linked without MIR compiler symbols. The desktop
  binary passed 50 Lambda and LambdaJS golden checks with ordinary and forced GC. The generator
  resolves owner paths once, applies the existing Input allocation rule to monolithic sources,
  and excludes Radiant's standalone entry point from the Lambda host (D9.1.1v4).
- Both pinned releases reproduce the timed JS cancellation failure that stops `make
  test-gc-rooting`. The other 16 runtime checks from that recipe passed independently on both.
  The static GC scripts also fail against the control source archive. Root-hazard diagnostics
  are identical. The candidate's effect output has two additional lexical endpoints originating at
  `key_span` and `rooted()` after the same memory-tracker `write` edge into compiler code.
- The source inventory records 378 public registry rows, 358 with boxed returns, and 1,280 distinct
  MIR import names across build guards. A probe of twelve emitted fixture groups found 99 registry
  helpers and one compiled-module import. An exhaustive hosted-descriptor, effect and retention
  audit remains follow-up work. See `public-builtin-carriers.tsv` and `candidate-emitted-helpers.tsv`.

## Recommendation

Keep the boxed AST walker in T0 and native lowering in MIR. Give both tiers one source for the
meaning of an operation, its checked runtime entry and its ownership contract. Share preparation
from the retained AST where the answer is independent of the backend. Let T0 evaluate children
into its planned root slots and let MIR lower them into its chosen carriers and control flow. This
preserves D8.3.1v3, the one indexed unit in D8.2.4, and the separation of semantic coercion,
representation conversion and rooting in D2.3 and D8.2.6.

Start with the boxed fallback paths. Their existing kernels make it possible to remove duplicate
dispatch and admission logic without taxing proven MIR fast paths. The first meaningful T0 speed
target is a call whose evaluated arguments are already in the caller's planned root window. Today
an ordinary interpreted call still goes through a dynamic adapter that roots and copies those
values again. A shared, audited prepared-arguments entry can remove that second pass when the
caller proves the required lifetimes. An interpreter that gets faster before promotion also
improves the cold path of AUTO.

## What the control code showed

`interp.cpp` had 9,574 physical lines, `interp_plan.cpp` had 2,298, and `interp.hpp` had 500, a
total of 12,372. These counts include scheduling, REPL, module, view and constant-folding code.
The total is a starting point for measuring simplification. Moving code to another file earns no
reduction in the combined source budget.

| Area | Current sharing and remaining duplication |
| --- | --- |
| Operators | The fallback switches in `eval_binary` and `em_boxed_binary_helper` choose the same symbol for all 23 common operators. This is a lexical comparison of the generic switch cases, including default boxed equality, for the generic paths. T0 also lists `is` and `is nan` in its switch. Raw `Bool` comparisons, vector comparisons, short-circuiting and constrained `is` need explicit treatment. [T0 switch](../../lambda/runtime/interp.cpp#L1716) · [MIR switch](../../lambda/runtime/mir_emitter_shared.hpp#L1137) |
| System calls | Both use `SysFuncInfo`. The control's `eval_sys_call` maps C prototypes and results, handles error operands, and recognizes raw bitwise entries separately. MVP independently selects raw, native or checked calls and converts their results. The control registry already routes generic `len` to `fn_len_checked` and shifts to boxed helpers. `count` and `band`/`bor`/`bxor`/`bnot` illustrate the control's raw-ABI adapters. [T0](../../lambda/runtime/interp.cpp#L1862) · [MVP](../../lambda/runtime/mvp/mvp_mir.cpp#L5865) · [registry](../../lambda/runtime/sys_func_registry.c#L385) |
| Calls and contracts | `ast_plan_call_shape`, `ast_resolve_call_arguments`, `ast_call_contract_return`, `fn_call_into` and the type-contract runtime are already common. `eval_call` stages arguments in rooted scratch, then ordinary control calls enter `lambda_dynamic_call`, which reserves another source span and re-homes wide values before optional/rest adaptation. T0's binding and parameter admission and MVP's boxed slow arms still choose checks and representation witnesses separately. [Call facts](../../lambda/runtime/ast-core.hpp#L775) · [T0 staging](../../lambda/runtime/interp.cpp#L2709) · [Dynamic adapter](../../lambda/runtime/lambda-eval.cpp#L2108) · [Contracts](../../lambda/runtime/type_contract.hpp) |
| Access and updates | Both use generic `fn_member`, `fn_index`, `fn_index_nd` and COW path helpers. T0 and MVP independently plan when to snapshot, detach and write back. The two paths for native `var` procedures both check the published kind and republish a place. MIR also has proven typed and shape-based paths. [T0 native `var`](../../lambda/runtime/interp.cpp#L2076) · [MVP native `var`](../../lambda/runtime/mvp/mvp_mir.cpp#L5185) · [MIR N-D fallback](../../lambda/runtime/mir_container_access.hpp#L22) |
| Frames, modules and tasks | Both use the same context, root/number side stacks, module state and activation service. T0's `InterpFrameGuard` writes through a static root window. MIR materializes roots at safepoints, uses generated epilogues and exposes `_b` entry points for promoted code. The remaining duplication concerns the entry/exit protocol, including borrow transport, return ownership and cleanup ordering. [T0 frame](../../lambda/runtime/interp.cpp#L519) · [MVP entry](../../lambda/runtime/mvp/mvp_mir.cpp#L9528) · [Runtime state](../../lambda/runtime/runtime-state.cpp#L309) · [Activation](../../lambda/runtime/activation.h) |
| Preparation | `interp_scan_supported` uses `AstIndex` when published. The control's `interp_visit_children` supplies extra Lambda edges to the front end, MIR planning and satellite indexing. `FnFramePlan` owns T0 slot and scratch requirements. MVP has separate liveness, shape and native-lane plans. T0's fast integer loop also rechecks AST shape and builds its literal lookup when it enters the loop. [Lambda traversal](../../lambda/runtime/ast-core.cpp#L1245) · [Admission](../../lambda/runtime/interp_plan.cpp#L578) · [Integer loop](../../lambda/runtime/interp.cpp#L5660) |

## Shared runtime and execution contracts

### 1. Use one checked entry per semantic operation

Use the existing builtin registry and JIT import metadata as the sources for builtins and MIR
imports. Start with one typed operator map from which T0 selects the function and MIR selects the
corresponding symbol and return ABI. The map describes operations after operand evaluation. Keep
`and`/`or`, `is nan`, constrained predicates and other evaluation-order decisions in the walker or
lowering. Record the actual C signature where an entry returns legacy `Bool` or another raw
carrier. Preserve error operands and visited child errors under S5.1, scalar symbolic comparison
under S6.1 and S10.2.2v2, and short-circuiting under S3.4. SO50 records the open questions on
competing-error selection and element-wise error shape. [Existing operator
map](../../lambda/runtime/mir_emitter_shared.hpp#L1137) · [Import table and
effects](../../lambda/runtime/sys_func_registry.h#L284)

Use `fn_len_checked` as the pattern for the public boxed entry. Start with `count` and the four
raw-registry bitwise functions, reusing their checked implementation or existing `fn_*_item`
kernels where applicable. Remove T0's corresponding raw result dispatch only as those entries
migrate. A raw leaf can serve as a proved fast path for either tier where its input domain and
completion are closed. Check error operand policy, legacy `BOOL_ERROR` and null pointer
conventions before migrating each family. The name of a C symbol alone conveys neither its
successful result representation nor its failure behavior. D7.4.3, D7.6.1–D7.6.2 and S7.10 govern
the checked boundary. [Builtin definitions](../../lambda/runtime/sys_func_registry.c#L389) ·
[Bitwise definitions](../../lambda/runtime/sys_func_registry.c#L1624)

Close the audit across the whole runtime surface incrementally. Enumerate T0 call targets, MIR
imports from actual emission (including indirect symbols from descriptors), and hosted descriptors
with dynamic registration. For each operation record source callers, input/return carriers, null
and completion semantics, GC and re-entry effects, and borrow, retention or write-back. Classify
each row as shared checked entry, proved fast path with a checked fallback, or an entry specific
to the interpreter, emitter, runtime lifecycle or foreign ABI. Compare emitted helper inventories
to the declarations at the milestone gate. This catches new one-tier paths while permitting e.g.
frame prologues and host adapters to retain their roles. Use existing `SysFuncInfo`,
`JitImportMetadata` and `mir.c` defaults, with unknown calls treated as `MAY_GC` under D6.3.2.
[Resolver](../../lambda/runtime/mir.c#L51) · [Registry](../../lambda/runtime/sys_func_registry.h)

### 2. Share argument and boundary preparation

Extend the existing AST call facts only where both consumers use the result. Share the resolved
source/formal mapping, presence/default/rest decisions, static signature facts and the
alias-sensitive argument positions. `ast_call_contract_return` remains the source of a
binding-declared return check, while a directly called definition uses its own declared result.
Carry explicit source and target types through the common `type_contract` runtime, where numeric
admission is value-aware and array/map witnesses keep their exact representation. Keep distinct
outcome routes for the initializer that binds its admission result, the malformed store that
follows its statement boundary, the returned error value and postfix `^`. A rejected parameter
becomes the *call's* value before the body runs under S7.7.3. Predicate execution remains an
explicit callback or emitted edge in the predicate's declaring module under S11.4.11. [Call
facts](../../lambda/runtime/ast.hpp#L357) · [T0 boundaries](../../lambda/runtime/interp.cpp#L1206)
· [MVP admission](../../lambda/runtime/mvp/mvp_mir.cpp#L717) · [Rulings S7.4, S7.6–S7.7, S11.4 and
D7.3.5](../../doc/Lambda_Formal_Semantics.md#s77-validation-outcomes-and-boundary-behavior)

Refactor the existing dynamic adapter to consume a prepared, already rooted argument window when
the caller can prove that the callable and Items stay in canonical roots and that all wide
payloads have stable number homes through the call. Start with positional fixed-arity calls and
borrowed **values**. An escaping or buffer-owned numeric payload still needs adoption. The current
owning route remains the fallback for foreign and public entry points, unstable payloads,
missing/default/rest adaptation where needed, and borrowed places until they qualify. Use the same
signature/entry-dispatch code for both routes. Compare a fault and an ordinary error through
T0→T0, T0→MIR and MIR→T0, and probe recursive and retained callbacks. The JIT's direct `Context*`
plus operand ABI and companion escape handling follow D7.4.2 and D6.2.1–D6.2.3. [Dynamic
adapter](../../lambda/runtime/lambda-eval.cpp#L2108) · [T0 call
frame](../../lambda/runtime/interp.cpp#L7354)

### 3. Share checked place operations and publication

Reuse `AstCowPath` for immutable place shape and the existing checked index/member and
`cow_path_*` helpers for value work. The caller evaluates receiver, keys and value in their
required order, with `last` scoped to the currently rooted owner. Capture each by-value argument,
including an injected pipe receiver, at its evaluation boundary. Form `var` borrows from evaluated
places in caller-owned homes. An address is a transport operand and the Item it refers to is
rooted in its existing slot. Preserve early failure, check published kinds, and write back after a
language error where D7.6.3 requires it. Start with a nested native `var` call and exercise typed
and open direct stores. Then extend the same discipline to N-D views, mutable captures,
method/state-backed places and collection mutation forms. Share helpers for checked
prepare/write-back while keeping tier-specific binding publication and MIR's proved direct
field/array operations. [T0 place keys](../../lambda/runtime/interp.cpp#L2008) · [T0 indexed
stores](../../lambda/runtime/interp.cpp#L5932) · [MVP place
writes](../../lambda/runtime/mvp/mvp_mir.cpp#L4334) · [Rulings S7.1–S7.2, S9.1.2–S9.1.3v4,
S10.1.2v5, D5.4 and D7.6.3–D7.6.4](../../doc/Lambda_Formal_Design.md#d54-copy-on-write)

Explore use of the existing rooted key-span COW entry points for small T0 places that currently
create a temporary key array. Size the span with the T0 frame plan and retain it through payload
conversion and write-back. Count actual short-lived allocations avoided. Apply the same method to
list/sequence assembly, paths, queries, comprehensions, grouping and joins only where a repeated
materialization is measured. Grouping already has a common kernel in `fn_group_by_keys`. Keep
positional append, spreading, void/absence and output demand explicit at the caller under S2.5 and
D3.3. [T0 grouping](../../lambda/runtime/interp.cpp#L4143) · [T0 checked nested
write](../../lambda/runtime/interp.cpp#L5882) · [MVP checked nested
write](../../lambda/runtime/mvp/mvp_mir.cpp#L4596)

### 4. Specify one entry/exit protocol with two frame implementations

Check the following sequence in both implementations. Establish the active context, module and
recovery checkpoint, reserve the planned roots and number extent, receive the already staged
values, admit contracts and execute. On completion, apply result admission and the required
commit, abort and write-back rules for that entry. Complete allocating cleanup before restoring
either watermark and preserve any escaping wide scalar through a destination home or a return
lane. Root any value that crosses an allocating step. Borrow transport is consumed and cleared at
the explicitly borrowed boundary. Ordinary language errors stay on the explicit outcome path.
System faults restore the checkpoint on the same activation, including when a language handler
catches a materialized fault. Both paths use the activation and scheduler service for suspension.
These invariants follow D6.1–D6.4, D7.4.3, D7.5.3, S7.11.2v2 and S12.4.4v4. [T0 borrowed
entry](../../lambda/runtime/interp.cpp#L7354) · [MIR borrowed
prologue](../../lambda/runtime/mvp/mvp_mir.cpp#L9528) · [Recovery
frame](../../lambda/runtime/recovery_frame.h) · [Open block
lowering](../../lambda/runtime/mvp/mvp_mir.cpp#L7320)

Keep storage policy with each owner. T0 continues to write through `RootFrame`/slots. Generated
code maintains safepoint-current homes through `MirEmitter`. Reuse `RuntimeModuleStateScope` and
the existing transaction and recovery primitives when entry-path behavior matches. Audit module
initialization, imports, host-retained functions and events, tasks and REPL rollback as entry
paths because their lifetimes go beyond an ordinary call. A task can park with its own root/number
segments and recovery chain. The REPL remains one persistent T0 session under D8.3.6. [T0 retained
entry](../../lambda/runtime/interp.cpp#L7755) · [T0 module
transaction](../../lambda/runtime/interp.cpp#L9357) · [Module
state](../../lambda/runtime/runtime-state.cpp#L314) · [Rulings D6 and
D9.3.2](../../doc/Lambda_Formal_Design.md#d93-script-packages)

Treat promotion as another consumer of the entry contract. Preserve `_b` entry, validated
self-tail/loop live-ins, the context-owned module slab, safe-point publication and image
generation/lifetime safeguards. Share checked entry and publication mechanics where eager,
satellite and experimental whole-script images meet the same conditions. Qualify default AUTO
separately from MIR optimization levels. A threshold or compiler
policy change is a separate performance variable. D8.3.1v3–D8.3.4 define the normal gating and
handoff behavior. [Promotion and experimental mode](../../lambda/runtime/interp.cpp#L8543) · [Loop
handoff](../../lambda/runtime/interp.cpp#L7173)

### 5. Prepare immutable facts once and simplify specialization

Move Lambda child-edge ownership to the Lambda profile and use the same traversal contract for
published `AstIndex`, fallback tree scans and satellite/REPL extensions. Verify edge coverage
before replacing any traversal. In particular, give the decomposed assignment place a single
semantic edge set even when the parsed LHS survives as a structural alias. Also check constrained
predicates, view/event bodies and `for`/join clauses. Keep the T0 frame and scratch plan specific
to the walker, and MIR liveness and carrier allocation specific to the emitter. Reuse AST facts
such as lexical binding identities, path descriptors, call shape and numeric proofs. Store only
genuinely shared semantic facts on the AST, and keep optional optimization data in the existing
places permitted by D8.2.4–D8.2.5. A reused fact must have an owner and a defined lifetime across
AST templates, REPL extension and satellite compilation.
[Traversal](../../lambda/runtime/ast-core.cpp#L1245) · [Indexed
profile](../../lambda/runtime/ast-core.cpp#L978) · [Frame
layout](../../lambda/runtime/ast-core.hpp#L378)

Measure the existing integer loop paths before consolidating them. `interp_fast_int_while`
currently checks shape, builds a local literal cache and tries a linear runner before its general
integer runner. First try moving stable loop and operand facts into the existing preparation pass
so tag and overflow guards are the remaining specialization checks inside the loop. Reuse the same
numeric domain, bounds and generic fallback with MIR, while allowing MIR to emit native
arithmetic. If the generic prepared walker recovers a specialized loop's gain, remove that
specialization and its private walk. If a narrow fast path still pays, retain one guarded
implementation for that case. A guard miss resumes from the current source point with already
committed changes and exact trip accounting. Use only static predictions and runtime guards for
these specializations under D7.4.1. [T0 integer runner](../../lambda/runtime/interp.cpp#L5660) ·
[MIR integer lowering](../../lambda/runtime/mvp/mvp_mir.cpp#L3118) · [Rulings D7.4.1 and
D8.3.2](../../doc/Lambda_Formal_Design.md#d74-dispatch-and-the-call-abi)

## Delivery order and evidence

| Phase | First change | Evidence to proceed |
| --- | --- | --- |
| 0. Freeze control | Pin release executable, source revision, submodule revisions, benchmark scripts and tier flags. Capture helper inventory, physical and nonblank/noncomment LOC, and a fresh strict-T0 census. | Committed lists currently contain 1,555 matching rows, zero excluded and zero inconclusive. That is a count of tracked records. Save a new sweep TSV under `temp/` before comparing a candidate. |
| 1. Helper selection | Replace the overlapping boxed operator selections, then migrate the checked builtin cohort. | Old switches/adapters are deleted where covered. Exact results for arithmetic, sized integers, poison, operand errors, comparisons and null. MIR emission and helper-effect checks pass. |
| 2. Call preparation | Add the prepared rooted route to the existing runtime dispatch and share boundary facts. Start with a fixed-arity call. | One fewer root-copy pass on the eligible route, with identical completion, output and diagnostics. Fixed/optional/rest, dynamic/hosted, mixed-tier, wide-return and forced-GC checks pass. |
| 3. Checked places | Unify checked nested writes and native `var` preparation and publication. Extend by indexing/storage family. | Exact results, alias snapshots, error write-back and task/fault cleanup across tiers. Report allocations and root-window usage separately. |
| 4. Runtime protocol and preparation | Align module/activation entry seams and move Lambda edge ownership out of the walker. Consolidate promotion helpers when publication semantics match. | Consistent entry/exit ordering, indexed coverage and frame budgets. Import, REPL, callback, task suspension and both promotion directions pass under stress. |
| 5. Hot paths and cleanup | Profile remaining dispatch, rooting and loop costs. Prepare or retire fast paths using paired release trials. Close the inventory family by family. | A net reduction across changed production files, measured T0 improvement on targeted workloads, and coverage for every discovered helper. |

Report LOC for the three interpreter files and for all production files touched by the series.
Attribute reductions to deleted branches, dispatch tables, duplicate walks or fast-loop machinery.
Report added shared code in the same total. Collect call/argument root copies, allocations,
generic helper calls, loop guard failures, and frame/root high-water marks in diagnostic runs.
Collect timing on ordinary release binaries with profiling and fault injection disabled. Compare
pure T0 work, startup/import cost and AUTO promotion/compilation separately.

Use paired, alternating release runs with identical typed and untyped source, fixed warmups, exact
non-timing stdout and exit status, and per-sample timing and hashes. Pick `r7rs/fib`, `tak` and
`sum` for recursion and scalar loops, `awfy/json`, `deltablue` and `richards` for call, container
and module edges, and `kostya/base64` plus `text/prettier_ast` for builtin and realistic text
paths. Include small direct-mode scripts to measure startup separately. The paired runner
on the control accepts `jit` and `auto`. The candidate supports strict `interp` for
baseline-versus-candidate T0 using the same per-sample output checks and tier provenance. Use
the golden harness invocation modes for direct and procedural correctness. Record whether the
`jit` trial used native MIR or the MIR interpreter fallback. On cold comparison runs, disable
code-cache reuse consistently. [Paired runner](../../test/benchmark/run_paired_benchmarks.py#L485)
· [Tier sweep](../../test/interp/tier_sweep.py) · [Turnaround and RSS
harness](../../test/interp/run_bench.py)

Use October's interpreter tuning report to select workloads and counters. Its 126-script
comparison used one strict-T0 timing per row. The newer MIR fast-L0 experiment was an eager native
comparison and gives compilation context. Measure the proposed call changes against the fresh
control. For the call pilot, use a proposed target of at least 10% lower strict-T0 execution time
on a frozen call-heavy cohort with a repeatable paired effect. Require stable cold wall time, peak
RSS, default AUTO and eager JIT on the rest of the cohort. Report individual regressions and
confidence intervals before taking an aggregate. Calibrate those thresholds against the control's
noise before testing candidates. [October
measurements](Lambda_Impl_Interp_Tune2.md#126-benchmark-round-and-threshold-evidence-2026-10-03) ·
[L0 experiment](../../test/benchmark/MIR_Fast_L0_POC_2026-11-18.md)

## Qualification

- Run focused `test_interp_gtest` and Lambda goldens for each changed family. Require exact
  `interp`/`jit`/`auto` output, matching error values and exit status, and zero Lambda MIR entries
  in strict T0. Include mutable aliasing, pipe receivers, `last`, N-D rank and views, invalid
  indices, wide scalar escapes, distinct errors and faults, transaction cleanup, imported
  predicates and retained callbacks. Compare tier output to the golden so a shared kernel
  regression cannot pass on tier equality alone.
- Exercise both directions of the mixed call boundary, plus forced synchronous entry and loop-head
  promotion (`LAMBDA_SATELLITE_SYNC=1` with low call and loop thresholds). Include a default
  asynchronous publication run that proves actual image publication and activation handoff. Add
  cancellation/stale-image coverage when touching publication. Test the `LAMBDA_NO_MIR` build when
  changing shared registry or entry code.
- Use `make test-lambda-interp` and capture a complete `test/interp/tier_sweep.py` TSV. Compare
  all three partitions and investigate mismatches/timeouts. Refresh generated lists together from
  one complete sweep only when the coverage changes. At milestones run `make test-gc-rooting`,
  `make test-mir-gc-stress`, `make check-error-recovery`, `make test-lambda-baseline` and `make
  test262-baseline`. Keep MIR emission ratchets and targeted wrapper checks under D8.6.1–D8.6.3.
  [Test map](../../test/README.md) · [Make targets](../../Makefile#L1594)

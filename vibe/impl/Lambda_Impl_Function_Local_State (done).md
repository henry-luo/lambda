# Lambda Function-Local State — Implementation Plan

**Date:** 2026-11-02.
**Status:** **complete for the ratified parent-fn proposal**, implemented and qualified on `codex/fn-local-state`, worktree `temp/fn-local-worktree` (2026-11-02). General indexed confinement analysis and precisely rooted shared homes replace the earlier unused-declaration subset. Final Lambda/input baseline: **8520/8520**. All 15 local-state fixtures match interpreter/JIT with zero fallback. Conservative unknown-retention handling, SO51 and unrelated existing failures remain explicitly recorded below.
**Follow-up audit:** comma declarations and closure escape through decomposition were missed by the initial qualification. Their correction and current gate status are tracked in [Function_Local_Decomposition](<Lambda_Impl_Function_Local_Decomposition (done).md>); the original 8520-test result below is historical evidence for the first qualification.
**Design owner:** [Lambda_Design_Procedural.md](../Lambda_Design_Procedural.md), §2, §4–§6.
**Authority:** S1.4v2, S2.5.3v3–S2.5.4v3, S7.4.6v3,
S9.1.2–S9.1.4v2, S9.1.7v2, S12.1.1v3–S12.1.2v3,
S12.1.4v4, S16.4.3v4, S16.6.5v2–S16.6.9v2;
D7.1.2, D7.2.3 and D5.3.2.

## 1. Delivery boundary

Implement local `var`, reassignment, member/index writes, `while`, `break`,
`continue` and `return` in named `fn` bodies and anonymous pure block bodies.
Preserve `fn` call colour, immutable ordinary parameters/captures and
observable container copies. Ordinary `fn` fallthrough still collects
results; local declarations, assignments and statement loops contribute no
result; explicit `return` replaces the accumulated result and passes through
the existing result normalization and return-contract checks.

Implement the user's capture clarification as **confinement**, not retained
mutable closure state: non-escaping nested `pn`s share their parent `fn`'s
local `var` bindings; any potentially escaping closure uses immutable
snapshots at creation and rejects captured writes. Pure closures always
snapshot. A mutable parent home may never escape in a returned closure.

**The `fn`→`pn` ban has no local-helper exception.** An `fn` cannot call a
nested `pn` directly, dynamically or through a polymorphic helper. Combined
with confinement, this leaves no legal execution path for a live-capture
procedure during the parent `fn` invocation. The capture declaration and
classification can be supported and verified, but a runtime example that
calls it from the `fn` is a negative test, not an acceptance demonstration.
Do not make the feature useful by silently weakening S12.1.1v3.

Out of scope: mutable escaping closures, `fn` inout parameters, widening the
capture exception to other parent colours, recolouring mutating builtins,
local `pn` call exceptions, new loop syntax and general comprehension
mutation/early exits. Functional `for` extensions remain SO51; existing
comprehensions must retain their meaning. If an implementation path needs
one of those new rulings, resolve it with the user before extending scope.

## 2. Source survey and reuse map

Read-only survey of the current working tree on 2026-11-02; symbol locations
must be rechecked when implementation starts. Existing unrelated changes in
the checkout are not part of this feature.

The worktree starts at `911a0b1ed8`. Its normative documents include the
ratified documentation snapshot from the main checkout, where this design
was still uncommitted. That snapshot also carries separately pending
error-design rulings; their implementation status remains in their own
footnotes. Runtime changes here implement the local-state slice.

| Area | Existing entry points | Required work |
|---|---|---|
| Parse and classify | `lambda/runtime/parser/lambda_parser.c`; `lambda/runtime/ast.hpp`: `is_procedural_only_stam`, `ast_block_is_procedural`; `build_ast.cpp`: branch/value-block classification | Distinguish local imperative statements from external effects and callable body position. Preserve map/block and brace rules. |
| Semantic admission | `build_ast.cpp`: `resolve_var_stam`, `resolve_assignment`, `resolve_control_statement`, `resolve_while` | Replace pn-only admission with callable ownership and lexical-target checks; retain call-effect checks independently. |
| Effect enforcement | `build_ast.cpp`: colour walk, `fn_colour_guard`, element-construction checks | Keep nested loops/handlers pure inside `fn`; keep static and dynamic pn refusal. |
| Captures and bindings | `build_ast.cpp`: `capture_indexed_outer_binding`, `finish_capture_analysis`, `analyze_captures`, `scan_invalidated_call`; `FnAnalysis`, `FnCapture`, `NameEntry` | Classify escape and owner before environment layout; reject snapshot writes; represent confined captures as access to the same parent binding. |
| Interpreter | `interp.cpp`: binding reads/writes, `interp_make_closure`, `EvalSignal`, loop and content evaluation | Preserve no-result versus null, propagate control correctly, use classified capture storage. |
| Production JIT | `transpile-mir.cpp`: closure environment emission, loop/assignment lowering, result collection and return epilogues | Reuse existing MIR control and COW paths with fn ownership; audit paths gated on procedure kind. |
| Auto tier | `interp.cpp`, `mir.c`, tier eligibility and handoff metadata | Carry capture/ownership facts across promotion without changing binding homes or results. |
| Experimental MIR path | `lambda/runtime/mvp/mvp_plan.cpp`, `mvp_mir.cpp` | Respect the shared semantic facts. Unsupported cases must use its documented rejection/fallback contract, never an unsafe admission. |
| GC and values | `lambda-eval.cpp`, `lambda-mem.cpp`, owned Item slots, precise root frames and closure tracing | Reuse existing ownership/rooting helpers. A live captured home is not an untraced native pointer or an independently writable snapshot. |
| Fixtures and harnesses | `test/lambda/`, `test/lambda/proc/`, `test/lambda/negative/`, parser/semantic GTests, GC stress and tier sweep | Migrate only obsolete fn-local rejection cases; preserve real effect and ownership negatives. |

Search for existing scope, ownership, escape and capture helpers before
adding any new mechanism. Extend shared indexed AST facts rather than
inventing separate interpreter and JIT escape classifiers. A third similar
case should generalize the shared helper (repository rule 13).

## 3. Phases and acceptance

### P1 — Separate semantic capabilities

1. Distinguish callable effect colour, lexical function/loop targets,
   mutable-root owner and value/statement context. Do not enable the feature
   by setting `NameScope::is_proc` on `fn` bodies or `while` scopes.
2. Admit local declarations and writes only inside a callable activation;
   module top level stays immutable. A nested `fn` starts a new ownership
   boundary, and neither kind of loop crosses that boundary for control.
3. Generalize statement/block classification so named fn bodies and pure
   arrow block bodies admit local statements. Preserve `() => {}` as an
   empty-map result and `(x) => {key: x}` as a map-valued arrow.
4. Retain static/dynamic procedure-call guards, including element literals,
   handlers and colour-polymorphic `function` calls. Their body-local
   controls follow the expanded fn rules; call colour remains separately
   checked under S12.1.4v4.
5. Audit the direct parser and Lambda-owned CST grammar together. Edit
   `grammar.js` only if needed, regenerate with `make generate-grammar`,
   and never edit a generated `parser.c` or a vendored grammar.

**Acceptance:** valid fn-local syntax reaches an AST with unchanged pure
call colour; top-level `var`, writes to outer variables, `fn(var x)`, a
procedure call hidden inside a while, and control targeting an outer
callable are rejected for the correct reason. Generic imperative operand
blocks and mixed value/control branches remain rejected. Existing pure
expressions and procedure parsing retain their behavior.

### P2 — Execute local computation on interp and MIR Direct

1. Reuse declaration/assignment, COW place-write, loop and completion paths.
   Rebinding and member/index updates must obey the ordinary declared-type,
   numeric, error and container-key contracts (S12.2.1v3, S9.1.6).
2. Keep the enclosing fn result collector distinct from the control body's
   sequential execution. A loop body expression must not leak a result into
   the enclosing fn merely because the effect colour is pure.
3. Return from the nearest callable through its existing normalization,
   contract and rooting epilogue. A handler's `return` must propagate out;
   its normal value still follows S7.6.1v9. Preserve ordinary error
   acknowledgement and typed-error changes already being implemented.
4. Derive mutable local types and carrier choices from all writes, including
   loop backedges. Audit code that assumes `AST_NODE_FUNC` has no return
   statement or `var` and code that selects scalar/vector lanes from the
   initializer alone.
5. Keep GC roots valid across allocating loop iterations, control exits,
   returning containers and early errors. Use `RootFrame`/`Rooted` and
   existing destination-owned scalar storage.

**Acceptance:** the same exact results and errors on `interp` and `jit` for
scalar accumulators, nested while/break/continue, early/bare returns, local
array/map updates, type-changing unannotated locals, annotated assignment
failures and allocating loops. Input values and saved snapshots remain
unchanged. Fn fallthrough collection and pn last-result behavior are
unchanged, including null versus no-result handling.

### P3 — Classify confined and escaping captures

1. Identify each captured binding's declaration owner. `fn` captures and
   captures outside the specifically authorized parent-fn case keep the
   existing snapshot contract.
2. Analyze escape through returns, aggregates, assignments, aliases,
   forwarding calls and transitive closure captures. A missing retaining
   summary is potentially escaping; it cannot authorize a live home.
   Compute facts before creation/lowering so a closure cannot switch from
   live access to a late snapshot after observing parent mutations.
3. A proven confined nested `pn` references the same parent home as its
   siblings. Whole-variable rebind and COW detach update that owner. Pure
   closures or ordinary value copies of its contents still snapshot.
4. Reject captured writes in every potentially escaping closure, including
   indirect escape via a returned object/array or another closure. Retire
   reliance on the old writable-env-copy/E231 behavior for these cases;
   replace it with direct ownership/capture diagnostics.
5. Keep lifetime and exclusivity boundaries explicit. No live home may be
   retained by a task, stored beyond the activation, forwarded across an
   immutable fn capture, or used after frame teardown. Preserve S9.1.5v2's
   no-cycle contract; a shared-capture backedge cannot justify silently
   adding recursive mutable function values.

**Acceptance:** a confined nested `pn` declaration may write a parent fn
local, while an escaping version is rejected. Returned read-only pn/fn
closures observe creation-time snapshots even when the parent subsequently
reassigns or mutates its local container. Function equality compares procedures formed at different parent values:
confined captures agree after rebinding, while pure and escaping snapshots
differ (S5.5.1v2). A native test verifies sibling writes through the same home,
owned scalar reads and GC reachability through closure environments alone. Calling either a confined or escaping
pn from fn remains a negative test. Do not claim runtime shared-write
coverage from a program forbidden by S12.1.1v3.

### P4 — Integrate optimization, promotion and precise roots

1. Audit constant evaluation, local CSE, loop transforms, stream fusion and
   parallelization consumers of `is_proc`/pure-colour facts. A local read
   depends on intervening writes even when the enclosing call is pure
   (D7.1.2). Reuse existing dependency analyses or decline the transform
   when the proof is absent; never hard-code a function or benchmark name.
2. Ensure compile-time evaluation terminates under its existing budget or
   declines unsupported loops. Do not compile a literal infinite while by
   executing it indefinitely.
3. Carry the same ownership/capture facts through auto promotion, cached
   code and runtime instantiation. New activations cannot share a live
   captured home. Snapshots can outlive their parent; live captures cannot.
4. Qualify GC at closure creation, COW replacement, loop backedges and
   explicit returns with forced collection. Include wide scalar snapshots
   as well as containers so owned-slot lifetime mistakes are visible.
5. Check MVP admission when this feature reaches its supported subset;
   extend its existing implementation or preserve explicit unsupported
   behavior. The production MIR Direct path is the required implementation.

**Acceptance:** optimization-on/off and interp/jit/auto agree on local
state-dependent results; promotion does not change capture values or
ownership; forced GC preserves outputs and finds no stale frame home.
Compilation of an intentionally nonterminating fn completes without running
the function. Use a bounded subprocess only if an execution-time control
test is needed; do not leave a hung process.

### P5 — Corpus migration and documentation closeout

1. Review negative fixtures such as `mutation_in_fn`,
   `proc_stam_outside_pn`, `function_body_var` and `capture_mutation` by
   their actual intent. Move only newly valid local-state examples into
   positive coverage. Preserve or split the effect, outer-write, top-level,
   escaped-write, immutable-binding and lexical-target negatives.
2. Add matching expected `.txt` output for each new `.ls` and follow the
   harness's diagnostic metadata conventions for negative cases. Give
   helpers/module fixtures the exempt names required by `test/README.md`.
3. Update `doc/Lambda_Func.md`, `doc/Lambda_Procedural.md`,
   `doc/Lambda_Expr_Stam.md`, affected tutorial/reference examples and the
   verified runtime descriptions only after behavior is qualified.
4. Update the formal implementation footnotes and this plan with actual
   completed behaviors, commands and results; regenerate the formal index.
   Keep SO51 and any unsupported backend slices explicit.

**Acceptance:** the Lambda baseline is green, changed examples are accurate,
and no negative test was removed merely to conceal a regression. Mark this
milestone complete when P1–P5 acceptance cases pass and required verification
is finished. Existing failures may remain only with base-control evidence
and explicit residue, as repository rule 1 permits; feature regressions
remain required work.

## 4. Focused behavior matrix

| Case | Required observation |
|---|---|
| Scalar while with continue and break | Deterministic accumulator; correct innermost targets. |
| Nested fn declared inside a loop | Its return exits itself; its break cannot target the parent's loop. |
| `fn f() { 1; var x = 0; x = 2; x }` | Same collected function result as the corresponding expressions; assignment emits nothing. |
| `fn f() { 1; return 2; 3 }` | Returns `2`, with normal function return conversion. |
| `fn f() { 1; return }` | Returns null, with annotation enforcement if present. |
| Local container copied from argument | Mutating the local leaves the argument and saved snapshots unchanged. |
| Nested while containing a pn call | Still rejected, including dynamic call paths. |
| Non-escaping pn with captured parent-fn write | Declaration/capture mode accepted; invocation from fn rejected. |
| Returned pn with captured write | Rejected, including escape through containers/aliases. |
| Returned read-only closure, parent later mutates | Snapshot reflects creation time, not return time. |
| Pure nested closure attempts outer write | Rejected even when the closure never escapes. |
| Ordinary `for` before/after adding a local var | Existing comprehension result unchanged. |
| Repeated fn calls, including after promotion | No live storage shared between activations. |
| Error/handler/return inside allocating loop | Correct outcome and rooting on every completion path. |

## 5. Verification budget

Before future builds or environmental investigations, read
[`Developer_Guide.md` §7](../../doc/dev/Developer_Guide.md#7-worktrees-and-agent-gotchas).
Keep temporary output under `./temp/`; preserve unrelated working-tree edits.

- **During each phase:** build the affected target once, then run focused
  parser/semantic or fixture filters appropriate to that phase. Pin runtime
  fixtures with `LAMBDA_EXEC_BACKEND=interp`, `jit` and `auto`; compare exact
  values and errors. Broaden only for new changes or unresolved failures.
- **At runtime/capture milestone boundaries:** run focused forced-GC and
  tier-parity coverage, then `make test-lambda-baseline` as the required
  aggregate core gate. Use `make interp-sweep` for interpreter changes,
  following the repository's harness guidance.
- **At final qualification:** run `make test-lambda-baseline`,
  `make test-mir-gc-stress`, and the relevant parser/semantic suite. Shared
  AST/runtime changes also require the affected JS baseline and rooting
  checks; do not change a JS/Test262 harness to mask a runtime failure.
- **Documentation:** `make formal-index`, `make check-formal-index`,
  `git diff --check`, and focused `make check-doc-code` checks for executable
  reference examples after implementation. Design-only examples carry
  `no-run`; executable acceptance cases have checked-in fixtures/goldens.
- **Performance, only after correctness:** optional release comparisons of
  iterative algorithms against existing pure formulations. Build with
  `make release` in the primary checkout, never in a worktree; record
  compiler/runtime configuration, repeated measurements and output parity.
  No debug-build performance claim and no required speedup assumption.

## 6. Completion record

| Milestone | State | Evidence |
|---|---|---|
| Design and formal rulings | Ratified 2026-11-02 | S9.1.4v2/D7.2.3; user's confinement clarification incorporated. |
| P1 semantic capabilities | Qualified | Callable ownership, local control targets and fn call colour remain separate. |
| P2 local execution | Qualified | Functional result collection, early returns, local writes and COW agree across production tiers. |
| P3 capture modes | Qualified | Shared indexed flow graph, known-call summaries, aliases/aggregates/transitive captures; T0/MIR share actual traced parent homes. Escaping and pure closures snapshot at creation and reject captured writes. |
| P4 optimization/promotion/GC | Qualified | Parent and environment homes precisely rooted; owned scalar reads; AUTO refuses unsupported shared-home satellites. Native sibling-home/GC and forced-collection parity cases added. |
| P5 qualification and reference docs | Complete | Final Lambda/input baseline 8520/8520; C/CST parsers 444/444 and 430/430; documented pre-existing sweep/checker failures; formal index and reference examples checked. |

### Expanded capture implementation

The graph has AST-value, binding, function-result and implicit variadic-input
nodes. Value edges carry closure identity through aliases, assignments,
aggregates and transitive captures. Known Lambda callees receive fixed-point
returned/retained-argument summaries, including `varg()` and named arguments;
audited native borrowing can prove non-retention. Missing summaries, tasks
and stores beyond the parent activation are retaining sinks. Comparisons
inspect their operands without returning them. An unconditional return
replaces earlier collected values, so discarding a local closure through a
known helper does not falsely make it escape. Proofs are conservative:
unknown callees require snapshots even when a particular invocation would
happen not to retain its argument.

Pure-fn capture boundaries, escaping intermediate pns and a backedge into a
captured parent binding also require snapshots. These checks preserve the
immutable capture and no-cycle contracts (S9.1.4v2, S9.1.5v2). Classification
finishes before capture creation; there is no late conversion of a live
capture into a snapshot.

Each shared binding owns one internal single-element GC Array. Its current
Item uses the existing owned-slot/scalar-tail protocol. Parent writes,
member/index COW replacement and sibling access use the same home. A fresh
execution of a local declaration creates a fresh home, including inside a
loop. Closure environments keep the home reachable through ordinary precise
GC edges; no native stack scan, new language reference type or vendor patch
is involved (D5.3.2). Snapshot reads copy wide scalar payloads and COW-mark
containers. T0 frame slots hold homes; MIR retains rooted homes and value
mirrors, refreshing mirrors after calls without clobbering pending scalar
result companions. AUTO uses the existing satellite refusal contract for
shared-home definitions, while explicit MIR JIT supports them.

S12.1.1v3 continues to prohibit invoking a confined pn from its parent fn.
Runtime sharing is observed legally through function equality (S5.5.1v2):
the same pn definition instantiated at two parent values compares equal
with a shared binding, while pure and escaping snapshots differ. Separate
loop-local bindings remain distinct. A native test checks actual sibling
writes, scalar payload independence and closure-only GC reachability;
it does not introduce a forbidden Lambda call path.

Focused qualification: `temp/fn_local_p3_smoke3.json` records **69/69**
three-tier checks, including forced GC/poisoning and low promotion thresholds
for positive fixtures. Seven additional escape negatives cover aliases,
aggregates, unknown callees, variadic return, transitive captures, pure-fn
capture boundaries, reference-cycle backedges and escaping pn ancestors.
The real Lambda harness passes 16 focused tests, the full tier-parity
harness passes, and the native ownership/escape filter passes 3/3. The native
home test also passes with GC forced at every allocation and freed-memory
poisoning. Expanded optimization checks pass 32/32 (MIR levels 0/3 and
constant folding on/off across all tiers). Returned pn snapshots from a
parent binding that also has a confined writer pass on all three tiers.
Parser gates pass C 444/444 and CST 430/430. Updated function examples have
31 clean blocks plus three expected errors; procedural examples have 25
clean blocks plus four expected errors, with no unexpected failures. All
31 newly added scripts have matching goldens. Final `make test-lambda-baseline`
passes **8520/8520**, including 2112 input, 1987 Lambda-script, 298 error/native,
350 MIR forced-GC and 849 JS MVP tests. No baseline failure remains.
Log: `temp/fn_local_p3_baseline.log`. Final focused harness/native logs:
`temp/fn_local_p3_harness_final.log`, `temp/fn_local_p3_native_final.log`.

### Existing qualification failures

The preceding implementation stage qualified the local-control slice with
`make test-lambda-baseline`: **8514/8514** passes, including 2112 input,
1983 Lambda-script, 296 error, 350 MIR forced-GC and 849 JS MVP tests.
Log: `temp/fn_local_baseline_qualified.log`. HTTP fixtures require loopback
access; an initial sandboxed attempt failed listener creation, and the
unsandboxed gate passed.

Its final differential sweep had **1417 scripts: 1397 matches, 11 exclusions,
eight mismatches and one LaTeX timeout**. All 11 local-state fixtures matched
with zero fallback. The eight mismatches are unchanged on saved base
`911a0b1ed8` and current per-tier binaries: `type_param_error_short_circuit`,
six chart error/control fixtures and `proc/proc_var_reassign_elem`.
A bounded isolated LaTeX control returns the same E308 stack overflow and
empty output on base and current. The sweep correctly refuses to regenerate
partition manifests while mismatches exist; they remain unchanged.
Evidence: `temp/fn_local_sweep_comparison.json`,
`temp/fn_local_latex_final_comparison.json`. The expanded sweep completes **1421 scripts: 1401 matches, 11 exclusions,
nine mismatches and zero timeouts**. All **15** local-state fixtures match
with zero fallback. The mismatch paths are exactly the nine previously
base-controlled cases, with no new mismatch path. The partition manifests
remain unchanged. Evidence: `temp/fn_local_p3_interp_sweep.log`,
`temp/fn_local_p3_sweep.tsv`, `temp/fn_local_p3_sweep_summary.json`.

`make test-gc-rooting-core` has an unchanged JS timer-cancellation failure
under forced GC: two base and two current runs let the cancelled timer fire.
Ten remaining runtime checks pass; the JS `--mir-interp` command is
unavailable on both base and current. Five preceding checks and the native
root hazard audit pass. Evidence: `temp/fn_local_timer_comparison.log`,
`temp/fn_local_remaining_roots.json`. The NO_GC source audit reports 12051
diagnostics on both base and the previous stage, with identical normalized
diagnostic sets (11933 unique); no added paths. Evidence:
`temp/fn_local_gc_effects_comparison.json`,
`temp/fn_local_gc_effects_delta.json`.

The expanded native-root hazard audit passes over **18556 functions**.
The expanded NO_GC audit still fails: **12054 diagnostics versus base 12051**
(11936 versus 11933 normalized unique paths). The three added diagnostics
name `RootFrame roots`, `Rooted<Item> initial` and `initial.get()` inside
`lambda_capture_home_new`; they extend the same pre-existing path through
`mvp_lmd_caught_value -> text -> paint`. That path treats the local
`Rooted<Item> text(...)` declaration as a call to the unrelated global
`text` function. The new home imports are all explicitly MAY_GC; this work
adds no NO_GC promise. Neither the checker nor its baseline was modified.
Evidence: `temp/fn_local_p3_root_hazards_final.log`,
`temp/fn_local_p3_gc_effects_final_delta.json`.

Previous optimization checks: six local-state fixtures agree at MIR levels
0 and 3 (12/12); five agree on interp/auto/jit with constant folding enabled
and disabled (30/30). An unused fn with an infinite while compiles and leaves
module result 42 within a bounded subprocess. Existing folding excludes
user-code calls and uses a fuel budget. Experimental MVP fails closed with
E501 for the scalar-loop probe; production MIR Direct is the required JIT.

Previous documentation checks pass for functions (31 clean, three expected
errors) and procedural examples (25 clean, four expected errors). Two
existing expression examples fail identically on base/current (E312 at line
411; E100 at line 1250). The formal index's duplicate DO25 warning predates
this work. The updated function/procedural example checks, formal index,
Markdown fence and whitespace checks pass. No unrelated golden or harness
was changed to conceal a failure. No release/performance claim is made.

### Additional root causes found during qualification

- **D6.2.1 / D6.3.4:** T0 returned Items pointing into C++-local ABI
  result homes. A later call overwrote an earlier binding's tiny float. The
  unchanged base reproduces `proc/mir_mvp_roots.ls` as false on interp/auto
  and true on jit. `interp_call_function_result` now uses the existing
  `scalar_storage_read` to move the payload into the current number frame
  before the local ABI home dies; method, borrowed, handoff and main paths
  share the helper. The JS export adapter applies the same lifetime rule.
- **S9.1.2 / S9.1.4v2:** closure capture stored a container pointer without
  marking it shared. Parent mutation changed the supposedly immutable
  snapshot. T0 now marks captured values, and the two MIR environment
  builders were consolidated into one precisely rooted snapshot builder.
  That builder also informs the MIR store planner that its source has an
  additional observer. Container captures are covered for returned fn and
  pn values under forced collection.
- **S9.1.4v2 / D7.2.3:** MIR rebuilt a named nested function's env at
  each direct call, observing later parent writes. Named captured definitions
  now bind a rooted closure at declaration. Direct calls load that closure's
  env while retaining static argument binding and the proven ABI; value uses
  load the same closure. Read-only capture slots never write back over newer
  parent values. `fn_local_named_snapshots` covers scalar/container reads,
  later aliases, sibling calls, repeated calls with intervening writes and
  named arguments; all three tiers pass under forced GC and poisoning.

- **D7.2.3 / D5.3.2:** initial capture-count publication read the cached
  count before initializing it, so MIR exposed a zero-field environment.
  A shared linked-capture counter now publishes the count before consumers
  use analysis. Live-versus-snapshot equality and forced-GC cases cover the
  resulting representation and tracing contract.

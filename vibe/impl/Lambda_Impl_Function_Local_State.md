# Lambda Function-Local State — Implementation Plan

**Date:** 2026-11-02.
**Status:** planned; no engine changes or execution qualification in this document update.
**Design owner:** [Lambda_Design_Procedural.md](../Lambda_Design_Procedural.md), §2, §4–§6.
**Authority:** S1.4v2, S2.5.3v3–S2.5.4v3, S7.4.6v3,
S9.1.2–S9.1.4v2, S9.1.7v2, S12.1.1v3–S12.1.2v3,
S12.1.4v4, S16.4.3v4, S16.6.5v2–S16.6.9v2;
D6.1.2v2, D6.2.3v3 and D4.3.3.

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
reassigns or mutates its local container. Analysis assertions establish that
sibling confined captures resolve the same binding owner and that every
escaping path retains snapshot mode. Calling either a confined or escaping
pn from fn remains a negative test. Do not claim runtime shared-write
coverage from a program forbidden by S12.1.1v3.

### P4 — Integrate optimization, promotion and precise roots

1. Audit constant evaluation, local CSE, loop transforms, stream fusion and
   parallelization consumers of `is_proc`/pure-colour facts. A local read
   depends on intervening writes even when the enclosing call is pure
   (D6.1.2v2). Reuse existing dependency analyses or decline the transform
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
plan complete only when P1–P5 and the required gates have passed.

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
  reference examples after implementation. Pending-design examples carry
  `no-run`; this documentation-only change needs no engine build.
- **Performance, only after correctness:** optional release comparisons of
  iterative algorithms against existing pure formulations. Build with
  `make release` in the primary checkout, never in a worktree; record
  compiler/runtime configuration, repeated measurements and output parity.
  No debug-build performance claim and no required speedup assumption.

## 6. Completion record

| Milestone | State | Evidence |
|---|---|---|
| Design and formal rulings | Ratified 2026-11-02 | Owning design and formal specs; user's confinement clarification incorporated. |
| P1 semantic capabilities | Not started | — |
| P2 local execution | Not started | — |
| P3 capture modes | Not started | — |
| P4 optimization/promotion/GC | Not started | — |
| P5 qualification and reference docs | Not started | — |

No runtime tests, benchmark results or implementation completion are claimed
by this plan. Comprehension extensions remain SO51, outside this milestone.

Documentation validation on 2026-11-02: the formal index was regenerated;
index freshness, added relative links, Markdown fence balance, history-ID
uniqueness and whitespace checks passed. The existing duplicate `DO25`
warning in the formal design predates this change and remains unchanged.

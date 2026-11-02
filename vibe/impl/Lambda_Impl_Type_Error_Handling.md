# Lambda — Type and Error Handling Implementation Plan

**Status:** planned; no implementation phase completed.
**Date:** 2026-11-02.
**Design:** [Type and Error Handling](../Lambda_Design_Type_Error_Handling.md),
TE-13.1, TE-16.1, and TE-18.1.
**Source survey:** working tree over `ca3cadb47`, 2026-11-02. Locations below identify
implementation entry points, not claims of new runtime conformance.

Implement the newly ratified rules together: initializer acknowledgement by
type admission, continued execution with a bound validation outcome, and
value-producing procedure handlers. Migrate authored Lambda binding annotations
from `T | error` to the existing `T^` shorthand where the annotation describes
the whole value-or-error outcome. The shorthand cleanup is explicitly requested
by the user; it is not a change to the language's union or return-channel rules.

## 1. Authority and acceptance boundary

| Area | Formal authority | Required result |
|---|---|---|
| Acknowledgement | S7.5.1v3, S7.5.2v3 | Inferred error-admitting initializers acknowledge; `any` and `any \| error` agree; undecidable admission warns rather than rejects. |
| Deferred validation | S7.7.1v2, S7.7.2v2, S7.7.5v2, S7.7.7v3, S11.4.1v4, S11.4.6v2, S11.4.7v2 | A deferred initializer binds the admitted value or rich validation error and continues; post-binding recovery is reachable. |
| Truthful types and storage | SI14v2, S12.2.1v2, D2.8.1, D2.8.3v2, D3.2.2v2, D3.2.3v2 | Keep the declared admission target separate from the effective outcome type; an error never enters a native value slot. |
| Handler results | S7.6.1v5, S7.6.7v5 | One-/two-arm handlers preserve normal results for `fn` and `pn`, including suspension and value positions. |
| Procedural sequences | S2.5.3v2, S2.5.4v3, S12.1.2v2 | Produced values replace earlier results, including `null`; declarations produce no result; explicit `return` exits the procedure. |
| Binding shorthand | S7.4.2 | `let`/`var` annotations `T^` and `T \| error` are equivalent. A function return `T^` retains its distinct raised-channel meaning. |

The [formal semantics](../../doc/Lambda_Formal_Semantics.md) and
[formal design](../../doc/Lambda_Formal_Design.md) govern this plan. Earlier
completed [containment](<Lambda_Impl_Ledger_Containment_Followup (done).md>) and
[error-rework](<Lambda_Impl_Error_Rework (done).md>) records explain the current
code; their initializer-skipping and statement-handler result rules are
superseded, not acceptance criteria for this implementation.

### Scope limits

- Implement `let` and `var` initialization, including nested blocks, ordinary
  loop bodies, and comma-separated declarations. Do not redefine reassignment,
  field/store failure, parameter admission, or dedicated typed loop-variable
  admission; retain their current contracts and test those boundaries separately.
- Reuse checked admission internally as a value-producing operation. The design's
  `admit(T, value)` is metanotation. Do not add a public builtin, cast operator,
  or grammar spelling before that surface is ruled. This does not block the
  initializer and existing-handler work.
- Preserve effect restrictions, immediate-expression acknowledgement outside
  the ruled initializer cases, system-fault boundaries, and precise GC roots.
  Do not introduce exception syntax, a new result object, or stack scanning.
- Do not expand the initializer warning exemption to parameters, returns,
  mutations, or unrelated nested calls. If an acceptance case needs an open
  design decision, record it and seek that ruling rather than silently choosing.

## 2. Current implementation and change map

| Entry point | Current behavior / required investigation |
|---|---|
| `lambda/runtime/build_ast.cpp`: `contract_acknowledges_error`, `validate_enforcing_calls_in_expression` | Uses the declared contract and explicit-error test; inferred initializers and `any` do not satisfy the new rule. Preserve the receiving-position distinctions when revising this walk. |
| `build_ast.cpp`: `resolve_declarator`, `check_declaration_static_boundary`; `lambda/runtime/type_contract.hpp`: `StaticBoundaryResult`, `lambda_static_boundary_relation` | The shared proven/rejected/deferred classification already exists. Reuse it to retain runtime validation, issue warnings, and derive the effective outcome type. |
| `lambda/runtime/type_contract.cpp`: `lambda_type_has_proven_error`; core `lambda_type_accepts_error` helpers | Distinguish explicit union membership, semantic admission, and undecidability. A conservative may-admit predicate alone is not proof of admission. |
| `lambda/runtime/ast-core.hpp`, `ast.hpp`, `interp_plan.cpp` | Audit declaration/name-entry type facts, carrier planning, and result contribution. Effective types must reach captures, reads, joins, and tier planning before code generation uses them. |
| `lambda/runtime/interp.cpp`: `interp_bind_declared_value`, `interp_coerce_declared_binding`, `interp_skip_binding` | Declaration failures currently request a binding skip. Bind the check's returned outcome instead; preserve the skip/report callers for unchanged mutation contracts. |
| `lambda/runtime/transpile-mir.cpp`: `MirBindingBoundary`, `emit_checked_boundary`, declaration lowering | Initializer checks currently branch to a declaring-block join. Return/join the outcome locally before publishing an error-capable binding; audit fused scalar/array checks and inlining. |
| `build_ast.cpp`: `handler_operand_is_proc`, `resolve_handler` | A `pn` operand selects `AST_NODE_HANDLER_STAM` and statement typing. Compute normal handler result types independently of callee effect. |
| `interp.cpp`: `eval_handler`; `transpile-mir.cpp`: `emit_handler_statement_result` | Normal procedure-handler outcomes are erased. Keep operand/selected-arm results while preserving fresh errors and control transfers. |
| `interp_plan.cpp`: `interp_proc_block_last_value`; `ast.hpp`: `is_side_effect_stam`; MIR `transpile_content_value` | Procedure handlers are excluded from block-result selection. Share the correct result-contribution rule without confusing an effectful value with a no-result declaration. |

`is_statement` also selects procedure-fault protection in the interpreter.
Simply clearing it to obtain a value would risk losing fault coverage. Separate
result production from the effect/fault-boundary decision and audit all users
of the node kind and flag. Keep the ordinary error ABI and precise ownership
rules shared by both execution tiers.

## 3. Implementation phases

### P0 — Pin the starting behavior and migration inventory

- Record the executable/commit used, active execution tier, and existing failures
  under `temp/error-handling-20261102/`. Read
  [Developer Guide §7](../../doc/dev/Developer_Guide.md#7-worktrees-and-agent-gotchas)
  before building or qualifying a worktree.
- Reproduce the existing handler result loss, declaration skip, and rejection
  of inferred/`any` acknowledgement with a small representative fixture set.
  Confirm `T^` binding support on both tiers; no grammar addition is expected.
- Inventory declaration failures whose callers rely on skipped effects or a
  block becoming an error. Classify each caller as preserving its intended
  early exit through explicit propagation/return, or adopting continued flow.
  Do not fix the migration solely by accepting new golden output.
- Inventory union-binding syntax across authored packages, demos, tests,
  benchmarks, and documentation snippets. Include multiline/compound types;
  exclude strings, comments, generated output, and vendored code from rewrites.

**Exit:** a bounded set of positive, negative, and unchanged-boundary cases,
plus a caller/syntax migration inventory. This is evidence collection, not a
new implementation of the semantics.

### P1 — Shared admission facts, effective types, and acknowledgement

- Preserve the written target contract and derive a separate effective binding
  type. Proven admission adds no new validation-error alternative; proven
  incompatibility remains a static error; deferred admission retains `T | error`
  or a sound approximation for a runtime-dependent target.
- Carry the effective type into name entries, downstream inference, captures,
  block joins, and representation plans. Do not widen a function signature or
  mutation target merely because a local declaration can fail.
- Reuse/extend the shared admission relation and existing union normalization;
  do not add independent interpreter/JIT type rules. Compare the initializer's
  possible error outcomes against the receiver, not merely the presence of
  some error subtype in the destination.
- Let inferred error-admitting bindings and semantic `any` acknowledge. Keep
  aliases and equivalent type expressions equivalent. Bare inference imprecision
  is not a blanket warning; unresolved annotated admission is.
- Add the unresolved-admission diagnostic through the existing diagnostic
  infrastructure, with binding/contract location and one useful warning per
  boundary. It must not invoke `--static-warning` behavior that drops a contract;
  the runtime annotation check remains active. Keep missing type names and known
  mismatches as errors. Reserve the diagnostic code through the existing registry.

**Exit:** frontend tests distinguish admission, rejection, and deferral; inferred
acknowledgement works without changing other receiving positions. P1's effective
type changes and P2's runtime routing must be qualified together before landing
if an intermediate state could select an incorrect carrier.

### P2 — Bind deferred validation outcomes on both tiers

- Make initializer admission a value-producing check using the existing runtime
  validator/admission helpers. Preserve single evaluation, numeric admission,
  map reification, array certificates, and rich boundary diagnostics.
- In T0, publish the admitted value or error in `interp_bind_declared_value`
  without requesting an initializer skip. Preserve root ownership and COW
  handling. Do not remove `interp_skip_binding` while mutation callers need it.
- In MIR, separate initializer outcome joins from the remaining boundary
  control/report destinations. Audit native scalar, nullable, array/map,
  fused-index, inline, and boxed declaration paths. A failure joins a safe
  outcome carrier before any native projection; it must not jump to the block
  tail or function epilogue.
- Keep the admitted-success representation when proven safe. Error-bearing
  locals/captures must use the existing suitable outcome representation until
  discharge/proof. No native error sentinel, fabricated null, unchecked cast,
  or early cleanup of a still-live block is permitted (D2.8.1, D5.1.4).
- Preserve no-result declaration semantics on both outcomes. Continue subsequent
  declarators and statements; return an error from the block only when ordinary
  result-producing or explicit control-flow code does so.
- Exercise ordinary loops and nested task scopes without changing iteration,
  cancellation, resource lifetime, or reassignment policy. Remove only routing
  that is now dead after all initializer paths have migrated.

**Exit:** failed deferred initialization binds an inspectable error and later
effects execute on interpreter, JIT, and auto tiering; native storage, captured
outcomes, and successful annotated fast paths remain correct.

### P3 — Uniform handler values and procedural results

- Infer handler results from the operand/selected arms for both `fn` and `pn`.
  Keep procedure effect and fault protection independent of whether the result
  is bound, used by another expression, or contributes to a sequence.
- Remove normal-result erasure in T0 and MIR. Success passes through in one-arm
  form; the selected arm supplies the two-arm result. An arm's new error is not
  handled again by the same construct.
- Update shared block-result classification so a final handler contributes its
  value, including `null`. Later no-result declarations leave it unchanged;
  later produced values replace it. Reuse or consolidate the existing helpers
  rather than adding divergent special cases to every backend.
- Preserve enclosing-procedure `return`, permitted loop control, and explicit
  propagation. Exercise synchronous and suspending procedure calls, ensuring
  the outcome and handler context remain rooted across suspension/resumption.
- Audit constrained/inlined/native-return paths and existing experimental MIR
  coverage affected by shared AST changes. Do not claim an additional compiler
  path conforms without exercising its admitted cases.

**Exit:** value-position and final-statement handlers agree across tiers,
including suspension, fresh errors, nested `^`/`~` contexts, and null results.

### P4 — Migrate callers and use `T^` for outcome bindings

The requested source cleanup is:

```lambda
let a: T | error = value
var b: T | error = value
```

to:

```lambda
let a: T^ = value
var b: T^ = value
```

- Rewrite **binding annotations only** where a top-level `error` alternative
  makes the whole bound value an outcome. Preserve grouping for compound `T`
  and verify the parsed type is equivalent; do not use a repository-wide text
  substitution. `(T | error)[]` describes per-element errors and is not `T[]^`.
- Do not convert function return declarations `T | error` to `T^`: that changes
  raising permission and caller obligations. Do not silently rewrite type
  aliases, parameter contracts, nested fields, or unions merely mentioning a
  constrained error subtype. Those are outside this binding-style cleanup.
- Apply the idiom to ordinary authored `.ls` code, including `any | error`
  bindings as `any^`, and corresponding usage examples. Keep a small deliberate
  set of union-spelling/equivalence tests, with an explicit reason, so support
  for the long spelling remains covered.
- Separately migrate callers that depended on implicit initializer exits.
  Express required early exits with existing propagation/return and required
  recovery with handlers. Retain continuation where it is intended. Update
  associated tests and goldens only against those reviewed behaviors.
- Simplify paired outcome-then-handler workarounds only where the new direct
  procedure-handler expression preserves the same return target and effects.
  Do not combine that semantic migration blindly with the spelling edit.

The initial single-line scan found **40 candidate matches**: `lmd/package` 3,
`test/demo` 6, `test/lambda` 29, and `test/benchmark` 2. This is an approximate
source scan, not a complete rewrite count; it includes examples/tests and misses
multiline declarations. Starting sites include:

- `lmd/package/chart/chart.ls`, `archive/service.ls`, and `edit/session.ls`.
- `test/demo/procart/painterly.ls`, `mail/mod_sync.ls`, and `word/mod_{io,app}.ls`.
- `test/lambda/proc/{handler_in_raising_pn,edit_session_save,ledger_containment}.ls`
  and the core acknowledgement/union fixtures.
- `test/benchmark/julia/micro_common{,2}.ls`.

**Exit:** reviewed callers preserve intended effects; ordinary outcome bindings
use `T^`; remaining long spellings are intentional tests or non-equivalent type
positions. The spelling-only subset preserves runtime output.

### P5 — Qualification and documentation

- Update the user references, tutorial snippets, and runtime implementation
  descriptions that still teach bare binding rejection, statement-result loss,
  or automatic initializer skipping. Start with `doc/Lambda_Error_Handling.md`,
  `doc/Lambda_Type.md`, `doc/Lambda_Procedural.md`, and relevant `doc/dev/lambda`
  chapters. Keep historical completed implementation plans historical.
- Update the design document and formal implementation footnotes only with
  measured results, execution modes, and remaining limitations. Regenerate the
  formal index if either formal spec changes.
- Record focused and aggregate results in this plan; do not mark a phase done
  because its code compiles or its goldens were regenerated.

**Exit:** the acceptance matrix below is qualified, caller migrations are
reviewed, required gates pass, and remaining open design work is distinguished
from implementation defects.

## 4. Acceptance matrix

| Case | Expected behavior |
|---|---|
| `let a = risky()` infers an error alternative | Acknowledged without redundant annotation. |
| `any`, `any \| error`, `any^`, and aliases | Equivalent initializer acknowledgement and retained outcomes. |
| Known `int \| error` source into declared `int` | Static rejection remains. |
| Unresolved annotated admission | One useful warning; runtime check remains active. |
| Runtime-dependent valid type vs missing type name | The former can defer; the latter remains a compile error. |
| Deferred valid/invalid value | Bind admitted value / rich error; subsequent effects run in both cases. |
| `a ^ { fallback }` following failed initialization | Recovery executes; diagnostic payload is available. |
| `{ 11; let a: T = dynamic_bad }` | Result is `11`; the declaration contributes no result. |
| `{ let a: T = dynamic_bad; a }` | Result is the validation error. |
| Nested block, loop-body declaration, comma-separated declarations | No initializer-driven block/iteration exit; later work executes. |
| Effectful initializer / validator | Each operand executes once; later effects follow normal flow. |
| Captured error, returned outcome, GC between binding and use | Payload/type/lifetime remain valid across both tiers and promotion. |
| Scalar, nullable, record, array, and inlined declaration paths | No error is unboxed; valid admission preserves representation contracts. |
| `pn_call() ^ { fallback }` in a binding and as final statement | Correct success/recovery value; null remains a real result. |
| Handler followed by declaration vs `null` | Declaration preserves prior result; null replaces it. |
| Two-arm/nested handlers, fresh arm error | One selected arm; correct `^`/`~` scope; no recursive self-catching. |
| `return` from a handler | Exits the enclosing procedure before binding/continuation. |
| Suspending procedure success/error | Same handler values and control targets after resume. |
| Rejected parameter / failed mutation | Existing callee-entry and no-invalid-write contracts remain. |
| `let`/`var` `T^` vs `T \| error` | Same admitted values, errors, acknowledgement, and allowed reassignment. |

## 5. Verification budget and completion record

Use existing fixture families first: `type_e228_acknowledgment`,
`ledger_named_error_ack`, `ledger_literal_type_syntax`, `handler_postfix_expr`,
`handler_two_arm`, `proc/handler_in_raising_pn`, `proc/ledger_containment`,
`proc/defect_value_flow`, parameter-admission fixtures, and concurrency handlers.
Audit negative declarations whose expected failure depended on skipping; retain
true static negatives and migrate changed runtime behavior to appropriate
positive assertions. Every new Lambda `.ls` fixture receives its `.txt` golden
and any registration required by [test/README.md](../../test/README.md).

Run focused tests during P1–P4 on `interp`, `jit`, and `auto`, with diagnostics
asserted separately from value goldens. Use forced/poisoned GC on retained error,
capture, suspension, and native-boundary cases with the current documented knobs.
Inspect focused MIR emission for unsafe unboxing, premature exits, duplicate
evaluation, and loss of proven clean fast paths; review changed emission/size
contracts rather than increasing budgets mechanically.

At the integrated milestone (and before any independently landed engine
milestone), run `make test-lambda-baseline` and the relevant existing MIR/GC
gates, including `make test-mir-gc-stress`. Exercise the changed package/demo
harnesses and the existing experimental MIR error tests if shared planning was
affected. Run grammar gates only if grammar/parser changes prove necessary;
the shorthand already exists. Run affected JS/Test262 gates if shared admission
or runtime helpers change guest-visible behavior; never mask harness failures.
No GUI/layout sweep is required solely for binding syntax edits.

Check documentation with `make check-formal-index`, link/whitespace checks, and
the applicable snippet checks when reference examples change. This plan itself
needs documentation checks, not an engine build. Performance claims require a
release build under the worktree rules; debug MIR inspection is not a timing
result. Keep all artifacts under `temp/`.

| Phase | Status | Evidence required before completion |
|---|---|---|
| P0 | Pending | Starting behavior, fixture set, caller and binding inventories |
| P1 | Pending | Admission/effective-type/diagnostic/acknowledgement cases |
| P2 | Pending | Continued validation outcomes and safe representation on all tiers |
| P3 | Pending | Uniform handler and sequence values, including suspension |
| P4 | Pending | Reviewed caller migration and binding shorthand diff |
| P5 | Pending | Aggregate gates, updated documentation, explicit residue |

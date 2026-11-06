# Lambda — Type and Error Handling Implementation Plan

**Status:** complete for production T0/MIR Direct, with the user parity waiver.
M1–M8 product changes and authored caller migration are implemented
and qualified by the Lambda, MIR, GC, input and Test262 gates in
`codex/error-handling-20261102`. On 2026-11-03 the user waived the final full
parity sweep and interpreter-manifest refresh and requested merge to remote
master and worktree removal. P5 is accepted with that explicit waiver; no
parity pass is claimed and all three manifests remain unchanged. The selected
normal admission ruling and approved MIR repair are implemented; safety
assertions remain intact. See the
[M8 qualification boundary](#m8-qualification-boundary-2026-11-03). Earlier
progress entries are historical checkpoints; their pending items do not override
the phase table or final production-integration record.
**Date:** 2026-11-02.
**Design:** [Type and Error Handling](../Lambda_Design_Type_Error_Handling.md),
TE-13.1–TE-13.4, TE-16.1–TE-16.4, and TE-18.1–TE-18.2.
**Source survey:** working tree over `ca3cadb47`, 2026-11-02. Locations below identify
implementation entry points, not claims of new runtime conformance.

Implement the newly ratified rules together: initializer and whole-variable
reassignment acknowledgement by type admission, continued execution with a bound validation outcome, and
value-producing procedure handlers with no optional success arm. Unify ordinary
raised/returned errors, return spellings, and outcome acknowledgement. Remove
`T^E` and its implicit broad ordinary-error allowance; use ordinary unions for
particular errors, keeping broad `T^`. Add
combined `or^` fallback, named filtered handlers and left-associative chains;
use `is (error | null)` for classification. Migrate
authored Lambda binding annotations
from `T | error` to the existing `T^` shorthand where the annotation describes
the whole value-or-error outcome. The shorthand cleanup is explicitly requested
by the user. Separately, TE-13.2 changes return semantics so that these spellings
are equivalent there too; the shorthand style cleanup remains focused on bindings.
TE-13.3 additionally requires migrating typed-caret contracts in every type
position, including returns and function types.

## 1. Authority and acceptance boundary

| Area | Formal authority | Required result |
|---|---|---|
| Unified ordinary errors | S7.4.1v2–S7.4.2v4, S7.4.5v2–S7.4.6v3, S11.4.2v3, S12.3.4v2, D6.2.1 | One ordinary-error discipline; equivalent return spellings; `raise` returns an admitted error early. |
| Acknowledgement | S7.5.1v6–S7.5.5 | Inferred initializers and whole-variable reassignments acknowledge; `any` and `any \| error` agree; unresolved explicit admission warns. Error-preserving composition carries outcomes to a receiver; unacknowledged fallible results cannot be overwritten. |
| Deferred validation | S7.7.1v3, S7.7.2v2, S7.7.5v3, S7.7.7v4, S11.4.1v5, S11.4.6v4, S11.4.7v3 | A deferred initializer binds the admitted value or rich validation error and continues; post-binding recovery is reachable. |
| Whole-variable reassignment | S7.5.2v4, S7.7.4v2–S7.7.6v2, S12.2.1v3 | Same admission decision as initialization; store the new outcome, including error, and continue. Never retain a stale old value on deferred failure. |
| Truthful types and storage | SI14v3, S12.2.1v3, D2.5.1, D2.5.3, D4.2.3, D4.2.4 | Keep the declared admission target separate from the effective outcome type; an error never enters a native value slot. |
| Handler results and surface | S7.6.1v9, S7.6.2v5, S7.6.6v6, S7.6.7v5, S10.1.7v4, S16.4.3v4 | The single error handler preserves results for `fn` and `pn`, including suspension. Reject and migrate the optional success arm; do not reinterpret it as cleanup. |
| Procedural sequences | S2.5.3v3, S2.5.4v3, S12.1.2v3 | Produced values replace earlier results, including `null`; declarations produce no result; explicit `return` exits the procedure. |
| Binding shorthand | S7.4.2v4 | `let`/`var` annotations `T^` and `T \| error` are equivalent; return spellings now have identical semantics too. |
| Particular error outcomes | S7.4.2v4, S11.4.2v3, D6.2.1 | Remove `T^E`; ordinary unions specify admitted error alternatives with no implicit broad ordinary errors. Derive type/ABI facts from the normalized contract. |
| Handler convenience | S7.6.8–S7.6.9, S7.5.6, S7.6.2v5, S7.6.5v4–S7.6.6v6, S10.1.7v4, S16.2.2v3, S16.2.6v2, S16.4.3v4 | `or^` covers errors/falsy values once; filtered handlers pass unmatched errors through and chains see fresh recovery errors. Explicit error-or-null classification acknowledges. |

The [formal semantics](../../doc/Lambda_Formal_Semantics.md) and
[formal design](../../doc/Lambda_Formal_Design.md) govern this plan. Earlier
completed [containment](<Lambda_Impl_Ledger_Containment_Followup (done).md>) and
[error-rework](<Lambda_Impl_Error_Rework (done).md>) records explain the current
code; their initializer/reassignment-skipping, retained-old-variable-value, and statement-handler result rules are
superseded, not acceptance criteria for this implementation. Their raised/returned
classification is likewise historical under TE-13.2.

### Scope limits

- Implement `let` and `var` initialization and whole-variable `var` reassignment,
  including nested blocks, ordinary loop bodies, and comma-separated declarations.
  Do not redefine field/element-store failure, parameter admission, or dedicated typed loop-variable
  admission; retain their current contracts and test those boundaries separately.
- Reuse checked admission internally as a value-producing operation. The design's
  `admit(T, value)` is metanotation. Do not add a public builtin, cast operator,
  or grammar spelling before that surface is ruled. This does not block the
  initializer and existing-handler work.
- Preserve effect restrictions, existing receiving contracts outside
  initializers and whole-variable reassignments, system-fault boundaries, and precise GC roots. Replace
  spelling-dependent acknowledgement with S7.5.1v6/S7.5.4 outcome tracking.
  Do not introduce exception syntax, a new result object, or stack scanning.
- General cleanup remains separate: no `finally` semantics or new cleanup
  keyword in this work. Existing resource rulings S12.4.2–S12.4.3 remain.
- Do not add `=^`, `^=`, or a `^or` alias. S7.5.7 rejects a predefined
  `empty` keyword/type alias; preserve ordinary `type empty = error | null;`
  declarations. Named error selectors are ruled; wider
  inline selector syntax and filtered system-fault policy remain follow-ups.
- Remove typed `T^E` syntax and its implicit broad ordinary-error allowance.
  Review each old contract before migrating it to `T | E`: recover/convert
  additional errors or retain broad admission with `T^` where needed.
  Inferred fallible function return contracts remain outside this ruling;
  preserve the unannotated-function default.
- Extend initializer acknowledgement and warnings unchanged to whole-variable
  reassignment. Do not extend them to parameters, returns, field/element stores,
  or unrelated nested calls. If an acceptance case needs an open
  design decision, record it and seek that ruling rather than silently choosing.

## 2. Current implementation and change map

| Entry point | Current behavior / required investigation |
|---|---|
| `lambda/runtime/build_ast.cpp`: `contract_acknowledges_error`, `validate_enforcing_calls_in_expression` | Uses the declared contract, explicit-error test, and `call->can_raise` gate. Check ordinary-error outcomes and track error-preserving composition to receivers. |
| `build_ast.cpp`: `validate_raise_channel`, `validate_function_return_contract`; `TypeFunc::can_raise`, `error_type`, `returned` | Separate spelling/ABI flags from semantic admission; normalize equivalent return contracts and permit `raise` under either spelling. Preserve unannotated-return defaults. |
| `lambda/runtime/parser/lambda_parser.c`: `parser_parse_return_types`, `LambdaCallableSignature`; type-pattern parsing; reference grammar `return_contract`, `value_error_type` | Remove the post-caret error type and second semantic return channel in return and value annotations. Preserve `T^` and ordinary unions in every type position; reject trailing typed-caret syntax instead of treating it as another construct. |
| T0/MIR raise/return lowering, direct/indirect/dynamic `call`, native error return planning | Forward existing errors unchanged, normalize non-errors once, and preserve caller behavior and native safety under either return spelling. |
| `build_ast.cpp`: `resolve_declarator`, `check_declaration_static_boundary`; `lambda/runtime/type_contract.hpp`: `StaticBoundaryResult`, `lambda_static_boundary_relation` | The shared proven/rejected/deferred classification already exists. Reuse it to retain runtime validation, issue warnings, and derive the effective outcome type. |
| `lambda/runtime/type_contract.cpp`: `lambda_type_has_proven_error`; core `lambda_type_accepts_error` helpers | Distinguish explicit union membership, semantic admission, and undecidability. A conservative may-admit predicate alone is not proof of admission. |
| `lambda/runtime/ast-core.hpp`, `ast.hpp`, `interp_plan.cpp` | Audit declaration/name-entry type facts, carrier planning, and result contribution. Effective types must reach captures, reads, joins, and tier planning before code generation uses them. |
| `lambda/runtime/interp.cpp`: `interp_bind_declared_value`, `interp_coerce_declared_binding`, `interp_skip_binding` | Declaration and whole-variable reassignment failures currently use containment. Bind/store the returned outcome instead; preserve remaining callers for unchanged field/element-store contracts. |
| `lambda/runtime/transpile-mir.cpp`: `MirBindingBoundary`, `emit_checked_boundary`, declaration and assignment lowering | Binding checks currently branch to a declaring-block join. Return/join the outcome locally before publishing an error-capable binding, replacing the old whole-variable value on reassignment; audit fused scalar/array checks and inlining. |
| `build_ast.cpp`: `AST_NODE_ASSIGN_STAM` admission/acknowledgement; shared variable type facts | Apply the same proven/rejected/deferred relation to whole-variable assignments. Track new effective outcomes through branches and loops while preserving the declared target. Keep indexed/member stores separate. |
| `build_ast.cpp`: `handler_operand_is_proc`, `resolve_handler` | A `pn` operand selects `AST_NODE_HANDLER_STAM` and statement typing. Compute normal handler result types independently of callee effect. |
| `lambda/runtime/parser/lambda_parser.c`; `lambda/tree-sitter-lambda/grammar.js`: `handler_expr` | Both accept the optional success suffix. Remove it, reject the retired form clearly, and preserve one-handler/postfix precedence. Regenerate the owned CST parser; never hand-edit generated `src/parser.c`. |
| `lambda/runtime/parser/lambda_lexer.c`, expression/postfix parser, reference grammar | Add contiguous `or^` at `or` precedence and named filtered handlers at postfix precedence. Preserve bare caret propagation, line-start rules, and structural bodies. |
| `AstHandlerNode::value_body`, handler syntax/reduction plumbing, AST visitors/dumps, T0/MIR arm lowering | Remove success-arm storage, typing, current-value contexts, and obsolete execution paths after callers and fixtures are migrated. Audit other consumers before deleting fields. |
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
- Inventory whole-variable reassignments relying on retained old values or a
  skipped tail. Pin differences from field/element stores, explicit RHS
  propagation, and statically rejected assignments; retain their distinct contracts.
- Pin the raised/returned difference using both return spellings and identical
  producer bodies. Inventory bare fallible statements, identity-handler
  softening wrappers, builtin conversions, compound expressions, callbacks,
  dynamic calls, and errors retained in collections.
- Inventory typed-caret contracts in authored declarations, callable types,
  packages, demos, tests, and examples. Separate actual error alternatives from
  the prior implicit broad allowance; do not match expression `^E { ... }`
  handlers as type annotations.
- Inventory union-binding syntax across authored packages, demos, tests,
  benchmarks, and documentation snippets. Include multiline/compound types;
  exclude strings, comments, generated output, and vendored code from rewrites.
- Inventory the retired success suffix independently, including same-line and
  multiline forms in packages, demos, tests, benchmarks, documentation, and
  AST/CST fixtures. Distinguish pass-through capture, true success transforms,
  and error-classification idioms before choosing each rewrite.

**Exit:** a bounded set of positive, negative, and unchanged-boundary cases,
plus a caller/syntax migration inventory. This is evidence collection, not a
new implementation of the semantics.

### P1 — Shared admission facts, effective types, and acknowledgement

- Normalize `T^` and `T | error` in function contracts as well as value types.
  Check `raise` permission against admitted errors, not caret syntax. Preserve
  plain-return rejection and implicit unannotated contracts. Derive specific
  error alternatives from ordinary unions, including aliases, nested function
  types, and error-only returns; no ordinary error is implicitly added to `E`.
- Remove typed-raise-only admission and automatic broad-error widening. Check
  raised, returned, propagated, builtin, and validation errors against the
  same return contract. Keep any useful error-set/ABI cache only as derived
  metadata; audit `TypeFunc::error_type` and `can_raise` consumers before
  removing or repurposing them. A map alternative remains a normal value,
  and an alias of `error` remains broad, not a nominal error subtype.
- Replace the enforcing-call-only diagnostic walk with outcome facts covering
  calls, constructors, conversions, and validation. Do not flag every AST node
  with an error union independently: an error preserved by a parent expression
  can be acknowledged once at that parent's receiver.
- Track result replacement in procedural sequences. A later produced result
  cannot erase an unacknowledged fallible outcome; a no-result declaration leaves
  it live. Check final outcomes against their actual receiver, not a scope-wide
  acknowledgement flag. Captured-but-unused outcomes remain legal.
- A recovery body's possible error remains an outcome requiring its own
  receiver; neither an identity handler nor a union spelling softens an error.
- Track which ordinary errors a filter can prove it covers; retain residual
  alternatives conservatively. Model `or^`'s local `^` as the actual error or
  falsy trigger, while ordinary/filtered handler subjects remain error-valued.
  Explicit classification including `error`, such as `is (error | null)`,
  acknowledges the tested outcome without introducing a new comparison rule.
- Preserve the written target contract and derive a separate effective binding
  type. Proven admission adds no new validation-error alternative; proven
  incompatibility remains a static error; deferred admission retains `T | error`
  or a sound approximation for a runtime-dependent target.
- Carry the effective type into name entries, downstream inference, captures,
  branch/loop joins, and representation plans. Update facts after every whole-variable
  assignment; an unannotated variable's earlier inferred type is not a fixed
  contract. Do not widen its declared target, a function signature, or a
  container element/field contract merely because a local binding check can fail.
- Reuse/extend the shared admission relation and existing union normalization;
  do not add independent interpreter/JIT type rules. Compare the initializer's
  possible error outcomes against the receiver, not merely the presence of
  some error subtype in the destination.
- Reuse that same relation for whole-variable reassignment acknowledgement.
  Known `int | error` into declared `int` stays a static error even after a
  prior deferred failure widened the effective type. Inferred receivers and
  `int^` retain the outcome; deferred explicit admission warns regardless of
  whether a tail exists. Capturing it needs no immediate inspection.
- Let inferred error-admitting bindings and semantic `any` acknowledge. Keep
  aliases and equivalent type expressions equivalent. Bare inference imprecision
  is not a blanket warning; unresolved annotated admission is.
- Add the unresolved-admission diagnostic through the existing diagnostic
  infrastructure, with binding/contract location and one useful warning per
  boundary. It must not invoke `--static-warning` behavior that drops a contract;
  the runtime annotation check remains active. Keep missing type names and known
  mismatches as errors. Reserve the diagnostic code through the existing registry.

**Exit:** frontend tests distinguish admission, rejection, and deferral; inferred
acknowledgement, return equivalence, composition, and overwritten-result checks
work under the existing receiving contracts. P1's effective
type changes and P2's runtime routing must be qualified together before landing
if an intermediate state could select an incorrect carrier.

### P2 — Bind validation outcomes and unify error returns on both tiers

- Make `raise` and an ordinary error return deliver the same outcome through
  existing cleanup/return paths. Normalize a non-error raise operand exactly
  once; forward existing errors unchanged. Do not legalize `return` in an `fn`
  or change the enclosing return target.
- Audit direct, indirect, recursive, inlined, closure, and dynamic `call`
  dispatch for distinctions selected by return spelling or `fn`/`pn` colour.
  Preserve effect restrictions and system-fault boundaries.
- Derive boxed/native result planning from normalized outcome contracts.
  Preserve native-success/error-lane safety and scalar companion homes;
  semantic channel unification must not erase necessary ABI information.
  Exercise specific-error unions and error-only returns through direct,
  indirect, and dynamic calls, without dependence on the retired typed caret.
- Make initializer and whole-variable reassignment admission a value-producing check using the existing runtime
  validator/admission helpers. Preserve single evaluation, numeric admission,
  map reification, array certificates, and rich boundary diagnostics.
- In T0, publish the admitted value or error in `interp_bind_declared_value`
  without requesting an initializer skip. Preserve root ownership and COW
  handling. Whole-variable reassignment must replace the old value with the
  new outcome and continue. Keep skip/report machinery while separate field/element
  store callers need it; do not change container contracts through a shared helper.
- In MIR, separate initializer/reassignment outcome joins from remaining boundary
  control/report destinations. Audit native scalar, nullable, array/map,
  fused-index, inline, and boxed binding paths. A failure joins a safe
  outcome carrier before any native projection; it must not jump to the block
  tail or function epilogue.
- Keep the admitted-success representation when proven safe. Error-bearing
  locals/captures must use the existing suitable outcome representation until
  discharge/proof. No native error sentinel, fabricated null, unchecked cast,
  or early cleanup of a still-live block is permitted (D2.5.1, D6.1.4).
- Preserve no-result declaration semantics on both outcomes. Continue subsequent
  declarators and statements; return an error from the block only when ordinary
  result-producing or explicit control-flow code does so.
- Exercise accumulator assignments, branch/loop joins, and nested task scopes.
  A stored error must not skip the tail or iteration. Preserve explicit RHS
  return/propagation before commit, cancellation, resource lifetime, and
  restrictions on module state/capture writes. Remove only routing now dead
  after initializer and whole-variable assignment paths have migrated.

**Exit:** failed deferred initialization/reassignment binds an inspectable error,
replaces any prior variable value, and later effects execute on interpreter, JIT, and auto tiering. Raised and returned
ordinary errors agree at callers under both return spellings. Native storage,
captured outcomes, and successful annotated fast paths remain correct.

### P3 — Handler forms, uniform values, and procedural results

- Remove typed-caret suffixes from production and reference type parsing,
  including named/anonymous callables, callable types, and value annotations.
  Audit `value_error_type` as well as `return_contract`; retain `T^` as the
  broad shorthand; normalize it with ordinary unions instead of restoring a
  raised channel. Reject `T^E` (including grouped/qualified error patterns) with
  migration guidance; a trailing type must not become a body or a new statement.
  Preserve expression propagation and filtered-handler grammar independently.
- Infer handler results from the operand/error body for both `fn` and `pn`.
  Keep procedure effect and fault protection independent of whether the result
  is bound, used by another expression, or contributes to a sequence.
- Remove normal-result erasure in T0 and MIR. Every non-error operand, including
  null/false/empty string, passes through; the error body supplies recovery.
  An error from that body is a fresh outcome, not handled again by the same construct.
- Remove the optional suffix from the production C parser and reference grammar;
  delete obsolete `value_body` plumbing and success-context setup in shared AST
  tools and both execution tiers. Add rejection coverage for the retired form,
  including multiline spelling, so it cannot silently parse as other statements.
  Update `grammar.js` and regenerate via `make generate-grammar`.
- Add `or^` and named filtered handlers to both front ends. Check ordinary
  `or` precedence/association, postfix handler chains, introducer/brace lines,
  and distinction from `e^ or fallback`. Do not reinterpret `^or` as recovery.
- Generalize shared handler representation and lowering for the selected
  trigger/filter instead of duplicating the existing handler shape. Preserve
  single operand evaluation, conditional body evaluation, and one result join.
  `or^` must not execute its body twice when recovery produces another falsy
  value; filtered misses must preserve the exact error without early return.
- Make each chained filter receive the preceding expression's result, including
  errors produced by recovery bodies. Reuse existing type/pattern matching;
  do not invent error equality or nominal identity for aliases of `error`.
  Preserve nested caret subjects and outer tilde contexts across suspension.
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

**Exit:** unfiltered/filtered single-arm handlers and combined fallback are
accepted, while the retired success suffix is rejected. Value-position and
final-statement handlers agree across tiers, including suspension, fresh errors, nested `^`
bindings, preserved outer `~`, and null results. Land parser retirement with
P4's caller/fixture migration so ordinary repository programs are not left broken.

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
- Function return declarations `T | error` and `T^` now have identical semantics
  under S7.4.2v3. Broad return-style, alias, parameter, and nested-field rewrites
  remain outside this binding-style cleanup; retain both return spellings in
  equivalence tests. Do not shorten constrained error subtypes to generic `T^`.
- Apply the idiom to ordinary authored `.ls` code, including `any | error`
  bindings as `any^`, and corresponding usage examples. Keep a small deliberate
  set of union-spelling/equivalence tests, with an explicit reason, so support
  for the long spelling remains covered.
- Separately migrate callers that depended on implicit initializer exits.
  Express required early exits with existing propagation/return and required
  recovery with handlers. Retain continuation where it is intended. Update
  associated tests and goldens only against those reviewed behaviors.
- Migrate bare fallible results formerly exempt as soft errors. Capture,
  recover, or propagate according to intended behavior; do not mechanically
  insert dummy captures to silence diagnostics. Replace identity handlers used
  only for softening with direct outcome bindings where no required procedure
  fault protection is lost.
- Migrate typed-caret contracts after reviewing every ordinary error producer.
  Use `T | E` when the outcomes fit; otherwise handle/convert extra errors or
  use `T^`. This is not a spelling-only rewrite: the old convention admitted
  additional ordinary errors implicitly. Include error-only contracts formerly
  written `none^E`, which can use admitted error type `E` or broad `error`.
  Keep the retired syntax only in explicit rejection tests and marked history.
- Review callers that relied on failed reassignment retaining an old value.
  If retention is intended, recover explicitly before committing the new value;
  if early exit is intended, use explicit propagation/return. A handler on the
  raw RHS does not catch later annotation validation. Do not invent the still-open
  public validation syntax to migrate a caller; record any actual dependency.
- Simplify paired outcome-then-handler workarounds only where the new direct
  procedure-handler expression preserves the same return target and effects.
  Do not combine that semantic migration blindly with the spelling edit.
- Migrate retired success-arm calls according to TE-16.2: direct assignment
  from `e ^ { recovery }` for pass-through capture; bind once and `match` for
  distinct success/error processing. A pure `^{true} ~{false}` error test can
  become an acknowledged outcome binding followed by `is error`.
  Preserve single evaluation, return/break/continue targets, and whether
  transform errors are caught. Do not map after recovery or move a success
  transform inside the protected operand unless that change is intended.
- Preserve lexical context during `match` migration. Its arms introduce `~`,
  whereas the old error body retained the outer `~`; capture/refer to the outer
  subject explicitly when needed and rewrite implicit fields carefully.
  Do not replace a success-only branch with an unconditional statement.
- Include existing uses in `test/demo/index.ls`, `photos/photos.ls`,
  `slides/mod_slide_drag.ls`, and sheet/sketch tests in the retirement inventory.
  Convert `handler_two_arm` success tests to equivalent one-handler/match cases;
  retain the retired spelling only in intentional syntax-rejection fixtures.

The initial single-line scan found **40 candidate matches**: `lmd/package` 3,
`test/demo` 6, `test/lambda` 29, and `test/benchmark` 2. This is an approximate
source scan, not a complete rewrite count; it includes examples/tests and misses
multiline declarations. Starting sites include:

- `lmd/package/chart/chart.ls`, `archive/service.ls`, and `edit/session.ls`.
- `test/demo/procart/painterly.ls`, `mail/mod_sync.ls`, and `word/mod_{io,app}.ls`.
- `test/lambda/proc/{handler_in_raising_pn,edit_session_save,ledger_containment}.ls`
  and the core acknowledgement/union fixtures.
- `test/benchmark/julia/micro_common{,2}.ls`.

**Exit:** no active success-arm uses remain outside explicit rejection tests or
marked historical documentation; reviewed callers preserve intended effects; ordinary outcome bindings
use `T^`; remaining long spellings are intentional tests or non-equivalent type
positions. The spelling-only subset preserves runtime output.

### P5 — Qualification and documentation

- Update the user references, tutorial snippets, and runtime implementation
  descriptions that still teach bare binding rejection, statement-result loss,
  automatic initializer/reassignment skipping, retained-old-variable behavior,
  or raised/returned obligation differences.
  Cover return equivalence, early error return, composition, and overwritten
  outcomes. Start with `doc/Lambda_Error_Handling.md`,
  `doc/Lambda_Type.md` and `doc/Lambda_Procedural.md`. Keep historical completed implementation plans historical.
- Document `or^` precedence and broader caret subject, filtered residual errors,
  chains versus a single match, and the existing union classification spelling.
  Document S7.5.7: `empty` is not reserved or predefined; users may declare
  it as an ordinary alias for `error | null`.
- Document `T^E` retirement, `T^` retention, and union-only particular errors.
  Remove any current explanation that limits `E` to user-raised errors or
  promises implicit extra ordinary errors. Preserve exceptional system faults
  and explain why migration can require broader admission or local recovery.
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
| Existing error via `raise` vs ordinary return | Same payload at the caller; explicit early return exits before later statements. |
| Non-error operand to `raise` | Normalize once, then exit the enclosing callable. |
| Function return `T^` vs `T \| error`, including aliases | Same admission, raise permission, acknowledgement, and safe ABI behavior. |
| `T \| E` with matching error raised, returned, or propagated | Same admitted error alternatives and acknowledgement. |
| Possible ordinary error outside a specific return union | No implicit admission; reject known incompatibility and require handling/conversion or widening. |
| Builtin or validation error under `T \| E` | Same ordinary-error contract; no producer-specific exemption. |
| `E` / `none \| E` error-only returns | Admit the specified error values without a fabricated successful result. |
| `type E = error`; ordinary non-error alternatives | Broad error alias stays broad; a non-error alternative does not become an error. |
| `T^E` in return/value annotations and callable types, including grouped/qualified patterns | Both parsers reject the retired syntax; never split the trailing type into a body or statement. |
| `T^`, union types, expression `e^`, and `e ^E { h }` | Retained forms remain distinct and valid under their respective rules. |
| `let n = int(text) + 1` | Retain and acknowledge the composed error at the binding. |
| Fallible statement overwritten by a later result | Static error without acknowledgement; no runtime skip. |
| Fallible result followed only by no-result declarations | Check retained result against its actual receiver. |
| Captured outcome never inspected | Legal; no mandatory inspection or unused-binding error. |
| Identity handler returning the caught error | Outcome remains error-capable; no softer category. |
| Dynamic `call` of `fn`/`pn`, direct and indirect calls | Uniform ordinary-error discipline; effect restrictions remain. |
| `or^` on errors/null/false/empty text | One operand/body evaluation; caret is the exact trigger. |
| `or^` on zero/NaN/empty containers/other truthy values | Operand passes through; body does not run. |
| Combined fallback body returns error/null/false/empty text | Fresh result is returned; no repeated fallback. |
| Sum followed by `or^`; mixed chains and parentheses | Ordinary `or` precedence; filtered handlers retain postfix precedence. |
| Filter match vs miss vs success | Recover once / retain original error / retain success. |
| First filter body produces an error matched by the next | Second handler executes; original operand still executes once. |
| Residual error into clean vs admitting receiver | Reject incomplete discharge / retain error possibility. |
| `is (error \| null)` with an effectful operand | Evaluate once and classify; acknowledge the tested error. |
| Nested handler subjects, scopes, suspension, explicit return/raise | Correct caret types/lifetimes and enclosing control targets. |
| `=^`, `^=`, `^or` as new syntax; predefined `empty` | No new operators, operator-alias meaning, or predefined name. |
| User-defined `type empty = error \| null;` | Ordinary scoped alias; `value is empty` matches the explicit union. |
| `let a = risky()` infers an error alternative | Acknowledged without redundant annotation. |
| `any`, `any \| error`, `any^`, and aliases | Equivalent initializer acknowledgement and retained outcomes. |
| Known `int \| error` source into declared `int` | Static rejection remains. |
| Unresolved annotated admission | One useful warning; runtime check remains active. |
| Runtime-dependent valid type vs missing type name | The former can defer; the latter remains a compile error. |
| Deferred valid/invalid value | Bind admitted value / rich error; subsequent effects run in both cases. |
| Unannotated `var count = 0; count = risky()` returning `int^` | Capture/acknowledge the new outcome; inference retains possible error; no imprecision-only warning. |
| Reassignment to `int^`, `any`, or equivalent aliases | Admit the compatible success/error outcome without a redundant acknowledgement warning. |
| Known `int \| error` assigned to declared `int` | Static error, including after an earlier deferred failure; effective widening never changes the declared contract. |
| Deferred assignment to declared `int`, valid/invalid runtime value | Warn; store admitted integer / rich validation error, replacing the old value; continue even in final position. |
| Error-producing RHS and later recovery/effects | RHS executes once; later code sees the new error, not the old value; no mandatory immediate inspection. |
| Assignment in a branch or loop, including zero iterations | Effective types cover all reachable outcomes; no stale clean native projection; no implicit loop exit. |
| Clean assignment after an error-capable assignment | Preserve the declared contract; native use requires a valid clean-path proof. |
| RHS handler succeeds but later annotation validation fails | No blanket diagnostic suppression; store validation error and reach subsequent recovery. |
| Explicit propagation/return in RHS | Ordinary control transfer before assignment; do not commit a partial/new value. |
| `a ^ { fallback }` following failed initialization | Recovery executes; diagnostic payload is available. |
| `{ 11; let a: T = dynamic_bad }` | Result is `11`; the declaration contributes no result. |
| `{ let a: T = dynamic_bad; a }` | Result is the validation error. |
| Nested block, loop-body declaration, comma-separated declarations | No initializer-driven block/iteration exit; later work executes. |
| Effectful initializer / validator | Each operand executes once; later effects follow normal flow. |
| Captured error, returned outcome, GC between binding and use | Payload/type/lifetime remain valid across both tiers and promotion. |
| Scalar, nullable, record, array, and inlined declaration paths | No error is unboxed; valid admission preserves representation contracts. |
| `pn_call() ^ { fallback }` in a binding and as final statement | Correct success/recovery value; null remains a real result. |
| Handler followed by declaration vs `null` | Declaration preserves prior result; null replaces it. |
| Retired `e ^ { h } ~ { v }`, same-line and multiline | Syntax error on both parsers; no cleanup or trailing-expression reinterpretation. |
| Nested handlers, fresh body error | Correct scoped `^`, preserved outer `~`, and no recursive self-catching. |
| Migrated success transform vs recovered outcome | Transform runs only on original success; its errors remain fresh. |
| `return` from a handler | Exits the enclosing procedure before binding/continuation. |
| Suspending procedure success/error | Same handler values and control targets after resume. |
| Rejected parameter / failed field or element store | Existing callee-entry and no-invalid-write/failure-control contracts remain. |
| `let`/`var` `T^` vs `T \| error` | Same admitted values, errors, acknowledgement, and allowed reassignment. |

## 5. Verification budget and completion record

Use existing fixture families first: `type_e228_acknowledgment`,
`ledger_named_error_ack`, `ledger_literal_type_syntax`, `handler_postfix_expr`,
the migrated `handler_two_arm` cases, `proc/handler_in_raising_pn`, `proc/ledger_containment`,
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
affected. Handler-suffix and typed-caret retirement require `make generate-grammar` and
`make test-grammar-s16`, plus production/reference-parser rejection parity;
the `T^` binding shorthand itself needs no new grammar. Run affected JS/Test262 gates if shared admission
or runtime helpers change guest-visible behavior; never mask harness failures.
No GUI/layout sweep is required solely for binding syntax edits.

Check documentation with `make check-formal-index`, link/whitespace checks, and
the applicable snippet checks when reference examples change. This plan itself
needs documentation checks, not an engine build. Performance claims require a
release build under the worktree rules; debug MIR inspection is not a timing
result. Keep all artifacts under `temp/`.

| Phase | Status | Evidence required before completion |
|---|---|---|
| P0 | Complete | Starting behavior, fixtures, caller and binding inventories are recorded below. |
| P1 | Complete | Admission/composition diagnostics pass 324/324 and the final Lambda aggregate passes 6,594/6,594. |
| P2 | Complete | M8 passed 288/288 tier/GC runs; the final host passes 12/12 integration runs, 285/285 MIR checks and 355/355 MIR GC cases. |
| P3 | Complete | Handler/fallback parsing, residual errors, chaining, results and suspension are qualified on production T0/MIR Direct. |
| P4 | Complete for production; MVP adaptation deferred by USER | Current inventory: 4,084/4,141 compile, with 46 reviewed legacy failures and 11 deferred MVP fixtures (LR01-22); no unexpected failures. Authored production callers and binding shorthand are migrated. |
| P5 | Accepted with user parity waiver, 2026-11-03 | Final Lambda 6,594/6,594, input 2,112/2,112 and Test262 40,261/40,261 pass. The user skipped full parity/manifest refresh and deferred MVP adaptation. Assertions remain intact; emission growth is reviewed under D8.6.1. |


<a id="m1-handler-outcomes"></a>

## 6. M1 — Handler outcomes and caller migration

**2026-11-02.** Worktree: `temp/error-handling-impl-20261102`, branch
`codex/error-handling-20261102`, based on `1e3ca619d`.

### Implemented

- **S7.6.1v8/S7.6.7v5:** procedure handlers preserve success and recovery
  results in bindings and final-result positions, including after suspension.
  Fault protection remains independent of result production. Fresh recovery
  errors remain values for their actual receiver.
- **S7.6.8/S7.6.9:** contiguous `or^`, named/qualified filters, and chained
  filters use shared handler representation and matching on interpreter and MIR.
  Filters preserve unmatched errors and conservatively retain their type;
  broad `error` aliases discharge all errors. Known value selectors are rejected.
- **S7.6.2v5/S10.1.7v4/S16.4.3v3:** both parsers reject the old success arm,
  including across lines. The AST and both tiers no longer store/lower it.
  The reference scanner now distinguishes handler carets from propagation so
  a handler cannot silently split into unrelated statements.
- **S7.7.4v2, admitted stores only:** a completed whole-variable assignment
  retains its admitted error instead of implicitly returning it from a fallible
  procedure. Invalid element/field stores keep their separate failure policy.
- **S10.1.3:** pipe subject detection traverses declaration/content blocks,
  retaining the existing scope barriers for nested match/pipe binders.
- Migrated **510** success-arm uses in **139** authored package/demo/test files.
  Error checks, direct recovery assignments, and explicit branching preserve
  single evaluation and lexical subjects. Mutable error captures retain their
  actual values. The author-source cleanup also uses `T^` for **9** ordinary
  outcome bindings in **7** package/demo files; explicit union-equivalence
  tests remain intact.
- Updated the error reference, current-value reference, and tutorial error
  chapter. Existing golden expectations are retained; new fixtures have paired
  goldens and negative cases are registered in the native runtime suite.

### Verification

- **40/40** focused executions: five positive fixtures on interpreter, JIT,
  automatic execution, and automatic execution with promotion threshold one,
  each with ordinary and forced/poisoned GC.
- Parser conformance: **453/453** production cases and **440/440** reference
  cases, including qualified selectors, line restrictions, and retired suffixes.
- **145/145** migrated source compile checks and **9/9** reference code-block
  compile checks. The updated tutorial chapter passes **11/11** output checks.
- Full `test-lambda-baseline`: **8,509/8,510** passed, including MIR GC stress
  **350/350** and HTTP fixtures **152/152**. The sole failure is the existing
  boxed-float snapshot case detailed below; it also fails with the saved base.
- The full tutorial currently has seven failures in unchanged chapters; all
  seven reproduce with the saved base executable and original checkout data.
  The changed chapter has no failures. No tutorial expectation was weakened.
- The base executable and this branch both reproduce the existing
  `proc/mir_mvp_roots.ls` boxed-float snapshot failure on `interp`/`auto`;
  both pass on `jit`. HTTP fixtures require loopback socket permission and pass
  **152/152** with it. These are distinguished from feature regressions.
- Integration with committed `master` (`0865507b1`) completed without conflicts.
  The merged code again passes **40/40** focused checks and **8,509/8,510**
  baseline tests, with only the same pre-existing boxed-float failure.

Evidence is under `temp/error-handling/`: `focused-tier-qualified.json`,
`changed-source-compile-qualified.json`, parser logs, tutorial/base comparison,
`base-root-alias-comparison.json`, and aggregate baseline logs. Integration
evidence: `merge-master-focused.json` and `merge-master-baseline.log`.

### Remaining after M1 (historical snapshot)

P0/P3/P4/P5 are partially completed by this milestone. P1/P2 still require
coordinated declared/effective type facts, continued deferred initializer and
reassignment outcomes, unresolved-admission warnings, inferred/`any`
acknowledgement, ordinary-error return unification, and overwritten-result
checks. **`T^E` is still accepted by the implementation**; its removal and
union-only particular-error contracts remain pending. General public validation
syntax and filtered system-fault policy still need their separate rulings.

Do not infer a clean native binding from the annotation, remove remaining
mutation checks, or mark the full plan complete from the M1 qualification.

<a id="m2m3--admission-and-ordinary-outcomes-in-progress-2026-11-02"></a>

## M2/M3 — Admission and ordinary outcomes (historical milestones, 2026-11-02–03)

The following progress entries preserve the earlier measurements and unresolved
questions at each checkpoint. Current decisions and qualification are in
[M8](#m8-qualification-boundary-2026-11-03).

**Authority:** S7.4.2v3, S7.5.1v6–S7.5.5, S7.7.2v2/S7.7.4v2,
S7.8.1–S7.8.2, SI14v3, D4.2.4, D6.2.1. This is an implementation
record, not a change to those rulings.

- Deferred local checks issue E241, bind/store the admitted value or validation
  error, and continue. The original target remains strict. Inferred receivers
  acknowledge their complete outcomes; explicit `any` is proven admission.
- Effective outcome facts reach reads, loop back edges, forward calls,
  captures and whole-binding borrow publication. MIR stores an outcome as an
  Item instead of unboxing an error through the original target's lane.
- Return contracts normalize ordinary unions and `T^` through shared helpers.
  Raised, returned, propagated and validation errors use the same admission
  checks. E228 tracks actual receivers and procedural result replacement;
  inferred array, comprehension-body and element-content slots retain errors.
- Production and reference type grammars reject `T^E`. Four authored broad
  typed-caret contracts were inventoried and migrated to `T^`; negative parser
  cases cover returns, aliases, bindings and callable types.
- Two ABI proofs required correction: duplicate return-only forward planning
  bypassed the borrowed-parameter guard, and a binding's native initializer
  was incorrectly reused as proof after deferred validation could replace it
  with an error. Both now use the same outcome facts as storage lowering.

Focused qualification so far: 48/48 outcome executions across interp/jit/auto,
with ordinary and forced/poisoned GC; 231/231 diagnostic/boundary cases; the
procedural containment fixture now checks continued declarations/reassignments,
ordinary child-task completion, and unchanged container-store containment on
all three tiers. The grammar gate passed C 461/461 and Tree-sitter 448/448.

**Remaining:** finish caller migration, run the required aggregate gate, review
all changed examples/references, and update the formal implementation footnotes
from the final qualification. The first broad run exposed many shared-package
import cascades and legacy assertions about declaration skipping. It is not a
passing qualification. Successful-value contracts remain strict; helpers that already let ordinary
errors escape need to recover them or declare the error outcome. Loop-source/predicate migrations
make their existing empty-source/excluded-row recovery explicit. General
flow-sensitive narrowing remains the separate SO9 question.

### Follow-ups from the source audit

S7.7.3 call-site admission now contributes an effective error alternative,
including deferred checks, forward resolution and piped arguments, without
widening the callee's declared signature. An interior failed binding whose
outcome is unused does not itself make the function's result fallible.
Task launches retain their separate packed-array validation route.

S7.5.1v6/S11.4.3 apply consistently to explicit `any` parameters and container
fields, including nested callback composition. The actual callback either
admits an argument error or returns its admission failure to the call's
receiver. Handler recovery retains its own acknowledgement obligation even
when its outcome normalizes to `any` (S7.6.1v9/S7.6.9).

S7.9.3 conversions preserve incoming errors, including `u64` and sized numeric
conversions. Conversion targets come from the callee rather than the widened
outcome type. Event handlers validate results against their own procedure
activation, while an earlier overwritten outcome still needs acknowledgement.
Immutable aliases and captures retain errors exposed by loop back edges
(SI14v3); these facts also prevent unsafe JIT unboxing at later calls.

The expanded diagnostic gate passes **237/237** cases. The focused matrix has
**88 executions** across interpreter, JIT, automatic and immediate-promotion
modes, each with ordinary and forced/poisoned GC. Final qualification of the
latest admission-equivalence case and the package/demo migration is still in
progress. Current package edits disclose existing error outcomes and make loop
source/predicate recovery explicit; they are not yet aggregate-qualified.

S7.6.3 early propagated errors now remain in inferred procedure outcomes after
success-type narrowing, and explicit return validation checks them against the
complete contract. The 88-execution focused matrix and 237-case diagnostics
pass with this follow-up. Incompatible normal alternatives in an error-admitting
return produce a type mismatch, rather than misleading error-exclusion help.
View-template return annotations were parsed but dropped. S12.1.3v2 and
S11.4.3 already settle the default: the body is a pure function with a non-error
unannotated return. Explicit view contracts are now retained and enforced, without a new
inferred-return exception.

Shared admission proofs now avoid spurious outcome alternatives for proven
array contracts and homogeneous text concatenation (S7.7.2v2/S10.6.1). The
`or^` success type excludes null inside unions and optional types (S7.6.8).
Strict local-binding regressions cover these proofs. Seven migrated procedural
fixtures retain their existing goldens in **42/42** runs over all three tiers,
with ordinary and forced/poisoned GC. Committed master `0452aaabc` is integrated;
its separate fn-local-control rulings remain outside this error-handling milestone.

### Outcome-evidence qualification (2026-11-02)

S7.5.1v6/S7.5.4 now retain actual producer evidence when an ordinary-error
alternative normalizes to `any`. Recovery returning `^.message` is clean;
S7.4.4's `.source` retains its optional error. Unknown callable admission and
bare callable pipes use the ordinary call path (S7.7.3/S10.1.2v4). Return
checks preserve the same evidence instead of silently accepting an open join.
Shared array/text admission proofs and `or^` null removal avoid false errors.
The diagnostic gate passes **239/239** and the focused tier/GC matrix passes
**96/96**. These results qualify the core changes, not the ongoing source
migration or the required aggregate gate. Legacy negative admission fixtures
now explicitly propagate their bound validation error to exercise runtime
rejection after implicit declaration exits were removed.

S7.7.2v2/D2.5.3: a deferred record receiver now contributes its actual
member-read alternatives, including metadata recovery. The shared read oracle
also refreshes loop-carried receiver outcomes. This fixes a reproduced tier
disagreement where an error-valued field was decoded as a clean JIT integer.
S7.6.3v3 now distinguishes defensive carets on inferred clean results (warning)
from explicitly error-free contracts (error). Mapping sequence bodies retain
outcomes in result slots; scalar/open mapping preserves the ordinary obligation
(S10.1.2v4/S7.8.2). The diagnostic gate passes **241/241** and the focused
matrix passes **112/112**. The updated function/error and procedure tutorials
pass **11/11** and **13/13** output checks; the error reference has **10/10**
clean extracted code units. Source migration and aggregate qualification remain
in progress. Only current direct-module diagnostics drive the migration;
importer snapshots are provisional until the final full recheck.


### View and strict-admission qualification

S12.1.3v2/S11.4.3 apply the existing pure-function return contract to view
bodies. Both parsers retain annotations, the shared frontend validates them,
and T0/MIR apply admission and success coercion at the view activation's
return. Event handlers keep their separate procedure receiver. Explicit broad
and ordinary-union outcomes, early propagation, deferred failures and normal
numeric coercion are covered. No fn-local control extension is claimed.

S7.5.1v6 producer facts now survive nested argument admission and remain tied
to the read at that program point; a later write cannot retroactively make an
earlier clean read fallible. S7.7.2v2 rejects a statically known whole-array
error at a clean array admission, keeping that distinct from an array whose
items may be errors. Failed whole-variable assignments report expected and
actual types as well as the receiver name.

The diagnostic gate passes **244/244**, and the focused tier/GC matrix passes
**120/120**. Caller migration includes MIR and standard-library fixtures.
The last aggregate run still failed (**4489/5501**), before these latest core
fixes and migrations; it is evidence of remaining work, not qualification.
Compile-success importer snapshots also require a final full recheck after
shared packages settle. MIR operation and budget checks must retain their
original assertions while fixtures explicitly discharge fallible inputs.


S7.5.4 now also carries a computed callee's error to the call's receiver.
Both tiers return that original error before argument evaluation; the outer
call retains its error alternative, including through method syntax. Known
record field-domain inclusion uses the maintained subtype relation for
admission (S7.7.2v2). JIT field construction uses the shared admission proof,
so float-to-int narrowing still validates instead of silently truncating.
These follow-ups pass **245/245** diagnostic cases and **120/120** tier/GC
executions. The migration review passes **224/279** MIR emission checks;
remaining source/operation failures are still being resolved. The migrated
cold-exit fixture keeps three admission calls, with declaration validation
before arithmetic at its explicit propagation point, and now checks the
out-of-range error result as well.


S7.5.1v6/S7.6.3v3 now apply to default expressions: an explicit
error-admitting parameter receives the default outcome, and postfix propagation
uses the enclosing callable's own return contract. A clean return still rejects
that propagation. S10.6.1's concrete numeric/text concatenation also proves a
string result, avoiding an unnecessary deferred admission on native string
accumulators. The diagnostic gate passes **246/246** and focused tier/GC
executions **120/120**. Reusing a destination constructor's live certificate
restores native array consumers without repeating representation guards
(D4.3.3). The latest MIR migration review passes **246/279** checks.

Migration preserves annotation contracts at mutation boundaries: a deferred
record or array admission can be retained in a checked binding and propagated
before creating the mutable receiver. An open error-admitting field may require
a checked copy and publication back to the owner under S9.1.2; it cannot be
assumed clean merely because the outer record was admitted. These cases also
retain their COW snapshot goldens. General validation-expression surface syntax
and flow-sensitive narrowing remain separate design work.


### Deferred return admission (2026-11-03)

A reproduced S11.4.1v5/S7.5.4 gap let a deferred function-return failure reach
an interpreter binding while JIT exited the caller. Return validation now
contributes its ordinary checked outcome to call inference, acknowledgement and
storage planning, without widening the successful-value contract. A clean
receiver rejects that known fallible call; inferred and error-admitting
receivers retain it and continue. The JIT's explicit-return pair shortcut now
requires redundant admission before forwarding, so it cannot publish a value
that the declared return must reject. Single produced values in functional
blocks reuse the shared declaration/no-result classifier (S2.5.4v3), while
an open recursive inference alone remains no ordinary-error proof.

The diagnostic gate passes **247/247** and the expanded tier/GC matrix passes
**128/128**, covering array, scalar, boolean and explicit procedural returns,
whole-variable writes, forwarding and recovery. New negative cases check clean
binding rejection, overwritten outcomes and return-validation diagnostic help.
The latest MIR review is **232/279**; newly visible checked-return outcomes
require further caller and native-proof migration. This supersedes the earlier
248-case MIR progress snapshot and is not aggregate qualification.

Broad top unions (`any^`, `any | error`) reuse the maintained top normalization
before undecidable admission is diagnosed; explicit-any equivalence produces
no E241. Tutorial chapters 02 and 03 retain their outputs after explicit
propagation at fallible display calls (**13/13**, **11/11**). Shared renderer
migration and the required aggregate checks remain unfinished.

### Recursive return-contract proofs (2026-11-03)

S11.4.1v5 admission effects exposed late header publication: a self or mutual
call could see `any` instead of its written result, manufacture a deferred
return effect, and reject otherwise valid recursion. Return contracts now
publish before body resolution. Closed forward return slots use the module
type environment; parameter names, binders, defaults and value-dependent
aliases keep their declaration scope. One shared helper publishes the contract
for early headers and completed bodies.

S2.5.4v3 single-result proofs now pass through nested functional branch blocks
without changing their execution carriers. S5.1.1v2/S5.1.2v2 equality against
`null` checks direct errors but does not inspect container children. The
expanded return-outcome fixture covers simple and braced recursion, mutual
recursion through a later type alias, a dependent binder shadowing a module
alias, and a null comparison containing a nested error. The focused matrix
passes **128/128**; factorial, mutual recursion and both tail-call MIR guards
pass **4/4**. The diagnostic gate passes **247/247**. An initial concurrent
run reproduced the previously observed mutual-stack-overflow `E0` instability;
its isolated repeat and the subsequent full diagnostic run pass.

The full source audit is **2,847/3,772** compiled, with **925** requiring
review. This is migration evidence, not qualification; shared package failures
still cascade into importers. No MIR expectation or budget is relaxed.

### View state propagation (2026-11-03)

S12.1.3v2/S7.6.3v3 view state setup now checks explicit propagation against
the view's own return contract. The previous checker lost that context while
visiting state initializers; it rejected existing `lower_slots(...)^` setup
even on an error-admitting view. Setup propagation validates its error against
the complete return contract and bypasses rendering. T0 now admits an explicit
setup return before completing the activation, matching the existing MIR
view return context. State-slot receiving policy is unchanged.

The view-outcome fixture covers successful setup and an original error returned
before a body that would produce `999`; a paired negative rejects setup
propagation through a clean result. Diagnostic checks pass **247/247** and
the tier/GC matrix passes **128/128**. Reference/production parser conformance
passes **448/448**, **461/461**. After caller migration passes 37–40, a full
audit compiles **3,355/3,772** authored non-negative sources; **417** remain for
review. MIR checks pass **245/279**: remaining native-proof regressions and a
string-builder representation failure still require root-cause work. These
figures do not replace the required aggregate qualification.

### Outcome carriers and compiler lifetime qualification (2026-11-03)

Three reproduced failures were fixed at their owning boundaries:

- S7.8.1/D6.2.1 native tail joins reuse return admission before a boxed
  outcome enters a scalar or pointer success lane. The string-builder fixture
  now checks an original recursive error as well as successful nested calls.
- D2.3.4–D2.3.5 call producers publish the actual post-conversion carrier.
  Explicit propagation checks an Item outcome; it does not decode a proven
  native integer as an Item. The latter crashed the native leaf/division case.
- D8.2.2 place-copy diagnostics belong to the compilation. Late MIR argument
  validation had appended pool-owned names to a global queue after the normal
  frontend flush; a subsequent batch compile followed freed names. The queue
  is now a `Transpiler` field, preserving its pool lifetime and import isolation.

The diagnostic gate passes **247/247**, the focused tier/GC matrix **128/128**,
and the extended string-builder case **8/8**. The full MIR GC corpus passes
**350/350**, including the original crashing multi-script batches. Existing
MIR emission checks pass **252/279**; no assertion or ratchet budget is relaxed.
Original strict MIR return contracts are restored where the current compiler
proves them; necessary error outcomes remain explicit. Caller fixes retain a
deliberately stored error through an inferred binding, disclose a fallible
constrained-array constructor, and admit a record before mutation.

The preceding aggregate run passed **5,006/5,509** Lambda checks and
**2,112/2,112** input checks (**503** total failures), before these carrier and
lifetime fixes. Its results are retained under
`temp/error-handling/m3-baseline-after-return-migration/`. The last full source
audit compiled **3,444/3,772** sources (**328** requiring review), before the
latest caller fixes and restored contracts. These figures are intermediate;
the aggregate, remaining native-proof checks and renderer/demo migration are
still outstanding.


### Scalar outcomes and numeric return carriers (2026-11-03)

S7.4.2v3 ordinary error unions reuse scalar identity-admission proofs on each
outcome. This removes redundant checks without skipping numeric conversions,
literal checks or container reification. S11.4.5 plain `float` return admission
widens an integer through the numeric conversion helper. Current interpreter union
admission preserves an already-conforming member; `float^` therefore cannot
promise a physical float result from an integer or mixed-kind body. The native
return proof now checks the actual normal kind across branches and explicit
returns, retaining the Item ABI when necessary (D2.3.4–D2.3.5). Skipping a
redundant check still routes an admitted error through the native error lane
(D6.2.1).

The return-outcome fixture checks plain float conversion, union member-kind
preservation through return and propagation, mixed numeric branches, and
forwarded explicit native errors. Diagnostic checks pass **247/247**, focused
tier/GC checks **128/128**, and the full MIR GC gate **350/350**. Existing MIR
emission checks improve to **255/279** with no relaxed assertions. Evidence:
`temp/error-handling/m3-exact-union-final-{diag,gc,mir}.json` and
`m3-return-numeric-conversion-focused.log`. The incremental caller audit is
**3,541/3,772** compiled; a final full audit and aggregate qualification remain
required.


### Optimized constructor outcomes (2026-11-03)

S7.7.2v2 also applies when a destination constructs an admitted value directly.
Three MIR paths still completed the activation on failure: an empty counted
array, a float literal containing an absent read, and a record literal with an
invalid dynamic field. Empty and float-array construction now publish the
admission outcome. Record construction reuses the call-admission join, shared
through `MirAdmissionGuard`; only the destination checks enter that join.
Field evaluation and procedural stores retain their own existing boundaries.
The same join prevents a failed optimized fill from escaping its receiving
initializer.

The admission fixture now checks continuation and error retention for all
three constructors, plus a failed empty-array return. Its tier/GC matrix
passes **128/128**, diagnostics **247/247**, extended native tails **8/8**,
and the full MIR GC corpus **350/350**. Current MIR emission checks pass
**263/279**, including source migration that explicitly propagates deferred
validation outcomes before typed mutation. Assertions remain unchanged.
Evidence: `temp/error-handling/m3-construction-joins-{diag,mir-final,gc-final}.json`
and `m3-construction-owned-checks-{focused,tail}.log`. Caller migration and
aggregate qualification remain open.


### Completed-block bridges and migration cleanup (2026-11-03)

S12.1.2v3 explicit completion produces no subsequent normal value. MIR lowering
may leave bridge instructions to satisfy structural value demands, including
an unreachable null after `return`. A final linear cleanup removes instructions
between an unconditional jump/return and the next label. Root/reload analysis
and witness emission consume their instruction records first; the cleanup does
not invalidate those records while they are in use. The required-field guard
again observes the actual field loads without the unrelated dead null.

Diagnostic checks pass **247/247**, the focused matrix **128/128**, and full
MIR GC **350/350**. Existing MIR emission checks pass **264/279**, unchanged.
Evidence: `temp/error-handling/m3-unreachable-bridges-{diag,gc,mir-full}.json`.
A full caller audit compiles **3,649/3,772** sources. Cleanup removes **67**
redundant propagation operators and **40** proven-unnecessary staging bindings.
The legacy rejected-error-flow fixture now retains errors in admitted `T^`
bindings and reassignments, with matching interpreter/JIT output. Remaining
caller fixes and aggregate qualification are still open.

The error-union normal-admission interaction is an outstanding design question,
not a settled exception to S7.4.2v3 or S11.4.5. Probes exposed integer
kind changes through `float^` propagation and an incorrect reordered-record
field read after propagation; the native representation fixes below now preserve
both successful values. The user has been asked whether error unions
apply T's normal conversion/reification or preserve matching members. No
runtime admission rule changes while that decision is pending.


### Closed empty-array admission evidence (2026-11-03)

S11.1.1v3 uncounted homogeneous arrays admit an empty literal without an
element check. Shared AST admission analysis retains that expression evidence
for declarations, arguments, explicit returns and defect summaries. The proof
does not remove runtime representation admission (D4.2.5), infer emptiness
from a mutable binding's historical length, or bypass counted/constrained
contracts. A definitely completed procedure is checked at its owned return
sites; its aggregate inferred type is not a second runtime boundary that can
erase the literal proof and introduce a spurious error outcome.

The return-outcome fixture constructs independent typed integer and boolean
arrays, then passes them to borrowed parameters without unnecessary result
propagation. Diagnostic checks pass **247/247**, the focused tier/GC matrix
**128/128**, and full MIR GC **350/350**. Existing MIR checks remain
**264/279** with all assertions retained. Evidence:
`temp/error-handling/m3-empty-array-proof-{diag,gc,mir}.json` and
`m3-empty-array-proof-focused.log`. Caller migration remains in progress;
the incremental inventory compiles **3,741/3,772** and needs a final full audit.


### Propagation representation and absent return checks (2026-11-03)

S7.6.3v3 propagation removes an error outcome without admitting the successful
value again. MIR keeps a propagated Item's actual numeric kind and derives
record-layout/array-lane proofs from the complete enforced producer contract,
not its error-stripped semantic type. An immutable array without a live physical
element witness uses the checked reader. Predicate-only values retain their
boolean proof through propagation, without general truthiness calls. These
changes preserve current union admission; they do not decide the pending
normal-conversion/reification policy.

D2.5.3/S11.4.1v5 strict return admission also rejects absent array reads. The
origination solver previously mistook a nullable native lane for proof that
this admission could not fail. It now distinguishes the physical carrier from
non-null membership, reserving the return error channel instead of publishing
an error Item through a numeric lane. The return-outcome fixture covers missing
integer, float and boolean elements as well as preserved scalar, record and
array members through propagation. The focused tier/GC matrix passes
**128/128**. Aggregate qualification is in progress; its native diagnostic
runner passes **321/321**, full MIR GC **350/350**, and MIR emission remains
**264/279**, with all assertions unchanged.


### Rebinding representation and legacy fixture admission (2026-11-03)

D4.3.3/S7.7.4v2 typed array reassignment retains its representation
admission even when the source's element type already proves membership.
Previously `values = fill(3, 9)^` skipped certification and the next checked
borrowed element write rejected the valid array. The borrowed-outcome fixture
now replaces a typed array, writes an element, and checks its earlier snapshot.

S11.4.6v3 existing-error preservation is shared between runtime admission and
MIR return joins: plain targets retain prior diagnostics, broad error targets
admit them, and particular error targets retain their validation. The compact
bitwise return guard now forwards a prior error without repeating admission.
Diagnostics pass **247/247**, focused tier/GC checks **128/128**, full MIR GC
**350/350**, and unchanged MIR emission checks **265/279**. Evidence:
`temp/error-handling/m3-array-rebind-admission-{diag,gc,mir}.json`.

Two legacy negative declaration tests now assert ordinary error retention and
continuation on all three execution tiers. Their former expectation of a CLI
failure depended on the retired implicit block exit. Intentional failed-store
caller probes recover explicitly with handlers so later checks still execute.
The fresh aggregate preceding this cohort passed **5,466/5,518** Lambda and
**2,112/2,112** input checks; **52** failures include caller updates, compiler
proofs and unrelated/environmental cases still under review. Its preserved
artifacts are under `temp/error-handling/m3-baseline-qualified-current/`.


### Snapshot observers through outcomes (2026-11-03)

S7.6.3v3/S9.1.2 propagation preserves the container's existing observers.
Shared AST ownership analysis now follows successful `^` values for ownership,
alias capture and insertion, and recognizes mutable members behind outcome
and occurrence types. Interpreter planning and MIR binding analysis use the
same value-source helper. MIR also records the runtime capture on the original
source binding; otherwise a later native writer could bypass the share bit.
The interpreter's borrowed-parameter capture mask follows the same source.

The snapshot-outcome fixture covers admitted roots, error-union snapshots,
propagated aliases, nested child snapshots, a replaced borrowed binding, and
ordinary error/type values passing these paths. The expanded tier/GC matrix
passes **136/136**, diagnostics **247/247**, full MIR GC **350/350**, and MIR
emission **265/279** without relaxed assertions. The formerly failing typed
path and scalar-child snapshot goldens now match on both tiers; the read-handle
snapshot also matches after MIR's source-capture bookkeeping follows `^`.
Evidence: `temp/error-handling/m3-outcome-snapshot-{qualified-diag,final-gc,qualified-mir}.json`,
`m3-outcome-snapshot-qualified-focused.log`, and `outcome-handle-qualified.out`.
A full runtime-golden audit found additional legacy differences and regressions
that remain under review; GC consistency alone does not qualify their values.

### Explicit propagation and constructor call lanes (2026-11-03)

S7.6.3v3/D2.5.3 explicit propagation now contributes its early-completion
error lane even when a callable's final normal result is scalar. A propagated
C-ABI Item result keeps its physical Item descriptor until the error check;
this repairs the native WFC failure path without forcing clean native math
producers through boxing. `proc/error_propagation_lanes` covers unary,
condition, direct-call and system-call propagation followed by scalar returns.

S7.7.2v2/S7.7.3 optimized nullable float literal admission now joins a rejected
member into the owning expression before publishing a successful array witness.
Both an initializer and a call retain that failure and continue; the admission
return fixture covers both receivers. S9.1.2 fresh-array ownership also survives
an error alternative on join operands. This restores later snapshot captures
on both tiers; the snapshot fixture now includes typed array concatenation.
Boolean outcome joins use their actual boolean domain for predicate admission
(S3.4), retaining the existing MIR predicate guards.

Qualification: focused tier/GC outcomes **144/144**, diagnostics **247/247**,
Procart noise/WFC checks **3/3**, MIR emission **266/279**. Full MIR GC initially
passed 349/350: the remaining bool-fill fixture relied on continuing after a
failed propagated fill. It now retains and inspects the two failed call outcomes,
with its focused GC check passing. The boxed-predicate and discarded-call
fixtures likewise preserve intended local recovery instead of adding `^` early
exits. Broad qualification and the outstanding native proof regressions remain
open. Evidence: `temp/error-handling/m3-propagation-final-diag.json`,
`m3-join-owner-focused.log`, `m3-join-owner-mir.json`,
`m3-propagation-wfc.json`, and `m3-bool-fill-propagation-gc.json`.

### Authored caller migration checkpoint (2026-11-03)

The remaining authored callers have been migrated to S7.5.1v6 acknowledgement,
S7.6.3v3 explicit propagation and S7.7.2v2/S7.7.4v2 retained boundary outcomes.
This includes packages, demos, benchmarks, tutorial examples, test modules and
embedded native-test source. Known failed stores keep explicit local recovery;
checked native accumulators stage admission before propagating so a nullable
source cannot silently enter a strict lane. Redundant propagation chains were
collapsed. The math corpus generator now acknowledges its generated formatting
call. These are source migrations; native optimization guards and budgets are
unchanged.

The fresh compile inventory reports **3,760/3,775** non-negative authored files,
with no timeouts. The remaining 15 are individually recorded in
`temp/error-handling/m3-source-compile-reviewed.json`: schema/parser and duplicate
name failures, a retired Python import, an invalid input arity, the established
RSA branch-shape failure, and WIP modules requiring further review. This count
is not a claim that all remaining failures are pre-existing. Aggregate runtime
and optimization qualification is still in progress.

### Method receiver publication on completion (2026-11-03)

S7.4.6v3/S12.1.2v3 completion preserves mutations already performed by a
method. MIR previously published mutable field locals only on fallthrough;
an explicit return or propagated/raised error skipped the receiver write-back.
Publication now runs at the common return epilogue before root cleanup. A
rejected publication exits that epilogue rather than re-entering its return
label. The existing field publication loop is shared by all completion paths.

The propagation fixture now checks normal return, propagation, returned error
and raise after a field update on interp/jit/auto and immediate promotion,
including forced/poisoned GC. The matrix passes **144/144**. The Counter API
fixture spells its numeric method results with explicit `return count`, and
its interpreter/JIT goldens match. The aggregate full MIR GC check passes
**350/350**; loopback-enabled HTTP checks pass **152/152**. The rebuilt aggregate
reports **8,524/8,554** Lambda checks and **2,112/2,112** input checks. Its 30
failures comprise 13 MIR proof checks, six native optimization checks, four MIR
size ratchets, four sandbox-blocked HTTP checks (passing separately), and three
main-runtime cases. The main runtime now passes **1,995/1,998**; its remaining
cases are the isolated MIR accumulator defect and the graphviz/MVP differences
still being compared against the base. Artifacts are frozen under
`temp/error-handling/m3-method-qualified-baseline/`; the aggregate log is
`m3-method-qualified-baseline.log`.

A separate accumulator regression has been reproduced in standalone valid MIR
with no Lambda runtime: levels 0/1 compute the expected result, while level 2
does not. The isolated source and runner are under
`temp/error-handling/loop-standalone.{mir,c,exe}`. No vendored code has changed;
AGENTS.md rule 16 approval is pending before a MIR fix and auditable patch.

### Broad Item inlining and sized admission (2026-11-03)

S7.4.2v3/D2.5.3 inlining now distinguishes a broad `any` return contract,
whose existing Item join admits both outcomes, from a narrower result contract
that needs its return firewall. The legacy `can_raise` rejection had disabled
safe broad-Item leaves after ordinary-error unification. Existing store-inline
and scalar-home MIR guards now pass, without changed expectations.

The u32 range boundary already returns an admitted Item or rich error. It now
serves a retaining initializer too (S7.7.2v2); the producer still must explicitly
prove an IntLane. The focused fixture checks zero, the u32 maximum and both
adjacent rejected domains, alongside a rejected inlined store whose caller
continues. The tier/GC matrix passes **144/144** and MIR emission **268/279**.
The u32 emission fixture still records an additional return-admission check;
its range-check proof is restored, and that remaining assertion is retained.

Two WIP modules that compiled at the base now disclose their ordinary outcomes
and propagate at pipeline/iteration boundaries. The full authored inventory
passes **3,763/3,776**, with its remaining 13 failures reproduced by the base
compiler. Evidence: `temp/error-handling/m3-item-inline-u32-{focused.log,mir.json}`,
`m3-item-qualified-source.log`, and `m3-base-remaining-source-review.json`.
Radiant qualification was interrupted when sandbox-denied macOS GUI services
made native cases stall; artifacts and the reason are preserved under
`temp/error-handling/m3-radiant-sandbox-interrupted/` for an unsandboxed rerun.

### Method resolution through error alternatives (2026-11-03)

S7.5.4/S12.3.3v2 method lookup now uses the successful receiver domain,
unwrapping a named type after removing its error alternative. The actual
receiver expression and its complete outcome still reach the selected call.
Both user-member precedence and builtin eligibility use that domain. The old
TypeId-only lookup rejected a `string | error` receiver, leaving a dynamic
member call that returned a payload-less error; Graphviz alignment values
therefore became errors after ordinary-outcome inference was enabled.

The new method fixture covers a shadowed builtin name, converted string
receivers, builtin chaining, preserved error payloads, user methods, function
fields, and a procedural method whose body runs only for a successful receiver.
Dynamic `var`-parameter dispatch remains the deliberate S12.3.2 restriction;
this change does not introduce an address transport for that call form.
Qualification: **152/152** focused tier/GC executions, **247/247** diagnostics,
**350/350** MIR GC cases, **268/279** MIR emission cases, and the Graphviz
HTML-interaction golden passes. Evidence:
`temp/error-handling/m3-method-receiver-qualified-{focused.log,gc.json,mir.json}`,
`m3-method-receiver-diag.json`, and `m3-graphviz-method-outcomes.json`.

### Explicitly propagated scalar-array results (2026-11-03)

S7.6.3v3/S7.7.2v2 scalar-array result elision now admits an explicitly
propagated call. An ordinary fallible initializer remains outside this plan,
since its cold arm must retain the outcome rather than exit the callable.
Previously the eligibility check rejected `^`, disabling the existing valid
fast arm after caller migration. The fixed-read result guard and its complete
runtime golden now pass, including a short-source failure followed by caller
continuation. Full MIR emission passes **269/279** without changed assertions;
the focused matrix remains **152/152**.

The runtime-golden audit passes 252/262 before one final caller correction:
`checked_rebind` intended to recover an invalid numeric conversion, so it now
uses `or^ { 5 }` (S7.6.8) before its strict assignment. Its unchanged golden and
MIR guard pass on interp and JIT. The previous `or` preserved the error under
S3.4, so adding propagation had completed the function instead of recovering.
The remaining audit differences have base-compiler comparisons recorded in
`temp/error-handling/m3-base-mir-runtime-review.json`; they are not hidden by
GC consistency checks. Evidence: `m3-propagated-array-qualified-{mir.json,runtime.log}`,
`m3-propagated-array-rvo-focused.log`, and `m3-rebind-fallback-mir.json`.

### Retained local checks in return analysis (2026-11-03)

S7.7.2v2/S7.7.4v2 retained initializer and reassignment failures no longer
count as activation exits in MIR's return/deferred-boundary analysis. Their
values remain subject to ordinary return-value analysis, and RHS propagation
is visited independently (S7.6.3v3). Unretained checks and call edges stay
conservative. The old implicit-exit commentary was removed from this analysis;
its historical rationale remains in the design document's legacy appendix.

`local_admission_return_lanes` verifies single native result lanes for local
checks followed by constant returns, while also checking actual returned and
propagated errors. Its precise ABI check fails before this fix and passes
after it. The expanded focused matrix passes **160/160**, MIR GC **351/351**,
and MIR emission **270/280**. Native optimization checks remain **49/55**;
no assertions or budgets changed. Evidence:
`temp/error-handling/m3-local-admission-lanes-before.json` and
`m3-retained-local-analysis-{focused.log,mir.json,gc.json,opt.json}`.

### Boxed-entry array proof parity (2026-11-03)

D4.3.3 full array admission now leaves the same contract proof and refreshed
header cache in a boxed body as in a native-witness body. Selecting the boxed
ABI for an ordinary error-union result had lost that proof. Borrowed roots
retain their ownership checks, and var-home publication reloads detached
headers; bindings whose deferred reassignments may publish errors still use
Item storage (S7.7.4v2). No union conversion policy changes.

The existing `tune26_var_call_contract` guard now passes without a repeated
`item_at` fallback. Focused tier/GC executions pass **160/160**, MIR GC
**351/351**, and MIR emission **271/280**, with unchanged expectations.
Evidence: `temp/error-handling/m3-boxed-array-proof-{focused.log,mir.json,gc.json}`.
The nine remaining emission guards all pass on the base compiler with their
original sources (`m3-base-mir-proof-review.json`); their current differences
remain under review.

### Embedded DevTools REPL acknowledgement (2026-11-03)

S7.5.1v6 also applies to Lambda source embedded in host-language tests. The
DevTools retained-value limit check now explicitly propagates its final
`len(large)` outcome after releasing inspector references. This preserves the
existing binding-retention assertion while acknowledging the potentially
fallible call. The full local inspector protocol suite passes **25/25** with
local socket access (`temp/error-handling/m3-devtools-acknowledgement.log`).

### Forwarded borrowed-array outcomes and fresh aggregate (2026-11-03)

S7.7.4v2/SI14v3 coverage now includes an error published through two `var
int[]` calls, a retained caller snapshot, and a later valid replacement. The
caller observes the error, and its invalid numeric index read yields null
(S7.1.1v3), without reading a stale packed-array header. The focused tier/GC
matrix passes **160/160** (`m3-forwarded-array-outcome-focused.log`).

The rebuilt Lambda runner passes **6,424/6,445**, including **1,997/1,999**
main runtime checks, **321/321** error checks, **50/50** REPL checks, **351/351**
GC cases, and **152/152** HTTP cases. Its 21 failures are nine MIR emission
guards, six optimization guards, four MIR size ratchets, the isolated MIR
optimizer defect awaiting vendor approval, and the base-confirmed MVP alias
difference. The required five input executables pass: WPT 364, Markdown 1,343,
YAML 391 corpus cases, ASCII math 3 and LaTeX math 11, totaling **2,112/2,112**.
Evidence: `m3-boxed-proof-lambda/`, `m3-required-input-*.log`. The direct MIR
runtime-golden audit passes **254/263**; all nine remaining differences have
base comparisons (`m3-base-mir-runtime-review.json`). No goldens or budgets
were relaxed.

The broader input-runner selection also attempted binaries outside the built
baseline and cannot serve as qualification: it reports absent executables and
12 document-format round-trip differences (AsciiDoc/man/Typst). Its artifacts
remain in `m3-boxed-proof-input/`; the required input counts above come from
the actual five baseline commands, not that incomplete selection.

### Expanded caller inventory (2026-11-03, qualification in progress)

The original authored inventory covered 3,778 scripts, passing 3,765 with 13
base failures. A full tracked-file comparison found 295 additional non-negative
`.ls` scripts, including reactive UI, scene, map and utility sources. Initially
185/295 compiled; a base/original-source audit identified **77 newly failing
sources** after completing its shared demo import tree. This exposed 64 UI scenarios that failed during compilation and
shared scene/utility dependencies missed by the earlier directory selection.

The reviewed migration changes 53 files. S7.4.2v3/S7.5.1v6 error-admitting
function/view contracts disclose existing outcomes; S7.6.3v3 propagation covers
conditions, iteration sources, filters and mutation operands. Successful values,
UI assertions and output goldens remain unchanged. The expanded group now
compiles **262/295**, with no new failures relative to the base. The 33 remaining
failures also fail at the base. Details and the original-source comparisons are
in `m3-supplemental-{source.json,base-source.json,base-complete.log,applied-source.log}`; the
reviewed file list is `m3-supplemental-migrated-files.json`. The combined
non-negative inventory is **4,027/4,073**, with 46 base failures.

The map gate recovers from 17/20 to **20/20**. The utility architecture self-test
passes on interp and JIT, and the SHA-256 `abc` vector matches on both tiers
under forced/poison GC. The 64 UI scenarios that failed compilation now pass (54 baseline and ten theme/timeline scenarios, with the two Blade cases rerun after the shared view correction). All ten scene cases pass. A second embedded-source caller, the native UTF-8 serialization formatter, now explicitly propagates its outcome; its original assertions pass. Evidence: `m3-ui-acknowledgement-{rerun,themes}.json`, `m3-blade-gallery-acknowledgement.json`, `m3-native-view-caller-rerun.json`, `m3-native-view-embedded-rerun.json` and `m3-supplemental-map.json`. The original UI aggregate still has 26 runtime failures under review, including three process aborts.
The pre-migration GUI-enabled aggregate completed, including text/query
438/438, scene units 378/378, DOM 165/165, page loads 111/111 and snapshots
51/51. Its UI/native-view failures remain subject to the caller reruns and
separate crash review; the aggregate is not reported as green.

### Compact integer proof under a broad error union (2026-11-03)

S7.4.2v3/S4.4.4 the existing compact-u32-to-int success proof now recognizes
an `int | error` destination that preserves the complete error outcome. The
normal arm must still be the plain int domain. Particular-error destinations,
nullable/literal domains and other normal conversions retain their firewalls.
This removes a redundant return check introduced when the SHA-style fixture
disclosed its ordinary errors; it does not choose the open union-reification
policy. The existing exact check counts for `tune21_u32_decl_lane` pass.

Qualification: **272/280** MIR emission checks, **160/160** focused tier/GC
executions, **351/351** full MIR GC cases, and **247/247** diagnostic/binder
checks. No assertions or budgets changed. Evidence:
`temp/error-handling/m3-u32-error-union-proof-{mir.json,focused.log,gc.json,diagnostics.json}`.

The three slide abort scenarios reproduce on three isolated current runs.
The original compiler/source/asset tree also aborts in the animation scenario
on all three runs, while home-actions and media-insertion fail their UI
assertions without aborting. The current home-actions debugger trace stops at
`state_store_caret_move` requiring a laid-out ViewText from a DOM text position.
The original animation backtrace is being compared before attributing or
expanding the host caret issue. Artifacts:
`m3-slide-abort-isolated-{1,2,3}.json`, `m3-slide-abort-base-{1,2,3}.json`,
`m3-slide-home-backtrace.log`. These UI failures remain open qualification
residue; their assertions are not suppressed.

### Broad container outcome proofs (2026-11-03)

S11.4.10/S7.4.2v3: broad `array | error` and `map | error` membership can
reuse a callee's complete enforced return contract. This proof uses semantic
subtyping, including independently allocated equivalent unions. Propagating an
immutable local's retained admission outcome also preserves its already-proven
broad normal container. Refined arrays and records still require their own
representation proof; no error union is narrowed at a receiving boundary.

The array-chain emission fixture now explicitly propagates each fallible
rotation (S7.6.3v3). This preserves its intended success-only loop state rather
than asking inferred reassignment to discard an error. Its golden and MIR
assertions are unchanged.

Qualification: **274/280** MIR emission checks, **160/160** focused tier/GC
executions, **351/351** full MIR GC cases, and **247/247** diagnostic/binder
checks. Evidence: `temp/error-handling/m3-broad-container-outcome-proof-{mir.json,focused.log,gc.json,diagnostics.json}`.

### Propagated fill membership and boundary ordering (2026-11-03)

S7.6.3v3/S11.4.6v3: the shared AST admission classifier now proves an
uncounted rank-one array's element membership after a direct propagated
`fill(count, numeric_literal)`. It reuses the existing literal-syntax and
element-admission classifiers. This is a membership proof, not a storage
certificate; runtime admission still reifies the carrier. Counted contracts,
arbitrary typed reads and other dynamic fillers keep their deferred checks.
The typed owner-cycle fixture can consequently retain `b = saved` without an
unnecessary propagation, and its existing no-share-mark assertion passes.

The new `admission_fill_outcomes` regression exposed a separate JIT ordering
bug: fused counted-fill construction applied destination admission inside
the propagated producer. A length mismatch therefore exited the activation
instead of being retained at the binding. The fusion gate now preserves the
producer/admission sequence when the counted destination can fail after fill
success (S7.7.2v2/S7.7.4v2). Both declaration and reassignment continue after
the length mismatch; invalid counts still propagate. Nullable indexed fillers
also retain their destination validation outcome and reach the following code.

Qualification: **168/168** focused tier/GC executions, **275/280** unchanged
MIR emission checks, **351/351** full MIR GC cases and **247/247** diagnostics.
Native optimization checks remain **49/55**, with the same six open failures;
no profile expectation, golden or ratchet budget was relaxed. Evidence:
`temp/error-handling/m3-propagated-fill-order-{focused.log,mir.json,gc.json,diagnostics.json,native.json}`.

### Native profile caller corrections (2026-11-03)

S7.7.2v2/S7.6.3v3: the nullable-float NaN fixture now admits its subtraction
into `checked_zero`, then explicitly propagates before binding the clean float
used by the packed literal. This preserves its former early-exit intent for
an invalid read. The original golden passes **8/8** tier/GC executions, and
the existing MIR emission and exact allocation/fallback counters pass.

The field-guard profile test also used the old `tree.value` byte offset 160.
Adding `^` to `total`'s return contract moved that source site to 161. The
assertion now addresses the current site, keeping the same attempt/fast counts
31 and 1; no counter budget or fast-path requirement changed. Both focused
native tests pass. The rebuilt aggregate's native optimization gate now passes
**51/55**, with four separate admission/COW/string-proof failures still open.
Evidence: `m3-profile-caller-{tiers.json,emission.log,native.json}` and
`m3-qualified-fill-lambda/test_lambda_opt_gtest_results.json`.

### Integer outcome proofs and aggregate checkpoint (2026-11-03)

S7.4.2v3/S7.9.3: an expression the existing integer-join lowering proves to
produce only int or error needs no repeated admission into exactly `int^`.
The shared plain-outcome proof now handles this alongside broad array/map
membership, reusing its conditional/sequence traversal. Other normal numeric
domains and particular-error destinations retain their checks; this does not
choose the pending numeric-union conversion policy. The branch walk now keeps
bare literal primaries, which previously disappeared when unwrapped as if they
were empty expression wrappers. Recursive union traversal consequently no
longer re-admits each integer result: its existing `<40` admission bound passes.

The allocating-forward-call fixture loses three root slots used only by the
removed admission. Its check now pins the resulting two-slot frame and adds
the actual string save/call/reload/length sequence, while forbidding redundant
return admission. The allocating call and error join remain. This updates a
changed frame shape while directly strengthening the live-string invariant
(D6.3.1–D6.3.2), rather than retaining unused roots to match an old slot count.

The immediately preceding rebuilt aggregate (commit `82a5ae590`) completed
**6,431/6,446**, including main **1,998/2,000**, errors **321/321**, REPL
**50/50**, full MIR GC **351/351** and HTTP **152/152**. The 15 failures were
five MIR checks, four native checks, four ratchets, the pending vendor MIR
optimizer defect and the existing MVP alias difference. Required input suites
pass **2,112/2,112**. Evidence: `m3-qualified-fill-lambda.log`, its per-suite
results directory and `m3-qualified-fill-input-{wpt,markdown,yaml,ascii,math}.log`.

After the integer proof: **276/280** MIR emission checks, **52/55** native
optimization checks, **168/168** focused tier/GC executions, **351/351** MIR GC
and **247/247** diagnostics. Evidence:
`temp/error-handling/m3-int-outcome-final-{mir.json,native.json,focused.log,gc.json,diagnostics.json}`.

### Dynamic union controls, typed reads and loop call carriers (2026-11-03)

S11.4.6v3/D4.2.3: the ANY-bearing refusal control now obtains its union
field from parsed input. The former literal value was completely proven and
safely used descriptor-aware `map_fill` construction, so it no longer exercised
the refusal path. The dynamic control keeps its original five admissions and
five reifications on both tiers. The shared admission regression also checks
the literal's actual union field, a dynamically admitted string member and an
invalid boolean member retained as the receiving binding's error.

The typed nucleotide benchmark now admits a dynamic line read into a checked
string, then explicitly propagates before using that string (S7.7.2v2,
S7.6.3v3). This preserves its normal output and exposes the string fact to the
existing concatenation path: generic joins fall from 177 to 93, within the
unchanged `<100` bound. Native optimization checks pass **54/55**; the COW
move-out regression remains open.

Its forced-GC promotion check exposed a separate dynamic-call descriptor bug.
Loop-only promotion reproduced the failure, while function-only promotion and
execution without promotion passed. A reduced loop showed the dispatcher
returning a boxed int directly into an array index. `emit_dynamic_call_value`
had kept the Item for `^`, then described it as a native scalar; propagation
therefore skipped both its error guard and normal unbox. The descriptor now
records that boxed result until `transpile_call` consumes it (S7.6.3v3,
D2.3.4–D2.3.5). The failure was an invalid index, not lost GC ownership; no
rooting policy or vendor code changed.

`admission_loop_call_outcomes` covers success and post-handoff failure. Its
native gate forces synchronous loop publication at thresholds one and five,
with forced/poison GC and function promotion disabled. It passes, as do
**176/176** focused tier/GC executions, **8/8** benchmark tier/GC executions
(including synchronous promotion), and **276/280** MIR checks. Evidence:
`m3-loop-call-carrier-{handoff.json,focused.log,benchmark.json,mir.json,native.json}`.

The rebuilt aggregate now completes **6,437/6,448**, including main
**2,000/2,002**, diagnostics **321/321**, REPL **50/50**, full MIR GC
**351/351** and HTTP **152/152**. Its 11 failures are four MIR checks, the
COW move-out check, four ratchets, the pending vendor MIR optimizer defect
and the existing MVP alias difference. The new synchronous handoff test and
both new script fixtures pass. Required input suites again pass
**2,112/2,112**. Evidence: `m3-qualified-loop-call-lambda.log`, its per-suite
results directory, and `m3-loop-call-carrier-input-{wpt,markdown,yaml,ascii,math}.log`.

The shrinking-index MIR check's `lt` substring also matched the `blt` branch
used by scalar Item boxing. Its opcode patterns now include the leading tab,
so the check still forbids repeated index comparisons without forbidding the
unrelated int53 boxing guard (D2.4.3/S7.1.3v2). No generated code or bounds
policy changed. The full emission gate passes **277/280**; evidence:
`m3-shrinking-opcode-mir.json`. The preceding aggregate count remains the
observed count above, rather than a synthetic rerun total.

### Nullable record declaration layout (2026-11-03)

S11.4.6v3/D4.2.5: declaration admission now uses the existing
`mir_map_contract_needs_reification` helper, as assignments, arguments and
returns already do. Its former inline gate inspected only the wrapper's
TypeId, so it missed the packed record inside `N?`. A more precise semantic
membership proof consequently skipped the representation boundary too. This
left literal-shaped maps behind named-record readers; it also delayed the
COW fixture's reification until its caller, adding forty copies/share marks.

The new nullable-record layout fixture uses extra leading fields and nested
links to detect incorrect packed offsets for both `let` and `var`. The shared
gate preserves the required admission independently of its proven semantic
membership. It does not change error-union member conversion policy.

Qualification: **184/184** focused tier/GC executions, **55/55** native
optimization checks with original counters, **351/351** full MIR GC cases,
and **277/280** MIR emission checks. Evidence:
`m3-nullable-record-declaration-{focused.log,native.json,gc.json,mir.json}`.

### Float outcome guard qualification (2026-11-03)

S7.7.2v2/S7.6.3v3/D2.5.3: the migrated float-read fixture stores each local
validation outcome, then propagates before using its normal float. Its old
whole-function ban on `it2d` therefore rejected safe normal-arm decoding too.
The MIR check now captures each exact Item, requires its error test and error
return sequence before that Item's decode, and retains all four cold admission
calls. It directly checks the intended safety invariant rather than forbidding
decoding after acknowledgement. The runtime golden now also covers negative
indices, both reads out of bounds, and the left/right reads failing separately;
each must return error rather than a fabricated number.

The expanded matrix passes **192/192** tier/GC executions; the full MIR gate
passes **278/280**. No production code or root policy changed in this check
update. Evidence: `m3-float-outcome-guard-{focused.log,mir.json,full-mir.json}`.

The final authored-source shorthand check found two remaining benchmark
initializers in `julia/micro_common{,2}.ls`. They now spell the admitted outcome
as `any^` instead of `any | error` (S7.4.2v3); both modules compile. Explicit
union fixtures remain unchanged where they test union equivalence or particular
error admission. Evidence: `m3-final-micro_common{,2}-ast.{dump,err}`.

### Rebuilt aggregate and UI review (2026-11-03)

The rebuilt core aggregate completes **6,441/6,449**: main **2,001/2,003**,
native optimization **55/55**, MIR emission **278/280**, MIR GC **351/351**,
runtime diagnostics **321/321**, REPL **50/50**, and HTTP **152/152**.
Required input suites pass **2,112/2,112**. Eight failing test cases remain:
two union-normal-admission optimization checks, four size ratchets, the
multi-fixture tier-parity case, and the previously reproduced MVP alias
difference. The tier-parity case includes a pending vendor MIR optimizer
defect, a boolean propagation regression and two stale interpreter-rejection
expectations; the latter three are addressed below. This is an observed gate,
not a claim that every failure is pre-existing. Evidence:
`m3-final-boundary-lambda.log`, its per-suite results directory, and
`m3-final-boundary-input-{wpt,markdown,yaml,ascii,math}.log`.

The rebuilt map gate passes **20/20**, and the migrated native-view/scene
selection passes **11/11**. A focused review of 90 previously failing UI
scenarios passes **67/90** in the parallel run, plus its three scheduler
checks. Five earlier runtime failures now pass: directory-row click, PGF
navigation, both document-editor splash cases, and Northstar slides. Two
previously recovered scenarios exit 137 in that parallel run; each passes
all **three** fresh isolated reruns. Their kill cause is unestablished.
The other **21** runtime scenarios remain unqualified, including slide
aborts; no Radiant production change or assertion weakening is included.
The home-actions abort reaches a DOM text node with no laid-out text view,
where caret movement requires `ViewText`. Media insertion also newly aborts
relative to the original source/compiler run; the original animations case
already aborts on a different stack. These are recorded separately from the
error-handling core, not described as a green Radiant aggregate. Evidence:
`m3-ui-boundary-review.{json,log}`, `m3-ui-killed-isolated-{1,2,3}.json`,
`m3-final-boundary-{map,native-view}.json`, and the slide differential logs.

### Concrete return admission and boolean outcome carriers (2026-11-03)

S11.4.1v5/S11.4.6v3: a concrete scalar source still crosses the full receiving
return contract. JIT skipped that check for both expression and explicit
returns: `int^ => 1.0` returned a float, and `int^ => 1.5` escaped as a normal
value. T0 respectively admits an integer and produces an error. Expression
returns, explicit returns, boxed slow bodies and return-failure analysis now
share `mir_return_boundary_applies`, reusing the existing deferred-admission
relation and record-reification predicate. The expanded return fixture covers
literal and parameter values, successful conversion and rejected integrality,
including nullable numeric contracts. A matching scalar lane proves identity
only for an unadorned scalar target and admitted nullability; different numeric
kinds, literals, constraints and particular-error contracts remain checked.
This preserves existing union admission while its normal-conversion policy
remains pending; it does not introduce a new numeric ruling.

Qualification caught a boxed-tail carrier error exposed by the stricter gate:
the body already produced an Item, but admission boxed its bits as the old
scalar kind a second time. The boundary now consumes that Item directly.
The existing read-handle fixture checks its ordinary integer result and now
also checks missing record elements yielding return-admission errors. Native
return guards likewise inspect an error tag only on an Item producer.

S7.6.3v3/D2.3.4–D2.3.5: raw boolean system calls box their outcome for `^`.
The producer previously published that register as a native bool because the
post-propagation AST names only its success kind. Propagation consequently
skipped the Item error test. The shared boolean-result helper now returns a
`MirValue` carrying its actual representation; both native string fast paths
and generic system-call lowering consume that descriptor. The existing bool
error-lane fixture additionally covers prefix/suffix propagation, invalid
operands, and typed-string success/false results.

The expanded focused matrix passes **232/232** tier/GC executions, and the
full MIR emission gate returns to **278/280**, with only the two previously
recorded union-normal-admission optimization checks failing. No assertions or
size budgets change. Both map
growth fixtures also match their goldens on interp/JIT/AUTO, with zero
interpreter fallback. Their parity entries now require that successful
execution instead of asserting interpreter rejection. The earlier interpreter
sweep was stopped after the boxed-tail defect was found; it published no
manifests. Final rebuilt aggregate qualification and a new complete sweep
remain required. Manifest updates must come from that one complete sweep.
Evidence: `m3-physical-return-boundary-{focused.log,mir.json}` and the final
gate logs below.

The final rebuilt aggregate completes **6,441/6,449**, including native
optimization **55/55**, MIR emission **278/280**, full MIR GC **351/351**,
diagnostics **321/321**, HTTP **152/152** and main **2,001/2,003**.
The tier-parity case now fails only on the independently reproduced vendor
optimizer defect; boolean propagation, record-field return values and both
map-growth parity entries pass. The other seven failing cases are the two
union-normal-admission optimization checks, four unchanged size budgets and
the earlier MVP alias difference. The map and native-view/scene selections
also pass **20/20** and **11/11**. The graphics selection needs its normal GUI
access; its initial restricted-sandbox launch was stopped and rerun with that
access, with the same assertions. Evidence:
`m3-return-outcomes-final-lambda.log`, its per-suite results directory, and
`m3-return-outcomes-final-{map,native-view-gui}.json`.

Further proof work should review the AST's conservative declaration/block
defect summaries against retained outcomes before changing any size budget
(S7.7.2v2/S7.7.4v2). `ast_block_may_carry_defect` and
`ast_find_defect_origin` still describe deferred local checks as possible
completion sources; MIR's retained-local scans already distinguish them.
An optimization needs a concrete producer/consumer witness and must preserve
parameter/store containment and explicit propagation. This is an investigation
entry point, not a claim that it explains all four remaining budget failures.


### Completed interpreter sweep and forward-boundary repairs (2026-11-03)

The complete `interp-sweep --jobs 3` examined **1,437** scripts: **1,416**
matched, **9** were excluded and **12** mismatched, with no timeouts. Its
failure prevented manifest publication; all three interpreter manifests remain
unchanged. Each mismatch was replayed with the original source/compiler in its
own checkout (S1.6). Eight already diverged there: six chart error-control
fixtures, `type_param_error_short_circuit`, and `proc/mir_mvp_roots`. Four were
new relative to that original pair: value-binder return admission, optional
object snapshots, the full LaTeX corpus, and the separately isolated vendor MIR
loop optimizer defect. The vendor remains unchanged pending explicit approval.
Evidence: `m3-return-outcomes-final-interp-sweep.log`, `temp/sweep_full.tsv`,
and `m4-sweep-mismatch-review.{json,log}`.

Three Lambda-side root causes are repaired:

- **S11.4.8v2/S4.2.2/D7.3.1:** static scalar value-binder selection uses
  the runtime binder's semantic type normalization. A concrete float literal
  had bound `N` to singleton `1.5`, so the newly enforced return boundary
  rejected `increment(1.5) => 2.5`. The shared AST collector already reaches
  embedded binder sites; normalizing there feeds both caller-visible result
  types and MIR keys. Runtime and compiler reuse the promoted
  `lambda_binder_value_type` helper. Explicit type binders retain Type identity,
  and existing container-key planning is retained. A conversion-dependent
  selection uses its bound as a conservative static approximation and cannot
  establish an exact raw key. Scalar raw arguments additionally require full
  value-set inclusion and an already-matching admission kind. The existing
  binder-contract resolver is shared by AST validation and MIR planning.
- **S7.7.4v2/S9.1.2:** a fallible object constructor still owns its successful
  container after its result becomes an Item. Boxed outcomes now reuse the
  shared fresh-container classification, including object literals and `new`.
  The mutation walk also recognizes a procedure method's implicit receiver,
  so a read-only alias optimization cannot span a method write. The optional
  presence snapshot and a simple counter snapshot agree on both tiers.
- **D2.3.4/D7.3.3:** a forward raw target without a native plan can gain native
  parameters when its body is lowered. Such calls now use its stable boxed
  public entry; already lowered bodies and proven native plans retain their
  direct routes. The LaTeX macro scanner had received an Item-tagged length in
  a later native integer parameter and recursed until E308. A macro with a
  parameter count and no body reproduces this in isolation. The new paired
  fixture covers that scanner boundary alongside an ordinary successful macro.

The expanded matrix passes **256/256** executions across interpreter, JIT,
AUTO, immediate promotion, and forced/poison GC. All 14 macros in the reduced
Vietnamese document agree across interpreter/JIT with and without forced GC.
The complete **31-document LaTeX corpus** now matches its golden under JIT
(exit 0), using a frozen final executable. This fixes all three newly found
Lambda-side divergences; it is not a claim that the complete sweep or its
manifests are green. Evidence: `m4-forward-entry-focused.log`,
`m4-macro-vietnamese-forward-entry.json`, and `m4-latex-forward-entry.json`.
Final rebuilt native/aggregate qualification for this checkpoint remains
pending below; no safety assertion, emission budget or interpreter manifest
was relaxed.

The first rebuilt aggregate for these repairs completed **6,441/6,450**:
MIR GC **351/351**, error diagnostics **321/321**, MIR emission **278/280**,
and runtime fixtures **2,002/2,004**. It exposed an overbroad binder-key
normalization in the initial repair: the caller-attribution profile lost its
two distinct array-shape raw variants. Normalization through the complete
binder table is now limited to scalar selections, retaining the original
container-key planning and exact profile assertions. That profile check
passes after the correction. Final aggregate qualification must use the
corrected source, not count the first aggregate as a passing checkpoint.
Evidence: `m4-forward-entry-final-lambda.log` and
`m4-scalar-binder-native-profile.log`.

A separate matrix-binder type-identity probe also found a **pre-existing**
S11.4.8v2 discrepancy: T0 and an intentionally boxed JIT entry select the
admitted matrix contract, while an exact raw entry can report broad `array`.
Both the current and original compiler/source reproduce it. It remains
recorded as binder-planning debt; this scalar fix does not claim complete
container-binder equivalence. The exploratory probe was not added as a new
passing regression fixture. Evidence: `_binder-array-identity-probe.ls` and
its replay under `base-proof-audit/temp/error-handling/`.


A further S7.7.3/S9.1.2 boundary probe retains a snapshot of an error-valued
mutable method receiver, then binds the outcome of its procedure method call.
T0 continues with that error at the receiving binding; JIT returned it from the
enclosing procedure while preparing the shared COW receiver. Method preparation
now bypasses an incoming error, leaving ordinary member/call dispatch to produce
the call's outcome. Successful receivers still detach before the bound method
closure captures them. `error_method_outcomes` retains and inspects the snapshot
after the refused call. Container-store containment is unchanged (S7.7.6v2).
The scalar-only binder correction passes the previously failed
caller-attribution profile check with its exact raw-variant assertions
unchanged. The additional method-outcome change requires a fresh focused run
and rebuilt qualification below.


With the original container-key behavior retained and the method-receiver
preparation corrected, the final focused matrix again passes **256/256**.
The independent error-receiver snapshot probe now returns
`[true, true, true]` on both tiers instead of an unhandled JIT error.
Evidence: `m4-method-snapshot-focused.log` and
`_error-method-snapshot-probe.ls`. Final native runners are rebuilding from
this same source before aggregate qualification.


S11.4.8v2 also requires selecting **after parameter admission**. A final
numeric probe found `float as F` receiving integer `1` selected `int` under JIT,
while T0 correctly selected `float`; `int as I` receiving `1.0` had the inverse
mismatch. Static inference now keeps a conversion-dependent bound as an
approximation, withholds exact raw-key evidence, and leaves admission to the
checked entry. The raw-key planner resolves ordinary/dependent scalar formals
against the invocation's type selections, requires value-set inclusion, and
rejects a kind-changing argument. This covers explicit type arguments as well
as value binders; it does not change the pending error-union conversion policy.
The expanded binder fixture checks both conversions, matching-kind calls,
explicit `identity(float, 1)` / `identity(int, 1.0)`, and repeated binder sites
sharing their required common bound. The independent numeric probe now matches
on both tiers. Fresh focused/native/aggregate qualification is required for
this final binder-inference refinement.


The final binder-inference source passes the expanded **256/256** focused
matrix. Rebuilt aggregate qualification completes **6,442/6,450**, with the
same eight outstanding checks: the two union-policy-dependent emission cases,
four size ratchets, the vendor-loop parity case, and the pre-existing
`proc/mir_mvp_roots` AUTO/T0 mismatch. Native optimization is **55/55**, MIR GC
**351/351**, and error diagnostics **321/321**. Input baselines pass
**2,112/2,112**, map **20/20**, and the selected native view/Scene3d checks
**11/11**. No assertion, size budget, or timeout was relaxed. Evidence:
`m4-admitted-binder-final-focused.log`, `m4-admitted-final-lambda.log`, its
per-suite directory, `m4-admitted-final-input.log`, and
`m4-admitted-final-{map,native-view-gui}.json`. A new full interpreter sweep
completed against this exact implementation: **1,438** scripts, **1,421**
matches, **8** exclusions, **9** mismatches and no timeouts. The previous new
binder/object/LaTeX divergences are resolved. Seven remaining mismatches are
the earlier tail-call/chart cases; the other two are the T0 scalar snapshot
and independently isolated vendor loop defect. Manifest publication remains
blocked by the sweep failure. Evidence: `m4-admitted-final-interp-sweep.log`
and its frozen `m4-admitted-final-sweep.tsv`.

### Tail-call rejection and scalar escape ownership (2026-11-03)

**S7.7.3/D8.2.6:** a tail-call rewrite exits only on its successful backedge.
Its rejected-argument join still produces the call's error outcome. MIR kept
`block_returned` set after opening that join, so enclosing branches discarded
the retained error and returned `null`. The shared admission-guard completion
now restores value-producing flow. A successful record tail rewrite also
records that it forwards its destination, avoiding unused fallback field-copy
code after the rejection check. The first rebuilt MIR gate exposed that missing
destination fact; its existing exact concatenation assertion remains unchanged.
`error_tail_call_outcomes` covers returned/raised errors, expression/procedure
tails, direct/boxed calls, and normal recursion.

**D6.1.3/D6.2.3:** T0 re-homed a tiny float into the callee's number extent
immediately before destroying that extent. A subsequent call reused the home
and changed an earlier snapshot. Return and borrowed-argument publication now
capture payloads through `lambda_item_adopt_scalar_home` before frame teardown,
then call `scalar_storage_read` in the caller's extent. This reuses the complete
scalar ownership protocol and removes the duplicated integer-only switches.
`error_scalar_escape_outcomes` checks deferred validation errors replacing a
tiny-float binding, saved normal/error outcomes, borrowed publication, and small
and full-width signed/unsigned type identity.

The expanded focused matrix passes **272/272** executions; all **68**
interpreter runs report zero fallback. Replaying the nine completed-sweep
mismatches now resolves **eight**; only `tune27_loop_accumulator` still diverges
through the already isolated vendor MIR optimizer defect. No vendor code was
changed. Evidence: `m5-tail-scalar-focused.log`, `m5-focused-current.json`,
`m5-sweep-mismatch-review.{json,log}`. Final qualification must include the
record-destination refinement and is recorded below.

The final record-destination source again passes **272/272** focused runs and
the unchanged renderer emission assertion. Rebuilt aggregate qualification
completes **6,445/6,452**, with runtime fixtures **2,005/2,006**, MIR GC
**351/351**, diagnostics **321/321**, native optimization **55/55**, and MIR
emission **278/280**. The first aggregate had **6,444/6,452**, including the
additional renderer emission failure repaired by the destination fact. The
remaining seven checks are the four size ratchets, the two union-policy-dependent
emission cases, and vendor-loop parity. No assertion, budget or timeout was
relaxed. Evidence: `m5-destination-{focused.log,renderer.json}` and
`m5-destination-final-lambda.log` with its per-suite results directory. A complete
sweep has not yet been rerun after these fixes; the nine-case differential is
not manifest qualification. All three interpreter manifests remain unchanged.

### Admitted scalar success lanes and immediate propagation (2026-11-03)

**S7.6.3v3/S11.4.6v3/D2.3.4–D2.3.5:** after `^`, a deferred immutable
local with a strict plain `int`/`float` contract has an already admitted normal
kind. The carrier oracle and native-return proof share that fact; the emitter
still checks an Item error before unboxing. Error unions, nullable/literal
contracts, mutable locals, captures, module reads and unresolved targets do not
gain that proof. In particular, `float^` retains its current union-member
behavior; the pending normal-admission ruling is not decided here.

**D6.2.1/D7.3.3:** procedure planning proves normal returns independently
of ordinary-error admission, using the existing native error companion. The
legacy blanket `can_raise` rejection blocked this proof. Exact ordinary-union
selection still verifies the actual normal kind. A shared analysis scope
replaces the repeated save/restore sequences and supplies the function context
to that exact-kind proof.

**S7.7.2v2/S7.6.3v3/D8.2.6:** a single immutable scalar binding whose
sole use is the next statement's direct `^` may fuse admission and propagation.
Only a producer-owned matching native kind qualifies. Failure explicitly exits
the function; it cannot take a containing block/store's implicit containment
edge. Intervening effects or another observation retain the ordinary boxed
outcome. Task cleanup keeps the regular unwind path. Deferred cold diagnostics
also retain their original declaration source, fixing a location probe that
reported the function header after moving the check (S7.4.4).

The paired `admitted_scalar_propagation` fixture covers normal/error returns,
direct/boxed calls, numeric conversions, union member identity, captures,
nullable/literal admission, reassignment, int/float array bounds, delayed
effects, error inspection, nested function-level propagation and exact diagnostic
locations. Its MIR sidecar checks native float plus error-companion returns and
preserved Item carriers where required. The expanded matrix passes **280/280**;
the new emission fixture passes and the original libm budget is restored.
Size ratchets are now **17/20**, with cube3d, deltablue and prettier remaining.
Evidence: `m6-final-{focused.log,new-proof.json,ratchet.json,location.out}`.
Final rebuilt aggregate qualification follows below. No budget is changed.
The existing terminal-OOB sidecar now pins both native null predicates to the
values consumed by arithmetic, replacing its boxed-tag/`it2d` sequence. It keeps
the four exact diagnostic calls and runtime negative/left/right-OOB cases, and
additionally forbids boxing and generic unboxing in the native body.

The rebuilt 31-suite aggregate records **6,447/6,454**, including MIR GC
**352/352**, diagnostics **321/321**, native optimization **55/55**, and
runtime fixtures **2,005/2,006**. Its seven failures include the terminal-OOB
sidecar's superseded boxed representation expectation. With that assertion
updated to the native dataflow, the complete MIR emission gate is **279/281**;
the final portable terminal-guard replay also passes. Product source is unchanged
between these runs. The six remaining checks are cube3d/deltablue/prettier size
ratchets, the two pending union-policy emission cases, and vendor-loop parity.
Evidence: `m6-final-lambda.log` and its per-suite results directory,
`m6-final-mir.{json,log}`, and `m6-portable-null-guards.{json,log}`.
The focused **280/280** result includes all 30 assertions in the new fixture,
including the diagnostic-location correction. Complete interpreter-sweep and
manifest qualification remain pending; no vendor code or size budget changed.

### Reviewed emission cost of retained outcomes (2026-11-03)

**S7.7.2v2/S7.7.4v2/S7.7.3/D2.5.3/D7.3.3:** removing the
implicit declaration exit changes the proof available after a checked binding.
A nullable array read admitted against a strict record, for example, now leaves
either the record or an error in its local. The declaration's normal contract
alone no longer permits an unchecked record load or native argument. Caller
migration also exposes ordinary error outcomes previously hidden by the legacy
returned/raised distinction. The remaining three size ratchets were compared
with the preserved original compiler and original sources (`0452aaabc`), then
with the final M6 emitter/source (MIR source blob
`164a6e70f7778ab1b4669e5177696a859b19d59d`, before this budget-only review).

| Probe | Module instructions | Ratcheted body instructions |
|---|---:|---:|
| cube3d | 17,056 → 19,194 | `run_cube`: 9,651 → 11,720 |
| deltablue | 10,882 → 14,374 | `constraint_choose_method`: 1,875 → 3,561 |
| prettier | 21,495 → 21,770 | `print_node`: 4,972 → 5,053 |

- Cube retains the three indexed edge outcomes used to construct coordinate
  vectors, and the successive fallible `mtrans` outcomes. `run_cube` adds nine
  generic index/multiply pairs, error-capable construction for three vectors,
  23 cold validators, and their guards/root joins. Three packed-literal and
  three uninitialized numeric-array constructor calls disappear. Across the
  module, several matrix helpers shrink; `draw_line` grows by 256 instructions
  because its array-fed scalar locals retain failures before explicit `^`.
- Delta's five nullable-slot record bindings retain failures. Its six `stronger`
  calls and one `weaker` call now require 14 checked integer arguments. The
  chosen body adds 26 generic member reads and four generic index calls, keeping
  errors outside raw record storage. Its 18 checked path stores and 18 failure
  reports remain under S7.7.6v2. The extra local error tests and admission joins explain the
  larger body; the old unchecked field operations are not valid substitutes.
- Prettier adds three cold admission calls: the `if_break_doc` default, the
  integer precedence argument, and the module-held hardline separator passed
  to `join_docs`. The rest of the body growth is guards, roots and outcome
  joins. Its forward `print_statement` target uses the stable boxed entry
  rather than an unproved native plan; the number of calls at that site is
  unchanged. No allocation-helper call is added to `print_node`.

**D8.6.1:** these are reviewed costs of the new semantics with the current
conservative proofs, not a claim that every extra instruction is irreducible.
The six exact default-profile ceilings are recorded in `mir_budgets.json`;
zero slack, all other probes, and every frame/root ceiling remain intact.
Unmeasured Windows profiles are unchanged and need native qualification.
Further reductions require valid flow-sensitive receiver/argument proofs;
they cannot restore the old implicit exit or assume an error-capable local
contains its declared normal type.

All three complete benchmarks pass their built-in oracles on both compiler
snapshots. Their output is byte-identical after removing timing lines, including
the complete printer output and checksum. Temporary probes made from the
current benchmark definitions also pass **18/18** executions across interp,
JIT and immediate promotion, each with ordinary and forced/poison GC. They
exercise short vectors, null/missing planner slots without unintended stores,
failed text conversion, rejected precedence and the valid default document.
These are correctness checks using debug hosts; no timing claim is made.
Evidence: `temp/error-handling/m7-emission-review.json`,
`m7-{cube,deltablue,prettier}-review.diff`,
`m7-benchmark-oracles.{json,log}`, and `m7-error-oracles.{json,log}`.
The complete ratchet replay passes **20/20** (`m7-final-ratchet.{json,log}`).
The final authored-source inventory also compiles **4,034/4,080** tracked
non-negative scripts: the same **46** previously audited base failures, with
**no new failures and no timeouts**. This includes all seven added runtime/MIR
fixtures since the preceding complete inventory. Evidence:
`m7-source-compile.{json,log}`. Both checks use the unchanged M6 executable
(SHA-256 `e5cc8e95a03e71676ee15484e6f3e017e7cb4abdbbcf3183d3e50bf4a561cff8`).

<a id="final-independent-qualification-boundary-2026-11-03"></a>

### M7 independent qualification boundary (2026-11-03; superseded by M8 below)

**S1.6/D8.3.1:** the new complete interpreter/JIT sweep covers **1,440**
scripts: **1,431 matches**, **8 exclusions**, **1 mismatch**, **0 timeouts**.
The sole mismatch is `proc/tune27_loop_accumulator.ls`, whose wrong level-2
code generation was independently reproduced without the Lambda runtime.
All other previously observed tier divergences are resolved. The failing
official `make interp-sweep` does not reach manifest refresh; all three
committed interpreter lists remain unchanged. The frozen evidence is
`temp/error-handling/m7-final-interp-sweep.log` and `m7-final-sweep.tsv`.

Together with the unchanged M6 product's complete gates above, the independent
review now leaves these three checks open:

| Check | Required next step |
|---|---|
| `typed_fill_count_boundary` emission | Resolve normal array admission/certification through an error union before selecting destination-aware construction for `native_fill`. |
| `tune24_record_context` emission | Resolve ordinary-union normal reification before extending record-return/context proofs to its fallible `chain`. |
| Runtime tier parity / full-sweep loop case | Obtain AGENTS.md rule 16 approval before fixing the vendor MIR optimizer and recording its auditable patch. |

The first two remain failed assertions, not newly waived expectations. The
pending S11.4.6v3 question is whether `T | error` performs T's normal admission
(including numeric conversion and container reification), or preserves an
already matching normal member. For example, should `float^ = 7` produce a
float or preserve its integer kind? The implementation keeps current runtime
admission while awaiting that ruling. Public validation-expression spelling
also remains an unratified follow-up; no new spelling or builtin was invented.

The vendor approval and union ruling have remained pending across the previous
qualification checkpoints. All currently independent implementation and
qualification work is recorded; completing this plan needs those inputs,
the affected fixes/gates, and a successful full sweep before manifest publication.

### Normal admission ruling and approved MIR repair (2026-11-03)

The user selected ordinary T admission for `T | error` / `T^` and approved the
MIR optimizer fix. **S7.4.2v4/S11.4.6v4/D4.2.5** capture the ruling;
TE-13.4 preserves the retired matching-member alternative and its rationale.
The formal specs receive major version bumps because normal numeric kinds
and container representations can change under the former union behavior.

The runtime projects the normal contract before membership can bypass numeric
conversion, array certification or record reification. The shared exclusion
walk preserves invocation-dependent type references; union normalization must
also retain those references rather than replacing them with their bounds
(S11.4.8v2). The static relation defers a selected type unless its bound proves
rejection. Construction hints and record-result plans reuse normal admission;
the two previously blocked emission checks pass with their assertions intact.
Native scalar return planning admits lossless int53-to-float promotion while
retaining checked narrowing and the independent ordinary-error companion.
Qualification of these coordinated changes is in progress below.

The approved vendor fix addresses the root availability condition in MIR's
address folder. Conventional SSA writes PHI registers on incoming edges;
a move saves their current value. Forwarding that PHI into another block may
read a later iteration's assignment. The old predicate found only single-block
cycles, missing the multi-block loop exposed after GVN. The shared guard now
rejects cross-block PHI forwarding; local PHIs and other stable expressions
still fold. All optimization passes remain enabled (S1.6, D8.2.6).
The vendor delta is recorded in `patches/mir-phi-address-snapshot.patch` and
`lambda/mir/VENDOR.md`, under the user's AGENTS.md rule 16 approval. Exact
upstream-plus-patches verification passes. Standalone original MIR computes
9,990 / 8,991 / 9,990 at levels 0/1, but the old level 2 computes
8,991 / -999 / 8,991. The corrected availability guard restores all three
results with level 2 fully enabled. A 97-line runtime-independent fixture and
native unit cover levels 0–3, both loop copies, continue, handoff and empty input.
Evidence: `temp/error-handling/m8-mir-pass-isolation.json`,
`m8-loop-twice.mir`, `m8-mir-phi-build.log`, and `m8-verify-mir-patches.log`.

#### Reviewed emission cost of normal admission

**D8.6.1:** final module/body counts are cube **19,194 / 11,720**,
DeltaBlue **14,486 / 3,561**, and printer **21,950 / 5,053**. Only the two
measured default-profile module ceilings increase from M7; body and platform
ceilings stay unchanged. No threshold assertion is removed or given slack.

DeltaBlue adds **112** instructions at eight scalar return boundaries:
`constraint_output` +60 (four validators), `constraint_input` +30 (two),
`chain_test` and `projection_test` +11 each. The producers are nullable/error
record accesses or inferred mutable values; semantic numeric membership does
not establish the actual int kind required by S11.4.6v4. Reusing an already
checked local/call contract removes redundant checks in the optional-int and
literal-extent emission fixtures without weakening their assertions.

The printer adds **180** instructions because `RenderResult^` now establishes
the normal packed record layout (D4.2.5). The direct result paths pass fields;
public boxed adapters still materialize maps: `render_parts_at` +92, its boxed
adapter +69, and `render_doc`'s boxed adapter +68; `render_doc` itself shrinks
31 and `render_doc_root` shrinks 18. Eight result-factory calls change to direct
field transport, pending-result work falls, and the root loses one
`heap_calloc_class` call. This is a reviewed code-size tradeoff, with no timing
claim. Exact output/checksum matches the preserved original compiler for all
three benchmarks, and their 18 invalid-input tier/GC probes pass.
Evidence: `m8-emission-review.json`, `m8-benchmark-oracles.json`, and
`m8-error-oracles.json` under `temp/error-handling/`.

#### Qualification environment correction

The first rebuilt 31-suite run inherited `LAMBDA_DISABLE_MIR_CACHE=1`. That
invalidates the cache-hit assertions in the input-cache and JS-script suites;
the input-cache suite passes **17/17** with the override removed. The terminal
suite passes **10/10** normally, but the override exposes the pre-existing
AST-claim/forced-JIT retry loop recorded as **LR01-21**. Its single JIT case
passes in isolation; the full disabled-cache sequence exhausts the native
stack after a prior interpreter import. The bounded LLDB stack identifies
`load_script:2291`, and both conflicting conditions are unchanged from
`0452aaabc`. This unrelated opt-out defect is recorded separately rather than
changing cache tests or the error-handling acceptance boundary. The aggregate
must be qualified with its normal cache policy; fresh subprocess tier/GC and
emission probes retain their explicit cache opt-out.

#### Final caller/proof review

The first M8 source inventory found six new failures. Five imported the jq VM:
its `any?` fields also admit ordinary Lambda errors, while its jq data helpers
deliberately accept error-free values. Preserve nullable/absent fields while
stating that contract as `(any ! error)?` for `vat` and `err`. The final five
modules compile. Source-derived jq probes cover data access, jq try/catch
payloads, path tracking and labels: **4/4 tier/GC runs** pass, each checking all
four cases, under JIT and immediate AUTO promotion. Forced interpreter execution
is explicitly excluded by its existing indexed-assignment planner restriction;
it is not reported as a semantic pass. Evidence: `m8-jq-final-compile.json` and
`m8-jq-payload-final-oracles.json`.

The sixth failure was `proc/type_binder_proc_raw.ls`: the conservative dependent
type relation had lost the proof that an unchanged `B as T` parameter is still
the value that selected this invocation's T (S11.4.8v2). Shared return admission
now recognizes that parameter by its current callable's binding identity and
refuses the proof after writes or for `var` parameters. A slot number alone is
never compared across invocation domains. This restores the existing raw-binder
fixture without widening its clean return annotation or removing any check.
The final focused matrix passes **288/288**, including generic and selected
normal conversion, branch returns, narrowed values and recoverable record-call
errors. The final whole-source audit compiles **4,035/4,081**, with exactly the
same **46 reviewed failures**, **zero new failures** and **zero timeouts**;
the nullable jq annotation's final form is separately rechecked in all five
importers. Evidence: `m8-final-focused.log`, `m8-source-compile.json`,
`m8-final-source-compile.log`, and `m8-binder-proc-fixed.log`.

The source recovery gate also found two stale allowlist entries after earlier
runtime extraction. Review confirms that `script_run.cpp` carries the script
execution/module boundaries and `activation.cpp` carries activation-root and
base-stack execution boundaries, all under S7.11/D6.3.6. Adding these files
restores the intended closed allowlist; no ordinary error implementation gains
an unwind boundary. `make check-error-recovery` passes. The Test262 baseline
passes **40,261/40,261** against the repaired MIR with its existing manifest and
runner unchanged. Its concurrent preflight during the first Lambda aggregate
correctly refused to share scratch output, so the final aggregate runs after
that baseline has finished.

The first final three-worker parity sweep was externally terminated after
750/1,441 completions. It produced no complete classification and did not
refresh any manifest. A fresh official `make interp-sweep ARGS='--jobs 6'`
runs after the aggregate gates finish, with the host and script sources fixed.
Its full result, rather than the interrupted prefix, is required for completion.
Evidence: `m8-final-interp-sweep.log` and `m8-final-interp-sweep-rerun.log`.

### Historical design-document implementation status (M1–M7; superseded)

The following notes are relocated from the design document's Appendix A.
They preserve the earlier carrier/proof investigations, counts and pending
items as history; the subsequent M8 record supersedes their implementation
status. In particular, the user settled normal admission through error unions
(S7.4.2v4/S11.4.6v4), approved the MIR repair, and the final aggregate gates pass.
The complete final parity sweep remains incomplete.

> The phased work and qualification criteria are recorded in
> [Type and Error Handling Implementation Plan](<Lambda_Impl_Type_Error_Handling (done).md>).
> M1 completes handler forms/results. M2/M3 implement core admission and
> ordinary outcomes; package/demo migration and aggregate qualification remain
> in progress. It includes the requested `T | error` to `T^`
> binding-annotation cleanup, continued whole-variable reassignment outcomes,
> migration of callers that relied on skips or retained old variable values, and
> success-arm and typed-caret retirement, ordinary-error unification, combined fallback, filtered
> handlers/chaining, explicit classification, and caller migration.
>
> ### Handler convenience status
>
> **2026-11-02, M1 implemented.** Production/reference parsing, shared AST,
> interpreter, and MIR support `or^`, named/qualified filters, and left-associative
> chains. Runtime selection and conservative residual types are qualified by the
> M1 tests. Explicit `is (error | null)` classification uses the existing operator and
> acknowledges its subject under the M2/M3 receiver checks.
> S7.5.7 needs no keyword implementation: `empty` is an ordinary user alias.
> See the [M1 record](<Lambda_Impl_Type_Error_Handling (done).md#m1-handler-outcomes>).
>
> ### Typed-caret retirement
>
> **2026-11-02, core implemented.** Production and reference type grammars reject
> `T^E` with migration guidance. Ordinary unions supply the particular error
> alternatives, with broad `T^` retained. Four authored broad contracts were
> migrated; the parser gate passes C 461/461 and Tree-sitter 448/448. General
> package/demo qualification is still in progress. See the
> [M2/M3 record](<Lambda_Impl_Type_Error_Handling (done).md#m2m3--admission-and-ordinary-outcomes-in-progress-2026-11-02>).
>
> ### Ordinary-error unification
>
> **2026-11-02, core implemented; aggregate qualification pending.** Return
> normalization derives error/ABI metadata from the ordinary outcome contract.
> The acknowledgement walk tracks producers through preserving expressions to
> actual receivers, including inferred bindings and collected Item slots.
> Procedural result replacement rejects an unacknowledged overwritten outcome.
> The non-error default of an unannotated `fn` is preserved. S11.4.6v3's base-only
> constraint admission is also preserved; predicate enforcement is not added by
> this change. Current package migration and remaining qualification are in the
> [M2/M3 record](<Lambda_Impl_Type_Error_Handling (done).md#m2m3--admission-and-ordinary-outcomes-in-progress-2026-11-02>).
>
> ### Success-arm retirement
>
> **2026-11-02, M1 implemented.** Both parsers reject the old suffix, even across
> lines; the AST and both execution tiers have no `value_body` storage or lowering.
> 510 authored uses were migrated to one-handler expressions, error checks, or
> explicit branching. Parser conformance and existing handler goldens qualify
> single evaluation, result preservation, and restored lexical subjects.
> See the [M1 record](<Lambda_Impl_Type_Error_Handling (done).md#m1-handler-outcomes>).
>
> ### Validation and reassignment outcomes
>
> **2026-11-02, core implemented; caller migration and qualification in progress.**
> S7.7.2v2/S7.7.4v2 checks now bind/store their admitted value or validation
> error and continue. Declared targets remain separate from effective outcome
> facts. Captures, loop back edges, borrow publication and native-lane planning
> use those facts; an error never becomes an integer merely because its
> initializer or target had an integer lane. Field/element stores retain their
> own containment. No public validation-expression spelling is added.
>
> **2026-11-03 follow-up (S11.4.1v5/S7.5.4):** a deferred return check
> contributes its possible rich validation error to the call's effective outcome.
> The successful-value admission target remains strict. Inferred/error-admitting
> receivers retain that outcome; a known fallible call cannot initialize a clean
> receiver. JIT pair forwarding must preserve return admission. This does not
> make an unused interior failed binding a returned error, or treat an open
> recursive inference as an ordinary-error producer.
>
> View state setup is part of the view's pure activation (S12.1.3v2). An explicit
> postfix propagation in a state initializer uses that view's error-admitting
> return contract (S7.6.3v3), and exits before rendering the body. A clean return
> contract still rejects it. The propagated outcome crosses the same return
> admission as a body result. This does not change state-slot receiving rules.
>
> **Historical carrier qualification (2026-11-03, S7.8.1/D2.3.4–D2.3.5; superseded by M8):** a propagated
> call consumes its producer's actual Item/native representation. A native tail
> join applies the callable's return admission before opening a success lane;
> an ordinary error is never decoded as an integer or pointer. The full MIR GC
> corpus passes 351/351; aggregate and remaining caller/native-proof qualification
> remain open in the implementation record. Scalar error-union admission reuses
> identity proofs without dropping the native error channel. At that checkpoint, interpreter union admission
> retained an already-conforming numeric member, while plain numeric contracts
> performed their required conversion. The user subsequently ruled that error
> unions apply T's normal admission (S7.4.2v4/S11.4.6v4). Native return selection
> therefore requires the actual normal carrier across all returns and branches;
> a success-type name alone is insufficient. The qualified MIR emission count
> is 278/280; remaining checks are recorded without relaxing safety requirements.
> S7.7.2v2 constructor qualification also covers empty counted arrays, nullable
> float literal members and dynamic record fields: optimized admission retains
> the error at the receiving binding and continues. The constructor shares the
> call-admission join without changing field evaluation or procedural store
> containment. The expanded tier/GC matrix and full MIR GC corpus pass.
> S7.6.3v3/S7.7.2v2 counted fill qualification keeps producer propagation before
> destination validation: `let a: int[2] = fill(n, 1)^` retains a length mismatch
> at `a` and continues, while an invalid fill count propagates. Uncounted numeric
> literal fill success has a static membership proof; runtime storage admission
> is still required. The focused tier/GC matrix passes 256/256.
> S7.6.3v3/D2.3.4 dynamic dispatch preserves its boxed outcome until propagation
> checks it, including when a loop continuation calls an existing module
> definition. The deterministic handoff regression checks both normal values
> and errors under forced/poison GC (D8.3.2).
> S11.4.6v3/D4.2.5 declarations use the same named-record reification gate
> as assignments, arguments and returns, including a record inside `N?`.
> Semantic membership does not establish its packed offsets. The native
> optimization gate passes 55/55 with its original copy/share budgets.
> S11.4.1v5/S11.4.6v3 concrete return values still require full admission when
> their numeric kind differs from the target: `int^ => 1.0` admits an integer,
> while `int^ => 1.5` produces an error under current runtime admission. The
> shared return gate keeps matching scalar lanes and nullable outcomes without
> revalidation. Boxed tails enter that gate as Items, avoiding a second boxing.
> S7.6.3v3 boolean system-call producers also publish whether they boxed their
> outcome before `^`; the normal bool type does not describe that earlier
> physical representation. These fixes retain the pending union-policy question.
> S11.4.8v2/S4.2.2/D7.3.1 exact raw scalar value-binder keys use the same semantic type as
> runtime binding, rather than retaining a singleton literal. Selection follows
> parameter admission: conversion-dependent static inference retains an
> approximation, and scalar raw entries require full membership and the admitted
> kind before bypassing the checked entry. S7.7.3/S9.1.2 method preparation also
> keeps an incoming error as the call's outcome when its receiver has a snapshot.
> S7.7.4v2/S9.1.2 boxed constructor outcomes also retain successful container ownership and
> snapshots across procedure-method writes. D7.3.3 forward calls whose native
> plan is not established use the stable public Item entry; a later native
> specialization cannot reinterpret tagged arguments. The full LaTeX corpus
> now matches its JIT golden after this boundary repair. A complete interpreter
> sweep still reports legacy and vendor divergences and has not published new
> manifests; detailed comparisons and qualification limits are in the
> [implementation record](<Lambda_Impl_Type_Error_Handling (done).md>).
> S7.7.3/D8.2.6 tail-call rejection retains its ordinary error outcome even when
> the successful path becomes a backedge. D6.1.3/D6.2.3 interpreter publication
> also captures wide scalar payloads before frame teardown, preserving snapshots
> when a later outcome replaces the variable. Qualification and the remaining
> vendor divergence are tracked in the
> [tail-call and ownership record](<Lambda_Impl_Type_Error_Handling (done).md#tail-call-rejection-and-scalar-escape-ownership-2026-11-03>).
> S7.6.3v3/S11.4.6v3 validated plain scalar locals also retain their proven
> normal kind after propagation. D6.2.1 procedure planning keeps the ordinary
> error companion independently of that success lane. An immediate, sole `^`
> may fuse a local's check without skipping intervening user code or changing its
> error location (S7.7.2v2/S7.4.4); other observations retain the boxed outcome.
> The [scalar qualification record](<Lambda_Impl_Type_Error_Handling (done).md#admitted-scalar-success-lanes-and-immediate-propagation-2026-11-03>)
> records the restored libm budget and remaining aggregate work.
> D8.6.1 also requires making intended emission growth explicit. The remaining
> cube/delta/printer comparisons retain validation outcomes and checked argument
> entry rather than relying on the retired declaration exit. Their reviewed
> zero-slack budgets pass 20/20; benchmark output matches the original compiler,
> and invalid-input tier/GC probes pass. The
> [emission review](<Lambda_Impl_Type_Error_Handling (done).md#reviewed-emission-cost-of-retained-outcomes-2026-11-03>)
> records the measured cost, conservative proof limits and qualification scope.
> The final complete tier sweep has 1,431 matches, eight exclusions and one
> vendor-loop mismatch among 1,440 scripts, without timeouts (S1.6). That mismatch
> prevents manifest publication. At that M7 checkpoint, the two ordinary-union admission proof checks
> awaited the ruling now settled by TE-13.4; the
> [final qualification boundary](<Lambda_Impl_Type_Error_Handling (done).md#final-independent-qualification-boundary-2026-11-03>)
> names the exact checks and required inputs. No assertion is waived.
>
> Return-admission inference publishes resolved declared results before a body
> checks recursive calls (S11.4.1v5). Closed module type results are also available
> to forward calls; parameter defaults and dependent return slots retain their
> declaration scope. A functional branch block with one produced result keeps
> that result's admission proof through nested branches (S2.5.4v3); its open
> execution carrier does not by itself introduce a validation failure. Equality
> against `null` retains direct error propagation without inventing a traversal
> of nested container errors (S5.1.1v2/S5.1.2v2).
>
> ### Initializer acknowledgement
>
> **2026-11-02, core implemented.** Inferred receivers acknowledge their complete
> outcomes; `any` admits error without an acknowledgement warning. E241 reports
> undecidable explicit local admission, retaining the runtime check and its
> failure outcome. A known incompatible source remains a static error. The same
> rule applies to whole-variable reassignment. Focused tier/GC and diagnostic
> qualification is recorded in the
> [M2/M3 record](<Lambda_Impl_Type_Error_Handling (done).md#m2m3--admission-and-ordinary-outcomes-in-progress-2026-11-02>).
>
> ### Handler results
>
> **2026-11-02, M1 implemented.** Procedure handlers preserve operand/recovery
> values in bindings and as final results, including suspension. `null` is a real
> replacement result; declarations still contribute no result. Interpreter and
> MIR use the same sequencing classification. Normal-result erasure and implicit
> return of fresh recovery errors were removed while preserving procedure fault
> protection. Focused tests cover explicit return, chained recovery, nested
> subjects, and forced GC. See the
> [M1 record](<Lambda_Impl_Type_Error_Handling (done).md#m1-handler-outcomes>).
>

### M8 qualification boundary (2026-11-03)

| Gate | Result |
|---|---|
| Lambda baseline aggregate | **6,456/6,456**, including **2,007/2,007** runtime cases |
| Diagnostics / native admission | **321/321** / **55/55** |
| Focused tier/GC matrix | **288/288** runs |
| MIR emission / size ratchets / forced-GC corpus | **282/282** / **20/20** / **352/352** |
| Required input baselines | **2,112/2,112** |
| Test262 baseline | **40,261/40,261**, existing runner and manifest unchanged |
| Whole authored-source compile audit | **4,035/4,081**, same **46** reviewed failures; no new failures or timeouts |
| Full parity / interpreter manifests | **Skipped by USER, 2026-11-03: no final parity claim; all three lists unchanged** |

The host is fixed at SHA256
`2b931d892ca69e2d941af420a175e81683b2722f22d30bc7fb7f6e5d5d0d6f1d`
(`temp/error-handling/m8-final-product.json`). The complete aggregate evidence
is `m8-final-lambda-baseline/test_summary.json`; the independent Test262
baseline and input evidence are `m8-test262-baseline.log` and
`m8-input-baseline.json`. Full-runtime, emission, admission and GC checks all
pass with their safety assertions intact.

Repeated full-sweep interruptions are not test verdicts. A diagnostic-only
Python signal observer recorded receiver **29987**, sender **35412**, UID
**501**, signal **SIGTERM**, `si_code=0`. The sender exited before process
inspection. The observer preserves the prior/default termination behavior;
neither product code nor test assertions/timeouts are modified. The monitored
make job also terminated, and no incomplete sweep is used to regenerate lists.
Evidence: `signal-probe/senders.txt`, `m8-probe-sweep-status.json`,
`m8-signal-probe-interp-sweep.log`, and the preceding monitored status/log.
The user subsequently waived the complete official sweep and three-list
refresh on 2026-11-03 and authorized merge and worktree removal. The interrupted
attempts are retained as evidence, not passing results. Standalone validation
spelling and general cleanup remain unratified questions outside this plan.


**Documentation and patch cleanup (2026-11-03):** current design/reference status
now reflects M8, with the complete parity sweep and three-list refresh still
incomplete. Earlier implementation notes are retained above as history, with
relocated links rebased and old milestone anchors preserved. No ruling changed.
The MIR patch was regenerated with Git's `diff.suppressBlankEmpty=true`, removing
only blank context-line spaces. `make verify-mir-patches` confirms the exact
upstream-plus-patches invariant; all 45 local links/anchors and whitespace checks
pass. `make check-formal-index` passes with its pre-existing duplicate DO25
warning. The host, vendored MIR source and interpreter manifests are unchanged;
no engine rebuild or regression rerun was needed for this cleanup. Evidence:
`temp/error-handling/doc-cleanup-{checks.json,verify-mir-patches.log}`.


**User acceptance (2026-11-03):** skip the parity check; merge these changes to
remote master and delete the implementation worktree. This waives only the final
parity sweep and its three-manifest refresh. The passing gates above retain
their recorded scope; no partial sweep is promoted to a passing result.

## Production integration and user acceptance (2026-11-03)

The merge preserves master's function-local control/capture homes and pure
benchmark migration. Completed `fn` bodies validate explicit exits without an
invented collected fallthrough (S12.1.2v3). The shared MIR exit walk retains
independent success/error carriers and exact-kind proofs (D6.2.1).
Binding outcomes initialize shared local capture homes without declaration
exits (S7.7.2v2); the paired `fn_local_error_outcomes` regression covers them.

Caller migration includes the newly published chat/file-picker, scene audio
and pure benchmark helpers. Fallible returns disclose outcomes; dynamic guards
and IO explicitly propagate. Best-effort chat notification helpers accept
errors and recover with a clean `null` contract. UI fallbacks use handlers and
`or^`. No new syntax is introduced.

Master advanced from `88d30f849` to `1b0927e30` during qualification. The latter's
MVP implementation/additions and shared error-index metadata fix are preserved.
Two older admission fixtures had treated indexing an error as absence; they
now assert error preservation under S7.9.3 and S7.1.1v3's non-error-base
qualification. Their expected output files are unchanged. USER deferred
experimental MVP adaptation (LR01-22); it is not qualified here.

| Production check | Result |
|---|---|
| Lambda baseline after latest master | 6,594/6,594 across 32 suites; runtime 2,039/2,039, diagnostics 324/324, MIR 285/285, ratchets 20/20, MIR GC 355/355 |
| Required input baselines | 2,112/2,112 |
| Test262 baseline | 40,261/40,261 on the final merged host |
| Focused shared-home/normal admission | 12/12 exact-output runs on the latest host, interp/JIT/AUTO with normal and forced/poisoned GC |
| Benchmark output oracles | Cube, DeltaBlue and printer match original output excluding timing; no performance claim |
| Current authored-source inventory | 4,084/4,141 compile; 46 reviewed legacy failures and 11 deferred MVP fixtures; zero unexpected failures |
| Full parity and three-manifest refresh | Skipped by USER; manifests unchanged; no final parity pass claimed |

D8.6.1 emission review combines both changes. Cube's module shrinks
19,194→19,072; its body gains exactly 12 join MOVs (11,720→11,732), with no new
calls. DeltaBlue's module grows 14,486→14,646: pure callees expose borrowed
aliases, requiring 3/2/5 guarded COW/boundary paths in
`constraint_execute`/`constraint_recalc`/`incremental_remove` (+84/+54/+122).
Five wrappers shrink by 17 each and the empty module collector by 15.
`choose_method` and all frame/root ceilings remain unchanged. Exact default
caps record the measured cost; other profiles are untouched.

Evidence is preserved outside the worktree under
`temp/error-handling-merge-20261103T054805Z/`. `qualification-evidence.tar.gz`
contains the earlier `temp/error-handling/` records; final native reports,
source inventories, MIR diffs, host hashes and focused/oracle results accompany
it. Standalone validation syntax and general cleanup remain separate questions.

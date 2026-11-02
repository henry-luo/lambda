# Lambda — Type and Error Handling Implementation Plan

**Status:** M1 handlers and M2/M3 core admission/outcome changes are implemented
in `codex/error-handling-20261102`. Package/demo migration and aggregate
qualification are in progress; the full P0–P5 plan is not complete.
**Date:** 2026-11-02.
**Design:** [Type and Error Handling](../Lambda_Design_Type_Error_Handling.md),
TE-13.1–TE-13.3, TE-16.1–TE-16.4, and TE-18.1–TE-18.2.
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
| Unified ordinary errors | S7.4.1v2–S7.4.2v3, S7.4.5v2–S7.4.6v2, S11.4.2v3, S12.3.4v2, D5.2.1v5 | One ordinary-error discipline; equivalent return spellings; `raise` returns an admitted error early. |
| Acknowledgement | S7.5.1v6–S7.5.5 | Inferred initializers and whole-variable reassignments acknowledge; `any` and `any \| error` agree; unresolved explicit admission warns. Error-preserving composition carries outcomes to a receiver; unacknowledged fallible results cannot be overwritten. |
| Deferred validation | S7.7.1v3, S7.7.2v2, S7.7.5v3, S7.7.7v4, S11.4.1v5, S11.4.6v3, S11.4.7v3 | A deferred initializer binds the admitted value or rich validation error and continues; post-binding recovery is reachable. |
| Whole-variable reassignment | S7.5.2v4, S7.7.4v2–S7.7.6v2, S12.2.1v3 | Same admission decision as initialization; store the new outcome, including error, and continue. Never retain a stale old value on deferred failure. |
| Truthful types and storage | SI14v3, S12.2.1v3, D2.8.1, D2.8.3v3, D3.2.2v3, D3.2.3v3 | Keep the declared admission target separate from the effective outcome type; an error never enters a native value slot. |
| Handler results and surface | S7.6.1v8, S7.6.2v5, S7.6.6v6, S7.6.7v5, S10.1.7v4, S16.4.3v3 | The single error handler preserves results for `fn` and `pn`, including suspension. Reject and migrate the optional success arm; do not reinterpret it as cleanup. |
| Procedural sequences | S2.5.3v2, S2.5.4v3, S12.1.2v2 | Produced values replace earlier results, including `null`; declarations produce no result; explicit `return` exits the procedure. |
| Binding shorthand | S7.4.2v3 | `let`/`var` annotations `T^` and `T \| error` are equivalent; return spellings now have identical semantics too. |
| Particular error outcomes | S7.4.2v3, S11.4.2v3, D5.2.1v5 | Remove `T^E`; ordinary unions specify admitted error alternatives with no implicit broad ordinary errors. Derive type/ABI facts from the normalized contract. |
| Handler convenience | S7.6.8–S7.6.9, S7.5.6, S7.6.2v5, S7.6.5v4–S7.6.6v6, S10.1.7v4, S16.2.2v3, S16.2.6v2, S16.4.3v3 | `or^` covers errors/falsy values once; filtered handlers pass unmatched errors through and chains see fresh recovery errors. Explicit error-or-null classification acknowledges. |

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
  or early cleanup of a still-live block is permitted (D2.8.1, D5.1.4).
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
  `doc/Lambda_Type.md`, `doc/Lambda_Procedural.md`, and relevant `doc/dev/lambda`
  chapters. Keep historical completed implementation plans historical.
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
| P0 | Pending | Starting behavior, fixture set, caller and binding inventories |
| P1 | Pending | Normalized outcome types, admission, composition, overwritten-result checks |
| P2 | Pending | Continued validation, uniform error returns, safe representation on all tiers |
| P3 | Pending | Handler/fallback parsing, residual errors and chaining, uniform results and suspension |
| P4 | Pending | Success-arm/caller migration and binding shorthand diff |
| P5 | Pending | Aggregate gates, updated documentation, explicit residue |


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

## M2/M3 — Admission and ordinary outcomes (in progress, 2026-11-02)

**Authority:** S7.4.2v3, S7.5.1v6–S7.5.5, S7.7.2v2/S7.7.4v2,
S7.8.1–S7.8.2, SI14v3, D3.2.3v3, D5.2.1v5. This is an implementation
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
errors escape need to
recover them or declare the error outcome. Loop-source/predicate migrations
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
when its outcome normalizes to `any` (S7.6.1v8/S7.6.9).

S7.9.3 conversions preserve incoming errors, including `u64` and sized numeric
conversions. Conversion targets come from the callee rather than the widened
outcome type. Event handlers validate results against their own procedure
activation, while an earlier overwritten outcome still needs acknowledgement.
Immutable aliases and captures retain errors exposed by loop back edges
(SI14v3); these facts also prevent unsafe JIT unboxing at later calls.

The expanded diagnostic gate passes **235/235** cases. The focused matrix has
**88 executions** across interpreter, JIT, automatic and immediate-promotion
modes, each with ordinary and forced/poisoned GC. Final qualification of the
latest admission-equivalence case and the package/demo migration is still in
progress. Current package edits disclose existing error outcomes and make loop
source/predicate recovery explicit; they are not yet aggregate-qualified.
